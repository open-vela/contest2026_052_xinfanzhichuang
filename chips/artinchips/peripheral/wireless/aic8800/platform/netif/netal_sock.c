/**
  ******************************************************************************
  * @file   netal_sock.c
  * @author AIC software development team
  ******************************************************************************
*/
/**
 * @attention
 * Copyright (c) 2018-2025 AICSemi Ltd. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "netal_sock.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <unistd.h>

#define NETAL_AIC_FDSET_BITS   64
#define NETAL_AIC_FDSET_BYTES  (NETAL_AIC_FDSET_BITS / 8)

#define NETAL_LWIP_MSG_PEEK      0x01
#define NETAL_LWIP_MSG_WAITALL   0x02
#define NETAL_LWIP_MSG_DONTWAIT  0x08
#define NETAL_LWIP_MSG_MORE      0x10
#define NETAL_LWIP_MSG_NOSIGNAL  0x20

static int netal_is_transient_errno(int err)
{
    return err == EAGAIN || err == EINTR;
}

static int netal_translate_msg_flags(int flags)
{
    int native_flags = 0;

    /*
     * The AIC prebuilt objects were built against lwIP/RT-Thread socket
     * constants. NuttX uses different numeric values, so translate here at
     * the ABI boundary instead of letting MSG_PEEK become MSG_OOB.
     */
    if (flags & NETAL_LWIP_MSG_PEEK)
        native_flags |= MSG_PEEK;

#ifdef MSG_WAITALL
    if (flags & NETAL_LWIP_MSG_WAITALL)
        native_flags |= MSG_WAITALL;
#endif

#ifdef MSG_DONTWAIT
    if (flags & NETAL_LWIP_MSG_DONTWAIT)
        native_flags |= MSG_DONTWAIT;
#endif

#ifdef MSG_MORE
    if (flags & NETAL_LWIP_MSG_MORE)
        native_flags |= MSG_MORE;
#endif

#ifdef MSG_NOSIGNAL
    if (flags & NETAL_LWIP_MSG_NOSIGNAL)
        native_flags |= MSG_NOSIGNAL;
#endif

    return native_flags;
}

static int netal_is_printable_data(const void *data, size_t len)
{
    const unsigned char *buf = (const unsigned char *)data;
    size_t i;

    if (buf == NULL || len == 0)
        return 0;

    for (i = 0; i < len; i++) {
        if (buf[i] == '\r' || buf[i] == '\n' || buf[i] == '\t')
            continue;

        if (buf[i] < 0x20 || buf[i] > 0x7e)
            return 0;
    }

    return 1;
}

static void netal_log_data(const char *op, int s, const void *data,
                           size_t len, int ret)
{
    size_t dump_len;

    if (ret <= 0 || !netal_is_printable_data(data, (size_t)ret))
        return;

    dump_len = (size_t)ret;
    if (dump_len > len)
        dump_len = len;
    if (dump_len > 96)
        dump_len = 96;

    printf("[AIC_I]netal_%s fd=%d len=%d data=\"%.*s\"\n",
           op, s, ret, (int)dump_len, (const char *)data);
}

static void netal_log_event_data(const char *op, int s, const void *data,
                                 size_t len, int flags, int native_flags,
                                 int ret)
{
    const unsigned char *buf = (const unsigned char *)data;
    uint16_t msg_len;
    uint16_t msg_id;

    if (ret <= 0 || data == NULL || netal_is_printable_data(data, (size_t)ret))
        return;

    if (len != 8 && len != 16 && len != 20 && len != 32)
        return;

    if (ret < 8) {
        printf("[AIC_I]netal_%s fd=%d req=%u ret=%d flags=0x%x->0x%x "
               "raw=%02x %02x %02x %02x\n",
               op, s, (unsigned int)len, ret, flags, native_flags,
               buf[0], ret > 1 ? buf[1] : 0, ret > 2 ? buf[2] : 0,
               ret > 3 ? buf[3] : 0);
        return;
    }

    msg_len = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
    msg_id = (uint16_t)buf[6] | ((uint16_t)buf[7] << 8);
    printf("[AIC_I]netal_%s fd=%d req=%u ret=%d flags=0x%x->0x%x "
           "hdr_len=%u hdr_id=%u\n",
           op, s, (unsigned int)len, ret, flags, native_flags,
           msg_len, msg_id);
}

