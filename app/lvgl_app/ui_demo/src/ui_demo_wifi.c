#include "config/lvgl_app_config.h"
#include "ui_demo/inc/ui_demo_wifi.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <syslog.h>
#include <unistd.h>

#include <lvgl/lvgl.h>

#ifndef CONFIG_LVGL_APP_UI_DEMO_WIFI_TASK_PRIORITY
#  define CONFIG_LVGL_APP_UI_DEMO_WIFI_TASK_PRIORITY 100
#endif

#ifndef CONFIG_LVGL_APP_UI_DEMO_WIFI_TASK_STACKSIZE
#  define CONFIG_LVGL_APP_UI_DEMO_WIFI_TASK_STACKSIZE 16384
#endif

#ifndef CONFIG_LVGL_APP_UI_DEMO_WIFI_SCAN_WAIT_MS
#  define CONFIG_LVGL_APP_UI_DEMO_WIFI_SCAN_WAIT_MS 5000
#endif

#ifndef CONFIG_LVGL_APP_UI_DEMO_WIFI_CONNECT_TIMEOUT_MS
#  define CONFIG_LVGL_APP_UI_DEMO_WIFI_CONNECT_TIMEOUT_MS 15000
#endif

#define UI_DEMO_WIFI_START_SETTLE_US 300000
#define UI_DEMO_WIFI_SCAN_FALLBACK_US 300000

#define UI_DEMO_WIFI_LOG_TAG "ui_demo_wifi"

#ifdef CONFIG_LVGL_APP_UI_DEMO_WIFI_BACKEND
#define UI_DEMO_WIFI_DRIVER_MAX_APS 32
#define UI_DEMO_WIFI_DRIVER_SSID_LEN 32

struct ui_demo_wifi_driver_mac_addr
{
  uint16_t array[3];
};

struct ui_demo_wifi_driver_ssid
{
  uint8_t length;
  uint8_t array[UI_DEMO_WIFI_DRIVER_SSID_LEN];
};

struct ui_demo_wifi_driver_chan_def
{
  uint16_t freq;
  uint8_t band;
  uint8_t flags;
  int8_t tx_power;
};

struct ui_demo_wifi_driver_scan_result
{
  bool valid_flag;
  struct ui_demo_wifi_driver_mac_addr bssid;
  struct ui_demo_wifi_driver_ssid ssid;
  uint16_t bsstype;
  struct ui_demo_wifi_driver_chan_def *chan;
  uint16_t beacon_period;
  uint16_t cap_info;
  uint32_t akm;
  uint16_t group_cipher;
  uint16_t pairwise_cipher;
  int8_t rssi;
  uint8_t mluti_bssid_index;
  uint8_t max_bssid_indicator;
};

extern int fhost_get_scan_results(void *link, int result_idx,
                                  int max_nb_result,
                                  struct ui_demo_wifi_driver_scan_result *result);
extern int wifistart_main(int argc, char *argv[]);
extern int wifiscan_main(int argc, char *argv[]);
extern int wificonnect_main(int argc, char *argv[]);
extern int wifidisconnect_main(int argc, char *argv[]);
extern int wifistop_main(int argc, char *argv[]);
#endif

struct ui_demo_wifi_context
{
  ui_demo_wifi_event_cb_t cb;
  void *user_data;
  bool enabled;
  bool scan_in_progress;
  bool connect_in_progress;
  bool stop_in_progress;
  bool scan_needs_start;
  enum ui_demo_wifi_status status;
  char message[UI_DEMO_WIFI_MESSAGE_LEN];
  char connected_ssid[UI_DEMO_WIFI_SSID_LEN];
  char pending_ssid[UI_DEMO_WIFI_SSID_LEN];
  char pending_password[UI_DEMO_WIFI_PASSWORD_LEN];
  uint32_t generation;
  uint32_t scan_generation;
  uint32_t connect_generation;
  uint32_t stop_generation;
  size_t ap_count;
  struct ui_demo_wifi_ap aps[UI_DEMO_WIFI_MAX_APS];
};

