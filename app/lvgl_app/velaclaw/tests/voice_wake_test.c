#include "../src/voice/voice_wake.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

static void expect_remainder(const char *input, const char *expected)
{
  const char *actual = voice_wake_remainder(input);

  assert(actual != NULL);
  assert(strcmp(actual, expected) == 0);
}

int main(void)
{
  expect_remainder("你好，openvela", "");
  expect_remainder("你好 Open Vela，今天天气", "今天天气");
  expect_remainder("Hello, openvela!", "");
  expect_remainder("Hello，OpenV啦。", "");
  expect_remainder("Hello，OpenVla。", "");
  expect_remainder("哈喽，OpenVela。", "");
  expect_remainder("你好，open v拉，播放音乐", "播放音乐");
  expect_remainder("你好，哦盆喂啦", "");
  expect_remainder("哈喽哦盆喂啦，今天天气", "今天天气");
  expect_remainder("哈罗，欧盆维拉。", "");
  expect_remainder("哈啰噢喷威啦，播放音乐", "播放音乐");
  expect_remainder("你好，奥喷微拉！", "");
  expect_remainder("你好，欧鹏卫拉。", "");
  expect_remainder("你好，欧鹏维拉。", "");
  expect_remainder("Hello，欧朋维拉。", "");
  expect_remainder("你好，哦，莫妮拉。", "");
  expect_remainder("hello open vela, what time is it?", "what time is it?");
  expect_remainder("noise HELLO---OPEN VELA: play music", "play music");

  assert(voice_wake_remainder("hello world") == NULL);
  assert(voice_wake_remainder("openvela") == NULL);
  assert(voice_wake_remainder("OpenV啦") == NULL);
  assert(voice_wake_remainder("哦盆喂啦") == NULL);
  assert(voice_wake_remainder("hello openvelarium") == NULL);
  assert(voice_wake_remainder("你好，打开音乐") == NULL);
  assert(voice_wake_remainder("你好，我们OK了。") == NULL);
  assert(voice_wake_remainder("你好，哦盆栽花") == NULL);
  assert(voice_wake_remainder("你好，我叫拉拉。") == NULL);
  assert(voice_wake_remainder("莫妮拉") == NULL);
  assert(voice_wake_remainder(NULL) == NULL);
  return 0;
}
