#ifndef UI_DEMO_BLUETOOTH_H
#define UI_DEMO_BLUETOOTH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define UI_DEMO_BLUETOOTH_NAME_LEN 64
#define UI_DEMO_BLUETOOTH_MESSAGE_LEN 192

enum ui_demo_bluetooth_event
{
  UI_DEMO_BLUETOOTH_EVENT_STATE_CHANGED = 0,
  UI_DEMO_BLUETOOTH_EVENT_PRESTART_FINISHED,
  UI_DEMO_BLUETOOTH_EVENT_SERVER_STARTED,
  UI_DEMO_BLUETOOTH_EVENT_STOP_FINISHED
};

enum ui_demo_bluetooth_status
{
  UI_DEMO_BLUETOOTH_STATUS_OFF = 0,
  UI_DEMO_BLUETOOTH_STATUS_PRESTARTING,
  UI_DEMO_BLUETOOTH_STATUS_READY,
  UI_DEMO_BLUETOOTH_STATUS_STARTING,
  UI_DEMO_BLUETOOTH_STATUS_ADVERTISING,
  UI_DEMO_BLUETOOTH_STATUS_CONNECTED,
  UI_DEMO_BLUETOOTH_STATUS_STOPPING,
  UI_DEMO_BLUETOOTH_STATUS_ERROR
};

struct ui_demo_bluetooth_snapshot
{
  bool hci_started;
  bool prestart_in_progress;
  bool start_in_progress;
  bool stop_in_progress;
  bool server_running;
  bool connected;
  enum ui_demo_bluetooth_status status;
  char device_name[UI_DEMO_BLUETOOTH_NAME_LEN];
  char message[UI_DEMO_BLUETOOTH_MESSAGE_LEN];
  int last_error;
};

typedef void (*ui_demo_bluetooth_event_cb_t)(
  enum ui_demo_bluetooth_event event,
  void *user_data);

void ui_demo_bluetooth_init(ui_demo_bluetooth_event_cb_t cb,
                            void *user_data);
void ui_demo_bluetooth_deinit(void);
void ui_demo_bluetooth_get_snapshot(
  struct ui_demo_bluetooth_snapshot *snapshot);

int ui_demo_bluetooth_prestart(void);
int ui_demo_bluetooth_start_server(void);
int ui_demo_bluetooth_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_DEMO_BLUETOOTH_H */
