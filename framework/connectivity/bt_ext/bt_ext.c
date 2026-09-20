/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/config.h>

#include <bt_ext/bt_ext.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define BT_EXT_RX_BUFFER_SIZE 512
#define BT_EXT_FRAME_HEADER_SIZE 4

#define VELA_DATA_LOG 0x80
#define VELA_DATA_BLUETOOTH_MUSIC 0x81
#define VELA_DATA_STATUS 0x82
#define VELA_CMD_BLUETOOTH 0x01

#ifndef CONFIG_AIC_EXBT_UART_DEV
#define CONFIG_AIC_EXBT_UART_DEV "/dev/ttyS0"
#endif

#ifndef CONFIG_AIC_EXBT_UART_BAUD
#define CONFIG_AIC_EXBT_UART_BAUD 115200
#endif

#ifdef CONFIG_AIC_EXBT
extern bool aic_board_exbt_uart0_selected(void);
extern bool aic_board_exbt_i2c0_available(void);
extern const char *aic_board_exbt_mode_name(void);
#endif

struct bt_ext_handle
{
  int fd;
  bt_ext_event_callback_t event_cb;
  void *event_arg;
  uint8_t rx[BT_EXT_RX_BUFFER_SIZE];
  size_t used;
};

struct bt_ext_waiter
{
  bool active;
  bool matched;
  uint8_t type;
  uint8_t subtype;
  uint8_t value;
};

static speed_t bt_ext_speed_from_baud(unsigned int baud)
{
  switch (baud)
    {
    case 9600:
      return B9600;
    case 19200:
      return B19200;
    case 38400:
      return B38400;
    case 57600:
      return B57600;
    case 115200:
      return B115200;
#ifdef B230400
    case 230400:
      return B230400;
#endif
#ifdef B460800
    case 460800:
      return B460800;
#endif
#ifdef B921600
    case 921600:
      return B921600;
#endif
    default:
      return B115200;
    }
}

static uint32_t bt_ext_get_le32(const uint8_t *data)
{
  return ((uint32_t)data[0]) | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int bt_ext_uart_configure(int fd, unsigned int baud)
{
  struct termios tio;
  speed_t speed = bt_ext_speed_from_baud(baud);

  if (tcgetattr(fd, &tio) != 0)
    {
      return -errno;
    }

  tio.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
  tio.c_cflag |= CS8 | CREAD | CLOCAL;
  tio.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR);
  tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
  tio.c_oflag &= ~OPOST;
  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;

  if (cfsetispeed(&tio, speed) != 0 || cfsetospeed(&tio, speed) != 0)
    {
      return -errno;
    }

  if (tcsetattr(fd, TCSANOW, &tio) != 0)
    {
      return -errno;
    }

  tcflush(fd, TCIOFLUSH);
  return 0;
}

static int bt_ext_write_all(int fd, const uint8_t *data, size_t len)
{
  size_t off = 0;

  while (off < len)
    {
      ssize_t n = write(fd, data + off, len - off);

      if (n < 0)
        {
          if (errno == EINTR || errno == EAGAIN)
            {
              continue;
            }

          return -errno;
        }

      if (n == 0)
        {
          return -EIO;
        }

      off += (size_t)n;
    }

  tcdrain(fd);
  return 0;
}

static void bt_ext_dispatch_frame(struct bt_ext_handle *handle, uint8_t type,
                                  uint8_t subtype, const uint8_t *data,
                                  uint16_t len,
                                  struct bt_ext_waiter *waiter)
{
  struct bt_ext_event event;

  memset(&event, 0, sizeof(event));
  event.type = BT_EXT_EVENT_RAW_FRAME;
  event.raw_type = type;
  event.raw_subtype = subtype;
  event.data_len = len;
  event.data = data;

  if (len >= 1)
    {
      event.value = data[0];
    }

  if (len >= 4)
    {
      event.u32_value = bt_ext_get_le32(data);
    }

