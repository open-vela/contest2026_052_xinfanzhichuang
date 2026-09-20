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

#include "llm/llm_proxy.h"
#include "config/config_store.h"
#include "proxy/http_proxy.h"
#include "tls/vela_tls.h"
#include "velaclaw_compat.h"
#include "velaclaw_config.h"

#include "cJSON.h"
#include "mbedtls/base64.h"
#include <stdlib.h>
#include <string.h>

static const char* TAG = "llm";

static char s_api_key[128] = { 0 };
static char s_model[64] = VELACLAW_LLM_DEFAULT_MODEL;
static char s_llm_host[128] = VELACLAW_LLM_API_HOST;
static char s_llm_path[128] = VELACLAW_LLM_API_PATH;
static char s_llm_port[8] = "443"; /* "443" for HTTPS, "80" etc for HTTP */

static pthread_mutex_t s_llm_lock = PTHREAD_MUTEX_INITIALIZER;

static void normalize_mimo_model_for_host(const char *host, const char *model,
    char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }

    if (model == NULL) {
        out[0] = '\0';
        return;
    }

    if (host != NULL && strstr(host, "xiaomimimo.com") != NULL &&
        (strcmp(model, "mimo-v2.5-pro") == 0 ||
         strcmp(model, "mimo-v2-flash") == 0)) {
        snprintf(out, out_size, "%s", "mimo-v2.5");
        return;
    }

    snprintf(out, out_size, "%s", model);
}

/* Check if host uses OpenAI-compatible max_completion_tokens param */
static bool is_openai_compat_host(const char* host)
{
    return strstr(host, "openai.com")
        || strstr(host, "openrouter.ai")
        || strstr(host, "xiaomimimo.com");
}

static bool is_mimo_host(const char* host)
{
    return host != NULL && strstr(host, "xiaomimimo.com") != NULL;
}

static int max_tokens_for_host(const char* host)
{
    if (is_mimo_host(host)) {
        return VELACLAW_LLM_MAX_TOKENS_MIMO;
    }

    return is_openai_compat_host(host)
        ? VELACLAW_LLM_MAX_TOKENS_OPENAI
        : VELACLAW_LLM_MAX_TOKENS;
}

static void add_provider_speed_options(cJSON* body, const char* host)
{
    if (!is_mimo_host(host)) {
        return;
    }

    cJSON* thinking = cJSON_CreateObject();
    if (thinking != NULL) {
        cJSON_AddStringToObject(thinking, "type", "disabled");
        cJSON_AddItemToObject(body, "thinking", thinking);
    }

    cJSON_AddNumberToObject(body, "temperature", 0.3);
}

/* ── Growable response buffer ─────────────────────────────── */

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} resp_buf_t;

static int resp_buf_init(resp_buf_t* rb, size_t initial_cap)
{
    rb->data = calloc(1, initial_cap);
    if (!rb->data)
        return ERROR;
    rb->len = 0;
    rb->cap = initial_cap;
    return OK;
}

static int resp_buf_append(resp_buf_t* rb, const char* data, size_t len)
{
    while (rb->len + len >= rb->cap) {
        size_t new_cap = rb->cap * 2;
        if (new_cap > VELACLAW_LLM_MAX_RESP_SIZE) {
            syslog(LOG_ERR, "llm: resp_buf exceeded %d limit\n",
                VELACLAW_LLM_MAX_RESP_SIZE);
            return ERROR;
        }
        char* tmp = realloc(rb->data, new_cap);
        if (!tmp)
            return ERROR;
        rb->data = tmp;
        rb->cap = new_cap;
    }
    memcpy(rb->data + rb->len, data, len);
    rb->len += len;
    rb->data[rb->len] = '\0';
    return OK;
}

static void resp_buf_free(resp_buf_t* rb)
{
    if (rb->data) {
        free(rb->data);
        rb->data = NULL;
    }
    rb->len = 0;
    rb->cap = 0;
}

/* ── Init ─────────────────────────────────────────────────── */

int llm_proxy_init(void)
{
    if (VELACLAW_SECRET_API_KEY[0])
        strncpy(s_api_key, VELACLAW_SECRET_API_KEY, sizeof(s_api_key) - 1);
    if (VELACLAW_SECRET_MODEL[0])
        strncpy(s_model, VELACLAW_SECRET_MODEL, sizeof(s_model) - 1);
    if (VELACLAW_SECRET_LLM_HOST[0])
        strncpy(s_llm_host, VELACLAW_SECRET_LLM_HOST, sizeof(s_llm_host) - 1);
    if (VELACLAW_SECRET_LLM_PATH[0])
        strncpy(s_llm_path, VELACLAW_SECRET_LLM_PATH, sizeof(s_llm_path) - 1);
    if (VELACLAW_SECRET_LLM_PORT[0])
        strncpy(s_llm_port, VELACLAW_SECRET_LLM_PORT, sizeof(s_llm_port) - 1);

    char tmp[128] = { 0 };
    if (claw_config_get(VELACLAW_CFG_KEY_API_KEY, tmp, sizeof(tmp)) == OK && tmp[0])
        strncpy(s_api_key, tmp, sizeof(s_api_key) - 1);
    memset(tmp, 0, sizeof(tmp));
    if (claw_config_get(VELACLAW_CFG_KEY_MODEL, tmp, sizeof(tmp)) == OK && tmp[0])
        strncpy(s_model, tmp, sizeof(s_model) - 1);

    memset(tmp, 0, sizeof(tmp));
    if (claw_config_get(VELACLAW_CFG_KEY_LLM_HOST, tmp, sizeof(tmp)) == OK && tmp[0])
        strncpy(s_llm_host, tmp, sizeof(s_llm_host) - 1);
    memset(tmp, 0, sizeof(tmp));
    if (claw_config_get(VELACLAW_CFG_KEY_LLM_PATH, tmp, sizeof(tmp)) == OK && tmp[0])
        strncpy(s_llm_path, tmp, sizeof(s_llm_path) - 1);
    memset(tmp, 0, sizeof(tmp));
    if (claw_config_get("llm_port", tmp, sizeof(tmp)) == OK && tmp[0])
        strncpy(s_llm_port, tmp, sizeof(s_llm_port) - 1);

    char normalized_model[sizeof(s_model)];
    normalize_mimo_model_for_host(s_llm_host, s_model,
        normalized_model, sizeof(normalized_model));
    if (strcmp(normalized_model, s_model) != 0) {
        strncpy(s_model, normalized_model, sizeof(s_model) - 1);
        s_model[sizeof(s_model) - 1] = '\0';
        claw_config_set(VELACLAW_CFG_KEY_MODEL, s_model);
    }

    if (s_api_key[0])
        syslog(LOG_INFO, "[%s] LLM proxy initialized (model: %s, host: %s)\n", TAG,
            s_model, s_llm_host);
    else
        syslog(LOG_WARNING,
            "[%s] No API key. Use CLI: velaclaw set_llm <preset> <key>\n",
            TAG);
    return OK;
}

