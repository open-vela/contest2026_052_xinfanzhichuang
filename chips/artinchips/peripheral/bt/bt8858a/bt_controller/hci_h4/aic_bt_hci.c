/****************************************************************************
 * AIC8800D40L Bluetooth HCI transport for openVela/Zblue.
 *
 * This file exposes the controller as /dev/ttyHCI0 through NuttX's BTH4
 * pseudo device.  The physical UART2 file descriptor is owned by the IO
 * kthread so nsh command task lifetime does not invalidate the transport.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <nuttx/clock.h>
#include <nuttx/kthread.h>
#include <nuttx/kmalloc.h>
#include <nuttx/semaphore.h>
#include <nuttx/wireless/bluetooth/bt_driver.h>
#include <nuttx/wireless/bluetooth/bt_hci.h>
#include <nuttx/wireless/bluetooth/bt_uart.h>

#include "aic_bt_hci.h"

#define AIC_BT_GPIO_GROUP_SIZE 32
#define AIC_BT_GPIO_GROUP(pin) ((pin) / AIC_BT_GPIO_GROUP_SIZE)
#define AIC_BT_GPIO_GROUP_PIN(pin) ((pin) % AIC_BT_GPIO_GROUP_SIZE)

#define AIC_BT_UART2_TX_GPIO "PD.4"
#define AIC_BT_UART2_RX_GPIO "PD.5"
#define AIC_BT_UART2_RTS_GPIO "PA.3"
#define AIC_BT_UART2_CTS_GPIO "PA.2"
#define AIC_BT_UART2_TXRX_FUNC 5
#define AIC_BT_UART2_FLOW_FUNC 8
#define AIC_BT_UART2_DRV      3
#define AIC_BT_PIN_PULL_DIS   0
#define AIC_BT_PIN_PULL_UP    3

#define AIC_BT_POWER_OFF_DELAY_US 100000
#define AIC_BT_POWER_ON_DELAY_US  800000
#define AIC_BT_RX_BUF_SIZE        2048
#define AIC_BT_TX_MAX_SIZE        2048
#define AIC_BT_IO_POLL_MS         5
#define AIC_BT_INTERNAL_CMD_TIMEOUT_MS 1000
#define AIC_BT_LE_OPT_EVT_MAX     16
#define AIC_BT_CMD_HDR_SIZE       sizeof(struct bt_hci_cmd_hdr_s)
#define AIC_BT_SCAN_RSP_MAP_MAX   8
#define AIC_BT_LEGACY_SCAN_TYPE   0x01
#define AIC_BT_LEGACY_SCAN_INTERVAL 0x0010
#define AIC_BT_LEGACY_SCAN_WINDOW   0x0010
#define AIC_BT_LE_CREATE_CONN_PARAM_LEN 25
#define AIC_BT_LE_EXT_CREATE_CONN_FIXED_LEN 10
#define AIC_BT_LE_EXT_CREATE_CONN_PHY_LEN 16
#define AIC_BT_REWRITE_PARAM_MAX AIC_BT_LE_CREATE_CONN_PARAM_LEN
#define AIC_BT_EXT_ADV_EVT_CONN     0x0001
#define AIC_BT_EXT_ADV_EVT_SCAN     0x0002
#define AIC_BT_EXT_ADV_EVT_DIRECT   0x0004
#define AIC_BT_EXT_ADV_EVT_SCAN_RSP 0x0008
#define AIC_BT_EXT_ADV_EVT_LEGACY   0x0010
#define AIC_BT_EXT_ADV_INFO_LEN     24
#define AIC_BT_EXT_ADV_FRAME_MAX    96
#define AIC_BT_HCI_PHY_LE_1M        0x01
#define AIC_BT_HCI_PHY_NONE         0x00
#define AIC_BT_HCI_POWER_INVALID    0x7f
#define AIC_BT_HCI_SID_INVALID      0xff

#define AIC_BT_OP_WRITE_LOCAL_NAME       BT_OP(BT_OGF_BASEBAND, 0x0013)
#define AIC_BT_OP_WRITE_SCAN_ENABLE      BT_OP(BT_OGF_BASEBAND, 0x001a)
#define AIC_BT_OP_READ_SUPPORTED_CMDS    BT_OP(BT_OGF_INFO, 0x0002)
#define AIC_BT_OP_LE_CONN_UPDATE         BT_OP(BT_OGF_LE, 0x0013)
#define AIC_BT_OP_LE_READ_REMOTE_FEATURES BT_OP(BT_OGF_LE, 0x0016)
#define AIC_BT_OP_LE_READ_SUPP_STATES    BT_OP(BT_OGF_LE, 0x001c)
#define AIC_BT_OP_LE_CONN_PARAM_REQ_REPLY BT_OP(BT_OGF_LE, 0x0020)
#define AIC_BT_OP_LE_WRITE_DEFAULT_DLEN  BT_OP(BT_OGF_LE, 0x0024)
#define AIC_BT_OP_LE_READ_RL_SIZE        BT_OP(BT_OGF_LE, 0x002a)
#define AIC_BT_OP_LE_SET_RPA_TIMEOUT     BT_OP(BT_OGF_LE, 0x002e)
#define AIC_BT_OP_LE_READ_MAX_DLEN       BT_OP(BT_OGF_LE, 0x002f)
#define AIC_BT_OP_LE_READ_PHY            BT_OP(BT_OGF_LE, 0x0030)
#define AIC_BT_OP_LE_SET_EXT_ADV_RANDOM  BT_OP(BT_OGF_LE, 0x0035)
#define AIC_BT_OP_LE_SET_EXT_ADV_PARAM   BT_OP(BT_OGF_LE, 0x0036)
#define AIC_BT_OP_LE_SET_EXT_ADV_DATA    BT_OP(BT_OGF_LE, 0x0037)
#define AIC_BT_OP_LE_SET_EXT_SCAN_RSP    BT_OP(BT_OGF_LE, 0x0038)
#define AIC_BT_OP_LE_SET_EXT_ADV_ENABLE  BT_OP(BT_OGF_LE, 0x0039)
#define AIC_BT_OP_LE_READ_MAX_ADV_DATA   BT_OP(BT_OGF_LE, 0x003a)
#define AIC_BT_OP_LE_READ_NUM_ADV_SETS   BT_OP(BT_OGF_LE, 0x003b)
#define AIC_BT_OP_LE_REMOVE_ADV_SET      BT_OP(BT_OGF_LE, 0x003c)
#define AIC_BT_OP_LE_SET_EXT_SCAN_PARAM  BT_OP(BT_OGF_LE, 0x0041)
#define AIC_BT_OP_LE_SET_EXT_SCAN_ENABLE BT_OP(BT_OGF_LE, 0x0042)
#define AIC_BT_OP_LE_EXT_CREATE_CONN     BT_OP(BT_OGF_LE, 0x0043)

#define AIC_BT_LE_ADV_REPORT             0x02
#define AIC_BT_LE_EXT_ADV_REPORT         0x0d

#ifndef CONFIG_AIC_DEV_AIC8800_BT_RST_GPIO
#  define CONFIG_AIC_DEV_AIC8800_BT_RST_GPIO "PD.6"
#endif

#ifdef CONFIG_AIC_BT_UART_PORT
#  define AIC_BT_UART_DEV CONFIG_AIC_BT_UART_PORT
#else
#  define AIC_BT_UART_DEV "/dev/ttyS2"
#endif

int hal_gpio_name2pin(const char *name);
int hal_gpio_direction_output(unsigned int group, unsigned int pin);
int hal_gpio_set_pin_value(unsigned int pin, unsigned int value);
int hal_gpio_set_func(unsigned int group, unsigned int pin, unsigned int func);
int hal_gpio_set_drive_strength(unsigned int group, unsigned int pin,
                                unsigned int strength);
int hal_gpio_set_bias_pull(unsigned int group, unsigned int pin,
                           unsigned int pull);

#if defined(CONFIG_AIC_WLAN_AIC8800D40L) && defined(CONFIG_AIC8800_BT_SUPPORT)
int aic8800_bt_patch_prepare(void);
void aic8800_bt_patch_release(void);
#endif

struct aic_bt_hci_tx
{
  FAR struct aic_bt_hci_tx *next;
  size_t len;
  uint16_t rsp_from;
  uint16_t rsp_to;
  int8_t scan_report_ext;
  bool scan_prepare;
  bool conn_prepare;
  uint8_t data[];
};

struct aic_bt_hci_rsp_map
{
  uint16_t from;
  uint16_t to;
  int8_t scan_report_ext;
};

struct aic_bt_hci_dev
{
  struct bt_driver_s btdev;

  pthread_mutex_t lock;
  pthread_mutex_t tx_lock;
  sem_t ready_sem;
  sem_t done_sem;
  sem_t tx_sem;

  FAR struct aic_bt_hci_tx *tx_head;
  FAR struct aic_bt_hci_tx *tx_tail;

  bool sem_inited;
  bool registered;
  bool powered;
  bool opened;
  bool io_running;
  bool io_ready;
  bool stopping;
  int io_error;
  pid_t io_pid;
  int bt_on_pin;

  bool have_dle_evt;
  bool have_phy_evt;
  uint8_t last_dle_evt_len;
  uint8_t last_phy_evt_len;
  uint32_t dropped_dle_evt;
  uint32_t dropped_phy_evt;
  uint32_t h4_resync_count;
  uint32_t h4_missing_evt_count;
  bool internal_rsp_waiting;
  bool internal_rsp_done;
  uint8_t internal_rsp_status;
  uint16_t internal_rsp_opcode;
  bool legacy_adv_report_as_ext;
  uint32_t converted_adv_reports;
  uint32_t converted_adv_deliver_failed;
  uint8_t rsp_map_head;
  uint8_t rsp_map_count;
  struct aic_bt_hci_rsp_map rsp_map[AIC_BT_SCAN_RSP_MAP_MAX];
  uint8_t last_dle_evt[AIC_BT_LE_OPT_EVT_MAX];
  uint8_t last_phy_evt[AIC_BT_LE_OPT_EVT_MAX];
};

static int aic_bt_hci_open(FAR struct bt_driver_s *btdev);
static int aic_bt_hci_send(FAR struct bt_driver_s *btdev,
                           enum bt_buf_type_e type,
                           FAR void *data, size_t len);
static void aic_bt_hci_close(FAR struct bt_driver_s *btdev);
static int aic_bt_hci_ioctl(FAR struct bt_driver_s *btdev, int cmd,
                            unsigned long arg);
static uint16_t aic_bt_get_le16(FAR const uint8_t *buf);
static void aic_bt_put_le16(FAR uint8_t *buf, uint16_t value);
static void aic_bt_hci_reset_optional_events(FAR struct aic_bt_hci_dev *dev);
static int aic_bt_io_drain_rx(FAR struct aic_bt_hci_dev *dev, int fd,
                              FAR uint8_t *rxbuf, FAR size_t *rxlen);

static const uint8_t g_aic_bt_event_mask[8] =
  {
    0x9c, 0xe8, 0x04, 0x02, 0x00, 0x80, 0x00, 0x20
  };

static const uint8_t g_aic_bt_le_event_mask[8] =
  {
    0x7f, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };

static const uint8_t g_aic_bt_scan_event_mask[8] =
  {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20
  };

static const uint8_t g_aic_bt_le_scan_event_mask[8] =
  {
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };

static struct aic_bt_hci_dev g_aic_bt_hci =
{
  .btdev =
  {
    .head_reserve = H4_HEADER_SIZE,
    .open         = aic_bt_hci_open,
    .send         = aic_bt_hci_send,
    .close        = aic_bt_hci_close,
    .ioctl        = aic_bt_hci_ioctl,
  },
  .lock      = PTHREAD_MUTEX_INITIALIZER,
  .tx_lock   = PTHREAD_MUTEX_INITIALIZER,
  .bt_on_pin = -1,
};

static speed_t aic_bt_uart_speed(void)
{
#if defined(CONFIG_AIC_BT_UART_BAUD) && CONFIG_AIC_BT_UART_BAUD == 115200
  return B115200;
#elif defined(CONFIG_AIC_BT_UART_BAUD) && CONFIG_AIC_BT_UART_BAUD == 921600 && \
      defined(B921600)
  return B921600;
#elif defined(CONFIG_AIC_BT_UART_BAUD) && CONFIG_AIC_BT_UART_BAUD == 1500000 && \
      defined(B1500000)
  return B1500000;
#else
  return B115200;
#endif
}

static int aic_bt_uart_baud_value(void)
{
#ifdef CONFIG_AIC_BT_UART_BAUD
  return CONFIG_AIC_BT_UART_BAUD;
#else
  return 115200;
#endif
}

#ifdef CONFIG_AIC_BT_BT8858A_VELA_HCI_DUMP
static void aic_bt_hci_dump(FAR const char *tag, FAR const uint8_t *buf,
                            size_t len)
{
  size_t dump_len = len > 96 ? 96 : len;
  size_t i;

  printf("AIC BT HCI %s len=%u:", tag, (unsigned int)len);
  for (i = 0; i < dump_len; i++)
    {
      printf(" %02x", buf[i]);
    }

  if (dump_len < len)
    {
      printf(" ...");
    }

  printf("\n");
}
#else
#  define aic_bt_hci_dump(t, b, l) do { } while (0)
#endif

static FAR const char *aic_bt_hci_opcode_name(uint16_t opcode)
{
  switch (opcode)
    {
      case BT_HCI_OP_DISCONNECT:
        return "DISCONNECT";

      case BT_HCI_OP_RESET:
        return "RESET";

      case BT_HCI_OP_SET_EVENT_MASK:
        return "SET_EVENT_MASK";

      case AIC_BT_OP_WRITE_LOCAL_NAME:
        return "WRITE_LOCAL_NAME";

      case AIC_BT_OP_WRITE_SCAN_ENABLE:
        return "WRITE_SCAN_ENABLE";

      case BT_HCI_OP_LE_WRITE_LE_HOST_SUPP:
        return "WRITE_LE_HOST_SUPP";

      case BT_HCI_OP_READ_LOCAL_VERSION_INFO:
        return "READ_LOCAL_VERSION";

      case AIC_BT_OP_READ_SUPPORTED_CMDS:
        return "READ_SUPPORTED_COMMANDS";

      case BT_HCI_OP_READ_LOCAL_FEATURES:
        return "READ_LOCAL_FEATURES";

      case BT_HCI_OP_READ_BUFFER_SIZE:
        return "READ_BUFFER_SIZE";

      case BT_HCI_OP_READ_BD_ADDR:
        return "READ_BD_ADDR";

      case BT_HCI_OP_LE_SET_EVENT_MASK:
        return "LE_SET_EVENT_MASK";

      case BT_HCI_OP_LE_READ_BUFFER_SIZE:
        return "LE_READ_BUFFER_SIZE";

      case BT_HCI_OP_LE_READ_LOCAL_FEATURES:
        return "LE_READ_LOCAL_FEATURES";

      case BT_HCI_OP_LE_SET_RAND_ADDR:
        return "LE_SET_RANDOM_ADDR";

      case BT_HCI_OP_LE_SET_ADV_PARAMETERS:
        return "LE_SET_ADV_PARAMETERS";

      case BT_HCI_OP_LE_SET_ADV_DATA:
        return "LE_SET_ADV_DATA";

      case BT_HCI_OP_LE_SET_SCAN_RSP_DATA:
        return "LE_SET_SCAN_RSP_DATA";

      case BT_HCI_OP_LE_SET_ADV_ENABLE:
        return "LE_SET_ADV_ENABLE";

      case BT_HCI_OP_LE_SET_SCAN_PARAMS:
        return "LE_SET_SCAN_PARAMS";

      case BT_HCI_OP_LE_SET_SCAN_ENABLE:
        return "LE_SET_SCAN_ENABLE";

      case BT_HCI_OP_LE_CREATE_CONN:
        return "LE_CREATE_CONN";

      case BT_HCI_OP_LE_CREATE_CONN_CANCEL:
        return "LE_CREATE_CONN_CANCEL";

      case AIC_BT_OP_LE_CONN_UPDATE:
        return "LE_CONN_UPDATE";

      case AIC_BT_OP_LE_READ_REMOTE_FEATURES:
        return "LE_READ_REMOTE_FEATURES";

      case AIC_BT_OP_LE_READ_SUPP_STATES:
        return "LE_READ_SUPPORTED_STATES";

      case AIC_BT_OP_LE_CONN_PARAM_REQ_REPLY:
        return "LE_CONN_PARAM_REQ_REPLY";

      case AIC_BT_OP_LE_WRITE_DEFAULT_DLEN:
        return "LE_WRITE_DEFAULT_DATA_LEN";

      case AIC_BT_OP_LE_READ_RL_SIZE:
        return "LE_READ_RESOLVING_LIST_SIZE";

      case AIC_BT_OP_LE_SET_RPA_TIMEOUT:
        return "LE_SET_RPA_TIMEOUT";

      case AIC_BT_OP_LE_READ_MAX_DLEN:
        return "LE_READ_MAX_DATA_LEN";

      case AIC_BT_OP_LE_READ_PHY:
        return "LE_READ_PHY";

      case AIC_BT_OP_LE_SET_EXT_ADV_RANDOM:
        return "LE_SET_EXT_ADV_RANDOM";

      case AIC_BT_OP_LE_SET_EXT_ADV_PARAM:
        return "LE_SET_EXT_ADV_PARAM";

      case AIC_BT_OP_LE_SET_EXT_ADV_DATA:
        return "LE_SET_EXT_ADV_DATA";

      case AIC_BT_OP_LE_SET_EXT_SCAN_RSP:
        return "LE_SET_EXT_SCAN_RSP";

      case AIC_BT_OP_LE_SET_EXT_ADV_ENABLE:
        return "LE_SET_EXT_ADV_ENABLE";

      case AIC_BT_OP_LE_READ_MAX_ADV_DATA:
        return "LE_READ_MAX_ADV_DATA_LEN";

      case AIC_BT_OP_LE_READ_NUM_ADV_SETS:
        return "LE_READ_NUM_ADV_SETS";

      case AIC_BT_OP_LE_REMOVE_ADV_SET:
        return "LE_REMOVE_ADV_SET";

      case AIC_BT_OP_LE_SET_EXT_SCAN_PARAM:
        return "LE_SET_EXT_SCAN_PARAM";

      case AIC_BT_OP_LE_SET_EXT_SCAN_ENABLE:
        return "LE_SET_EXT_SCAN_ENABLE";

      case AIC_BT_OP_LE_EXT_CREATE_CONN:
        return "LE_EXT_CREATE_CONN";

      default:
        return "UNKNOWN";
    }
}

static FAR const char *aic_bt_hci_le_event_name(uint8_t subevent)
{
  switch (subevent)
    {
      case BT_HCI_EVT_LE_CONN_COMPLETE:
        return "LE_CONN_COMPLETE";

      case BT_HCI_EVT_LE_ADVERTISING_REPORT:
        return "LE_ADVERTISING_REPORT";

      case BT_HCI_EVT_LE_CONN_UPDATE_COMPLETE:
        return "LE_CONN_UPDATE_COMPLETE";

      case BT_HCI_EVT_LE_READ_REM_FEAT_COMPLETE:
        return "LE_READ_REMOTE_FEATURES_COMPLETE";

      case BT_HCI_EVT_LE_CONN_PARAM_REQ:
        return "LE_CONN_PARAM_REQ";

      case BT_HCI_EVT_LE_DATA_LEN_CHANGE:
        return "LE_DATA_LEN_CHANGE";

      case BT_HCI_EVT_LE_ENH_CONN_COMPLETE:
        return "LE_ENH_CONN_COMPLETE";

      case BT_HCI_EVT_LE_PHY_UPDATE_COMPLETE:
        return "LE_PHY_UPDATE_COMPLETE";

      case BT_HCI_EVT_LE_EXT_ADVERTISING_REPORT:
        return "LE_EXT_ADVERTISING_REPORT";

      case BT_HCI_EVT_LE_ADV_SET_TERMINATED:
        return "LE_ADV_SET_TERMINATED";

      case BT_HCI_EVT_LE_SCAN_REQ_RECEIVED:
        return "LE_SCAN_REQ_RECEIVED";

      case BT_HCI_EVT_LE_CHAN_SEL_ALGO:
        return "LE_CHAN_SEL_ALGO";

      default:
        return "UNKNOWN";
    }
}

static void aic_bt_hci_print_addr(FAR const uint8_t *addr)
{
  printf("%02x:%02x:%02x:%02x:%02x:%02x",
         addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

static void aic_bt_hci_log_cmd_payload(uint16_t opcode,
                                       FAR const uint8_t *params,
                                       uint8_t len)
{
  switch (opcode)
    {
      case BT_HCI_OP_LE_SET_RAND_ADDR:
        if (len >= 6)
          {
            printf(" random=");
            aic_bt_hci_print_addr(params);
          }

        break;

      case BT_HCI_OP_LE_SET_ADV_PARAMETERS:
        if (len >= 15)
          {
            printf(" type=%u own_type=%u channel=0x%02x",
                   params[4], params[5], params[13]);
          }

        break;

      case BT_HCI_OP_LE_SET_ADV_DATA:
      case BT_HCI_OP_LE_SET_SCAN_RSP_DATA:
        if (len >= 1)
          {
            printf(" data_len=%u", params[0]);
          }

        break;

      case BT_HCI_OP_LE_SET_ADV_ENABLE:
      case BT_HCI_OP_LE_SET_SCAN_ENABLE:
        if (len >= 1)
          {
            printf(" enable=%u", params[0]);
          }

        break;

      case BT_HCI_OP_LE_CREATE_CONN:
        if (len >= 25)
          {
            printf(" scan=0x%02x%02x/0x%02x%02x filter=%u "
                   "peer_type=%u peer=",
                   params[1], params[0], params[3], params[2], params[4],
                   params[5]);
            aic_bt_hci_print_addr(&params[6]);
            printf(" own=%u interval=0x%02x%02x-0x%02x%02x "
                   "latency=0x%02x%02x timeout=0x%02x%02x",
                   params[12], params[14], params[13], params[16],
                   params[15], params[18], params[17], params[20],
                   params[19]);
          }

        break;

      case AIC_BT_OP_LE_EXT_CREATE_CONN:
        if (len >= 10)
          {
            printf(" peer_type=%u peer=", params[2]);
            aic_bt_hci_print_addr(&params[3]);
          }

        break;

      case AIC_BT_OP_LE_CONN_UPDATE:
      case AIC_BT_OP_LE_CONN_PARAM_REQ_REPLY:
        if (len >= 14)
          {
            printf(" handle=0x%04x interval=0x%04x-0x%04x latency=%u "
                   "timeout=%u", aic_bt_get_le16(params),
                   aic_bt_get_le16(&params[2]),
                   aic_bt_get_le16(&params[4]),
                   aic_bt_get_le16(&params[6]),
                   aic_bt_get_le16(&params[8]));
          }

        break;

      case AIC_BT_OP_LE_READ_REMOTE_FEATURES:
      case AIC_BT_OP_LE_READ_PHY:
        if (len >= 2)
          {
            printf(" handle=0x%04x", aic_bt_get_le16(params));
          }

        break;

      case AIC_BT_OP_LE_SET_EXT_ADV_RANDOM:
        if (len >= 7)
          {
            printf(" handle=%u random=", params[0]);
            aic_bt_hci_print_addr(&params[1]);
          }

        break;

      case AIC_BT_OP_LE_SET_EXT_ADV_PARAM:
        if (len >= 25)
          {
            printf(" handle=%u props=0x%02x%02x own_type=%u channel=0x%02x",
                   params[0], params[2], params[1], params[4], params[24]);
          }

        break;

      case AIC_BT_OP_LE_SET_EXT_ADV_DATA:
      case AIC_BT_OP_LE_SET_EXT_SCAN_RSP:
        if (len >= 4)
          {
            printf(" handle=%u op=%u data_len=%u",
                   params[0], params[1], params[3]);
          }

        break;

      case AIC_BT_OP_LE_SET_EXT_ADV_ENABLE:
        if (len >= 2)
          {
            printf(" enable=%u sets=%u", params[0], params[1]);
          }

        break;

      default:
        break;
    }
}

static void aic_bt_hci_log_le_event(FAR const uint8_t *payload,
                                    uint8_t plen)
{
  uint8_t subevent = payload[0];

  if (subevent == AIC_BT_LE_ADV_REPORT ||
      subevent == AIC_BT_LE_EXT_ADV_REPORT)
    {
      return;
    }

  printf("AIC BT HCI EVT LE_META subevent=0x%02x %s len=%u",
         subevent, aic_bt_hci_le_event_name(subevent), plen);
  if ((subevent == BT_HCI_EVT_LE_CONN_COMPLETE ||
       subevent == BT_HCI_EVT_LE_ENH_CONN_COMPLETE) && plen >= 12)
    {
      uint16_t handle = aic_bt_get_le16(&payload[2]);

      printf(" status=0x%02x handle=0x%04x role=%u peer_type=%u peer=",
             payload[1], handle, payload[4], payload[5]);
      aic_bt_hci_print_addr(&payload[6]);
    }
  else if (subevent == BT_HCI_EVT_LE_CONN_UPDATE_COMPLETE && plen >= 10)
    {
      uint16_t handle = aic_bt_get_le16(&payload[2]);

      printf(" status=0x%02x handle=0x%04x", payload[1], handle);
    }
  else if (subevent == BT_HCI_EVT_LE_DATA_LEN_CHANGE && plen >= 11)
    {
      uint16_t handle = aic_bt_get_le16(&payload[1]);

      printf(" handle=0x%04x", handle);
    }

  printf("\n");
}

static void aic_bt_hci_log_tx(enum bt_buf_type_e type,
                              FAR const uint8_t *data, size_t len)
{
  uint16_t opcode;
  uint8_t param_len;

  if (type != BT_CMD || len < sizeof(struct bt_hci_cmd_hdr_s))
    {
      return;
    }

  opcode = aic_bt_get_le16(data);
  param_len = data[2];

  printf("AIC BT HCI TX CMD opcode=0x%04x %s len=%u",
         opcode, aic_bt_hci_opcode_name(opcode), param_len);
  if (len >= sizeof(struct bt_hci_cmd_hdr_s) + param_len)
    {
      aic_bt_hci_log_cmd_payload(opcode, &data[3], param_len);
    }

  printf("\n");
}

static void aic_bt_hci_log_event(FAR const uint8_t *packet, size_t len)
{
  uint8_t evt;
  uint8_t plen;
  FAR const uint8_t *payload;

  if (len < H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s) ||
      packet[0] != H4_EVT)
    {
      return;
    }

  evt = packet[H4_HEADER_SIZE];
  plen = packet[H4_HEADER_SIZE + 1];
  payload = &packet[H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s)];
  if (len < H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s) + plen)
    {
      return;
    }

  if (evt == BT_HCI_EVT_CMD_COMPLETE && plen >= 3)
    {
      uint16_t opcode = aic_bt_get_le16(&payload[1]);
      uint8_t status = plen >= 4 ? payload[3] : 0xff;

      printf("AIC BT HCI EVT CMD_COMPLETE opcode=0x%04x %s status=0x%02x\n",
             opcode, aic_bt_hci_opcode_name(opcode), status);
    }
  else if (evt == BT_HCI_EVT_CMD_STATUS && plen >= 4)
    {
      uint16_t opcode = aic_bt_get_le16(&payload[2]);

      printf("AIC BT HCI EVT CMD_STATUS opcode=0x%04x %s status=0x%02x\n",
             opcode, aic_bt_hci_opcode_name(opcode), payload[0]);
    }
  else if (evt == BT_HCI_EVT_DISCONN_COMPLETE && plen >= 4)
    {
      uint16_t handle = aic_bt_get_le16(&payload[1]);

      printf("AIC BT HCI EVT DISCONN_COMPLETE status=0x%02x "
             "handle=0x%04x reason=0x%02x\n",
             payload[0], handle, payload[3]);
    }
  else if (evt == BT_HCI_EVT_LE_META_EVENT && plen >= 1)
    {
      aic_bt_hci_log_le_event(payload, plen);
    }
}

static void aic_bt_hci_reset_optional_events(FAR struct aic_bt_hci_dev *dev)
{
  dev->have_dle_evt = false;
  dev->have_phy_evt = false;
  dev->last_dle_evt_len = 0;
  dev->last_phy_evt_len = 0;
  dev->dropped_dle_evt = 0;
  dev->dropped_phy_evt = 0;
}

static bool aic_bt_hci_optional_event_seen(FAR uint8_t *last,
                                           FAR uint8_t *last_len,
                                           FAR bool *have,
                                           FAR const uint8_t *payload,
                                           uint8_t plen)
{
  uint8_t copy_len;

  if (*have)
    {
      return true;
    }

  copy_len = plen > AIC_BT_LE_OPT_EVT_MAX ? AIC_BT_LE_OPT_EVT_MAX : plen;
  if (copy_len > 0)
    {
      memcpy(last, payload, copy_len);
    }

  *last_len = copy_len;
  *have = true;
  return false;
}

static bool aic_bt_hci_drop_optional_event(FAR struct aic_bt_hci_dev *dev,
                                           FAR const uint8_t *payload,
                                           uint8_t plen)
{
  bool duplicate;
  FAR uint32_t *dropped;
  FAR const char *name;

  if (plen < 1)
    {
      return false;
    }

  switch (payload[0])
    {
      case BT_HCI_EVT_LE_DATA_LEN_CHANGE:
        duplicate =
          aic_bt_hci_optional_event_seen(dev->last_dle_evt,
                                         &dev->last_dle_evt_len,
                                         &dev->have_dle_evt,
                                         payload, plen);
        dropped = &dev->dropped_dle_evt;
        name = "LE_DATA_LEN_CHANGE";
        break;

      case BT_HCI_EVT_LE_PHY_UPDATE_COMPLETE:
        duplicate =
          aic_bt_hci_optional_event_seen(dev->last_phy_evt,
                                         &dev->last_phy_evt_len,
                                         &dev->have_phy_evt,
                                         payload, plen);
        dropped = &dev->dropped_phy_evt;
        name = "LE_PHY_UPDATE_COMPLETE";
        break;

      default:
        return false;
    }

  if (!duplicate)
    {
      *dropped = 0;
      return false;
    }

  (*dropped)++;
  if (*dropped == 1 || (*dropped % 32) == 0)
    {
      printf("AIC BT HCI: drop repeated %s count=%lu\n",
             name, (unsigned long)*dropped);
    }

  return true;
}

static bool aic_bt_hci_cmd_event_opcode(FAR const uint8_t *packet,
                                        size_t len, FAR uint16_t *opcode,
                                        FAR uint8_t *status)
{
  FAR const uint8_t *payload;
  uint8_t evt;
  uint8_t plen;

  if (len < H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s) ||
      packet[0] != H4_EVT)
    {
      return false;
    }

  evt = packet[H4_HEADER_SIZE];
  plen = packet[H4_HEADER_SIZE + 1];
  payload = &packet[H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s)];
  if (len < H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s) + plen)
    {
      return false;
    }

  if (evt == BT_HCI_EVT_CMD_COMPLETE && plen >= 3)
    {
      *opcode = aic_bt_get_le16(&payload[1]);
      *status = plen >= 4 ? payload[3] : 0xff;
      return true;
    }

  if (evt == BT_HCI_EVT_CMD_STATUS && plen >= 4)
    {
      *opcode = aic_bt_get_le16(&payload[2]);
      *status = payload[0];
      return true;
    }

  return false;
}

static bool aic_bt_hci_should_drop_rx(FAR struct aic_bt_hci_dev *dev,
                                      FAR const uint8_t *packet,
                                      size_t len)
{
  uint8_t evt;
  uint8_t plen;
  FAR const uint8_t *payload;
  uint16_t opcode;
  uint8_t status;

  if (dev->internal_rsp_waiting &&
      aic_bt_hci_cmd_event_opcode(packet, len, &opcode, &status) &&
      opcode == dev->internal_rsp_opcode)
    {
      dev->internal_rsp_status = status;
      dev->internal_rsp_done = true;
      printf("AIC BT HCI: suppress internal command response opcode=0x%04x "
             "%s status=0x%02x\n",
             opcode, aic_bt_hci_opcode_name(opcode), status);
      return true;
    }

  if (len < H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s) ||
      packet[0] != H4_EVT)
    {
      return false;
    }

  evt = packet[H4_HEADER_SIZE];
  plen = packet[H4_HEADER_SIZE + 1];
  payload = &packet[H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s)];
  if (len < H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s) + plen)
    {
      return false;
    }

  if (evt == BT_HCI_EVT_DISCONN_COMPLETE)
    {
      aic_bt_hci_reset_optional_events(dev);
      return false;
    }

  if (evt != BT_HCI_EVT_LE_META_EVENT || plen < 1)
    {
      return false;
    }

  if (payload[0] == BT_HCI_EVT_LE_CONN_COMPLETE ||
      payload[0] == BT_HCI_EVT_LE_ENH_CONN_COMPLETE)
    {
      aic_bt_hci_reset_optional_events(dev);
      return false;
    }

  return aic_bt_hci_drop_optional_event(dev, payload, plen);
}

static int aic_bt_sem_wait(FAR sem_t *sem)
{
  int ret;

  do
    {
      ret = nxsem_wait(sem);
    }
  while (ret == -EINTR);

  return ret;
}

static void aic_bt_sem_drain(FAR sem_t *sem)
{
  while (nxsem_trywait(sem) == 0)
    {
    }
}

static void aic_bt_sem_post_once(FAR sem_t *sem)
{
  int semcount;

  nxsem_get_value(sem, &semcount);
  if (semcount < 1)
    {
      nxsem_post(sem);
    }
}

static int aic_bt_init_sems(FAR struct aic_bt_hci_dev *dev)
{
  if (dev->sem_inited)
    {
      return 0;
    }

  if (nxsem_init(&dev->ready_sem, 0, 0) < 0)
    {
      return -errno;
    }

  if (nxsem_init(&dev->done_sem, 0, 0) < 0)
    {
      nxsem_destroy(&dev->ready_sem);
      return -errno;
    }

  if (nxsem_init(&dev->tx_sem, 0, 0) < 0)
    {
      nxsem_destroy(&dev->done_sem);
      nxsem_destroy(&dev->ready_sem);
      return -errno;
    }

  dev->sem_inited = true;
  return 0;
}

static int aic_bt_uart_pinmux_one(FAR const char *name, unsigned int func,
                                  unsigned int pull)
{
  int pin = hal_gpio_name2pin(name);
  unsigned int group;
  unsigned int group_pin;

  if (pin < 0)
    {
      printf("AIC BT HCI: invalid UART2 pin %s\n", name);
      return -EINVAL;
    }

  group = AIC_BT_GPIO_GROUP(pin);
  group_pin = AIC_BT_GPIO_GROUP_PIN(pin);

  hal_gpio_set_func(group, group_pin, func);
  hal_gpio_set_bias_pull(group, group_pin, pull);
  hal_gpio_set_drive_strength(group, group_pin, AIC_BT_UART2_DRV);
  return 0;
}

static int aic_bt_uart_pinmux_init(void)
{
  int ret;

  ret = aic_bt_uart_pinmux_one(AIC_BT_UART2_TX_GPIO,
                               AIC_BT_UART2_TXRX_FUNC,
                               AIC_BT_PIN_PULL_DIS);
  if (ret < 0)
    {
      return ret;
    }

  ret = aic_bt_uart_pinmux_one(AIC_BT_UART2_RX_GPIO,
                               AIC_BT_UART2_TXRX_FUNC,
                               AIC_BT_PIN_PULL_UP);
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_AIC_BT_UART_HW_FLOWCTRL
  ret = aic_bt_uart_pinmux_one(AIC_BT_UART2_RTS_GPIO,
                               AIC_BT_UART2_FLOW_FUNC,
                               AIC_BT_PIN_PULL_DIS);
  if (ret < 0)
    {
      return ret;
    }

  ret = aic_bt_uart_pinmux_one(AIC_BT_UART2_CTS_GPIO,
                               AIC_BT_UART2_FLOW_FUNC,
                               AIC_BT_PIN_PULL_UP);
  if (ret < 0)
    {
      return ret;
    }

  printf("AIC BT HCI: UART2 pinmux %s/%s func %d, %s/%s func %d\n",
         AIC_BT_UART2_TX_GPIO, AIC_BT_UART2_RX_GPIO,
         AIC_BT_UART2_TXRX_FUNC, AIC_BT_UART2_RTS_GPIO,
         AIC_BT_UART2_CTS_GPIO, AIC_BT_UART2_FLOW_FUNC);
#else
  printf("AIC BT HCI: UART2 pinmux %s/%s func %d, RTS/CTS disabled\n",
         AIC_BT_UART2_TX_GPIO, AIC_BT_UART2_RX_GPIO,
         AIC_BT_UART2_TXRX_FUNC);
#endif
  return 0;
}

static int aic_bt_power_init(FAR struct aic_bt_hci_dev *dev)
{
  int pin;

  if (dev->bt_on_pin >= 0)
    {
      return 0;
    }

  pin = hal_gpio_name2pin(CONFIG_AIC_DEV_AIC8800_BT_RST_GPIO);
  if (pin < 0)
    {
      printf("AIC BT HCI: invalid BT_ON gpio %s\n",
             CONFIG_AIC_DEV_AIC8800_BT_RST_GPIO);
      return -EINVAL;
    }

  hal_gpio_direction_output(AIC_BT_GPIO_GROUP(pin),
                            AIC_BT_GPIO_GROUP_PIN(pin));
  dev->bt_on_pin = pin;
  printf("AIC BT HCI: using BT_ON gpio %s pin %d\n",
         CONFIG_AIC_DEV_AIC8800_BT_RST_GPIO, dev->bt_on_pin);
  return 0;
}

static void aic_bt_power_set(FAR struct aic_bt_hci_dev *dev, bool on)
{
  if (aic_bt_power_init(dev) == 0)
    {
      hal_gpio_set_pin_value((unsigned int)dev->bt_on_pin, on ? 1 : 0);
      dev->powered = on;
    }
}

static int aic_bt_hw_prepare_locked(FAR struct aic_bt_hci_dev *dev)
{
  int ret;

  ret = aic_bt_init_sems(dev);
  if (ret < 0)
    {
      return ret;
    }

  ret = aic_bt_power_init(dev);
  if (ret < 0)
    {
      return ret;
    }

  ret = aic_bt_uart_pinmux_init();
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_AIC_BT_BT8858A_VELA_HCI_POWER_CYCLE
  aic_bt_power_set(dev, false);
  usleep(AIC_BT_POWER_OFF_DELAY_US);
  aic_bt_power_set(dev, true);
  usleep(AIC_BT_POWER_ON_DELAY_US);
#else
  if (!dev->powered)
    {
      aic_bt_power_set(dev, true);
      usleep(AIC_BT_POWER_ON_DELAY_US);
    }
#endif

  return 0;
}

static int aic_bt_controller_prepare(void)
{
#if defined(CONFIG_AIC_WLAN_AIC8800D40L) && defined(CONFIG_AIC8800_BT_SUPPORT)
  int ret;

  ret = aic8800_bt_patch_prepare();
  if (ret < 0)
    {
      printf("AIC BT HCI: AIC8800 SDIO BT patch prepare failed: %d\n",
             ret);
      return ret;
    }

  printf("AIC BT HCI: AIC8800 SDIO BT patch prepared\n");
#endif

  return 0;
}

static int aic_bt_uart_configure(int fd)
{
  struct termios tio;

  if (tcgetattr(fd, &tio) < 0)
    {
      printf("AIC BT HCI: tcgetattr failed: %d\n", errno);
      return -errno;
    }

  cfsetispeed(&tio, aic_bt_uart_speed());
  cfsetospeed(&tio, aic_bt_uart_speed());

  tio.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
  tio.c_cflag |= CS8 | CLOCAL | CREAD;
#ifdef CRTSCTS
  tio.c_cflag &= ~CRTSCTS;
#  ifdef CONFIG_AIC_BT_UART_HW_FLOWCTRL
  tio.c_cflag |= CRTSCTS;
#  endif
#endif

  tio.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR);
  tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
  tio.c_oflag &= ~OPOST;
  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;

  if (tcsetattr(fd, TCSANOW, &tio) < 0)
    {
      printf("AIC BT HCI: tcsetattr failed: %d\n", errno);
      return -errno;
    }

  tcflush(fd, TCIOFLUSH);
  return 0;
}

static int aic_bt_uart_open(void)
{
  int fd;
  int ret;

  fd = open(AIC_BT_UART_DEV, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0)
    {
      printf("AIC BT HCI: open %s failed: %d\n", AIC_BT_UART_DEV, errno);
      return -errno;
    }

  ret = aic_bt_uart_configure(fd);
  if (ret < 0)
    {
      close(fd);
      return ret;
    }

  printf("AIC BT HCI: UART open success on %s baud %d%s fd=%d\n",
         AIC_BT_UART_DEV, aic_bt_uart_baud_value(),
#ifdef CONFIG_AIC_BT_UART_HW_FLOWCTRL
         " flowctrl",
#else
         "",
#endif
         fd);

  return fd;
}

static FAR struct aic_bt_hci_tx *
aic_bt_tx_pop(FAR struct aic_bt_hci_dev *dev)
{
  FAR struct aic_bt_hci_tx *tx;

  pthread_mutex_lock(&dev->tx_lock);
  tx = dev->tx_head;
  if (tx != NULL)
    {
      dev->tx_head = tx->next;
      if (dev->tx_head == NULL)
        {
          dev->tx_tail = NULL;
        }
    }

  pthread_mutex_unlock(&dev->tx_lock);
  return tx;
}

static bool aic_bt_tx_pending(FAR struct aic_bt_hci_dev *dev)
{
  bool pending;

  pthread_mutex_lock(&dev->tx_lock);
  pending = dev->tx_head != NULL;
  pthread_mutex_unlock(&dev->tx_lock);
  return pending;
}

static void aic_bt_tx_purge(FAR struct aic_bt_hci_dev *dev)
{
  FAR struct aic_bt_hci_tx *tx;

  while ((tx = aic_bt_tx_pop(dev)) != NULL)
    {
      kmm_free(tx);
    }
}

static int aic_bt_uart_write_all(int fd, FAR const uint8_t *data, size_t len)
{
  size_t written = 0;

  while (written < len)
    {
      ssize_t ret = write(fd, data + written, len - written);
      if (ret > 0)
        {
          written += ret;
        }
      else if (ret < 0 && (errno == EAGAIN || errno == EINTR))
        {
          struct pollfd pfd;

          pfd.fd = fd;
          pfd.events = POLLOUT;
          pfd.revents = 0;
          poll(&pfd, 1, AIC_BT_IO_POLL_MS);
        }
      else if (ret == 0)
        {
          usleep(1000);
        }
      else
        {
          printf("AIC BT HCI: uart write failed: %d\n", errno);
          return -errno;
        }
    }

  return 0;
}

static uint16_t aic_bt_get_le16(FAR const uint8_t *buf)
{
  return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static void aic_bt_put_le16(FAR uint8_t *buf, uint16_t value)
{
  buf[0] = (uint8_t)value;
  buf[1] = (uint8_t)(value >> 8);
}

static void aic_bt_hci_rsp_map_reset(FAR struct aic_bt_hci_dev *dev)
{
  dev->rsp_map_head = 0;
  dev->rsp_map_count = 0;
  dev->legacy_adv_report_as_ext = false;
  dev->converted_adv_reports = 0;
  dev->converted_adv_deliver_failed = 0;
}

static void aic_bt_hci_rsp_map_push(FAR struct aic_bt_hci_dev *dev,
                                    uint16_t from, uint16_t to,
                                    int8_t scan_report_ext)
{
  uint8_t tail;

  if (from == 0 || to == 0)
    {
      return;
    }

  if (dev->rsp_map_count == AIC_BT_SCAN_RSP_MAP_MAX)
    {
      printf("AIC BT HCI: response opcode map full, drop 0x%04x->0x%04x\n",
             dev->rsp_map[dev->rsp_map_head].from,
             dev->rsp_map[dev->rsp_map_head].to);
      dev->rsp_map_head =
        (dev->rsp_map_head + 1) % AIC_BT_SCAN_RSP_MAP_MAX;
      dev->rsp_map_count--;
    }

  tail = (dev->rsp_map_head + dev->rsp_map_count) %
         AIC_BT_SCAN_RSP_MAP_MAX;
  dev->rsp_map[tail].from = from;
  dev->rsp_map[tail].to = to;
  dev->rsp_map[tail].scan_report_ext = scan_report_ext;
  dev->rsp_map_count++;
}

static bool aic_bt_hci_rsp_map_pop(FAR struct aic_bt_hci_dev *dev,
                                   uint16_t from, FAR uint16_t *to,
                                   FAR int8_t *scan_report_ext)
{
  uint8_t i;

  for (i = 0; i < dev->rsp_map_count; i++)
    {
      uint8_t pos = (dev->rsp_map_head + i) % AIC_BT_SCAN_RSP_MAP_MAX;

      if (dev->rsp_map[pos].from == from)
        {
          *to = dev->rsp_map[pos].to;
          *scan_report_ext = dev->rsp_map[pos].scan_report_ext;

          while (i + 1 < dev->rsp_map_count)
            {
              uint8_t next =
                (dev->rsp_map_head + i + 1) % AIC_BT_SCAN_RSP_MAP_MAX;
              uint8_t cur =
                (dev->rsp_map_head + i) % AIC_BT_SCAN_RSP_MAP_MAX;

              dev->rsp_map[cur] = dev->rsp_map[next];
              i++;
            }

          dev->rsp_map_count--;
          return true;
        }
    }

  return false;
}

static uint8_t aic_bt_hci_legacy_own_addr_type(uint8_t own_addr_type)
{
  /* Keep legacy scan close to the known-good bthci uartscan sequence. */
  switch (own_addr_type)
    {
      case 0x00:
      case 0x02:
      case 0x03:
        return 0x00;

      case 0x01:
        return 0x01;

      default:
        return 0x00;
    }
}