static pthread_mutex_t g_wifi_lock = PTHREAD_MUTEX_INITIALIZER;
static struct ui_demo_wifi_context g_wifi;

static void ui_demo_wifi_set_message_locked(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(g_wifi.message, sizeof(g_wifi.message), fmt, ap);
  va_end(ap);
}

static void ui_demo_wifi_copy_text(char *dst, size_t dst_len,
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

static void ui_demo_wifi_async_cb(void *user_data)
{
  enum ui_demo_wifi_event event =
    (enum ui_demo_wifi_event)(uintptr_t)user_data;
  ui_demo_wifi_event_cb_t cb;
  void *cb_user_data;

  pthread_mutex_lock(&g_wifi_lock);
  cb = g_wifi.cb;
  cb_user_data = g_wifi.user_data;
  pthread_mutex_unlock(&g_wifi_lock);

  if (cb != NULL)
    {
      cb(event, cb_user_data);
    }
}

static void ui_demo_wifi_notify(enum ui_demo_wifi_event event)
{
  if (lv_async_call(ui_demo_wifi_async_cb,
                    (void *)(uintptr_t)event) != LV_RESULT_OK)
    {
      syslog(LOG_WARNING, "%s: async notify failed event=%d\n",
             UI_DEMO_WIFI_LOG_TAG, event);
    }
}

#ifdef CONFIG_LVGL_APP_UI_DEMO_WIFI_BACKEND
static int32_t ui_demo_wifi_signal_from_rssi(int32_t rssi)
{
  if (rssi == 0)
    {
      return 0;
    }

  if (rssi >= -35)
    {
      return 100;
    }

  if (rssi <= -95)
    {
      return 5;
    }

  return 5 + ((rssi + 95) * 95) / 60;
}

static void ui_demo_wifi_store_or_update_ap(struct ui_demo_wifi_ap *aps,
                                            size_t *count,
                                            const struct ui_demo_wifi_ap *ap)
{
  size_t i;

  for (i = 0; i < *count; i++)
    {
      if (strcmp(aps[i].ssid, ap->ssid) == 0)
        {
          if (ap->rssi > aps[i].rssi)
            {
              aps[i] = *ap;
            }

          return;
        }
    }

  if (*count < UI_DEMO_WIFI_MAX_APS)
    {
      aps[*count] = *ap;
      (*count)++;
    }
}

static uint32_t ui_demo_wifi_channel_from_freq(uint32_t freq)
{
  if (freq >= 2412 && freq <= 2472)
    {
      return (freq - 2407) / 5;
    }

  if (freq == 2484)
    {
      return 14;
    }

  if (freq >= 5000 && freq <= 5900)
    {
      return (freq - 5000) / 5;
    }

  return freq;
}

static size_t ui_demo_wifi_read_scan_results(struct ui_demo_wifi_ap *aps,
                                             size_t max_count)
{
  size_t count = 0;
  int idx;

  if (aps == NULL || max_count == 0)
    {
      return 0;
    }

  for (idx = 0; idx < UI_DEMO_WIFI_DRIVER_MAX_APS; idx++)
    {
      struct ui_demo_wifi_driver_scan_result result;
      struct ui_demo_wifi_ap ap;
      size_t ssid_len;
      int ret;

      memset(&result, 0, sizeof(result));
      ret = fhost_get_scan_results(NULL, idx, 1, &result);
      if (ret <= 0)
        {
          break;
        }

      if (!result.valid_flag || result.ssid.length == 0)
        {
          continue;
        }

      memset(&ap, 0, sizeof(ap));
      ssid_len = result.ssid.length;
      if (ssid_len > UI_DEMO_WIFI_DRIVER_SSID_LEN)
        {
          ssid_len = UI_DEMO_WIFI_DRIVER_SSID_LEN;
        }

      if (ssid_len >= sizeof(ap.ssid))
        {
          ssid_len = sizeof(ap.ssid) - 1;
        }

      memcpy(ap.ssid, result.ssid.array, ssid_len);
      ap.ssid[ssid_len] = '\0';
      ap.rssi = (int32_t)result.rssi;
      ap.signal = ui_demo_wifi_signal_from_rssi(ap.rssi);
      ap.channel = result.chan == NULL ? 0 :
                   ui_demo_wifi_channel_from_freq(result.chan->freq);
      snprintf(ap.status, sizeof(ap.status),
               "CH %lu  RSSI %ld dBm",
               (unsigned long)ap.channel, (long)ap.rssi);

      ui_demo_wifi_store_or_update_ap(aps, &count, &ap);
      if (count >= max_count)
        {
          break;
        }
    }

  return count;
}
#endif

static bool ui_demo_wifi_password_is_hex64(const char *password)
{
  size_t i;

  if (password == NULL || strlen(password) != 64)
    {
      return false;
    }

  for (i = 0; i < 64; i++)
    {
      char ch = password[i];

      if (!((ch >= '0' && ch <= '9') ||
            (ch >= 'a' && ch <= 'f') ||
            (ch >= 'A' && ch <= 'F')))
        {
          return false;
        }
    }

  return true;
}

static bool ui_demo_wifi_password_is_valid(const char *password)
{
  size_t len;

  if (password == NULL)
    {
      return false;
    }

  len = strlen(password);
  if (len == 0)
    {
      return true;
    }

  if (len >= 8 && len <= 63)
    {
      return true;
    }

  return ui_demo_wifi_password_is_hex64(password);
}

static int ui_demo_wifi_scan_entry(int argc, char *argv[])
{
  struct ui_demo_wifi_ap aps[UI_DEMO_WIFI_MAX_APS];
#ifdef CONFIG_LVGL_APP_UI_DEMO_WIFI_BACKEND
  char scan_wait_arg[16];
  char *scan_argv[] =
  {
    "wifiscan",
    scan_wait_arg,
    NULL
  };
  char *scan_standalone_argv[] =
  {
    "wifiscan",
    "--standalone",
    scan_wait_arg,
    NULL
  };
#endif
  bool need_start;
  bool wifi_started;
  uint32_t generation;
  size_t count = 0;
  int ret = 0;

  (void)argc;
  (void)argv;

  pthread_mutex_lock(&g_wifi_lock);
  need_start = g_wifi.scan_needs_start;
  g_wifi.scan_needs_start = false;
  wifi_started = g_wifi.enabled;
  generation = g_wifi.scan_generation;
  pthread_mutex_unlock(&g_wifi_lock);

  memset(aps, 0, sizeof(aps));

  if (need_start)
    {
#ifdef CONFIG_LVGL_APP_UI_DEMO_WIFI_BACKEND
      char *start_argv[] =
      {
        "wifistart",
        NULL
      };

      ret = wifistart_main(1, start_argv);
#else
      ret = -ENOSYS;
#endif
      if (ret < 0)
        {
          pthread_mutex_lock(&g_wifi_lock);
          if (generation == g_wifi.generation)
            {
              g_wifi.scan_in_progress = false;
              g_wifi.enabled = false;
              g_wifi.status = UI_DEMO_WIFI_STATUS_ERROR;
              ui_demo_wifi_set_message_locked(
                ret == -ENOSYS ?
                "当前固件未启用 WiFi 支持" :
                "WiFi 启动失败，错误码 %d",
                ret);
            }
          pthread_mutex_unlock(&g_wifi_lock);
          ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_SCAN_FINISHED);
          return ret;
        }

      wifi_started = true;
      usleep(UI_DEMO_WIFI_START_SETTLE_US);
    }

#ifdef CONFIG_LVGL_APP_UI_DEMO_WIFI_BACKEND
  snprintf(scan_wait_arg, sizeof(scan_wait_arg), "%d",
           CONFIG_LVGL_APP_UI_DEMO_WIFI_SCAN_WAIT_MS);
  ret = wifiscan_main(2, scan_argv);
  if (ret == -EIO)
    {
      usleep(UI_DEMO_WIFI_SCAN_FALLBACK_US);
      ret = wifiscan_main(3, scan_standalone_argv);
    }

  if (ret == 0)
    {
      count = ui_demo_wifi_read_scan_results(aps, UI_DEMO_WIFI_MAX_APS);
    }
#else
  ret = -ENOSYS;
#endif

  pthread_mutex_lock(&g_wifi_lock);
  if (generation == g_wifi.generation)
    {
      g_wifi.scan_in_progress = false;
      g_wifi.enabled = wifi_started;

      if (ret < 0)
        {
          g_wifi.status = UI_DEMO_WIFI_STATUS_ERROR;
          ui_demo_wifi_set_message_locked(
            ret == -ENOSYS ?
            "当前固件未启用 WiFi 支持" :
            "WiFi 扫描失败，错误码 %d",
            ret);
        }
      else
        {
          g_wifi.enabled = true;
          memcpy(g_wifi.aps, aps, sizeof(aps));
          g_wifi.ap_count = count;
          g_wifi.status = UI_DEMO_WIFI_STATUS_READY;

          if (count == 0)
            {
              ui_demo_wifi_set_message_locked(
                "未扫描到 WiFi，请靠近路由器后重新扫描");
            }
          else
            {
              ui_demo_wifi_set_message_locked("扫描完成，发现 %lu 个 WiFi",
                                              (unsigned long)count);
            }
        }
    }
  pthread_mutex_unlock(&g_wifi_lock);

  ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_SCAN_FINISHED);
  return ret;
}