  if (type == VELA_DATA_LOG)
    {
      event.type = BT_EXT_EVENT_LOG;
      event.log_level = (enum bt_ext_log_level)subtype;
    }
  else if (type == VELA_DATA_BLUETOOTH_MUSIC)
    {
      event.type = BT_EXT_EVENT_MUSIC_INFO;
      event.music_attr = (enum bt_ext_music_attr)subtype;
    }
  else if (type == VELA_DATA_STATUS && len >= 1)
    {
      event.status_type = (enum bt_ext_status_type)subtype;

      if (subtype == BT_EXT_STATUS_BLUETOOTH)
        {
          event.type = BT_EXT_EVENT_CONNECTION_STATE;
        }
      else if (subtype == BT_EXT_STATUS_PLAY_STATE)
        {
          event.type = BT_EXT_EVENT_PLAY_STATE;
        }
      else if (subtype == BT_EXT_STATUS_AUDIO_SOURCE)
        {
          event.type = BT_EXT_EVENT_AUDIO_SOURCE;
        }
    }
  else if (type == VELA_CMD_BLUETOOTH && len >= 1)
    {
      event.type = BT_EXT_EVENT_COMMAND_RESPONSE;
      event.cmd = (enum bt_ext_cmd)subtype;
    }

  if (waiter != NULL && waiter->active && type == waiter->type &&
      subtype == waiter->subtype && len >= 1)
    {
      waiter->value = data[0];
      waiter->matched = true;
    }

  if (handle->event_cb != NULL)
    {
      handle->event_cb(&event, handle->event_arg);
    }
}

static int bt_ext_parse_buffer(struct bt_ext_handle *handle,
                               struct bt_ext_waiter *waiter)
{
  while (handle->used >= BT_EXT_FRAME_HEADER_SIZE)
    {
      uint16_t len = ((uint16_t)handle->rx[3] << 8) | handle->rx[2];
      size_t total = BT_EXT_FRAME_HEADER_SIZE + len;

      if (total > sizeof(handle->rx))
        {
          memmove(handle->rx, handle->rx + 1, handle->used - 1);
          handle->used--;
          continue;
        }

      if (handle->used < total)
        {
          break;
        }

      bt_ext_dispatch_frame(handle, handle->rx[0], handle->rx[1],
                            handle->rx + BT_EXT_FRAME_HEADER_SIZE,
                            len, waiter);

      if (handle->used > total)
        {
          memmove(handle->rx, handle->rx + total, handle->used - total);
        }

      handle->used -= total;

      if (waiter != NULL && waiter->matched)
        {
          break;
        }
    }

  if (handle->used == sizeof(handle->rx))
    {
      handle->used = 0;
    }

  return 0;
}

static int bt_ext_poll_read(struct bt_ext_handle *handle, int wait_ms)
{
  struct pollfd pfd;
  ssize_t n;
  int ret;

  pfd.fd = handle->fd;
  pfd.events = POLLIN;
  pfd.revents = 0;

  ret = poll(&pfd, 1, wait_ms);
  if (ret < 0)
    {
      if (errno == EINTR)
        {
          return 0;
        }

      return -errno;
    }

  if (ret == 0)
    {
      return 0;
    }

  if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
    {
      return -EIO;
    }

  if ((pfd.revents & POLLIN) == 0)
    {
      return 0;
    }

  if (handle->used == sizeof(handle->rx))
    {
      handle->used = 0;
    }

  n = read(handle->fd, handle->rx + handle->used,
           sizeof(handle->rx) - handle->used);
  if (n < 0)
    {
      if (errno == EINTR || errno == EAGAIN)
        {
          return 0;
        }

      return -errno;
    }

  if (n == 0)
    {
      return -EIO;
    }

  handle->used += (size_t)n;
  return 0;
}

static int bt_ext_read_frames(struct bt_ext_handle *handle,
                              uint32_t timeout_ms,
                              struct bt_ext_waiter *waiter)
{
  uint32_t elapsed = 0;
  int ret;

  ret = bt_ext_parse_buffer(handle, waiter);
  if (ret < 0 || (waiter != NULL && waiter->matched))
    {
      return ret;
    }

  if (timeout_ms == BT_EXT_TIMEOUT_NONE)
    {
      ret = bt_ext_poll_read(handle, 0);
      if (ret < 0)
        {
          return ret;
        }

      return bt_ext_parse_buffer(handle, waiter);
    }

