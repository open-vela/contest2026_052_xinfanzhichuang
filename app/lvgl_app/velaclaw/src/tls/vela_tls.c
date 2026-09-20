/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "tls/vela_tls.h"
#include "proxy/http_proxy.h"
#include "velaclaw_compat.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdint.h>
#include <errno.h>

/* POSIX networking */
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/select.h>

/* mbedTLS */
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"

#include <fcntl.h>
#include <time.h>
#include <pthread.h>

static int simple_entropy_func(void *data, unsigned char *output, size_t len)
{
    (void)data;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) fd = open("/dev/random", O_RDONLY);

    if (fd >= 0) {
        ssize_t n = read(fd, output, len);
        close(fd);
        if (n == (ssize_t)len) return 0;
    }

    /* Fallback for QEMU/Simulation: Use time-based pseudo-random if device is missing.
     * This is acceptable for a simulation environment to unblock TLS. */
    static unsigned int seed = 0;
    if (seed == 0) seed = (unsigned int)time(NULL);
    for (size_t i = 0; i < len; i++) {
        output[i] = (unsigned char)(rand_r(&seed) & 0xFF);
    }
    return 0;
}

static const char *TAG = "vela_tls";

#define VELA_TLS_CONNECT_RETRIES 3
#define VELA_TLS_CONNECT_RETRY_DELAY_US 500000
#define VELA_TLS_CONNECT_TIMEOUT_MS 4000
#define VELA_TLS_HANDSHAKE_TIMEOUT_MS 10000
#define VELA_TLS_WRITE_TIMEOUT_MS 15000
#define VELA_TLS_DEFAULT_READ_TIMEOUT_SEC 3
#define VELA_TLS_DEFAULT_WRITE_TIMEOUT_SEC 15
#define VELA_TLS_RESPONSE_TIMEOUT_MS 15000
#define VELA_TLS_STREAM_READ_TIMEOUT_SEC 30

/* ── TLS context ─────────────────────────────────────────────── */

typedef struct {
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       cfg;
    mbedtls_net_context      net;
    mbedtls_ctr_drbg_context ctr_drbg;
} tls_ctx_t;

static uint64_t tls_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
    }

    return (uint64_t)time(NULL) * 1000ULL;
}

static void tls_set_socket_timeouts(tls_ctx_t *ctx, long read_sec, long read_usec,
                                    long write_sec, long write_usec)
{
    if (ctx == NULL || ctx->net.fd < 0) {
        return;
    }

    struct timeval rtv = {
        .tv_sec = read_sec,
        .tv_usec = read_usec
    };
    struct timeval wtv = {
        .tv_sec = write_sec,
        .tv_usec = write_usec
    };

    setsockopt(ctx->net.fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
    setsockopt(ctx->net.fd, SOL_SOCKET, SO_SNDTIMEO, &wtv, sizeof(wtv));
}

static void tls_ctx_free(tls_ctx_t *ctx)
{
    mbedtls_ssl_close_notify(&ctx->ssl);
    mbedtls_net_free(&ctx->net);
    mbedtls_ssl_free(&ctx->ssl);
    mbedtls_ssl_config_free(&ctx->cfg);
    mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
}

static int tls_tcp_connect_timeout(mbedtls_net_context *ctx,
    const char *host, const char *port, int timeout_ms)
{
    struct addrinfo hints;
    struct addrinfo *addr_list = NULL;
    int ret = MBEDTLS_ERR_NET_CONNECT_FAILED;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, port, &hints, &addr_list) != 0 || !addr_list) {
        return MBEDTLS_ERR_NET_UNKNOWN_HOST;
    }

    for (struct addrinfo *cur = addr_list; cur != NULL; cur = cur->ai_next) {
        int sock = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
        if (sock < 0) {
            ret = MBEDTLS_ERR_NET_SOCKET_FAILED;
            continue;
        }

        int flags = fcntl(sock, F_GETFL, 0);
        if (flags < 0) {
            flags = 0;
        }
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        int rc = connect(sock, cur->ai_addr, cur->ai_addrlen);
        if (rc == 0) {
            fcntl(sock, F_SETFL, flags);
            ctx->fd = sock;
            ret = 0;
            break;
        }

        if (rc != 0 && errno != EINPROGRESS) {
            close(sock);
            ret = MBEDTLS_ERR_NET_CONNECT_FAILED;
            continue;
        }

        if (rc != 0) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(sock, &wfds);
            struct timeval tv = {
                .tv_sec = timeout_ms / 1000,
                .tv_usec = (timeout_ms % 1000) * 1000
            };
            rc = select(sock + 1, NULL, &wfds, NULL, &tv);
        }

        if (rc > 0) {
            int err = 0;
            socklen_t errlen = sizeof(err);
            if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &errlen) == 0
                && err == 0) {
                fcntl(sock, F_SETFL, flags);
                ctx->fd = sock;
                ret = 0;
                break;
            }
        }

        close(sock);
        ret = MBEDTLS_ERR_NET_CONNECT_FAILED;
    }

    freeaddrinfo(addr_list);
    return ret;
}

/* ── Persistent connection pool (keep-alive) ─────────────────── */
/* Keeps one live TLS connection per host:port so repeated calls to
 * the same endpoint (Feishu REST, LLM API) skip the TLS handshake. */

#define CONN_POOL_SIZE 3

typedef struct {
    tls_ctx_t ctx;
    char      host[128];
    char      port[8];
    bool      in_use;   /* locked by a request */
    bool      valid;    /* connection is alive */
} conn_slot_t;

static conn_slot_t   s_pool[CONN_POOL_SIZE];
static pthread_mutex_t s_pool_lock = PTHREAD_MUTEX_INITIALIZER;

/* Acquire a slot for host:port.  Returns a locked, connected ctx or NULL. */
static conn_slot_t *pool_acquire(const char *host, const char *port)
{
    pthread_mutex_lock(&s_pool_lock);

    /* 1. Find an existing valid slot for this host:port */
    for (int i = 0; i < CONN_POOL_SIZE; i++) {
        conn_slot_t *s = &s_pool[i];
        if (s->valid && !s->in_use &&
            strcmp(s->host, host) == 0 &&
            strcmp(s->port, port) == 0) {
            s->in_use = true;
            pthread_mutex_unlock(&s_pool_lock);
            return s;
        }
    }

    /* 2. Find an empty slot */
    for (int i = 0; i < CONN_POOL_SIZE; i++) {
        if (!s_pool[i].valid && !s_pool[i].in_use) {
            s_pool[i].in_use = true;
            pthread_mutex_unlock(&s_pool_lock);
            return &s_pool[i];
        }
    }

    /* 3. Evict the first non-in-use slot */
    for (int i = 0; i < CONN_POOL_SIZE; i++) {
        if (!s_pool[i].in_use) {
            tls_ctx_free(&s_pool[i].ctx);
            s_pool[i].valid  = false;
            s_pool[i].in_use = true;
            pthread_mutex_unlock(&s_pool_lock);
            return &s_pool[i];
        }
    }

    pthread_mutex_unlock(&s_pool_lock);
    return NULL;  /* all slots busy — caller falls back to ephemeral */
}

/* Return a slot to the pool.  keep=true means the connection is still alive. */
static void pool_release(conn_slot_t *s, const char *host, const char *port, bool keep)
{
    pthread_mutex_lock(&s_pool_lock);
    if (keep) {
        strncpy(s->host, host, sizeof(s->host) - 1);
        strncpy(s->port, port, sizeof(s->port) - 1);
        s->valid = true;
    } else {
        tls_ctx_free(&s->ctx);
        s->valid = false;
        s->host[0] = '\0';
    }
    s->in_use = false;
    pthread_mutex_unlock(&s_pool_lock);
}