static int ui_demo_wifi_connect_entry(int argc, char *argv[])
{
  char ssid[UI_DEMO_WIFI_SSID_LEN];
  char password[UI_DEMO_WIFI_PASSWORD_LEN];
  char timeout_arg[16];
  char *connect_argv[] =
  {
    "wificonnect",
    ssid,
    password,
    timeout_arg,
    NULL
  };
  uint32_t generation;
  int ret;

  (void)argc;
  (void)argv;

  pthread_mutex_lock(&g_wifi_lock);
  ui_demo_wifi_copy_text(ssid, sizeof(ssid), g_wifi.pending_ssid);
  ui_demo_wifi_copy_text(password, sizeof(password), g_wifi.pending_password);
  generation = g_wifi.connect_generation;
  pthread_mutex_unlock(&g_wifi_lock);

#ifdef CONFIG_LVGL_APP_UI_DEMO_WIFI_BACKEND
  snprintf(timeout_arg, sizeof(timeout_arg), "%d",
           CONFIG_LVGL_APP_UI_DEMO_WIFI_CONNECT_TIMEOUT_MS);
  ret = wificonnect_main(4, connect_argv);
#else
  (void)timeout_arg;
  (void)connect_argv;
  ret = -ENOSYS;
#endif

  pthread_mutex_lock(&g_wifi_lock);
  if (generation == g_wifi.generation)
    {
      g_wifi.connect_in_progress = false;

      if (ret == 0)
        {
          g_wifi.enabled = true;
          g_wifi.status = UI_DEMO_WIFI_STATUS_CONNECTED;
          ui_demo_wifi_copy_text(g_wifi.connected_ssid,
                                 sizeof(g_wifi.connected_ssid), ssid);
          ui_demo_wifi_set_message_locked("WiFi 连接成功：%s", ssid);
        }
      else
        {
          g_wifi.status = UI_DEMO_WIFI_STATUS_ERROR;
          ui_demo_wifi_set_message_locked(
            ret == -ENOSYS ?
            "当前固件未启用 WiFi 支持" :
            "WiFi 连接失败：可能是密码错误、认证失败、信号弱或网络已离线");
        }
    }
  pthread_mutex_unlock(&g_wifi_lock);

  ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_CONNECT_FINISHED);
  return ret;
}