  while (timeout_ms == BT_EXT_TIMEOUT_FOREVER || elapsed < timeout_ms)
    {
      int wait_ms = 100;

      if (timeout_ms != BT_EXT_TIMEOUT_FOREVER &&
          timeout_ms - elapsed < (uint32_t)wait_ms)
        {
          wait_ms = (int)(timeout_ms - elapsed);
        }

      ret = bt_ext_poll_read(handle, wait_ms);
      if (ret < 0)
        {
          return ret;
        }

      ret = bt_ext_parse_buffer(handle, waiter);
      if (ret < 0 || (waiter != NULL && waiter->matched))
        {
          return ret;
        }

      if (timeout_ms != BT_EXT_TIMEOUT_FOREVER)
        {
          elapsed += (uint32_t)wait_ms;
        }
    }

  return 0;
}

static int bt_ext_wait_response(struct bt_ext_handle *handle,
                                enum bt_ext_cmd cmd, uint8_t *value,
                                uint32_t timeout_ms)
{
  struct bt_ext_waiter waiter;
  int ret;

  if (value == NULL)
    {
      return -EINVAL;
    }

  ret = bt_ext_send_command(handle, cmd);
  if (ret < 0)
    {
      return ret;
    }

  memset(&waiter, 0, sizeof(waiter));
  waiter.active = true;
  waiter.type = VELA_CMD_BLUETOOTH;
  waiter.subtype = (uint8_t)cmd;

  ret = bt_ext_read_frames(handle, timeout_ms, &waiter);
  if (ret < 0)
    {
      return ret;
    }

  if (!waiter.matched)
    {
      return -ETIMEDOUT;
    }

  *value = waiter.value;
  return 0;
}

static int bt_ext_wait_command_result(struct bt_ext_handle *handle,
                                      enum bt_ext_cmd cmd,
                                      uint32_t timeout_ms)
{
  uint8_t value;
  int ret;

  ret = bt_ext_wait_response(handle, cmd, &value, timeout_ms);
  if (ret < 0)
    {
      return ret;
    }

  return value == BT_EXT_CMD_SUCCESS ? 0 : -EIO;
}

bool bt_ext_is_external_mode(void)
{
#ifdef CONFIG_AIC_EXBT
  return aic_board_exbt_uart0_selected();
#else
  return false;
#endif
}

bool bt_ext_i2c0_available(void)
{
#ifdef CONFIG_AIC_EXBT
  return aic_board_exbt_i2c0_available();
#else
  return true;
#endif
}

const char *bt_ext_mode_name(void)
{
#ifdef CONFIG_AIC_EXBT
  return aic_board_exbt_mode_name();
#else
  return "disabled";
#endif
}

const char *bt_ext_uart_dev(void)
{
  return CONFIG_AIC_EXBT_UART_DEV;
}

unsigned int bt_ext_uart_baud(void)
{
  return CONFIG_AIC_EXBT_UART_BAUD;
}

