#include "config/lvgl_app_config.h"
#include "ui_demo/inc/ui_demo_ai.h"

#include <errno.h>
#include <nuttx/config.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <syslog.h>

#include <lvgl/lvgl.h>

#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
#  include "bus/message_bus.h"
#  include "bus/message_bus_tap.h"
#  include "velaclaw_config.h"
#  include "velaclaw_service.h"
#  include "voice/voice_channel.h"
#endif

#define UI_DEMO_AI_LOG_TAG "ui_demo_ai"
#define UI_DEMO_AI_CHANNEL "ui"
#define UI_DEMO_AI_CHAT_ID "ui_demo"

struct ui_demo_ai_context
{
  ui_demo_ai_event_cb_t cb;
  void *user_data;
  enum ui_demo_ai_status status;
  bool enabled;
  bool start_in_progress;
  bool stop_in_progress;
  bool response_waiting;
  bool voice_enabled;
  bool tap_registered;
  char message[UI_DEMO_AI_MESSAGE_LEN];
  char voice_message[UI_DEMO_AI_MESSAGE_LEN];
  char last_title[48];
  char last_user[UI_DEMO_AI_MESSAGE_LEN];
  char last_reply[UI_DEMO_AI_REPLY_LEN];
};

static pthread_mutex_t g_ai_lock = PTHREAD_MUTEX_INITIALIZER;
static struct ui_demo_ai_context g_ai;

static void ui_demo_ai_copy_text(char *dst, size_t dst_len,
                                 const char *src)
{
  if (dst_len == 0)
    {
      return;
    }

  if (src == NULL)
    {
      dst[0] = '\0';
      return;
    }

  snprintf(dst, dst_len, "%s", src);
}

static void ui_demo_ai_async_cb(void *user_data)
{
  enum ui_demo_ai_event event =
    (enum ui_demo_ai_event)(uintptr_t)user_data;
  ui_demo_ai_event_cb_t cb;
  void *cb_user_data;

  pthread_mutex_lock(&g_ai_lock);
  cb = g_ai.cb;
  cb_user_data = g_ai.user_data;
  pthread_mutex_unlock(&g_ai_lock);

  if (cb != NULL)
    {
      cb(event, cb_user_data);
    }
}

static void ui_demo_ai_notify(enum ui_demo_ai_event event)
{
  if (lv_async_call(ui_demo_ai_async_cb,
                    (void *)(uintptr_t)event) != LV_RESULT_OK)
    {
      syslog(LOG_WARNING, "%s: async notify failed event=%d\n",
             UI_DEMO_AI_LOG_TAG, event);
    }
}

static void ui_demo_ai_set_turn_locked(const char *title,
                                       const char *user_text,
                                       const char *reply_text,
                                       bool response_waiting)
{
  ui_demo_ai_copy_text(g_ai.last_title, sizeof(g_ai.last_title),
                       title == NULL ? "AI" : title);
  ui_demo_ai_copy_text(g_ai.last_user, sizeof(g_ai.last_user),
                       user_text);
  ui_demo_ai_copy_text(g_ai.last_reply, sizeof(g_ai.last_reply),
                       reply_text);
  g_ai.response_waiting = response_waiting;
}

#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
static void ui_demo_ai_tap_cb(const velaclaw_msg_t *msg, void *cookie)
{
  (void)cookie;

  if (msg == NULL || msg->content == NULL)
    {
      return;
    }

  pthread_mutex_lock(&g_ai_lock);
  g_ai.response_waiting = false;
  ui_demo_ai_copy_text(g_ai.last_reply, sizeof(g_ai.last_reply),
                       msg->content);
  pthread_mutex_unlock(&g_ai_lock);

  ui_demo_ai_notify(UI_DEMO_AI_EVENT_RESPONSE_RECEIVED);
}

