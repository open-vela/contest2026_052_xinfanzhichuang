#ifndef UI_DEMO_WIFI_H
#define UI_DEMO_WIFI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define UI_DEMO_WIFI_MAX_APS 24
#define UI_DEMO_WIFI_SSID_LEN 64
#define UI_DEMO_WIFI_PASSWORD_LEN 128
#define UI_DEMO_WIFI_STATUS_LEN 96
#define UI_DEMO_WIFI_MESSAGE_LEN 160

enum ui_demo_wifi_event
{
  UI_DEMO_WIFI_EVENT_STATE_CHANGED = 0,
  UI_DEMO_WIFI_EVENT_SCAN_STARTED,
  UI_DEMO_WIFI_EVENT_SCAN_FINISHED,
  UI_DEMO_WIFI_EVENT_CONNECT_STARTED,
  UI_DEMO_WIFI_EVENT_CONNECT_FINISHED,
  UI_DEMO_WIFI_EVENT_STOP_FINISHED
};

enum ui_demo_wifi_status
{
  UI_DEMO_WIFI_STATUS_OFF = 0,
  UI_DEMO_WIFI_STATUS_STARTING,
  UI_DEMO_WIFI_STATUS_SCANNING,
  UI_DEMO_WIFI_STATUS_READY,
  UI_DEMO_WIFI_STATUS_CONNECTING,
  UI_DEMO_WIFI_STATUS_CONNECTED,
  UI_DEMO_WIFI_STATUS_STOPPING,
  UI_DEMO_WIFI_STATUS_ERROR
};

struct ui_demo_wifi_ap
{
  char ssid[UI_DEMO_WIFI_SSID_LEN];
  char status[UI_DEMO_WIFI_STATUS_LEN];
  int32_t signal;
  int32_t rssi;
  uint32_t channel;
};

struct ui_demo_wifi_snapshot
{
  bool enabled;
  bool scan_in_progress;
  bool connect_in_progress;
  bool stop_in_progress;
  enum ui_demo_wifi_status status;
  char message[UI_DEMO_WIFI_MESSAGE_LEN];
  char connected_ssid[UI_DEMO_WIFI_SSID_LEN];
  size_t ap_count;
  struct ui_demo_wifi_ap aps[UI_DEMO_WIFI_MAX_APS];
};

typedef void (*ui_demo_wifi_event_cb_t)(enum ui_demo_wifi_event event,
                                        void *user_data);

void ui_demo_wifi_init(ui_demo_wifi_event_cb_t cb, void *user_data);
void ui_demo_wifi_deinit(void);
void ui_demo_wifi_get_snapshot(struct ui_demo_wifi_snapshot *snapshot);

int ui_demo_wifi_start_scan(bool force_refresh);
int ui_demo_wifi_connect(const char *ssid, const char *password);
int ui_demo_wifi_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_DEMO_WIFI_H */
