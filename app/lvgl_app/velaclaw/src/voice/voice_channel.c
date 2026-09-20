/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#include "voice/voice_channel.h"

#include "bus/message_bus.h"
#include "config/config_store.h"
#include "skills/local_skill.h"
#include "velaclaw_compat.h"
#include "velaclaw_config.h"
#include "voice/audio_capture.h"
#include "voice/audio_playback.h"
#include "voice/mimo_asr.h"
#include "voice/mimo_tts.h"
#include "voice/voice_asr.h"
#include "voice/voice_tts.h"
#include "voice_vad.h"
#include "voice_wake.h"
#include "voice/volc_asr.h"
#include "voice/volc_tts.h"

#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
#  include "../../../ui_demo/inc/ui_demo_ai.h"
#endif

#ifdef CONFIG_AIC_INPUT_BUTTON
#  include <input/button.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <malloc.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

static const char *TAG = "voice";

#define ASR_THREAD_STACK (24 * 1024)
#define VOICE_SUPPRESS_AFTER_TTS_MS 600
#define VOICE_REPLY_TIMEOUT_MS 16000
#define VOICE_TEXT_CAP 512
#define VOICE_TEST_MAX_PCM_BYTES (640 * 1024)
#define VOICE_UTTERANCE_QUEUE_DEPTH 1
#define VOICE_WAKE_ACK_TEXT "我在"
#define VOICE_BUSY_SLEEP_US (20 * 1000)
#define VOICE_TTS_CACHE_DIR "/data/velaclaw/tts_cache"
#define VOICE_TTS_CACHE_MAX_TEXT_BYTES 96
#define VOICE_TTS_CACHE_IO_CHUNK 4096

enum voice_mode
{
  VOICE_MODE_OFF = 0,
  VOICE_MODE_WAKE_LISTEN,
  VOICE_MODE_DIALOG
};

typedef struct
{
  unsigned char *pcm;
  size_t len;
} voice_utterance_t;

static struct
{
  pthread_mutex_t lock;
  pthread_cond_t stopped_cond;
  pthread_mutex_t work_lock;
  pthread_cond_t work_cond;
  pthread_mutex_t capture_lock;
  enum voice_mode mode;
  bool initialized;
  bool running;
  bool stop_requested;
  bool worker_stop;
  bool worker_started;
  bool speaking;
  bool awaiting_reply;
  bool capture_suspended_for_reply;
  bool recognizing;
  bool direct_dialog;
  bool button_subscribed;
  bool reset_vad;
  audio_capture_t *cap;
  pthread_t thread;
  pthread_t worker_thread;
  voice_utterance_t work_queue[VOICE_UTTERANCE_QUEUE_DEPTH];
  unsigned int work_head;
  unsigned int work_tail;
  unsigned int work_count;
  char wake_word[64];
  unsigned int idle_timeout_ms;
  unsigned int max_utterance_ms;
  unsigned int vad_min_energy;
  unsigned int generation;
  uint64_t last_activity_ms;
  uint64_t reply_started_ms;
  uint64_t suppress_until_ms;
} s_voice = {
  .lock = PTHREAD_MUTEX_INITIALIZER,
  .stopped_cond = PTHREAD_COND_INITIALIZER,
  .work_lock = PTHREAD_MUTEX_INITIALIZER,
  .work_cond = PTHREAD_COND_INITIALIZER,
  .capture_lock = PTHREAD_MUTEX_INITIALIZER,
  .mode = VOICE_MODE_OFF,
};

static uint64_t voice_now_ms(void)
{
  struct timespec ts;

#ifdef CLOCK_MONOTONIC
  clock_gettime(CLOCK_MONOTONIC, &ts);
#else
  clock_gettime(CLOCK_REALTIME, &ts);
#endif
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static unsigned int voice_pcm_len_to_ms(size_t pcm_len)
{
  unsigned int bytes_per_ms =
    VELACLAW_VOICE_SAMPLE_RATE * VELACLAW_VOICE_CHANNELS *
    (VELACLAW_VOICE_BITS / 8) / 1000;

  return bytes_per_ms == 0 ? 0 : (unsigned int)(pcm_len / bytes_per_ms);
}

static int write_pcm_file(const char *path,
                          const unsigned char *data, size_t len);

static unsigned int voice_current_generation(void)
{
  unsigned int gen;

  pthread_mutex_lock(&s_voice.lock);
  gen = s_voice.generation;
  pthread_mutex_unlock(&s_voice.lock);
  return gen;
}

static void voice_clear_work_queue_locked(void)
{
  while (s_voice.work_count > 0) {
    voice_utterance_t item = s_voice.work_queue[s_voice.work_head];

    memset(&s_voice.work_queue[s_voice.work_head], 0,
           sizeof(s_voice.work_queue[s_voice.work_head]));
    s_voice.work_head =
      (s_voice.work_head + 1) % VOICE_UTTERANCE_QUEUE_DEPTH;
    s_voice.work_count--;
    free(item.pcm);
  }
  s_voice.work_head = 0;
  s_voice.work_tail = 0;
}

static unsigned int cfg_get_uint(const char *key, unsigned int def)
{
  char buf[32];
  char *endp;
  unsigned long value;

  if (claw_config_get(key, buf, sizeof(buf)) != OK || buf[0] == '\0') {
    return def;
  }

  value = strtoul(buf, &endp, 10);
  if (endp == buf || value == 0 || value > 600000) {
    return def;
  }

  return (unsigned int)value;
}

static void cfg_get_string(const char *key, const char *def,
                           char *buf, size_t cap)
{
  if (cap == 0) {
    return;
  }

  if (claw_config_get(key, buf, cap) == OK && buf[0] != '\0') {
    return;
  }

  snprintf(buf, cap, "%s", def ? def : "");
}

static uint32_t voice_hash32(const char *text)
{
  uint32_t h = 2166136261u;

  if (text == NULL) {
    return h;
  }

  while (*text != '\0') {
    h ^= (unsigned char)*text++;
    h *= 16777619u;
  }

  return h;
}

static bool voice_tts_cache_path(const char *text,
                                 unsigned int sample_rate,
                                 char *path, size_t cap)
{
  size_t len;

  if (text == NULL || path == NULL || cap == 0) {
    return false;
  }

  len = strlen(text);
  if (len == 0 || len > VOICE_TTS_CACHE_MAX_TEXT_BYTES) {
    return false;
  }

  snprintf(path, cap, "%s/%08lx_%u_%u_%u.pcm",
           VOICE_TTS_CACHE_DIR, (unsigned long)voice_hash32(text),
           sample_rate, VELACLAW_VOICE_CHANNELS, VELACLAW_VOICE_BITS);
  return true;
}

static void voice_tts_cache_prepare(void)
{
  (void)mkdir("/data/velaclaw", 0755);
  (void)mkdir(VOICE_TTS_CACHE_DIR, 0755);
}

static int voice_play_cached_pcm(const char *path, unsigned int sample_rate)
{
  unsigned char buf[VOICE_TTS_CACHE_IO_CHUNK];
  audio_playback_t *pb;
  struct stat st;
  uint64_t start_ms;
  size_t total = 0;
  int fd;
  int ret = 0;

  if (path == NULL || stat(path, &st) < 0 || st.st_size <= 0) {
    return -ENOENT;
  }

  fd = open(path, O_RDONLY);
  if (fd < 0) {
    return -errno;
  }

  pb = audio_playback_open(VELACLAW_AUDIO_PLAYBACK_DEV,
                           sample_rate,
                           VELACLAW_VOICE_CHANNELS,
                           VELACLAW_VOICE_BITS);
  if (pb == NULL) {
    close(fd);
    return -EIO;
  }

  start_ms = voice_now_ms();
  syslog(LOG_INFO, "[%s] TTS cache hit: %s (%ld bytes)\n",
         TAG, path, (long)st.st_size);

  for (;;) {
    ssize_t n = read(fd, buf, sizeof(buf));

    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      ret = -errno;
      break;
    }
    if (n == 0) {
      break;
    }

    ret = audio_playback_write(pb, buf, (size_t)n);
    if (ret < 0) {
      break;
    }
    total += (size_t)n;
  }
  close(fd);

  {
    int pret = audio_playback_close(pb);

    if (ret >= 0 && pret < 0) {
      ret = pret;
    }
  }

  syslog(LOG_INFO, "[%s] TTS cache playback done ret=%d bytes=%zu in %llums\n",
         TAG, ret, total,
         (unsigned long long)(voice_now_ms() - start_ms));
  return ret < 0 ? ret : 0;
}