static void aic_bt_hci_rewrite_cmd_complete(FAR struct aic_bt_hci_dev *dev,
                                            FAR uint8_t *packet,
                                            size_t len)
{
  FAR uint8_t *payload;
  FAR uint8_t *opcode_ptr;
  uint16_t from;
  uint16_t to;
  int8_t scan_report_ext;
  uint8_t evt;
  uint8_t plen;
  uint8_t status = 0xff;

  if (len < H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s) ||
      packet[0] != H4_EVT)
    {
      return;
    }

  evt = packet[H4_HEADER_SIZE];
  plen = packet[H4_HEADER_SIZE + 1];
  payload = &packet[H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s)];
  if (len < H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s) + plen)
    {
      return;
    }

  if (evt == BT_HCI_EVT_CMD_COMPLETE && plen >= 3)
    {
      opcode_ptr = &payload[1];
      if (plen >= 4)
        {
          status = payload[3];
        }
    }
  else if (evt == BT_HCI_EVT_CMD_STATUS && plen >= 4)
    {
      opcode_ptr = &payload[2];
      status = payload[0];
    }
  else
    {
      return;
    }

  from = aic_bt_get_le16(opcode_ptr);
  if (!aic_bt_hci_rsp_map_pop(dev, from, &to, &scan_report_ext))
    {
      return;
    }

  aic_bt_put_le16(opcode_ptr, to);
  printf("AIC BT HCI: rewrite command response opcode 0x%04x -> 0x%04x\n",
         from, to);
  if (scan_report_ext != 0 && status == 0)
    {
      dev->legacy_adv_report_as_ext = scan_report_ext > 0;
      printf("AIC BT HCI: legacy adv report conversion %s\n",
             dev->legacy_adv_report_as_ext ? "enabled" : "disabled");
    }
}

