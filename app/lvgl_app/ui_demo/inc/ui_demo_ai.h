#ifndef UI_DEMO_AI_H
#define UI_DEMO_AI_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define UI_DEMO_AI_MESSAGE_LEN 256
#define UI_DEMO_AI_REPLY_LEN 512

enum ui_demo_ai_event
{
  UI_DEMO_AI_EVENT_STATE_CHANGED = 0,
  UI_DEMO_AI_EVENT_START_FINISHED,
  UI_DEMO_AI_EVENT_STOP_FINISHED,
  UI_DEMO_AI_EVENT_VOICE_CHANGED,
  UI_DEMO_AI_EVENT_RESPONSE_RECEIVED
};

enum ui_demo_ai_status
{
  UI_DEMO_AI_STATUS_OFF = 0,
  UI_DEMO_AI_STATUS_STARTING,
  UI_DEMO_AI_STATUS_CONNECTED,
  UI_DEMO_AI_STATUS_STOPPING,
  UI_DEMO_AI_STATUS_ERROR
};

struct ui_demo_ai_snapshot
{
  enum ui_demo_ai_status status;
  bool enabled;
  bool start_in_progress;
  bool stop_in_progress;
  bool response_waiting;
  bool voice_enabled;
  char message[UI_DEMO_AI_MESSAGE_LEN];
  char voice_message[UI_DEMO_AI_MESSAGE_LEN];
  char last_title[48];
  char last_user[UI_DEMO_AI_MESSAGE_LEN];
  char last_reply[UI_DEMO_AI_REPLY_LEN];
};

typedef void (*ui_demo_ai_event_cb_t)(enum ui_demo_ai_event event,
                                      void *user_data);

void ui_demo_ai_init(ui_demo_ai_event_cb_t cb, void *user_data);
void ui_demo_ai_deinit(void);
void ui_demo_ai_get_snapshot(struct ui_demo_ai_snapshot *snapshot);
int ui_demo_ai_start(void);
int ui_demo_ai_stop(void);
int ui_demo_ai_send_prompt(const char *title, const char *prompt);
void ui_demo_ai_set_voice_enabled(bool enabled);
void ui_demo_ai_set_turn(const char *title, const char *user_text,
                         const char *reply_text, bool response_waiting);
void ui_demo_ai_set_reply(const char *reply_text);

#ifdef __cplusplus
}
#endif

#endif /* UI_DEMO_AI_H */
