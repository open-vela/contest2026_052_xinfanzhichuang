#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/wireless/bluetooth/bt_hci.h>
#include <nuttx/wireless/bluetooth/bt_uart.h>

#define BTHCI_DEV            "/dev/ttyHCI0"
#define BTHCI_MAX_PACKET     512
#define BTHCI_POLL_SLICE_MS  100
#define BTHCI_PARSE_LE_ADV   (1 << 0)
#define BTHCI_PARSE_INQUIRY  (1 << 1)
#define BTHCI_PARSE_LE_CONN  (1 << 2)
#define BTHCI_ADV_DATA_MAX   31
#define BTHCI_LOCAL_NAME_MAX 248
#define BTHCI_DEFAULT_NAME   "AIC8800-Vela"
#define BTHCI_ADDR_KEY_LEN   12
#define BTHCI_ADDR_KEY_MAX   (BTHCI_ADDR_KEY_LEN + 1)
#define BTHCI_GPIO_GROUP_SIZE 32
#define BTHCI_GPIO_GROUP(pin) ((pin) / BTHCI_GPIO_GROUP_SIZE)
#define BTHCI_GPIO_GROUP_PIN(pin) ((pin) % BTHCI_GPIO_GROUP_SIZE)
#define BTHCI_UART2_TX_GPIO  "PD.4"
#define BTHCI_UART2_RX_GPIO  "PD.5"
#define BTHCI_UART2_RTS_GPIO "PA.3"
#define BTHCI_UART2_CTS_GPIO "PA.2"
#define BTHCI_UART2_TXRX_FUNC 5
#define BTHCI_UART2_FLOW_FUNC 8
#define BTHCI_UART2_DRV 3
#define BTHCI_PIN_PULL_DIS 0
#define BTHCI_PIN_PULL_UP 3

#define BTHCI_OP_READ_SUPPORTED_CMDS BT_OP(BT_OGF_INFO, 0x0002)
#define BTHCI_OP_AIC_DBG_RD_MEM     0xfc01
#define BTHCI_OP_AIC_FW_STATUS_GET  0xfc78
#define BTHCI_AIC_DBG_MEM_TYPE_U32  32
#define BTHCI_AIC_DBG_MEM_LEN_U32   4
#define BTHCI_AIC_PROBE_RSP_MAX     260

#ifdef CONFIG_AIC_BT_CMD_CLASSIC_DIAG
#  ifndef BT_HCI_OP_INQUIRY
#    define BT_HCI_OP_INQUIRY BT_OP(BT_OGF_LINK_CTRL, 0x0001)
#  endif

#  ifndef BT_HCI_OP_INQUIRY_CANCEL
#    define BT_HCI_OP_INQUIRY_CANCEL BT_OP(BT_OGF_LINK_CTRL, 0x0002)
#  endif

#  define BTHCI_OP_WRITE_LOCAL_NAME   BT_OP(BT_OGF_BASEBAND, 0x0013)
#  define BTHCI_OP_WRITE_SCAN_ENABLE  BT_OP(BT_OGF_BASEBAND, 0x001a)
#  define BTHCI_OP_WRITE_CLASS_OF_DEV BT_OP(BT_OGF_BASEBAND, 0x0024)
#endif

#ifdef CONFIG_AIC_BT_UART_PORT
#  define BTHCI_UART_DEV CONFIG_AIC_BT_UART_PORT
#else
#  define BTHCI_UART_DEV "/dev/ttyS2"
#endif

static const char *g_bthci_scan_filter;
static unsigned int g_bthci_scan_reports;
static unsigned int g_bthci_scan_matches;
static bool g_bthci_conn_connected;
static uint16_t g_bthci_conn_handle;

static bool bthci_addr_key_from_text(const char *text, char *key,
                                     size_t key_size);
static int bthci_parse_hex_byte(const char *s, uint8_t *value);
#ifdef CONFIG_AIC_BT_CMD_CLASSIC_DIAG
static void bthci_fill_local_name(uint8_t *local_name, const char *name);
#endif

int hal_gpio_name2pin(const char *name);
int hal_gpio_set_func(unsigned int group, unsigned int pin,
                      unsigned int func);
int hal_gpio_set_drive_strength(unsigned int group, unsigned int pin,
                                unsigned int strength);
int hal_gpio_set_bias_pull(unsigned int group, unsigned int pin,
                           unsigned int pull);

#if defined(CONFIG_AIC_WLAN_AIC8800D40L) && defined(CONFIG_AIC8800_BT_SUPPORT)
int aic8800_bt_patch_prepare(void);
void aic8800_bt_patch_release(void);
#endif