static bool text_has(const char *text, const char *needle)
{
  return text != NULL && needle != NULL && needle[0] != '\0' &&
         strstr(text, needle) != NULL;
}

static bool voice_wake_retry_candidate(const char *text)
{
  if (text == NULL) {
    return false;
  }

  return text_has(text, "你好") || text_has(text, "哈喽") ||
         text_has(text, "哈罗") || text_has(text, "哈啰") ||
         text_has(text, "哈咯") ||
         strcasestr(text, "hello") != NULL;
}

static bool is_exit_phrase(const char *text)
{
  return text_has(text, "退出") ||
         text_has(text, "结束对话") ||
         text_has(text, "停止对话") ||
         text_has(text, "关闭对话") ||
         text_has(text, "不用了");
}

static bool voice_is_low_confidence_asr(const char *text)
{
  static const char *const noise[] = {
    "高", "高。",
    "啊", "啊。",
    "嗯", "嗯。",
    "呃", "呃。",
    "额", "额。",
    "哦", "哦。",
    "啦", "啦。",
    "哈", "哈。",
    "三一", "三一。",
    "Good job", "Good job.",
    "good job", "good job.",
    NULL
  };

  if (text == NULL || text[0] == '\0') {
    return true;
  }

  for (int i = 0; noise[i] != NULL; i++) {
    if (strcmp(text, noise[i]) == 0) {
      return true;
    }
  }

  return false;
}

static int push_voice_prompt(const char *text)
{
  velaclaw_msg_t msg;
  unsigned int gen;

  if (text == NULL || text[0] == '\0') {
    return -EINVAL;
  }

  gen = voice_current_generation();
  memset(&msg, 0, sizeof(msg));
  snprintf(msg.channel, sizeof(msg.channel), "%s", VELACLAW_CHAN_VOICE);
  snprintf(msg.chat_id, sizeof(msg.chat_id), "voice:%u", gen);
  msg.content = strdup(text);
  if (msg.content == NULL) {
    return -ENOMEM;
  }

  if (message_bus_push_inbound(&msg) != OK) {
    free(msg.content);
    return -EIO;
  }

  return 0;
}

bool voice_channel_accept_response(const char *chat_id)
{
  unsigned int gen;
  unsigned int current;

  if (chat_id == NULL || strncmp(chat_id, "voice:", 6) != 0) {
    return true;
  }

  gen = (unsigned int)strtoul(chat_id + 6, NULL, 10);
  current = voice_current_generation();
  if (gen != current) {
    syslog(LOG_INFO,
           "[%s] drop stale voice response chat_id=%s current=%u\n",
           TAG, chat_id, current);
    return false;
  }

  return true;
}

static void voice_play_wake_ack(void)
{
  syslog(LOG_INFO, "[%s] wake ack: %s\n", TAG, VOICE_WAKE_ACK_TEXT);
  printf("Voice: %s\n", VOICE_WAKE_ACK_TEXT);
#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
  ui_demo_ai_set_reply(VOICE_WAKE_ACK_TEXT);
#endif
  (void)voice_channel_speak(VOICE_WAKE_ACK_TEXT);
}

static void voice_enter_dialog_locked(uint64_t now)
{
  s_voice.mode = VOICE_MODE_DIALOG;
  s_voice.last_activity_ms = now;
}

static void voice_enter_wake_listen_locked(uint64_t now)
{
  s_voice.mode = VOICE_MODE_WAKE_LISTEN;
  s_voice.awaiting_reply = false;
  s_voice.speaking = false;
  s_voice.recognizing = false;
  s_voice.last_activity_ms = now;
}

static void voice_set_recognizing(bool recognizing)
{
  pthread_mutex_lock(&s_voice.lock);
  s_voice.recognizing = recognizing;
  if (recognizing) {
    s_voice.last_activity_ms = voice_now_ms();
  }
  pthread_mutex_unlock(&s_voice.lock);
}

static void voice_suspend_capture_for_reply(void)
{
  audio_capture_t *cap = NULL;
  struct mallinfo before;
  struct mallinfo after;

  pthread_mutex_lock(&s_voice.capture_lock);
  pthread_mutex_lock(&s_voice.lock);
  if (s_voice.running && !s_voice.stop_requested &&
      s_voice.awaiting_reply && s_voice.cap != NULL) {
    cap = s_voice.cap;
    s_voice.cap = NULL;
    s_voice.capture_suspended_for_reply = true;
    s_voice.reset_vad = true;
  }
  pthread_mutex_unlock(&s_voice.lock);

  if (cap != NULL) {
    before = mallinfo();
    (void)audio_capture_stop(cap);
    audio_capture_close(cap);
    after = mallinfo();
    syslog(LOG_INFO,
           "[%s] capture released while awaiting reply: free=%d -> %d\n",
           TAG, before.fordblks, after.fordblks);
  }
  pthread_mutex_unlock(&s_voice.capture_lock);
}

static void load_runtime_config(void)
{
  unsigned int idle_timeout_ms;
  unsigned int max_utterance_ms;
  unsigned int vad_min_energy;

  idle_timeout_ms =
    cfg_get_uint(VELACLAW_CFG_KEY_VOICE_IDLE_TIMEOUT_MS,
                 VELACLAW_VOICE_IDLE_TIMEOUT_MS);
  max_utterance_ms =
    cfg_get_uint(VELACLAW_CFG_KEY_VOICE_MAX_UTTERANCE_MS,
                 VELACLAW_VOICE_MAX_UTTERANCE_MS);
  vad_min_energy =
    cfg_get_uint(VELACLAW_CFG_KEY_VOICE_VAD_MIN_ENERGY,
                 VELACLAW_VOICE_VAD_MIN_ENERGY);

  if (idle_timeout_ms > 0 && idle_timeout_ms < VELACLAW_VOICE_IDLE_TIMEOUT_MS) {
    idle_timeout_ms = VELACLAW_VOICE_IDLE_TIMEOUT_MS;
  }
  if (max_utterance_ms > VELACLAW_VOICE_MAX_UTTERANCE_MS) {
    max_utterance_ms = VELACLAW_VOICE_MAX_UTTERANCE_MS;
  }

  pthread_mutex_lock(&s_voice.lock);
  snprintf(s_voice.wake_word, sizeof(s_voice.wake_word), "%s",
           VELACLAW_VOICE_WAKE_WORD);
  s_voice.idle_timeout_ms = idle_timeout_ms;
  s_voice.max_utterance_ms = max_utterance_ms;
  s_voice.vad_min_energy = vad_min_energy;
  pthread_mutex_unlock(&s_voice.lock);
}

static void select_configured_backends(void)
{
  char name[32];
  int ret;

  cfg_get_string(VELACLAW_CFG_KEY_VOICE_ASR_BACKEND,
                 CONFIG_EXAMPLES_VELACLAW_VOICE_ASR_DEFAULT_BACKEND,
                 name, sizeof(name));
  ret = voice_asr_set_backend(name);
  if (ret < 0 && strcmp(name, "mimo") != 0) {
    (void)voice_asr_set_backend("mimo");
  }

  cfg_get_string(VELACLAW_CFG_KEY_VOICE_TTS_BACKEND,
                 CONFIG_EXAMPLES_VELACLAW_VOICE_TTS_DEFAULT_BACKEND,
                 name, sizeof(name));
  ret = voice_tts_set_backend(name);
  if (ret < 0 && strcmp(name, "mimo") != 0) {
    (void)voice_tts_set_backend("mimo");
  }
}