static void ui_demo_ai_apply_service_snapshot_locked(
  const struct velaclaw_service_snapshot *snapshot)
{
  if (snapshot == NULL)
    {
      return;
    }

  switch (snapshot->status)
    {
      case VELACLAW_SERVICE_STATUS_STARTING:
        g_ai.status = UI_DEMO_AI_STATUS_STARTING;
        g_ai.enabled = true;
        g_ai.start_in_progress = true;
        g_ai.stop_in_progress = false;
        ui_demo_ai_copy_text(g_ai.message, sizeof(g_ai.message),
                             snapshot->message[0] == '\0' ?
                             "AI 正在连接..." : snapshot->message);
        break;

      case VELACLAW_SERVICE_STATUS_READY:
        g_ai.status = UI_DEMO_AI_STATUS_CONNECTED;
        g_ai.enabled = true;
        g_ai.start_in_progress = false;
        g_ai.stop_in_progress = false;
        ui_demo_ai_copy_text(g_ai.message, sizeof(g_ai.message),
                             snapshot->message[0] == '\0' ?
                             "AI 连接成功" : snapshot->message);
        break;

      case VELACLAW_SERVICE_STATUS_STOPPING:
        g_ai.status = UI_DEMO_AI_STATUS_STOPPING;
        g_ai.enabled = true;
        g_ai.start_in_progress = false;
        g_ai.stop_in_progress = true;
        ui_demo_ai_copy_text(g_ai.message, sizeof(g_ai.message),
                             snapshot->message[0] == '\0' ?
                             "AI 正在退出..." : snapshot->message);
        break;

      case VELACLAW_SERVICE_STATUS_ERROR:
        g_ai.status = UI_DEMO_AI_STATUS_ERROR;
        g_ai.enabled = false;
        g_ai.start_in_progress = false;
        g_ai.stop_in_progress = false;
        ui_demo_ai_copy_text(g_ai.message, sizeof(g_ai.message),
                             snapshot->message[0] == '\0' ?
                             "AI 连接失败" : snapshot->message);
        break;

      case VELACLAW_SERVICE_STATUS_STOPPED:
      default:
        g_ai.status = UI_DEMO_AI_STATUS_OFF;
        g_ai.enabled = false;
        g_ai.start_in_progress = false;
        g_ai.stop_in_progress = false;
        ui_demo_ai_copy_text(g_ai.message, sizeof(g_ai.message),
                             snapshot->message[0] == '\0' ?
                             "AI 未启动" : snapshot->message);
        break;
    }
}

static void ui_demo_ai_service_event(enum velaclaw_service_event event,
                                     void *user_data)
{
  struct velaclaw_service_snapshot snapshot;
  enum ui_demo_ai_event ui_event = UI_DEMO_AI_EVENT_STATE_CHANGED;
  bool start_voice = false;
  bool stop_voice = false;

  (void)user_data;

  velaclaw_service_get_snapshot(&snapshot);

  pthread_mutex_lock(&g_ai_lock);
  ui_demo_ai_apply_service_snapshot_locked(&snapshot);
  if (event == VELACLAW_SERVICE_EVENT_READY && !g_ai.voice_enabled)
    {
      start_voice = true;
    }
  else if ((event == VELACLAW_SERVICE_EVENT_STOPPED ||
            event == VELACLAW_SERVICE_EVENT_ERROR) &&
           g_ai.voice_enabled)
    {
      stop_voice = true;
    }
  pthread_mutex_unlock(&g_ai_lock);

  if (start_voice)
    {
      ui_demo_ai_set_voice_enabled(true);
    }
  else if (stop_voice)
    {
      ui_demo_ai_set_voice_enabled(false);
    }

  if (event == VELACLAW_SERVICE_EVENT_READY ||
      event == VELACLAW_SERVICE_EVENT_ERROR)
    {
      ui_event = UI_DEMO_AI_EVENT_START_FINISHED;
    }
  else if (event == VELACLAW_SERVICE_EVENT_STOPPED)
    {
      ui_event = UI_DEMO_AI_EVENT_STOP_FINISHED;
    }

  ui_demo_ai_notify(ui_event);
}
#endif

void ui_demo_ai_init(ui_demo_ai_event_cb_t cb, void *user_data)
{
  pthread_mutex_lock(&g_ai_lock);
  memset(&g_ai, 0, sizeof(g_ai));
  g_ai.cb = cb;
  g_ai.user_data = user_data;
  g_ai.status = UI_DEMO_AI_STATUS_OFF;
  ui_demo_ai_copy_text(g_ai.message, sizeof(g_ai.message), "AI 未启动");
  ui_demo_ai_copy_text(g_ai.voice_message, sizeof(g_ai.voice_message),
                       "语音 AI 未开启");
  ui_demo_ai_copy_text(g_ai.last_title, sizeof(g_ai.last_title),
                       "智能问答");
  ui_demo_ai_copy_text(g_ai.last_user, sizeof(g_ai.last_user),
                       "给我讲个笑话");
  ui_demo_ai_copy_text(g_ai.last_reply, sizeof(g_ai.last_reply),
                       "程序员去面试，面试官问：你有什么优点？\n"
                       "程序员说：我特别擅长把复杂问题变简单。\n"
                       "面试官：举个例子？\n"
                       "程序员：这个问题很简单。");
  pthread_mutex_unlock(&g_ai_lock);

#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
  velaclaw_service_set_event_cb(ui_demo_ai_service_event, NULL);
  if (mbus_tap_register(UI_DEMO_AI_CHANNEL, ui_demo_ai_tap_cb, NULL) == OK)
    {
      pthread_mutex_lock(&g_ai_lock);
      g_ai.tap_registered = true;
      pthread_mutex_unlock(&g_ai_lock);
    }
#endif
}

