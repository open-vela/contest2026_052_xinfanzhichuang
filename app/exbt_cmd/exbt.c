/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/config.h>

#include <bt_ext/bt_ext.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CONFIG_AIC_EXBT_DETECT_PIN
#define CONFIG_AIC_EXBT_DETECT_PIN "I2C0"
#endif

static void exbt_usage(void)
{
  printf("Usage: exbt [--force] mode|status|playstatus|play|prev|next|listen [timeout_ms]\n");
}

static void exbt_print_payload_text(const char *prefix,
                                    const struct bt_ext_event *event)
{
  printf("%s%.*s\n", prefix, (int)event->data_len,
         (const char *)event->data);
}

static void exbt_event_printer(const struct bt_ext_event *event, void *arg)
{
  (void)arg;

  switch (event->type)
    {
    case BT_EXT_EVENT_LOG:
      exbt_print_payload_text("log: ", event);
      break;
    case BT_EXT_EVENT_MUSIC_INFO:
      if (event->music_attr == BT_EXT_MUSIC_ATTR_PLAY_POS)
        {
          printf("position: %" PRIu32 "\n", event->u32_value);
        }
      else if (event->music_attr == BT_EXT_MUSIC_ATTR_DURATION)
        {
          printf("duration: %" PRIu32 "\n", event->u32_value);
        }
      else if (event->music_attr == BT_EXT_MUSIC_ATTR_TITLE)
        {
          exbt_print_payload_text("title: ", event);
        }
      else if (event->music_attr == BT_EXT_MUSIC_ATTR_ARTIST)
        {
          exbt_print_payload_text("artist: ", event);
        }
      else if (event->music_attr == BT_EXT_MUSIC_ATTR_ALBUM)
        {
          exbt_print_payload_text("album: ", event);
        }
      else
        {
          printf("music: attr=0x%02x len=%u\n", event->raw_subtype,
                 event->data_len);
        }

      break;
    case BT_EXT_EVENT_CONNECTION_STATE:
      printf("status bluetooth: %s (%u)\n",
             bt_ext_connection_state_name(
               (enum bt_ext_connection_state)event->value),
             event->value);
      break;
    case BT_EXT_EVENT_PLAY_STATE:
      printf("status play: %s (%u)\n",
             bt_ext_play_state_name((enum bt_ext_play_state)event->value),
             event->value);
      break;
    case BT_EXT_EVENT_AUDIO_SOURCE:
      printf("status audio_source: %u\n", event->value);
      break;
    case BT_EXT_EVENT_COMMAND_RESPONSE:
      if (event->cmd == BT_EXT_CMD_GET_STATUS)
        {
          printf("rsp status: %s (%u)\n",
                 bt_ext_connection_state_name(
                   (enum bt_ext_connection_state)event->value),
                 event->value);
        }
      else if (event->cmd == BT_EXT_CMD_GET_PLAY_STATUS)
        {
          printf("rsp play: %s (%u)\n",
                 bt_ext_play_state_name(
                   (enum bt_ext_play_state)event->value),
                 event->value);
        }
      else
        {
          printf("rsp cmd=0x%02x: %s (%u)\n", event->raw_subtype,
                 event->value == BT_EXT_CMD_SUCCESS ? "success" : "fail",
                 event->value);
        }

      break;
    default:
      printf("frame: type=0x%02x sub=0x%02x len=%u\n", event->raw_type,
             event->raw_subtype, event->data_len);
      break;
    }
}

static int exbt_open(bool force, struct bt_ext_handle **bt)
{
  struct bt_ext_config config;
  int ret;

  memset(&config, 0, sizeof(config));
  config.force = force;
  config.event_cb = exbt_event_printer;

  ret = bt_ext_open(bt, &config);
  if (ret == -ENODEV)
    {
      fprintf(stderr,
              "exbt: UART0 is in debug mode, not external Bluetooth mode\n");
      fprintf(stderr, "exbt: pull %s low at boot or pass --force for diagnostics\n",
              CONFIG_AIC_EXBT_DETECT_PIN);
    }
  else if (ret < 0)
    {
      fprintf(stderr, "exbt: open %s failed: %d\n", bt_ext_uart_dev(), -ret);
    }

  return ret;
}

static int exbt_run_status(struct bt_ext_handle *bt)
{
  enum bt_ext_connection_state state;
  int ret;

  ret = bt_ext_get_connection_state(bt, &state, BT_EXT_DEFAULT_WAIT_MS);
  (void)state;
  return ret;
}

static int exbt_run_playstatus(struct bt_ext_handle *bt)
{
  enum bt_ext_play_state state;
  int ret;

  ret = bt_ext_get_play_state(bt, &state, BT_EXT_DEFAULT_WAIT_MS);
  (void)state;
  return ret;
}

int exbt_main(int argc, char *argv[])
{
  const char *cmd;
  struct bt_ext_handle *bt = NULL;
  bool force = false;
  int argi = 1;
  int ret;

  if (argc > argi && strcmp(argv[argi], "--force") == 0)
    {
      force = true;
      argi++;
    }

  if (argc <= argi)
    {
      exbt_usage();
      return 1;
    }

  cmd = argv[argi++];

  if (strcmp(cmd, "mode") == 0)
    {
      printf("exbt: mode=%s detect=%s uart=%s baud=%u i2c0=%s\n",
             bt_ext_mode_name(), CONFIG_AIC_EXBT_DETECT_PIN,
             bt_ext_uart_dev(), bt_ext_uart_baud(),
             bt_ext_i2c0_available() ? "available" : "disabled");
      return 0;
    }

  ret = exbt_open(force, &bt);
  if (ret < 0)
    {
      return 1;
    }

  if (strcmp(cmd, "status") == 0)
    {
      ret = exbt_run_status(bt);
    }
  else if (strcmp(cmd, "playstatus") == 0)
    {
      ret = exbt_run_playstatus(bt);
    }
  else if (strcmp(cmd, "prev") == 0)
    {
      ret = bt_ext_prev_track(bt, BT_EXT_DEFAULT_WAIT_MS);
    }
  else if (strcmp(cmd, "play") == 0)
    {
      ret = bt_ext_play_pause(bt, BT_EXT_DEFAULT_WAIT_MS);
    }
  else if (strcmp(cmd, "next") == 0)
    {
      ret = bt_ext_next_track(bt, BT_EXT_DEFAULT_WAIT_MS);
    }
  else if (strcmp(cmd, "listen") == 0)
    {
      uint32_t timeout_ms = 10000;

      if (argc > argi)
        {
          timeout_ms = (uint32_t)strtoul(argv[argi], NULL, 0);
        }

      if (timeout_ms == 0)
        {
          timeout_ms = BT_EXT_TIMEOUT_FOREVER;
        }

      ret = bt_ext_process(bt, timeout_ms);
    }
  else
    {
      exbt_usage();
      bt_ext_close(bt);
      return 1;
    }

  bt_ext_close(bt);

  if (ret < 0)
    {
      fprintf(stderr, "exbt: command failed: %d\n", -ret);
      return 1;
    }

  return 0;
}