static int ui_demo_wifi_stop_entry(int argc, char *argv[])
{
  bool need_disconnect = false;
  char *stop_argv[] =
  {
    "wifistop",
    NULL
  };
  uint32_t generation;
  int ret;

  (void)argc;
  (void)argv;

  pthread_mutex_lock(&g_wifi_lock);
  if (g_wifi.status == UI_DEMO_WIFI_STATUS_CONNECTED)
    {
      need_disconnect = true;
    }

  generation = g_wifi.stop_generation;
  pthread_mutex_unlock(&g_wifi_lock);

#ifdef CONFIG_LVGL_APP_UI_DEMO_WIFI_BACKEND
  if (need_disconnect)
    {
      char *disconnect_argv[] =
      {
        "wifidisconnect",
        NULL
      };

      (void)wifidisconnect_main(1, disconnect_argv);
    }

  ret = wifistop_main(1, stop_argv);
#else
  (void)stop_argv;
  ret = 0;
#endif

  pthread_mutex_lock(&g_wifi_lock);
  if (generation == g_wifi.generation)
    {
      g_wifi.stop_in_progress = false;

      if (ret == 0)
        {
          g_wifi.enabled = false;
          g_wifi.scan_in_progress = false;
          g_wifi.connect_in_progress = false;
          g_wifi.status = UI_DEMO_WIFI_STATUS_OFF;
          g_wifi.ap_count = 0;
          g_wifi.connected_ssid[0] = '\0';
          ui_demo_wifi_set_message_locked(
#ifdef CONFIG_LVGL_APP_UI_DEMO_WIFI_BACKEND
            "WiFi 已关闭"
#else
            "当前固件未启用 WiFi 支持"
#endif
          );
        }
      else
        {
          g_wifi.enabled = true;
          g_wifi.status = UI_DEMO_WIFI_STATUS_ERROR;
          ui_demo_wifi_set_message_locked("WiFi 关闭失败，错误码 %d", ret);
        }
    }
  pthread_mutex_unlock(&g_wifi_lock);

  ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_STOP_FINISHED);
  return ret;
}