void ui_demo_ai_deinit(void)
{
#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
  velaclaw_service_set_event_cb(NULL, NULL);
  pthread_mutex_lock(&g_ai_lock);
  if (g_ai.tap_registered)
    {
      pthread_mutex_unlock(&g_ai_lock);
      mbus_tap_unregister(UI_DEMO_AI_CHANNEL);
      pthread_mutex_lock(&g_ai_lock);
      g_ai.tap_registered = false;
    }
  pthread_mutex_unlock(&g_ai_lock);
#else
  pthread_mutex_lock(&g_ai_lock);
  g_ai.cb = NULL;
  g_ai.user_data = NULL;
  pthread_mutex_unlock(&g_ai_lock);
  return;
#endif

  pthread_mutex_lock(&g_ai_lock);
  g_ai.cb = NULL;
  g_ai.user_data = NULL;
  pthread_mutex_unlock(&g_ai_lock);
}

void ui_demo_ai_get_snapshot(struct ui_demo_ai_snapshot *snapshot)
{
  if (snapshot == NULL)
    {
      return;
    }

  pthread_mutex_lock(&g_ai_lock);
  snapshot->status = g_ai.status;
  snapshot->enabled = g_ai.enabled;
  snapshot->start_in_progress = g_ai.start_in_progress;
  snapshot->stop_in_progress = g_ai.stop_in_progress;
  snapshot->response_waiting = g_ai.response_waiting;
  snapshot->voice_enabled = g_ai.voice_enabled;
  ui_demo_ai_copy_text(snapshot->message, sizeof(snapshot->message),
                       g_ai.message);
  ui_demo_ai_copy_text(snapshot->voice_message,
                       sizeof(snapshot->voice_message),
                       g_ai.voice_message);
  ui_demo_ai_copy_text(snapshot->last_title, sizeof(snapshot->last_title),
                       g_ai.last_title);
  ui_demo_ai_copy_text(snapshot->last_user, sizeof(snapshot->last_user),
                       g_ai.last_user);
  ui_demo_ai_copy_text(snapshot->last_reply, sizeof(snapshot->last_reply),
                       g_ai.last_reply);
  pthread_mutex_unlock(&g_ai_lock);
}

int ui_demo_ai_start(void)
{
#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
  struct velaclaw_service_snapshot service_snapshot;
#endif
  int ret = OK;

#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
  velaclaw_service_get_snapshot(&service_snapshot);
  if (service_snapshot.status == VELACLAW_SERVICE_STATUS_READY)
    {
      pthread_mutex_lock(&g_ai_lock);
      ui_demo_ai_apply_service_snapshot_locked(&service_snapshot);
      pthread_mutex_unlock(&g_ai_lock);
      ui_demo_ai_set_voice_enabled(true);
      ui_demo_ai_notify(UI_DEMO_AI_EVENT_START_FINISHED);
      return OK;
    }
#endif

  pthread_mutex_lock(&g_ai_lock);
  if (g_ai.status == UI_DEMO_AI_STATUS_STARTING ||
      g_ai.start_in_progress)
    {
      ui_demo_ai_copy_text(g_ai.message, sizeof(g_ai.message),
                           "AI 正在启动，请稍候");
      pthread_mutex_unlock(&g_ai_lock);
      ui_demo_ai_notify(UI_DEMO_AI_EVENT_STATE_CHANGED);
      return -EINPROGRESS;
    }

  g_ai.status = UI_DEMO_AI_STATUS_STARTING;
  g_ai.enabled = true;
  g_ai.start_in_progress = true;
  g_ai.stop_in_progress = false;
  ui_demo_ai_copy_text(g_ai.message, sizeof(g_ai.message),
                       "AI 正在连接...");
  pthread_mutex_unlock(&g_ai_lock);

#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
  ret = velaclaw_service_start();
#else
  ret = -ENOSYS;
#endif

  if (ret < 0)
    {
      pthread_mutex_lock(&g_ai_lock);
      g_ai.status = UI_DEMO_AI_STATUS_ERROR;
      g_ai.enabled = false;
      g_ai.start_in_progress = false;
      g_ai.stop_in_progress = false;
#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
      velaclaw_service_get_snapshot(&service_snapshot);
      ui_demo_ai_copy_text(g_ai.message, sizeof(g_ai.message),
                           service_snapshot.message[0] == '\0' ?
                           "AI 启动失败：任务创建失败" :
                           service_snapshot.message);
#else
      ui_demo_ai_copy_text(g_ai.message, sizeof(g_ai.message),
                           "AI 启动失败：VelaClaw 未启用");
#endif
      pthread_mutex_unlock(&g_ai_lock);
      ui_demo_ai_notify(UI_DEMO_AI_EVENT_START_FINISHED);
      return ret;
    }

#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
  velaclaw_service_get_snapshot(&service_snapshot);
  pthread_mutex_lock(&g_ai_lock);
  ui_demo_ai_apply_service_snapshot_locked(&service_snapshot);
  pthread_mutex_unlock(&g_ai_lock);
#endif

  ui_demo_ai_notify(UI_DEMO_AI_EVENT_STATE_CHANGED);
  return OK;
}