int llm_set_api_key(const char* api_key)
{
    claw_config_set(VELACLAW_CFG_KEY_API_KEY, api_key);
    /* MiMo voice backends reuse the general key unless overridden. */
    if (api_key && api_key[0]) {
        claw_config_set(VELACLAW_CFG_KEY_MIMO_API_KEY, api_key);
    }
    pthread_mutex_lock(&s_llm_lock);
    strncpy(s_api_key, api_key, sizeof(s_api_key) - 1);
    pthread_mutex_unlock(&s_llm_lock);
    syslog(LOG_INFO, "[%s] API key saved\n", TAG);
    return OK;
}

int llm_set_model(const char* model)
{
    char host[sizeof(s_llm_host)];
    char normalized[sizeof(s_model)];

    pthread_mutex_lock(&s_llm_lock);
    memcpy(host, s_llm_host, sizeof(host));
    pthread_mutex_unlock(&s_llm_lock);

    normalize_mimo_model_for_host(host, model, normalized, sizeof(normalized));
    claw_config_set(VELACLAW_CFG_KEY_MODEL, normalized);
    pthread_mutex_lock(&s_llm_lock);
    strncpy(s_model, normalized, sizeof(s_model) - 1);
    pthread_mutex_unlock(&s_llm_lock);
    syslog(LOG_INFO, "[%s] Model set to: %s\n", TAG, s_model);
    return OK;
}

int llm_set_backend(const char* host, const char* path)
{
    pthread_mutex_lock(&s_llm_lock);
    if (host && host[0]) {
        claw_config_set(VELACLAW_CFG_KEY_LLM_HOST, host);
        strncpy(s_llm_host, host, sizeof(s_llm_host) - 1);
    }
    if (path && path[0]) {
        claw_config_set(VELACLAW_CFG_KEY_LLM_PATH, path);
        strncpy(s_llm_path, path, sizeof(s_llm_path) - 1);
    }
    pthread_mutex_unlock(&s_llm_lock);
    syslog(LOG_INFO, "[%s] LLM backend: %s%s\n", TAG, s_llm_host, s_llm_path);
    return OK;
}

int llm_set_port(const char* port)
{
    if (port && port[0]) {
        claw_config_set("llm_port", port);
        strncpy(s_llm_port, port, sizeof(s_llm_port) - 1);
        s_llm_port[sizeof(s_llm_port) - 1] = '\0';
    }
    return OK;
}

int llm_set_all(const char* host, const char* path,
    const char* port, const char* api_key, const char* model)
{
    pthread_mutex_lock(&s_llm_lock);

    if (host && host[0]) {
        claw_config_set(VELACLAW_CFG_KEY_LLM_HOST, host);
        strncpy(s_llm_host, host, sizeof(s_llm_host) - 1);
        s_llm_host[sizeof(s_llm_host) - 1] = '\0';
    }
    if (path && path[0]) {
        claw_config_set(VELACLAW_CFG_KEY_LLM_PATH, path);
        strncpy(s_llm_path, path, sizeof(s_llm_path) - 1);
        s_llm_path[sizeof(s_llm_path) - 1] = '\0';
    }
    if (port && port[0]) {
        claw_config_set("llm_port", port);
        strncpy(s_llm_port, port, sizeof(s_llm_port) - 1);
        s_llm_port[sizeof(s_llm_port) - 1] = '\0';
    }
    if (is_mimo_host(s_llm_host)) {
        claw_config_set(VELACLAW_CFG_KEY_MIMO_HOST, s_llm_host);
        claw_config_set(VELACLAW_CFG_KEY_MIMO_PATH, s_llm_path);
        claw_config_set(VELACLAW_CFG_KEY_MIMO_PORT, s_llm_port);
    }
    if (api_key && api_key[0]) {
        claw_config_set(VELACLAW_CFG_KEY_API_KEY, api_key);
        if (strstr(s_llm_host, "xiaomimimo.com") != NULL) {
            claw_config_set(VELACLAW_CFG_KEY_MIMO_API_KEY, api_key);
        }
        strncpy(s_api_key, api_key, sizeof(s_api_key) - 1);
        s_api_key[sizeof(s_api_key) - 1] = '\0';
    }
    if (model && model[0]) {
        char normalized[sizeof(s_model)];
        normalize_mimo_model_for_host(s_llm_host, model, normalized,
            sizeof(normalized));
        claw_config_set(VELACLAW_CFG_KEY_MODEL, normalized);
        strncpy(s_model, normalized, sizeof(s_model) - 1);
        s_model[sizeof(s_model) - 1] = '\0';
    }

    pthread_mutex_unlock(&s_llm_lock);

    syslog(LOG_INFO, "[%s] LLM config updated atomically: %s%s (model: %s)\n",
        TAG, s_llm_host, s_llm_path, s_model);
    return OK;
}

/* ── HTTP helpers ─────────────────────────────────────────── */

/**
 * Return the model name to send in the API request.
 * Multi-provider gateways (e.g. OpenRouter) require the full
 * "provider/model" string, while single-vendor APIs expect only
 * the bare model name after the slash.
 */
static const char* model_name_for_api(const char* model, const char* host)
{
    /* Multi-provider gateways need the full identifier */
    if (strstr(host, "openrouter.ai")) {
        return model;
    }

    const char* slash = strchr(model, '/');
    return slash ? slash + 1 : model;
}

