#ifndef VELACLAW_SERVICE_H
#define VELACLAW_SERVICE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define VELACLAW_SERVICE_MESSAGE_LEN 160

enum velaclaw_service_status
{
  VELACLAW_SERVICE_STATUS_STOPPED = 0,
  VELACLAW_SERVICE_STATUS_STARTING,
  VELACLAW_SERVICE_STATUS_READY,
  VELACLAW_SERVICE_STATUS_STOPPING,
  VELACLAW_SERVICE_STATUS_ERROR
};

enum velaclaw_service_event
{
  VELACLAW_SERVICE_EVENT_STATE_CHANGED = 0,
  VELACLAW_SERVICE_EVENT_READY,
  VELACLAW_SERVICE_EVENT_ERROR,
  VELACLAW_SERVICE_EVENT_STOPPED
};

struct velaclaw_service_snapshot
{
  enum velaclaw_service_status status;
  bool task_running;
  char message[VELACLAW_SERVICE_MESSAGE_LEN];
};

typedef void (*velaclaw_service_event_cb_t)(
  enum velaclaw_service_event event, void *user_data);

void velaclaw_service_set_event_cb(velaclaw_service_event_cb_t cb,
                                   void *user_data);
void velaclaw_service_get_snapshot(
  struct velaclaw_service_snapshot *snapshot);
int velaclaw_service_start(void);
int velaclaw_service_stop(void);

void velaclaw_service_report_starting(const char *message);
void velaclaw_service_report_ready(const char *message);
void velaclaw_service_report_error(const char *message);
void velaclaw_service_report_stopped(const char *message);

#ifdef __cplusplus
}
#endif

#endif /* VELACLAW_SERVICE_H */
