#include "config/lvgl_app_config.h"
#include "ui_demo/inc/ui_demo_netinfo.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <nuttx/config.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#include <lvgl/lvgl.h>

#include <cJSON.h>

#ifndef CONFIG_LVGL_APP_UI_DEMO_NETINFO_TASK_PRIORITY
#  define CONFIG_LVGL_APP_UI_DEMO_NETINFO_TASK_PRIORITY 100
#endif

#ifndef CONFIG_LVGL_APP_UI_DEMO_NETINFO_TASK_STACKSIZE
#  define CONFIG_LVGL_APP_UI_DEMO_NETINFO_TASK_STACKSIZE 24576
#endif

#ifndef CONFIG_LVGL_APP_UI_DEMO_NETINFO_HTTP_TIMEOUT_MS
#  define CONFIG_LVGL_APP_UI_DEMO_NETINFO_HTTP_TIMEOUT_MS 8000
#endif

#ifndef CONFIG_LVGL_APP_UI_DEMO_WEATHER_CITY_NAME
#  define CONFIG_LVGL_APP_UI_DEMO_WEATHER_CITY_NAME "北京"
#endif

#ifndef CONFIG_LVGL_APP_UI_DEMO_WEATHER_AMAP_CITY
#  define CONFIG_LVGL_APP_UI_DEMO_WEATHER_AMAP_CITY "110101"
#endif

#ifndef CONFIG_LVGL_APP_UI_DEMO_WEATHER_AMAP_KEY
#  define CONFIG_LVGL_APP_UI_DEMO_WEATHER_AMAP_KEY ""
#endif

#ifndef CONFIG_LVGL_APP_UI_DEMO_WEATHER_FALLBACK_CITY_ID
#  define CONFIG_LVGL_APP_UI_DEMO_WEATHER_FALLBACK_CITY_ID "101010100"
#endif

#define UI_DEMO_NETINFO_LOG_TAG "ui_demo_netinfo"
#define UI_DEMO_NETINFO_TIME_HOST "quan.suning.com"
#define UI_DEMO_NETINFO_TIME_PATH "/getSysTime.do"
#define UI_DEMO_NETINFO_QQ_TIME_HOST "vv.video.qq.com"
#define UI_DEMO_NETINFO_QQ_TIME_PATH "/checktime?otype=json"
#define UI_DEMO_NETINFO_AMAP_HOST "restapi.amap.com"
#define UI_DEMO_NETINFO_FALLBACK_WEATHER_HOST "t.weather.itboy.net"
#define UI_DEMO_NETINFO_HTTP_BUF_LEN 8192
#define UI_DEMO_NETINFO_WATCH_STACKSIZE 8192
#define UI_DEMO_NETINFO_WATCH_INTERVAL_US 1000000
#define UI_DEMO_NETINFO_RETRY_SECONDS 30

/* Keep common dynamic weather/city glyphs in the generated UI font. */
#define UI_DEMO_NETINFO_FONT_SYMBOLS \
  "天气未获取同步更新失败北京东城区晴阴云雨雪雷雾霾沙尘暴冰雹霜冻" \
  "小中大特强阵多转夹浮扬东北西南风级省市区县湿度温度" \
  "等待连接重试"

struct ui_demo_netinfo_context
{
  ui_demo_netinfo_event_cb_t cb;
  void *user_data;
  bool refresh_in_progress;
  bool initialized;
  bool network_connected;
  bool time_valid;
  bool weather_valid;
  bool temperature_valid;
  bool humidity_valid;
  int32_t temperature_c;
  int32_t humidity_percent;
  uint32_t generation;
  char network_ip[INET_ADDRSTRLEN];
  char time_text[UI_DEMO_NETINFO_TEXT_LEN];
  char weather_text[UI_DEMO_NETINFO_TEXT_LEN];
  char message[UI_DEMO_NETINFO_MESSAGE_LEN];
};

static pthread_mutex_t g_netinfo_lock = PTHREAD_MUTEX_INITIALIZER;
static struct ui_demo_netinfo_context g_netinfo;
static bool g_netinfo_watch_started;