static int netal_msghdr_flatten(const struct msghdr *msg, char *buf,
                                size_t buf_size, size_t *total_len)
{
    size_t total = 0;
    unsigned long i;

    if (msg == NULL || msg->msg_iov == NULL || msg->msg_iovlen == 0 ||
        msg->msg_iovlen > 4 || msg->msg_name != NULL ||
        msg->msg_control != NULL)
        return -1;

    for (i = 0; i < msg->msg_iovlen; i++) {
        const struct iovec *iov = &msg->msg_iov[i];

        if (iov->iov_len == 0)
            continue;

        if (iov->iov_base == NULL || iov->iov_len > buf_size - total)
            return -1;

        memcpy(buf + total, iov->iov_base, iov->iov_len);
        total += iov->iov_len;
    }

    if (total == 0)
        return -1;

    *total_len = total;
    return 0;
}

static int netal_aic_fd_isset(const unsigned char *set, int fd)
{
    if (set == NULL || fd < 0 || fd >= NETAL_AIC_FDSET_BITS)
        return 0;

    return (set[fd / 8] & (1 << (fd & 7))) != 0;
}

static void netal_aic_fd_set(unsigned char *set, int fd)
{
    if (set == NULL || fd < 0 || fd >= NETAL_AIC_FDSET_BITS)
        return;

    set[fd / 8] |= 1 << (fd & 7);
}

static void netal_fdset_from_aic(const void *aic_set, fd_set *nuttx_set,
                                 int nfds)
{
    int fd;
    int max_fd = nfds;

    FD_ZERO(nuttx_set);

    if (aic_set == NULL)
        return;

    if (max_fd > NETAL_AIC_FDSET_BITS)
        max_fd = NETAL_AIC_FDSET_BITS;
    if (max_fd > FD_SETSIZE)
        max_fd = FD_SETSIZE;

    for (fd = 0; fd < max_fd; fd++) {
        if (netal_aic_fd_isset((const unsigned char *)aic_set, fd))
            FD_SET(fd, nuttx_set);
    }
}

static void netal_fdset_to_aic(void *aic_set, const fd_set *nuttx_set,
                               int nfds)
{
    int fd;
    int max_fd = nfds;

    if (aic_set == NULL)
        return;

    memset(aic_set, 0, NETAL_AIC_FDSET_BYTES);

    if (nuttx_set == NULL)
        return;

    if (max_fd > NETAL_AIC_FDSET_BITS)
        max_fd = NETAL_AIC_FDSET_BITS;
    if (max_fd > FD_SETSIZE)
        max_fd = FD_SETSIZE;

    for (fd = 0; fd < max_fd; fd++) {
        if (FD_ISSET(fd, nuttx_set))
            netal_aic_fd_set((unsigned char *)aic_set, fd);
    }
}

static void netal_timeval_from_aic(const void *aic_timeout,
                                   struct timeval *nuttx_timeout)
{
    int32_t sec_lo;
    int32_t sec_hi;
    int32_t usec;
    int64_t sec;

    memcpy(&sec_lo, aic_timeout, sizeof(sec_lo));
    memcpy(&sec_hi, (const char *)aic_timeout + 4, sizeof(sec_hi));
    memcpy(&usec, (const char *)aic_timeout + 8, sizeof(usec));

    sec = ((int64_t)sec_hi << 32) | (uint32_t)sec_lo;
    if (sec < 0)
        sec = 0;

    while (usec >= 1000000) {
        usec -= 1000000;
        sec++;
    }

    if (usec < 0)
        usec = 0;

    nuttx_timeout->tv_sec = (time_t)sec;
    nuttx_timeout->tv_usec = usec;
}

static const void *netal_rebuild_ipv4_sockaddr(const void *name,
                                               struct sockaddr_in *storage,
                                               const char *reason)
{
    const struct sockaddr_in *in = (const struct sockaddr_in *)name;
    unsigned int old_family = (unsigned int)in->sin_family;

    memset(storage, 0, sizeof(*storage));
    memcpy(storage, name, sizeof(*storage));
    storage->sin_family = AF_INET;

    printf("[AIC_W]netal: normalize sockaddr family 0x%04x (%s) -> AF_INET\n",
           old_family, reason);

    return storage;
}

