/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "voice/mimo_tts.h"

#include "config/config_store.h"
#include "tls/vela_tls.h"
#include "velaclaw_compat.h"
#include "velaclaw_config.h"
#include "voice/voice_tts.h"

#include "mbedtls/base64.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

static const char *TAG = "mimo_tts";

static char s_api_key[160];
static char s_host[128];
static char s_port[8];
static char s_path[96];
static char s_model[64];
static char s_voice[64];

#define MIMO_TTS_B64_BLOCK 1024
#define MIMO_TTS_PCM_BLOCK ((MIMO_TTS_B64_BLOCK / 4) * 3)
#define MIMO_TTS_SSE_PREFIX_LEN 5
#define MIMO_TTS_JSON_KEY_CAP 12

enum mimo_tts_json_key
{
  MIMO_TTS_KEY_OTHER = 0,
  MIMO_TTS_KEY_AUDIO,
  MIMO_TTS_KEY_DATA,
  MIMO_TTS_KEY_ERROR
};

enum mimo_tts_audio_mode
{
  MIMO_TTS_AUDIO_PREFIX = 0,
  MIMO_TTS_AUDIO_DATA_URI,
  MIMO_TTS_AUDIO_BASE64
};

static uint64_t mimo_tts_now_ms(void)
{
  struct timespec ts;

#ifdef CLOCK_MONOTONIC
  clock_gettime(CLOCK_MONOTONIC, &ts);
#else
  clock_gettime(CLOCK_REALTIME, &ts);
#endif
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

typedef struct
{
  char sse_prefix[MIMO_TTS_SSE_PREFIX_LEN];
  size_t sse_prefix_len;
  bool sse_data_line;
  bool sse_ignore_line;
  bool line_has_data;
  bool line_has_audio;
  bool json_started;
  bool json_in_string;
  bool json_escape;
  bool json_string_is_value;
  bool json_key_ready;
  bool json_expect_value;
  enum mimo_tts_json_key json_key;
  enum mimo_tts_json_key json_value_key;
  char json_key_buf[MIMO_TTS_JSON_KEY_CAP];
  size_t json_key_len;
  enum mimo_tts_audio_mode audio_mode;
  char audio_prefix[5];
  size_t audio_prefix_len;
  size_t data_uri_match;
  char b64_block[MIMO_TTS_B64_BLOCK];
  size_t b64_block_len;
  unsigned char pcm_block[MIMO_TTS_PCM_BLOCK];
  voice_tts_chunk_cb cb;
  void *user_data;
  uint64_t start_ms;
  uint64_t first_pcm_ms;
  size_t body_bytes;
  size_t sse_lines;
  size_t json_events;
  size_t audio_events;
  size_t ignored_events;
  size_t malformed_events;
  size_t pcm_bytes;
  size_t chunks;
  int err;
} mimo_tts_stream_ctx_t;

typedef struct
{
  unsigned char *buf;
  size_t len;
  size_t cap;
  int err;
} mimo_tts_collect_ctx_t;

static int config_get_or_default(const char *key, const char *def,
                                 char *buf, size_t cap)
{
  if (cap == 0) {
    return -EINVAL;
  }

  if (claw_config_get(key, buf, cap) == OK && buf[0] != '\0') {
    return 0;
  }

  snprintf(buf, cap, "%s", def ? def : "");
  return 0;
}

static void normalize_token_plan_host(void)
{
  if (strncmp(s_api_key, "tp-", 3) == 0 &&
      strcmp(s_host, "api.xiaomimimo.com") == 0) {
    snprintf(s_host, sizeof(s_host), "%s", "token-plan-cn.xiaomimimo.com");
    syslog(LOG_INFO, "[%s] Token Plan key detected; using Token Plan host\n",
           TAG);
  } else if (strncmp(s_api_key, "sk-", 3) == 0 &&
             strstr(s_host, "token-plan-") == s_host) {
    snprintf(s_host, sizeof(s_host), "%s", "api.xiaomimimo.com");
    syslog(LOG_INFO, "[%s] Pay-as-you-go key detected; using API host\n",
           TAG);
  }
}

static void mimo_tts_stream_free(mimo_tts_stream_ctx_t *ctx)
{
  if (ctx == NULL) {
    return;
  }

  memset(ctx, 0, sizeof(*ctx));
}

static int mimo_tts_init(void)
{
  memset(s_api_key, 0, sizeof(s_api_key));

  if (claw_config_get(VELACLAW_CFG_KEY_MIMO_API_KEY,
                      s_api_key, sizeof(s_api_key)) != OK ||
      s_api_key[0] == '\0') {
    (void)claw_config_get(VELACLAW_CFG_KEY_API_KEY,
                          s_api_key, sizeof(s_api_key));
  }

  if (s_api_key[0] == '\0' && VELACLAW_SECRET_API_KEY[0] != '\0') {
    snprintf(s_api_key, sizeof(s_api_key), "%s", VELACLAW_SECRET_API_KEY);
  }

  config_get_or_default(VELACLAW_CFG_KEY_MIMO_HOST,
                        VELACLAW_MIMO_HOST, s_host, sizeof(s_host));
  config_get_or_default(VELACLAW_CFG_KEY_MIMO_PORT,
                        VELACLAW_MIMO_PORT, s_port, sizeof(s_port));
  config_get_or_default(VELACLAW_CFG_KEY_MIMO_PATH,
                        VELACLAW_MIMO_PATH, s_path, sizeof(s_path));
  config_get_or_default(VELACLAW_CFG_KEY_MIMO_TTS_MODEL,
                        VELACLAW_MIMO_TTS_MODEL, s_model, sizeof(s_model));
  config_get_or_default(VELACLAW_CFG_KEY_MIMO_TTS_VOICE,
                        VELACLAW_MIMO_TTS_VOICE, s_voice, sizeof(s_voice));
  normalize_token_plan_host();

  return 0;
}

static int mimo_tts_emit_pcm(mimo_tts_stream_ctx_t *ctx,
                             const unsigned char *pcm, size_t len)
{
  uint64_t now_ms;
  size_t total;

  if (ctx->cb == NULL || pcm == NULL || len == 0) {
    return 0;
  }

  now_ms = mimo_tts_now_ms();
  total = ctx->pcm_bytes + len;
  if (ctx->chunks == 0) {
    ctx->first_pcm_ms = now_ms;
    syslog(LOG_INFO,
           "[%s] first PCM chunk in %llums (%zu bytes)\n",
           TAG,
           (unsigned long long)(ctx->first_pcm_ms - ctx->start_ms),
           len);
  }

  ctx->pcm_bytes = total;
  ctx->cb(pcm, len, 0, ctx->user_data);
  ctx->chunks++;
  return 0;
}

static int mimo_tts_flush_pending(mimo_tts_stream_ctx_t *ctx, bool final)
{
  size_t decode_len = (ctx->b64_block_len / 4) * 4;
  size_t out_len = 0;
  int ret;

  if (ctx->b64_block_len == 0) {
    return 0;
  }

  if (final && decode_len != ctx->b64_block_len) {
    decode_len = ((ctx->b64_block_len + 3) / 4) * 4;
    if (decode_len > sizeof(ctx->b64_block)) {
      return -EOVERFLOW;
    }
    memset(ctx->b64_block + ctx->b64_block_len, '=',
           decode_len - ctx->b64_block_len);
  }

  if (decode_len == 0) {
    return 0;
  }

  ret = mbedtls_base64_decode(ctx->pcm_block, sizeof(ctx->pcm_block),
                              &out_len,
                              (const unsigned char *)ctx->b64_block,
                              decode_len);
  if (ret != 0) {
    return -EPROTO;
  }

  if (out_len > 0) {
    (void)mimo_tts_emit_pcm(ctx, ctx->pcm_block, out_len);
  }

  if (!final && ctx->b64_block_len > decode_len) {
    memmove(ctx->b64_block, ctx->b64_block + decode_len,
            ctx->b64_block_len - decode_len);
    ctx->b64_block_len -= decode_len;
  } else {
    ctx->b64_block_len = 0;
  }

  return 0;
}

static int mimo_tts_append_b64(mimo_tts_stream_ctx_t *ctx, char c)
{
  if (ctx->b64_block_len == sizeof(ctx->b64_block)) {
    int ret = mimo_tts_flush_pending(ctx, false);
    if (ret < 0) {
      return ret;
    }
  }

  ctx->b64_block[ctx->b64_block_len++] = c;
  return 0;
}

static int mimo_tts_feed_audio_char(mimo_tts_stream_ctx_t *ctx, char c)
{
  static const char marker[] = "base64,";

  if (ctx->audio_mode == MIMO_TTS_AUDIO_PREFIX) {
    ctx->audio_prefix[ctx->audio_prefix_len++] = c;
    if (ctx->audio_prefix_len < sizeof(ctx->audio_prefix)) {
      return 0;
    }
    if (memcmp(ctx->audio_prefix, "data:", 5) == 0) {
      ctx->audio_mode = MIMO_TTS_AUDIO_DATA_URI;
      return 0;
    }
    ctx->audio_mode = MIMO_TTS_AUDIO_BASE64;
    for (size_t i = 0; i < ctx->audio_prefix_len; i++) {
      int ret = mimo_tts_append_b64(ctx, ctx->audio_prefix[i]);
      if (ret < 0) {
        return ret;
      }
    }
    return 0;
  }

  if (ctx->audio_mode == MIMO_TTS_AUDIO_DATA_URI) {
    if (c == marker[ctx->data_uri_match]) {
      ctx->data_uri_match++;
    } else {
      ctx->data_uri_match = c == marker[0] ? 1 : 0;
    }
    if (ctx->data_uri_match == sizeof(marker) - 1) {
      ctx->audio_mode = MIMO_TTS_AUDIO_BASE64;
    }
    return 0;
  }

  return mimo_tts_append_b64(ctx, c);
}

static int mimo_tts_finish_audio_value(mimo_tts_stream_ctx_t *ctx)
{
  int ret;

  if (ctx->audio_mode == MIMO_TTS_AUDIO_PREFIX) {
    ctx->audio_mode = MIMO_TTS_AUDIO_BASE64;
    for (size_t i = 0; i < ctx->audio_prefix_len; i++) {
      ret = mimo_tts_append_b64(ctx, ctx->audio_prefix[i]);
      if (ret < 0) {
        return ret;
      }
    }
  } else if (ctx->audio_mode == MIMO_TTS_AUDIO_DATA_URI) {
    return -EPROTO;
  }

  ret = mimo_tts_flush_pending(ctx, false);
  if (ret == 0) {
    ctx->audio_events++;
    ctx->line_has_audio = true;
  }
  return ret;
}

static enum mimo_tts_json_key mimo_tts_key(const char *key, size_t len)
{
  if (len == 5 && memcmp(key, "audio", 5) == 0) {
    return MIMO_TTS_KEY_AUDIO;
  }
  if (len == 4 && memcmp(key, "data", 4) == 0) {
    return MIMO_TTS_KEY_DATA;
  }
  if (len == 5 && memcmp(key, "error", 5) == 0) {
    return MIMO_TTS_KEY_ERROR;
  }
  return MIMO_TTS_KEY_OTHER;
}

static int mimo_tts_feed_json_char(mimo_tts_stream_ctx_t *ctx, char c)
{
  if (!ctx->json_started) {
    if (c == ' ' || c == '\t') {
      return 0;
    }
    if (c != '{' && c != '[') {
      ctx->sse_ignore_line = true;
      return 0;
    }
    ctx->json_started = true;
    ctx->json_events++;
    return 0;
  }

  if (ctx->json_in_string) {
    if (ctx->json_escape) {
      ctx->json_escape = false;
      if (ctx->json_string_is_value &&
          (ctx->json_value_key == MIMO_TTS_KEY_AUDIO ||
           ctx->json_value_key == MIMO_TTS_KEY_DATA)) {
        if (c != '/') {
          return -EPROTO;
        }
        return mimo_tts_feed_audio_char(ctx, c);
      }
      return 0;
    }

    if (c == '\\') {
      ctx->json_escape = true;
      return 0;
    }
    if (c == '"') {
      int ret = 0;

      if (ctx->json_string_is_value) {
        if (ctx->json_value_key == MIMO_TTS_KEY_AUDIO ||
            ctx->json_value_key == MIMO_TTS_KEY_DATA) {
          ret = mimo_tts_finish_audio_value(ctx);
        }
      } else {
        ctx->json_key = mimo_tts_key(ctx->json_key_buf,
                                     ctx->json_key_len);
        ctx->json_key_ready = true;
      }
      ctx->json_in_string = false;
      ctx->json_string_is_value = false;
      return ret;
    }

    if (ctx->json_string_is_value &&
        (ctx->json_value_key == MIMO_TTS_KEY_AUDIO ||
         ctx->json_value_key == MIMO_TTS_KEY_DATA)) {
      return mimo_tts_feed_audio_char(ctx, c);
    }
    if (!ctx->json_string_is_value &&
        ctx->json_key_len < sizeof(ctx->json_key_buf)) {
      ctx->json_key_buf[ctx->json_key_len++] = c;
    }
    return 0;
  }

  if (ctx->json_key_ready) {
    if (c == ' ' || c == '\t') {
      return 0;
    }
    if (c == ':') {
      ctx->json_expect_value = true;
      ctx->json_value_key = ctx->json_key;
      ctx->json_key_ready = false;
      return 0;
    }
    ctx->json_key_ready = false;
  }

  if (ctx->json_expect_value) {
    if (c == ' ' || c == '\t') {
      return 0;
    }
    if (ctx->json_value_key == MIMO_TTS_KEY_ERROR && c != 'n') {
      syslog(LOG_ERR, "[%s] SSE response contains an error object\n", TAG);
      return -EIO;
    }
    if (c == '"') {
      ctx->json_in_string = true;
      ctx->json_string_is_value = true;
      ctx->json_expect_value = false;
      if (ctx->json_value_key == MIMO_TTS_KEY_AUDIO ||
          ctx->json_value_key == MIMO_TTS_KEY_DATA) {
        ctx->audio_mode = MIMO_TTS_AUDIO_PREFIX;
        ctx->audio_prefix_len = 0;
        ctx->data_uri_match = 0;
      }
      return 0;
    }
    ctx->json_expect_value = false;
  }

  if (c == '"') {
    ctx->json_in_string = true;
    ctx->json_string_is_value = false;
    ctx->json_key_len = 0;
  }
  return 0;
}

static void mimo_tts_reset_sse_line(mimo_tts_stream_ctx_t *ctx)
{
  ctx->sse_prefix_len = 0;
  ctx->sse_data_line = false;
  ctx->sse_ignore_line = false;
  ctx->line_has_data = false;
  ctx->line_has_audio = false;
  ctx->json_started = false;
  ctx->json_in_string = false;
  ctx->json_escape = false;
  ctx->json_string_is_value = false;
  ctx->json_key_ready = false;
  ctx->json_expect_value = false;
  ctx->json_key = MIMO_TTS_KEY_OTHER;
  ctx->json_value_key = MIMO_TTS_KEY_OTHER;
  ctx->json_key_len = 0;
}

static int mimo_tts_finish_sse_line(mimo_tts_stream_ctx_t *ctx)
{
  if (ctx->sse_data_line && ctx->line_has_data) {
    ctx->sse_lines++;
    if (ctx->json_started && !ctx->line_has_audio) {
      ctx->ignored_events++;
      if (ctx->ignored_events <= 2) {
        syslog(LOG_INFO, "[%s] SSE event without audio #%zu\n",
               TAG, ctx->ignored_events);
      }
    }
  }
  if (ctx->json_in_string && ctx->json_string_is_value &&
      (ctx->json_value_key == MIMO_TTS_KEY_AUDIO ||
       ctx->json_value_key == MIMO_TTS_KEY_DATA)) {
    return -EPROTO;
  }
  mimo_tts_reset_sse_line(ctx);
  return 0;
}

static int mimo_tts_body_cb(const char *data, size_t len, void *user_data)
{
  mimo_tts_stream_ctx_t *ctx = (mimo_tts_stream_ctx_t *)user_data;
  size_t pos = 0;

  if (ctx == NULL || data == NULL || len == 0) {
    return 0;
  }

  ctx->body_bytes += len;

  while (pos < len) {
    char c = data[pos++];

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      int ret = mimo_tts_finish_sse_line(ctx);
      if (ret < 0) {
        ctx->err = ret;
        return ret;
      }
      continue;
    }

    if (ctx->sse_ignore_line) {
      continue;
    }
    if (!ctx->sse_data_line) {
      if (ctx->sse_prefix_len < sizeof(ctx->sse_prefix)) {
        ctx->sse_prefix[ctx->sse_prefix_len++] = c;
      }
      if (ctx->sse_prefix_len == sizeof(ctx->sse_prefix)) {
        if (memcmp(ctx->sse_prefix, "data:", 5) == 0) {
          ctx->sse_data_line = true;
        } else {
          ctx->sse_ignore_line = true;
        }
      }
      continue;
    }

    ctx->line_has_data = true;
    {
      int ret = mimo_tts_feed_json_char(ctx, c);
      if (ret < 0) {
        ctx->err = ret;
        return ret;
      }
    }
  }

  return 0;
}