static void ui_demo_netinfo_copy_text(char *dst, size_t dst_len,
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

static void ui_demo_netinfo_async_cb(void *user_data)
{
  enum ui_demo_netinfo_event event =
    (enum ui_demo_netinfo_event)(uintptr_t)user_data;
  ui_demo_netinfo_event_cb_t cb;
  void *cb_user_data;

  pthread_mutex_lock(&g_netinfo_lock);
  cb = g_netinfo.cb;
  cb_user_data = g_netinfo.user_data;
  pthread_mutex_unlock(&g_netinfo_lock);

  if (cb != NULL)
    {
      cb(event, cb_user_data);
    }
}

static void ui_demo_netinfo_notify(enum ui_demo_netinfo_event event)
{
  if (lv_async_call(ui_demo_netinfo_async_cb,
                    (void *)(uintptr_t)event) != LV_RESULT_OK)
    {
      syslog(LOG_WARNING, "%s: async notify failed event=%d\n",
             UI_DEMO_NETINFO_LOG_TAG, event);
    }
}

static int ui_demo_netinfo_connect(const char *host, const char *port,
                                   int timeout_ms)
{
  struct addrinfo hints;
  struct addrinfo *result = NULL;
  struct addrinfo *cur;
  int ret = -EHOSTUNREACH;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  ret = getaddrinfo(host, port, &hints, &result);
  if (ret != 0 || result == NULL)
    {
      syslog(LOG_WARNING, "%s: dns failed host=%s ret=%d\n",
             UI_DEMO_NETINFO_LOG_TAG, host, ret);
      return -EHOSTUNREACH;
    }

  for (cur = result; cur != NULL; cur = cur->ai_next)
    {
      int sock;
      int flags;
      int rc;

      sock = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
      if (sock < 0)
        {
          ret = errno == 0 ? -EIO : -errno;
          continue;
        }

      flags = fcntl(sock, F_GETFL, 0);
      if (flags < 0)
        {
          flags = 0;
        }

      (void)fcntl(sock, F_SETFL, flags | O_NONBLOCK);
      rc = connect(sock, cur->ai_addr, cur->ai_addrlen);
      if (rc < 0 && errno == EINPROGRESS)
        {
          fd_set wfds;
          struct timeval tv;

          FD_ZERO(&wfds);
          FD_SET(sock, &wfds);
          tv.tv_sec = timeout_ms / 1000;
          tv.tv_usec = (timeout_ms % 1000) * 1000;
          rc = select(sock + 1, NULL, &wfds, NULL, &tv);
          if (rc > 0)
            {
              int err = 0;
              socklen_t err_len = sizeof(err);

              if (getsockopt(sock, SOL_SOCKET, SO_ERROR,
                             &err, &err_len) < 0 || err != 0)
                {
                  rc = -1;
                  errno = err == 0 ? EIO : err;
                }
              else
                {
                  rc = 0;
                }
            }
          else if (rc == 0)
            {
              rc = -1;
              errno = ETIMEDOUT;
            }
        }

      if (rc == 0)
        {
          struct timeval tv;

          (void)fcntl(sock, F_SETFL, flags);
          tv.tv_sec = timeout_ms / 1000;
          tv.tv_usec = (timeout_ms % 1000) * 1000;
          (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                           &tv, sizeof(tv));
          (void)setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO,
                           &tv, sizeof(tv));
          freeaddrinfo(result);
          return sock;
        }

      ret = errno == 0 ? -EIO : -errno;
      close(sock);
    }

  freeaddrinfo(result);
  return ret;
}

static bool ui_demo_netinfo_get_network_ip(char *ifname,
                                           size_t ifname_len,
                                           char *ip,
                                           size_t ip_len)
{
  struct ifaddrs *ifa_list = NULL;
  struct ifaddrs *ifa;
  bool found = false;

  if (ifname != NULL && ifname_len > 0)
    {
      ifname[0] = '\0';
    }

  if (ip != NULL && ip_len > 0)
    {
      ip[0] = '\0';
    }

  if (ip == NULL || ip_len == 0 || getifaddrs(&ifa_list) != 0)
    {
      return false;
    }

  for (ifa = ifa_list; ifa != NULL; ifa = ifa->ifa_next)
    {
      struct sockaddr_in *sin;
      uint32_t addr;

      if (ifa->ifa_addr == NULL ||
          ifa->ifa_addr->sa_family != AF_INET)
        {
          continue;
        }

      if (ifa->ifa_name != NULL &&
          strncmp(ifa->ifa_name, "lo", 2) == 0)
        {
          continue;
        }

      sin = (struct sockaddr_in *)ifa->ifa_addr;
      addr = ntohl(sin->sin_addr.s_addr);
      if (addr == 0 || (addr >> 24) == 127)
        {
          continue;
        }

      if (inet_ntop(AF_INET, &sin->sin_addr, ip, ip_len) != NULL)
        {
          if (ifname != NULL && ifname_len > 0)
            {
              snprintf(ifname, ifname_len, "%s",
                       ifa->ifa_name == NULL ? "?" : ifa->ifa_name);
            }

          found = true;
          break;
        }
    }

  freeifaddrs(ifa_list);
  return found;
}

static int ui_demo_netinfo_send_all(int sock, const char *data, size_t len)
{
  size_t sent = 0;

  while (sent < len)
    {
      ssize_t n = send(sock, data + sent, len - sent, 0);

      if (n < 0)
        {
          return errno == 0 ? -EIO : -errno;
        }

      if (n == 0)
        {
          return -EPIPE;
        }

      sent += (size_t)n;
    }

  return OK;
}