static const void *netal_normalize_sockaddr(const void *name,
                                            netal_socklen_t namelen,
                                            struct sockaddr_in *storage)
{
    const struct sockaddr_in *in = (const struct sockaddr_in *)name;
    const unsigned char *raw = (const unsigned char *)name;

    if (name == NULL || namelen < sizeof(struct sockaddr_in) || storage == NULL)
        return name;

    if (in->sin_family == AF_INET)
        return name;

    if (in->sin_family == htons(AF_INET))
        return netal_rebuild_ipv4_sockaddr(name, storage, "network-order");

    if (raw[1] == AF_INET)
        return netal_rebuild_ipv4_sockaddr(name, storage, "byte1-family");

    return name;
}

static void netal_log_sockaddr(const char *op, int s, const void *name,
                               netal_socklen_t namelen, int ret, int err)
{
    if (name != NULL && namelen >= sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)name;

        if (in->sin_family == AF_INET) {
            uint32_t ip = ntohl(in->sin_addr.s_addr);

            printf("[AIC_E]netal_%s fd=%d ret=%d errno=%d "
                   "addr=%lu.%lu.%lu.%lu:%u\n",
                   op, s, ret, err,
                   (unsigned long)((ip >> 24) & 0xff),
                   (unsigned long)((ip >> 16) & 0xff),
                   (unsigned long)((ip >> 8) & 0xff),
                   (unsigned long)(ip & 0xff),
                   (unsigned int)ntohs(in->sin_port));
            return;
        }

        printf("[AIC_E]netal_%s fd=%d ret=%d errno=%d family=%d len=%d "
               "raw=%02x %02x %02x %02x\n",
               op, s, ret, err, in->sin_family, (int)namelen,
               ((const unsigned char *)name)[0],
               ((const unsigned char *)name)[1],
               ((const unsigned char *)name)[2],
               ((const unsigned char *)name)[3]);
        return;
    }

    printf("[AIC_E]netal_%s fd=%d ret=%d errno=%d\n", op, s, ret, err);
}

static int netal_return_with_log(const char *op, int s, int ret)
{
    if (ret < 0) {
        int err = errno;

        if (!netal_is_transient_errno(err))
            netal_log_sockaddr(op, s, NULL, 0, ret, err);
        errno = err;
    }

    return ret;
}

int netal_accept(int s, void *addr, netal_socklen_t *addrlen)
{
    return accept(s, addr, addrlen);
}

int netal_bind(int s, const void *name, netal_socklen_t namelen)
{
    struct sockaddr_in storage;
    const void *addr = netal_normalize_sockaddr(name, namelen, &storage);
    int ret = bind(s, addr, namelen);
    if (ret < 0) {
        int err = errno;
        netal_log_sockaddr("bind", s, addr, namelen, ret, err);
        errno = err;
    }
    return ret;
}

int netal_shutdown(int s, int how)
{
    return shutdown(s, how);
}

int netal_getpeername(int s, void *name, netal_socklen_t *namelen)
{
    return getpeername(s, name, namelen);
}

int netal_getsockname(int s, void *name, netal_socklen_t *namelen)
{
    return getsockname(s, name, namelen);
}

int netal_getsockopt(int s, int level, int optname, void *optval, netal_socklen_t *optlen)
{
    return getsockopt(s, level, optname, optval, optlen);
}

int netal_setsockopt(int s, int level, int optname, const void *optval, netal_socklen_t optlen)
{
    return setsockopt(s, level, optname, optval, optlen);
}

int netal_connect(int s, const void *name, netal_socklen_t namelen)
{
    struct sockaddr_in storage;
    const void *addr = netal_normalize_sockaddr(name, namelen, &storage);
    int ret = connect(s, addr, namelen);
    if (ret < 0) {
        int err = errno;
        netal_log_sockaddr("connect", s, addr, namelen, ret, err);
        errno = err;
    }
    return ret;
}

int netal_listen(int s, int backlog)
{
    return listen(s, backlog);
}

int netal_recv(int s, void *mem, size_t len, int flags)
{
    int native_flags = netal_translate_msg_flags(flags);
    int ret = recv(s, mem, len, native_flags);
    netal_log_data("recv", s, mem, len, ret);
    netal_log_event_data("recv", s, mem, len, flags, native_flags, ret);
    return netal_return_with_log("recv", s, ret);
}

