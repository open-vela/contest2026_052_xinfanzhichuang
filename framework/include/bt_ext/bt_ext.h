#ifndef __FRAMEWORK_INCLUDE_BT_EXT_BT_EXT_H
#define __FRAMEWORK_INCLUDE_BT_EXT_BT_EXT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BT_EXT_TIMEOUT_NONE 0
#define BT_EXT_TIMEOUT_FOREVER UINT32_MAX
#define BT_EXT_DEFAULT_WAIT_MS 1000

enum bt_ext_log_level
{
  BT_EXT_LOG_ERROR = 0x01,
  BT_EXT_LOG_WARN = 0x02,
  BT_EXT_LOG_INFO = 0x03,
  BT_EXT_LOG_DEBUG = 0x04,
  BT_EXT_LOG_VERBOSE = 0x05,
};

enum bt_ext_music_attr
{
  BT_EXT_MUSIC_ATTR_TITLE = 0x01,
  BT_EXT_MUSIC_ATTR_ARTIST = 0x02,
  BT_EXT_MUSIC_ATTR_ALBUM = 0x03,
  BT_EXT_MUSIC_ATTR_PLAY_POS = 0x04,
  BT_EXT_MUSIC_ATTR_DURATION = 0x05,
};

enum bt_ext_status_type
{
  BT_EXT_STATUS_BLUETOOTH = 0x00,
  BT_EXT_STATUS_PLAY_STATE = 0x01,
  BT_EXT_STATUS_AUDIO_SOURCE = 0x02,
};

enum bt_ext_cmd
{
  BT_EXT_CMD_GET_STATUS = 0x00,
  BT_EXT_CMD_GET_PLAY_STATUS = 0x01,
  BT_EXT_CMD_PREV = 0x02,
  BT_EXT_CMD_PLAY_PAUSE = 0x03,
  BT_EXT_CMD_NEXT = 0x04,
};

enum bt_ext_connection_state
{
  BT_EXT_CONNECTION_DISCONNECTED = 0x00,
  BT_EXT_CONNECTION_CONNECTED = 0x01,
};

enum bt_ext_play_state
{
  BT_EXT_PLAY_STATE_NONE = 0x00,
  BT_EXT_PLAY_STATE_PLAYING = 0x01,
  BT_EXT_PLAY_STATE_PAUSE = 0x02,
};

enum bt_ext_cmd_result
{
  BT_EXT_CMD_FAIL = 0x00,
  BT_EXT_CMD_SUCCESS = 0x01,
};

enum bt_ext_event_type
{
  BT_EXT_EVENT_LOG,
  BT_EXT_EVENT_MUSIC_INFO,
  BT_EXT_EVENT_CONNECTION_STATE,
  BT_EXT_EVENT_PLAY_STATE,
  BT_EXT_EVENT_AUDIO_SOURCE,
  BT_EXT_EVENT_COMMAND_RESPONSE,
  BT_EXT_EVENT_RAW_FRAME,
};

struct bt_ext_event
{
  enum bt_ext_event_type type;
  uint8_t raw_type;
  uint8_t raw_subtype;
  uint16_t data_len;
  const uint8_t *data;
  uint8_t value;
  uint32_t u32_value;
  enum bt_ext_log_level log_level;
  enum bt_ext_music_attr music_attr;
  enum bt_ext_status_type status_type;
  enum bt_ext_cmd cmd;
};

/* event->data is valid only during the callback. */
typedef void (*bt_ext_event_callback_t)(const struct bt_ext_event *event,
                                        void *arg);

struct bt_ext_config
{
  const char *dev_path;
  unsigned int baud;
  bool force;
  bt_ext_event_callback_t event_cb;
  void *event_arg;
};

struct bt_ext_handle;

bool bt_ext_is_external_mode(void);
bool bt_ext_i2c0_available(void);
const char *bt_ext_mode_name(void);
const char *bt_ext_uart_dev(void);
unsigned int bt_ext_uart_baud(void);

int bt_ext_open(struct bt_ext_handle **handle,
                const struct bt_ext_config *config);
void bt_ext_close(struct bt_ext_handle *handle);
void bt_ext_set_event_callback(struct bt_ext_handle *handle,
                               bt_ext_event_callback_t event_cb,
                               void *event_arg);

/* Poll and dispatch ESP32 reports. timeout 0 is non-blocking. */
int bt_ext_process(struct bt_ext_handle *handle, uint32_t timeout_ms);

/* Asynchronous command send; call bt_ext_process() later for the response. */
int bt_ext_send_command(struct bt_ext_handle *handle, enum bt_ext_cmd cmd);

/* Synchronous helpers send a command and wait for its response. */
int bt_ext_get_connection_state(struct bt_ext_handle *handle,
                                enum bt_ext_connection_state *state,
                                uint32_t timeout_ms);
int bt_ext_get_play_state(struct bt_ext_handle *handle,
                          enum bt_ext_play_state *state,
                          uint32_t timeout_ms);
int bt_ext_play_pause(struct bt_ext_handle *handle, uint32_t timeout_ms);
int bt_ext_next_track(struct bt_ext_handle *handle, uint32_t timeout_ms);
int bt_ext_prev_track(struct bt_ext_handle *handle, uint32_t timeout_ms);

const char *bt_ext_connection_state_name(enum bt_ext_connection_state state);
const char *bt_ext_play_state_name(enum bt_ext_play_state state);
const char *bt_ext_music_attr_name(enum bt_ext_music_attr attr);

#ifdef __cplusplus
}
#endif

#endif /* __FRAMEWORK_INCLUDE_BT_EXT_BT_EXT_H */