static uint16_t bthci_get_le16(const uint8_t *p)
{
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void bthci_put_le16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

static uint32_t bthci_get_le32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void bthci_put_le32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static uint64_t bthci_now_ms(void)
{
  struct timeval tv;

#ifdef CLOCK_MONOTONIC
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
    {
      return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
    }
#endif

  if (gettimeofday(&tv, NULL) == 0)
    {
      return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
    }

  return 0;
}

static void bthci_dump(const char *tag, const uint8_t *buf, size_t len)
{
  size_t i;

  printf("%s len=%u:", tag, (unsigned int)len);
  for (i = 0; i < len; i++)
    {
      printf(" %02x", buf[i]);
    }

  printf("\n");
}

static void bthci_print_hex_field(const char *tag, const uint8_t *buf,
                                  size_t len)
{
  size_t i;

  printf("%s:", tag);
  for (i = 0; i < len; i++)
    {
      printf(" %02x", buf[i]);
    }

  printf("\n");
}

static void bthci_print_lmp_feature_summary(const uint8_t *features)
{
  bool no_bredr = (features[4] & BT_LMP_NO_BREDR) != 0;
  bool le = (features[4] & BT_LMP_LE) != 0;

  printf("Feature summary: BR/EDR=%s%s LE=%s%s\n",
         no_bredr ? "not-supported" : "not-blocked",
         no_bredr ? " (BT_LMP_NO_BREDR set)" : "",
         le ? "supported" : "not-reported",
         le ? " (BT_LMP_LE set)" : "");

  if (no_bredr)
    {
      printf("Feature summary: Classic BR/EDR profiles are unavailable; "
             "use BLE GAP/GATT commands on this board.\n");
    }
}

static void bthci_print_addr(const uint8_t *addr)
{
  printf("%02x:%02x:%02x:%02x:%02x:%02x",
         addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

static void bthci_addr_to_str(const uint8_t *addr, char *str, size_t len)
{
  snprintf(str, len, "%02x:%02x:%02x:%02x:%02x:%02x",
           addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

static int bthci_parse_addr(const char *text, uint8_t *addr)
{
  char key[BTHCI_ADDR_KEY_MAX];
  size_t i;

  if (!bthci_addr_key_from_text(text, key, sizeof(key)) ||
      strlen(key) != BTHCI_ADDR_KEY_LEN)
    {
      return -EINVAL;
    }

  for (i = 0; i < 6; i++)
    {
      char byte_text[3];
      uint8_t value;

      byte_text[0] = key[BTHCI_ADDR_KEY_LEN - 2 - i * 2];
      byte_text[1] = key[BTHCI_ADDR_KEY_LEN - 1 - i * 2];
      byte_text[2] = '\0';
      if (bthci_parse_hex_byte(byte_text, &value) < 0)
        {
          return -EINVAL;
        }

      addr[i] = value;
    }

  return 0;
}

static int bthci_parse_addr_type(const char *text, uint8_t *type)
{
  if (strcmp(text, "public") == 0 || strcmp(text, "0") == 0)
    {
      *type = 0;
      return 0;
    }

  if (strcmp(text, "random") == 0 || strcmp(text, "1") == 0)
    {
      *type = 1;
      return 0;
    }

  printf("bthci: invalid addr type %s, use public|random|0|1\n", text);
  return -EINVAL;
}

static char bthci_ascii_tolower(char c)
{
  if (c >= 'A' && c <= 'Z')
    {
      return c - 'A' + 'a';
    }

  return c;
}

static bool bthci_ascii_contains_nocase(const char *haystack,
                                        const char *needle)
{
  size_t haystack_len;
  size_t needle_len;
  size_t i;
  size_t j;

  if (haystack == NULL || needle == NULL || needle[0] == '\0')
    {
      return false;
    }

  haystack_len = strlen(haystack);
  needle_len = strlen(needle);
  if (needle_len > haystack_len)
    {
      return false;
    }

  for (i = 0; i + needle_len <= haystack_len; i++)
    {
      for (j = 0; j < needle_len; j++)
        {
          if (bthci_ascii_tolower(haystack[i + j]) !=
              bthci_ascii_tolower(needle[j]))
            {
              break;
            }
        }

      if (j == needle_len)
        {
          return true;
        }
    }

  return false;
}

static int bthci_hex_value(char c)
{
  c = bthci_ascii_tolower(c);
  if (c >= '0' && c <= '9')
    {
      return c - '0';
    }

  if (c >= 'a' && c <= 'f')
    {
      return c - 'a' + 10;
    }

  return -1;
}

static bool bthci_addr_key_from_text(const char *text, char *key,
                                     size_t key_size)
{
  size_t out = 0;
  size_t i;

  if (text == NULL || text[0] == '\0' || key_size < BTHCI_ADDR_KEY_MAX)
    {
      return false;
    }

  for (i = 0; text[i] != '\0'; i++)
    {
      char c = text[i];

      if (c == ':' || c == '-' || c == '.' || c == '_')
        {
          continue;
        }

      if (bthci_hex_value(c) < 0)
        {
          return false;
        }

      if (out >= BTHCI_ADDR_KEY_LEN)
        {
          return false;
        }

      key[out++] = bthci_ascii_tolower(c);
    }

  key[out] = '\0';
  return out >= 4;
}

static void bthci_addr_key_reverse(const char *src, char *dst,
                                   size_t dst_size)
{
  size_t i;

  if (dst_size < BTHCI_ADDR_KEY_MAX || strlen(src) != BTHCI_ADDR_KEY_LEN)
    {
      dst[0] = '\0';
      return;
    }

  for (i = 0; i < BTHCI_ADDR_KEY_LEN / 2; i++)
    {
      dst[i * 2] = src[BTHCI_ADDR_KEY_LEN - 2 - i * 2];
      dst[i * 2 + 1] = src[BTHCI_ADDR_KEY_LEN - 1 - i * 2];
    }

  dst[BTHCI_ADDR_KEY_LEN] = '\0';
}

static bool bthci_addr_filter_match(const char *filter, const char *addr)
{
  char filter_key[BTHCI_ADDR_KEY_MAX];
  char addr_key[BTHCI_ADDR_KEY_MAX];
  char addr_key_rev[BTHCI_ADDR_KEY_MAX];

  if (!bthci_addr_key_from_text(filter, filter_key, sizeof(filter_key)) ||
      !bthci_addr_key_from_text(addr, addr_key, sizeof(addr_key)))
    {
      return false;
    }

  if (strstr(addr_key, filter_key) != NULL)
    {
      return true;
    }

  bthci_addr_key_reverse(addr_key, addr_key_rev, sizeof(addr_key_rev));
  return addr_key_rev[0] != '\0' &&
         strstr(addr_key_rev, filter_key) != NULL;
}

static bool bthci_payload_contains(const uint8_t *data, uint8_t len,
                                   const char *needle)
{
  size_t needle_len;
  uint8_t i;
  size_t j;

  if (needle == NULL || needle[0] == '\0')
    {
      return false;
    }

  needle_len = strlen(needle);
  if (needle_len == 0 || needle_len > len)
    {
      return false;
    }

  for (i = 0; i + needle_len <= len; i++)
    {
      for (j = 0; j < needle_len; j++)
        {
          if (bthci_ascii_tolower((char)data[i + j]) !=
              bthci_ascii_tolower(needle[j]))
            {
              break;
            }
        }

      if (j == needle_len)
        {
          return true;
        }
    }

  return false;
}

static bool bthci_scan_filter_match(const char *filter, const char *addr,
                                    const uint8_t *data, uint8_t len)
{
  if (filter == NULL)
    {
      return true;
    }

  return bthci_ascii_contains_nocase(addr, filter) ||
         bthci_addr_filter_match(filter, addr) ||
         bthci_payload_contains(data, len, filter);
}

static void bthci_print_name(const uint8_t *data, uint8_t len)
{
  uint8_t pos = 0;

  while (pos < len)
    {
      uint8_t field_len = data[pos];
      uint8_t type;
      uint8_t text_len;
      uint8_t i;

      if (field_len == 0 || pos + field_len >= len)
        {
          break;
        }

      type = data[pos + 1];
      text_len = field_len - 1;
      if (type == BT_EIR_NAME_COMPLETE || type == 0x08)
        {
          printf(" name=\"");
          for (i = 0; i < text_len; i++)
            {
              uint8_t ch = data[pos + 2 + i];
              putchar(ch >= 32 && ch <= 126 ? ch : '.');
            }

          printf("\"");
          return;
        }

      pos += field_len + 1;
    }
}

static void bthci_parse_le_adv_report(const uint8_t *payload, size_t len)
{
  uint8_t num_reports;
  size_t pos = 2;
  uint8_t i;

  if (len < 2 || payload[0] != BT_HCI_EVT_LE_ADVERTISING_REPORT)
    {
      return;
    }

  num_reports = payload[1];
  for (i = 0; i < num_reports && pos + 10 <= len; i++)
    {
      uint8_t evt_type = payload[pos];
      uint8_t addr_type = payload[pos + 1];
      const uint8_t *addr = &payload[pos + 2];
      uint8_t data_len = payload[pos + 8];
      const uint8_t *data = &payload[pos + 9];
      int8_t rssi;
      char addr_str[18];
      bool match;

      if (pos + 10 + data_len > len)
        {
          printf("LE adv report truncated\n");
          return;
        }

      rssi = (int8_t)payload[pos + 9 + data_len];
      bthci_addr_to_str(addr, addr_str, sizeof(addr_str));
      g_bthci_scan_reports++;
      match = bthci_scan_filter_match(g_bthci_scan_filter, addr_str, data,
                                      data_len);
      if (!match)
        {
          pos += 10 + data_len;
          continue;
        }

      g_bthci_scan_matches++;
      printf("LE adv type=%u addr_type=%u addr=", evt_type, addr_type);
      bthci_print_addr(addr);
      printf(" rssi=%d", rssi);
      if (g_bthci_scan_filter != NULL)
        {
          printf(" match=1");
        }

      bthci_print_name(data, data_len);
      printf("\n");
      pos += 10 + data_len;
    }
}

static void bthci_parse_le_conn_event(const uint8_t *payload, size_t len)
{
  uint8_t subevent;
  uint8_t status;
  uint16_t handle;

  if (len < 2)
    {
      return;
    }

  subevent = payload[0];
  status = payload[1];

  if (subevent == BT_HCI_EVT_LE_CONN_COMPLETE && len >= 19)
    {
      handle = bthci_get_le16(&payload[2]);
      printf("LE conn complete status=0x%02x handle=0x%04x role=%u "
             "peer_type=%u peer=",
             status, handle, payload[4], payload[5]);
      bthci_print_addr(&payload[6]);
      printf(" interval=0x%04x latency=0x%04x timeout=0x%04x\n",
             bthci_get_le16(&payload[12]), bthci_get_le16(&payload[14]),
             bthci_get_le16(&payload[16]));
      if (status == 0)
        {
          g_bthci_conn_connected = true;
          g_bthci_conn_handle = handle;
        }
    }
  else if (subevent == BT_HCI_EVT_LE_ENH_CONN_COMPLETE && len >= 31)
    {
      handle = bthci_get_le16(&payload[2]);
      printf("LE enhanced conn complete status=0x%02x handle=0x%04x role=%u "
             "peer_type=%u peer=",
             status, handle, payload[4], payload[5]);
      bthci_print_addr(&payload[6]);
      printf(" interval=0x%04x latency=0x%04x timeout=0x%04x\n",
             bthci_get_le16(&payload[24]), bthci_get_le16(&payload[26]),
             bthci_get_le16(&payload[28]));
      if (status == 0)
        {
          g_bthci_conn_connected = true;
          g_bthci_conn_handle = handle;
        }
    }
}

#ifdef CONFIG_AIC_BT_CMD_CLASSIC_DIAG
static void bthci_parse_inquiry_report(uint8_t evt, const uint8_t *payload,
                                       size_t len)
{
  uint8_t num_reports;
  size_t item_len;
  size_t pos = 1;
  uint8_t i;

  if (evt == BT_HCI_EVT_INQUIRY_COMPLETE)
    {
      printf("Inquiry complete status=0x%02x\n", len > 0 ? payload[0] : 0xff);
      return;
    }

  if (len < 1)
    {
      return;
    }

  if (evt == BT_HCI_EVT_INQUIRY_RESULT)
    {
      item_len = 13;
    }
  else if (evt == BT_HCI_EVT_INQUIRY_RESULT_WITH_RSSI)
    {
      item_len = 14;
    }
  else if (evt == BT_HCI_EVT_EXTENDED_INQUIRY_RESULT)
    {
      item_len = 254;
    }
  else
    {
      return;
    }

  num_reports = payload[0];
  for (i = 0; i < num_reports; i++)
    {
      const uint8_t *addr;
      const uint8_t *cod;

      if (pos + item_len > len)
        {
          printf("Inquiry report truncated\n");
          return;
        }

      addr = &payload[pos];
      cod = &payload[pos + 8];

      printf("Inquiry addr=");
      bthci_print_addr(addr);
      printf(" class=%02x%02x%02x", cod[2], cod[1], cod[0]);

      if (evt == BT_HCI_EVT_INQUIRY_RESULT_WITH_RSSI ||
          evt == BT_HCI_EVT_EXTENDED_INQUIRY_RESULT)
        {
          printf(" rssi=%d", (int8_t)payload[pos + 13]);
        }

      if (evt == BT_HCI_EVT_EXTENDED_INQUIRY_RESULT)
        {
          bthci_print_name(&payload[pos + 14], 240);
        }

      printf("\n");
      pos += item_len;
    }
}
#endif

static void bthci_parse_cmd_complete(uint16_t opcode,
                                     const uint8_t *params,
                                     size_t len)
{
  uint8_t status = len > 0 ? params[0] : 0xff;

  printf("Command Complete opcode=0x%04x status=0x%02x\n",
         opcode, status);

  if (status != 0)
    {
      return;
    }

  if (opcode == BT_HCI_OP_READ_LOCAL_VERSION_INFO && len >= 9)
    {
      printf("Local version: hci=0x%02x hci_rev=0x%04x lmp=0x%02x "
             "manufacturer=0x%04x lmp_subver=0x%04x\n",
             params[1], bthci_get_le16(&params[2]), params[4],
             bthci_get_le16(&params[5]), bthci_get_le16(&params[7]));
    }
  else if (opcode == BT_HCI_OP_READ_BD_ADDR && len >= 7)
    {
      printf("BD_ADDR: ");
      bthci_print_addr(&params[1]);
      printf("\n");
    }
  else if (opcode == BT_HCI_OP_READ_LOCAL_FEATURES && len >= 9)
    {
      bthci_print_hex_field("Local features", &params[1], 8);
      bthci_print_lmp_feature_summary(&params[1]);
    }
  else if (opcode == BTHCI_OP_READ_SUPPORTED_CMDS && len >= 65)
    {
      bthci_print_hex_field("Supported commands", &params[1], 64);
    }
  else if (opcode == BT_HCI_OP_LE_READ_LOCAL_FEATURES && len >= 9)
    {
      bthci_print_hex_field("LE features", &params[1], 8);
    }
  else if (opcode == BT_HCI_OP_READ_BUFFER_SIZE && len >= 8)
    {
      printf("Buffer size: acl_len=%u sco_len=%u acl_num=%u sco_num=%u\n",
             bthci_get_le16(&params[1]), params[3],
             bthci_get_le16(&params[4]), bthci_get_le16(&params[6]));
    }
  else if (opcode == BT_HCI_OP_LE_READ_BUFFER_SIZE && len >= 4)
    {
      printf("LE buffer size: le_len=%u le_num=%u\n",
             bthci_get_le16(&params[1]), params[3]);
    }
}

static int bthci_packet_len(const uint8_t *buf, size_t len)
{
  if (len < 1)
    {
      return 0;
    }

  switch (buf[0])
    {
      case H4_EVT:
        if (len < 3)
          {
            return 0;
          }

        return len >= (size_t)(3 + buf[2]) ? 3 + buf[2] : 0;

      case H4_ACL:
        if (len < 5)
          {
            return 0;
          }

        return len >= (size_t)(5 + bthci_get_le16(&buf[3])) ?
               5 + bthci_get_le16(&buf[3]) : 0;

      case H4_ISO:
        if (len < 5)
          {
            return 0;
          }

        return len >= (size_t)(5 + bthci_get_le16(&buf[3])) ?
               5 + bthci_get_le16(&buf[3]) : 0;

      default:
        printf("Unknown H4 packet type 0x%02x, drop one byte\n", buf[0]);
        return -EINVAL;
    }
}

static int bthci_read_events(int fd, unsigned int timeout_ms,
                             uint16_t wait_opcode, bool stop_on_opcode,
                             unsigned int parse_flags)
{
  uint8_t buf[BTHCI_MAX_PACKET];
  uint64_t start_ms = bthci_now_ms();
  size_t used = 0;
  int matched_status = -ETIMEDOUT;

  for (; ; )
    {
      struct pollfd pfd;
      uint64_t now_ms = bthci_now_ms();
      unsigned int elapsed = now_ms >= start_ms ?
                             (unsigned int)(now_ms - start_ms) : 0;
      int slice;
      bool got_data = false;
      int ret;

      if (elapsed >= timeout_ms)
        {
          break;
        }

      slice = timeout_ms - elapsed;
      for (; ; )
        {
          ssize_t nread;

          if (used == sizeof(buf))
            {
              printf("bthci: RX buffer full, drop cached data\n");
              used = 0;
            }

          nread = read(fd, &buf[used], sizeof(buf) - used);
          if (nread < 0 && errno == EINTR)
            {
              continue;
            }
          else if (nread < 0 && errno == EAGAIN)
            {
              break;
            }
          else if (nread < 0)
            {
              printf("bthci: read failed errno=%d\n", errno);
              return -errno;
            }
          else if (nread == 0)
            {
              break;
            }

          used += (size_t)nread;
          got_data = true;
        }

      while (used > 0)
        {
          int pktlen = bthci_packet_len(buf, used);
          uint8_t evt;
          uint8_t plen;
          const uint8_t *payload;

          if (pktlen == 0)
            {
              break;
            }

          if (pktlen < 0)
            {
              memmove(buf, buf + 1, --used);
              continue;
            }

          if (buf[0] == H4_EVT && pktlen >= 3)
            {
              evt = buf[1];
              plen = buf[2];
              payload = &buf[3];

              if (evt != BT_HCI_EVT_LE_META_EVENT ||
                  (parse_flags & BTHCI_PARSE_LE_ADV) == 0)
                {
                  bthci_dump("RX", buf, (size_t)pktlen);
                }

              if (evt == BT_HCI_EVT_CMD_COMPLETE && plen >= 3)
                {
                  uint16_t opcode = bthci_get_le16(&payload[1]);
                  const uint8_t *params = &payload[3];
                  size_t params_len = plen - 3;

                  bthci_parse_cmd_complete(opcode, params, params_len);
                  if (wait_opcode == opcode)
                    {
                      matched_status = params_len > 0 ? -params[0] : 0;
                      if (stop_on_opcode)
                        {
                          return matched_status;
                        }
                    }
                }
              else if (evt == BT_HCI_EVT_CMD_STATUS && plen >= 4)
                {
                  uint16_t opcode = bthci_get_le16(&payload[2]);

                  printf("Command Status opcode=0x%04x status=0x%02x\n",
                         opcode, payload[0]);
                  if (wait_opcode == opcode)
                    {
                      matched_status = -payload[0];
                      if (stop_on_opcode)
                        {
                          return matched_status;
                        }
                    }
                }
	              else if (evt == BT_HCI_EVT_LE_META_EVENT)
	                {
	                  if ((parse_flags & BTHCI_PARSE_LE_CONN) != 0)
	                    {
	                      bthci_parse_le_conn_event(payload, plen);
	                    }

	                  if ((parse_flags & BTHCI_PARSE_LE_ADV) != 0)
	                    {
	                      bthci_parse_le_adv_report(payload, plen);
	                    }
	                }
#ifdef CONFIG_AIC_BT_CMD_CLASSIC_DIAG
              else if ((parse_flags & BTHCI_PARSE_INQUIRY) != 0 &&
                       (evt == BT_HCI_EVT_INQUIRY_COMPLETE ||
                        evt == BT_HCI_EVT_INQUIRY_RESULT ||
                        evt == BT_HCI_EVT_INQUIRY_RESULT_WITH_RSSI ||
                        evt == BT_HCI_EVT_EXTENDED_INQUIRY_RESULT))
                {
                  bthci_parse_inquiry_report(evt, payload, plen);
                }
#endif
              else
                {
                  printf("Event 0x%02x len=%u\n", evt, plen);
                }
            }
          else
            {
              bthci_dump("RX", buf, (size_t)pktlen);
            }

          used -= (size_t)pktlen;
          memmove(buf, buf + pktlen, used);
        }

      if (got_data)
        {
          continue;
        }

      if (slice > BTHCI_POLL_SLICE_MS)
        {
          slice = BTHCI_POLL_SLICE_MS;
        }

      pfd.fd = fd;
      pfd.events = POLLIN;
      pfd.revents = 0;

      ret = poll(&pfd, 1, slice);
      if (ret < 0 && errno == EINTR)
        {
          continue;
        }
      else if (ret < 0)
        {
          printf("bthci: poll failed errno=%d\n", errno);
          return -errno;
        }
    }

  return matched_status;
}

static int bthci_read_cmd_capture(int fd, unsigned int timeout_ms,
                                  uint16_t wait_opcode, uint8_t *rsp,
                                  size_t *rsp_len, size_t rsp_cap)
{
  uint8_t buf[BTHCI_MAX_PACKET];
  uint64_t start_ms = bthci_now_ms();
  size_t used = 0;

  if (rsp_len != NULL)
    {
      *rsp_len = 0;
    }

  for (; ; )
    {
      struct pollfd pfd;
      uint64_t now_ms = bthci_now_ms();
      unsigned int elapsed = now_ms >= start_ms ?
                             (unsigned int)(now_ms - start_ms) : 0;
      int slice;
      bool got_data = false;
      int ret;

      if (elapsed >= timeout_ms)
        {
          break;
        }

      slice = timeout_ms - elapsed;
      for (; ; )
        {
          ssize_t nread;

          if (used == sizeof(buf))
            {
              printf("bthci: RX buffer full, drop cached data\n");
              used = 0;
            }

          nread = read(fd, &buf[used], sizeof(buf) - used);
          if (nread < 0 && errno == EINTR)
            {
              continue;
            }
          else if (nread < 0 && errno == EAGAIN)
            {
              break;
            }
          else if (nread < 0)
            {
              printf("bthci: read failed errno=%d\n", errno);
              return -errno;
            }
          else if (nread == 0)
            {
              break;
            }

          used += (size_t)nread;
          got_data = true;
        }

      while (used > 0)
        {
          int pktlen = bthci_packet_len(buf, used);
          uint8_t evt;
          uint8_t plen;
          const uint8_t *payload;

          if (pktlen == 0)
            {
              break;
            }

          if (pktlen < 0)
            {
              memmove(buf, buf + 1, --used);
              continue;
            }

          if (buf[0] == H4_EVT && pktlen >= 3)
            {
              evt = buf[1];
              plen = buf[2];
              payload = &buf[3];
              bthci_dump("RX", buf, (size_t)pktlen);

              if (evt == BT_HCI_EVT_CMD_COMPLETE && plen >= 3)
                {
                  uint16_t opcode = bthci_get_le16(&payload[1]);
                  const uint8_t *params = &payload[3];
                  size_t params_len = plen - 3;

                  bthci_parse_cmd_complete(opcode, params, params_len);
                  if (wait_opcode == opcode)
                    {
                      if (rsp != NULL && rsp_len != NULL)
                        {
                          size_t copy_len = params_len;

                          if (copy_len > rsp_cap)
                            {
                              copy_len = rsp_cap;
                              printf("bthci: response truncated %u -> %u\n",
                                     (unsigned int)params_len,
                                     (unsigned int)copy_len);
                            }

                          memcpy(rsp, params, copy_len);
                          *rsp_len = copy_len;
                        }

                      return params_len > 0 ? -params[0] : 0;
                    }
                }
              else if (evt == BT_HCI_EVT_CMD_STATUS && plen >= 4)
                {
                  uint16_t opcode = bthci_get_le16(&payload[2]);

                  printf("Command Status opcode=0x%04x status=0x%02x\n",
                         opcode, payload[0]);
                  if (wait_opcode == opcode)
                    {
                      if (rsp != NULL && rsp_len != NULL && rsp_cap > 0)
                        {
                          rsp[0] = payload[0];
                          *rsp_len = 1;
                        }

                      return -payload[0];
                    }
                }
              else
                {
                  printf("Event 0x%02x len=%u\n", evt, plen);
                }
            }
          else
            {
              bthci_dump("RX", buf, (size_t)pktlen);
            }

          used -= (size_t)pktlen;
          memmove(buf, buf + pktlen, used);
        }

      if (got_data)
        {
          continue;
        }

      if (slice > BTHCI_POLL_SLICE_MS)
        {
          slice = BTHCI_POLL_SLICE_MS;
        }

      pfd.fd = fd;
      pfd.events = POLLIN;
      pfd.revents = 0;

      ret = poll(&pfd, 1, slice);
      if (ret < 0 && errno == EINTR)
        {
          continue;
        }
      else if (ret < 0)
        {
          printf("bthci: poll failed errno=%d\n", errno);
          return -errno;
        }
    }

  return -ETIMEDOUT;
}

static int bthci_write_all(int fd, const uint8_t *buf, size_t len)
{
  size_t done = 0;

  while (done < len)
    {
      ssize_t ret = write(fd, &buf[done], len - done);

      if (ret > 0)
        {
          done += (size_t)ret;
        }
      else if (ret < 0 && (errno == EAGAIN || errno == EINTR))
        {
          struct pollfd pfd;

          pfd.fd = fd;
          pfd.events = POLLOUT;
          pfd.revents = 0;
          poll(&pfd, 1, BTHCI_POLL_SLICE_MS);
        }
      else if (ret == 0)
        {
          usleep(1000);
        }
      else
        {
          printf("bthci: write failed errno=%d\n", errno);
          return -errno;
        }
    }

  return 0;
}

static int bthci_send_cmd(int fd, uint16_t opcode,
                          const uint8_t *params, uint8_t params_len,
                          unsigned int wait_ms)
{
  uint8_t packet[BTHCI_MAX_PACKET];
  size_t len = H4_HEADER_SIZE + sizeof(struct bt_hci_cmd_hdr_s) + params_len;
  int ret;

  if (len > sizeof(packet))
    {
      return -E2BIG;
    }

  packet[0] = H4_CMD;
  bthci_put_le16(&packet[1], opcode);
  packet[3] = params_len;
  if (params_len > 0)
    {
      memcpy(&packet[4], params, params_len);
    }

  bthci_dump("TX", packet, len);
  ret = bthci_write_all(fd, packet, len);
  if (ret < 0)
    {
      return ret;
    }

  ret = bthci_read_events(fd, wait_ms, opcode, true, 0);
  if (ret == -ETIMEDOUT)
    {
      printf("bthci: opcode 0x%04x timed out after %u ms\n",
             opcode, wait_ms);
    }
  else if (ret < 0)
    {
      printf("bthci: opcode 0x%04x returned error %d\n", opcode, ret);
    }

  return ret;
}

static int bthci_send_cmd_capture(int fd, uint16_t opcode,
                                  const uint8_t *params, uint8_t params_len,
                                  unsigned int wait_ms, uint8_t *rsp,
                                  size_t *rsp_len, size_t rsp_cap)
{
  uint8_t packet[BTHCI_MAX_PACKET];
  size_t len = H4_HEADER_SIZE + sizeof(struct bt_hci_cmd_hdr_s) + params_len;
  int ret;

  if (len > sizeof(packet))
    {
      return -E2BIG;
    }

  packet[0] = H4_CMD;
  bthci_put_le16(&packet[1], opcode);
  packet[3] = params_len;
  if (params_len > 0)
    {
      memcpy(&packet[4], params, params_len);
    }

  bthci_dump("TX", packet, len);
  ret = bthci_write_all(fd, packet, len);
  if (ret < 0)
    {
      return ret;
    }

  ret = bthci_read_cmd_capture(fd, wait_ms, opcode, rsp, rsp_len, rsp_cap);
  if (ret == -ETIMEDOUT)
    {
      printf("bthci: opcode 0x%04x timed out after %u ms\n",
             opcode, wait_ms);
    }
  else if (ret < 0)
    {
      printf("bthci: opcode 0x%04x returned error %d\n", opcode, ret);
    }

  return ret;
}

static int bthci_send_cmd_allow_timeout(int fd, uint16_t opcode,
                                        const uint8_t *params,
                                        uint8_t params_len,
                                        unsigned int wait_ms)
{
  int ret = bthci_send_cmd(fd, opcode, params, params_len, wait_ms);

  if (ret == -ETIMEDOUT)
    {
      printf("bthci: opcode 0x%04x timeout ignored for fire-and-hold test\n",
             opcode);
      return 0;
    }

  return ret;
}

static int bthci_open(void)
{
  int fd;

  fd = open(BTHCI_DEV, O_RDWR | O_NONBLOCK);
  if (fd < 0)
    {
      printf("bthci: open %s failed errno=%d. Run btstart first.\n",
             BTHCI_DEV, errno);
      return -1;
    }

  return fd;
}

static unsigned int bthci_default_uart_baud(void)
{
#ifdef CONFIG_AIC_BT_UART_BAUD
  return CONFIG_AIC_BT_UART_BAUD;
#else
  return 115200;
#endif
}

static bool bthci_default_uart_flowctrl(void)
{
#ifdef CONFIG_AIC_BT_UART_HW_FLOWCTRL
  return true;
#else
  return false;
#endif
}

static speed_t bthci_uart_speed(unsigned int baud)
{
  switch (baud)
    {
      case 115200:
        return B115200;

#ifdef B921600
      case 921600:
        return B921600;
#endif

#ifdef B1500000
      case 1500000:
        return B1500000;
#endif

      default:
        return 0;
    }
}

static int bthci_parse_flow(const char *arg, bool *flowctrl)
{
  if (arg == NULL)
    {
      *flowctrl = bthci_default_uart_flowctrl();
      return 0;
    }

  if (strcmp(arg, "1") == 0 || strcmp(arg, "on") == 0 ||
      strcmp(arg, "flow") == 0 || strcmp(arg, "rtscts") == 0)
    {
      *flowctrl = true;
      return 0;
    }

  if (strcmp(arg, "0") == 0 || strcmp(arg, "off") == 0 ||
      strcmp(arg, "noflow") == 0)
    {
      *flowctrl = false;
      return 0;
    }

  printf("bthci: invalid flow option %s\n", arg);
  return -EINVAL;
}

static int bthci_uart_configure(int fd, unsigned int baud, bool flowctrl)
{
  struct termios tio;
  speed_t speed = bthci_uart_speed(baud);

  if (speed == 0)
    {
      printf("bthci UART: unsupported baud %u\n", baud);
      return -EINVAL;
    }

  if (tcgetattr(fd, &tio) < 0)
    {
      printf("bthci UART: tcgetattr failed errno=%d\n", errno);
      return -errno;
    }

  cfsetispeed(&tio, speed);
  cfsetospeed(&tio, speed);

  tio.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
  tio.c_cflag |= CS8 | CLOCAL | CREAD;
#ifdef CRTSCTS
  tio.c_cflag &= ~CRTSCTS;
  if (flowctrl)
    {
      tio.c_cflag |= CRTSCTS;
    }
#else
  if (flowctrl)
    {
      printf("bthci UART: CRTSCTS is unavailable, flowctrl ignored\n");
    }
#endif

  tio.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR);
  tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
  tio.c_oflag &= ~OPOST;
  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;

  if (tcsetattr(fd, TCSANOW, &tio) < 0)
    {
      printf("bthci UART: tcsetattr failed errno=%d\n", errno);
      return -errno;
    }

  tcflush(fd, TCIOFLUSH);
  return 0;
}

static int bthci_uart_pinmux_one(const char *name, unsigned int func,
                                 unsigned int pull)
{
  int pin = hal_gpio_name2pin(name);
  unsigned int group;
  unsigned int group_pin;

  if (pin < 0)
    {
      printf("bthci UART: invalid pin %s\n", name);
      return -EINVAL;
    }

  group = BTHCI_GPIO_GROUP(pin);
  group_pin = BTHCI_GPIO_GROUP_PIN(pin);

  hal_gpio_set_func(group, group_pin, func);
  hal_gpio_set_bias_pull(group, group_pin, pull);
  hal_gpio_set_drive_strength(group, group_pin, BTHCI_UART2_DRV);
  return 0;
}

static int bthci_uart_pinmux_init(bool flowctrl)
{
  int ret;

  ret = bthci_uart_pinmux_one(BTHCI_UART2_TX_GPIO,
                              BTHCI_UART2_TXRX_FUNC,
                              BTHCI_PIN_PULL_DIS);
  if (ret < 0)
    {
      return ret;
    }

  ret = bthci_uart_pinmux_one(BTHCI_UART2_RX_GPIO,
                              BTHCI_UART2_TXRX_FUNC,
                              BTHCI_PIN_PULL_UP);
  if (ret < 0)
    {
      return ret;
    }

  if (flowctrl)
    {
      ret = bthci_uart_pinmux_one(BTHCI_UART2_RTS_GPIO,
                                  BTHCI_UART2_FLOW_FUNC,
                                  BTHCI_PIN_PULL_DIS);
      if (ret < 0)
        {
          return ret;
        }

      ret = bthci_uart_pinmux_one(BTHCI_UART2_CTS_GPIO,
                                  BTHCI_UART2_FLOW_FUNC,
                                  BTHCI_PIN_PULL_UP);
      if (ret < 0)
        {
          return ret;
        }

      printf("bthci UART: pinmux %s/%s func %u, %s/%s func %u\n",
             BTHCI_UART2_TX_GPIO, BTHCI_UART2_RX_GPIO,
             BTHCI_UART2_TXRX_FUNC, BTHCI_UART2_RTS_GPIO,
             BTHCI_UART2_CTS_GPIO, BTHCI_UART2_FLOW_FUNC);
    }
  else
    {
      printf("bthci UART: pinmux %s/%s func %u, RTS/CTS disabled\n",
             BTHCI_UART2_TX_GPIO, BTHCI_UART2_RX_GPIO,
             BTHCI_UART2_TXRX_FUNC);
    }

  return 0;
}

static int bthci_uart_prepare(bool flowctrl)
{
  int ret;

#if defined(CONFIG_AIC_WLAN_AIC8800D40L) && defined(CONFIG_AIC8800_BT_SUPPORT)
  ret = aic8800_bt_patch_prepare();
  if (ret < 0)
    {
      printf("bthci UART: AIC8800 BT patch prepare failed: %d\n", ret);
      return ret;
    }

  printf("bthci UART: AIC8800 BT patch prepared\n");
#endif

  ret = bthci_uart_pinmux_init(flowctrl);
  if (ret < 0)
    {
      return ret;
    }

  return 0;
}

static int bthci_uart_open(unsigned int baud, bool flowctrl)
{
  int fd;
  int ret;

  ret = bthci_uart_prepare(flowctrl);
  if (ret < 0)
    {
      return -1;
    }

  fd = open(BTHCI_UART_DEV, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0)
    {
      printf("bthci UART: open %s failed errno=%d\n", BTHCI_UART_DEV,
             errno);
#if defined(CONFIG_AIC_WLAN_AIC8800D40L) && defined(CONFIG_AIC8800_BT_SUPPORT)
      aic8800_bt_patch_release();
#endif
      return -1;
    }

  ret = bthci_uart_configure(fd, baud, flowctrl);
  if (ret < 0)
    {
      close(fd);
#if defined(CONFIG_AIC_WLAN_AIC8800D40L) && defined(CONFIG_AIC8800_BT_SUPPORT)
      aic8800_bt_patch_release();
#endif
      return -1;
    }

  printf("bthci UART: open %s baud %u%s fd=%d\n", BTHCI_UART_DEV, baud,
         flowctrl ? " flowctrl" : "", fd);
  return fd;
}

static void bthci_uart_close(int fd)
{
  close(fd);
#if defined(CONFIG_AIC_WLAN_AIC8800D40L) && defined(CONFIG_AIC8800_BT_SUPPORT)
  aic8800_bt_patch_release();
#endif
}

static int bthci_uart_reset_once(unsigned int wait_ms, unsigned int baud,
                                 bool flowctrl)
{
  int fd;
  int ret;

  fd = bthci_uart_open(baud, flowctrl);
  if (fd < 0)
    {
      return 1;
    }

  ret = bthci_send_cmd(fd, BT_HCI_OP_RESET, NULL, 0, wait_ms);
  bthci_uart_close(fd);
  return ret == 0 ? 0 : 1;
}

static int bthci_uart_reset_cmd(int argc, char *argv[])
{
  unsigned int wait_ms = argc >= 3 ? (unsigned int)atoi(argv[2]) : 1000;
  unsigned int baud = argc >= 4 ? (unsigned int)atoi(argv[3]) :
                      bthci_default_uart_baud();
  bool flowctrl;

  if (bthci_parse_flow(argc >= 5 ? argv[4] : NULL, &flowctrl) < 0)
    {
      return 1;
    }

  if (wait_ms == 0)
    {
      wait_ms = 1000;
    }

  return bthci_uart_reset_once(wait_ms, baud, flowctrl);
}

static int bthci_uart_auto(unsigned int wait_ms)
{
  static const struct
  {
    unsigned int baud;
    bool flowctrl;
  } probes[] =
    {
      {1500000, true},
      {1500000, false},
      {115200, false},
      {115200, true},
      {921600, true},
      {921600, false},
    };
  size_t i;
  int ok = 0;

  if (wait_ms == 0)
    {
      wait_ms = 1000;
    }

  for (i = 0; i < sizeof(probes) / sizeof(probes[0]); i++)
    {
      printf("bthci UART auto: HCI reset baud=%u%s\n",
             probes[i].baud, probes[i].flowctrl ? " flowctrl" : "");
      if (bthci_uart_reset_once(wait_ms, probes[i].baud,
                                probes[i].flowctrl) == 0)
        {
          ok = 1;
        }
    }

  return ok ? 0 : 1;
}

static int bthci_cmd_no_params(uint16_t opcode, unsigned int wait_ms)
{
  int fd = bthci_open();
  int ret;

  if (fd < 0)
    {
      return 1;
    }

  ret = bthci_send_cmd(fd, opcode, NULL, 0, wait_ms);
  close(fd);
  return ret == 0 ? 0 : 1;
}

static int bthci_info(unsigned int wait_ms)
{
  int fd = bthci_open();
  int ret = 0;

  if (fd < 0)
    {
      return 1;
    }

  ret |= bthci_send_cmd(fd, BT_HCI_OP_RESET, NULL, 0, wait_ms) != 0;
  ret |= bthci_send_cmd(fd, BT_HCI_OP_READ_LOCAL_VERSION_INFO,
                        NULL, 0, wait_ms) != 0;
  ret |= bthci_send_cmd(fd, BTHCI_OP_READ_SUPPORTED_CMDS,
                        NULL, 0, wait_ms) != 0;
  ret |= bthci_send_cmd(fd, BT_HCI_OP_READ_BD_ADDR, NULL, 0, wait_ms) != 0;
  ret |= bthci_send_cmd(fd, BT_HCI_OP_READ_LOCAL_FEATURES,
                        NULL, 0, wait_ms) != 0;
  printf("Classic buffer size is optional; use `bthci buffers` to probe it.\n");
  ret |= bthci_send_cmd(fd, BT_HCI_OP_LE_READ_LOCAL_FEATURES,
                        NULL, 0, wait_ms) != 0;
  ret |= bthci_send_cmd(fd, BT_HCI_OP_LE_READ_BUFFER_SIZE,
                        NULL, 0, wait_ms) != 0;

  close(fd);
  return ret ? 1 : 0;
}

static void bthci_aic_print_mem_value(const char *tag, uint32_t addr,
                                      const uint8_t *rsp, size_t rsp_len)
{
  uint8_t status = rsp_len > 0 ? rsp[0] : 0xff;

  printf("%s mem[0x%08lx] status=0x%02x", tag, (unsigned long)addr,
         status);
  if (status == 0 && rsp_len >= 6)
    {
      uint8_t len = rsp[1];
      uint32_t value = bthci_get_le32(&rsp[2]);

      printf(" len=%u value=0x%08lx", len, (unsigned long)value);
      if (addr == 0x40500184)
        {
          printf(" cinit_begin=%u cinit_done=%u",
                 (value & (1u << 12)) != 0,
                 (value & (1u << 13)) != 0);
        }
    }
  else if (status == 0)
    {
      printf(" short-rsp-len=%u", (unsigned int)rsp_len);
    }

  printf("\n");
}

static int bthci_aic_fw_status(int fd, unsigned int wait_ms, const char *tag)
{
  uint8_t rsp[BTHCI_AIC_PROBE_RSP_MAX];
  size_t rsp_len = 0;
  int ret;

  ret = bthci_send_cmd_capture(fd, BTHCI_OP_AIC_FW_STATUS_GET, NULL, 0,
                               wait_ms, rsp, &rsp_len, sizeof(rsp));
  printf("%s FW_STATUS_GET ret=%d", tag, ret);
  if (rsp_len > 0)
    {
      printf(" status=0x%02x", rsp[0]);
    }

  printf(" rsp_len=%u\n", (unsigned int)rsp_len);
  return ret;
}

static int bthci_aic_read_mem(int fd, uint32_t addr, unsigned int wait_ms,
                              const char *tag)
{
  uint8_t params[6];
  uint8_t rsp[BTHCI_AIC_PROBE_RSP_MAX];
  size_t rsp_len = 0;
  int ret;

  bthci_put_le32(&params[0], addr);
  params[4] = BTHCI_AIC_DBG_MEM_TYPE_U32;
  params[5] = BTHCI_AIC_DBG_MEM_LEN_U32;

  ret = bthci_send_cmd_capture(fd, BTHCI_OP_AIC_DBG_RD_MEM, params,
                               sizeof(params), wait_ms, rsp, &rsp_len,
                               sizeof(rsp));
  bthci_aic_print_mem_value(tag, addr, rsp, rsp_len);
  return ret;
}

#ifdef CONFIG_AIC_BT_CMD_CLASSIC_DIAG
static int bthci_aic_classic_smoke(int fd, unsigned int wait_ms,
                                   const char *tag)
{
  uint8_t local_name[BTHCI_LOCAL_NAME_MAX];
  uint8_t class_of_dev[3] = {0x00, 0x00, 0x00};
  uint8_t scan_disable[1] = {0x00};
  int ret = 0;

  printf("%s Classic command smoke test\n", tag);
  bthci_fill_local_name(local_name, "AIC8800-Probe");
  ret |= bthci_send_cmd(fd, BT_HCI_OP_READ_BUFFER_SIZE, NULL, 0,
                        wait_ms) != 0;
  ret |= bthci_send_cmd(fd, BTHCI_OP_WRITE_LOCAL_NAME, local_name,
                        sizeof(local_name), wait_ms) != 0;
  ret |= bthci_send_cmd(fd, BTHCI_OP_WRITE_CLASS_OF_DEV, class_of_dev,
                        sizeof(class_of_dev), wait_ms) != 0;
  ret |= bthci_send_cmd(fd, BTHCI_OP_WRITE_SCAN_ENABLE, scan_disable,
                        sizeof(scan_disable), wait_ms) != 0;

  printf("%s Classic smoke result=%s\n", tag, ret ? "failed" : "ok");
  return ret ? -EIO : 0;
}
#endif

static int bthci_aic_probe_on_fd(int fd, unsigned int wait_ms,
                                 const char *tag)
{
  static const uint32_t probe_addrs[] =
    {
      0x40200024,
      0x40500000,
      0x00000020,
      0x40500148,
      0x40500184,
      0x40505010,
      0x40506004,
      0x40509000,
    };
  size_t i;
  int standard_fail = 0;
  int vendor_fail = 0;
#ifdef CONFIG_AIC_BT_CMD_CLASSIC_DIAG
  int classic_fail = 0;
#endif

  if (wait_ms == 0)
    {
      wait_ms = 1000;
    }

  printf("%s standard HCI capability probe, wait=%u ms\n", tag, wait_ms);
  standard_fail |= bthci_send_cmd(fd, BT_HCI_OP_RESET, NULL, 0,
                                  wait_ms) != 0;
  standard_fail |= bthci_send_cmd(fd, BT_HCI_OP_READ_LOCAL_VERSION_INFO,
                                  NULL, 0, wait_ms) != 0;
  standard_fail |= bthci_send_cmd(fd, BT_HCI_OP_READ_BD_ADDR, NULL, 0,
                                  wait_ms) != 0;
  standard_fail |= bthci_send_cmd(fd, BT_HCI_OP_READ_LOCAL_FEATURES,
                                  NULL, 0, wait_ms) != 0;
  standard_fail |= bthci_send_cmd(fd, BT_HCI_OP_LE_READ_LOCAL_FEATURES,
                                  NULL, 0, wait_ms) != 0;

  printf("%s AIC vendor probe\n", tag);
  vendor_fail |= bthci_aic_fw_status(fd, wait_ms, tag) != 0;
  for (i = 0; i < sizeof(probe_addrs) / sizeof(probe_addrs[0]); i++)
    {
      vendor_fail |= bthci_aic_read_mem(fd, probe_addrs[i], wait_ms,
                                        tag) != 0;
    }

#ifdef CONFIG_AIC_BT_CMD_CLASSIC_DIAG
  classic_fail |= bthci_aic_classic_smoke(fd, wait_ms, tag) != 0;
  printf("%s summary: standard=%s vendor=%s classic=%s\n", tag,
         standard_fail ? "has-error" : "ok",
         vendor_fail ? "has-error" : "ok",
         classic_fail ? "has-error" : "ok");
#else
  printf("%s summary: standard=%s vendor=%s\n", tag,
         standard_fail ? "has-error" : "ok",
         vendor_fail ? "has-error" : "ok");
#endif
  printf("%s next: this board is handled as BLE-only when Local features "
         "show BT_LMP_NO_BREDR; use BLE GAP/GATT commands.\n", tag);
  return 0;
}

static int bthci_aic_probe(unsigned int wait_ms)
{
  int fd = bthci_open();
  int ret;

  if (fd < 0)
    {
      return 1;
    }

  ret = bthci_aic_probe_on_fd(fd, wait_ms, "AIC probe");
  close(fd);
  return ret == 0 ? 0 : 1;
}

static int bthci_aic_uart_probe(unsigned int wait_ms, unsigned int baud,
                                bool flowctrl)
{
  int fd;
  int ret;

  fd = bthci_uart_open(baud, flowctrl);
  if (fd < 0)
    {
      return 1;
    }

  printf("AIC UART probe: exclusive probe on %s; run btstop first\n",
         BTHCI_UART_DEV);
  ret = bthci_aic_probe_on_fd(fd, wait_ms, "AIC UART probe");
  bthci_uart_close(fd);
  return ret == 0 ? 0 : 1;
}

static uint8_t bthci_parse_scan_type(const char *arg, uint8_t fallback)
{
  if (arg == NULL)
    {
      return fallback;
    }

  if (strcmp(arg, "passive") == 0 || strcmp(arg, "0") == 0)
    {
      return BT_LE_SCAN_PASSIVE;
    }

  if (strcmp(arg, "active") == 0 || strcmp(arg, "1") == 0)
    {
      return BT_LE_SCAN_ACTIVE;
    }

  printf("bthci: unknown scan type \"%s\", use %s\n", arg,
         fallback == BT_LE_SCAN_ACTIVE ? "active" : "passive");
  return fallback;
}

static const char *bthci_parse_scan_filter(const char *arg)
{
  if (arg == NULL || strcmp(arg, "-") == 0 || strcmp(arg, "none") == 0 ||
      strcmp(arg, "null") == 0)
    {
      return NULL;
    }

  return arg;
}

static uint8_t bthci_parse_dup_filter(const char *arg, uint8_t fallback)
{
  if (arg == NULL)
    {
      return fallback;
    }

  if (strcmp(arg, "dups") == 0 || strcmp(arg, "dup") == 0 ||
      strcmp(arg, "0") == 0)
    {
      return BT_LE_SCAN_FILTER_DUP_DISABLE;
    }

  if (strcmp(arg, "nodups") == 0 || strcmp(arg, "filter") == 0 ||
      strcmp(arg, "1") == 0)
    {
      return BT_LE_SCAN_FILTER_DUP_ENABLE;
    }

  printf("bthci: unknown duplicate option \"%s\", use %s\n", arg,
         fallback == BT_LE_SCAN_FILTER_DUP_ENABLE ? "nodups" : "dups");
  return fallback;
}

static int bthci_scan_on_fd(int fd, unsigned int seconds, const char *filter,
                            uint8_t scan_type, uint8_t dup_filter,
                            const char *tag)
{
  static const uint8_t event_mask[8] =
    {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20
    };
  static const uint8_t le_event_mask[8] =
    {
      0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
  uint8_t scan_params[7] =
    {
      BT_LE_SCAN_ACTIVE, 0x10, 0x00, 0x10, 0x00, 0x00, 0x00
    };
  uint8_t scan_enable[2] =
    {
      BT_LE_SCAN_ENABLE, BT_LE_SCAN_FILTER_DUP_ENABLE
    };
  static const uint8_t scan_disable[2] =
    {
      BT_LE_SCAN_DISABLE, BT_LE_SCAN_FILTER_DUP_DISABLE
    };
  int ret = 0;

  if (seconds == 0)
    {
      seconds = 5;
    }

  scan_params[0] = scan_type;
  scan_enable[1] = dup_filter;
  g_bthci_scan_filter = filter;
  g_bthci_scan_reports = 0;
  g_bthci_scan_matches = 0;

  ret |= bthci_send_cmd(fd, BT_HCI_OP_RESET, NULL, 0, 1000) != 0;
  ret |= bthci_send_cmd(fd, BT_HCI_OP_SET_EVENT_MASK,
                        event_mask, sizeof(event_mask), 1000) != 0;
  ret |= bthci_send_cmd(fd, BT_HCI_OP_LE_SET_EVENT_MASK,
                        le_event_mask, sizeof(le_event_mask), 1000) != 0;
  ret |= bthci_send_cmd(fd, BT_HCI_OP_LE_SET_SCAN_PARAMS,
                        scan_params, sizeof(scan_params), 1000) != 0;
  ret |= bthci_send_cmd(fd, BT_HCI_OP_LE_SET_SCAN_ENABLE,
                        scan_enable, sizeof(scan_enable), 1000) != 0;

  if (!ret)
    {
      printf("%s scanning for %u second(s), type=%s dup=%s",
             tag, seconds,
             scan_type == BT_LE_SCAN_ACTIVE ? "active" : "passive",
             dup_filter == BT_LE_SCAN_FILTER_DUP_ENABLE ? "nodups" :
             "dups");
      if (filter != NULL)
        {
          char addr_key[BTHCI_ADDR_KEY_MAX];

          printf(" filter=\"%s\"", filter);
          if (bthci_addr_key_from_text(filter, addr_key, sizeof(addr_key)))
            {
              printf(" addr-key=%s", addr_key);
            }
        }

      printf("\n");
      bthci_read_events(fd, seconds * 1000, 0, false,
                        BTHCI_PARSE_LE_ADV);
    }

  printf("%s scan summary reports=%u matches=%u",
         tag, g_bthci_scan_reports, g_bthci_scan_matches);
  if (filter != NULL)
    {
      printf(" filter=\"%s\"", filter);
    }

  printf("\n");
  bthci_send_cmd(fd, BT_HCI_OP_LE_SET_SCAN_ENABLE,
                 scan_disable, sizeof(scan_disable), 1000);

  g_bthci_scan_filter = NULL;
  return ret ? 1 : 0;
}

static int bthci_scan(unsigned int seconds, const char *filter,
                      uint8_t scan_type, uint8_t dup_filter)
{
  int fd = bthci_open();
  int ret;

  if (fd < 0)
    {
      return 1;
    }

  ret = bthci_scan_on_fd(fd, seconds, filter, scan_type, dup_filter,
                         "bthci");
  close(fd);
  return ret;
}

static int bthci_uart_scan(unsigned int seconds, const char *filter,
                           uint8_t scan_type, uint8_t dup_filter,
                           unsigned int baud, bool flowctrl)
{
  int fd;
  int ret;

  fd = bthci_uart_open(baud, flowctrl);
  if (fd < 0)
    {
      return 1;
    }

  printf("bthci UART: exclusive scan on %s; do not run btle/bluetoothd "
         "at the same time\n", BTHCI_UART_DEV);
  ret = bthci_scan_on_fd(fd, seconds, filter, scan_type, dup_filter,
                         "bthci UART");
  bthci_uart_close(fd);
  return ret;
}

static int bthci_uart_connect(const char *addr_text, const char *type_text,
                              unsigned int wait_ms, unsigned int baud,
                              bool flowctrl)
{
  static const uint8_t event_mask[8] =
    {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20
    };
  static const uint8_t le_event_mask[8] =
    {
      0x7f, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
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
  uint8_t params[25] =
    {
      0x10, 0x00, 0x10, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00,
      0x18, 0x00, 0x28, 0x00, 0x00, 0x00,
      0x90, 0x01, 0x00, 0x00, 0x00, 0x00
    };
  uint8_t addr[6];
  uint8_t addr_type;
  uint8_t disconnect[3];
  int fd;
  int ret = 0;

  if (bthci_parse_addr(addr_text, addr) < 0 ||
      bthci_parse_addr_type(type_text, &addr_type) < 0)
    {
      printf("bthci uartconn: usage: bthci uartconn <addr> "
             "<public|random|0|1> [wait_ms] [baud] [flow|noflow]\n");
      return 1;
    }

  fd = bthci_uart_open(baud, flowctrl);
  if (fd < 0)
    {
      return 1;
    }

  params[5] = addr_type;
  memcpy(&params[6], addr, sizeof(addr));
  printf("bthci UART: exclusive legacy connect on %s; run btstop first\n",
         BTHCI_UART_DEV);
  printf("bthci UART: target type=%u addr=", addr_type);
  bthci_print_addr(addr);
  printf(" wait_ms=%u\n", wait_ms);

  g_bthci_conn_connected = false;
  g_bthci_conn_handle = 0xffff;

  ret |= bthci_send_cmd(fd, BT_HCI_OP_RESET, NULL, 0, 1000) != 0;
  ret |= bthci_send_cmd(fd, BT_HCI_OP_SET_EVENT_MASK,
                        event_mask, sizeof(event_mask), 1000) != 0;
  bthci_send_cmd(fd, BT_HCI_OP_LE_WRITE_LE_HOST_SUPP,
                 le_host_support, sizeof(le_host_support), 1000);
  ret |= bthci_send_cmd(fd, BT_HCI_OP_LE_SET_EVENT_MASK,
                        le_event_mask, sizeof(le_event_mask), 1000) != 0;
  bthci_send_cmd(fd, BT_HCI_OP_LE_SET_SCAN_ENABLE,
                 scan_disable, sizeof(scan_disable), 1000);
  bthci_send_cmd(fd, BT_HCI_OP_LE_SET_ADV_ENABLE,
                 adv_disable, sizeof(adv_disable), 1000);

  ret |= bthci_send_cmd(fd, BT_HCI_OP_LE_CREATE_CONN,
                        params, sizeof(params), 1000) != 0;
  if (!ret)
    {
      bthci_read_events(fd, wait_ms, 0, false, BTHCI_PARSE_LE_CONN);
    }

  if (g_bthci_conn_connected)
    {
      bthci_put_le16(disconnect, g_bthci_conn_handle);
      disconnect[2] = 0x13;
      bthci_send_cmd(fd, BT_HCI_OP_DISCONNECT, disconnect,
                     sizeof(disconnect), 1000);
      bthci_read_events(fd, 2000, 0, false, BTHCI_PARSE_LE_CONN);
    }
  else
    {
      bthci_send_cmd(fd, BT_HCI_OP_LE_CREATE_CONN_CANCEL, NULL, 0, 1000);
    }

  bthci_uart_close(fd);
  return ret ? 1 : 0;
}

static size_t bthci_limited_strlen(const char *s, size_t max_len)
{
  size_t len = 0;

  while (len < max_len && s[len] != '\0')
    {
      len++;
    }

  return len;
}

static void bthci_fill_adv_data(uint8_t *params, const char *name)
{
  const size_t name_max = BTHCI_ADV_DATA_MAX - 5;
  size_t name_len = bthci_limited_strlen(name, name_max);
  bool complete_name = name[name_len] == '\0';
  size_t pos = 1;

  memset(params, 0, BTHCI_ADV_DATA_MAX + 1);

  params[pos++] = 2;
  params[pos++] = BT_EIR_FLAGS;
  params[pos++] = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;

  if (name_len > 0)
    {
      params[pos++] = (uint8_t)(name_len + 1);
      params[pos++] = complete_name ? BT_EIR_NAME_COMPLETE : 0x08;
      memcpy(&params[pos], name, name_len);
      pos += name_len;
    }

  params[0] = (uint8_t)(pos - 1);
}

static int bthci_le_advertise(unsigned int seconds, const char *name)
{
  uint8_t adv_params[15] =
    {
      0
    };
  uint8_t adv_data[BTHCI_ADV_DATA_MAX + 1];
  uint8_t adv_enable[1] = {1};
  uint8_t adv_disable[1] = {0};
  int fd = bthci_open();
  int ret = 0;

  if (fd < 0)
    {
      return 1;
    }

  if (seconds == 0)
    {
      seconds = 30;
    }

  bthci_put_le16(&adv_params[0], 0x00a0);
  bthci_put_le16(&adv_params[2], 0x00f0);
  adv_params[4] = BT_LE_ADV_IND;
  adv_params[5] = BT_ADDR_LE_PUBLIC;
  adv_params[13] = 0x07;
  bthci_fill_adv_data(adv_data, name);

  ret |= bthci_send_cmd_allow_timeout(fd, BT_HCI_OP_RESET, NULL, 0,
                                      1000) != 0;
  ret |= bthci_send_cmd_allow_timeout(fd, BT_HCI_OP_LE_SET_ADV_ENABLE,
                                      adv_disable, sizeof(adv_disable),
                                      1000) != 0;
  ret |= bthci_send_cmd_allow_timeout(fd, BT_HCI_OP_LE_SET_ADV_PARAMETERS,
                                      adv_params, sizeof(adv_params),
                                      1000) != 0;
  ret |= bthci_send_cmd_allow_timeout(fd, BT_HCI_OP_LE_SET_ADV_DATA,
                                      adv_data, sizeof(adv_data),
                                      1000) != 0;
  ret |= bthci_send_cmd_allow_timeout(fd, BT_HCI_OP_LE_SET_SCAN_RSP_DATA,
                                      adv_data, sizeof(adv_data),
                                      1000) != 0;
  ret |= bthci_send_cmd_allow_timeout(fd, BT_HCI_OP_LE_SET_ADV_ENABLE,
                                      adv_enable, sizeof(adv_enable),
                                      1000) != 0;

  if (!ret)
    {
      printf("LE advertising as \"%s\" for %u second(s)...\n",
             name, seconds);
      sleep(seconds);
    }

  bthci_send_cmd(fd, BT_HCI_OP_LE_SET_ADV_ENABLE,
                 adv_disable, sizeof(adv_disable), 1000);

  close(fd);
  return ret ? 1 : 0;
}

#ifdef CONFIG_AIC_BT_CMD_CLASSIC_DIAG
static void bthci_fill_local_name(uint8_t *local_name, const char *name)
{
  size_t len;

  memset(local_name, 0, BTHCI_LOCAL_NAME_MAX);
  len = bthci_limited_strlen(name, BTHCI_LOCAL_NAME_MAX - 1);
  memcpy(local_name, name, len);
}

static int bthci_visible(unsigned int seconds, const char *name)
{
  uint8_t local_name[BTHCI_LOCAL_NAME_MAX];
  uint8_t class_of_dev[3] = {0x00, 0x00, 0x00};
  uint8_t scan_enable[1] = {0x03};
  uint8_t scan_disable[1] = {0x00};
  int fd = bthci_open();
  int ret = 0;

  if (fd < 0)
    {
      return 1;
    }

  if (seconds == 0)
    {
      seconds = 30;
    }

  bthci_fill_local_name(local_name, name);

  ret |= bthci_send_cmd(fd, BT_HCI_OP_RESET, NULL, 0, 1000) != 0;
  ret |= bthci_send_cmd(fd, BTHCI_OP_WRITE_LOCAL_NAME,
                        local_name, sizeof(local_name), 1000) != 0;
  ret |= bthci_send_cmd(fd, BTHCI_OP_WRITE_CLASS_OF_DEV,
                        class_of_dev, sizeof(class_of_dev), 1000) != 0;
  ret |= bthci_send_cmd(fd, BTHCI_OP_WRITE_SCAN_ENABLE,
                        scan_enable, sizeof(scan_enable), 1000) != 0;

  if (!ret)
    {
      printf("Classic discoverable as \"%s\" for %u second(s)...\n",
             name, seconds);
      sleep(seconds);
    }

  bthci_send_cmd(fd, BTHCI_OP_WRITE_SCAN_ENABLE,
                 scan_disable, sizeof(scan_disable), 1000);

  close(fd);
  return ret ? 1 : 0;
}

static int bthci_inquiry(unsigned int seconds)
{
  static const uint8_t event_mask[8] =
    {
      0x43, 0x60, 0x00, 0x00, 0x02, 0x40, 0x00, 0x00
    };
  uint8_t params[5] = {0x33, 0x8b, 0x9e, 0x08, 0x00};
  unsigned int units;
  unsigned int wait_ms;
  int fd = bthci_open();
  int ret = 0;

  if (fd < 0)
    {
      return 1;
    }

  if (seconds == 0)
    {
      seconds = 10;
    }

  units = (seconds * 1000 + 1279) / 1280;
  if (units < 1)
    {
      units = 1;
    }
  else if (units > 48)
    {
      units = 48;
    }

  params[3] = (uint8_t)units;
  wait_ms = units * 1280 + 2000;

  ret |= bthci_send_cmd(fd, BT_HCI_OP_RESET, NULL, 0, 1000) != 0;
  bthci_send_cmd(fd, BT_HCI_OP_SET_EVENT_MASK,
                 event_mask, sizeof(event_mask), 1000);
  ret |= bthci_send_cmd(fd, BT_HCI_OP_INQUIRY,
                        params, sizeof(params), 1000) != 0;

  if (!ret)
    {
      printf("Classic inquiry for about %u ms...\n", wait_ms);
      bthci_read_events(fd, wait_ms, 0, false, BTHCI_PARSE_INQUIRY);
    }

  close(fd);
  return ret ? 1 : 0;
}
#endif

static int bthci_parse_hex_byte(const char *s, uint8_t *value)
{
  char *end;
  long v;

  errno = 0;
  v = strtol(s, &end, 16);
  if (errno != 0 || end == s || *end != '\0' || v < 0 || v > 0xff)
    {
      return -EINVAL;
    }

  *value = (uint8_t)v;
  return 0;
}

static int bthci_raw(int argc, char *argv[])
{
  uint8_t params[255];
  unsigned long opcode;
  char *end;
  int fd;
  int i;
  int ret;

  if (argc < 3)
    {
      return -EINVAL;
    }

  errno = 0;
  opcode = strtoul(argv[2], &end, 16);
  if (errno != 0 || end == argv[2] || *end != '\0' || opcode > 0xffff)
    {
      printf("bthci: invalid opcode %s\n", argv[2]);
      return -EINVAL;
    }

  for (i = 3; i < argc; i++)
    {
      if (bthci_parse_hex_byte(argv[i], &params[i - 3]) < 0)
        {
          printf("bthci: invalid payload byte %s\n", argv[i]);
          return -EINVAL;
        }
    }

  fd = bthci_open();
  if (fd < 0)
    {
      return 1;
    }

  ret = bthci_send_cmd(fd, (uint16_t)opcode, params, (uint8_t)(argc - 3),
                       1000);
  close(fd);
  return ret == 0 ? 0 : 1;
}

static void bthci_usage(void)
{
  printf("Usage:\n");
  printf("  bthci reset [wait_ms]\n");
  printf("  bthci version [wait_ms]\n");
  printf("  bthci bdaddr [wait_ms]\n");
  printf("  bthci commands [wait_ms]\n");
  printf("  bthci features [wait_ms]\n");
  printf("  bthci lefeatures [wait_ms]\n");
  printf("  bthci buffers [wait_ms]\n");
  printf("  bthci lebuffers [wait_ms]\n");
  printf("  bthci info [wait_ms]\n");
  printf("  bthci aicprobe [wait_ms]\n");
  printf("  bthci aicuartprobe [wait_ms] [baud] [flow|noflow]\n");
  printf("  bthci scan|lescan [seconds] [name-or-mac-filter] "
         "[active|passive] [dups|nodups]\n");
  printf("  bthci uartscan [seconds] [name-or-mac-filter|-] "
         "[active|passive] [dups|nodups] [baud] [flow|noflow]\n");
  printf("  bthci uartconn <addr> <public|random|0|1> "
         "[wait_ms] [baud] [flow|noflow]\n");
  printf("  bthci leadv [seconds] [name]\n");
#ifdef CONFIG_AIC_BT_CMD_CLASSIC_DIAG
  printf("  bthci inquiry [seconds]\n");
  printf("  bthci visible [seconds] [name]\n");
#endif
  printf("  bthci raw <opcode_hex> [payload_hex_byte...]\n");
  printf("  bthci uartreset [wait_ms] [baud] [flow|noflow]\n");
  printf("  bthci uartauto [wait_ms]\n");
}

int bthci_main(int argc, char *argv[])
{
  unsigned int value;

  if (argc < 2)
    {
      bthci_usage();
      return 1;
    }

  value = argc >= 3 ? (unsigned int)atoi(argv[2]) : 1000;

  if (strcmp(argv[1], "reset") == 0)
    {
      return bthci_cmd_no_params(BT_HCI_OP_RESET, value);
    }
  else if (strcmp(argv[1], "version") == 0)
    {
      return bthci_cmd_no_params(BT_HCI_OP_READ_LOCAL_VERSION_INFO, value);
    }
  else if (strcmp(argv[1], "bdaddr") == 0)
    {
      return bthci_cmd_no_params(BT_HCI_OP_READ_BD_ADDR, value);
    }
  else if (strcmp(argv[1], "commands") == 0)
    {
      return bthci_cmd_no_params(BTHCI_OP_READ_SUPPORTED_CMDS, value);
    }
  else if (strcmp(argv[1], "features") == 0)
    {
      return bthci_cmd_no_params(BT_HCI_OP_READ_LOCAL_FEATURES, value);
    }
  else if (strcmp(argv[1], "lefeatures") == 0)
    {
      return bthci_cmd_no_params(BT_HCI_OP_LE_READ_LOCAL_FEATURES, value);
    }
  else if (strcmp(argv[1], "buffers") == 0)
    {
      return bthci_cmd_no_params(BT_HCI_OP_READ_BUFFER_SIZE, value);
    }
  else if (strcmp(argv[1], "lebuffers") == 0)
    {
      return bthci_cmd_no_params(BT_HCI_OP_LE_READ_BUFFER_SIZE, value);
    }
  else if (strcmp(argv[1], "info") == 0)
    {
      return bthci_info(value);
    }
  else if (strcmp(argv[1], "aicprobe") == 0)
    {
      return bthci_aic_probe(value);
    }
  else if (strcmp(argv[1], "aicuartprobe") == 0)
    {
      unsigned int baud;
      bool flowctrl;

      value = argc >= 3 ? (unsigned int)atoi(argv[2]) : 1000;
      baud = argc >= 4 ? (unsigned int)atoi(argv[3]) :
             bthci_default_uart_baud();
      if (bthci_parse_flow(argc >= 5 ? argv[4] : NULL, &flowctrl) < 0)
        {
          return 1;
        }

      return bthci_aic_uart_probe(value, baud, flowctrl);
    }
  else if (strcmp(argv[1], "scan") == 0 ||
           strcmp(argv[1], "lescan") == 0)
    {
      uint8_t scan_type;
      uint8_t dup_filter;

      value = argc >= 3 ? (unsigned int)atoi(argv[2]) : 5;
      scan_type = bthci_parse_scan_type(argc >= 5 ? argv[4] : NULL,
                                        BT_LE_SCAN_ACTIVE);
      dup_filter = bthci_parse_dup_filter(argc >= 6 ? argv[5] : NULL,
                                          BT_LE_SCAN_FILTER_DUP_ENABLE);
      return bthci_scan(value,
                        bthci_parse_scan_filter(argc >= 4 ? argv[3] :
                                                NULL),
                        scan_type, dup_filter);
    }
  else if (strcmp(argv[1], "uartscan") == 0)
    {
      uint8_t scan_type;
      uint8_t dup_filter;
      unsigned int baud;
      bool flowctrl;

      value = argc >= 3 ? (unsigned int)atoi(argv[2]) : 30;
      scan_type = bthci_parse_scan_type(argc >= 5 ? argv[4] : NULL,
                                        BT_LE_SCAN_PASSIVE);
      dup_filter = bthci_parse_dup_filter(argc >= 6 ? argv[5] : NULL,
                                          BT_LE_SCAN_FILTER_DUP_DISABLE);
      baud = argc >= 7 ? (unsigned int)atoi(argv[6]) :
             bthci_default_uart_baud();
      if (bthci_parse_flow(argc >= 8 ? argv[7] : NULL, &flowctrl) < 0)
        {
          return 1;
        }

      return bthci_uart_scan(value,
                             bthci_parse_scan_filter(argc >= 4 ? argv[3] :
                                                     NULL),
                             scan_type, dup_filter, baud, flowctrl);
    }
	  else if (strcmp(argv[1], "leadv") == 0)
	    {
	      value = argc >= 3 ? (unsigned int)atoi(argv[2]) : 30;
	      return bthci_le_advertise(value, argc >= 4 ? argv[3] :
	                                BTHCI_DEFAULT_NAME);
	    }
	  else if (strcmp(argv[1], "uartconn") == 0)
	    {
	      unsigned int wait_ms;
	      unsigned int baud;
	      bool flowctrl;

	      if (argc < 4)
	        {
	          bthci_usage();
	          return 1;
	        }

	      wait_ms = argc >= 5 ? (unsigned int)atoi(argv[4]) : 10000;
	      baud = argc >= 6 ? (unsigned int)atoi(argv[5]) :
	             bthci_default_uart_baud();
	      if (bthci_parse_flow(argc >= 7 ? argv[6] : NULL, &flowctrl) < 0)
	        {
	          return 1;
	        }

	      return bthci_uart_connect(argv[2], argv[3], wait_ms, baud,
	                                flowctrl);
	    }
#ifdef CONFIG_AIC_BT_CMD_CLASSIC_DIAG
  else if (strcmp(argv[1], "inquiry") == 0)
    {
      value = argc >= 3 ? (unsigned int)atoi(argv[2]) : 10;
      return bthci_inquiry(value);
    }
  else if (strcmp(argv[1], "visible") == 0)
    {
      value = argc >= 3 ? (unsigned int)atoi(argv[2]) : 30;
      return bthci_visible(value, argc >= 4 ? argv[3] :
                           BTHCI_DEFAULT_NAME);
    }
#endif
  else if (strcmp(argv[1], "raw") == 0)
    {
      return bthci_raw(argc, argv) == 0 ? 0 : 1;
    }
  else if (strcmp(argv[1], "uartreset") == 0)
    {
      return bthci_uart_reset_cmd(argc, argv);
    }
  else if (strcmp(argv[1], "uartauto") == 0)
    {
      return bthci_uart_auto(value);
    }

  bthci_usage();
  return 1;
}
