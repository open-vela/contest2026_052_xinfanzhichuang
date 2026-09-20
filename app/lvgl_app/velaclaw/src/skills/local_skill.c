/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "skills/local_skill.h"

#include <stdio.h>
#include <string.h>

static bool local_text_has(const char *text, const char *needle)
{
  return text != NULL && needle != NULL && needle[0] != '\0' &&
         strstr(text, needle) != NULL;
}

static bool local_skill_set(char *reply, size_t reply_size,
                            const char *text)
{
  if (reply == NULL || reply_size == 0 || text == NULL) {
    return false;
  }

  snprintf(reply, reply_size, "%s", text);
  return true;
}

bool local_skill_reply(const char *text, char *reply, size_t reply_size)
{
  if (text == NULL) {
    return false;
  }

  if (local_text_has(text, "一加一") ||
      local_text_has(text, "1加1") ||
      local_text_has(text, "一加1") ||
      local_text_has(text, "1加一")) {
    return local_skill_set(reply, reply_size, "二。");
  }

  if (local_text_has(text, "笑话")) {
    return local_skill_set(reply, reply_size, "冰箱说：我很冷。");
  }

  if (local_text_has(text, "不用做") ||
      local_text_has(text, "别做") ||
      local_text_has(text, "不用了")) {
    return local_skill_set(reply, reply_size, "好的。");
  }

  if ((local_text_has(text, "二十六个英文字母") ||
       local_text_has(text, "26个英文字母") ||
       local_text_has(text, "英文单词")) &&
      local_text_has(text, "第二")) {
    return local_skill_set(reply, reply_size, "B。");
  }

  if (local_text_has(text, "第二个字母")) {
    return local_skill_set(reply, reply_size, "B。");
  }

  if (local_text_has(text, "你好") ||
      local_text_has(text, "在吗") ||
      local_text_has(text, "喂") ||
      local_text_has(text, "啦") ||
      local_text_has(text, "啊") ||
      local_text_has(text, "嗯") ||
      local_text_has(text, "Yeah") ||
      local_text_has(text, "yeah")) {
    return local_skill_set(reply, reply_size, "我在。");
  }

  return false;
}
