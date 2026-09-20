/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "voice_wake.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static int ascii_equal_nocase(const char *text, const char *pattern,
                              size_t len)
{
  for (size_t i = 0; i < len; i++) {
    unsigned char a = (unsigned char)text[i];
    unsigned char b = (unsigned char)pattern[i];

    if (tolower(a) != tolower(b)) {
      return 0;
    }
  }

  return 1;
}

static const char *find_ascii_nocase(const char *text, const char *pattern)
{
  size_t pattern_len;

  if (text == NULL || pattern == NULL || pattern[0] == '\0') {
    return NULL;
  }

  pattern_len = strlen(pattern);
  for (const char *p = text; *p != '\0'; p++) {
    if (strlen(p) < pattern_len) {
      break;
    }
    if (ascii_equal_nocase(p, pattern, pattern_len)) {
      return p;
    }
  }

  return NULL;
}

static const char *skip_prompt_gap(const char *p)
{
  while (p != NULL && *p != '\0') {
    unsigned char c = (unsigned char)*p;

    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
        c == ',' || c == '.' || c == '?' || c == '!' ||
        c == ':' || c == ';') {
      p++;
      continue;
    }
    if (strncmp(p, "，", strlen("，")) == 0 ||
        strncmp(p, "。", strlen("。")) == 0 ||
        strncmp(p, "？", strlen("？")) == 0 ||
        strncmp(p, "！", strlen("！")) == 0 ||
        strncmp(p, "：", strlen("：")) == 0 ||
        strncmp(p, "；", strlen("；")) == 0) {
      p += strlen("，");
      continue;
    }
    if (strncmp(p, "啊", strlen("啊")) == 0 ||
        strncmp(p, "呀", strlen("呀")) == 0 ||
        strncmp(p, "嗯", strlen("嗯")) == 0 ||
        strncmp(p, "呃", strlen("呃")) == 0 ||
        strncmp(p, "额", strlen("额")) == 0) {
      p += strlen("啊");
      continue;
    }
    break;
  }

  return p == NULL ? "" : p;
}

static const char *skip_wake_separator(const char *p)
{
  while (p != NULL && *p != '\0') {
    unsigned char c = (unsigned char)*p;

    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
        c == ',' || c == '.' || c == '?' || c == '!' ||
        c == ':' || c == ';' || c == '-') {
      p++;
      continue;
    }
    if (strncmp(p, "，", strlen("，")) == 0 ||
        strncmp(p, "。", strlen("。")) == 0 ||
        strncmp(p, "？", strlen("？")) == 0 ||
        strncmp(p, "！", strlen("！")) == 0) {
      p += strlen("，");
      continue;
    }
    break;
  }

  return p == NULL ? "" : p;
}

static const char *match_utf8_token(const char *p,
                                    const char *const *tokens)
{
  if (p == NULL || tokens == NULL) {
    return NULL;
  }

  for (size_t i = 0; tokens[i] != NULL; i++) {
    size_t len = strlen(tokens[i]);

    if (strncmp(p, tokens[i], len) == 0) {
      return p + len;
    }
  }
  return NULL;
}

static const char *match_openvela_after(const char *p)
{
  static const char *const open_homophones[] = {
    "哦盆", "欧盆", "噢盆", "奥盆",
    "哦喷", "欧喷", "噢喷", "奥喷",
    "哦鹏", "欧鹏", "噢鹏", "奥鹏",
    "哦朋", "欧朋", "噢朋", "奥朋",
    NULL
  };
  static const char *const vela_homophones[] = {
    "喂啦", "喂拉", "维拉", "维啦", "威拉", "威啦",
    "微拉", "微啦", "薇拉", "唯拉", "卫拉", "卫啦",
    NULL
  };
  const char *after_open;
  const char *after_vela;

  p = skip_wake_separator(p);

  /* Observed MiMo rendering of "哦盆喂啦". Keep this exact alias behind a
   * complete greeting so ordinary mentions of a person's name do not wake. */
  if (strncmp(p, "哦，莫妮拉", strlen("哦，莫妮拉")) == 0) {
    return skip_prompt_gap(p + strlen("哦，莫妮拉"));
  }

  if (strlen(p) >= 4 && ascii_equal_nocase(p, "open", 4)) {
    after_open = p + 4;
  } else {
    after_open = match_utf8_token(p, open_homophones);
    if (after_open == NULL) {
      return NULL;
    }
  }

  p = skip_wake_separator(after_open);
  if (strlen(p) >= 4 && ascii_equal_nocase(p, "vela", 4)) {
    p += 4;
    if (isalnum((unsigned char)*p) || *p == '_') {
      return NULL;
    }
    return skip_prompt_gap(p);
  }

  /* MiMo sometimes drops the unstressed 'e': OpenVela -> OpenVla. */
  if (strlen(p) >= 3 && ascii_equal_nocase(p, "vla", 3)) {
    p += 3;
    if (isalnum((unsigned char)*p) || *p == '_') {
      return NULL;
    }
    return skip_prompt_gap(p);
  }

  /* Mixed Chinese/English ASR commonly renders the final "ela" as 啦/拉.
   * Keep this alias gated behind the complete 你好/Hello prefix. */
  if ((*p == 'v' || *p == 'V') &&
      (strncmp(p + 1, "啦", strlen("啦")) == 0 ||
       strncmp(p + 1, "拉", strlen("拉")) == 0)) {
    return skip_prompt_gap(p + 1 + strlen("啦"));
  }

  after_vela = match_utf8_token(p, vela_homophones);
  if (after_vela != NULL) {
    return skip_prompt_gap(after_vela);
  }

  return NULL;
}

static const char *match_chinese_greeting(const char *text,
                                          const char *greeting)
{
  const char *p = strstr(text, greeting);

  while (p != NULL) {
    const char *ret = match_openvela_after(p + strlen(greeting));

    if (ret != NULL) {
      return ret;
    }
    p = strstr(p + strlen(greeting), greeting);
  }
  return NULL;
}

const char *voice_wake_remainder(const char *text)
{
  const char *p;
  const char *ret;

  if (text == NULL) {
    return NULL;
  }

  ret = match_chinese_greeting(text, "你好");
  if (ret != NULL) {
    return ret;
  }

  {
    static const char *const greetings[] = {
      "哈喽", "哈罗", "哈啰", "哈咯", NULL
    };

    for (size_t i = 0; greetings[i] != NULL; i++) {
      ret = match_chinese_greeting(text, greetings[i]);
      if (ret != NULL) {
        return ret;
      }
    }
  }

  p = find_ascii_nocase(text, "hello");
  while (p != NULL) {
    ret = match_openvela_after(p + strlen("hello"));
    if (ret != NULL) {
      return ret;
    }
    p = find_ascii_nocase(p + strlen("hello"), "hello");
  }

  return NULL;
}