static const char *ui_demo_netinfo_strcasestr(const char *haystack,
                                              const char *needle)
{
  size_t needle_len;

  if (haystack == NULL || needle == NULL)
    {
      return NULL;
    }

  needle_len = strlen(needle);
  if (needle_len == 0)
    {
      return haystack;
    }

  for (; *haystack != '\0'; haystack++)
    {
      if (strncasecmp(haystack, needle, needle_len) == 0)
        {
          return haystack;
        }
    }

  return NULL;
}

static int ui_demo_netinfo_decode_chunked(const char *src,
                                          char *dst,
                                          size_t dst_cap)
{
  const char *p = src;
  size_t used = 0;

  if (src == NULL || dst == NULL || dst_cap == 0)
    {
      return -EINVAL;
    }

  while (*p != '\0')
    {
      char *end = NULL;
      unsigned long chunk_len;

      while (*p == '\r' || *p == '\n')
        {
          p++;
        }

      chunk_len = strtoul(p, &end, 16);
      if (end == p)
        {
          return -EINVAL;
        }

      while (*end != '\0' && *end != '\r' && *end != '\n')
        {
          end++;
        }

      if (end[0] == '\r' && end[1] == '\n')
        {
          p = end + 2;
        }
      else if (end[0] == '\n')
        {
          p = end + 1;
        }
      else
        {
          return -EINVAL;
        }

      if (chunk_len == 0)
        {
          dst[used] = '\0';
          return OK;
        }

      if (used + chunk_len >= dst_cap)
        {
          return -ENOSPC;
        }

      if (strlen(p) < chunk_len)
        {
          return -EINVAL;
        }

      memcpy(dst + used, p, chunk_len);
      used += chunk_len;
      p += chunk_len;

      if (p[0] == '\r' && p[1] == '\n')
        {
          p += 2;
        }
      else if (p[0] == '\n')
        {
          p += 1;
        }
    }

  return -EINVAL;
}

static int ui_demo_netinfo_http_get(const char *host, const char *path,
                                    char *body, size_t body_cap)
{
  char request[512];
  char *response;
  char *body_start;
  char *header_end;
  bool chunked = false;
  size_t used = 0;
  int sock;
  int ret;

  if (host == NULL || path == NULL || body == NULL || body_cap == 0)
    {
      return -EINVAL;
    }

  body[0] = '\0';
  response = malloc(UI_DEMO_NETINFO_HTTP_BUF_LEN);
  if (response == NULL)
    {
      return -ENOMEM;
    }

  sock = ui_demo_netinfo_connect(host, "80",
                                 CONFIG_LVGL_APP_UI_DEMO_NETINFO_HTTP_TIMEOUT_MS);
  if (sock < 0)
    {
      syslog(LOG_WARNING, "%s: connect failed host=%s ret=%d\n",
             UI_DEMO_NETINFO_LOG_TAG, host, sock);
      free(response);
      return sock;
    }

  snprintf(request, sizeof(request),
           "GET %s HTTP/1.1\r\n"
           "Host: %s\r\n"
           "User-Agent: Mozilla/5.0\r\n"
           "Accept: */*\r\n"
           "Connection: close\r\n"
           "\r\n",
           path, host);

  ret = ui_demo_netinfo_send_all(sock, request, strlen(request));
  if (ret < 0)
    {
      syslog(LOG_WARNING, "%s: send failed host=%s ret=%d\n",
             UI_DEMO_NETINFO_LOG_TAG, host, ret);
      close(sock);
      free(response);
      return ret;
    }

  while (used + 1 < UI_DEMO_NETINFO_HTTP_BUF_LEN)
    {
      ssize_t n = recv(sock, response + used,
                       UI_DEMO_NETINFO_HTTP_BUF_LEN - used - 1, 0);

      if (n < 0)
        {
          if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
              break;
            }

          close(sock);
          free(response);
          syslog(LOG_WARNING, "%s: recv failed host=%s err=%d\n",
                 UI_DEMO_NETINFO_LOG_TAG, host, errno);
          return errno == 0 ? -EIO : -errno;
        }

      if (n == 0)
        {
          break;
        }

      used += (size_t)n;
    }

  close(sock);
  response[used] = '\0';

  if (strncmp(response, "HTTP/", 5) == 0)
    {
      int status = 0;

      if (sscanf(response, "HTTP/%*s %d", &status) != 1 ||
          status < 200 || status >= 300)
        {
          syslog(LOG_WARNING, "%s: http status host=%s status=%d\n",
                 UI_DEMO_NETINFO_LOG_TAG, host, status);
          free(response);
          return -EIO;
        }
    }

  header_end = strstr(response, "\r\n\r\n");
  if (header_end == NULL)
    {
      body_start = response;
    }
  else
    {
      *header_end = '\0';
      body_start = header_end + 4;
      chunked =
        ui_demo_netinfo_strcasestr(response, "Transfer-Encoding:") != NULL &&
        ui_demo_netinfo_strcasestr(response, "chunked") != NULL;
    }

  if (chunked)
    {
      ret = ui_demo_netinfo_decode_chunked(body_start, body, body_cap);
      if (ret < 0)
        {
          syslog(LOG_WARNING, "%s: decode chunked failed host=%s ret=%d\n",
                 UI_DEMO_NETINFO_LOG_TAG, host, ret);
          free(response);
          return ret;
        }
    }
  else
    {
      snprintf(body, body_cap, "%s", body_start);
    }

  syslog(LOG_INFO, "%s: http ok host=%s bytes=%lu\n",
         UI_DEMO_NETINFO_LOG_TAG, host, (unsigned long)strlen(body));
  free(response);
  return OK;
}