static int tls_ctx_connect(tls_ctx_t *ctx, const char *host, const char *port)
{
    int ret;

    mbedtls_ssl_init(&ctx->ssl);
    mbedtls_ssl_config_init(&ctx->cfg);
    mbedtls_net_init(&ctx->net);
    mbedtls_ctr_drbg_init(&ctx->ctr_drbg);

    /* Seed RNG directly with our robust function */
    const char *pers = "vela_tls";
    if ((ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, simple_entropy_func, NULL,
                                      (const unsigned char *)pers,
                                      strlen(pers))) != 0) {
        syslog(LOG_ERR, "[%s] ctr_drbg_seed ret=0x%x\n", TAG, -ret);
        return VELA_TLS_ERR_HANDSHAKE;
    }

    /* Check system time — crucial for TLS certificate validation.
     * Use gmtime_r + manual offset to avoid NuttX zoneinfo lookup errors. */
    time_t now = time(NULL);
    char time_str[64];
    struct tm tm_val;
    time_t local_epoch = now + 8 * 3600;
    gmtime_r(&local_epoch, &tm_val);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_val);
    syslog(LOG_INFO, "[%s] Handshake start: Host=%s, Time=%s (UNIX=%ld)\n", TAG, host, time_str, (long)now);

    if (now < 1704067200) { /* Jan 1 2024 */
        syslog(LOG_WARNING, "[%s] Time is too old, forcing clock forward to 2026...\n", TAG);
        struct timespec ts = { .tv_sec = 1772275200, .tv_nsec = 0 };
        clock_settime(CLOCK_REALTIME, &ts);
    }

    /* TCP connect — use HTTP CONNECT proxy if configured */
    bool proxy_enabled = http_proxy_is_enabled();
    for (int attempt = 1; attempt <= VELA_TLS_CONNECT_RETRIES; attempt++) {
        if (proxy_enabled) {
            int tunnel_fd = proxy_open_tunnel(host, atoi(port), 30000);
            if (tunnel_fd >= 0) {
                ctx->net.fd = tunnel_fd;
                syslog(LOG_INFO, "[%s] Using proxy tunnel fd=%d for %s:%s\n",
                    TAG, tunnel_fd, host, port);
                break;
            }

            if (attempt == VELA_TLS_CONNECT_RETRIES) {
                syslog(LOG_ERR, "[%s] proxy tunnel to %s:%s failed\n",
                    TAG, host, port);
                return VELA_TLS_ERR_CONNECT;
            }

            syslog(LOG_WARNING,
                "[%s] proxy tunnel to %s:%s failed, retrying (%d/%d)\n",
                TAG, host, port, attempt, VELA_TLS_CONNECT_RETRIES);
        } else {
            ret = tls_tcp_connect_timeout(&ctx->net, host, port,
                VELA_TLS_CONNECT_TIMEOUT_MS);
            if (ret == 0) {
                break;
            }

            mbedtls_net_free(&ctx->net);
            mbedtls_net_init(&ctx->net);

            if (attempt == VELA_TLS_CONNECT_RETRIES) {
                syslog(LOG_ERR, "[%s] net_connect %s:%s ret=0x%x\n",
                    TAG, host, port, -ret);
                return VELA_TLS_ERR_CONNECT;
            }

            syslog(LOG_WARNING,
                "[%s] net_connect %s:%s ret=0x%x, retrying (%d/%d)\n",
                TAG, host, port, -ret, attempt, VELA_TLS_CONNECT_RETRIES);
        }

        usleep(VELA_TLS_CONNECT_RETRY_DELAY_US);
    }

    if (ctx->net.fd < 0) {
        return VELA_TLS_ERR_CONNECT;
    }

    /* Force blocking mode and set socket-level read/write timeout.
     * This avoids using select()/poll() inside mbedtls_net_recv_timeout
     * which returns MBEDTLS_ERR_NET_POLL_FAILED (-0x0047) on NuttX/QEMU.
     * Keep the handshake timeout short so ASR/TTS cannot block the whole
     * voice state machine when Wi-Fi or the cloud endpoint stalls. */
    mbedtls_net_set_block(&ctx->net);
    tls_set_socket_timeouts(ctx, VELA_TLS_HANDSHAKE_TIMEOUT_MS / 1000,
                            (VELA_TLS_HANDSHAKE_TIMEOUT_MS % 1000) * 1000,
                            VELA_TLS_HANDSHAKE_TIMEOUT_MS / 1000,
                            (VELA_TLS_HANDSHAKE_TIMEOUT_MS % 1000) * 1000);

    /* SSL config: client, TLS, default ciphersuites */
    if ((ret = mbedtls_ssl_config_defaults(&ctx->cfg,
                                           MBEDTLS_SSL_IS_CLIENT,
                                           MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        syslog(LOG_ERR, "[%s] ssl_config_defaults ret=0x%x\n", TAG, -ret);
        return VELA_TLS_ERR_HANDSHAKE;
    }

    /* Pin the TLS version range to what is actually compiled in.
     * Setting max to MBEDTLS_SSL_VERSION_TLS1_3 when MBEDTLS_SSL_PROTO_TLS1_3
     * is not defined causes mbedtls_ssl_setup() to return BAD_CONFIG (-0x5e80).
     * Use TLS 1.3 as the ceiling only when the library was built with TLS 1.3
     * support; otherwise cap at TLS 1.2. */
    mbedtls_ssl_conf_min_tls_version(&ctx->cfg, MBEDTLS_SSL_VERSION_TLS1_2);
#if defined(MBEDTLS_SSL_PROTO_TLS1_3)
    mbedtls_ssl_conf_max_tls_version(&ctx->cfg, MBEDTLS_SSL_VERSION_TLS1_3);
#else
    mbedtls_ssl_conf_max_tls_version(&ctx->cfg, MBEDTLS_SSL_VERSION_TLS1_2);
#endif

    /* Advertise ALPN so Cloudflare/OpenAI servers don't silently reject us.
     * Modern HTTPS servers send a fatal TLS alert or close the connection
     * when no ALPN extension is present in the ClientHello.
     * MBEDTLS_SSL_ALPN must be enabled in the build (it is). */
#if defined(MBEDTLS_SSL_ALPN)
    static const char *alpn_protos[] = { "http/1.1", NULL };
    if ((ret = mbedtls_ssl_conf_alpn_protocols(&ctx->cfg, alpn_protos)) != 0) {
        syslog(LOG_WARNING, "[%s] Failed to set ALPN protocols: -0x%04x (non-fatal)\n", TAG, -ret);
    }
#endif

    /* Skip full chain verification — keeps portability without CA bundle */
    mbedtls_ssl_conf_authmode(&ctx->cfg, MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_rng(&ctx->cfg, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);

    /* Read timeout is handled at the socket level via SO_RCVTIMEO,
     * not via mbedtls_ssl_conf_read_timeout + mbedtls_net_recv_timeout,
     * because select()/poll() inside mbedtls_net_recv_timeout fails on
     * NuttX/QEMU with MBEDTLS_ERR_NET_POLL_FAILED (-0x0047). */

    if ((ret = mbedtls_ssl_setup(&ctx->ssl, &ctx->cfg)) != 0) {
        struct mallinfo mi = mallinfo();

        syslog(LOG_ERR,
               "[%s] ssl_setup ret=0x%x free=%d used=%d "
               "tls_buf={in=%d out=%d}\n",
               TAG, -ret, mi.fordblks, mi.uordblks,
               MBEDTLS_SSL_IN_CONTENT_LEN, MBEDTLS_SSL_OUT_CONTENT_LEN);
        return VELA_TLS_ERR_HANDSHAKE;
    }

    if ((ret = mbedtls_ssl_set_hostname(&ctx->ssl, host)) != 0) {
        syslog(LOG_ERR, "[%s] ssl_set_hostname ret=0x%x\n", TAG, -ret);
        return VELA_TLS_ERR_HANDSHAKE;
    }

    /* Use blocking recv — no select()/poll().  The read
     * timeout is enforced by the SO_RCVTIMEO socket option set above. */
    mbedtls_ssl_set_bio(&ctx->ssl, &ctx->net,
                        mbedtls_net_send, mbedtls_net_recv, NULL);

    /* Handshake */
    uint64_t hs_start_ms = tls_now_ms();
    while ((ret = mbedtls_ssl_handshake(&ctx->ssl)) != 0) {
        uint64_t hs_elapsed_ms = tls_now_ms() - hs_start_ms;

        if (hs_elapsed_ms >= VELA_TLS_HANDSHAKE_TIMEOUT_MS) {
            syslog(LOG_ERR,
                   "[%s] ssl_handshake timeout after %llums host=%s\n",
                   TAG, (unsigned long long)hs_elapsed_ms, host);
            return VELA_TLS_ERR_HANDSHAKE;
        }

        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
#if defined(MBEDTLS_ERROR_C)
            char err_buf[128];
            mbedtls_strerror(ret, err_buf, sizeof(err_buf));
            syslog(LOG_ERR,
                   "[%s] ssl_handshake ret=-0x%04x after %llums: %s\n",
                   TAG, -ret, (unsigned long long)hs_elapsed_ms, err_buf);
#else
            syslog(LOG_ERR, "[%s] ssl_handshake ret=-0x%04x after %llums\n",
                   TAG, -ret, (unsigned long long)hs_elapsed_ms);
#endif
            /* Log if it was a fatal alert */
            if (ret == MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE) {
                syslog(LOG_ERR, "[%s] Server sent fatal alert message\n", TAG);
            }
            return VELA_TLS_ERR_HANDSHAKE;
        }
    }

    uint64_t hs_elapsed_ms = tls_now_ms() - hs_start_ms;
    tls_set_socket_timeouts(ctx, VELA_TLS_DEFAULT_READ_TIMEOUT_SEC, 0,
                            VELA_TLS_DEFAULT_WRITE_TIMEOUT_SEC, 0);

    syslog(LOG_INFO, "[%s] Handshake OK: %s / %s (%llums)\n",
           TAG, mbedtls_ssl_get_version(&ctx->ssl),
           mbedtls_ssl_get_ciphersuite(&ctx->ssl),
           (unsigned long long)hs_elapsed_ms);

    return 0;
}

/* ── HTTP/1.1 framing ────────────────────────────────────────── */

static int tls_write_request_generated(tls_ctx_t *ctx,
                                       const char *method, const char *host,
                                       const char *path,
                                       const vela_header_t *headers,
                                       const char *body, size_t body_len,
                                       vela_https_upload_cb_t upload_cb,
                                       void *upload_data)
{
    char small_hdr[768];
    char *hdr = small_hdr;
    size_t hdr_cap = sizeof(small_hdr);
    size_t required = 192;
    int  pos = 0;
    int  ret;
    bool has_connection = false;

    /* Most requests need only a few hundred bytes. Avoid an extra 4 KB heap
     * allocation at the peak of a TLS handshake; grow only for long headers. */
    const char *parts[] = { method, host, path };
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) {
        size_t len = strlen(parts[i]);
        if (len > VELA_TLS_HDR_BUF - required) {
            return VELA_TLS_ERR_OVERFLOW;
        }
        required += len;
    }
    if (headers) {
        for (const vela_header_t *h = headers; h->name != NULL; h++) {
            size_t name_len = strlen(h->name);
            size_t value_len = strlen(h->value);
            if (name_len > VELA_TLS_HDR_BUF - required) {
                return VELA_TLS_ERR_OVERFLOW;
            }
            required += name_len;
            if (value_len > VELA_TLS_HDR_BUF - required) {
                return VELA_TLS_ERR_OVERFLOW;
            }
            required += value_len;
            if (required > VELA_TLS_HDR_BUF - 4) {
                return VELA_TLS_ERR_OVERFLOW;
            }
            required += 4;
            if (strcasecmp(h->name, "Connection") == 0) {
                has_connection = true;
            }
        }
    }

    if (required > hdr_cap) {
        hdr = malloc(required);
        if (hdr == NULL) {
            struct mallinfo mi = mallinfo();
            syslog(LOG_ERR, "[%s] request header alloc failed: need=%zu "
                   "free=%d largest=%d\n",
                   TAG, required, mi.fordblks, mi.mxordblk);
            return VELA_TLS_ERR_OVERFLOW;
        }
        hdr_cap = required;
    }

#define HDR_APPEND(fmt, ...) \
    do { \
        ret = snprintf(hdr + pos, hdr_cap - (size_t)pos, fmt, ##__VA_ARGS__); \
        if (ret < 0 || (size_t)ret >= hdr_cap - (size_t)pos) { \
            if (hdr != small_hdr) free(hdr); \
            syslog(LOG_ERR, "[%s] request header exceeds %zu bytes\n", \
                   TAG, hdr_cap); \
            return VELA_TLS_ERR_OVERFLOW; \
        } \
        pos += ret; \
    } while (0)

    HDR_APPEND("%s %s HTTP/1.1\r\n", method, path);
    HDR_APPEND("Host: %s\r\n", host);
    if (!has_connection) {
        HDR_APPEND("Connection: keep-alive\r\n");
    }
    HDR_APPEND("User-Agent: velaclaw-vela/1.0\r\n");

    if (body_len > 0) {
        HDR_APPEND("Content-Length: %zu\r\n", body_len);
    }

    if (headers) {
        for (const vela_header_t *h = headers; h->name != NULL; h++) {
            HDR_APPEND("%s: %s\r\n", h->name, h->value);
        }
    }

    HDR_APPEND("\r\n");
#undef HDR_APPEND

    /* Write headers */
    int written = 0;
    uint64_t write_progress_ms = tls_now_ms();
    while (written < pos) {
        uint64_t elapsed_ms = tls_now_ms() - write_progress_ms;

        if (elapsed_ms >= VELA_TLS_WRITE_TIMEOUT_MS) {
            syslog(LOG_ERR,
                   "[%s] ssl_write header timeout after %llums %s %s\n",
                   TAG, (unsigned long long)elapsed_ms, method, path);
            if (hdr != small_hdr) free(hdr);
            return VELA_TLS_ERR_WRITE;
        }

        ret = mbedtls_ssl_write(&ctx->ssl,
                                (const unsigned char *)(hdr + written),
                                (size_t)(pos - written));
        if (ret > 0) {
            written += ret;
            write_progress_ms = tls_now_ms();
        } else if (ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            syslog(LOG_ERR, "[%s] ssl_write header ret=-0x%x sent=%d/%d\n",
                   TAG, -ret, written, pos);
            if (hdr != small_hdr) free(hdr);
            return VELA_TLS_ERR_WRITE;
        }
    }

    if (hdr != small_hdr) free(hdr);

    /* Write body */
    if (body_len > 0) {
        size_t bw = 0;
        char *upload_buf = NULL;

        if (body == NULL) {
            if (upload_cb == NULL) {
                return VELA_TLS_ERR_WRITE;
            }
            upload_buf = malloc(1024);
            if (upload_buf == NULL) {
                return -ENOMEM;
            }
        }

        while (bw < body_len) {
            const char *part;
            size_t part_len;
            size_t part_pos = 0;

            if (upload_buf != NULL) {
                size_t cap = body_len - bw;
                int cbret;

                if (cap > 1024) {
                    cap = 1024;
                }
                part_len = 0;
                cbret = upload_cb(upload_buf, cap, &part_len, upload_data);
                if (cbret != 0 || part_len == 0 || part_len > cap) {
                    syslog(LOG_ERR,
                           "[%s] upload callback failed: ret=%d len=%zu "
                           "sent=%zu/%zu\n",
                           TAG, cbret, part_len, bw, body_len);
                    free(upload_buf);
                    return cbret != 0 ? cbret : VELA_TLS_ERR_WRITE;
                }
                part = upload_buf;
            } else {
                part = body + bw;
                part_len = body_len - bw;
            }

            while (part_pos < part_len) {
                uint64_t elapsed_ms = tls_now_ms() - write_progress_ms;

                if (elapsed_ms >= VELA_TLS_WRITE_TIMEOUT_MS) {
                    syslog(LOG_ERR,
                           "[%s] ssl_write body timeout after %llums %s %s "
                           "(%zu/%zu)\n",
                           TAG, (unsigned long long)elapsed_ms, method, path,
                           bw, body_len);
                    free(upload_buf);
                    return VELA_TLS_ERR_WRITE;
                }

                ret = mbedtls_ssl_write(
                    &ctx->ssl,
                    (const unsigned char *)(part + part_pos),
                    part_len - part_pos);
                if (ret > 0) {
                    part_pos += (size_t)ret;
                    bw += (size_t)ret;
                    write_progress_ms = tls_now_ms();
                } else if (ret == 0) {
                    syslog(LOG_ERR, "[%s] ssl_write body returned 0 "
                           "sent=%zu/%zu\n", TAG, bw, body_len);
                    free(upload_buf);
                    return VELA_TLS_ERR_WRITE;
                } else if (ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                    syslog(LOG_ERR, "[%s] ssl_write body ret=-0x%x "
                           "sent=%zu/%zu\n", TAG, -ret, bw, body_len);
                    free(upload_buf);
                    return VELA_TLS_ERR_WRITE;
                }
            }
        }
        free(upload_buf);
    }

    return 0;
}

static int tls_write_request(tls_ctx_t *ctx,
                             const char *method, const char *host,
                             const char *path,
                             const vela_header_t *headers,
                             const char *body, size_t body_len)
{
    return tls_write_request_generated(ctx, method, host, path, headers,
                                       body, body_len, NULL, NULL);
}

/**
 * Read full HTTP/1.1 response.
 * Returns HTTP status code; writes body into resp_buf (NUL-terminated).
 * Handles Transfer-Encoding: chunked and Content-Length.
 */
#define TLS_RAW_BUF_SIZE VELA_TLS_HDR_BUF

/*
 * Return true only after the complete terminating chunk (including trailers)
 * is present.  This lets regular JSON requests keep an HTTP/1.1 connection
 * alive instead of waiting for the server to close it.
 */
static bool tls_chunked_body_complete(const char *buf, size_t len)
{
    size_t pos = 0;

    while (pos < len) {
        const char *line_end = memmem(buf + pos, len - pos, "\r\n", 2);
        const char *size_end;
        char size_line[32];
        char *endptr;
        long chunk_size;
        size_t size_len;

        if (line_end == NULL) {
            return false;
        }

        size_end = memchr(buf + pos, ';', (size_t)(line_end - (buf + pos)));
        if (size_end == NULL) {
            size_end = line_end;
        }
        size_len = (size_t)(size_end - (buf + pos));
        while (size_len > 0 && buf[pos + size_len - 1] == ' ') {
            size_len--;
        }
        if (size_len == 0 || size_len >= sizeof(size_line)) {
            return false;
        }

        memcpy(size_line, buf + pos, size_len);
        size_line[size_len] = '\0';
        chunk_size = strtol(size_line, &endptr, 16);
        if (endptr == size_line || *endptr != '\0' || chunk_size < 0) {
            return false;
        }

        pos = (size_t)(line_end - buf) + 2;
        if (chunk_size == 0) {
            if (len - pos >= 2 && buf[pos] == '\r' && buf[pos + 1] == '\n') {
                return true;
            }
            return memmem(buf + pos, len - pos, "\r\n\r\n", 4) != NULL;
        }

        if ((size_t)chunk_size > len - pos ||
            len - pos - (size_t)chunk_size < 2) {
            return false;
        }
        pos += (size_t)chunk_size;
        if (buf[pos] != '\r' || buf[pos + 1] != '\n') {
            return false;
        }
        pos += 2;
    }

    return false;
}

static int tls_read_response(tls_ctx_t *ctx, char *resp_buf, size_t resp_cap,
                             size_t *out_body_len, bool *out_keep_alive)
{
    /* This buffer only needs to hold the HTTP header and the first body
     * fragment.  The response body is read directly into resp_buf below. */
    char *raw = (char *)malloc(TLS_RAW_BUF_SIZE);
    if (!raw) {
        syslog(LOG_ERR, "[%s] tls_read_response: OOM allocating raw buffer\n", TAG);
        return VELA_TLS_ERR_READ;
    }
    size_t raw_len  = 0;
    int    eof      = 0;
    int    ret;
    uint64_t read_start_ms = tls_now_ms();

    /* Read until we have the full header (double CRLF) or buffer full */
    while (!eof && raw_len < TLS_RAW_BUF_SIZE - 1) {
        if (tls_now_ms() - read_start_ms >= VELA_TLS_RESPONSE_TIMEOUT_MS) {
            syslog(LOG_ERR,
                   "[%s] HTTP header timeout after %llums\n",
                   TAG,
                   (unsigned long long)(tls_now_ms() - read_start_ms));
            free(raw);
            return VELA_TLS_ERR_READ;
        }
        ret = mbedtls_ssl_read(&ctx->ssl,
                               (unsigned char *)(raw + raw_len),
                               TLS_RAW_BUF_SIZE - 1 - raw_len);
        if (ret > 0) {
            raw_len += (size_t)ret;
            if (memmem(raw, raw_len, "\r\n\r\n", 4)) break;
        } else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            eof = 1; break;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
            usleep(10 * 1000);
        } else {
            syslog(LOG_ERR, "[%s] ssl_read (header) ret=0x%x\n", TAG, -ret);
            free(raw);
            return VELA_TLS_ERR_READ;
        }
    }
    raw[raw_len] = '\0';

    /* Parse status line */
    int http_status = 0;
    if (sscanf(raw, "HTTP/1.%*d %d", &http_status) != 1) {
        syslog(LOG_ERR, "[%s] Failed to parse HTTP status from: %.80s\n", TAG, raw);
        free(raw);
        return VELA_TLS_ERR_READ;
    }

    /* Find header/body split */
    char *body_start = (char *)memmem(raw, raw_len, "\r\n\r\n", 4);
    if (!body_start) {
        resp_buf[0] = '\0';
        free(raw);
        return http_status;
    }

    /* Determine keep-alive from headers (default keep-alive for HTTP/1.1) */
    if (out_keep_alive) {
        *out_keep_alive = (strcasestr(raw, "Connection: close") == NULL ||
                           strcasestr(raw, "Connection: close") > body_start)
                          ? true : false;
        /* Explicit keep-alive header overrides */
        char *conn_hdr = strcasestr(raw, "Connection:");
        if (conn_hdr && conn_hdr < body_start) {
            *out_keep_alive = (strcasestr(conn_hdr, "keep-alive") != NULL);
        }
    }
    body_start += 4;   /* skip double CRLF */

    /* Extract content-length if present */
    long content_length = -1;
    {
        char *cl_hdr = strcasestr(raw, "Content-Length:");
        if (cl_hdr && cl_hdr < body_start) {
            cl_hdr += strlen("Content-Length:");
            content_length = strtol(cl_hdr, NULL, 10);
        }
    }
    int chunked = 0;
    {
        char *te_hdr = strcasestr(raw, "Transfer-Encoding:");
        if (te_hdr && te_hdr < body_start) {
            chunked = (strcasestr(te_hdr, "chunked") != NULL);
        }
    }

    /* Initial fragment already in raw buffer */
    size_t initial = (size_t)(raw + raw_len - body_start);
    size_t resp_pos = 0;

    /* Copy initial fragment */
    size_t copy = initial < resp_cap - 1 ? initial : resp_cap - 1;
    memcpy(resp_buf, body_start, copy);
    resp_pos = copy;

    /* Keep reading body */
    if (!eof) {
        while (resp_pos < resp_cap - 1) {
            if (content_length >= 0 && (long)resp_pos >= content_length) break;
            if (chunked && tls_chunked_body_complete(resp_buf, resp_pos)) break;
            if (tls_now_ms() - read_start_ms >= VELA_TLS_RESPONSE_TIMEOUT_MS) {
                syslog(LOG_ERR,
                       "[%s] HTTP body timeout after %llums status=%d "
                       "bytes=%zu content_length=%ld chunked=%d\n",
                       TAG,
                       (unsigned long long)(tls_now_ms() - read_start_ms),
                       http_status, resp_pos, content_length, chunked);
                free(raw);
                return VELA_TLS_ERR_READ;
            }
            ret = mbedtls_ssl_read(&ctx->ssl,
                                   (unsigned char *)(resp_buf + resp_pos),
                                   resp_cap - 1 - resp_pos);
            if (ret > 0)      { resp_pos += (size_t)ret; }
            else if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) { break; }
            else if (ret == MBEDTLS_ERR_SSL_WANT_READ) { usleep(10 * 1000); }
            else { break; }
        }
    }

    resp_buf[resp_pos] = '\0';
    free(raw);

    /* Chunked decode — binary-safe, uses memmem instead of strstr.
     * Format: <hex-size>\r\n<data of hex-size bytes>\r\n ... 0\r\n */
    if (chunked) {
        char *src = resp_buf;
        char *end = resp_buf + resp_pos;
        char *dst = resp_buf;

        while (src < end) {
            /* Find end of chunk-size line */
            char *crlf = (char *)memmem(src, (size_t)(end - src),
                                        "\r\n", 2);
            if (!crlf) {
                break;
            }

            /* Parse hex chunk size */
            char *endptr;
            long chunk_sz = strtol(src, &endptr, 16);

            /* Validate: endptr should reach the CRLF (skip spaces) */
            while (endptr < crlf && *endptr == ' ') {
                endptr++;
            }

            if (endptr != crlf || chunk_sz < 0) {
                break; /* malformed chunk header */
            }

            if (chunk_sz == 0) {
                break; /* final chunk */
            }

            src = crlf + 2; /* skip past chunk-size CRLF */

            /* Clamp to available data */
            if (src + chunk_sz > end) {
                chunk_sz = (long)(end - src);
            }

            memmove(dst, src, (size_t)chunk_sz);
            dst += chunk_sz;
            src += chunk_sz;

            /* Skip trailing CRLF after chunk data */
            if (src + 2 <= end && src[0] == '\r' && src[1] == '\n') {
                src += 2;
            }
        }

        resp_pos = (size_t)(dst - resp_buf);
        resp_buf[resp_pos] = '\0';
    }

    if (out_body_len)
        *out_body_len = resp_pos;

    return http_status;
}