/* Direct path via vela_tls */
static int llm_http_direct(const char* post_data, resp_buf_t* rb,
    int* out_status)
{
    /* Snapshot config under lock */
    char api_key[128], llm_host[128], llm_path[128], llm_port[8], model[64];
    pthread_mutex_lock(&s_llm_lock);
    memcpy(api_key, s_api_key, sizeof(api_key));
    memcpy(llm_host, s_llm_host, sizeof(llm_host));
    memcpy(llm_path, s_llm_path, sizeof(llm_path));
    memcpy(llm_port, s_llm_port, sizeof(llm_port));
    memcpy(model, s_model, sizeof(model));
    pthread_mutex_unlock(&s_llm_lock);

    /* Allocate a large temporary buffer for the raw HTTP response */
    size_t raw_cap = VELACLAW_LLM_STREAM_BUF_SIZE;
    char* raw_buf = calloc(1, raw_cap);
    if (!raw_buf)
        return ERROR;

    /* OpenAI-compatible format: Authorization: Bearer <key> */
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);

    /* Extract provider ID from model string (e.g. "xiaomi/mimo-claw-0301" →
     * "xiaomi") Mify gateway requires X-Model-Provider-Id header for routing. */
    char provider[64] = { 0 };
    const char* slash = strchr(model, '/');
    if (slash) {
        size_t plen = (size_t)(slash - model);
        if (plen >= sizeof(provider))
            plen = sizeof(provider) - 1;
        memcpy(provider, model, plen);
    }

    vela_header_t hdrs[] = { { "Authorization", auth_header },
        { "Accept-Encoding", "identity" },
        { is_mimo_host(llm_host) ? NULL : "api-key",
            is_mimo_host(llm_host) ? NULL : api_key },
        { provider[0] ? "X-Model-Provider-Id" : NULL,
            provider[0] ? provider : NULL },
        { NULL, NULL } };

    int status;
    int use_tls = (strcmp(llm_port, "443") == 0);

    if (use_tls) {
        status = vela_https_post_json(llm_host, llm_port, llm_path, hdrs, post_data,
            raw_buf, raw_cap);
    } else {
        status = vela_http_post_json(llm_host, llm_port, llm_path, hdrs, post_data,
            raw_buf, raw_cap);
    }

    if (status < 0) {
        syslog(LOG_ERR,
            "[%s] LLM HTTP request failed: status=%d host=%s port=%s path=%s\n",
            TAG, status, llm_host, llm_port, llm_path);
        free(raw_buf);
        return ERROR;
    }

    /* Copy into growable resp_buf so caller can use it uniformly */
    size_t body_len = strlen(raw_buf);
    if (resp_buf_init(rb, body_len + 1) != OK) {
        free(raw_buf);
        return ERROR;
    }
    resp_buf_append(rb, raw_buf, body_len);
    free(raw_buf);

    *out_status = status;
    return OK;
}

/* Proxy path via CONNECT tunnel */
static int llm_http_via_proxy(const char* post_data, resp_buf_t* rb,
    int* out_status)
{
    /* Snapshot config under lock */
    char api_key[128], llm_host[128], llm_path[128], llm_port[8], model[64];
    pthread_mutex_lock(&s_llm_lock);
    memcpy(api_key, s_api_key, sizeof(api_key));
    memcpy(llm_host, s_llm_host, sizeof(llm_host));
    memcpy(llm_path, s_llm_path, sizeof(llm_path));
    memcpy(llm_port, s_llm_port, sizeof(llm_port));
    memcpy(model, s_model, sizeof(model));
    pthread_mutex_unlock(&s_llm_lock);

    int port = atoi(llm_port);
    if (port <= 0)
        port = 443;

    proxy_conn_t* conn = proxy_conn_open(llm_host, port, 30000);
    if (!conn)
        return ERROR;

    int body_len = (int)strlen(post_data);
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);

    /* Extract provider ID from model (e.g. "xiaomi/mimo-claw-0301" → "xiaomi") */
    char provider_hdr[128] = { 0 };
    const char* slash = strchr(model, '/');
    if (slash) {
        char provider[64] = { 0 };
        size_t plen = (size_t)(slash - model);
        if (plen >= sizeof(provider))
            plen = sizeof(provider) - 1;
        memcpy(provider, model, plen);
        snprintf(provider_hdr, sizeof(provider_hdr), "X-Model-Provider-Id: %s\r\n",
            provider);
    }

    char header[1024];
    int hlen = snprintf(header, sizeof(header),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Authorization: %s\r\n"
        "api-key: %s\r\n"
        "%s"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        llm_path, llm_host, auth_header, api_key, provider_hdr, body_len);

    if (proxy_conn_write(conn, header, hlen) < 0 || proxy_conn_write(conn, post_data, body_len) < 0) {
        proxy_conn_close(conn);
        return ERROR;
    }

    if (resp_buf_init(rb, VELACLAW_LLM_STREAM_BUF_SIZE) != OK) {
        proxy_conn_close(conn);
        return ERROR;
    }

    char tmp[4096];
    while (1) {
        int n = proxy_conn_read(conn, tmp, sizeof(tmp), 120000);
        if (n <= 0)
            break;
        if (resp_buf_append(rb, tmp, (size_t)n) != OK) {
            syslog(LOG_ERR, "[%s] resp_buf_append OOM, truncating\n", TAG);
            break;
        }
    }
    proxy_conn_close(conn);

    /* Parse status from raw HTTP response */
    *out_status = 0;
    if (rb->len > 5 && strncmp(rb->data, "HTTP/", 5) == 0) {
        const char* sp = strchr(rb->data, ' ');
        if (sp)
            *out_status = atoi(sp + 1);
    }

    /* Strip HTTP header, keep body */
    char* body = strstr(rb->data, "\r\n\r\n");
    if (body) {
        body += 4;
        size_t blen = rb->len - (size_t)(body - rb->data);
        memmove(rb->data, body, blen);
        rb->len = blen;
        rb->data[rb->len] = '\0';
    }

    return OK;
}

static int llm_http_call(const char* post_data, resp_buf_t* rb,
    int* out_status)
{
    /* For plain HTTP endpoints (port != 443), always use direct path.
     * The proxy does CONNECT + TLS which fails on non-TLS endpoints.
     * Non-TLS HTTP endpoints (port != 443) are reachable
     * directly without a proxy anyway. */
    int use_tls;
    pthread_mutex_lock(&s_llm_lock);
    use_tls = (strcmp(s_llm_port, "443") == 0);
    pthread_mutex_unlock(&s_llm_lock);

    if (use_tls && http_proxy_is_enabled())
        return llm_http_via_proxy(post_data, rb, out_status);
    return llm_http_direct(post_data, rb, out_status);
}

/* ── JSON helpers ─────────────────────────────────────────── */

/* Extract text from OpenAI response format: choices[0].message.content */
static void extract_text(cJSON* root, char* buf, size_t size)
{
    buf[0] = '\0';
    cJSON* choices = cJSON_GetObjectItem(root, "choices");
    if (!choices || !cJSON_IsArray(choices))
        return;

    cJSON* first = choices->child;
    if (!first)
        return;

    cJSON* message = cJSON_GetObjectItem(first, "message");
    if (!message)
        return;

    cJSON* content = cJSON_GetObjectItem(message, "content");
    if (!content || !cJSON_IsString(content))
        return;

    size_t tlen = strlen(content->valuestring);
    size_t copy = (tlen < size - 1) ? tlen : size - 1;
    memcpy(buf, content->valuestring, copy);
    buf[copy] = '\0';
}

/* ── Public: simple chat ──────────────────────────────────── */