#ifdef CONFIG_AIC_INPUT_BUTTON
static void voice_button_cb(const struct aic_button_event *event, void *arg)
{
  uint64_t now;

  (void)arg;

  if (event == NULL || event->id != AIC_BUTTON_WAKEUP ||
      event->action != AIC_BUTTON_PRESSED) {
    return;
  }

  now = voice_now_ms();
  pthread_mutex_lock(&s_voice.lock);
  if (s_voice.running && !s_voice.stop_requested) {
    voice_enter_dialog_locked(now);
    s_voice.awaiting_reply = false;
    s_voice.suppress_until_ms = now + 200;
    syslog(LOG_INFO, "[%s] WAKEUP button entered dialog mode\n", TAG);
  }
  pthread_mutex_unlock(&s_voice.lock);
}

static void voice_button_attach(void)
{
  int ret;

  ret = aic_button_subscribe(voice_button_cb, NULL);
  if (ret == 0 || ret == -EALREADY) {
    pthread_mutex_lock(&s_voice.lock);
    s_voice.button_subscribed = true;
    pthread_mutex_unlock(&s_voice.lock);
  } else {
    syslog(LOG_WARNING, "[%s] button subscribe failed: %d\n", TAG, ret);
  }

  ret = aic_button_service_start();
  if (ret < 0) {
    syslog(LOG_WARNING, "[%s] button service start failed: %d\n", TAG, ret);
  }
}

static void voice_button_detach(void)
{
  bool subscribed;

  pthread_mutex_lock(&s_voice.lock);
  subscribed = s_voice.button_subscribed;
  s_voice.button_subscribed = false;
  pthread_mutex_unlock(&s_voice.lock);

  if (subscribed) {
    aic_button_unsubscribe(voice_button_cb, NULL);
  }
}
#else
static void voice_button_attach(void)
{
}

static void voice_button_detach(void)
{
}
#endif

static void process_utterance(unsigned char *pcm, size_t pcm_len,
                              bool heap_owned)
{
  char text[VOICE_TEXT_CAP];
  char retry_text[VOICE_TEXT_CAP];
  const char *prompt = NULL;
  enum voice_mode mode;
  bool busy;
  bool direct_dialog;
  bool wake_stripped = false;
  bool running;
  uint64_t now;
  uint64_t asr_start_ms;
  bool wake_recognition;
  int ret;

  if (pcm == NULL || pcm_len == 0) {
    if (heap_owned) {
      free(pcm);
    }
    return;
  }

  syslog(LOG_INFO, "[%s] ASR: recognizing %zu bytes\n", TAG, pcm_len);
  asr_start_ms = voice_now_ms();
  voice_set_recognizing(true);
  pthread_mutex_lock(&s_voice.lock);
  wake_recognition = s_voice.mode == VOICE_MODE_WAKE_LISTEN &&
                     !s_voice.direct_dialog;
  pthread_mutex_unlock(&s_voice.lock);
  text[0] = '\0';
  ret = voice_asr_recognize(pcm, pcm_len, text, sizeof(text));

  if (ret == 0 && text[0] != '\0' && wake_recognition &&
      voice_wake_remainder(text) == NULL &&
      voice_wake_retry_candidate(text)) {
    int retry_ret;

    retry_text[0] = '\0';
    syslog(LOG_INFO,
           "[%s] wake ASR auto miss; retrying same audio language=zh\n",
           TAG);
    retry_ret = voice_asr_recognize_language(pcm, pcm_len, "zh",
                                              retry_text,
                                              sizeof(retry_text));
    if (retry_ret == 0 && retry_text[0] != '\0') {
      syslog(LOG_INFO, "[%s] wake ASR zh result: %s\n", TAG, retry_text);
      if (voice_wake_remainder(retry_text) != NULL) {
        snprintf(text, sizeof(text), "%s", retry_text);
      }
    } else if (retry_ret != -ENOSYS) {
      syslog(LOG_WARNING, "[%s] wake ASR zh retry failed: %d\n",
             TAG, retry_ret);
    }
  }
  if (heap_owned) {
    free(pcm);
  }
  voice_set_recognizing(false);
  syslog(LOG_INFO, "[%s] ASR done in %llums ret=%d\n",
         TAG, (unsigned long long)(voice_now_ms() - asr_start_ms), ret);

  if (ret != 0 || text[0] == '\0') {
    syslog(LOG_WARNING, "[%s] ASR failed or empty: %d\n", TAG, ret);
    return;
  }

  syslog(LOG_INFO, "[%s] ASR result: %s\n", TAG, text);
  printf("ASR: %s\n", text);
  now = voice_now_ms();

#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
  ui_demo_ai_set_turn("语音", text, "已识别，等待 AI 回复...", true);
#endif

  /*
   * The capture thread can finish another utterance while this request is
   * in flight.  Decide what to do with that result using the current state,
   * rather than the state from before the network request.
   */
  pthread_mutex_lock(&s_voice.lock);
  mode = s_voice.mode;
  direct_dialog = s_voice.direct_dialog;
  running = s_voice.running && !s_voice.stop_requested;
  busy = s_voice.speaking || s_voice.awaiting_reply;
  pthread_mutex_unlock(&s_voice.lock);

  if (!running || busy) {
    syslog(LOG_INFO, "[%s] drop ASR result while voice state is busy\n", TAG);
    return;
  }

  if (mode == VOICE_MODE_WAKE_LISTEN && !direct_dialog) {
    prompt = voice_wake_remainder(text);
    wake_stripped = prompt != NULL;
    if (prompt == NULL) {
      syslog(LOG_INFO,
             "[%s] ignored ASR result in wake mode: no wake phrase\n",
             TAG);
      return;
    }
  }

  if (voice_is_low_confidence_asr(text)) {
    syslog(LOG_WARNING,
           "[%s] low-confidence ASR ignored: \"%.*s\" (%zu bytes pcm=%ums)\n",
           TAG, 80, text, strlen(text), voice_pcm_len_to_ms(pcm_len));
#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
    ui_demo_ai_set_turn("语音", text, "没听清，请再说一遍。", false);
#endif
    (void)voice_channel_speak("没听清，请再说一遍。");
    return;
  }

  if (is_exit_phrase(text)) {
    pthread_mutex_lock(&s_voice.lock);
    if (s_voice.running) {
      if (s_voice.direct_dialog) {
        voice_enter_dialog_locked(now);
        s_voice.awaiting_reply = false;
        s_voice.speaking = false;
      } else {
        voice_enter_wake_listen_locked(now);
      }
    }
    pthread_mutex_unlock(&s_voice.lock);
    printf(direct_dialog ?
           "Voice: direct dialog listening.\n" :
           "Voice: dialog ended, wake listening.\n");
    return;
  }

  if (mode == VOICE_MODE_WAKE_LISTEN) {
    if (prompt == NULL) {
      prompt = voice_wake_remainder(text);
      wake_stripped = prompt != NULL;
    }
    if (prompt == NULL) {
      if (!direct_dialog) {
        syslog(LOG_INFO,
               "[%s] ignored ASR result in wake mode: no wake word\n",
               TAG);
        return;
      }
      prompt = text;
    }

    pthread_mutex_lock(&s_voice.lock);
    if (s_voice.running && !s_voice.stop_requested) {
      voice_enter_dialog_locked(now);
    }
    pthread_mutex_unlock(&s_voice.lock);

    if (prompt[0] == '\0') {
      printf("Voice: wake word detected.\n");
      voice_play_wake_ack();
      return;
    }
  } else if (mode == VOICE_MODE_DIALOG) {
    const char *after_wake = voice_wake_remainder(text);
    if (after_wake != NULL) {
      wake_stripped = true;
      if (after_wake[0] == '\0') {
        pthread_mutex_lock(&s_voice.lock);
        if (s_voice.running && !s_voice.stop_requested) {
          s_voice.last_activity_ms = now;
        }
        pthread_mutex_unlock(&s_voice.lock);
        syslog(LOG_INFO,
               "[%s] wake-only utterance handled locally in dialog mode\n",
               TAG);
        voice_play_wake_ack();
        return;
      }
      prompt = after_wake;
    } else {
      prompt = text;
    }
  } else {
    return;
  }

  if (strcmp(prompt, "讲") == 0 ||
      strcmp(prompt, "讲。") == 0 ||
      strcmp(prompt, "讲一下") == 0 ||
      strcmp(prompt, "讲一下。") == 0) {
    prompt = "讲个短笑话，20个字以内。";
  }

  {
    char local[96];
    if (local_skill_reply(prompt, local, sizeof(local))) {
      syslog(LOG_INFO, "[%s] local voice reply: \"%.*s\" -> \"%s\"\n",
             TAG, 80, prompt, local);
      (void)voice_channel_speak(local);
      return;
    }
  }

  pthread_mutex_lock(&s_voice.lock);
  if (!s_voice.running || s_voice.stop_requested) {
    pthread_mutex_unlock(&s_voice.lock);
    return;
  }
  s_voice.awaiting_reply = true;
  s_voice.reply_started_ms = now;
  s_voice.last_activity_ms = now;
  pthread_mutex_unlock(&s_voice.lock);

  voice_suspend_capture_for_reply();

  syslog(LOG_INFO,
         "[%s] prompt queued: mode=%d direct=%d stripped=%d \"%.*s\"\n",
         TAG, (int)mode, direct_dialog ? 1 : 0, wake_stripped ? 1 : 0,
         80, prompt);
  ret = push_voice_prompt(prompt);
  if (ret < 0) {
    pthread_mutex_lock(&s_voice.lock);
    s_voice.awaiting_reply = false;
    pthread_mutex_unlock(&s_voice.lock);
    syslog(LOG_ERR, "[%s] push voice prompt failed: %d\n", TAG, ret);
#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
    ui_demo_ai_set_reply("语音消息发送失败");
#endif
  }
}