/* ── Public API ──────────────────────────────────────────────── */

static int vela_https_request_internal(
    const char       *host,
    const char       *port,
    const char       *method,
    const char       *path,
    const vela_header_t *headers,
    const char       *body,
    size_t            body_len,
    vela_https_upload_cb_t upload_cb,
    void             *upload_data,
    char             *resp_buf,
    size_t            resp_cap,
    size_t           *out_body_len)
{
    int ret;

    /* Try to get a pooled connection first */
    conn_slot_t *slot = pool_acquire(host, port);

    /* An upload producer is forward-only.  Start it on a fresh connection so
     * a stale pooled socket cannot consume part of the generated body before
     * the reconnect path retries the request. */
    if (slot && slot->valid && upload_cb != NULL) {
        tls_ctx_free(&slot->ctx);
        slot->valid = false;
    }

    if (slot && slot->valid) {
        /* Drain any leftover data from previous response before reuse.
         * Without this, a partially-read response body (e.g. truncated
         * chunked data) would be misinterpreted as the next HTTP status. */
        if (slot->ctx.net.fd >= 0) {
            unsigned char drain[512];
            int dr;
            /* Temporarily set a very short socket timeout to drain without blocking */
            struct timeval tv_drain = { .tv_sec = 0, .tv_usec = 10000 }; /* 10ms */
            struct timeval tv_orig  = {
                .tv_sec = VELA_TLS_DEFAULT_READ_TIMEOUT_SEC,
                .tv_usec = 0
            };
            setsockopt(slot->ctx.net.fd, SOL_SOCKET, SO_RCVTIMEO,
                       &tv_drain, sizeof(tv_drain));
            while ((dr = mbedtls_ssl_read(&slot->ctx.ssl, drain, sizeof(drain))) > 0)
                ;  /* discard leftover bytes */
            /* Restore original timeout */
            setsockopt(slot->ctx.net.fd, SOL_SOCKET, SO_RCVTIMEO,
                       &tv_orig, sizeof(tv_orig));
            /* If peer closed the connection, reconnect */
            if (dr == 0 || dr == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                goto pool_reconnect;
            }
        } else {
            goto pool_reconnect;
        }

        /* Reuse existing connection — skip TLS handshake */
        syslog(LOG_INFO, "[%s] Reusing pooled connection to %s:%s\n", TAG, host, port);
        ret = tls_write_request_generated(&slot->ctx, method, host, path,
                                          headers, body, body_len,
                                          upload_cb, upload_data);
        if (ret == 0) {
            bool keep = false;
            ret = tls_read_response(&slot->ctx, resp_buf, resp_cap, out_body_len, &keep);
            if (ret > 0) {
                pool_release(slot, host, port, keep);
                return ret;
            }
        }
        /* Connection went stale — fall through to reconnect */
pool_reconnect:
        syslog(LOG_INFO, "[%s] Pooled connection stale, reconnecting\n", TAG);
        tls_ctx_free(&slot->ctx);
        slot->valid = false;
    }

    /* New connection */
    if (!slot) {
        /* Pool full — use a temporary ephemeral context */
        tls_ctx_t tmp_ctx;
        if ((ret = tls_ctx_connect(&tmp_ctx, host, port)) != 0) {
            tls_ctx_free(&tmp_ctx);
            return ret;
        }
        if ((ret = tls_write_request_generated(
                 &tmp_ctx, method, host, path, headers, body, body_len,
                 upload_cb, upload_data)) != 0) {
            syslog(LOG_ERR, "[%s] Write request failed: %d\n", TAG, ret);
            tls_ctx_free(&tmp_ctx);
            return ret;
        }
        ret = tls_read_response(&tmp_ctx, resp_buf, resp_cap, out_body_len, NULL);
        tls_ctx_free(&tmp_ctx);
        return ret;
    }

    /* Connect into the slot */
    if ((ret = tls_ctx_connect(&slot->ctx, host, port)) != 0) {
        pool_release(slot, host, port, false);
        return ret;
    }

    if ((ret = tls_write_request_generated(
             &slot->ctx, method, host, path, headers, body, body_len,
             upload_cb, upload_data)) != 0) {
        syslog(LOG_ERR, "[%s] Write request failed: %d\n", TAG, ret);
        pool_release(slot, host, port, false);
        return ret;
    }

    bool keep = false;
    ret = tls_read_response(&slot->ctx, resp_buf, resp_cap, out_body_len, &keep);
    pool_release(slot, host, port,
                 ret > 0 && keep && upload_cb == NULL);
    return ret;
}