int bt_ext_open(struct bt_ext_handle **handle,
                const struct bt_ext_config *config)
{
  struct bt_ext_handle *ctx;
  const char *dev_path = CONFIG_AIC_EXBT_UART_DEV;
  unsigned int baud = CONFIG_AIC_EXBT_UART_BAUD;
  bool force = false;
  int ret;

  if (handle == NULL)
    {
      return -EINVAL;
    }

  *handle = NULL;

  if (config != NULL)
    {
      if (config->dev_path != NULL)
        {
          dev_path = config->dev_path;
        }

      if (config->baud != 0)
        {
          baud = config->baud;
        }

      force = config->force;
    }

  if (!force && !bt_ext_is_external_mode())
    {
      return -ENODEV;
    }

  ctx = calloc(1, sizeof(*ctx));
  if (ctx == NULL)
    {
      return -ENOMEM;
    }

  ctx->fd = -1;

  if (config != NULL)
    {
      ctx->event_cb = config->event_cb;
      ctx->event_arg = config->event_arg;
    }

  ctx->fd = open(dev_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (ctx->fd < 0)
    {
      ret = -errno;
      free(ctx);
      return ret;
    }

  ret = bt_ext_uart_configure(ctx->fd, baud);
  if (ret < 0)
    {
      close(ctx->fd);
      free(ctx);
      return ret;
    }

  *handle = ctx;
  return 0;
}

void bt_ext_close(struct bt_ext_handle *handle)
{
  if (handle == NULL)
    {
      return;
    }

  if (handle->fd >= 0)
    {
      close(handle->fd);
    }

  free(handle);
}

void bt_ext_set_event_callback(struct bt_ext_handle *handle,
                               bt_ext_event_callback_t event_cb,
                               void *event_arg)
{
  if (handle == NULL)
    {
      return;
    }

  handle->event_cb = event_cb;
  handle->event_arg = event_arg;
}

int bt_ext_process(struct bt_ext_handle *handle, uint32_t timeout_ms)
{
  if (handle == NULL)
    {
      return -EINVAL;
    }

  return bt_ext_read_frames(handle, timeout_ms, NULL);
}

int bt_ext_send_command(struct bt_ext_handle *handle, enum bt_ext_cmd cmd)
{
  uint8_t frame[BT_EXT_FRAME_HEADER_SIZE];

  if (handle == NULL)
    {
      return -EINVAL;
    }

  if (cmd < BT_EXT_CMD_GET_STATUS || cmd > BT_EXT_CMD_NEXT)
    {
      return -EINVAL;
    }

  frame[0] = VELA_CMD_BLUETOOTH;
  frame[1] = (uint8_t)cmd;
  frame[2] = 0;
  frame[3] = 0;

  return bt_ext_write_all(handle->fd, frame, sizeof(frame));
}

int bt_ext_get_connection_state(struct bt_ext_handle *handle,
                                enum bt_ext_connection_state *state,
                                uint32_t timeout_ms)
{
  uint8_t value;
  int ret;

  if (state == NULL)
    {
      return -EINVAL;
    }

  ret = bt_ext_wait_response(handle, BT_EXT_CMD_GET_STATUS, &value,
                             timeout_ms);
  if (ret < 0)
    {
      return ret;
    }

  *state = (enum bt_ext_connection_state)value;
  return 0;
}

int bt_ext_get_play_state(struct bt_ext_handle *handle,
                          enum bt_ext_play_state *state,
                          uint32_t timeout_ms)
{
  uint8_t value;
  int ret;

  if (state == NULL)
    {
      return -EINVAL;
    }

  ret = bt_ext_wait_response(handle, BT_EXT_CMD_GET_PLAY_STATUS, &value,
                             timeout_ms);
  if (ret < 0)
    {
      return ret;
    }

  *state = (enum bt_ext_play_state)value;
  return 0;
}

int bt_ext_play_pause(struct bt_ext_handle *handle, uint32_t timeout_ms)
{
  return bt_ext_wait_command_result(handle, BT_EXT_CMD_PLAY_PAUSE,
                                    timeout_ms);
}

int bt_ext_next_track(struct bt_ext_handle *handle, uint32_t timeout_ms)
{
  return bt_ext_wait_command_result(handle, BT_EXT_CMD_NEXT, timeout_ms);
}

int bt_ext_prev_track(struct bt_ext_handle *handle, uint32_t timeout_ms)
{
  return bt_ext_wait_command_result(handle, BT_EXT_CMD_PREV, timeout_ms);
}

const char *bt_ext_connection_state_name(enum bt_ext_connection_state state)
{
  return state == BT_EXT_CONNECTION_CONNECTED ? "connected" : "disconnected";
}

const char *bt_ext_play_state_name(enum bt_ext_play_state state)
{
  if (state == BT_EXT_PLAY_STATE_PLAYING)
    {
      return "playing";
    }

  if (state == BT_EXT_PLAY_STATE_PAUSE)
    {
      return "pause";
    }

  return "none";
}

const char *bt_ext_music_attr_name(enum bt_ext_music_attr attr)
{
  switch (attr)
    {
    case BT_EXT_MUSIC_ATTR_TITLE:
      return "title";
    case BT_EXT_MUSIC_ATTR_ARTIST:
      return "artist";
    case BT_EXT_MUSIC_ATTR_ALBUM:
      return "album";
    case BT_EXT_MUSIC_ATTR_PLAY_POS:
      return "position";
    case BT_EXT_MUSIC_ATTR_DURATION:
      return "duration";
    default:
      return "unknown";
    }
}