static bool aic_bt_hci_rewrite_le_cmd(FAR const uint8_t *data, size_t len,
                                      FAR uint8_t *out, FAR size_t *out_len,
                                      FAR uint16_t *rsp_from,
                                      FAR uint16_t *rsp_to,
                                      FAR int8_t *scan_report_ext,
                                      FAR bool *scan_prepare,
                                      FAR bool *conn_prepare)
{
  FAR const uint8_t *params;
  uint16_t opcode;
  uint8_t param_len;

  if (len < AIC_BT_CMD_HDR_SIZE)
    {
      return false;
    }

  opcode = aic_bt_get_le16(data);
  param_len = data[2];
  if (len < AIC_BT_CMD_HDR_SIZE + param_len)
    {
      return false;
    }

  params = &data[AIC_BT_CMD_HDR_SIZE];
  if (opcode == AIC_BT_OP_LE_SET_EXT_SCAN_PARAM && param_len >= 8 &&
      (params[2] & 0x01) != 0)
    {
      aic_bt_put_le16(out, BT_HCI_OP_LE_SET_SCAN_PARAMS);
      out[2] = 7;
      out[3] = AIC_BT_LEGACY_SCAN_TYPE;
      aic_bt_put_le16(&out[4], AIC_BT_LEGACY_SCAN_INTERVAL);
      aic_bt_put_le16(&out[6], AIC_BT_LEGACY_SCAN_WINDOW);
      out[8] = aic_bt_hci_legacy_own_addr_type(params[0]);
      out[9] = params[1];
      *out_len = AIC_BT_CMD_HDR_SIZE + 7;
      *rsp_from = BT_HCI_OP_LE_SET_SCAN_PARAMS;
      *rsp_to = AIC_BT_OP_LE_SET_EXT_SCAN_PARAM;
      *scan_prepare = true;
      printf("AIC BT HCI: map LE_SET_EXT_SCAN_PARAM to legacy scan params "
             "type=%u->%u interval=0x%02x%02x->0x%02x%02x "
             "window=0x%02x%02x->0x%02x%02x own=%u->%u filter=%u\n",
             params[3], out[3], params[5], params[4], out[5], out[4],
             params[7], params[6], out[7], out[6], params[0], out[8],
             out[9]);
      return true;
    }

  if (opcode == AIC_BT_OP_LE_SET_EXT_SCAN_ENABLE && param_len >= 2)
    {
      aic_bt_put_le16(out, BT_HCI_OP_LE_SET_SCAN_ENABLE);
      out[2] = 2;
      out[3] = params[0];
      out[4] = params[1] == 0 ? 0 : 1;
      *out_len = AIC_BT_CMD_HDR_SIZE + 2;
      *rsp_from = BT_HCI_OP_LE_SET_SCAN_ENABLE;
      *rsp_to = AIC_BT_OP_LE_SET_EXT_SCAN_ENABLE;
      *scan_report_ext = params[0] != 0 ? 1 : -1;
      printf("AIC BT HCI: map LE_SET_EXT_SCAN_ENABLE to legacy scan enable "
             "report=%s\n", params[0] != 0 ? "ext" : "off");
      return true;
    }

  if (opcode == AIC_BT_OP_LE_EXT_CREATE_CONN &&
      param_len >= AIC_BT_LE_EXT_CREATE_CONN_FIXED_LEN +
                   AIC_BT_LE_EXT_CREATE_CONN_PHY_LEN)
    {
      FAR const uint8_t *phy = &params[AIC_BT_LE_EXT_CREATE_CONN_FIXED_LEN];

      aic_bt_put_le16(out, BT_HCI_OP_LE_CREATE_CONN);
      out[2] = AIC_BT_LE_CREATE_CONN_PARAM_LEN;
      memcpy(&out[3], phy, 4);
      out[7] = params[0];
      out[8] = params[2];
      memcpy(&out[9], &params[3], 6);
      out[15] = aic_bt_hci_legacy_own_addr_type(params[1]);
      memcpy(&out[16], &phy[4], 12);
      *out_len = AIC_BT_CMD_HDR_SIZE + AIC_BT_LE_CREATE_CONN_PARAM_LEN;
      *rsp_from = BT_HCI_OP_LE_CREATE_CONN;
      *rsp_to = AIC_BT_OP_LE_EXT_CREATE_CONN;
      *conn_prepare = true;
      printf("AIC BT HCI: map LE_EXT_CREATE_CONN to legacy create conn "
             "phys=0x%02x scan=0x%02x%02x/0x%02x%02x own=%u->%u "
             "peer_type=%u peer=",
             params[9], phy[1], phy[0], phy[3], phy[2], params[1], out[15],
             out[8]);
      aic_bt_hci_print_addr(&out[9]);
      printf(" interval=0x%02x%02x-0x%02x%02x timeout=0x%02x%02x\n",
             out[17], out[16], out[19], out[18], out[23], out[22]);
      return true;
    }

  return false;
}