static char *build_tts_body(const char *text, size_t *body_len)
{
  static const char prefix_fmt[] =
    "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\","
    "\"content\":\"Read the following text clearly at a natural, slightly "
    "fast pace. Do not add any words.\"},{\"role\":\"assistant\","
    "\"content\":\"";
  static const char suffix[] =
    "\"}],\"audio\":{\"format\":\"pcm16\",\"voice\":\"";
  static const char tail[] = "\"},\"stream\":true}";
  size_t need;
  size_t prefix_len;
  size_t text_len;
  size_t total;
  char prefix[256];
  char *body;
  int n;

  if (text == NULL) {
    return NULL;
  }

  n = snprintf(prefix, sizeof(prefix), prefix_fmt, s_model);
  if (n < 0 || (size_t)n >= sizeof(prefix)) {
    return NULL;
  }

  prefix_len = (size_t)n;
  text_len = strlen(text);
  need = prefix_len + text_len * 2 + sizeof(suffix) - 1 +
         strlen(s_voice) + sizeof(tail) - 1 + 1;
  body = malloc(need);
  if (body == NULL) {
    return NULL;
  }

  memcpy(body, prefix, prefix_len);
  total = prefix_len;

  for (size_t i = 0; i < text_len; i++) {
    char ch = text[i];
    if (ch == '\\' || ch == '\"') {
      body[total++] = '\\';
    }
    else if (ch == '\n') {
      body[total++] = '\\';
      body[total++] = 'n';
      continue;
    }
    else if (ch == '\r') {
      body[total++] = '\\';
      body[total++] = 'r';
      continue;
    }
    else if (ch == '\t') {
      body[total++] = '\\';
      body[total++] = 't';
      continue;
    }
    body[total++] = ch;
  }

  memcpy(body + total, suffix, sizeof(suffix) - 1);
  total += sizeof(suffix) - 1;
  memcpy(body + total, s_voice, strlen(s_voice));
  total += strlen(s_voice);
  memcpy(body + total, tail, sizeof(tail) - 1);
  total += sizeof(tail) - 1;
  body[total] = '\0';
  if (body_len != NULL) {
    *body_len = total;
  }
  return body;
}