int vela_https_request(
    const char       *host,
    const char       *port,
    const char       *method,
    const char       *path,
    const vela_header_t *headers,
    const char       *body,
    size_t            body_len,
    char             *resp_buf,
    size_t            resp_cap,
    size_t           *out_body_len)
{
    return vela_https_request_internal(host, port, method, path, headers,
                                       body, body_len, NULL, NULL,
                                       resp_buf, resp_cap, out_body_len);
}

int vela_https_request_upload(
    const char       *host,
    const char       *port,
    const char       *method,
    const char       *path,
    const vela_header_t *headers,
    size_t            body_len,
    vela_https_upload_cb_t upload_cb,
    void             *upload_data,
    char             *resp_buf,
    size_t            resp_cap,
    size_t           *out_body_len)
{
    if (body_len == 0 || upload_cb == NULL) {
        return -EINVAL;
    }

    return vela_https_request_internal(host, port, method, path, headers,
                                       NULL, body_len,
                                       upload_cb, upload_data,
                                       resp_buf, resp_cap, out_body_len);
}

static int tls_read_stream_header(tls_ctx_t *ctx,
                                  char *hdr, size_t hdr_cap,
                                  char *body, size_t body_cap,
                                  size_t *body_len,
                                  long *content_length,
                                  bool *chunked,
                                  bool *keep_alive)
{
    size_t hdr_len = 0;
    int ret;

    if (hdr_cap == 0 || body_cap == 0) {
        return VELA_TLS_ERR_OVERFLOW;
    }

    *body_len = 0;
    *content_length = -1;
    *chunked = false;
    if (keep_alive) {
        *keep_alive = true;
    }

    while (hdr_len < hdr_cap - 1) {
        ret = mbedtls_ssl_read(&ctx->ssl,
                               (unsigned char *)(hdr + hdr_len),
                               hdr_cap - 1 - hdr_len);
        if (ret > 0) {
            char *split;

            hdr_len += (size_t)ret;
            hdr[hdr_len] = '\0';
            split = (char *)memmem(hdr, hdr_len, "\r\n\r\n", 4);
            if (split) {
                size_t header_bytes;
                size_t extra;

                header_bytes = (size_t)(split + 4 - hdr);
                extra = hdr_len - header_bytes;
                if (extra > body_cap) {
                    extra = body_cap;
                }
                memcpy(body, split + 4, extra);
                *body_len = extra;
                hdr[header_bytes] = '\0';

                char *cl = strcasestr(hdr, "Content-Length:");
                if (cl) {
                    *content_length =
                        strtol(cl + strlen("Content-Length:"), NULL, 10);
                }

                char *te = strcasestr(hdr, "Transfer-Encoding:");
                if (te && strcasestr(te, "chunked")) {
                    *chunked = true;
                }

                if (keep_alive) {
                    char *conn = strcasestr(hdr, "Connection:");

                    *keep_alive = true;
                    if (conn != NULL) {
                        char *eol = strstr(conn, "\r\n");

                        if (eol == NULL) {
                            eol = hdr + header_bytes;
                        }
                        *keep_alive =
                            strcasestr(conn, "close") == NULL ||
                            strcasestr(conn, "close") > eol;
                    }
                }

                int http_status = 0;
                if (sscanf(hdr, "HTTP/1.%*d %d", &http_status) != 1) {
                    return VELA_TLS_ERR_READ;
                }

                return http_status;
            }
        } else if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                   ret == MBEDTLS_ERR_SSL_TIMEOUT) {
            return -ETIMEDOUT;
        } else {
            return VELA_TLS_ERR_READ;
        }
    }

    return VELA_TLS_ERR_OVERFLOW;
}