static bool aic_bt_h4_type_valid(uint8_t type)
{
  return type == H4_EVT || type == H4_ACL || type == H4_ISO ||
         type == H4_SCO;
}

static bool aic_bt_hci_evt_code_valid(uint8_t evt)
{
  switch (evt)
    {
      case BT_HCI_EVT_DISCONN_COMPLETE:
      case BT_HCI_EVT_ENCRYPT_CHANGE:
      case BT_HCI_EVT_CMD_COMPLETE:
      case BT_HCI_EVT_CMD_STATUS:
      case BT_HCI_EVT_NUM_COMPLETED_PACKETS:
      case BT_HCI_EVT_HARDWARE_ERROR:
      case BT_HCI_EVT_LE_META_EVENT:
        return true;

      default:
        return false;
    }
}

static bool aic_bt_hci_le_subevent_valid(uint8_t subevent)
{
  switch (subevent)
    {
      case BT_HCI_EVT_LE_CONN_COMPLETE:
      case BT_HCI_EVT_LE_ADVERTISING_REPORT:
      case BT_HCI_EVT_LE_CONN_UPDATE_COMPLETE:
      case BT_HCI_EVT_LE_READ_REM_FEAT_COMPLETE:
      case BT_HCI_EVT_LE_CONN_PARAM_REQ:
      case BT_HCI_EVT_LE_DATA_LEN_CHANGE:
      case BT_HCI_EVT_LE_ENH_CONN_COMPLETE:
      case BT_HCI_EVT_LE_DIRECT_ADV_REPORT:
      case BT_HCI_EVT_LE_PHY_UPDATE_COMPLETE:
      case BT_HCI_EVT_LE_EXT_ADVERTISING_REPORT:
      case BT_HCI_EVT_LE_ADV_SET_TERMINATED:
      case BT_HCI_EVT_LE_SCAN_REQ_RECEIVED:
      case BT_HCI_EVT_LE_CHAN_SEL_ALGO:
        return true;

      default:
        return false;
    }
}

