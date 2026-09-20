#include "velaclaw_service.h"

#include <errno.h>
#include <nuttx/config.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include "velaclaw_compat.h"

#ifndef CONFIG_EXAMPLES_VELACLAW_VELA_PRIORITY
#  define CONFIG_EXAMPLES_VELACLAW_VELA_PRIORITY 100
#endif

#ifndef CONFIG_EXAMPLES_VELACLAW_VELA_STACKSIZE
#  define CONFIG_EXAMPLES_VELACLAW_VELA_STACKSIZE 32768
#endif

extern int velaclaw_main(int argc, char *argv[]);

struct velaclaw_service_context
{
  pthread_mutex_t lock;
  velaclaw_service_event_cb_t cb;
  void *user_data;
  enum velaclaw_service_status status;
  bool task_running;
  char message[VELACLAW_SERVICE_MESSAGE_LEN];
};

static struct velaclaw_service_context g_service =
{
  .lock = PTHREAD_MUTEX_INITIALIZER,
  .status = VELACLAW_SERVICE_STATUS_STOPPED,
  .message = "AI 未启动",
};

static void velaclaw_service_copy_text(char *dst, size_t dst_len,
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

static void velaclaw_service_notify(enum velaclaw_service_event event)
{
  velaclaw_service_event_cb_t cb;
  void *user_data;

  pthread_mutex_lock(&g_service.lock);
  cb = g_service.cb;
  user_data = g_service.user_data;
  pthread_mutex_unlock(&g_service.lock);

  if (cb != NULL)
    {
      cb(event, user_data);
    }
}

static void velaclaw_service_set_state(enum velaclaw_service_status status,
                                       const char *message,
                                       enum velaclaw_service_event event)
{
  pthread_mutex_lock(&g_service.lock);
  g_service.status = status;
  if (status == VELACLAW_SERVICE_STATUS_STARTING ||
      status == VELACLAW_SERVICE_STATUS_READY ||
      status == VELACLAW_SERVICE_STATUS_STOPPING)
    {
      g_service.task_running = true;
    }
  else if (status == VELACLAW_SERVICE_STATUS_STOPPED)
    {
      g_service.task_running = false;
    }

  velaclaw_service_copy_text(g_service.message,
                             sizeof(g_service.message),
                             message == NULL ? "" : message);
  pthread_mutex_unlock(&g_service.lock);

  velaclaw_service_notify(event);
}

static int velaclaw_service_task_entry(int argc, char *argv[])
{
  int ret;

  ret = velaclaw_main(0, NULL);

  pthread_mutex_lock(&g_service.lock);
  g_service.task_running = false;
  if (g_service.status == VELACLAW_SERVICE_STATUS_STOPPING ||
      velaclaw_shutdown_requested())
    {
      g_service.status = VELACLAW_SERVICE_STATUS_STOPPED;
      velaclaw_service_copy_text(g_service.message,
                                 sizeof(g_service.message),
                                 "AI 已退出");
      pthread_mutex_unlock(&g_service.lock);
      velaclaw_service_notify(VELACLAW_SERVICE_EVENT_STOPPED);
    }
  else if (ret != 0)
    {
      g_service.status = VELACLAW_SERVICE_STATUS_ERROR;
      snprintf(g_service.message, sizeof(g_service.message),
               "AI 启动失败，错误码 %d", ret);
      pthread_mutex_unlock(&g_service.lock);
      velaclaw_service_notify(VELACLAW_SERVICE_EVENT_ERROR);
    }
  else
    {
      g_service.status = VELACLAW_SERVICE_STATUS_STOPPED;
      velaclaw_service_copy_text(g_service.message,
                                 sizeof(g_service.message),
                                 "AI 已退出");
      pthread_mutex_unlock(&g_service.lock);
      velaclaw_service_notify(VELACLAW_SERVICE_EVENT_STOPPED);
    }

  return ret;
}

void velaclaw_service_set_event_cb(velaclaw_service_event_cb_t cb,
                                   void *user_data)
{
  pthread_mutex_lock(&g_service.lock);
  g_service.cb = cb;
  g_service.user_data = user_data;
  pthread_mutex_unlock(&g_service.lock);
}

void velaclaw_service_get_snapshot(
  struct velaclaw_service_snapshot *snapshot)
{
  if (snapshot == NULL)
    {
      return;
    }

  pthread_mutex_lock(&g_service.lock);
  snapshot->status = g_service.status;
  snapshot->task_running = g_service.task_running;
  velaclaw_service_copy_text(snapshot->message,
                             sizeof(snapshot->message),
                             g_service.message);
  pthread_mutex_unlock(&g_service.lock);
}

int velaclaw_service_start(void)
{
  char *argv[] =
  {
    "velaclaw",
    NULL
  };
  int pid;

  pthread_mutex_lock(&g_service.lock);
  if (g_service.task_running ||
      g_service.status == VELACLAW_SERVICE_STATUS_STARTING ||
      g_service.status == VELACLAW_SERVICE_STATUS_READY ||
      g_service.status == VELACLAW_SERVICE_STATUS_STOPPING)
    {
      pthread_mutex_unlock(&g_service.lock);
      return OK;
    }

  g_service.status = VELACLAW_SERVICE_STATUS_STARTING;
  g_service.task_running = true;
  velaclaw_service_copy_text(g_service.message,
                             sizeof(g_service.message),
                             "AI 正在启动...");
  pthread_mutex_unlock(&g_service.lock);

  pid = task_create("velaclaw_ui",
                    CONFIG_EXAMPLES_VELACLAW_VELA_PRIORITY,
                    CONFIG_EXAMPLES_VELACLAW_VELA_STACKSIZE,
                    velaclaw_service_task_entry,
                    argv);
  if (pid < 0)
    {
      int err = errno == 0 ? EIO : errno;

      pthread_mutex_lock(&g_service.lock);
      g_service.status = VELACLAW_SERVICE_STATUS_ERROR;
      g_service.task_running = false;
      snprintf(g_service.message, sizeof(g_service.message),
               "AI 启动失败：创建任务失败 %d", -err);
      pthread_mutex_unlock(&g_service.lock);
      velaclaw_service_notify(VELACLAW_SERVICE_EVENT_ERROR);
      return -err;
    }

  velaclaw_service_notify(VELACLAW_SERVICE_EVENT_STATE_CHANGED);
  return OK;
}

int velaclaw_service_stop(void)
{
  pthread_mutex_lock(&g_service.lock);
  if (!g_service.task_running &&
      g_service.status == VELACLAW_SERVICE_STATUS_STOPPED)
    {
      pthread_mutex_unlock(&g_service.lock);
      return OK;
    }

  g_service.status = VELACLAW_SERVICE_STATUS_STOPPING;
  velaclaw_service_copy_text(g_service.message,
                             sizeof(g_service.message),
                             "AI 正在退出...");
  pthread_mutex_unlock(&g_service.lock);

  velaclaw_request_shutdown();
  velaclaw_service_notify(VELACLAW_SERVICE_EVENT_STATE_CHANGED);
  return OK;
}

void velaclaw_service_report_starting(const char *message)
{
  velaclaw_service_set_state(
    VELACLAW_SERVICE_STATUS_STARTING,
    message == NULL ? "AI 正在初始化..." : message,
    VELACLAW_SERVICE_EVENT_STATE_CHANGED);
}

void velaclaw_service_report_ready(const char *message)
{
  velaclaw_service_set_state(
    VELACLAW_SERVICE_STATUS_READY,
    message == NULL ? "AI 连接成功" : message,
    VELACLAW_SERVICE_EVENT_READY);
}

void velaclaw_service_report_error(const char *message)
{
  velaclaw_service_set_state(
    VELACLAW_SERVICE_STATUS_ERROR,
    message == NULL ? "AI 连接失败" : message,
    VELACLAW_SERVICE_EVENT_ERROR);
}

void velaclaw_service_report_stopped(const char *message)
{
  velaclaw_service_set_state(
    VELACLAW_SERVICE_STATUS_STOPPED,
    message == NULL ? "AI 已退出" : message,
    VELACLAW_SERVICE_EVENT_STOPPED);
}