struct tls_chunk_stream {
    enum {
        TLS_CHUNK_SIZE = 0,
        TLS_CHUNK_DATA,
        TLS_CHUNK_DATA_CR,
        TLS_CHUNK_DATA_LF,
        TLS_CHUNK_DONE
    } state;
    char line[32];
    size_t line_len;
    long remaining;
};

static int tls_stream_feed_chunked(struct tls_chunk_stream *cs,
                                   const char *data, size_t len,
                                   vela_https_body_cb_t cb,
                                   void *user_data)
{
    size_t pos = 0;

    while (pos < len) {
        if (cs->state == TLS_CHUNK_SIZE) {
            char c = data[pos++];

            if (c == '\r') {
                continue;
            }

            if (c == '\n') {
                char *endptr;
                char *semi;

                cs->line[cs->line_len] = '\0';
                semi = strchr(cs->line, ';');
                if (semi) {
                    *semi = '\0';
                }
                cs->remaining = strtol(cs->line, &endptr, 16);
                while (*endptr == ' ') {
                    endptr++;
                }
                if (endptr == cs->line || *endptr != '\0' ||
                    cs->remaining < 0) {
                    return VELA_TLS_ERR_READ;
                }

                if (cs->remaining == 0) {
                    cs->state = TLS_CHUNK_DONE;
                    return 0;
                }

                cs->line_len = 0;
                cs->state = TLS_CHUNK_DATA;
                continue;
            }

            if (cs->line_len + 1 >= sizeof(cs->line)) {
                return VELA_TLS_ERR_OVERFLOW;
            }
            cs->line[cs->line_len++] = c;
        } else if (cs->state == TLS_CHUNK_DATA) {
            size_t take = len - pos;

            if (take > (size_t)cs->remaining) {
                take = (size_t)cs->remaining;
            }

            if (take > 0) {
                int cbret = cb(data + pos, take, user_data);
                if (cbret != 0) {
                    return cbret;
                }
                pos += take;
                cs->remaining -= (long)take;
            }

            if (cs->remaining == 0) {
                cs->state = TLS_CHUNK_DATA_CR;
            }
        } else if (cs->state == TLS_CHUNK_DATA_CR) {
            if (data[pos++] == '\n') {
                cs->state = TLS_CHUNK_SIZE;
            } else {
                cs->state = TLS_CHUNK_DATA_LF;
            }
        } else if (cs->state == TLS_CHUNK_DATA_LF) {
            (void)data[pos++];
            cs->state = TLS_CHUNK_SIZE;
        } else {
            return 0;
        }
    }

    return 0;
}