static int aic_bt_hci_evt_len_without_h4(FAR const uint8_t *buf,
                                         size_t len)
{
  size_t pktlen;
  uint8_t evt;
  uint8_t plen;

  if (len < sizeof(struct bt_hci_evt_hdr_s))
    {
      return 0;
    }

  evt = buf[0];
  plen = buf[1];
  if (!aic_bt_hci_evt_code_valid(evt))
    {
      return -EINVAL;
    }

  pktlen = sizeof(struct bt_hci_evt_hdr_s) + plen;
  if (pktlen + H4_HEADER_SIZE > AIC_BT_RX_BUF_SIZE)
    {
      return -E2BIG;
    }

  if (evt == BT_HCI_EVT_LE_META_EVENT && len >= pktlen && plen >= 1 &&
      !aic_bt_hci_le_subevent_valid(buf[2]))
    {
      return -EINVAL;
    }

  if (len < pktlen)
    {
      return 0;
    }

  return (int)pktlen;
}

static size_t aic_bt_h4_find_next(FAR const uint8_t *buf, size_t len)
{
  size_t i;

  for (i = 1; i < len; i++)
    {
      if (aic_bt_h4_type_valid(buf[i]))
        {
          return i;
        }
    }

  return len;
}

static void aic_bt_h4_drop_prefix(FAR struct aic_bt_hci_dev *dev,
                                  FAR uint8_t *buf, FAR size_t *rxlen,
                                  size_t drop, FAR const char *reason)
{
  if (drop == 0)
    {
      return;
    }

  dev->h4_resync_count++;
  if (dev->h4_resync_count <= 8 || (dev->h4_resync_count % 64) == 0)
    {
      printf("AIC BT HCI: H4 resync %s first=0x%02x drop=%u cached=%u "
             "count=%lu\n", reason, buf[0], (unsigned int)drop,
             (unsigned int)*rxlen, (unsigned long)dev->h4_resync_count);
    }

  if (drop >= *rxlen)
    {
      *rxlen = 0;
      return;
    }

  *rxlen -= drop;
  memmove(buf, buf + drop, *rxlen);
}