int ui_demo_ai_send_prompt(const char *title, const char *prompt)
{
#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
  struct velaclaw_service_snapshot service_snapshot;
  velaclaw_msg_t msg;
  int ret;

  if (prompt == NULL || prompt[0] == '\0')
    {
      return -EINVAL;
    }

  velaclaw_service_get_snapshot(&service_snapshot);
  if (service_snapshot.status != VELACLAW_SERVICE_STATUS_READY)
    {
      (void)ui_demo_ai_start();
      pthread_mutex_lock(&g_ai_lock);
      ui_demo_ai_copy_text(g_ai.last_title, sizeof(g_ai.last_title),
                           title == NULL ? "AI" : title);
      ui_demo_ai_copy_text(g_ai.last_user, sizeof(g_ai.last_user), prompt);
      ui_demo_ai_copy_text(g_ai.last_reply, sizeof(g_ai.last_reply),
                           "AI 正在启动，连接成功后请再点一次");
      pthread_mutex_unlock(&g_ai_lock);
      ui_demo_ai_notify(UI_DEMO_AI_EVENT_STATE_CHANGED);
      return -EINPROGRESS;
    }

  memset(&msg, 0, sizeof(msg));
  ui_demo_ai_copy_text(msg.channel, sizeof(msg.channel),
                       UI_DEMO_AI_CHANNEL);
  ui_demo_ai_copy_text(msg.chat_id, sizeof(msg.chat_id),
                       UI_DEMO_AI_CHAT_ID);
  msg.content = strdup(prompt);
  if (msg.content == NULL)
    {
      return -ENOMEM;
    }

  pthread_mutex_lock(&g_ai_lock);
  g_ai.response_waiting = true;
  ui_demo_ai_copy_text(g_ai.last_title, sizeof(g_ai.last_title),
                       title == NULL ? "AI" : title);
  ui_demo_ai_copy_text(g_ai.last_user, sizeof(g_ai.last_user), prompt);
  ui_demo_ai_copy_text(g_ai.last_reply, sizeof(g_ai.last_reply),
                       "已发送，等待 AI 回复...");
  pthread_mutex_unlock(&g_ai_lock);

  ret = message_bus_push_inbound(&msg);
  if (ret != OK)
    {
      free(msg.content);
      pthread_mutex_lock(&g_ai_lock);
      g_ai.response_waiting = false;
      ui_demo_ai_copy_text(g_ai.last_reply, sizeof(g_ai.last_reply),
                           "发送失败：Agent 消息队列不可用");
      pthread_mutex_unlock(&g_ai_lock);
      ui_demo_ai_notify(UI_DEMO_AI_EVENT_RESPONSE_RECEIVED);
      return -EIO;
    }

  ui_demo_ai_notify(UI_DEMO_AI_EVENT_STATE_CHANGED);
  return OK;
#else
  (void)title;
  (void)prompt;

  pthread_mutex_lock(&g_ai_lock);
  ui_demo_ai_copy_text(g_ai.last_reply, sizeof(g_ai.last_reply),
                       "发送失败：VelaClaw 未启用");
  pthread_mutex_unlock(&g_ai_lock);
  ui_demo_ai_notify(UI_DEMO_AI_EVENT_RESPONSE_RECEIVED);
  return -ENOSYS;
#endif
}