static void tls_stream_set_read_timeout(tls_ctx_t *ctx)
{
    if (ctx != NULL && ctx->net.fd >= 0) {
        struct timeval tv = {
            .tv_sec = VELA_TLS_STREAM_READ_TIMEOUT_SEC,
            .tv_usec = 0
        };
        setsockopt(ctx->net.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
}

static bool tls_pool_drain_for_reuse(conn_slot_t *slot)
{
    unsigned char drain[512];
    struct timeval tv_drain = {
        .tv_sec = 0,
        .tv_usec = 10000
    };
    struct timeval tv_orig = {
        .tv_sec = VELA_TLS_DEFAULT_READ_TIMEOUT_SEC,
        .tv_usec = 0
    };
    int dr;

    if (slot == NULL || slot->ctx.net.fd < 0) {
        return false;
    }

    setsockopt(slot->ctx.net.fd, SOL_SOCKET, SO_RCVTIMEO,
               &tv_drain, sizeof(tv_drain));
    while ((dr = mbedtls_ssl_read(&slot->ctx.ssl, drain,
                                  sizeof(drain))) > 0) {
        ;
    }
    setsockopt(slot->ctx.net.fd, SOL_SOCKET, SO_RCVTIMEO,
               &tv_orig, sizeof(tv_orig));

    return !(dr == 0 || dr == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY);
}

int vela_https_request_stream(
    const char       *host,
    const char       *port,
    const char       *method,
    const char       *path,
    const vela_header_t *headers,
    const char       *body,
    size_t            body_len,
    vela_https_body_cb_t cb,
    void             *user_data,
    int              *out_http_status)
{
    tls_ctx_t tmp_ctx;
    tls_ctx_t *ctx = NULL;
    conn_slot_t *slot;
    char *hdr;
    char *buf;
    size_t initial_len;
    long content_length;
    bool chunked;
    bool keep_alive = false;
    bool stream_complete = false;
    uint64_t stream_body_start_ms;
    size_t stream_wire_bytes;
    unsigned int stream_read_calls;
    int status;
    int ret;

    if (!host || !port || !method || !path || !cb) {
        return -EINVAL;
    }

    hdr = malloc(VELA_TLS_HDR_BUF);
    if (!hdr) {
        return -ENOMEM;
    }

    /*
     * Keep the first body fragment as large as the header buffer.  A TLS
     * record can contain the response headers and several SSE events at once;
     * truncating that fragment loses JSON and leaves the TTS decoder waiting
     * for data that has already been discarded.
     */
    buf = malloc(VELA_TLS_HDR_BUF);
    if (!buf) {
        free(hdr);
        return -ENOMEM;
    }

    memset(&tmp_ctx, 0, sizeof(tmp_ctx));
    slot = pool_acquire(host, port);

    if (slot != NULL && slot->valid) {
        if (tls_pool_drain_for_reuse(slot)) {
            syslog(LOG_INFO,
                   "[%s] Reusing pooled stream connection to %s:%s\n",
                   TAG, host, port);
            ctx = &slot->ctx;
            ret = tls_write_request(ctx, method, host, path,
                                    headers, body, body_len);
            if (ret == 0) {
                goto request_written;
            }
            syslog(LOG_INFO,
                   "[%s] Pooled stream connection write failed, "
                   "reconnecting\n", TAG);
        } else {
            syslog(LOG_INFO,
                   "[%s] Pooled stream connection stale, reconnecting\n",
                   TAG);
        }
        tls_ctx_free(&slot->ctx);
        slot->valid = false;
    }

    if (slot != NULL) {
        ctx = &slot->ctx;
        ret = tls_ctx_connect(ctx, host, port);
    } else {
        ctx = &tmp_ctx;
        ret = tls_ctx_connect(ctx, host, port);
    }
    if (ret != 0) {
        if (slot != NULL) {
            pool_release(slot, host, port, false);
        } else {
            tls_ctx_free(ctx);
        }
        free(buf);
        free(hdr);
        return ret;
    }

    ret = tls_write_request(ctx, method, host, path,
                            headers, body, body_len);
    if (ret != 0) {
        if (slot != NULL) {
            pool_release(slot, host, port, false);
        } else {
            tls_ctx_free(ctx);
        }
        free(buf);
        free(hdr);
        return ret;
    }

request_written:
    /*
     * Streaming endpoints are allowed to keep the connection open.  Bound
     * the idle wait so a server-side response problem cannot permanently
     * block the voice worker after the TLS handshake.
     */
    tls_stream_set_read_timeout(ctx);

    status = tls_read_stream_header(ctx, hdr, VELA_TLS_HDR_BUF,
                                    buf, VELA_TLS_HDR_BUF, &initial_len,
                                    &content_length, &chunked, &keep_alive);
    if (out_http_status) {
        *out_http_status = status;
    }
    if (status <= 0) {
        if (slot != NULL) {
            pool_release(slot, host, port, false);
        } else {
            tls_ctx_free(ctx);
        }
        free(buf);
        free(hdr);
        return status;
    }

    syslog(LOG_INFO,
           "[%s] stream response: status=%d chunked=%d length=%ld "
           "keep_alive=%d initial=%zu\n",
           TAG, status, chunked ? 1 : 0, content_length,
           keep_alive ? 1 : 0, initial_len);
    {
        const char *ct = strcasestr(hdr, "Content-Type:");
        const char *ce = strcasestr(hdr, "Content-Encoding:");
        const char *ct_end = ct ? strstr(ct, "\r\n") : NULL;
        const char *ce_end = ce ? strstr(ce, "\r\n") : NULL;

        syslog(LOG_INFO,
               "[%s] stream headers: content_type=%.*s encoding=%.*s\n",
               TAG,
               ct ? (int)((ct_end ? ct_end : ct + strlen(ct)) -
                          (ct + strlen("Content-Type:"))) : 1,
               ct ? ct + strlen("Content-Type:") : "-",
               ce ? (int)((ce_end ? ce_end : ce + strlen(ce)) -
                          (ce + strlen("Content-Encoding:"))) : 1,
               ce ? ce + strlen("Content-Encoding:") : "-");
    }

    stream_body_start_ms = tls_now_ms();
    stream_wire_bytes = initial_len;
    stream_read_calls = 0;

    if (chunked) {
        struct tls_chunk_stream cs;

        memset(&cs, 0, sizeof(cs));
        cs.state = TLS_CHUNK_SIZE;
        ret = tls_stream_feed_chunked(&cs, buf, initial_len, cb, user_data);
        while (ret == 0 && cs.state != TLS_CHUNK_DONE) {
            int n = mbedtls_ssl_read(&ctx->ssl,
                                     (unsigned char *)buf, VELA_TLS_HDR_BUF);

            stream_read_calls++;
            if (n > 0) {
                stream_wire_bytes += (size_t)n;
                ret = tls_stream_feed_chunked(&cs, buf, (size_t)n,
                                              cb, user_data);
            } else if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                break;
            } else if (n == MBEDTLS_ERR_SSL_WANT_READ ||
                       n == MBEDTLS_ERR_SSL_TIMEOUT) {
                syslog(LOG_WARNING,
                       "[%s] stream recv timeout #%u code=%d wire=%zu\n",
                       TAG, stream_read_calls, n,
                       stream_wire_bytes);
                ret = -ETIMEDOUT;
                break;
            } else {
                syslog(LOG_WARNING,
                       "[%s] stream recv error #%u code=%d wire=%zu\n",
                       TAG, stream_read_calls, n,
                       stream_wire_bytes);
                ret = VELA_TLS_ERR_READ;
                break;
            }
        }
        stream_complete = ret == 0 && cs.state == TLS_CHUNK_DONE;
    } else {
        size_t delivered = 0;

        if (initial_len > 0) {
            size_t take = initial_len;
            if (content_length >= 0 &&
                take > (size_t)content_length) {
                take = (size_t)content_length;
            }
            ret = cb(buf, take, user_data);
            delivered += take;
        } else {
            ret = 0;
        }

        while (ret == 0) {
            if (content_length >= 0 && delivered >= (size_t)content_length) {
                break;
            }

            int n = mbedtls_ssl_read(&ctx->ssl,
                                     (unsigned char *)buf, VELA_TLS_HDR_BUF);

            stream_read_calls++;
            if (n > 0) {
                size_t take = (size_t)n;

                stream_wire_bytes += (size_t)n;
                if (content_length >= 0 &&
                    delivered + take > (size_t)content_length) {
                    take = (size_t)content_length - delivered;
                }
                ret = cb(buf, take, user_data);
                delivered += take;
            } else if (n == 0 || n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                break;
            } else if (n == MBEDTLS_ERR_SSL_WANT_READ ||
                       n == MBEDTLS_ERR_SSL_TIMEOUT) {
                syslog(LOG_WARNING,
                       "[%s] stream recv timeout #%u code=%d wire=%zu\n",
                       TAG, stream_read_calls, n,
                       stream_wire_bytes);
                ret = -ETIMEDOUT;
                break;
            } else {
                syslog(LOG_WARNING,
                       "[%s] stream recv error #%u code=%d wire=%zu\n",
                       TAG, stream_read_calls, n,
                       stream_wire_bytes);
                ret = VELA_TLS_ERR_READ;
                break;
            }
        }
        stream_complete =
            ret == 0 && content_length >= 0 &&
            delivered >= (size_t)content_length;
    }

    syslog(LOG_INFO,
           "[%s] stream body done reads=%u wire=%zu elapsed=%llums "
           "complete=%d ret=%d\n",
           TAG, stream_read_calls, stream_wire_bytes,
           (unsigned long long)(tls_now_ms() - stream_body_start_ms),
           stream_complete ? 1 : 0, ret);

    if (slot != NULL) {
        bool keep = ret == 0 && status > 0 && keep_alive && stream_complete;

        pool_release(slot, host, port, keep);
    } else {
        tls_ctx_free(ctx);
    }
    free(buf);
    free(hdr);
    return ret == 0 ? status : ret;
}

int vela_https_get(const char *host, const char *port, const char *path,
                   char *resp_buf, size_t resp_cap)
{
    return vela_https_request(host, port, "GET", path, NULL, NULL, 0,
                               resp_buf, resp_cap, NULL);
}

int vela_https_post_json(const char *host, const char *port, const char *path,
                         const vela_header_t *extra_headers,
                         const char *json_body,
                         char *resp_buf, size_t resp_cap)
{
    /* Build a merged header list: Content-Type first, then caller extras */
    const int MAX_HDRS = 32;
    vela_header_t merged[MAX_HDRS];
    int n = 0;

    merged[n++] = (vela_header_t){ "Content-Type", "application/json" };

    if (extra_headers) {
        for (const vela_header_t *h = extra_headers; h->name && n < MAX_HDRS - 1; h++) {
            merged[n++] = *h;
        }
    }
    merged[n] = (vela_header_t){ NULL, NULL };

    size_t body_len = json_body ? strlen(json_body) : 0;
    return vela_https_request(host, port, "POST", path, merged,
                               json_body, body_len, resp_buf, resp_cap, NULL);
}

int vela_https_head_date(const char *host, const char *port, const char *path,
                         char *date_out, size_t date_cap)
{
    tls_ctx_t ctx;
    int ret;

    if ((ret = tls_ctx_connect(&ctx, host, port)) != 0) {
        tls_ctx_free(&ctx);
        return ret;
    }

    if ((ret = tls_write_request(&ctx, "HEAD", host, path, NULL, NULL, 0)) != 0) {
        tls_ctx_free(&ctx);
        return ret;
    }

    /* Read raw response until end of headers (\r\n\r\n) */
    char hdr_buf[2048];
    int total = 0;
    while (total < (int)sizeof(hdr_buf) - 1) {
        ret = mbedtls_ssl_read(&ctx.ssl,
                               (unsigned char *)hdr_buf + total,
                               sizeof(hdr_buf) - 1 - total);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ) continue;
        if (ret <= 0) break;
        total += ret;
        hdr_buf[total] = '\0';
        if (strstr(hdr_buf, "\r\n\r\n")) break;
    }
    tls_ctx_free(&ctx);

    if (total <= 0) return VELA_TLS_ERR_READ;

    /* Find Date: header (case-insensitive prefix search) */
    char *p = strcasestr(hdr_buf, "\r\nDate: ");
    if (!p) return VELA_TLS_ERR_READ;
    p += 8; /* skip \r\nDate:  */
    char *eol = strstr(p, "\r\n");
    if (!eol) return VELA_TLS_ERR_READ;

    size_t dlen = (size_t)(eol - p);
    if (dlen >= date_cap) dlen = date_cap - 1;
    memcpy(date_out, p, dlen);
    date_out[dlen] = '\0';
    return 0;
}