static void mimo_tts_collect_cb(const unsigned char *pcm_data,
                                size_t pcm_len,
                                int is_last,
                                void *user_data)
{
  mimo_tts_collect_ctx_t *collect = user_data;

  if (is_last) {
    return;
  }
  if (pcm_data == NULL || pcm_len == 0 || collect == NULL) {
    return;
  }
  if (collect->len + pcm_len > collect->cap) {
    collect->err = -ENOSPC;
    return;
  }
  memcpy(collect->buf + collect->len, pcm_data, pcm_len);
  collect->len += pcm_len;
}

static int mimo_tts_synthesize_stream(const char *text,
                                      voice_tts_chunk_cb cb,
                                      void *user_data)
{
  mimo_tts_stream_ctx_t ctx;
  char *body = NULL;
  char auth[192];
  size_t body_len = 0;
  uint64_t start_ms;
  int http_status = 0;
  int attempt;
  int ret;

  if (text == NULL || cb == NULL) {
    return -EINVAL;
  }

  mimo_tts_init();
  if (s_api_key[0] == '\0') {
    syslog(LOG_ERR, "[%s] API key not configured\n", TAG);
    return -ENOENT;
  }

  body = build_tts_body(text, &body_len);
  if (body == NULL) {
    return -ENOMEM;
  }

  snprintf(auth, sizeof(auth), "Bearer %s", s_api_key);
  vela_header_t hdrs[] = {
    { "Content-Type", "application/json" },
    { "Accept", "text/event-stream" },
    { "Accept-Encoding", "identity" },
    { "Cache-Control", "no-cache" },
    { "Authorization", auth },
    { NULL, NULL }
  };

  for (attempt = 1; attempt <= 2; attempt++) {
    memset(&ctx, 0, sizeof(ctx));
    ctx.cb = cb;
    ctx.user_data = user_data;
    http_status = 0;

    syslog(LOG_INFO,
           "[%s] stream request attempt=%d: text=%zu bytes host=%s "
           "model=%s\n",
           TAG, attempt, strlen(text), s_host, s_model);
    start_ms = mimo_tts_now_ms();
    ctx.start_ms = start_ms;
    ret = vela_https_request_stream(s_host, s_port, "POST", s_path,
                                    hdrs, body, body_len,
                                    mimo_tts_body_cb, &ctx, &http_status);

    /* The shared HTTPS API returns the HTTP status on transport success. */
    if (ret > 0) {
      ret = 0;
    }
    if (ret == 0 && (ctx.sse_prefix_len > 0 || ctx.sse_data_line)) {
      ret = mimo_tts_finish_sse_line(&ctx);
    }
    if (ret == 0 && http_status != 200) {
      ret = -EIO;
    }
    if (ret == 0 && ctx.err < 0) {
      ret = ctx.err;
    }
    if (ret == 0) {
      ret = mimo_tts_flush_pending(&ctx, true);
    }
    if (ret == 0 && ctx.chunks == 0) {
      ret = -ENODATA;
    }

    syslog(LOG_INFO,
           "[%s] stream done attempt=%d in %llums: ret=%d http=%d "
           "body=%zu lines=%zu json=%zu audio_events=%zu ignored=%zu "
           "malformed=%zu chunks=%zu pcm=%zu first=%llums\n",
           TAG, attempt,
           (unsigned long long)(mimo_tts_now_ms() - start_ms),
           ret, http_status, ctx.body_bytes, ctx.sse_lines, ctx.json_events,
           ctx.audio_events, ctx.ignored_events, ctx.malformed_events,
           ctx.chunks, ctx.pcm_bytes,
           (unsigned long long)(ctx.first_pcm_ms > 0 ?
             ctx.first_pcm_ms - start_ms : 0));

    if (ret == 0) {
      cb(NULL, 0, 1, user_data);
      mimo_tts_stream_free(&ctx);
      break;
    }

    if (ret == -ENODATA && attempt == 1) {
      syslog(LOG_WARNING,
             "[%s] HTTP 200 returned no PCM; retrying once on the "
             "same endpoint\n",
             TAG);
      mimo_tts_stream_free(&ctx);
      continue;
    }

    if (http_status != 200) {
      syslog(LOG_ERR, "[%s] HTTP %d during stream\n", TAG, http_status);
    } else {
      syslog(LOG_ERR, "[%s] stream request failed: %d\n", TAG, ret);
    }
    mimo_tts_stream_free(&ctx);
    break;
  }

  free(body);
  return ret;
}