static int submit_utterance(unsigned char *pcm, size_t pcm_len)
{
  int ret = 0;

  if (pcm == NULL || pcm_len == 0) {
    free(pcm);
    return -EINVAL;
  }

  pthread_mutex_lock(&s_voice.work_lock);
  if (s_voice.worker_stop ||
      s_voice.work_count >= VOICE_UTTERANCE_QUEUE_DEPTH) {
    ret = -EBUSY;
  } else {
    s_voice.work_queue[s_voice.work_tail].pcm = pcm;
    s_voice.work_queue[s_voice.work_tail].len = pcm_len;
    s_voice.work_tail =
      (s_voice.work_tail + 1) % VOICE_UTTERANCE_QUEUE_DEPTH;
    s_voice.work_count++;
    voice_set_recognizing(true);
    pthread_cond_signal(&s_voice.work_cond);
  }
  pthread_mutex_unlock(&s_voice.work_lock);

  if (ret < 0) {
    syslog(LOG_WARNING, "[%s] ASR queue full/stopped, dropping %zu bytes\n",
           TAG, pcm_len);
    free(pcm);
  }
  return ret;
}

static void *voice_asr_worker(void *arg)
{
  (void)arg;

  while (1) {
    voice_utterance_t item;

    memset(&item, 0, sizeof(item));
    pthread_mutex_lock(&s_voice.work_lock);
    while (s_voice.work_count == 0 && !s_voice.worker_stop) {
      pthread_cond_wait(&s_voice.work_cond, &s_voice.work_lock);
    }

    if (s_voice.worker_stop) {
      while (s_voice.work_count > 0) {
        item = s_voice.work_queue[s_voice.work_head];
        memset(&s_voice.work_queue[s_voice.work_head], 0,
               sizeof(s_voice.work_queue[s_voice.work_head]));
        s_voice.work_head =
          (s_voice.work_head + 1) % VOICE_UTTERANCE_QUEUE_DEPTH;
        s_voice.work_count--;
        free(item.pcm);
      }
      pthread_mutex_unlock(&s_voice.work_lock);
      voice_set_recognizing(false);
      break;
    }

    item = s_voice.work_queue[s_voice.work_head];
    memset(&s_voice.work_queue[s_voice.work_head], 0,
           sizeof(s_voice.work_queue[s_voice.work_head]));
    s_voice.work_head =
      (s_voice.work_head + 1) % VOICE_UTTERANCE_QUEUE_DEPTH;
    s_voice.work_count--;
    pthread_mutex_unlock(&s_voice.work_lock);

    process_utterance(item.pcm, item.len, true);
  }

  return NULL;
}

static bool voice_should_discard_audio(uint64_t now)
{
  bool discard;

  pthread_mutex_lock(&s_voice.lock);
  if (s_voice.awaiting_reply &&
      now - s_voice.reply_started_ms > VOICE_REPLY_TIMEOUT_MS) {
    s_voice.awaiting_reply = false;
    s_voice.generation++;
    s_voice.last_activity_ms = now;
    syslog(LOG_WARNING,
           "[%s] reply timeout, resume listening (generation=%u)\n",
           TAG, s_voice.generation);
  }
  discard = s_voice.speaking || s_voice.awaiting_reply ||
            s_voice.recognizing ||
            now < s_voice.suppress_until_ms;
  pthread_mutex_unlock(&s_voice.lock);
  return discard;
}

static int voice_reopen_capture_locked(audio_capture_t **pcap)
{
  audio_capture_t *old_cap;
  audio_capture_t *new_cap;
  int ret;

  if (pcap == NULL) {
    return -EINVAL;
  }

  old_cap = *pcap;
  if (old_cap != NULL) {
    pthread_mutex_lock(&s_voice.lock);
    if (s_voice.cap == old_cap) {
      s_voice.cap = NULL;
    }
    pthread_mutex_unlock(&s_voice.lock);

    audio_capture_close(old_cap);
  }

  new_cap = audio_capture_open(VELACLAW_AUDIO_CAPTURE_DEV,
                               VELACLAW_VOICE_SAMPLE_RATE,
                               VELACLAW_VOICE_CHANNELS,
                               VELACLAW_VOICE_BITS);
  if (new_cap == NULL) {
    syslog(LOG_ERR, "[%s] hard DMIC reopen failed: open\n", TAG);
    *pcap = NULL;
    return -EIO;
  }

  ret = audio_capture_start(new_cap);
  if (ret < 0) {
    syslog(LOG_ERR, "[%s] hard DMIC reopen failed: start=%d\n",
           TAG, ret);
    audio_capture_close(new_cap);
    *pcap = NULL;
    return ret;
  }

  *pcap = new_cap;
  pthread_mutex_lock(&s_voice.lock);
  if (s_voice.running && !s_voice.stop_requested) {
    s_voice.cap = new_cap;
    s_voice.capture_suspended_for_reply = false;
    s_voice.reset_vad = true;
    s_voice.last_activity_ms = voice_now_ms();
  }
  pthread_mutex_unlock(&s_voice.lock);

  syslog(LOG_INFO, "[%s] hard DMIC reopened\n", TAG);
  return 0;
}