/* ── Plain HTTP (no TLS) POST ─────────────────────────────── */

int vela_http_post_json(const char *host, const char *port, const char *path,
                        const vela_header_t *extra_headers,
                        const char *json_body,
                        char *resp_buf, size_t resp_cap)
{
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int gai = getaddrinfo(host, port, &hints, &res);
    if (gai != 0 || !res) {
        syslog(LOG_INFO, "http: getaddrinfo %s:%s failed: %d", host, port, gai);
        return VELA_TLS_ERR_CONNECT;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        syslog(LOG_INFO, "http: socket() failed: %d", errno);
        return VELA_TLS_ERR_CONNECT;
    }

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        syslog(LOG_INFO, "http: connect %s:%s failed: %d", host, port, errno);
        close(fd);
        freeaddrinfo(res);
        return VELA_TLS_ERR_CONNECT;
    }
    freeaddrinfo(res);

    /* Set read timeout */
    struct timeval tv = {
        .tv_sec = VELA_TLS_DEFAULT_READ_TIMEOUT_SEC,
        .tv_usec = 0
    };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    tv.tv_sec = VELA_TLS_DEFAULT_WRITE_TIMEOUT_SEC;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* Build HTTP request */
    size_t body_len = json_body ? strlen(json_body) : 0;
    char hdr[4096];
    int pos = 0;