int llm_chat(const char* system_prompt, const char* messages_json,
    char* response_buf, size_t buf_size)
{
    /* Snapshot config under lock */
    char model[64], api_key[128], llm_host[128];
    pthread_mutex_lock(&s_llm_lock);
    memcpy(model, s_model, sizeof(model));
    memcpy(api_key, s_api_key, sizeof(api_key));
    memcpy(llm_host, s_llm_host, sizeof(llm_host));
    pthread_mutex_unlock(&s_llm_lock);

    if (api_key[0] == '\0') {
        snprintf(response_buf, buf_size, "Error: No API key configured");
        return ERROR;
    }

    cJSON* body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model",
        model_name_for_api(model, llm_host));

    if (is_openai_compat_host(llm_host))
        cJSON_AddNumberToObject(body, "max_completion_tokens",
            max_tokens_for_host(llm_host));
    else
        cJSON_AddNumberToObject(body, "max_tokens",
            max_tokens_for_host(llm_host));
    add_provider_speed_options(body, llm_host);

    cJSON* messages = cJSON_Parse(messages_json);
    if (!messages)
        messages = cJSON_CreateArray();

    /* Prepend system message (OpenAI format) */
    cJSON* sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", system_prompt);
    cJSON_InsertItemInArray(messages, 0, sys_msg);

    cJSON_AddItemToObject(body, "messages", messages);

    char* post_data = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!post_data) {
        snprintf(response_buf, buf_size, "Error: Failed to build request");
        return ERROR;
    }

    syslog(LOG_INFO, "[%s] Calling LLM API (model: %s, host: %s, %d bytes)\n",
        TAG, model, llm_host, (int)strlen(post_data));

    resp_buf_t rb = { 0 };
    int status = 0;
    int err = ERROR;
    int retry;

    for (retry = 0; retry <= VELACLAW_LLM_MAX_RETRIES; retry++) {
        if (retry > 0) {
            unsigned int delay = VELACLAW_LLM_RETRY_BASE_SEC << (retry - 1);
            syslog(LOG_WARNING, "[%s] Rate limited (429), retry %d/%d after %us\n",
                TAG, retry, VELACLAW_LLM_MAX_RETRIES, delay);
            sleep(delay);
        }

        rb.len = 0;
        status = 0;
        err = llm_http_call(post_data, &rb, &status);

        if (err != OK) {
            resp_buf_free(&rb);
            free(post_data);
            snprintf(response_buf, buf_size, "Error: HTTP request failed");
            return err;
        }

        if (status != 429)
            break;

        resp_buf_free(&rb);
        memset(&rb, 0, sizeof(rb));
    }

    free(post_data);

    if (status != 200) {
        snprintf(response_buf, buf_size, "API error (HTTP %d): %.200s", status,
            rb.data ? rb.data : "");
        resp_buf_free(&rb);
        return ERROR;
    }

    cJSON* root = cJSON_Parse(rb.data);
    resp_buf_free(&rb);

    if (!root) {
        snprintf(response_buf, buf_size, "Error: Failed to parse response");
        return ERROR;
    }

    extract_text(root, response_buf, buf_size);
    cJSON_Delete(root);

    if (response_buf[0] == '\0')
        snprintf(response_buf, buf_size, "No response from LLM API");
    else
        syslog(LOG_INFO, "[%s] LLM response: %d bytes\n", TAG,
            (int)strlen(response_buf));

    return OK;
}

/* ── Public: chat with tools ──────────────────────────────── */

/* ── Tools format conversion ──────────────────────────────── */

/* Convert internal tools JSON to OpenAI function-calling format.
 * Returns a cJSON array to attach to the request body, or NULL. */
static cJSON* build_openai_tools_array(const char* tools_json)
{
    if (!tools_json) {
        return NULL;
    }

    cJSON* tools_spec = cJSON_Parse(tools_json);

    if (!tools_spec || !cJSON_IsArray(tools_spec)) {
        cJSON_Delete(tools_spec);
        return NULL;
    }

    cJSON* tools_arr = cJSON_CreateArray();
    cJSON* tool_def;

    cJSON_ArrayForEach(tool_def, tools_spec)
    {
        cJSON* wrapper = cJSON_CreateObject();

        cJSON_AddStringToObject(wrapper, "type", "function");

        cJSON* func = cJSON_CreateObject();
        cJSON* name = cJSON_GetObjectItem(tool_def, "name");
        cJSON* desc = cJSON_GetObjectItem(tool_def, "description");
        cJSON* schema = cJSON_GetObjectItem(tool_def, "input_schema");

        if (name && cJSON_IsString(name)) {
            cJSON_AddStringToObject(func, "name", name->valuestring);
        }
        if (desc && cJSON_IsString(desc)) {
            cJSON_AddStringToObject(func, "description",
                desc->valuestring);
        }
        if (schema) {
            cJSON_AddItemToObject(func, "parameters",
                cJSON_Duplicate(schema, 1));
        } else {
            cJSON* empty = cJSON_CreateObject();
            cJSON_AddStringToObject(empty, "type", "object");
            cJSON_AddItemToObject(empty, "properties",
                cJSON_CreateObject());
            cJSON_AddItemToObject(func, "parameters", empty);
        }

        cJSON_AddItemToObject(wrapper, "function", func);
        cJSON_AddItemToArray(tools_arr, wrapper);
    }

    cJSON_Delete(tools_spec);
    return tools_arr;
}

/* ── XML fallback parsers ─────────────────────────────────── */

/* Parse Format 1: <tool_call> <function=NAME> <parameter=KEY>VAL
 * </parameter> </function> </tool_call> */
static void __attribute__((unused)) parse_xml_tool_calls(llm_response_t* resp)
{
    if (resp->call_count > 0 || !resp->text) {
        return;
    }
    if (!strstr(resp->text, "<tool_call>")) {
        return;
    }

    const char* p = resp->text;

    while (resp->call_count < VELACLAW_MAX_TOOL_CALLS) {
        const char* tc_start = strstr(p, "<tool_call>");
        const char* tc_end = strstr(p, "</tool_call>");

        if (!tc_start || !tc_end || tc_end <= tc_start) {
            break;
        }

        const char* fn_start = strstr(tc_start, "<function=");
        const char* fn_close = fn_start
            ? strchr(fn_start + 10, '>')
            : NULL;

        if (!fn_start || !fn_close || fn_start > tc_end) {
            p = tc_end + 12;
            continue;
        }

        llm_tool_call_t* call = &resp->calls[resp->call_count];
        size_t name_len = (size_t)(fn_close - (fn_start + 10));

        if (name_len >= sizeof(call->name)) {
            name_len = sizeof(call->name) - 1;
        }
        memcpy(call->name, fn_start + 10, name_len);
        call->name[name_len] = '\0';
        snprintf(call->id, sizeof(call->id), "xml_%d",
            resp->call_count);

        cJSON* args = cJSON_CreateObject();
        const char* pp = fn_close + 1;

        while (pp < tc_end) {
            const char* ps = strstr(pp, "<parameter=");

            if (!ps || ps >= tc_end) {
                break;
            }
            const char* ke = strchr(ps + 11, '>');

            if (!ke || ke >= tc_end) {
                break;
            }

            char key[64];
            size_t klen = (size_t)(ke - (ps + 11));

            if (klen >= sizeof(key)) {
                klen = sizeof(key) - 1;
            }
            memcpy(key, ps + 11, klen);
            key[klen] = '\0';

            const char* vs = ke + 1;
            const char* ve = strstr(vs, "</parameter>");

            if (!ve || ve > tc_end) {
                break;
            }

            size_t vlen = (size_t)(ve - vs);
            char* val = calloc(1, vlen + 1);

            if (val) {
                memcpy(val, vs, vlen);
                cJSON_AddStringToObject(args, key, val);
                free(val);
            }
            pp = ve + 12;
        }

        char* args_str = cJSON_PrintUnformatted(args);

        cJSON_Delete(args);
        if (args_str) {
            call->input = args_str;
            call->input_len = strlen(args_str);
        }

        resp->call_count++;
        p = tc_end + 12;
    }

    if (resp->call_count > 0) {
        resp->tool_use = true;
        free(resp->text);
        resp->text = NULL;
        resp->text_len = 0;
        syslog(LOG_WARNING,
            "[%s] Parsed %d tool call(s) from XML fallback\n",
            TAG, resp->call_count);
    }
}