int netal_recvfrom(int s, void *mem, size_t len, int flags,
                   void *from, netal_socklen_t *fromlen)
{
    int native_flags = netal_translate_msg_flags(flags);
    return netal_return_with_log("recvfrom", s,
                                 recvfrom(s, mem, len, native_flags,
                                          from, fromlen));
}

int netal_recvmsg(int s, void *message, int flags)
{
    return netal_return_with_log("recvmsg", s,
                                 recvmsg(s, message,
                                         netal_translate_msg_flags(flags)));
}

int netal_sendmsg(int s, const void *message, int flags)
{
    char flat_buf[256];
    size_t flat_len = 0;
    int native_flags = netal_translate_msg_flags(flags);
    int ret;

    if (netal_msghdr_flatten((const struct msghdr *)message, flat_buf,
                             sizeof(flat_buf), &flat_len) == 0) {
        ret = send(s, flat_buf, flat_len, native_flags);
        netal_log_data("sendmsg_flat", s, flat_buf, flat_len, ret);
        netal_log_event_data("sendmsg_flat", s, flat_buf, flat_len,
                             flags, native_flags, ret);
        return netal_return_with_log("sendmsg_flat", s, ret);
    }

    ret = sendmsg(s, message, native_flags);
    return netal_return_with_log("sendmsg", s, ret);
}

int netal_send(int s, const void *dataptr, size_t size, int flags)
{
    int native_flags = netal_translate_msg_flags(flags);
    int ret = send(s, dataptr, size, native_flags);
    netal_log_data("send", s, dataptr, size, ret);
    netal_log_event_data("send", s, dataptr, size, flags, native_flags, ret);
    return netal_return_with_log("send", s, ret);
}

int netal_sendto(int s, const void *dataptr, size_t size, int flags,
                 const void *to, netal_socklen_t tolen)
{
    struct sockaddr_in storage;
    const void *addr = netal_normalize_sockaddr(to, tolen, &storage);
    int ret = sendto(s, dataptr, size, netal_translate_msg_flags(flags),
                     addr, tolen);
    if (ret < 0) {
        int err = errno;
        netal_log_sockaddr("sendto", s, addr, tolen, ret, err);
        errno = err;
    }
    return ret;
}

int netal_socket(int domain, int type, int protocol)
{
    int ret = socket(domain, type, protocol);
    if (ret < 0) {
        int err = errno;
        printf("[AIC_E]netal_socket domain=%d type=%d protocol=%d errno=%d\n",
               domain, type, protocol, err);
        errno = err;
    }
    return ret;
}

int netal_closesocket(int s)
{
    return close(s);
}

int netal_ioctlsocket(int s, long cmd, void *arg)
{
    return netal_return_with_log("ioctl", s, ioctl(s, cmd, (unsigned long)arg));
}

int netal_select(int nfds, void *readfds, void *writefds, void *exceptfds, void *timeout)
{
    fd_set read_set;
    fd_set write_set;
    fd_set except_set;
    struct timeval timeout_set;
    fd_set *readp = NULL;
    fd_set *writep = NULL;
    fd_set *exceptp = NULL;
    struct timeval *timeoutp = NULL;
    int ret;

    if (readfds != NULL) {
        netal_fdset_from_aic(readfds, &read_set, nfds);
        readp = &read_set;
    }

    if (writefds != NULL) {
        netal_fdset_from_aic(writefds, &write_set, nfds);
        writep = &write_set;
    }

    if (exceptfds != NULL) {
        netal_fdset_from_aic(exceptfds, &except_set, nfds);
        exceptp = &except_set;
    }

    if (timeout != NULL) {
        netal_timeval_from_aic(timeout, &timeout_set);
        timeoutp = &timeout_set;
    }

    ret = select(nfds, readp, writep, exceptp, timeoutp);
    if (ret >= 0) {
        netal_fdset_to_aic(readfds, readp, nfds);
        netal_fdset_to_aic(writefds, writep, nfds);
        netal_fdset_to_aic(exceptfds, exceptp, nfds);
    } else {
        return netal_return_with_log("select", -1, ret);
    }

    if (ret == 0 && timeoutp != NULL) {
        printf("[AIC_W]netal_select timeout nfds=%d timeout=%ld.%06ld\n",
               nfds, (long)timeout_set.tv_sec,
               (long)timeout_set.tv_usec);
    }

    return ret;
}