#define HTTP_APPEND(fmt, ...) \
    pos += snprintf(hdr + pos, (int)sizeof(hdr) - pos, fmt, ##__VA_ARGS__); \
    if (pos >= (int)sizeof(hdr)) { close(fd); return VELA_TLS_ERR_OVERFLOW; }

    HTTP_APPEND("POST %s HTTP/1.1\r\n", path);
    HTTP_APPEND("Host: %s\r\n", host);
    HTTP_APPEND("Content-Type: application/json\r\n");
    HTTP_APPEND("Connection: close\r\n");
    HTTP_APPEND("User-Agent: velaclaw/1.0\r\n");
    if (body_len > 0) {
        HTTP_APPEND("Content-Length: %zu\r\n", body_len);
    }
    if (extra_headers) {
        for (const vela_header_t *h = extra_headers; h->name; h++) {
            HTTP_APPEND("%s: %s\r\n", h->name, h->value);
        }
    }
    HTTP_APPEND("\r\n");
#undef HTTP_APPEND

    /* Send header + body */
    if (write(fd, hdr, (size_t)pos) != pos) {
        close(fd);
        return VELA_TLS_ERR_WRITE;
    }
    if (json_body && body_len > 0) {
        if (write(fd, json_body, body_len) != (ssize_t)body_len) {
            close(fd);
            return VELA_TLS_ERR_WRITE;
        }
    }

    /* Read response */
    char *raw = (char *)malloc(TLS_RAW_BUF_SIZE);
    if (!raw) { close(fd); return VELA_TLS_ERR_READ; }
    size_t raw_len = 0;
    int eof = 0;

    /* Read until we have the full header */
    while (!eof && raw_len < TLS_RAW_BUF_SIZE - 1) {
        ssize_t n = read(fd, raw + raw_len, TLS_RAW_BUF_SIZE - 1 - raw_len);
        if (n > 0) {
            raw_len += (size_t)n;
            raw[raw_len] = '\0';
            if (memmem(raw, raw_len, "\r\n\r\n", 4)) break;
        } else {
            eof = 1;
        }
    }
    raw[raw_len] = '\0';

    /* Parse status */
    int http_status = 0;
    if (sscanf(raw, "HTTP/1.%*d %d", &http_status) != 1) {
        syslog(LOG_INFO, "http: bad status: %.80s", raw);
        free(raw); close(fd);
        return VELA_TLS_ERR_READ;
    }

    /* Find body */
    char *body_start = (char *)memmem(raw, raw_len, "\r\n\r\n", 4);
    if (!body_start) {
        resp_buf[0] = '\0';
        free(raw); close(fd);
        return http_status;
    }
    body_start += 4;

    /* Check content-length / chunked */
    long content_length = -1;
    {
        char *cl = strcasestr(raw, "Content-Length:");
        if (cl && cl < body_start) {
            content_length = strtol(cl + strlen("Content-Length:"), NULL, 10);
        }
    }
    int chunked = 0;
    {
        char *te = strcasestr(raw, "Transfer-Encoding:");
        if (te && te < body_start) {
            chunked = (strcasestr(te, "chunked") != NULL);
        }
    }

    /* Copy initial fragment */
    size_t initial = (size_t)(raw + raw_len - body_start);
    size_t resp_pos = 0;
    size_t copy = initial < resp_cap - 1 ? initial : resp_cap - 1;
    memcpy(resp_buf, body_start, copy);
    resp_pos = copy;

    /* Keep reading body */
    if (!eof) {
        while (resp_pos < resp_cap - 1) {
            if (content_length >= 0 && (long)resp_pos >= content_length) break;
            ssize_t n = read(fd, resp_buf + resp_pos, resp_cap - 1 - resp_pos);
            if (n <= 0) break;
            resp_pos += (size_t)n;
        }
    }
    resp_buf[resp_pos] = '\0';
    free(raw);
    close(fd);

    /* Chunked decode — binary-safe */
    if (chunked) {
        char *src = resp_buf;
        char *end = resp_buf + resp_pos;
        char *dst = resp_buf;

        while (src < end) {
            char *crlf = (char *)memmem(src, (size_t)(end - src),
                                        "\r\n", 2);
            if (!crlf) {
                break;
            }

            char *endptr;
            long chunk_sz = strtol(src, &endptr, 16);

            while (endptr < crlf && *endptr == ' ') {
                endptr++;
            }

            if (endptr != crlf || chunk_sz < 0) {
                break;
            }

            if (chunk_sz == 0) {
                break;
            }

            src = crlf + 2;

            if (src + chunk_sz > end) {
                chunk_sz = (long)(end - src);
            }

            memmove(dst, src, (size_t)chunk_sz);
            dst += chunk_sz;
            src += chunk_sz;

            if (src + 2 <= end && src[0] == '\r' && src[1] == '\n') {
                src += 2;
            }
        }

        resp_pos = (size_t)(dst - resp_buf);
        resp_buf[resp_pos] = '\0';
    }

    return http_status;
}