static bool aic_bt_h4_try_prepend_evt(FAR struct aic_bt_hci_dev *dev,
                                      FAR uint8_t *buf,
                                      FAR size_t *rxlen)
{
  int evtlen;

  evtlen = aic_bt_hci_evt_len_without_h4(buf, *rxlen);
  if (evtlen == 0)
    {
      return true;
    }

  if (evtlen < 0)
    {
      return false;
    }

  if (*rxlen >= AIC_BT_RX_BUF_SIZE)
    {
      return false;
    }

  memmove(buf + H4_HEADER_SIZE, buf, *rxlen);
  buf[0] = H4_EVT;
  *rxlen += H4_HEADER_SIZE;
  dev->h4_missing_evt_count++;
  if (dev->h4_missing_evt_count <= 8 ||
      (dev->h4_missing_evt_count % 64) == 0)
    {
      printf("AIC BT HCI: repaired missing H4 EVT prefix len=%d count=%lu\n",
             evtlen, (unsigned long)dev->h4_missing_evt_count);
    }

  return true;
}

static uint16_t aic_bt_hci_legacy_adv_evt_type(uint8_t evt_type)
{
  switch (evt_type)
    {
      case BT_LE_ADV_IND:
        return AIC_BT_EXT_ADV_EVT_CONN | AIC_BT_EXT_ADV_EVT_SCAN |
               AIC_BT_EXT_ADV_EVT_LEGACY;

      case BT_LE_ADV_DIRECT_IND:
        return AIC_BT_EXT_ADV_EVT_CONN | AIC_BT_EXT_ADV_EVT_DIRECT |
               AIC_BT_EXT_ADV_EVT_LEGACY;

      case BT_LE_ADV_SCAN_IND:
        return AIC_BT_EXT_ADV_EVT_SCAN | AIC_BT_EXT_ADV_EVT_LEGACY;

      case BT_LE_ADV_NONCONN_IND:
        return AIC_BT_EXT_ADV_EVT_LEGACY;

      case BT_LE_ADV_SCAN_RSP:
        return AIC_BT_EXT_ADV_EVT_SCAN_RSP | AIC_BT_EXT_ADV_EVT_SCAN |
               AIC_BT_EXT_ADV_EVT_LEGACY;

      default:
        return AIC_BT_EXT_ADV_EVT_LEGACY;
    }
}

static bool aic_bt_hci_deliver_legacy_adv_as_ext(
  FAR struct aic_bt_hci_dev *dev, FAR const uint8_t *packet, size_t len)
{
  FAR const uint8_t *payload;
  uint8_t converted[AIC_BT_EXT_ADV_FRAME_MAX];
  uint8_t num_reports;
  uint8_t plen;
  size_t pos;
  bool delivered = false;
  uint8_t i;

  if (!dev->legacy_adv_report_as_ext ||
      len < H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s) + 2 ||
      packet[0] != H4_EVT ||
      packet[H4_HEADER_SIZE] != BT_HCI_EVT_LE_META_EVENT)
    {
      return false;
    }

  plen = packet[H4_HEADER_SIZE + 1];
  payload = &packet[H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s)];
  if (payload[0] != BT_HCI_EVT_LE_ADVERTISING_REPORT || plen < 2)
    {
      return false;
    }

  num_reports = payload[1];
  pos = 2;
  for (i = 0; i < num_reports && pos + 10 <= plen; i++)
    {
      uint8_t evt_type = payload[pos];
      uint8_t addr_type = payload[pos + 1];
      FAR const uint8_t *addr = &payload[pos + 2];
      uint8_t data_len = payload[pos + 8];
      FAR const uint8_t *data = &payload[pos + 9];
      int8_t rssi;
      uint16_t ext_evt_type;
      size_t ext_plen;
      size_t out = 0;
      int recvret;

      if (pos + 10 + data_len > plen ||
          H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s) + 2 +
          AIC_BT_EXT_ADV_INFO_LEN + data_len > sizeof(converted))
        {
          printf("AIC BT HCI: skip legacy adv conversion, malformed len=%u "
                 "data_len=%u pos=%u\n", plen, data_len, (unsigned int)pos);
          return delivered;
        }

      rssi = (int8_t)payload[pos + 9 + data_len];
      ext_evt_type = aic_bt_hci_legacy_adv_evt_type(evt_type);
      ext_plen = 2 + AIC_BT_EXT_ADV_INFO_LEN + data_len;

      converted[out++] = H4_EVT;
      converted[out++] = BT_HCI_EVT_LE_META_EVENT;
      converted[out++] = (uint8_t)ext_plen;
      converted[out++] = BT_HCI_EVT_LE_EXT_ADVERTISING_REPORT;
      converted[out++] = 1;
      aic_bt_put_le16(&converted[out], ext_evt_type);
      out += 2;
      converted[out++] = addr_type;
      memcpy(&converted[out], addr, 6);
      out += 6;
      converted[out++] = AIC_BT_HCI_PHY_LE_1M;
      converted[out++] = AIC_BT_HCI_PHY_NONE;
      converted[out++] = AIC_BT_HCI_SID_INVALID;
      converted[out++] = AIC_BT_HCI_POWER_INVALID;
      converted[out++] = (uint8_t)rssi;
      aic_bt_put_le16(&converted[out], 0);
      out += 2;
      memset(&converted[out], 0, 7);
      out += 7;
      converted[out++] = data_len;
      memcpy(&converted[out], data, data_len);
      out += data_len;

      dev->converted_adv_reports++;
      if (dev->converted_adv_reports <= 8 ||
          (dev->converted_adv_reports % 64) == 0)
        {
          printf("AIC BT HCI: convert legacy adv report to ext count=%lu "
                 "evt=%u ext=0x%04x addr_type=%u addr=",
                 (unsigned long)dev->converted_adv_reports, evt_type,
                 ext_evt_type, addr_type);
          aic_bt_hci_print_addr(addr);
          printf(" rssi=%d data_len=%u\n", rssi, data_len);
        }

      aic_bt_hci_dump("RX", converted, out);
      aic_bt_hci_log_event(converted, out);
      recvret = bt_netdev_receive(&dev->btdev, BT_EVT,
                                  &converted[H4_HEADER_SIZE],
                                  out - H4_HEADER_SIZE);
      if (recvret < 0)
        {
          dev->converted_adv_deliver_failed++;
          if (dev->converted_adv_deliver_failed <= 8 ||
              (dev->converted_adv_deliver_failed % 64) == 0)
            {
              printf("AIC BT HCI: deliver converted adv failed ret=%d "
                     "count=%lu\n", recvret,
                     (unsigned long)dev->converted_adv_deliver_failed);
            }
        }
      else if (dev->converted_adv_reports <= 8 ||
               (dev->converted_adv_reports % 64) == 0)
        {
          printf("AIC BT HCI: deliver converted adv ok ret=%d count=%lu\n",
                 recvret, (unsigned long)dev->converted_adv_reports);
        }

      delivered = true;
      pos += 10 + data_len;
    }

  return delivered;
}

static int aic_bt_h4_packet_len(FAR const uint8_t *buf, size_t len)
{
  size_t hdrlen;
  size_t payload;

  if (len < H4_HEADER_SIZE)
    {
      return 0;
    }

  switch (buf[0])
    {
      case H4_EVT:
        if (len < H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s))
          {
            return 0;
          }

        hdrlen = H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s);
        payload = buf[H4_HEADER_SIZE + 1];
        break;

      case H4_ACL:
        if (len < H4_HEADER_SIZE + sizeof(struct bt_hci_acl_hdr_s))
          {
            return 0;
          }

        hdrlen = H4_HEADER_SIZE + sizeof(struct bt_hci_acl_hdr_s);
        payload = aic_bt_get_le16(&buf[H4_HEADER_SIZE + 2]);
        break;

      case H4_ISO:
        if (len < H4_HEADER_SIZE + sizeof(struct bt_hci_iso_hdr_s))
          {
            return 0;
          }

        hdrlen = H4_HEADER_SIZE + sizeof(struct bt_hci_iso_hdr_s);
        payload = aic_bt_get_le16(&buf[H4_HEADER_SIZE + 2]);
        break;

      case H4_SCO:
        printf("AIC BT HCI: SCO packet is not supported yet\n");
        return -ENOTSUP;

      default:
        printf("AIC BT HCI: unknown H4 packet type 0x%02x\n", buf[0]);
        return -EINVAL;
    }

  if (hdrlen + payload > AIC_BT_RX_BUF_SIZE)
    {
      printf("AIC BT HCI: oversize H4 packet %u\n",
             (unsigned int)(hdrlen + payload));
      return -E2BIG;
    }

  if (len < hdrlen + payload)
    {
      return 0;
    }

  return (int)(hdrlen + payload);
}