/* Parse Format 2: <PREFIX:tool_call> <invoke name="NAME">
 * <parameter name="KEY">VAL</parameter> </invoke>
 * </PREFIX:tool_call> */
static void __attribute__((unused)) parse_ns_xml_tool_calls(llm_response_t* resp)
{
    if (resp->call_count > 0 || !resp->text) {
        return;
    }
    if (!strstr(resp->text, ":tool_call>")) {
        return;
    }

    const char* p = resp->text;

    while (resp->call_count < VELACLAW_MAX_TOOL_CALLS) {
        const char* colon = strstr(p, ":tool_call>");

        if (!colon) {
            break;
        }

        /* Walk back to find '<' */
        const char* tc_start = colon;

        while (tc_start > p && *(tc_start - 1) != '<') {
            tc_start--;
        }
        if (tc_start <= p || *(tc_start - 1) != '<') {
            break;
        }
        tc_start--;

        size_t prefix_len = (size_t)(colon - (tc_start + 1));

        if (prefix_len == 0 || prefix_len > 31) {
            p = colon + 11;
            continue;
        }

        char prefix[32];

        memcpy(prefix, tc_start + 1, prefix_len);
        prefix[prefix_len] = '\0';

        char close_tag[64];

        snprintf(close_tag, sizeof(close_tag),
            "</%s:tool_call>", prefix);

        const char* tc_end = strstr(colon, close_tag);

        if (!tc_end || tc_end <= tc_start) {
            p = colon + 11;
            continue;
        }

        const char* inv = strstr(tc_start, "<invoke name=\"");

        if (!inv || inv > tc_end) {
            p = tc_end + strlen(close_tag);
            continue;
        }

        const char* ns = inv + 14;
        const char* ne = strchr(ns, '"');

        if (!ne || ne > tc_end) {
            p = tc_end + strlen(close_tag);
            continue;
        }

        llm_tool_call_t* call = &resp->calls[resp->call_count];
        size_t name_len = (size_t)(ne - ns);

        if (name_len >= sizeof(call->name)) {
            name_len = sizeof(call->name) - 1;
        }
        memcpy(call->name, ns, name_len);
        call->name[name_len] = '\0';
        snprintf(call->id, sizeof(call->id), "nsxml_%d",
            resp->call_count);

        cJSON* args = cJSON_CreateObject();
        const char* pp = ne;

        while (pp < tc_end) {
            const char* ps = strstr(pp, "<parameter name=\"");

            if (!ps || ps >= tc_end) {
                break;
            }

            const char* ks = ps + 17;
            const char* ke = strchr(ks, '"');

            if (!ke || ke >= tc_end) {
                break;
            }

            const char* tc = strchr(ke, '>');

            if (!tc || tc >= tc_end) {
                break;
            }

            char key[64];
            size_t klen = (size_t)(ke - ks);

            if (klen >= sizeof(key)) {
                klen = sizeof(key) - 1;
            }
            memcpy(key, ks, klen);
            key[klen] = '\0';

            const char* vs = tc + 1;
            const char* ve = strstr(vs, "</parameter>");

            if (!ve || ve > tc_end) {
                break;
            }

            size_t vlen = (size_t)(ve - vs);
            char* val = calloc(1, vlen + 1);

            if (val) {
                memcpy(val, vs, vlen);
                cJSON_AddStringToObject(args, key, val);
                free(val);
            }
            pp = ve + 12;
        }

        char* args_str = cJSON_PrintUnformatted(args);

        cJSON_Delete(args);
        if (args_str) {
            call->input = args_str;
            call->input_len = strlen(args_str);
        }

        resp->call_count++;
        p = tc_end + strlen(close_tag);
    }

    if (resp->call_count > 0) {
        resp->tool_use = true;
        free(resp->text);
        resp->text = NULL;
        resp->text_len = 0;
        syslog(LOG_WARNING,
            "[%s] Parsed %d tool call(s) from namespaced XML\n",
            TAG, resp->call_count);
    }
}

/* ── Extract OpenAI tool_calls from response message ──────── */

static void __attribute__((unused)) extract_openai_tool_calls(cJSON* message,
    llm_response_t* resp)
{
    /* Text content */
    cJSON* text_content = cJSON_GetObjectItem(message, "content");

    if (text_content && cJSON_IsString(text_content)
        && text_content->valuestring) {
        size_t tlen = strlen(text_content->valuestring);

        resp->text = calloc(1, tlen + 1);
        if (resp->text) {
            memcpy(resp->text, text_content->valuestring, tlen);
            resp->text_len = tlen;
        }
    }

    /* Kimi thinking mode: preserve reasoning_content */
    cJSON* rc = cJSON_GetObjectItem(message, "reasoning_content");

    if (rc && cJSON_IsString(rc) && rc->valuestring
        && rc->valuestring[0]) {
        resp->reasoning_content = strdup(rc->valuestring);
    }

    /* Tool calls array */
    cJSON* tool_calls = cJSON_GetObjectItem(message, "tool_calls");

    if (!tool_calls || !cJSON_IsArray(tool_calls)) {
        return;
    }

    cJSON* tc;

    cJSON_ArrayForEach(tc, tool_calls)
    {
        if (resp->call_count >= VELACLAW_MAX_TOOL_CALLS) {
            break;
        }

        llm_tool_call_t* call = &resp->calls[resp->call_count];
        cJSON* id_item = cJSON_GetObjectItem(tc, "id");

        if (id_item && cJSON_IsString(id_item)) {
            strncpy(call->id, id_item->valuestring,
                sizeof(call->id) - 1);
        }

        cJSON* func_obj = cJSON_GetObjectItem(tc, "function");

        if (func_obj) {
            cJSON* n = cJSON_GetObjectItem(func_obj, "name");
            cJSON* a = cJSON_GetObjectItem(func_obj, "arguments");

            if (n && cJSON_IsString(n)) {
                strncpy(call->name, n->valuestring,
                    sizeof(call->name) - 1);
            }
            if (a && cJSON_IsString(a)) {
                size_t alen = strlen(a->valuestring);

                call->input = calloc(1, alen + 1);
                if (call->input) {
                    memcpy(call->input, a->valuestring, alen);
                    call->input_len = alen;
                }
            }
        }

        resp->call_count++;
    }

    if (resp->call_count > 0) {
        resp->tool_use = true;
    }
}

