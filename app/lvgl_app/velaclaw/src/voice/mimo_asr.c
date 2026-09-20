/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "voice/mimo_asr.h"

#include "config/config_store.h"
#include "tls/vela_tls.h"
#include "velaclaw_compat.h"
#include "velaclaw_config.h"
#include "voice/voice_asr.h"

#include "cJSON.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

static const char *TAG = "mimo_asr";

#define MIMO_ASR_RESP_BUF_SIZE (16 * 1024)

static char s_api_key[160];
static char s_host[128];
static char s_port[8];
static char s_path[96];
static char s_model[64];
static char s_resp_buf[MIMO_ASR_RESP_BUF_SIZE];

static void put_le16(unsigned char *p, uint16_t v)
{
  p[0] = (unsigned char)(v & 0xff);
  p[1] = (unsigned char)((v >> 8) & 0xff);
}

static void put_le32(unsigned char *p, uint32_t v)
{
  p[0] = (unsigned char)(v & 0xff);
  p[1] = (unsigned char)((v >> 8) & 0xff);
  p[2] = (unsigned char)((v >> 16) & 0xff);
  p[3] = (unsigned char)((v >> 24) & 0xff);
}

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

static int mimo_asr_init(void)
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
  config_get_or_default(VELACLAW_CFG_KEY_MIMO_ASR_MODEL,
                        VELACLAW_MIMO_ASR_MODEL, s_model, sizeof(s_model));
  normalize_token_plan_host();

  return 0;
}

static void build_wav_header(unsigned char *wav, size_t pcm_len)
{
  const uint32_t sample_rate = VELACLAW_VOICE_SAMPLE_RATE;
  const uint16_t channels = VELACLAW_VOICE_CHANNELS;
  const uint16_t bits = VELACLAW_VOICE_BITS;
  const uint16_t block_align = channels * bits / 8;
  const uint32_t byte_rate = sample_rate * block_align;

  memcpy(wav, "RIFF", 4);
  put_le32(wav + 4, (uint32_t)(pcm_len + 36));
  memcpy(wav + 8, "WAVEfmt ", 8);
  put_le32(wav + 16, 16);
  put_le16(wav + 20, 1);
  put_le16(wav + 22, channels);
  put_le32(wav + 24, sample_rate);
  put_le32(wav + 28, byte_rate);
  put_le16(wav + 32, block_align);
  put_le16(wav + 34, bits);
  memcpy(wav + 36, "data", 4);
  put_le32(wav + 40, (uint32_t)pcm_len);
}

typedef struct
{
  char prefix[256];
  size_t prefix_len;
  size_t prefix_pos;
  unsigned char wav_header[44];
  const unsigned char *pcm;
  size_t pcm_len;
  size_t raw_pos;
  char suffix[80];
  size_t suffix_len;
  size_t suffix_pos;
} asr_upload_t;

static unsigned char asr_upload_raw_byte(const asr_upload_t *upload,
                                         size_t pos)
{
  if (pos < sizeof(upload->wav_header)) {
    return upload->wav_header[pos];
  }
  return upload->pcm[pos - sizeof(upload->wav_header)];
}

static int asr_upload_next(char *buf, size_t cap, size_t *out_len,
                           void *user_data)
{
  static const char enc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  asr_upload_t *upload = (asr_upload_t *)user_data;
  size_t raw_len;
  size_t used = 0;

  if (buf == NULL || cap == 0 || out_len == NULL || upload == NULL) {
    return -EINVAL;
  }

  raw_len = sizeof(upload->wav_header) + upload->pcm_len;

  while (used < cap) {
    if (upload->prefix_pos < upload->prefix_len) {
      size_t n = upload->prefix_len - upload->prefix_pos;

      if (n > cap - used) {
        n = cap - used;
      }
      memcpy(buf + used, upload->prefix + upload->prefix_pos, n);
      upload->prefix_pos += n;
      used += n;
      continue;
    }

    if (upload->raw_pos < raw_len) {
      unsigned char in[3] = { 0, 0, 0 };
      size_t n = raw_len - upload->raw_pos;

      if (cap - used < 4) {
        break;
      }
      if (n > 3) {
        n = 3;
      }
      for (size_t i = 0; i < n; i++) {
        in[i] = asr_upload_raw_byte(upload, upload->raw_pos++);
      }
      buf[used++] = enc[in[0] >> 2];
      buf[used++] = enc[((in[0] & 0x03) << 4) |
                        (n > 1 ? in[1] >> 4 : 0)];
      buf[used++] = n > 1 ? enc[((in[1] & 0x0f) << 2) |
                                (n > 2 ? in[2] >> 6 : 0)] : '=';
      buf[used++] = n > 2 ? enc[in[2] & 0x3f] : '=';
      continue;
    }

    if (upload->suffix_pos < upload->suffix_len) {
      size_t n = upload->suffix_len - upload->suffix_pos;

      if (n > cap - used) {
        n = cap - used;
      }
      memcpy(buf + used, upload->suffix + upload->suffix_pos, n);
      upload->suffix_pos += n;
      used += n;
      continue;
    }
    break;
  }

  *out_len = used;
  return 0;
}