static const char *ui_demo_netinfo_json_string(cJSON *obj,
                                               const char *name)
{
  cJSON *item;

  if (obj == NULL || name == NULL)
    {
      return NULL;
    }

  item = cJSON_GetObjectItemCaseSensitive(obj, name);
  if (cJSON_IsString(item) && item->valuestring != NULL)
    {
      return item->valuestring;
    }

  return NULL;
}

static int ui_demo_netinfo_parse_int_text(const char *text, int *out)
{
  const char *p;

  if (text == NULL || out == NULL)
    {
      return -EINVAL;
    }

  for (p = text; *p != '\0'; p++)
    {
      if ((*p >= '0' && *p <= '9') ||
          (*p == '-' && p[1] >= '0' && p[1] <= '9'))
        {
          char *end = NULL;
          long value = strtol(p, &end, 10);

          if (end != p)
            {
              *out = (int)value;
              return OK;
            }
        }
    }

  return -EINVAL;
}

static int ui_demo_netinfo_parse_time(const char *json,
                                      char *time_text,
                                      size_t time_len)
{
  cJSON *root;
  const char *sys_time;
  int ret = -EINVAL;

  root = cJSON_Parse(json);
  if (root == NULL)
    {
      return -EINVAL;
    }

  sys_time = ui_demo_netinfo_json_string(root, "sysTime2");
  if (sys_time != NULL && strlen(sys_time) >= 16)
    {
      snprintf(time_text, time_len, "%.5s", sys_time + 11);
      ret = OK;
    }

  cJSON_Delete(root);
  return ret;
}

static int ui_demo_netinfo_parse_epoch_time(const char *text,
                                            char *time_text,
                                            size_t time_len)
{
  const char *p;
  long epoch;
  long local_seconds;
  int hour;
  int minute;
  char *end = NULL;

  if (text == NULL || time_text == NULL || time_len == 0)
    {
      return -EINVAL;
    }

  p = strstr(text, "\"t\":");
  if (p != NULL)
    {
      p += 4;
    }
  else
    {
      p = strstr(text, "<t>");
      if (p == NULL)
        {
          return -EINVAL;
        }

      p += 3;
    }

  epoch = strtol(p, &end, 10);
  if (end == p || epoch <= 0)
    {
      return -EINVAL;
    }

  local_seconds = epoch + 8 * 60 * 60;
  hour = (int)((local_seconds / 3600) % 24);
  minute = (int)((local_seconds / 60) % 60);
  if (hour < 0)
    {
      hour += 24;
    }

  if (minute < 0)
    {
      minute += 60;
    }

  snprintf(time_text, time_len, "%02d:%02d", hour, minute);
  return OK;
}

static int ui_demo_netinfo_parse_amap_weather(
  const char *json, struct ui_demo_netinfo_snapshot *snapshot)
{
  cJSON *root;
  cJSON *lives;
  cJSON *live;
  const char *status;
  const char *city;
  const char *weather;
  const char *temperature;
  const char *humidity;
  int value;
  int ret = -EINVAL;

  root = cJSON_Parse(json);
  if (root == NULL)
    {
      return -EINVAL;
    }

  status = ui_demo_netinfo_json_string(root, "status");
  lives = cJSON_GetObjectItemCaseSensitive(root, "lives");
  live = cJSON_IsArray(lives) ? cJSON_GetArrayItem(lives, 0) : NULL;

  if (status != NULL && strcmp(status, "1") == 0 && cJSON_IsObject(live))
    {
      city = ui_demo_netinfo_json_string(live, "city");
      weather = ui_demo_netinfo_json_string(live, "weather");
      temperature = ui_demo_netinfo_json_string(live, "temperature");
      humidity = ui_demo_netinfo_json_string(live, "humidity");

      snprintf(snapshot->weather_text, sizeof(snapshot->weather_text),
               "%s %s",
               city == NULL ? CONFIG_LVGL_APP_UI_DEMO_WEATHER_CITY_NAME :
               city,
               weather == NULL ? "天气" : weather);
      snapshot->weather_valid = true;

      if (ui_demo_netinfo_parse_int_text(temperature, &value) == OK)
        {
          snapshot->temperature_c = value;
          snapshot->temperature_valid = true;
        }

      if (ui_demo_netinfo_parse_int_text(humidity, &value) == OK)
        {
          snapshot->humidity_percent = value;
          snapshot->humidity_valid = true;
        }

      ret = OK;
    }

  cJSON_Delete(root);
  return ret;
}