void llm_response_free(llm_response_t* resp)
{
    free(resp->text);
    resp->text = NULL;
    resp->text_len = 0;
    free(resp->reasoning_content);
    resp->reasoning_content = NULL;
    for (int i = 0; i < resp->call_count; i++) {
        free(resp->calls[i].input);
        resp->calls[i].input = NULL;
    }
    resp->call_count = 0;
    resp->tool_use = false;
}

int llm_chat_tools(const char* system_prompt, cJSON* messages,
    const char* tools_json, llm_response_t* resp)
{
    memset(resp, 0, sizeof(*resp));

    /* Snapshot config under lock */
    char model[64];
    char api_key[128];
    char llm_host[128];

    pthread_mutex_lock(&s_llm_lock);
    memcpy(model, s_model, sizeof(model));
    memcpy(api_key, s_api_key, sizeof(api_key));
    memcpy(llm_host, s_llm_host, sizeof(llm_host));
    pthread_mutex_unlock(&s_llm_lock);

    if (api_key[0] == '\0') {
        return ERROR;
    }

    cJSON* body = cJSON_CreateObject();

    cJSON_AddStringToObject(body, "model",
        model_name_for_api(model, llm_host));

    if (is_openai_compat_host(llm_host)) {
        cJSON_AddNumberToObject(body, "max_completion_tokens",
            max_tokens_for_host(llm_host));
    } else {
        cJSON_AddNumberToObject(body, "max_tokens",
            max_tokens_for_host(llm_host));
    }
    add_provider_speed_options(body, llm_host);

    /* Clone messages and prepend system message */
    cJSON* msgs = cJSON_Duplicate(messages, 1);
    cJSON* sys_msg = cJSON_CreateObject();

    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", system_prompt);
    cJSON_InsertItemInArray(msgs, 0, sys_msg);
    cJSON_AddItemToObject(body, "messages", msgs);

    /* Convert tools to OpenAI format */
    cJSON* tools_arr = build_openai_tools_array(tools_json);

    if (tools_arr) {
        cJSON_AddItemToObject(body, "tools", tools_arr);
    }

    char* post_data = cJSON_PrintUnformatted(body);

    cJSON_Delete(body);
    if (!post_data) {
        return ERROR;
    }

    syslog(LOG_INFO,
        "[%s] OpenAI API with tools (model: %s, %d bytes)\n",
        TAG, model, (int)strlen(post_data));

    resp_buf_t rb = { 0 };
    int status = 0;
    int err = ERROR;
    int retry;

    for (retry = 0; retry <= VELACLAW_LLM_MAX_RETRIES; retry++) {
        if (retry > 0) {
            unsigned int delay = VELACLAW_LLM_RETRY_BASE_SEC << (retry - 1);
            syslog(LOG_WARNING, "[%s] Rate limited (429), retry %d/%d after %us\n",
                TAG, retry, VELACLAW_LLM_MAX_RETRIES, delay);
            sleep(delay);
        }

        rb.len = 0;
        status = 0;
        err = llm_http_call(post_data, &rb, &status);

        if (err != OK) {
            resp_buf_free(&rb);
            free(post_data);
            return err;
        }

        if (status != 429)
            break;

        resp_buf_free(&rb);
        memset(&rb, 0, sizeof(rb));
    }

    free(post_data);

    if (status != 200) {
        syslog(LOG_ERR, "[%s] API error %d: %.500s\n", TAG, status,
            rb.data ? rb.data : "");
        resp_buf_free(&rb);
        return ERROR;
    }

    cJSON* root = cJSON_Parse(rb.data);

    resp_buf_free(&rb);
    if (!root) {
        syslog(LOG_ERR, "[%s] Failed to parse API JSON\n", TAG);
        return ERROR;
    }

    /* Extract from OpenAI response */
    cJSON* choices = cJSON_GetObjectItem(root, "choices");

    if (!choices || !cJSON_IsArray(choices) || !choices->child) {
        cJSON_Delete(root);
        return ERROR;
    }

    cJSON* choice = choices->child;
    cJSON* finish = cJSON_GetObjectItem(choice, "finish_reason");

    resp->tool_use = (finish && cJSON_IsString(finish)
        && strcmp(finish->valuestring, "tool_calls") == 0);

    cJSON* message = cJSON_GetObjectItem(choice, "message");

    if (message) {
        cJSON* text_content = cJSON_GetObjectItem(message, "content");
        if (text_content && cJSON_IsString(text_content) && text_content->valuestring) {
            size_t tlen = strlen(text_content->valuestring);
            resp->text = calloc(1, tlen + 1);
            if (resp->text) {
                memcpy(resp->text, text_content->valuestring, tlen);
                resp->text_len = tlen;
            }
        }

        /* Kimi thinking mode: preserve reasoning_content so it can be echoed
         * back in the next turn (required by the API). */
        cJSON* rc = cJSON_GetObjectItem(message, "reasoning_content");
        if (rc && cJSON_IsString(rc) && rc->valuestring && rc->valuestring[0]) {
            resp->reasoning_content = strdup(rc->valuestring);
        }

        /* Extract tool calls from OpenAI format */
        cJSON* tool_calls = cJSON_GetObjectItem(message, "tool_calls");
        if (tool_calls && cJSON_IsArray(tool_calls)) {
            cJSON* tc;
            cJSON_ArrayForEach(tc, tool_calls)
            {
                if (resp->call_count >= VELACLAW_MAX_TOOL_CALLS)
                    break;

                llm_tool_call_t* call = &resp->calls[resp->call_count];

                /* OpenAI tool_calls format:
                   { "id": "call_xyz", "type": "function",
                     "function": { "name": "foo", "arguments": "{...}" }
                   }
                */
                cJSON* id_item = cJSON_GetObjectItem(tc, "id");
                if (id_item && cJSON_IsString(id_item))
                    strncpy(call->id, id_item->valuestring, sizeof(call->id) - 1);

                cJSON* func_obj = cJSON_GetObjectItem(tc, "function");
                if (func_obj) {
                    cJSON* name_item = cJSON_GetObjectItem(func_obj, "name");
                    cJSON* args_item = cJSON_GetObjectItem(func_obj, "arguments");

                    if (name_item && cJSON_IsString(name_item))
                        strncpy(call->name, name_item->valuestring, sizeof(call->name) - 1);

                    if (args_item && cJSON_IsString(args_item)) {
                        size_t alen = strlen(args_item->valuestring);
                        call->input = calloc(1, alen + 1);
                        if (call->input) {
                            memcpy(call->input, args_item->valuestring, alen);
                            call->input_len = alen;
                        }
                    } else if (args_item && cJSON_IsObject(args_item)) {
                        /* Some models return arguments as a JSON object
                         * instead of a JSON string — serialize it. */
                        char* serialized = cJSON_PrintUnformatted(args_item);
                        if (serialized) {
                            call->input = serialized;
                            call->input_len = strlen(serialized);
                        }
                    }
                }

                resp->call_count++;
            }

            /* If we found tool calls, ensure tool_use flag is set
             * (some models/proxies return stop_reason="end_turn" even with tools) */
            if (resp->call_count > 0) {
                resp->tool_use = true;
            }
        }
    }

    /* Extract token usage from API response */
    cJSON* usage = cJSON_GetObjectItem(root, "usage");
    if (usage) {
        cJSON* pt = cJSON_GetObjectItem(usage, "prompt_tokens");
        cJSON* ct = cJSON_GetObjectItem(usage, "completion_tokens");
        cJSON* tt = cJSON_GetObjectItem(usage, "total_tokens");
        if (pt && cJSON_IsNumber(pt))
            resp->prompt_tokens = (int)pt->valuedouble;
        if (ct && cJSON_IsNumber(ct))
            resp->completion_tokens = (int)ct->valuedouble;
        if (tt && cJSON_IsNumber(tt))
            resp->total_tokens = (int)tt->valuedouble;
    }

    /* XML fallback parsers for non-standard models */
    parse_xml_tool_calls(resp);
    parse_ns_xml_tool_calls(resp);

    cJSON_Delete(root);

    syslog(LOG_INFO,
        "[%s] Response: %d bytes text, %d tool calls, finish=%s\n",
        TAG, (int)resp->text_len, resp->call_count,
        resp->tool_use ? "tool_calls" : "end_turn");

    return OK;
}