static void *voice_listen_thread(void *arg)
{
  audio_capture_t *cap = (audio_capture_t *)arg;
  unsigned char chunk[VELACLAW_ASR_CHUNK_SIZE];
  voice_vad_t vad;
  voice_vad_config_t vad_cfg;
  bool initial_direct_dialog;
  bool vad_buffers_released = false;
  int capture_errors = 0;
  int ret;

  pthread_mutex_lock(&s_voice.lock);
  initial_direct_dialog = s_voice.direct_dialog;
  memset(&vad_cfg, 0, sizeof(vad_cfg));
  vad_cfg.sample_rate = VELACLAW_VOICE_SAMPLE_RATE;
  vad_cfg.channels = VELACLAW_VOICE_CHANNELS;
  vad_cfg.bits_per_sample = VELACLAW_VOICE_BITS;
  vad_cfg.min_utterance_ms = VELACLAW_VOICE_MIN_UTTERANCE_MS;
  vad_cfg.max_utterance_ms = s_voice.max_utterance_ms;
  if (!initial_direct_dialog &&
      vad_cfg.max_utterance_ms > VELACLAW_VOICE_WAKE_MAX_UTTERANCE_MS) {
    vad_cfg.max_utterance_ms = VELACLAW_VOICE_WAKE_MAX_UTTERANCE_MS;
  }
  vad_cfg.silence_ms = VELACLAW_VOICE_VAD_SILENCE_MS;
  if (!initial_direct_dialog &&
      vad_cfg.silence_ms > VELACLAW_VOICE_WAKE_VAD_SILENCE_MS) {
    vad_cfg.silence_ms = VELACLAW_VOICE_WAKE_VAD_SILENCE_MS;
  }
  vad_cfg.start_chunks = VELACLAW_VOICE_VAD_START_CHUNKS;
  vad_cfg.min_energy = s_voice.vad_min_energy;
  pthread_mutex_unlock(&s_voice.lock);

  ret = voice_vad_init(&vad, &vad_cfg);
  if (ret < 0) {
    syslog(LOG_ERR, "[%s] VAD init failed: %d\n", TAG, ret);
    goto out;
  }

  syslog(LOG_INFO,
         "[%s] listening started, mode=%s wake_word=%s vad={min=%u max=%u "
         "silence=%u start=%u energy=%u}\n",
         TAG, initial_direct_dialog ? "direct" : "wake", s_voice.wake_word,
         vad_cfg.min_utterance_ms, vad_cfg.max_utterance_ms,
         vad_cfg.silence_ms, vad_cfg.start_chunks, vad_cfg.min_energy);

  while (1) {
    bool stop;
    enum voice_mode mode;
    unsigned int idle_timeout_ms;
    uint64_t last_activity_ms;
    bool direct_dialog;
    uint64_t now;
    int n;

    pthread_mutex_lock(&s_voice.lock);
    stop = s_voice.stop_requested;
    mode = s_voice.mode;
    idle_timeout_ms = s_voice.idle_timeout_ms;
    last_activity_ms = s_voice.last_activity_ms;
    direct_dialog = s_voice.direct_dialog;
    pthread_mutex_unlock(&s_voice.lock);
    if (stop) {
      break;
    }

    now = voice_now_ms();
    if (voice_should_discard_audio(now)) {
      if (!vad_buffers_released) {
        voice_vad_release_buffers(&vad);
        vad_buffers_released = true;
        syslog(LOG_INFO, "[%s] VAD buffers released while busy\n", TAG);
      }

      /* Keep the two-period DMA ring drained while ASR/the agent is busy.
       * TTS temporarily removes s_voice.cap, so a NULL capture still sleeps. */
      pthread_mutex_lock(&s_voice.capture_lock);
      pthread_mutex_lock(&s_voice.lock);
      cap = s_voice.cap;
      pthread_mutex_unlock(&s_voice.lock);
      n = cap != NULL ? audio_capture_read(cap, chunk, sizeof(chunk)) : -EPIPE;
      pthread_mutex_unlock(&s_voice.capture_lock);
      if (n <= 0) {
        usleep(VOICE_BUSY_SLEEP_US);
      }
      continue;
    }

    vad_buffers_released = false;

    pthread_mutex_lock(&s_voice.capture_lock);
    pthread_mutex_lock(&s_voice.lock);
    cap = s_voice.cap;
    pthread_mutex_unlock(&s_voice.lock);
    n = cap != NULL ? audio_capture_read(cap, chunk, sizeof(chunk)) : -EPIPE;
    pthread_mutex_unlock(&s_voice.capture_lock);
    if (n <= 0) {
      pthread_mutex_lock(&s_voice.lock);
      stop = s_voice.stop_requested;
      pthread_mutex_unlock(&s_voice.lock);
      if (stop) {
        break;
      }
      if (n == -EAGAIN || n == -ETIMEDOUT || n == -EPIPE) {
        capture_errors++;
        if (capture_errors >= 1) {
          pthread_mutex_lock(&s_voice.capture_lock);
          if (voice_reopen_capture_locked(&cap) == 0) {
            capture_errors = 0;
            voice_vad_reset(&vad);
          }
          pthread_mutex_unlock(&s_voice.capture_lock);
        }
      }
      usleep(20 * 1000);
      continue;
    }
    capture_errors = 0;

    now = voice_now_ms();
    if (voice_should_discard_audio(now)) {
      voice_vad_reset(&vad);
      continue;
    }

    pthread_mutex_lock(&s_voice.lock);
    if (s_voice.reset_vad) {
      s_voice.reset_vad = false;
      pthread_mutex_unlock(&s_voice.lock);
      voice_vad_reset(&vad);
      syslog(LOG_INFO, "[%s] VAD reset after capture resume\n", TAG);
      continue;
    }
    pthread_mutex_unlock(&s_voice.lock);

    if (!direct_dialog &&
        mode == VOICE_MODE_DIALOG &&
        idle_timeout_ms > 0 &&
        now - last_activity_ms > idle_timeout_ms) {
      pthread_mutex_lock(&s_voice.lock);
      if (s_voice.running && s_voice.mode == VOICE_MODE_DIALOG &&
          !s_voice.awaiting_reply && !s_voice.speaking) {
        voice_enter_wake_listen_locked(now);
        syslog(LOG_INFO, "[%s] dialog idle timeout, wake listening\n", TAG);
      }
      pthread_mutex_unlock(&s_voice.lock);
      voice_vad_reset(&vad);
      continue;
    }

    unsigned char *utt = NULL;
    size_t utt_len = 0;

    ret = voice_vad_process(&vad, chunk, (size_t)n, &utt, &utt_len);
    if (ret < 0) {
      syslog(LOG_WARNING, "[%s] VAD error: %d\n", TAG, ret);
      voice_vad_reset(&vad);
      continue;
    }
    if (ret == VOICE_VAD_FINISH_BORROWED) {
      syslog(LOG_WARNING,
             "[%s] VAD utterance using borrowed buffer: %zu bytes %ums\n",
             TAG, utt_len, voice_pcm_len_to_ms(utt_len));
      process_utterance(utt, utt_len, false);
      voice_vad_reset(&vad);
      continue;
    }
    if (ret == 1) {
      syslog(LOG_INFO,
             "[%s] VAD utterance ready: %zu bytes %ums mode=%d "
             "reason=%u threshold=%u noise=%u last_energy=%u "
             "tail_silence=%ums\n",
             TAG, utt_len, voice_pcm_len_to_ms(utt_len), (int)mode,
             vad.last_finish_reason, vad.threshold, vad.noise_floor,
             vad.last_finish_energy,
             vad.bytes_per_ms == 0 ? 0 :
             (unsigned int)(vad.last_finish_silence_bytes /
                            vad.bytes_per_ms));
      if (submit_utterance(utt, utt_len) < 0) {
        voice_vad_reset(&vad);
      }
    }
  }

  voice_vad_deinit(&vad);

out:
  voice_button_detach();
  pthread_mutex_lock(&s_voice.capture_lock);
  pthread_mutex_lock(&s_voice.lock);
  cap = s_voice.cap;
  s_voice.cap = NULL;
  pthread_mutex_unlock(&s_voice.lock);
  if (cap != NULL) {
    audio_capture_stop(cap);
    audio_capture_close(cap);
  }
  pthread_mutex_unlock(&s_voice.capture_lock);

  pthread_mutex_lock(&s_voice.work_lock);
  s_voice.worker_stop = true;
  pthread_cond_broadcast(&s_voice.work_cond);
  pthread_mutex_unlock(&s_voice.work_lock);

  pthread_mutex_lock(&s_voice.lock);
  s_voice.running = false;
  s_voice.stop_requested = false;
  s_voice.speaking = false;
  s_voice.awaiting_reply = false;
  s_voice.capture_suspended_for_reply = false;
  s_voice.recognizing = false;
  s_voice.direct_dialog = false;
  s_voice.mode = VOICE_MODE_OFF;
  pthread_cond_broadcast(&s_voice.stopped_cond);
  pthread_mutex_unlock(&s_voice.lock);

  syslog(LOG_INFO, "[%s] listening stopped\n", TAG);
  return NULL;
}