int ui_demo_ai_stop(void)
{
#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
  struct velaclaw_service_snapshot service_snapshot;
#endif
  int ret = OK;

  pthread_mutex_lock(&g_ai_lock);
  g_ai.status = UI_DEMO_AI_STATUS_STOPPING;
  g_ai.enabled = false;
  g_ai.start_in_progress = false;
  g_ai.stop_in_progress = true;
  ui_demo_ai_copy_text(g_ai.message, sizeof(g_ai.message),
                       "AI 正在退出...");
  pthread_mutex_unlock(&g_ai_lock);

#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
  ui_demo_ai_set_voice_enabled(false);
  ret = velaclaw_service_stop();
#endif

  if (ret < 0)
    {
      pthread_mutex_lock(&g_ai_lock);
      g_ai.status = UI_DEMO_AI_STATUS_ERROR;
      g_ai.stop_in_progress = false;
      ui_demo_ai_copy_text(g_ai.message, sizeof(g_ai.message),
                           "AI 退出失败，请查看日志");
      pthread_mutex_unlock(&g_ai_lock);
      ui_demo_ai_notify(UI_DEMO_AI_EVENT_STATE_CHANGED);
      return ret;
    }

#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
  velaclaw_service_get_snapshot(&service_snapshot);
  if (service_snapshot.status == VELACLAW_SERVICE_STATUS_STOPPED)
    {
      pthread_mutex_lock(&g_ai_lock);
      ui_demo_ai_apply_service_snapshot_locked(&service_snapshot);
      pthread_mutex_unlock(&g_ai_lock);
      ui_demo_ai_notify(UI_DEMO_AI_EVENT_STOP_FINISHED);
      return OK;
    }

  ui_demo_ai_notify(UI_DEMO_AI_EVENT_STATE_CHANGED);
#else
  pthread_mutex_lock(&g_ai_lock);
  g_ai.status = UI_DEMO_AI_STATUS_OFF;
  g_ai.stop_in_progress = false;
  ui_demo_ai_copy_text(g_ai.message, sizeof(g_ai.message), "AI 已退出");
  pthread_mutex_unlock(&g_ai_lock);
  ui_demo_ai_notify(UI_DEMO_AI_EVENT_STOP_FINISHED);
#endif

  return OK;
}

void ui_demo_ai_set_voice_enabled(bool enabled)
{
  int ret = 0;

#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
  /*
   * The switch controls the actual PTT capture session. Starting/stopping
   * outside g_ai_lock is important because audio teardown waits for a thread.
   */
  if (enabled)
    {
      ret = voice_channel_init();
      if (ret == 0)
        {
          ret = voice_channel_start();
        }
    }
  else
    {
      ret = voice_channel_stop();
      if (ret == -EINVAL)
        {
          ret = 0;
        }
    }
#else
  (void)enabled;
#endif

  pthread_mutex_lock(&g_ai_lock);
  g_ai.voice_enabled = ret == 0 && enabled;
  if (ret == 0)
    {
      ui_demo_ai_copy_text(g_ai.voice_message, sizeof(g_ai.voice_message),
                           enabled ?
                           "语音 AI 已开启，直接说话即可" :
                           "语音 AI 已关闭");
    }
  else
    {
      ui_demo_ai_copy_text(g_ai.voice_message, sizeof(g_ai.voice_message),
                           enabled ? "语音 AI 启动失败" : "语音 AI 停止失败");
    }
  pthread_mutex_unlock(&g_ai_lock);

  if (ret != 0)
    {
      syslog(LOG_ERR, "%s: voice %s failed: %d\n",
             UI_DEMO_AI_LOG_TAG, enabled ? "start" : "stop", ret);
    }

  ui_demo_ai_notify(UI_DEMO_AI_EVENT_VOICE_CHANGED);
}

void ui_demo_ai_set_turn(const char *title, const char *user_text,
                         const char *reply_text, bool response_waiting)
{
#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
  pthread_mutex_lock(&g_ai_lock);
  ui_demo_ai_set_turn_locked(title, user_text, reply_text,
                             response_waiting);
  pthread_mutex_unlock(&g_ai_lock);
  ui_demo_ai_notify(response_waiting ? UI_DEMO_AI_EVENT_STATE_CHANGED
                                     : UI_DEMO_AI_EVENT_RESPONSE_RECEIVED);
#else
  (void)title;
  (void)user_text;
  (void)reply_text;
  (void)response_waiting;
#endif
}

void ui_demo_ai_set_reply(const char *reply_text)
{
#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
  pthread_mutex_lock(&g_ai_lock);
  ui_demo_ai_copy_text(g_ai.last_reply, sizeof(g_ai.last_reply),
                       reply_text);
  g_ai.response_waiting = false;
  pthread_mutex_unlock(&g_ai_lock);
  ui_demo_ai_notify(UI_DEMO_AI_EVENT_RESPONSE_RECEIVED);
#else
  (void)reply_text;
#endif
}