/* ── Public: vision chat (text + image) ───────────────────── */

int llm_chat_vision(const char* prompt, const char* image_b64,
    const char* mime_type, char* response_buf,
    size_t buf_size)
{
    char model[64], api_key[128], llm_host[128];
    pthread_mutex_lock(&s_llm_lock);
    memcpy(model, s_model, sizeof(model));
    memcpy(api_key, s_api_key, sizeof(api_key));
    memcpy(llm_host, s_llm_host, sizeof(llm_host));
    pthread_mutex_unlock(&s_llm_lock);

    if (api_key[0] == '\0') {
        snprintf(response_buf, buf_size, "Error: No API key configured");
        return ERROR;
    }

    if (!mime_type || !mime_type[0])
        mime_type = "image/jpeg";

    /* Build OpenAI Vision content array:
     * [{"type":"text","text":"..."},
     *  {"type":"image_url","image_url":{"url":"data:<mime>;base64,..."}}]
     */
    cJSON* content_arr = cJSON_CreateArray();

    cJSON* text_part = cJSON_CreateObject();
    cJSON_AddStringToObject(text_part, "type", "text");
    cJSON_AddStringToObject(text_part, "text",
        prompt ? prompt : VELACLAW_VISION_DEFAULT_PROMPT);
    cJSON_AddItemToArray(content_arr, text_part);

    /* Build data URI — "data:<mime>;base64," + b64 string */
    size_t b64_len = strlen(image_b64);
    size_t prefix_len = 5 + strlen(mime_type) + 8; /* "data:" + mime + ";base64," */
    size_t uri_len = prefix_len + b64_len + 1;
    char* data_uri = malloc(uri_len);
    if (!data_uri) {
        cJSON_Delete(content_arr);
        snprintf(response_buf, buf_size, "Error: OOM building data URI");
        return ERROR;
    }
    snprintf(data_uri, uri_len, "data:%s;base64,%s", mime_type, image_b64);

    cJSON* img_part = cJSON_CreateObject();
    cJSON_AddStringToObject(img_part, "type", "image_url");
    cJSON* img_url_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(img_url_obj, "url", data_uri);
    cJSON_AddItemToObject(img_part, "image_url", img_url_obj);
    cJSON_AddItemToArray(content_arr, img_part);
    free(data_uri);

    /* Build messages array with single user message */
    cJSON* user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddItemToObject(user_msg, "content", content_arr);

    cJSON* messages = cJSON_CreateArray();
    cJSON_AddItemToArray(messages, user_msg);

    cJSON* body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model",
        model_name_for_api(model, llm_host));

    if (is_openai_compat_host(llm_host))
        cJSON_AddNumberToObject(body, "max_completion_tokens",
            VELACLAW_VISION_MAX_TOKENS);
    else
        cJSON_AddNumberToObject(body, "max_tokens", VELACLAW_VISION_MAX_TOKENS);

    cJSON_AddItemToObject(body, "messages", messages);

    char* post_data = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!post_data) {
        snprintf(response_buf, buf_size, "Error: Failed to build vision request");
        return ERROR;
    }

    syslog(LOG_INFO, "[%s] Vision API call (model: %s, %d bytes)\n", TAG, model,
        (int)strlen(post_data));

    resp_buf_t rb = { 0 };
    int status = 0;
    int err = llm_http_call(post_data, &rb, &status);
    free(post_data);

    if (err != OK) {
        resp_buf_free(&rb);
        snprintf(response_buf, buf_size, "Error: Vision HTTP request failed");
        return err;
    }

    if (status != 200) {
        syslog(LOG_ERR, "[%s] Vision API error HTTP %d: %.300s\n", TAG, status,
            rb.data ? rb.data : "");
        snprintf(response_buf, buf_size, "Vision API error (HTTP %d): %.200s",
            status, rb.data ? rb.data : "");
        resp_buf_free(&rb);
        return ERROR;
    }

    cJSON* root = cJSON_Parse(rb.data);
    resp_buf_free(&rb);

    if (!root) {
        snprintf(response_buf, buf_size, "Error: Failed to parse vision response");
        return ERROR;
    }

    extract_text(root, response_buf, buf_size);
    cJSON_Delete(root);

    if (response_buf[0] == '\0')
        snprintf(response_buf, buf_size, "No response from Vision API");
    else
        syslog(LOG_INFO, "[%s] Vision response: %d bytes\n", TAG,
            (int)strlen(response_buf));

    return OK;
}