static cJSON *ui_demo_netinfo_find_weather_object(cJSON *root)
{
  cJSON *data;
  cJSON *info;

  if (root == NULL)
    {
      return NULL;
    }

  data = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (cJSON_IsArray(data))
    {
      return cJSON_GetArrayItem(data, 0);
    }

  if (cJSON_IsObject(data))
    {
      return data;
    }

  info = cJSON_GetObjectItemCaseSensitive(root, "info");
  if (cJSON_IsObject(info))
    {
      return info;
    }

  return root;
}

static int ui_demo_netinfo_parse_fallback_weather(
  const char *json, struct ui_demo_netinfo_snapshot *snapshot)
{
  cJSON *root;
  cJSON *city_info;
  cJSON *weather_obj;
  cJSON *forecast;
  cJSON *today;
  const char *city;
  const char *weather;
  const char *temperature;
  const char *humidity;
  int value;
  int ret = -EINVAL;

  root = cJSON_Parse(json);
  if (root == NULL)
    {
      return -EINVAL;
    }

  city_info = cJSON_GetObjectItemCaseSensitive(root, "cityInfo");
  weather_obj = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (!cJSON_IsObject(weather_obj))
    {
      weather_obj = ui_demo_netinfo_find_weather_object(root);
    }

  if (weather_obj != NULL)
    {
      city = ui_demo_netinfo_json_string(city_info, "city");
      if (city == NULL)
        {
          city = ui_demo_netinfo_json_string(root, "city");
        }

      if (city == NULL)
        {
          city = ui_demo_netinfo_json_string(weather_obj, "city");
        }

      weather = ui_demo_netinfo_json_string(weather_obj, "weather");
      if (weather == NULL)
        {
          weather = ui_demo_netinfo_json_string(weather_obj, "type");
        }

      if (weather == NULL)
        {
          forecast = cJSON_GetObjectItemCaseSensitive(weather_obj,
                                                     "forecast");
          today = cJSON_IsArray(forecast) ?
                  cJSON_GetArrayItem(forecast, 0) : NULL;
          weather = ui_demo_netinfo_json_string(today, "type");
        }

      snprintf(snapshot->weather_text, sizeof(snapshot->weather_text),
               "%s %s",
               city == NULL ? CONFIG_LVGL_APP_UI_DEMO_WEATHER_CITY_NAME :
               city,
               weather == NULL ? "天气" : weather);
      snapshot->weather_valid = true;

      temperature = ui_demo_netinfo_json_string(weather_obj, "temperature");
      if (temperature == NULL)
        {
          temperature = ui_demo_netinfo_json_string(weather_obj, "wendu");
        }

      if (temperature == NULL)
        {
          temperature = ui_demo_netinfo_json_string(weather_obj, "high");
        }

      if (ui_demo_netinfo_parse_int_text(temperature, &value) == OK)
        {
          snapshot->temperature_c = value;
          snapshot->temperature_valid = true;
        }

      humidity = ui_demo_netinfo_json_string(weather_obj, "humidity");
      if (humidity == NULL)
        {
          humidity = ui_demo_netinfo_json_string(weather_obj, "shidu");
        }

      if (ui_demo_netinfo_parse_int_text(humidity, &value) == OK)
        {
          snapshot->humidity_percent = value;
          snapshot->humidity_valid = true;
        }

      ret = OK;
    }

  cJSON_Delete(root);
  return ret;
}

static int ui_demo_netinfo_fetch_time(
  struct ui_demo_netinfo_snapshot *snapshot)
{
  char json[UI_DEMO_NETINFO_HTTP_BUF_LEN];
  char time_text[UI_DEMO_NETINFO_TEXT_LEN];
  int ret;

  ret = ui_demo_netinfo_http_get(UI_DEMO_NETINFO_TIME_HOST,
                                 UI_DEMO_NETINFO_TIME_PATH,
                                 json, sizeof(json));
  if (ret == OK)
    {
      ret = ui_demo_netinfo_parse_time(json, time_text, sizeof(time_text));
      if (ret == OK)
        {
          ui_demo_netinfo_copy_text(snapshot->time_text,
                                    sizeof(snapshot->time_text), time_text);
          snapshot->time_valid = true;
          return OK;
        }
    }

  syslog(LOG_WARNING, "%s: suning time failed ret=%d, try qq\n",
         UI_DEMO_NETINFO_LOG_TAG, ret);