static void aic_bt_h4_deliver(FAR struct aic_bt_hci_dev *dev,
                              FAR uint8_t *packet, size_t len)
{
  enum bt_buf_type_e type;

  switch (packet[0])
    {
      case H4_EVT:
        type = BT_EVT;
        break;

      case H4_ACL:
        type = BT_ACL_IN;
        break;

      case H4_ISO:
        type = BT_ISO_IN;
        break;

      default:
        return;
    }

  if (aic_bt_hci_should_drop_rx(dev, packet, len))
    {
      return;
    }

  aic_bt_hci_rewrite_cmd_complete(dev, packet, len);
  if (aic_bt_hci_deliver_legacy_adv_as_ext(dev, packet, len))
    {
      return;
    }

  aic_bt_hci_dump("RX", packet, len);
  aic_bt_hci_log_event(packet, len);
  bt_netdev_receive(&dev->btdev, type, &packet[H4_HEADER_SIZE],
                    len - H4_HEADER_SIZE);
}

static void aic_bt_h4_consume(FAR struct aic_bt_hci_dev *dev,
                              FAR uint8_t *buf, FAR size_t *rxlen)
{
  while (*rxlen > 0)
    {
      int pktlen;

      if (!aic_bt_h4_type_valid(buf[0]))
        {
          if (aic_bt_h4_try_prepend_evt(dev, buf, rxlen))
            {
              if (!aic_bt_h4_type_valid(buf[0]))
                {
                  return;
                }
            }
          else
            {
              size_t drop = aic_bt_h4_find_next(buf, *rxlen);

              if (drop == 0)
                {
                  drop = 1;
                }

              aic_bt_h4_drop_prefix(dev, buf, rxlen, drop, "bad-type");
              continue;
            }
        }

      pktlen = aic_bt_h4_packet_len(buf, *rxlen);

      if (pktlen == 0)
        {
          return;
        }

      if (pktlen < 0)
        {
          size_t drop = aic_bt_h4_find_next(buf, *rxlen);

          if (drop == 0)
            {
              drop = 1;
            }

          aic_bt_h4_drop_prefix(dev, buf, rxlen, drop, "bad-packet");
          continue;
        }

      aic_bt_h4_deliver(dev, buf, (size_t)pktlen);
      *rxlen -= (size_t)pktlen;
      memmove(buf, buf + pktlen, *rxlen);
    }
}

static int aic_bt_hci_send_internal_cmd(FAR struct aic_bt_hci_dev *dev,
                                        int fd, FAR uint8_t *rxbuf,
                                        FAR size_t *rxlen,
                                        uint16_t opcode,
                                        FAR const uint8_t *params,
                                        uint8_t param_len)
{
  uint8_t cmd[H4_HEADER_SIZE + AIC_BT_CMD_HDR_SIZE + 8];
  unsigned int waited = 0;
  int ret;

  if (param_len > 8)
    {
      return -EINVAL;
    }

  cmd[0] = H4_CMD;
  aic_bt_put_le16(&cmd[H4_HEADER_SIZE], opcode);
  cmd[H4_HEADER_SIZE + 2] = param_len;
  if (param_len > 0)
    {
      memcpy(&cmd[H4_HEADER_SIZE + AIC_BT_CMD_HDR_SIZE],
             params, param_len);
    }

  printf("AIC BT HCI: internal TX CMD opcode=0x%04x %s len=%u\n",
         opcode, aic_bt_hci_opcode_name(opcode), param_len);

  dev->internal_rsp_opcode = opcode;
  dev->internal_rsp_status = 0xff;
  dev->internal_rsp_done = false;
  dev->internal_rsp_waiting = true;

  ret = aic_bt_uart_write_all(fd, cmd,
                              H4_HEADER_SIZE + AIC_BT_CMD_HDR_SIZE +
                              param_len);
  if (ret < 0)
    {
      dev->internal_rsp_waiting = false;
      return ret;
    }

  while (!dev->internal_rsp_done &&
         waited < AIC_BT_INTERNAL_CMD_TIMEOUT_MS)
    {
      struct pollfd pfd;

      ret = aic_bt_io_drain_rx(dev, fd, rxbuf, rxlen);
      if (ret < 0 || dev->internal_rsp_done)
        {
          break;
        }

      pfd.fd = fd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      poll(&pfd, 1, AIC_BT_IO_POLL_MS);
      waited += AIC_BT_IO_POLL_MS;
    }

  dev->internal_rsp_waiting = false;
  if (ret < 0)
    {
      return ret;
    }

  if (!dev->internal_rsp_done)
    {
      printf("AIC BT HCI: internal command timeout opcode=0x%04x %s\n",
             opcode, aic_bt_hci_opcode_name(opcode));
      return -ETIMEDOUT;
    }

  return dev->internal_rsp_status == 0 ? 0 : -EIO;
}

static int aic_bt_hci_prepare_legacy_scan(FAR struct aic_bt_hci_dev *dev,
                                          int fd, FAR uint8_t *rxbuf,
                                          FAR size_t *rxlen)
{
  int ret;

  printf("AIC BT HCI: prepare legacy scan with internal reset sequence\n");
  aic_bt_hci_reset_optional_events(dev);

  ret = aic_bt_hci_send_internal_cmd(dev, fd, rxbuf, rxlen,
                                     BT_HCI_OP_RESET, NULL, 0);
  if (ret < 0)
    {
      return ret;
    }

  ret = aic_bt_hci_send_internal_cmd(dev, fd, rxbuf, rxlen,
                                     BT_HCI_OP_SET_EVENT_MASK,
                                     g_aic_bt_scan_event_mask,
                                     sizeof(g_aic_bt_scan_event_mask));
  if (ret < 0)
    {
      return ret;
    }

  return aic_bt_hci_send_internal_cmd(dev, fd, rxbuf, rxlen,
                                      BT_HCI_OP_LE_SET_EVENT_MASK,
                                      g_aic_bt_le_scan_event_mask,
                                      sizeof(g_aic_bt_le_scan_event_mask));
}

static int aic_bt_hci_prepare_legacy_connect(FAR struct aic_bt_hci_dev *dev,
                                             int fd, FAR uint8_t *rxbuf,
                                             FAR size_t *rxlen)
{
  static const uint8_t le_host_support[2] =
    {
      0x01, 0x00
    };
  static const uint8_t scan_disable[2] =
    {
      BT_LE_SCAN_DISABLE, BT_LE_SCAN_FILTER_DUP_DISABLE
    };
  static const uint8_t adv_disable[1] =
    {
      0x00
    };
  int ret;

  printf("AIC BT HCI: prepare legacy create connection with internal reset\n");
  aic_bt_hci_reset_optional_events(dev);

  ret = aic_bt_hci_send_internal_cmd(dev, fd, rxbuf, rxlen,
                                     BT_HCI_OP_RESET, NULL, 0);
  if (ret < 0)
    {
      return ret;
    }

  ret = aic_bt_hci_send_internal_cmd(dev, fd, rxbuf, rxlen,
                                     BT_HCI_OP_SET_EVENT_MASK,
                                     g_aic_bt_event_mask,
                                     sizeof(g_aic_bt_event_mask));
  if (ret < 0)
    {
      return ret;
    }

  ret = aic_bt_hci_send_internal_cmd(dev, fd, rxbuf, rxlen,
                                     BT_HCI_OP_LE_WRITE_LE_HOST_SUPP,
                                     le_host_support,
                                     sizeof(le_host_support));
  if (ret < 0)
    {
      printf("AIC BT HCI: ignore LE host support before connect ret=%d\n",
             ret);
    }

  ret = aic_bt_hci_send_internal_cmd(dev, fd, rxbuf, rxlen,
                                     BT_HCI_OP_LE_SET_EVENT_MASK,
                                     g_aic_bt_le_event_mask,
                                     sizeof(g_aic_bt_le_event_mask));
  if (ret < 0)
    {
      return ret;
    }

  ret = aic_bt_hci_send_internal_cmd(dev, fd, rxbuf, rxlen,
                                     BT_HCI_OP_LE_SET_SCAN_ENABLE,
                                     scan_disable, sizeof(scan_disable));
  if (ret < 0)
    {
      printf("AIC BT HCI: ignore scan disable before connect ret=%d\n", ret);
    }

  ret = aic_bt_hci_send_internal_cmd(dev, fd, rxbuf, rxlen,
                                     BT_HCI_OP_LE_SET_ADV_ENABLE,
                                     adv_disable, sizeof(adv_disable));
  if (ret < 0)
    {
      printf("AIC BT HCI: ignore adv disable before connect ret=%d\n", ret);
    }

  return 0;
}

static void aic_bt_io_drain_tx(FAR struct aic_bt_hci_dev *dev, int fd,
                               FAR uint8_t *rxbuf, FAR size_t *rxlen)
{
  FAR struct aic_bt_hci_tx *tx;

  while ((tx = aic_bt_tx_pop(dev)) != NULL)
    {
      if (tx->scan_prepare)
        {
          int ret = aic_bt_hci_prepare_legacy_scan(dev, fd, rxbuf, rxlen);

          if (ret < 0)
            {
              printf("AIC BT HCI: prepare legacy scan failed: %d\n", ret);
            }
        }

      if (tx->conn_prepare)
        {
          int ret = aic_bt_hci_prepare_legacy_connect(dev, fd, rxbuf, rxlen);

          if (ret < 0)
            {
              printf("AIC BT HCI: prepare legacy connect failed: %d\n", ret);
            }
        }

      aic_bt_hci_dump("TX", tx->data, tx->len);
      aic_bt_hci_rsp_map_push(dev, tx->rsp_from, tx->rsp_to,
                              tx->scan_report_ext);
      aic_bt_uart_write_all(fd, tx->data, tx->len);
      kmm_free(tx);
    }
}

static void aic_bt_io_wait_rx_or_tx(FAR struct aic_bt_hci_dev *dev, int fd)
{
  struct pollfd pfd;
  int ret;

  if (aic_bt_tx_pending(dev))
    {
      return;
    }

  pfd.fd = fd;
  pfd.events = POLLIN;
  pfd.revents = 0;

  ret = poll(&pfd, 1, AIC_BT_IO_POLL_MS);
  if (ret < 0 && errno != EINTR)
    {
      usleep(1000);
    }
}