int voice_channel_init(void)
{
  pthread_mutex_lock(&s_voice.lock);
  if (s_voice.initialized) {
    pthread_mutex_unlock(&s_voice.lock);
    return 0;
  }
  pthread_mutex_unlock(&s_voice.lock);

  syslog(LOG_INFO, "[%s] Voice channel initializing\n", TAG);

#ifdef CONFIG_EXAMPLES_VELACLAW_VOICE_BACKEND_MIMO
  (void)mimo_tts_register();
  (void)mimo_asr_register();
#endif
#ifdef CONFIG_EXAMPLES_VELACLAW_VOICE_BACKEND_VOLC
  (void)volc_tts_register();
  (void)volc_asr_register();
#endif

  select_configured_backends();
  load_runtime_config();

  pthread_mutex_lock(&s_voice.lock);
  s_voice.initialized = true;
  pthread_mutex_unlock(&s_voice.lock);
  return 0;
}

int voice_channel_start(void)
{
  audio_capture_t *cap;
  pthread_attr_t attr;
  pthread_t stale_worker;
  bool stale_worker_started;
  bool worker_started = false;
  uint64_t now;
  int ret;

  ret = voice_channel_init();
  if (ret < 0) {
    return ret;
  }

  load_runtime_config();

  pthread_mutex_lock(&s_voice.lock);
  if (s_voice.running) {
    now = voice_now_ms();
    s_voice.generation++;
    s_voice.awaiting_reply = false;
    s_voice.capture_suspended_for_reply = false;
    s_voice.speaking = false;
    s_voice.recognizing = false;
    s_voice.direct_dialog = false;
    s_voice.reset_vad = true;
    voice_enter_wake_listen_locked(now);
    pthread_mutex_unlock(&s_voice.lock);
    pthread_mutex_lock(&s_voice.work_lock);
    voice_clear_work_queue_locked();
    pthread_mutex_unlock(&s_voice.work_lock);
    message_bus_clear_channel(VELACLAW_CHAN_VOICE);
    return 0;
  }
  stale_worker_started = s_voice.worker_started;
  stale_worker = s_voice.worker_thread;
  pthread_mutex_unlock(&s_voice.lock);

  if (stale_worker_started) {
    (void)pthread_join(stale_worker, NULL);
    pthread_mutex_lock(&s_voice.lock);
    s_voice.worker_started = false;
    pthread_mutex_unlock(&s_voice.lock);
  }

  cap = audio_capture_open(VELACLAW_AUDIO_CAPTURE_DEV,
                           VELACLAW_VOICE_SAMPLE_RATE,
                           VELACLAW_VOICE_CHANNELS,
                           VELACLAW_VOICE_BITS);
  if (cap == NULL) {
    syslog(LOG_ERR, "[%s] capture open failed\n", TAG);
    return -EIO;
  }

  ret = audio_capture_start(cap);
  if (ret < 0) {
    audio_capture_close(cap);
    return ret;
  }

  now = voice_now_ms();
  pthread_mutex_lock(&s_voice.lock);
  s_voice.cap = cap;
  s_voice.running = true;
  s_voice.stop_requested = false;
  s_voice.speaking = false;
  s_voice.awaiting_reply = false;
  s_voice.capture_suspended_for_reply = false;
  s_voice.recognizing = false;
  s_voice.direct_dialog = false;
  s_voice.reset_vad = true;
  s_voice.generation++;
  s_voice.suppress_until_ms = now + 300;
  voice_enter_wake_listen_locked(now);
  pthread_mutex_unlock(&s_voice.lock);

  pthread_mutex_lock(&s_voice.work_lock);
  s_voice.worker_stop = false;
  voice_clear_work_queue_locked();
  pthread_mutex_unlock(&s_voice.work_lock);
  message_bus_clear_channel(VELACLAW_CHAN_VOICE);

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, ASR_THREAD_STACK);
  ret = pthread_create(&s_voice.worker_thread, &attr,
                       voice_asr_worker, NULL);
  pthread_attr_destroy(&attr);
  if (ret != 0) {
    struct mallinfo mi = mallinfo();

    syslog(LOG_ERR,
           "[%s] ASR worker create failed: %d stack=%u free=%d\n",
           TAG, ret, (unsigned int)ASR_THREAD_STACK, mi.fordblks);
    pthread_mutex_lock(&s_voice.work_lock);
    s_voice.worker_stop = true;
    pthread_cond_broadcast(&s_voice.work_cond);
    pthread_mutex_unlock(&s_voice.work_lock);
    audio_capture_stop(cap);
    audio_capture_close(cap);
    pthread_mutex_lock(&s_voice.lock);
    s_voice.cap = NULL;
    s_voice.running = false;
    s_voice.recognizing = false;
    s_voice.direct_dialog = false;
    s_voice.mode = VOICE_MODE_OFF;
    pthread_mutex_unlock(&s_voice.lock);
    return -ret;
  }
  worker_started = true;
  pthread_mutex_lock(&s_voice.lock);
  s_voice.worker_started = true;
  pthread_mutex_unlock(&s_voice.lock);

  voice_button_attach();

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, VELACLAW_VOICE_STACK);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  ret = pthread_create(&s_voice.thread, &attr, voice_listen_thread, cap);
  pthread_attr_destroy(&attr);
  if (ret != 0) {
    struct mallinfo mi = mallinfo();

    syslog(LOG_ERR,
           "[%s] listener create failed: %d stack=%u free=%d\n",
           TAG, ret, (unsigned int)VELACLAW_VOICE_STACK, mi.fordblks);
    pthread_mutex_lock(&s_voice.work_lock);
    s_voice.worker_stop = true;
    pthread_cond_broadcast(&s_voice.work_cond);
    pthread_mutex_unlock(&s_voice.work_lock);
    if (worker_started) {
      (void)pthread_join(s_voice.worker_thread, NULL);
    }
    pthread_mutex_lock(&s_voice.lock);
    s_voice.cap = NULL;
    s_voice.running = false;
    s_voice.worker_started = false;
    s_voice.recognizing = false;
    s_voice.direct_dialog = false;
    s_voice.mode = VOICE_MODE_OFF;
    pthread_mutex_unlock(&s_voice.lock);
    voice_button_detach();
    audio_capture_stop(cap);
    audio_capture_close(cap);
    return -ret;
  }

  syslog(LOG_INFO, "[%s] wake listening started\n", TAG);
  printf("Voice: wake listening. Say 你好，哦盆喂啦 or 哈喽，哦盆喂啦.\n");
  return 0;
}