  ret = ui_demo_netinfo_http_get(UI_DEMO_NETINFO_QQ_TIME_HOST,
                                 UI_DEMO_NETINFO_QQ_TIME_PATH,
                                 json, sizeof(json));
  if (ret == OK)
    {
      ret = ui_demo_netinfo_parse_epoch_time(json, time_text,
                                             sizeof(time_text));
      if (ret == OK)
        {
          ui_demo_netinfo_copy_text(snapshot->time_text,
                                    sizeof(snapshot->time_text), time_text);
          snapshot->time_valid = true;
        }
    }

  return ret;
}

static int ui_demo_netinfo_fetch_amap_weather(
  struct ui_demo_netinfo_snapshot *snapshot)
{
  char path[256];
  char json[UI_DEMO_NETINFO_HTTP_BUF_LEN];
  int ret;

  if (CONFIG_LVGL_APP_UI_DEMO_WEATHER_AMAP_KEY[0] == '\0')
    {
      return -ENOENT;
    }

  snprintf(path, sizeof(path),
           "/v3/weather/weatherInfo?city=%s&key=%s&extensions=base&output=JSON",
           CONFIG_LVGL_APP_UI_DEMO_WEATHER_AMAP_CITY,
           CONFIG_LVGL_APP_UI_DEMO_WEATHER_AMAP_KEY);

  ret = ui_demo_netinfo_http_get(UI_DEMO_NETINFO_AMAP_HOST,
                                 path, json, sizeof(json));
  if (ret < 0)
    {
      return ret;
    }

  return ui_demo_netinfo_parse_amap_weather(json, snapshot);
}

static int ui_demo_netinfo_fetch_fallback_weather(
  struct ui_demo_netinfo_snapshot *snapshot)
{
  char path[160];
  char json[UI_DEMO_NETINFO_HTTP_BUF_LEN];
  int ret;

  snprintf(path, sizeof(path), "/api/weather/city/%s",
           CONFIG_LVGL_APP_UI_DEMO_WEATHER_FALLBACK_CITY_ID);

  ret = ui_demo_netinfo_http_get(UI_DEMO_NETINFO_FALLBACK_WEATHER_HOST,
                                 path, json, sizeof(json));
  if (ret < 0)
    {
      return ret;
    }

  return ui_demo_netinfo_parse_fallback_weather(json, snapshot);
}

static int ui_demo_netinfo_refresh_entry(int argc, char *argv[])
{
  struct ui_demo_netinfo_snapshot snapshot;
  uint32_t generation;
  int time_ret;
  int weather_ret;

  (void)argc;
  (void)argv;

  memset(&snapshot, 0, sizeof(snapshot));
  ui_demo_netinfo_copy_text(snapshot.time_text, sizeof(snapshot.time_text),
                            "--:--");
  ui_demo_netinfo_copy_text(snapshot.weather_text,
                            sizeof(snapshot.weather_text),
                            "天气 未获取");

  pthread_mutex_lock(&g_netinfo_lock);
  generation = g_netinfo.generation;
  pthread_mutex_unlock(&g_netinfo_lock);

  time_ret = ui_demo_netinfo_fetch_time(&snapshot);
  weather_ret = ui_demo_netinfo_fetch_amap_weather(&snapshot);
  if (weather_ret < 0)
    {
      if (weather_ret != -ENOENT)
        {
          syslog(LOG_WARNING, "%s: amap weather failed ret=%d\n",
                 UI_DEMO_NETINFO_LOG_TAG, weather_ret);
        }

      weather_ret = ui_demo_netinfo_fetch_fallback_weather(&snapshot);
    }

  snprintf(snapshot.message, sizeof(snapshot.message), "%s%s",
           time_ret == OK ? "时间已更新" : "时间更新失败",
           weather_ret == OK ? "，天气已更新" : "，天气更新失败");

  pthread_mutex_lock(&g_netinfo_lock);
  if (generation == g_netinfo.generation && g_netinfo.initialized)
    {
      g_netinfo.refresh_in_progress = false;
      g_netinfo.time_valid = snapshot.time_valid;
      g_netinfo.weather_valid = snapshot.weather_valid;
      g_netinfo.temperature_valid = snapshot.temperature_valid;
      g_netinfo.humidity_valid = snapshot.humidity_valid;
      g_netinfo.temperature_c = snapshot.temperature_c;
      g_netinfo.humidity_percent = snapshot.humidity_percent;
      ui_demo_netinfo_copy_text(g_netinfo.time_text,
                                sizeof(g_netinfo.time_text),
                                snapshot.time_text);
      ui_demo_netinfo_copy_text(g_netinfo.weather_text,
                                sizeof(g_netinfo.weather_text),
                                snapshot.weather_text);
      ui_demo_netinfo_copy_text(g_netinfo.message,
                                sizeof(g_netinfo.message),
                                snapshot.message);
    }
  pthread_mutex_unlock(&g_netinfo_lock);

  syslog(LOG_INFO, "%s: refresh finished time_ret=%d weather_ret=%d msg=%s\n",
         UI_DEMO_NETINFO_LOG_TAG, time_ret, weather_ret, snapshot.message);
  ui_demo_netinfo_notify(UI_DEMO_NETINFO_EVENT_REFRESH_FINISHED);
  return time_ret == OK || weather_ret == OK ? OK : -EIO;
}