static int aic_bt_io_drain_rx(FAR struct aic_bt_hci_dev *dev, int fd,
                              FAR uint8_t *rxbuf, FAR size_t *rxlen)
{
  for (; ; )
    {
      ssize_t nread;

      if (*rxlen == AIC_BT_RX_BUF_SIZE)
        {
          printf("AIC BT HCI: rx buffer overflow, drop cached data\n");
          *rxlen = 0;
        }

      nread = read(fd, rxbuf + *rxlen, AIC_BT_RX_BUF_SIZE - *rxlen);
      if (nread < 0 && (errno == EAGAIN || errno == EINTR))
        {
          return 0;
        }

      if (nread < 0)
        {
          printf("AIC BT HCI: uart read failed: %d\n", errno);
          return -errno;
        }

      if (nread == 0)
        {
          return 0;
        }

      *rxlen += (size_t)nread;
      aic_bt_h4_consume(dev, rxbuf, rxlen);
    }
}

static int aic_bt_hci_io_thread(int argc, FAR char *argv[])
{
  FAR struct aic_bt_hci_dev *dev = &g_aic_bt_hci;
  uint8_t rxbuf[AIC_BT_RX_BUF_SIZE];
  size_t rxlen = 0;
  int fd;

  (void)argc;
  (void)argv;

  fd = aic_bt_uart_open();
  pthread_mutex_lock(&dev->lock);
  dev->io_error = fd < 0 ? fd : 0;
  dev->io_ready = fd >= 0;
  pthread_mutex_unlock(&dev->lock);
  nxsem_post(&dev->ready_sem);

  if (fd < 0)
    {
      goto out;
    }

  while (dev->io_running)
    {
      int ret;

      aic_bt_io_drain_tx(dev, fd, rxbuf, &rxlen);
      ret = aic_bt_io_drain_rx(dev, fd, rxbuf, &rxlen);
      if (ret < 0)
        {
          break;
        }

      aic_bt_io_wait_rx_or_tx(dev, fd);
    }

  close(fd);

out:
  aic_bt_tx_purge(dev);
  pthread_mutex_lock(&dev->lock);
  dev->io_running = false;
  dev->io_ready = false;
  dev->io_pid = 0;
  pthread_mutex_unlock(&dev->lock);
  nxsem_post(&dev->done_sem);
  return 0;
}

static int aic_bt_hci_start_io_locked(FAR struct aic_bt_hci_dev *dev)
{
  int pid;
  int ret;

  if (dev->io_running)
    {
      return 0;
    }

  dev->io_ready = false;
  dev->io_error = 0;
  dev->io_running = true;
  aic_bt_hci_rsp_map_reset(dev);
  aic_bt_sem_drain(&dev->ready_sem);
  aic_bt_sem_drain(&dev->done_sem);
  aic_bt_sem_drain(&dev->tx_sem);

  pid = kthread_create("aic_bt_hci", 100, 4096,
                       aic_bt_hci_io_thread, NULL);
  if (pid < 0)
    {
      dev->io_running = false;
      printf("AIC BT HCI: create io thread failed: %d\n", pid);
      return pid;
    }

  dev->io_pid = (pid_t)pid;
  pthread_mutex_unlock(&dev->lock);
  ret = aic_bt_sem_wait(&dev->ready_sem);
  pthread_mutex_lock(&dev->lock);

  if (ret < 0)
    {
      dev->io_running = false;
      return ret;
    }

  if (!dev->io_ready)
    {
      ret = dev->io_error < 0 ? dev->io_error : -EIO;
      pthread_mutex_unlock(&dev->lock);
      aic_bt_sem_wait(&dev->done_sem);
      pthread_mutex_lock(&dev->lock);
      return ret;
    }

  return 0;
}

static void aic_bt_hci_stop_io(FAR struct aic_bt_hci_dev *dev)
{
  bool wait_done = false;

  pthread_mutex_lock(&dev->lock);
  if (dev->io_running)
    {
      dev->io_running = false;
      aic_bt_sem_post_once(&dev->tx_sem);
      wait_done = true;
    }

  pthread_mutex_unlock(&dev->lock);

  if (wait_done)
    {
      aic_bt_sem_wait(&dev->done_sem);
    }

  pthread_mutex_lock(&dev->lock);
  dev->legacy_adv_report_as_ext = false;
  pthread_mutex_unlock(&dev->lock);
  aic_bt_tx_purge(dev);
}

static int aic_bt_hci_open(FAR struct bt_driver_s *btdev)
{
  FAR struct aic_bt_hci_dev *dev = &g_aic_bt_hci;
  int ret;

  (void)btdev;

  pthread_mutex_lock(&dev->lock);

  ret = aic_bt_controller_prepare();
  if (ret < 0)
    {
      goto out;
    }

  ret = aic_bt_hw_prepare_locked(dev);
  if (ret < 0)
    {
      goto out;
    }

  ret = aic_bt_hci_start_io_locked(dev);
  if (ret < 0)
    {
      goto out;
    }

  dev->opened = true;
  printf("AIC BT HCI: /dev/ttyHCI0 opened\n");
  ret = 0;

out:
  pthread_mutex_unlock(&dev->lock);
  return ret;
}

static int aic_bt_hci_send(FAR struct bt_driver_s *btdev,
                           enum bt_buf_type_e type,
                           FAR void *data, size_t len)
{
  FAR struct aic_bt_hci_dev *dev = &g_aic_bt_hci;
  FAR struct aic_bt_hci_tx *tx;
  FAR const uint8_t *tx_data = data;
  size_t tx_len = len;
  uint8_t rewrite[AIC_BT_CMD_HDR_SIZE + AIC_BT_REWRITE_PARAM_MAX];
  uint16_t rsp_from = 0;
  uint16_t rsp_to = 0;
  int8_t scan_report_ext = 0;
  bool scan_prepare = false;
  bool conn_prepare = false;
  uint8_t h4;

  (void)btdev;

  if (data == NULL || len == 0 || len > AIC_BT_TX_MAX_SIZE)
    {
      return -EINVAL;
    }

  switch (type)
    {
      case BT_CMD:
        h4 = H4_CMD;
        break;

      case BT_ACL_OUT:
        h4 = H4_ACL;
        break;

      case BT_ISO_OUT:
        h4 = H4_ISO;
        break;

      default:
        return -EINVAL;
    }

  pthread_mutex_lock(&dev->lock);
  if (!dev->io_running || !dev->io_ready || dev->stopping)
    {
      pthread_mutex_unlock(&dev->lock);
      return -ENODEV;
    }

  pthread_mutex_unlock(&dev->lock);

  if (type == BT_CMD)
    {
      aic_bt_hci_rewrite_le_cmd(data, len, rewrite, &tx_len,
                                &rsp_from, &rsp_to,
                                &scan_report_ext,
                                &scan_prepare,
                                &conn_prepare);
      if (rsp_from != 0)
        {
          tx_data = rewrite;
        }
    }

  tx = kmm_zalloc(sizeof(*tx) + H4_HEADER_SIZE + tx_len);
  if (tx == NULL)
    {
      return -ENOMEM;
    }

  tx->len = H4_HEADER_SIZE + tx_len;
  tx->rsp_from = rsp_from;
  tx->rsp_to = rsp_to;
  tx->scan_report_ext = scan_report_ext;
  tx->scan_prepare = scan_prepare;
  tx->conn_prepare = conn_prepare;
  tx->data[0] = h4;
  memcpy(&tx->data[H4_HEADER_SIZE], tx_data, tx_len);
  aic_bt_hci_log_tx(type, tx_data, tx_len);

  pthread_mutex_lock(&dev->tx_lock);
  if (dev->tx_tail == NULL)
    {
      dev->tx_head = tx;
      dev->tx_tail = tx;
    }
  else
    {
      dev->tx_tail->next = tx;
      dev->tx_tail = tx;
    }

  pthread_mutex_unlock(&dev->tx_lock);
  aic_bt_sem_post_once(&dev->tx_sem);
  return 0;
}

static void aic_bt_hci_close(FAR struct bt_driver_s *btdev)
{
  FAR struct aic_bt_hci_dev *dev = &g_aic_bt_hci;

  (void)btdev;

  aic_bt_hci_stop_io(dev);
  pthread_mutex_lock(&dev->lock);
  dev->opened = false;
  pthread_mutex_unlock(&dev->lock);
  printf("AIC BT HCI: /dev/ttyHCI0 closed\n");
}

static int aic_bt_hci_ioctl(FAR struct bt_driver_s *btdev, int cmd,
                            unsigned long arg)
{
  (void)btdev;
  (void)cmd;
  (void)arg;

  return -ENOTTY;
}

int aic_bt_vela_hci_start(void)
{
  FAR struct aic_bt_hci_dev *dev = &g_aic_bt_hci;
  int ret = 0;

  pthread_mutex_lock(&dev->lock);

  ret = aic_bt_controller_prepare();
  if (ret < 0)
    {
      goto out;
    }

  if (dev->registered)
    {
      if (!dev->powered)
        {
          ret = aic_bt_hw_prepare_locked(dev);
          if (ret < 0)
            {
              goto out;
            }
        }

      printf("AIC BT HCI: /dev/ttyHCI0 already registered\n");
      goto out;
    }

  ret = aic_bt_hw_prepare_locked(dev);
  if (ret < 0)
    {
      goto out;
    }

  ret = bt_driver_register(&dev->btdev);
  if (ret < 0)
    {
      printf("AIC BT HCI: register /dev/ttyHCI0 failed: %d\n", ret);
      goto out;
    }

  dev->registered = true;
  printf("AIC BT HCI: registered /dev/ttyHCI0\n");

out:
  pthread_mutex_unlock(&dev->lock);
  return ret;
}

int aic_bt_vela_hci_stop(void)
{
  FAR struct aic_bt_hci_dev *dev = &g_aic_bt_hci;

  pthread_mutex_lock(&dev->lock);
  dev->stopping = true;
  pthread_mutex_unlock(&dev->lock);

  aic_bt_hci_stop_io(dev);

  pthread_mutex_lock(&dev->lock);
#ifdef CONFIG_AIC_BT_BT8858A_VELA_HCI_POWER_CYCLE
  aic_bt_power_set(dev, false);
#endif
  dev->opened = false;
  dev->stopping = false;
  pthread_mutex_unlock(&dev->lock);

  printf("AIC BT HCI: stopped; /dev/ttyHCI0 remains registered%s\n",
#ifdef CONFIG_AIC_BT_BT8858A_VELA_HCI_POWER_CYCLE
         ", BT_ON off"
#else
         ", BT_ON kept on"
#endif
         );

#if defined(CONFIG_AIC_WLAN_AIC8800D40L) && defined(CONFIG_AIC8800_BT_SUPPORT)
  aic8800_bt_patch_release();
#endif

  return 0;
}

int aic_bt_vela_hci_is_registered(void)
{
  return g_aic_bt_hci.registered;
}