static int ui_demo_wifi_create_task(const char *name,
                                    int (*entry)(int argc, char *argv[]))
{
  char *task_argv[] =
  {
    (char *)name,
    NULL
  };
  int pid;

  pid = task_create(name,
                    CONFIG_LVGL_APP_UI_DEMO_WIFI_TASK_PRIORITY,
                    CONFIG_LVGL_APP_UI_DEMO_WIFI_TASK_STACKSIZE,
                    entry,
                    task_argv);
  if (pid < 0)
    {
      return errno == 0 ? -EIO : -errno;
    }

  return 0;
}

void ui_demo_wifi_init(ui_demo_wifi_event_cb_t cb, void *user_data)
{
  pthread_mutex_lock(&g_wifi_lock);
  memset(&g_wifi, 0, sizeof(g_wifi));
  g_wifi.cb = cb;
  g_wifi.user_data = user_data;
  g_wifi.status = UI_DEMO_WIFI_STATUS_OFF;
  ui_demo_wifi_set_message_locked(
#ifdef CONFIG_LVGL_APP_UI_DEMO_WIFI_BACKEND
    "WiFi 未开启"
#else
    "当前固件未启用 WiFi 支持"
#endif
  );
  pthread_mutex_unlock(&g_wifi_lock);
}

void ui_demo_wifi_deinit(void)
{
  pthread_mutex_lock(&g_wifi_lock);
  g_wifi.cb = NULL;
  g_wifi.user_data = NULL;
  pthread_mutex_unlock(&g_wifi_lock);
}

void ui_demo_wifi_get_snapshot(struct ui_demo_wifi_snapshot *snapshot)
{
  if (snapshot == NULL)
    {
      return;
    }

  pthread_mutex_lock(&g_wifi_lock);
  snapshot->enabled = g_wifi.enabled;
  snapshot->scan_in_progress = g_wifi.scan_in_progress;
  snapshot->connect_in_progress = g_wifi.connect_in_progress;
  snapshot->stop_in_progress = g_wifi.stop_in_progress;
  snapshot->status = g_wifi.status;
  ui_demo_wifi_copy_text(snapshot->message, sizeof(snapshot->message),
                         g_wifi.message);
  ui_demo_wifi_copy_text(snapshot->connected_ssid,
                         sizeof(snapshot->connected_ssid),
                         g_wifi.connected_ssid);
  snapshot->ap_count = g_wifi.ap_count;
  memcpy(snapshot->aps, g_wifi.aps, sizeof(snapshot->aps));
  pthread_mutex_unlock(&g_wifi_lock);
}