/* ── JSON string escape helper ────────────────────────────── */

/**
 * Escape a string for safe embedding in a JSON value.
 * Handles: \\ \" \n \r \t \b \f and control chars (U+0000..U+001F).
 * Returns a malloc'd string; caller must free.  NULL on OOM.
 */
static char* json_escape_string(const char* src)
{
    if (!src)
        return NULL;

    /* Worst case: every char becomes \uXXXX (6 bytes) */
    size_t src_len = strlen(src);
    size_t cap = src_len * 6 + 1;
    char* out = malloc(cap);
    if (!out)
        return NULL;

    char* w = out;
    for (const char* r = src; *r; r++) {
        unsigned char c = (unsigned char)*r;
        switch (c) {
        case '"':
            *w++ = '\\';
            *w++ = '"';
            break;
        case '\\':
            *w++ = '\\';
            *w++ = '\\';
            break;
        case '\n':
            *w++ = '\\';
            *w++ = 'n';
            break;
        case '\r':
            *w++ = '\\';
            *w++ = 'r';
            break;
        case '\t':
            *w++ = '\\';
            *w++ = 't';
            break;
        case '\b':
            *w++ = '\\';
            *w++ = 'b';
            break;
        case '\f':
            *w++ = '\\';
            *w++ = 'f';
            break;
        default:
            if (c < 0x20) {
                w += sprintf(w, "\\u%04x", c);
            } else {
                *w++ = (char)c;
            }
            break;
        }
    }
    *w = '\0';
    return out;
}

/* ── Public: memory-optimized vision chat (raw image bytes) ── */

int llm_chat_vision_raw(const char* prompt,
    const unsigned char* raw_image, size_t raw_len,
    const char* mime_type,
    char* response_buf, size_t buf_size)
{
    char model[64], api_key[128], llm_host[128];
    pthread_mutex_lock(&s_llm_lock);
    memcpy(model, s_model, sizeof(model));
    memcpy(api_key, s_api_key, sizeof(api_key));
    memcpy(llm_host, s_llm_host, sizeof(llm_host));
    pthread_mutex_unlock(&s_llm_lock);

    if (api_key[0] == '\0') {
        snprintf(response_buf, buf_size, "Error: No API key configured");
        return ERROR;
    }

    if (!mime_type || !mime_type[0])
        mime_type = "image/jpeg";

    if (!prompt || !prompt[0])
        prompt = VELACLAW_VISION_DEFAULT_PROMPT;

    /* Escape prompt for safe JSON embedding — handles " \ newlines
     * and control chars that would break the hand-built JSON body. */
    char* escaped_prompt = json_escape_string(prompt);
    if (!escaped_prompt) {
        snprintf(response_buf, buf_size, "Error: OOM escaping prompt");
        return ERROR;
    }

    int max_tokens = strstr(llm_host, "openai.com")
        ? VELACLAW_VISION_MAX_TOKENS
        : VELACLAW_VISION_MAX_TOKENS;
    const char* tokens_key = strstr(llm_host, "openai.com")
        ? "max_completion_tokens"
        : "max_tokens";

    /* ── Step 1: compute base64 output size ────────────────── */
    size_t b64_len = 0;
    mbedtls_base64_encode(NULL, 0, &b64_len, raw_image, raw_len);

    /* ── Step 2: build JSON body in a single buffer ────────── *
     * Layout: [json_header][base64_data][json_trailer]        *
     * This avoids separate b64 + data_uri + cJSON copies.     */

    /* JSON before the base64 payload */
    const char* hdr_fmt = "{\"model\":\"%s\",\"%s\":%d,\"messages\":"
                          "[{\"role\":\"user\",\"content\":["
                          "{\"type\":\"text\",\"text\":\"%s\"},"
                          "{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:%s;base64,";
    const char* trailer = "\"}}]}]}";

    size_t hdr_max = strlen(hdr_fmt) + sizeof(model) + 32
        + strlen(tokens_key) + strlen(escaped_prompt) + strlen(mime_type) + 64;
    size_t total = hdr_max + b64_len + strlen(trailer) + 1;

    char* body = malloc(total);
    if (!body) {
        free(escaped_prompt);
        snprintf(response_buf, buf_size, "Error: OOM building vision request");
        return ERROR;
    }

    int off = snprintf(body, total, hdr_fmt,
        model_name_for_api(model, llm_host), tokens_key, max_tokens,
        escaped_prompt, mime_type);
    free(escaped_prompt);

    /* ── Step 3: base64 encode directly into the body buffer ─ */
    size_t written = 0;
    int rc = mbedtls_base64_encode(
        (unsigned char*)(body + off), total - (size_t)off - strlen(trailer) - 1,
        &written, raw_image, raw_len);
    if (rc != 0) {
        free(body);
        snprintf(response_buf, buf_size, "Error: base64 encode failed (%d)", rc);
        return ERROR;
    }
    off += (int)written;

    /* ── Step 4: append JSON trailer ───────────────────────── */
    memcpy(body + off, trailer, strlen(trailer) + 1);

    syslog(LOG_INFO, "[%s] Vision raw API call (model: %s, body=%d bytes, "
                     "image=%zu raw -> %zu b64)\n",
        TAG, model, off + (int)strlen(trailer), raw_len, written);

    /* ── Step 5: HTTP call — only `body` is alive, no other
     *    large buffers on the heap ─────────────────────────── */
    resp_buf_t rb = { 0 };
    int status = 0;
    int err = llm_http_call(body, &rb, &status);
    free(body);

    if (err != OK) {
        resp_buf_free(&rb);
        snprintf(response_buf, buf_size, "Error: Vision HTTP request failed");
        return err;
    }

    if (status != 200) {
        syslog(LOG_ERR, "[%s] Vision raw API error HTTP %d: %.300s\n", TAG,
            status, rb.data ? rb.data : "");
        snprintf(response_buf, buf_size, "Vision API error (HTTP %d): %.200s",
            status, rb.data ? rb.data : "");
        resp_buf_free(&rb);
        return ERROR;
    }

    cJSON* root = cJSON_Parse(rb.data);
    resp_buf_free(&rb);

    if (!root) {
        snprintf(response_buf, buf_size, "Error: Failed to parse vision response");
        return ERROR;
    }

    extract_text(root, response_buf, buf_size);
    cJSON_Delete(root);

    if (response_buf[0] == '\0')
        snprintf(response_buf, buf_size, "No response from Vision API");
    else
        syslog(LOG_INFO, "[%s] Vision raw response: %d bytes\n", TAG,
            (int)strlen(response_buf));

    return OK;
}