static int ui_demo_netinfo_watch_entry(int argc, char *argv[])
{
  int retry_delay = 0;

  (void)argc;
  (void)argv;

  syslog(LOG_INFO, "%s: network watcher started\n",
         UI_DEMO_NETINFO_LOG_TAG);

  for (; ; )
    {
      char ifname[16];
      char ip[INET_ADDRSTRLEN];
      bool initialized;
      bool connected;
      bool trigger_refresh = false;
      bool notify = false;

      pthread_mutex_lock(&g_netinfo_lock);
      initialized = g_netinfo.initialized;
      pthread_mutex_unlock(&g_netinfo_lock);

      if (!initialized)
        {
          retry_delay = 0;
          usleep(UI_DEMO_NETINFO_WATCH_INTERVAL_US);
          continue;
        }

      connected = ui_demo_netinfo_get_network_ip(ifname, sizeof(ifname),
                                                 ip, sizeof(ip));

      pthread_mutex_lock(&g_netinfo_lock);
      if (g_netinfo.initialized)
        {
          if (connected)
            {
              if (!g_netinfo.network_connected ||
                  strcmp(g_netinfo.network_ip, ip) != 0)
                {
                  g_netinfo.network_connected = true;
                  ui_demo_netinfo_copy_text(g_netinfo.network_ip,
                                            sizeof(g_netinfo.network_ip),
                                            ip);
                  ui_demo_netinfo_copy_text(g_netinfo.message,
                                            sizeof(g_netinfo.message),
                                            "WiFi已连接，正在更新时间和天气");
                  trigger_refresh = true;
                  notify = true;
                  retry_delay = UI_DEMO_NETINFO_RETRY_SECONDS;
                  syslog(LOG_INFO, "%s: network up iface=%s ip=%s\n",
                         UI_DEMO_NETINFO_LOG_TAG,
                         ifname[0] == '\0' ? "?" : ifname, ip);
                }
              else if (!g_netinfo.refresh_in_progress &&
                       (!g_netinfo.time_valid || !g_netinfo.weather_valid))
                {
                  if (retry_delay <= 0)
                    {
                      ui_demo_netinfo_copy_text(g_netinfo.message,
                                                sizeof(g_netinfo.message),
                                                "正在重试更新时间和天气");
                      trigger_refresh = true;
                      notify = true;
                      retry_delay = UI_DEMO_NETINFO_RETRY_SECONDS;
                    }
                }
            }
          else
            {
              if (g_netinfo.network_connected)
                {
                  g_netinfo.network_connected = false;
                  g_netinfo.network_ip[0] = '\0';
                  ui_demo_netinfo_copy_text(g_netinfo.message,
                                            sizeof(g_netinfo.message),
                                            "等待 WiFi 连接");
                  notify = true;
                  syslog(LOG_INFO, "%s: network down\n",
                         UI_DEMO_NETINFO_LOG_TAG);
                }

              retry_delay = 0;
            }
        }
      pthread_mutex_unlock(&g_netinfo_lock);

      if (notify)
        {
          ui_demo_netinfo_notify(UI_DEMO_NETINFO_EVENT_STATE_CHANGED);
        }

      if (trigger_refresh)
        {
          (void)ui_demo_netinfo_refresh();
        }
      else if (connected && retry_delay > 0)
        {
          retry_delay--;
        }

      usleep(UI_DEMO_NETINFO_WATCH_INTERVAL_US);
    }

  return OK;
}