static int mimo_tts_synthesize(const char *text,
                               unsigned char *pcm_out,
                               size_t pcm_cap,
                               size_t *pcm_len)
{
  mimo_tts_collect_ctx_t collect = {
    .buf = pcm_out,
    .len = 0,
    .cap = pcm_cap,
    .err = 0,
  };
  int ret;

  if (text == NULL || pcm_out == NULL || pcm_len == NULL) {
    return -EINVAL;
  }

  ret = mimo_tts_synthesize_stream(text, mimo_tts_collect_cb, &collect);
  if (collect.err < 0) {
    ret = collect.err;
  }
  if (ret == 0) {
    *pcm_len = collect.len;
  }
  return ret;
}

#ifdef VELACLAW_MIMO_TTS_TEST
typedef struct
{
  unsigned char *pcm;
  size_t len;
  size_t cap;
} mimo_tts_test_output_t;

static void mimo_tts_test_cb(const unsigned char *pcm, size_t len,
                             int is_last, void *user_data)
{
  mimo_tts_test_output_t *out = user_data;

  if (!is_last && pcm != NULL && out->len + len <= out->cap) {
    memcpy(out->pcm + out->len, pcm, len);
    out->len += len;
  }
}

int mimo_tts_test_parse_sse(const char *sse, size_t sse_len,
                            size_t input_chunk,
                            unsigned char *pcm, size_t pcm_cap,
                            size_t *pcm_len)
{
  mimo_tts_test_output_t out = { pcm, 0, pcm_cap };
  mimo_tts_stream_ctx_t ctx;
  size_t pos = 0;
  int ret = 0;

  if (sse == NULL || input_chunk == 0 || pcm == NULL || pcm_len == NULL) {
    return -EINVAL;
  }

  memset(&ctx, 0, sizeof(ctx));
  ctx.cb = mimo_tts_test_cb;
  ctx.user_data = &out;
  ctx.start_ms = mimo_tts_now_ms();

  while (pos < sse_len && ret == 0) {
    size_t take = sse_len - pos;
    if (take > input_chunk) {
      take = input_chunk;
    }
    ret = mimo_tts_body_cb(sse + pos, take, &ctx);
    pos += take;
  }
  if (ret == 0 && (ctx.sse_prefix_len > 0 || ctx.sse_data_line)) {
    ret = mimo_tts_finish_sse_line(&ctx);
  }
  if (ret == 0) {
    ret = mimo_tts_flush_pending(&ctx, true);
  }
  *pcm_len = out.len;
  return ret;
}
#endif

static const voice_tts_ops_t s_mimo_tts_ops = {
  .name = "mimo",
  .init = mimo_tts_init,
  .synthesize = mimo_tts_synthesize,
  .synthesize_stream = mimo_tts_synthesize_stream,
  .deinit = NULL,
};

int mimo_tts_register(void)
{
  return voice_tts_register(&s_mimo_tts_ops);
}