int ui_demo_wifi_start_scan(bool force_refresh)
{
  bool first_start;
  int ret;

  pthread_mutex_lock(&g_wifi_lock);
  if (g_wifi.scan_in_progress)
    {
      ui_demo_wifi_set_message_locked("正在扫描 WiFi，请稍候");
      pthread_mutex_unlock(&g_wifi_lock);
      ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_STATE_CHANGED);
      return -EINPROGRESS;
    }

  if (g_wifi.connect_in_progress || g_wifi.stop_in_progress)
    {
      ui_demo_wifi_set_message_locked("WiFi 正在执行其他操作，请稍候");
      pthread_mutex_unlock(&g_wifi_lock);
      ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_STATE_CHANGED);
      return -EBUSY;
    }

  if (g_wifi.status == UI_DEMO_WIFI_STATUS_CONNECTED)
    {
      ui_demo_wifi_set_message_locked(
        "已连接 WiFi，请关闭连接后重新扫描");
      pthread_mutex_unlock(&g_wifi_lock);
      ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_STATE_CHANGED);
      return -EBUSY;
    }

  first_start = !g_wifi.enabled;
  g_wifi.enabled = true;
  g_wifi.scan_in_progress = true;
  g_wifi.scan_needs_start = first_start;
  g_wifi.status = first_start ? UI_DEMO_WIFI_STATUS_STARTING :
                                UI_DEMO_WIFI_STATUS_SCANNING;
  g_wifi.scan_generation = g_wifi.generation;

  if (force_refresh && g_wifi.ap_count == 0)
    {
      ui_demo_wifi_set_message_locked("正在启动并扫描 WiFi...");
    }
  else if (first_start)
    {
      ui_demo_wifi_set_message_locked("正在启动并扫描 WiFi...");
    }
  else
    {
      ui_demo_wifi_set_message_locked("正在后台刷新 WiFi 列表...");
    }
  pthread_mutex_unlock(&g_wifi_lock);

  ret = ui_demo_wifi_create_task("ui_wifi_scan", ui_demo_wifi_scan_entry);
  if (ret < 0)
    {
      pthread_mutex_lock(&g_wifi_lock);
      g_wifi.scan_in_progress = false;
      if (first_start)
        {
          g_wifi.enabled = false;
        }

      g_wifi.status = UI_DEMO_WIFI_STATUS_ERROR;
      ui_demo_wifi_set_message_locked("创建 WiFi 扫描任务失败：%d",
                                      ret);
      pthread_mutex_unlock(&g_wifi_lock);
      ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_SCAN_FINISHED);
      return ret;
    }

  ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_SCAN_STARTED);
  return 0;
}