void ui_demo_netinfo_init(ui_demo_netinfo_event_cb_t cb, void *user_data)
{
  bool start_watcher = false;

  pthread_mutex_lock(&g_netinfo_lock);
  memset(&g_netinfo, 0, sizeof(g_netinfo));
  g_netinfo.cb = cb;
  g_netinfo.user_data = user_data;
  g_netinfo.initialized = true;
  ui_demo_netinfo_copy_text(g_netinfo.time_text,
                            sizeof(g_netinfo.time_text), "--:--");
  ui_demo_netinfo_copy_text(g_netinfo.weather_text,
                            sizeof(g_netinfo.weather_text),
                            "天气 未获取");
  ui_demo_netinfo_copy_text(g_netinfo.message,
                            sizeof(g_netinfo.message),
                            "WiFi 连接后自动更新时间和天气");
  if (!g_netinfo_watch_started)
    {
      g_netinfo_watch_started = true;
      start_watcher = true;
    }
  pthread_mutex_unlock(&g_netinfo_lock);

  if (start_watcher)
    {
      char *task_argv[] =
      {
        "ui_netwatch",
        NULL
      };
      int pid;

      pid = task_create("ui_netwatch",
                        CONFIG_LVGL_APP_UI_DEMO_NETINFO_TASK_PRIORITY,
                        UI_DEMO_NETINFO_WATCH_STACKSIZE,
                        ui_demo_netinfo_watch_entry,
                        task_argv);
      if (pid < 0)
        {
          int err = errno == 0 ? EIO : errno;

          pthread_mutex_lock(&g_netinfo_lock);
          g_netinfo_watch_started = false;
          snprintf(g_netinfo.message, sizeof(g_netinfo.message),
                   "创建网络监听任务失败：%d", -err);
          pthread_mutex_unlock(&g_netinfo_lock);
          syslog(LOG_WARNING, "%s: create watcher failed ret=%d\n",
                 UI_DEMO_NETINFO_LOG_TAG, -err);
        }
    }

  (void)UI_DEMO_NETINFO_FONT_SYMBOLS;
}

void ui_demo_netinfo_deinit(void)
{
  pthread_mutex_lock(&g_netinfo_lock);
  g_netinfo.cb = NULL;
  g_netinfo.user_data = NULL;
  g_netinfo.initialized = false;
  g_netinfo.refresh_in_progress = false;
  g_netinfo.generation++;
  pthread_mutex_unlock(&g_netinfo_lock);
}

void ui_demo_netinfo_get_snapshot(
  struct ui_demo_netinfo_snapshot *snapshot)
{
  if (snapshot == NULL)
    {
      return;
    }

  pthread_mutex_lock(&g_netinfo_lock);
  snapshot->refresh_in_progress = g_netinfo.refresh_in_progress;
  snapshot->time_valid = g_netinfo.time_valid;
  snapshot->weather_valid = g_netinfo.weather_valid;
  snapshot->temperature_valid = g_netinfo.temperature_valid;
  snapshot->humidity_valid = g_netinfo.humidity_valid;
  snapshot->temperature_c = g_netinfo.temperature_c;
  snapshot->humidity_percent = g_netinfo.humidity_percent;
  ui_demo_netinfo_copy_text(snapshot->time_text,
                            sizeof(snapshot->time_text),
                            g_netinfo.time_text);
  ui_demo_netinfo_copy_text(snapshot->weather_text,
                            sizeof(snapshot->weather_text),
                            g_netinfo.weather_text);
  ui_demo_netinfo_copy_text(snapshot->message,
                            sizeof(snapshot->message),
                            g_netinfo.message);
  pthread_mutex_unlock(&g_netinfo_lock);
}

int ui_demo_netinfo_refresh(void)
{
  char *task_argv[] =
  {
    "ui_netinfo",
    NULL
  };
  int pid;

  pthread_mutex_lock(&g_netinfo_lock);
  if (!g_netinfo.initialized)
    {
      pthread_mutex_unlock(&g_netinfo_lock);
      return -ENODEV;
    }

  if (g_netinfo.refresh_in_progress)
    {
      ui_demo_netinfo_copy_text(g_netinfo.message,
                                sizeof(g_netinfo.message),
                                "正在更新时间和天气");
      pthread_mutex_unlock(&g_netinfo_lock);
      ui_demo_netinfo_notify(UI_DEMO_NETINFO_EVENT_STATE_CHANGED);
      return -EINPROGRESS;
    }

  g_netinfo.generation++;
  g_netinfo.refresh_in_progress = true;
  ui_demo_netinfo_copy_text(g_netinfo.message,
                            sizeof(g_netinfo.message),
                            "正在更新时间和天气");
  pthread_mutex_unlock(&g_netinfo_lock);

  syslog(LOG_INFO, "%s: refresh started\n", UI_DEMO_NETINFO_LOG_TAG);
  pid = task_create("ui_netinfo",
                    CONFIG_LVGL_APP_UI_DEMO_NETINFO_TASK_PRIORITY,
                    CONFIG_LVGL_APP_UI_DEMO_NETINFO_TASK_STACKSIZE,
                    ui_demo_netinfo_refresh_entry,
                    task_argv);
  if (pid < 0)
    {
      int err = errno == 0 ? EIO : errno;

      pthread_mutex_lock(&g_netinfo_lock);
      g_netinfo.refresh_in_progress = false;
      snprintf(g_netinfo.message, sizeof(g_netinfo.message),
               "创建时间天气任务失败：%d", -err);
      pthread_mutex_unlock(&g_netinfo_lock);
      ui_demo_netinfo_notify(UI_DEMO_NETINFO_EVENT_REFRESH_FINISHED);
      return -err;
    }

  ui_demo_netinfo_notify(UI_DEMO_NETINFO_EVENT_STATE_CHANGED);
  return OK;
}