int voice_channel_stop(void)
{
  audio_capture_t *cap;
  pthread_t worker;
  bool worker_started;

  pthread_mutex_lock(&s_voice.lock);
  if (!s_voice.running) {
    worker_started = s_voice.worker_started;
    worker = s_voice.worker_thread;
    s_voice.recognizing = false;
    s_voice.direct_dialog = false;
    pthread_mutex_unlock(&s_voice.lock);
    if (worker_started) {
      (void)pthread_join(worker, NULL);
      pthread_mutex_lock(&s_voice.lock);
      s_voice.worker_started = false;
      pthread_mutex_unlock(&s_voice.lock);
    }
    return -EINVAL;
  }

  s_voice.stop_requested = true;
  s_voice.mode = VOICE_MODE_OFF;
  s_voice.recognizing = false;
  s_voice.direct_dialog = false;
  s_voice.awaiting_reply = false;
  s_voice.capture_suspended_for_reply = false;
  s_voice.speaking = false;
  s_voice.generation++;
  worker_started = s_voice.worker_started;
  worker = s_voice.worker_thread;
  cap = s_voice.cap;
  pthread_mutex_unlock(&s_voice.lock);

  voice_button_detach();

  pthread_mutex_lock(&s_voice.work_lock);
  s_voice.worker_stop = true;
  voice_clear_work_queue_locked();
  pthread_cond_broadcast(&s_voice.work_cond);
  pthread_mutex_unlock(&s_voice.work_lock);
  message_bus_clear_channel(VELACLAW_CHAN_VOICE);

  if (cap != NULL) {
    pthread_mutex_lock(&s_voice.capture_lock);
    pthread_mutex_lock(&s_voice.lock);
    cap = s_voice.cap;
    pthread_mutex_unlock(&s_voice.lock);
    if (cap != NULL) {
      audio_capture_stop(cap);
    }
    pthread_mutex_unlock(&s_voice.capture_lock);
  }

  pthread_mutex_lock(&s_voice.lock);
  while (s_voice.running) {
    pthread_cond_wait(&s_voice.stopped_cond, &s_voice.lock);
  }
  pthread_mutex_unlock(&s_voice.lock);

  if (worker_started) {
    (void)pthread_join(worker, NULL);
    pthread_mutex_lock(&s_voice.lock);
    s_voice.worker_started = false;
    pthread_mutex_unlock(&s_voice.lock);
  }

  printf("Voice: stopped.\n");
  return 0;
}

typedef struct
{
  audio_playback_t *pb;
  int write_error;
  int cache_fd;
  int cache_error;
  uint64_t start_ms;
  size_t chunks;
  size_t total_pcm;
  size_t cache_pcm;
  unsigned int sample_rate;
  unsigned int channels;
  unsigned int bits_per_sample;
} tts_playback_ctx_t;

static void tts_stream_cb(const unsigned char *pcm_data,
                          size_t pcm_len,
                          int is_last,
                          void *user_data)
{
  tts_playback_ctx_t *ctx = (tts_playback_ctx_t *)user_data;
  uint64_t now_ms;
  int ret;

  if (ctx == NULL) {
    return;
  }

  now_ms = voice_now_ms();
  if (is_last) {
    syslog(LOG_INFO,
           "[%s] TTS PCM callback done chunks=%zu total=%zu audio=%llums "
           "elapsed=%llums\n",
           TAG, ctx->chunks, ctx->total_pcm,
           (unsigned long long)(
             ctx->sample_rate == 0 || ctx->channels == 0 ||
             ctx->bits_per_sample == 0 ? 0 :
             ctx->total_pcm * 1000 /
             ((uint64_t)ctx->sample_rate * ctx->channels *
              (ctx->bits_per_sample / 8))),
           (unsigned long long)(now_ms - ctx->start_ms));
    return;
  }

  if (pcm_data != NULL && pcm_len > 0 && ctx->pb != NULL) {
    ret = audio_playback_write(ctx->pb, pcm_data, pcm_len);
    if (ctx->cache_fd >= 0 && ctx->cache_error == 0) {
      size_t off = 0;

      while (off < pcm_len) {
        ssize_t n = write(ctx->cache_fd, pcm_data + off, pcm_len - off);

        if (n < 0) {
          if (errno == EINTR) {
            continue;
          }
          ctx->cache_error = errno == 0 ? EIO : errno;
          break;
        }
        if (n == 0) {
          ctx->cache_error = EIO;
          break;
        }
        off += (size_t)n;
      }
      ctx->cache_pcm += off;
    }
    ctx->chunks++;
    ctx->total_pcm += pcm_len;
    if (ret < 0 && ctx->write_error == 0) {
      ctx->write_error = ret;
      syslog(LOG_ERR, "[%s] PCM write failed: %d\n", TAG, ret);
    }
  }
}