static int asr_upload_init(asr_upload_t *upload,
                           const unsigned char *pcm, size_t pcm_len,
                           const char *language,
                           size_t *body_len)
{
  size_t raw_len;
  size_t b64_len;
  int ret;

  if (upload == NULL || pcm == NULL || pcm_len == 0 || language == NULL ||
      body_len == NULL ||
      pcm_len > UINT32_MAX - sizeof(upload->wav_header)) {
    return -EINVAL;
  }

  memset(upload, 0, sizeof(*upload));
  ret = snprintf(upload->prefix, sizeof(upload->prefix),
                 "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\","
                 "\"content\":[{\"type\":\"input_audio\","
                 "\"input_audio\":{\"data\":\"data:audio/wav;base64,",
                 s_model);
  if (ret < 0 || (size_t)ret >= sizeof(upload->prefix)) {
    return -EOVERFLOW;
  }

  upload->prefix_len = (size_t)ret;
  ret = snprintf(upload->suffix, sizeof(upload->suffix),
                 "\",\"format\":\"wav\"}}]}],\"asr_options\":{"
                 "\"language\":\"%s\"},\"stream\":false}", language);
  if (ret < 0 || (size_t)ret >= sizeof(upload->suffix)) {
    return -EOVERFLOW;
  }
  upload->suffix_len = (size_t)ret;
  upload->pcm = pcm;
  upload->pcm_len = pcm_len;
  build_wav_header(upload->wav_header, pcm_len);

  raw_len = sizeof(upload->wav_header) + pcm_len;
  b64_len = ((raw_len + 2) / 3) * 4;
  if (upload->prefix_len > SIZE_MAX - b64_len ||
      upload->prefix_len + b64_len > SIZE_MAX - upload->suffix_len) {
    return -EOVERFLOW;
  }
  *body_len = upload->prefix_len + b64_len + upload->suffix_len;
  return 0;
}

static const char *json_string_at(cJSON *root, const char *a,
                                  const char *b, const char *c,
                                  const char *d)
{
  cJSON *item = root;

  if (a) {
    item = cJSON_GetObjectItem(item, a);
  }
  if (item && b) {
    item = cJSON_GetObjectItem(item, b);
  }
  if (item && c) {
    item = cJSON_GetObjectItem(item, c);
  }
  if (item && d) {
    item = cJSON_GetObjectItem(item, d);
  }

  return item && cJSON_IsString(item) ? item->valuestring : NULL;
}

static int copy_json_content(cJSON *content, char *out, size_t out_cap)
{
  size_t used = 0;

  if (content == NULL || out == NULL || out_cap == 0) {
    return -EINVAL;
  }

  out[0] = '\0';

  if (cJSON_IsString(content)) {
    snprintf(out, out_cap, "%s", content->valuestring);
    return out[0] ? 0 : -ENODATA;
  }

  if (cJSON_IsArray(content)) {
    int count = cJSON_GetArraySize(content);

    for (int i = 0; i < count && used + 1 < out_cap; i++) {
      cJSON *item = cJSON_GetArrayItem(content, i);
      const char *s = json_string_at(item, "text", NULL, NULL, NULL);

      if (s == NULL) {
        s = json_string_at(item, "input_text", NULL, NULL, NULL);
      }
      if (s == NULL) {
        s = json_string_at(item, "content", NULL, NULL, NULL);
      }
      if (s != NULL) {
        int n = snprintf(out + used, out_cap - used, "%s", s);

        if (n < 0) {
          return -EIO;
        }
        if ((size_t)n >= out_cap - used) {
          used = out_cap - 1;
          break;
        }
        used += (size_t)n;
      }
    }

    return out[0] ? 0 : -ENODATA;
  }

  return -ENODATA;
}

