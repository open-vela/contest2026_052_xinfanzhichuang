#ifndef UI_DEMO_NETINFO_H
#define UI_DEMO_NETINFO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define UI_DEMO_NETINFO_TEXT_LEN 64
#define UI_DEMO_NETINFO_MESSAGE_LEN 128

enum ui_demo_netinfo_event
{
  UI_DEMO_NETINFO_EVENT_STATE_CHANGED = 0,
  UI_DEMO_NETINFO_EVENT_REFRESH_FINISHED
};

struct ui_demo_netinfo_snapshot
{
  bool refresh_in_progress;
  bool time_valid;
  bool weather_valid;
  bool temperature_valid;
  bool humidity_valid;
  int32_t temperature_c;
  int32_t humidity_percent;
  char time_text[UI_DEMO_NETINFO_TEXT_LEN];
  char weather_text[UI_DEMO_NETINFO_TEXT_LEN];
  char message[UI_DEMO_NETINFO_MESSAGE_LEN];
};

typedef void (*ui_demo_netinfo_event_cb_t)(
  enum ui_demo_netinfo_event event, void *user_data);

void ui_demo_netinfo_init(ui_demo_netinfo_event_cb_t cb, void *user_data);
void ui_demo_netinfo_deinit(void);
void ui_demo_netinfo_get_snapshot(
  struct ui_demo_netinfo_snapshot *snapshot);
int ui_demo_netinfo_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_DEMO_NETINFO_H */