int voice_channel_speak(const char *text)
{
  audio_playback_t *pb;
  audio_capture_t *cap;
  uint64_t now;
  uint64_t speak_start_ms;
  uint64_t reply_wait_ms = 0;
  unsigned int sample_rate = VELACLAW_VOICE_SAMPLE_RATE;
  const char *backend;
  tts_playback_ctx_t tts_ctx;
  char cache_path[160];
  bool have_cache_path = false;
  bool active;
  bool capture_paused = false;
  bool resumed_dialog;
  int ret;

  if (text == NULL || text[0] == '\0') {
    return -EINVAL;
  }

  pthread_mutex_lock(&s_voice.lock);
  active = s_voice.running && !s_voice.stop_requested;
  if (active) {
    now = voice_now_ms();
    if (s_voice.awaiting_reply && s_voice.reply_started_ms > 0) {
      reply_wait_ms = now - s_voice.reply_started_ms;
    }
    s_voice.speaking = true;
    s_voice.awaiting_reply = false;
  }
  pthread_mutex_unlock(&s_voice.lock);

  if (!active) {
    syslog(LOG_WARNING, "[%s] skip TTS while voice is stopped\n", TAG);
    return -ESHUTDOWN;
  }

  pthread_mutex_lock(&s_voice.capture_lock);
  pthread_mutex_lock(&s_voice.lock);
  cap = s_voice.cap;
  capture_paused = s_voice.capture_suspended_for_reply;
  if (cap != NULL) {
    s_voice.cap = NULL;
    s_voice.reset_vad = true;
  }
  pthread_mutex_unlock(&s_voice.lock);
  if (cap != NULL && audio_capture_stop(cap) == 0) {
    struct mallinfo before = mallinfo();

    audio_capture_close(cap);
    cap = NULL;
    capture_paused = true;
    syslog(LOG_INFO,
           "[%s] capture released for TTS: free=%d -> %d\n",
           TAG, before.fordblks, mallinfo().fordblks);
  } else if (cap != NULL) {
    pthread_mutex_lock(&s_voice.lock);
    if (s_voice.cap == NULL && s_voice.running && !s_voice.stop_requested) {
      s_voice.cap = cap;
    }
    pthread_mutex_unlock(&s_voice.lock);
  }
  pthread_mutex_unlock(&s_voice.capture_lock);

  backend = voice_tts_get_backend();
  if (backend != NULL && strcmp(backend, "mimo") == 0) {
    sample_rate = VELACLAW_MIMO_TTS_SAMPLE_RATE;
  }

  speak_start_ms = voice_now_ms();

#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
  ui_demo_ai_set_reply(text);
#endif

  syslog(LOG_INFO,
         "[%s] speak: reply_wait=%llums bytes=%zu text=\"%.*s\" (%uHz)\n",
         TAG, (unsigned long long)reply_wait_ms, strlen(text),
         60, text, sample_rate);

  have_cache_path = voice_tts_cache_path(text, sample_rate,
                                         cache_path, sizeof(cache_path));
  if (have_cache_path) {
    ret = voice_play_cached_pcm(cache_path, sample_rate);
    if (ret == 0) {
      goto out;
    }
  }

  pb = audio_playback_open(VELACLAW_AUDIO_PLAYBACK_DEV,
                           sample_rate,
                           VELACLAW_VOICE_CHANNELS,
                           VELACLAW_VOICE_BITS);
  if (pb == NULL) {
    ret = -EIO;
    goto out;
  }

  memset(&tts_ctx, 0, sizeof(tts_ctx));
  tts_ctx.cache_fd = -1;
  tts_ctx.pb = pb;
  tts_ctx.start_ms = speak_start_ms;
  tts_ctx.sample_rate = sample_rate;
  tts_ctx.channels = VELACLAW_VOICE_CHANNELS;
  tts_ctx.bits_per_sample = VELACLAW_VOICE_BITS;
  if (have_cache_path) {
    voice_tts_cache_prepare();
    tts_ctx.cache_fd = open(cache_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (tts_ctx.cache_fd < 0) {
      syslog(LOG_WARNING, "[%s] TTS cache open failed: %d path=%s\n",
             TAG, errno, cache_path);
    }
  }
  ret = voice_tts_speak_stream(text, tts_stream_cb, &tts_ctx);
  if (ret == 0 && tts_ctx.write_error < 0) {
    ret = tts_ctx.write_error;
  }
  if (tts_ctx.cache_fd >= 0) {
    close(tts_ctx.cache_fd);
    tts_ctx.cache_fd = -1;
    if (ret == 0 && tts_ctx.cache_error == 0 && tts_ctx.cache_pcm > 0) {
      syslog(LOG_INFO, "[%s] TTS cache stored: %s (%zu bytes)\n",
             TAG, cache_path, tts_ctx.cache_pcm);
    } else {
      (void)unlink(cache_path);
      if (tts_ctx.cache_error != 0) {
        syslog(LOG_WARNING, "[%s] TTS cache write failed: %d\n",
               TAG, tts_ctx.cache_error);
      }
    }
  }
  if (ret == 0) {
    int pret = audio_playback_close(pb);
    if (ret == 0 && pret < 0) {
      ret = pret;
    }
  } else {
    (void)audio_playback_abort(pb);
  }

out:
  if (capture_paused) {
    int cret;

    pthread_mutex_lock(&s_voice.capture_lock);
    pthread_mutex_lock(&s_voice.lock);
    active = s_voice.running && !s_voice.stop_requested;
    pthread_mutex_unlock(&s_voice.lock);
    if (active) {
      cret = voice_reopen_capture_locked(&cap);
    } else {
      cret = -ESHUTDOWN;
    }
    if (cret == 0) {
      now = voice_now_ms();
      pthread_mutex_lock(&s_voice.lock);
      s_voice.reset_vad = true;
      s_voice.last_activity_ms = now;
      if (s_voice.direct_dialog) {
        s_voice.mode = VOICE_MODE_DIALOG;
      }
      pthread_mutex_unlock(&s_voice.lock);
      syslog(LOG_INFO, "[%s] capture hard-reopened after TTS\n", TAG);
    } else if (active) {
      syslog(LOG_ERR, "[%s] capture resume failed: %d\n", TAG, cret);
    }
    pthread_mutex_unlock(&s_voice.capture_lock);
  }

  now = voice_now_ms();
  pthread_mutex_lock(&s_voice.lock);
  s_voice.speaking = false;
  resumed_dialog = s_voice.mode == VOICE_MODE_DIALOG;
  if (s_voice.running && !s_voice.stop_requested) {
    if (resumed_dialog) {
      s_voice.mode = VOICE_MODE_DIALOG;
    } else {
      voice_enter_wake_listen_locked(now);
    }
    s_voice.last_activity_ms = now;
    s_voice.suppress_until_ms = now + VOICE_SUPPRESS_AFTER_TTS_MS;
  }
  pthread_mutex_unlock(&s_voice.lock);

  if (ret != 0) {
    syslog(LOG_ERR, "[%s] TTS failed: %d\n", TAG, ret);
    return ret;
  }

  syslog(LOG_INFO,
         "[%s] speak done in %llums; %s listening resumed\n",
         TAG, (unsigned long long)(now - speak_start_ms),
         resumed_dialog ? "dialog" : "wake");
  return 0;
}

static int read_pcm_file(const char *path,
                         unsigned char **out, size_t *out_len)
{
  int fd;
  off_t fsize;
  unsigned char *buf;
  ssize_t nread;

  if (path == NULL || out == NULL || out_len == NULL) {
    return -EINVAL;
  }

  fd = open(path, O_RDONLY);
  if (fd < 0) {
    return -errno;
  }

  fsize = lseek(fd, 0, SEEK_END);
  if (fsize <= 0 || (size_t)fsize > VOICE_TEST_MAX_PCM_BYTES) {
    close(fd);
    return -EFBIG;
  }
  lseek(fd, 0, SEEK_SET);

  buf = malloc((size_t)fsize);
  if (buf == NULL) {
    close(fd);
    return -ENOMEM;
  }

  nread = read(fd, buf, (size_t)fsize);
  close(fd);
  if (nread != (ssize_t)fsize) {
    free(buf);
    return -EIO;
  }

  *out = buf;
  *out_len = (size_t)fsize;
  return 0;
}

static int write_pcm_file(const char *path,
                          const unsigned char *data, size_t len)
{
  int fd;
  size_t off = 0;

  if (path == NULL || data == NULL) {
    return -EINVAL;
  }

  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    return -errno;
  }

  while (off < len) {
    ssize_t nw = write(fd, data + off, len - off);
    if (nw < 0) {
      if (errno == EINTR) {
        continue;
      }
      close(fd);
      return -errno;
    }
    if (nw == 0) {
      close(fd);
      return -EIO;
    }
    off += (size_t)nw;
  }

  close(fd);
  return 0;
}

int voice_channel_test_tts(const char *text, const char *out_path)
{
  unsigned char *pcm;
  size_t pcm_len = 0;
  int ret;

  if (text == NULL || out_path == NULL) {
    printf("Usage: voice_test_tts <text> [output_path]\n");
    return -EINVAL;
  }

  ret = voice_channel_init();
  if (ret < 0) {
    return ret;
  }

  pcm = malloc(VELACLAW_VOICE_PCM_BUF_SIZE);
  if (pcm == NULL) {
    return -ENOMEM;
  }

  printf("TTS: synthesizing \"%s\" ...\n", text);
  ret = voice_tts_speak(text, pcm, VELACLAW_VOICE_PCM_BUF_SIZE, &pcm_len);
  if (ret == 0) {
    ret = write_pcm_file(out_path, pcm, pcm_len);
  }

  if (ret == 0) {
    printf("TTS OK: %zu bytes -> %s\n", pcm_len, out_path);
  } else {
    printf("TTS failed: %d\n", ret);
  }

  free(pcm);
  return ret;
}

typedef struct
{
  char pcm_path[256];
} asr_task_args_t;

static void *asr_file_worker(void *arg)
{
  asr_task_args_t *a = (asr_task_args_t *)arg;
  unsigned char *pcm = NULL;
  size_t pcm_len = 0;
  char text[VOICE_TEXT_CAP];
  int ret;

  ret = read_pcm_file(a->pcm_path, &pcm, &pcm_len);
  if (ret != 0) {
    printf("Failed to read %s: %d\n", a->pcm_path, ret);
    free(a);
    return NULL;
  }

  printf("ASR: recognizing %zu bytes from %s ...\n", pcm_len, a->pcm_path);
  ret = voice_asr_recognize(pcm, pcm_len, text, sizeof(text));
  free(pcm);

  if (ret == 0) {
    printf("ASR result: %s\n", text);
    (void)push_voice_prompt(text);
  } else {
    printf("ASR failed: %d\n", ret);
  }

  free(a);
  return NULL;
}

int voice_channel_test_asr(const char *pcm_path)
{
  asr_task_args_t *args;
  int ret;

  if (pcm_path == NULL) {
    printf("Usage: voice_test_asr <pcm_file>\n");
    return -EINVAL;
  }

  ret = voice_channel_init();
  if (ret < 0) {
    return ret;
  }

  args = malloc(sizeof(*args));
  if (args == NULL) {
    return -ENOMEM;
  }

  snprintf(args->pcm_path, sizeof(args->pcm_path), "%s", pcm_path);
  ret = velaclaw_task_create(asr_file_worker, "asr_test",
                             ASR_THREAD_STACK, args,
                             VELACLAW_VOICE_PRIO);
  if (ret != OK) {
    free(args);
    return -EIO;
  }

  printf("ASR test started (background thread).\n");
  return 0;
}