static int parse_asr_response(const char *resp, char *text, size_t text_cap)
{
  cJSON *root;
  cJSON *choices;
  cJSON *choice;
  cJSON *message;
  cJSON *content;
  const char *direct;
  int ret;

  if (resp == NULL || text == NULL || text_cap == 0) {
    return -EINVAL;
  }

  text[0] = '\0';
  root = cJSON_Parse(resp);
  if (root == NULL) {
    return -EPROTO;
  }

  choices = cJSON_GetObjectItem(root, "choices");
  choice = cJSON_GetArrayItem(choices, 0);
  message = choice ? cJSON_GetObjectItem(choice, "message") : NULL;
  content = message ? cJSON_GetObjectItem(message, "content") : NULL;
  ret = copy_json_content(content, text, text_cap);
  if (ret == 0) {
    cJSON_Delete(root);
    return 0;
  }

  direct = json_string_at(root, "text", NULL, NULL, NULL);
  if (direct == NULL) {
    direct = json_string_at(root, "result", "text", NULL, NULL);
  }
  if (direct == NULL) {
    direct = json_string_at(root, "data", "text", NULL, NULL);
  }

  if (direct != NULL) {
    snprintf(text, text_cap, "%s", direct);
    ret = text[0] ? 0 : -ENODATA;
  }

  cJSON_Delete(root);
  return ret;
}

static int mimo_asr_recognize_language(const unsigned char *pcm_data,
                                       size_t pcm_len,
                                       const char *language,
                                       char *text_out,
                                       size_t text_cap)
{
  asr_upload_t upload;
  char *resp = s_resp_buf;
  char auth[192];
  size_t body_len = 0;
  size_t resp_len = 0;
  int status;
  int ret;

  if (pcm_data == NULL || pcm_len == 0 || text_out == NULL ||
      text_cap == 0) {
    return -EINVAL;
  }

  mimo_asr_init();
  if (s_api_key[0] == '\0') {
    syslog(LOG_ERR, "[%s] API key not configured\n", TAG);
    return -ENOENT;
  }

  ret = asr_upload_init(&upload, pcm_data, pcm_len, language, &body_len);
  if (ret < 0) {
    return ret;
  }

  memset(resp, 0, MIMO_ASR_RESP_BUF_SIZE);

  snprintf(auth, sizeof(auth), "Bearer %s", s_api_key);
  vela_header_t hdrs[] = {
    { "Content-Type", "application/json" },
    { "Accept-Encoding", "identity" },
    { "Authorization", auth },
    { NULL, NULL }
  };

  syslog(LOG_INFO, "[%s] request %zu bytes pcm language=%s via %s/%s\n",
         TAG, pcm_len, language, s_host, s_model);
  status = vela_https_request_upload(s_host, s_port, "POST", s_path,
                                     hdrs, body_len,
                                     asr_upload_next, &upload,
                                     resp, MIMO_ASR_RESP_BUF_SIZE,
                                     &resp_len);

  if (status != 200) {
    syslog(LOG_ERR, "[%s] HTTP %d: %.160s\n", TAG, status, resp);
    return -EIO;
  }

  ret = parse_asr_response(resp, text_out, text_cap);
  if (ret != 0) {
    syslog(LOG_WARNING, "[%s] parse failed: %d\n", TAG, ret);
    return ret;
  }

  syslog(LOG_INFO, "[%s] recognized: %.80s\n", TAG, text_out);
  return 0;
}

static int mimo_asr_recognize(const unsigned char *pcm_data,
                              size_t pcm_len,
                              char *text_out,
                              size_t text_cap)
{
  return mimo_asr_recognize_language(pcm_data, pcm_len, "auto",
                                     text_out, text_cap);
}

static const voice_asr_ops_t s_mimo_asr_ops = {
  .name = "mimo",
  .init = mimo_asr_init,
  .recognize = mimo_asr_recognize,
  .recognize_language = mimo_asr_recognize_language,
  .deinit = NULL,
};

int mimo_asr_register(void)
{
  return voice_asr_register(&s_mimo_asr_ops);
}