int ui_demo_wifi_connect(const char *ssid, const char *password)
{
  int ret;

  if (ssid == NULL || ssid[0] == '\0')
    {
      pthread_mutex_lock(&g_wifi_lock);
      g_wifi.status = UI_DEMO_WIFI_STATUS_ERROR;
      ui_demo_wifi_set_message_locked("未选择 WiFi，无法连接");
      pthread_mutex_unlock(&g_wifi_lock);
      ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_STATE_CHANGED);
      return -EINVAL;
    }

  if (password == NULL)
    {
      password = "";
    }

  if (!ui_demo_wifi_password_is_valid(password))
    {
      pthread_mutex_lock(&g_wifi_lock);
      g_wifi.status = UI_DEMO_WIFI_STATUS_ERROR;
      ui_demo_wifi_set_message_locked(
        "密码长度不合法：WPA/WPA2 密码为 8-63 位，开放网络可留空");
      pthread_mutex_unlock(&g_wifi_lock);
      ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_STATE_CHANGED);
      return -EINVAL;
    }

  pthread_mutex_lock(&g_wifi_lock);
  if (!g_wifi.enabled)
    {
      g_wifi.status = UI_DEMO_WIFI_STATUS_ERROR;
      ui_demo_wifi_set_message_locked("WiFi 未启动，请先扫描 WiFi");
      pthread_mutex_unlock(&g_wifi_lock);
      ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_STATE_CHANGED);
      return -ENODEV;
    }

  if (g_wifi.status == UI_DEMO_WIFI_STATUS_CONNECTED &&
      strcmp(g_wifi.connected_ssid, ssid) == 0)
    {
      ui_demo_wifi_set_message_locked("已连接到目标 WiFi：%s", ssid);
      pthread_mutex_unlock(&g_wifi_lock);
      ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_STATE_CHANGED);
      return 0;
    }

  if (g_wifi.connect_in_progress || g_wifi.stop_in_progress)
    {
      ui_demo_wifi_set_message_locked("WiFi 正在执行其他操作，请稍候");
      pthread_mutex_unlock(&g_wifi_lock);
      ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_STATE_CHANGED);
      return -EBUSY;
    }

  g_wifi.generation++;
  g_wifi.scan_in_progress = false;
  g_wifi.connect_in_progress = true;
  g_wifi.status = UI_DEMO_WIFI_STATUS_CONNECTING;
  g_wifi.connect_generation = g_wifi.generation;
  ui_demo_wifi_copy_text(g_wifi.pending_ssid, sizeof(g_wifi.pending_ssid),
                         ssid);
  ui_demo_wifi_copy_text(g_wifi.pending_password,
                         sizeof(g_wifi.pending_password), password);
  ui_demo_wifi_set_message_locked("正在连接 WiFi：%s", ssid);
  pthread_mutex_unlock(&g_wifi_lock);

  ret = ui_demo_wifi_create_task("ui_wifi_conn", ui_demo_wifi_connect_entry);
  if (ret < 0)
    {
      pthread_mutex_lock(&g_wifi_lock);
      g_wifi.connect_in_progress = false;
      g_wifi.status = UI_DEMO_WIFI_STATUS_ERROR;
      ui_demo_wifi_set_message_locked(
        "创建 WiFi 连接任务失败：%d", ret);
      pthread_mutex_unlock(&g_wifi_lock);
      ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_CONNECT_FINISHED);
      return ret;
    }

  ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_CONNECT_STARTED);
  return 0;
}

int ui_demo_wifi_stop(void)
{
  int ret;

  pthread_mutex_lock(&g_wifi_lock);
#ifndef CONFIG_LVGL_APP_UI_DEMO_WIFI_BACKEND
  if (!g_wifi.enabled && !g_wifi.scan_in_progress &&
      !g_wifi.connect_in_progress)
    {
      g_wifi.status = UI_DEMO_WIFI_STATUS_OFF;
      ui_demo_wifi_set_message_locked("WiFi 已关闭");
      pthread_mutex_unlock(&g_wifi_lock);
      ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_STOP_FINISHED);
      return 0;
    }
#endif

  if (g_wifi.stop_in_progress)
    {
      ui_demo_wifi_set_message_locked("正在关闭 WiFi，请稍候");
      pthread_mutex_unlock(&g_wifi_lock);
      ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_STATE_CHANGED);
      return -EINPROGRESS;
    }

  g_wifi.generation++;
  g_wifi.scan_in_progress = false;
  g_wifi.connect_in_progress = false;
  g_wifi.stop_in_progress = true;
  g_wifi.stop_generation = g_wifi.generation;
  g_wifi.status = UI_DEMO_WIFI_STATUS_STOPPING;
  ui_demo_wifi_set_message_locked("正在关闭 WiFi...");
  pthread_mutex_unlock(&g_wifi_lock);

  ret = ui_demo_wifi_create_task("ui_wifi_stop", ui_demo_wifi_stop_entry);
  if (ret < 0)
    {
      pthread_mutex_lock(&g_wifi_lock);
      g_wifi.stop_in_progress = false;
      g_wifi.status = UI_DEMO_WIFI_STATUS_ERROR;
      ui_demo_wifi_set_message_locked("创建 WiFi 关闭任务失败：%d",
                                      ret);
      pthread_mutex_unlock(&g_wifi_lock);
      ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_STOP_FINISHED);
      return ret;
    }

  ui_demo_wifi_notify(UI_DEMO_WIFI_EVENT_STATE_CHANGED);
  return 0;
}
