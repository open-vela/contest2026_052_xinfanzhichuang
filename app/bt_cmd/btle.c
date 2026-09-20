#include <nuttx/config.h>

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include "bluetooth.h"
#include "bt_addr.h"
#include "bt_adapter.h"
#include "bt_device.h"
#include "bt_gattc.h"
#include "bt_gatts.h"
#include "bt_le_scan.h"
#include "bt_uuid.h"

#include "aic_ble_gap.h"
#include "aic_ble_gatt_lifecycle.h"

#define BTLE_DEFAULT_NAME    "AIC8800-Vela"
#define BTLE_DEFAULT_SECONDS 60
#define BTLE_SCAN_SECONDS    10
#define BTLE_SCAN_NAME_MAX   32
#define BTLE_SCAN_RAW_MAX    64
#define BTLE_SCAN_CACHE_MAX  64
#define BTLE_SCAN_UNMATCHED_MAX 12
#define BTLE_SCAN_UUID16_MAX 4
#define BTLE_ADDR_KEY_LEN    12
#define BTLE_ADDR_KEY_MAX    (BTLE_ADDR_KEY_LEN + 1)
#define BTLE_CONNECT_SECONDS 10
#define BTLE_CONNECT_WAIT_MS 15000
#define BTLE_DISCONNECT_WAIT_MS 5000
#define BTLE_STATE_WAIT_MS   8000
#define BTLE_DISABLE_WAIT_MS 1500
#define BTLE_STATE_POLL_US   200000
#define BTLE_GATT_VALUE_MAX  64
#define BTLE_GATT_POLL_US    100000
#define BTLE_GATT_POLLS_PER_SECOND (1000000 / BTLE_GATT_POLL_US)
#define BTLE_GATT_NOTIFY_INTERVAL 5
#define BTLE_GATT_NOTIFY_POLLS \
  (BTLE_GATT_NOTIFY_INTERVAL * BTLE_GATT_POLLS_PER_SECOND)
#define BTLE_GATT_NOTIFY_TIMEOUT_POLLS (2 * BTLE_GATT_POLLS_PER_SECOND)
#define BTLE_GATT_NOTIFY_PAYLOAD_DEFAULT 20
#define BTLE_GATT_NOTIFY_DEFAULT_MAX 0
#define BTLE_GATT_CLEANUP_DEFAULT false
#define BTLE_GATT_ECHO_QUEUE_DEPTH 8
#define BTLE_GATT_CMD_PREFIX "cmd:"
#define BTLE_GATTC_DISCOVER_WAIT_MS 10000
#define BTLE_GATTC_OP_WAIT_MS       4000
#define BTLE_GATTC_NOTIFY_WAIT_MS   10000
#define BTLE_GATTC_AUTO_WRITE_VALUE "cmd:ping"

#define BTLE_GATTS_SERVICE_ID     1
#define BTLE_GATTS_NOTIFY_CHR_ID  2
#define BTLE_GATTS_NOTIFY_CCC_ID  3
#define BTLE_GATTS_RW_CHR_ID      4
#define BTLE_GATTS_INFO_CHR_ID    5

#define BTLE_UUID_AIC_TEST_SERVICE 0xff00
#define BTLE_UUID_AIC_TEST_NOTIFY  0xff01
#define BTLE_UUID_AIC_TEST_RW      0xff02
#define BTLE_UUID_AIC_TEST_INFO    0xff03
#define BTLE_UUID_PHONE_TEST_SERVICE 0xfff0
#define BTLE_UUID_PHONE_TEST_WRITE   0xfff1
#define BTLE_UUID_PHONE_TEST_NOTIFY  0xfff2
#define BTLE_UUID_PHONE_TEST_READ    0xfff3

#define BTLE_SCAN_MATCH_ALL     (1 << 0)
#define BTLE_SCAN_MATCH_NAME    (1 << 1)
#define BTLE_SCAN_MATCH_ADDR    (1 << 2)
#define BTLE_SCAN_MATCH_PAYLOAD (1 << 3)

struct btle_gatts_echo_item
{
  uint8_t value[BTLE_GATT_VALUE_MAX];
  uint16_t len;
};

struct btle_adv_summary
{
  char name[BTLE_SCAN_NAME_MAX];
  uint8_t adv_len;
  uint8_t adv_total_len;
  uint8_t adv_data[BTLE_SCAN_RAW_MAX];
  uint16_t uuid16[BTLE_SCAN_UUID16_MAX];
  uint8_t uuid16_count;
  uint16_t mfg_id;
  uint16_t service_data16;
  bool have_mfg_id;
  bool have_service_data16;
};

struct btle_scan_entry
{
  bt_address_t addr;
  uint8_t addr_type;
  uint8_t adv_type;
  int8_t rssi;
  int8_t best_rssi;
  unsigned int seen;
  uint8_t match_reasons;
  bool matched;
  bool connectable;
  bool scan_rsp_seen;
  struct btle_adv_summary adv;
};

struct btle_gattc_options
{
  unsigned int hold_seconds;
  unsigned int mtu;
  uint16_t read_handle;
  uint16_t write_handle;
  uint16_t subscribe_handle;
  const char *write_value;
  const char *auto_write_value;
  bool exchange_mtu;
  bool read;
  bool write;
  bool write_without_response;
  bool subscribe;
  bool auto_test;
};

static volatile bt_adapter_state_t g_btle_state = BT_ADAPTER_STATE_OFF;
static volatile bool g_btle_scan_started;
static volatile bool g_btle_scan_failed;
static volatile unsigned int g_btle_scan_results;
static volatile unsigned int g_btle_scan_rsp_results;
static volatile unsigned int g_btle_scan_name_hits;
static const char *g_btle_scan_filter;
static bool g_btle_scan_raw;
static bool g_btle_scan_list;
static struct btle_scan_entry g_btle_scan_cache[BTLE_SCAN_CACHE_MAX];
static unsigned int g_btle_scan_cache_count;
static unsigned int g_btle_scan_cache_dropped;
static bt_address_t g_btle_conn_target;
static volatile bool g_btle_conn_track;
static volatile bool g_btle_conn_connected;
static volatile bool g_btle_conn_disconnected;
static volatile connection_state_t g_btle_conn_state =
  CONNECTION_STATE_DISCONNECTED;
static char g_btle_gatt_value[BTLE_GATT_VALUE_MAX] = "AIC8800-Vela";
static uint8_t g_btle_gatt_value_len = 12;
static uint8_t g_btle_gatt_info_value[] = "AIC8800 Vela GATT";
static gatts_handle_t g_btle_gatts_handle;
static volatile bool g_btle_gatts_table_added;
static volatile bool g_btle_gatts_table_removed;
static volatile gatt_status_t g_btle_gatts_table_status;
static volatile uint16_t g_btle_gatts_table_handle;
static volatile bool g_btle_gatts_connected;
static volatile bool g_btle_gatts_disconnected;
static bt_address_t g_btle_gatts_peer;
static uint16_t g_btle_gatts_cccd;
static uint16_t g_btle_gatts_notify_payload =
  BTLE_GATT_NOTIFY_PAYLOAD_DEFAULT;
static volatile bool g_btle_gatts_notify_pending;
static unsigned int g_btle_gatts_notify_pending_polls;
static volatile unsigned int g_btle_gatts_notify_done;
static pthread_mutex_t g_btle_gatts_echo_lock = PTHREAD_MUTEX_INITIALIZER;
static struct btle_gatts_echo_item
  g_btle_gatts_echo_queue[BTLE_GATT_ECHO_QUEUE_DEPTH];
static uint8_t g_btle_gatts_echo_head;
static uint8_t g_btle_gatts_echo_tail;
static uint8_t g_btle_gatts_echo_count;
static unsigned int g_btle_gatts_echo_dropped;
static unsigned int g_btle_gatts_connect_total;
static unsigned int g_btle_gatts_read_total;
static unsigned int g_btle_gatts_write_total;
static unsigned int g_btle_gatts_ccc_total;
static unsigned int g_btle_gatts_cmd_total;
static unsigned int g_btle_gatts_notify_req_total;
static unsigned int g_btle_gatts_notify_ok_total;
static unsigned int g_btle_gatts_notify_fail_total;
static gattc_handle_t g_btle_gattc_handle;
static bt_address_t g_btle_gattc_peer;
static volatile bool g_btle_gattc_connected;
static volatile bool g_btle_gattc_disconnected;
static volatile bool g_btle_gattc_discover_done;
static volatile bool g_btle_gattc_read_done;
static volatile bool g_btle_gattc_write_done;
static volatile bool g_btle_gattc_subscribe_done;
static volatile bool g_btle_gattc_mtu_done;
static volatile unsigned int g_btle_gattc_notify_count;
static volatile gatt_status_t g_btle_gattc_last_status;
static unsigned int g_btle_gattc_discovered_attrs;
static uint16_t g_btle_gattc_auto_notify_handle;
static uint16_t g_btle_gattc_auto_rw_handle;
static uint16_t g_btle_gattc_auto_info_handle;
static uint32_t g_btle_gattc_auto_notify_props;
static uint32_t g_btle_gattc_auto_rw_props;
static uint32_t g_btle_gattc_auto_info_props;
static const char *g_btle_gattc_auto_profile;
static volatile sig_atomic_t g_btle_stop_requested;

static uint16_t btle_gatts_read_value(gatts_handle_t srv_handle,
                                      bt_address_t *addr,
                                      uint16_t attr_handle,
                                      uint32_t req_handle);
static uint16_t btle_gatts_write_value(gatts_handle_t srv_handle,
                                       bt_address_t *addr,
                                       uint16_t attr_handle,
                                       const uint8_t *value,
                                       uint16_t length,
                                       uint16_t offset);
static uint16_t btle_gatts_ccc_changed(gatts_handle_t srv_handle,
                                       bt_address_t *addr,
                                       uint16_t attr_handle,
                                       const uint8_t *value,
                                       uint16_t length,
                                       uint16_t offset);

static gatt_attr_db_t g_btle_gatts_attrs[] =
{
  GATT_H_PRIMARY_SERVICE(BT_UUID_DECLARE_16(0xff00),
                         BTLE_GATTS_SERVICE_ID),
  GATT_H_CHARACTERISTIC_AUTO_RSP(BT_UUID_DECLARE_16(0xff01),
                                 GATT_PROP_NOTIFY, 0, NULL, 0,
                                 BTLE_GATTS_NOTIFY_CHR_ID),
  GATT_H_CCCD(GATT_PERM_READ | GATT_PERM_WRITE, btle_gatts_ccc_changed,
              BTLE_GATTS_NOTIFY_CCC_ID),
  GATT_H_CHARACTERISTIC_USER_RSP(BT_UUID_DECLARE_16(0xff02),
                                 GATT_PROP_READ | GATT_PROP_WRITE |
                                 GATT_PROP_WRITE_NR,
                                 GATT_PERM_READ | GATT_PERM_WRITE,
                                 btle_gatts_read_value,
                                 btle_gatts_write_value,
                                 BTLE_GATTS_RW_CHR_ID),
  GATT_H_CHARACTERISTIC_AUTO_RSP(BT_UUID_DECLARE_16(0xff03),
                                 GATT_PROP_READ, GATT_PERM_READ,
                                 g_btle_gatt_info_value,
                                 sizeof(g_btle_gatt_info_value) - 1,
                                 BTLE_GATTS_INFO_CHR_ID),
};

static gatt_srv_db_t g_btle_gatts_service_db =
{
  .attr_num = sizeof(g_btle_gatts_attrs) / sizeof(g_btle_gatts_attrs[0]),
  .attr_db = g_btle_gatts_attrs,
};

static const char *btle_state_name(bt_adapter_state_t state)
{
  switch (state)
    {
      case BT_ADAPTER_STATE_OFF:
        return "OFF";

      case BT_ADAPTER_STATE_BLE_TURNING_ON:
        return "BLE_TURNING_ON";

      case BT_ADAPTER_STATE_BLE_ON:
        return "BLE_ON";

      case BT_ADAPTER_STATE_TURNING_ON:
        return "TURNING_ON";

      case BT_ADAPTER_STATE_ON:
        return "ON";

      case BT_ADAPTER_STATE_TURNING_OFF:
        return "TURNING_OFF";

      case BT_ADAPTER_STATE_BLE_TURNING_OFF:
        return "BLE_TURNING_OFF";

      default:
        return "UNKNOWN";
    }
}

static void btle_adapter_state_changed(void *cookie, bt_adapter_state_t state)
{
  (void)cookie;

  g_btle_state = state;
  printf("btle: adapter state=%d (%s)\n", state, btle_state_name(state));
}

static void btle_signal_stop(int signo)
{
  (void)signo;

  g_btle_stop_requested = 1;
}

static const char *btle_addr_type_name(ble_addr_type_t type)
{
  switch (type)
    {
      case BT_LE_ADDR_TYPE_PUBLIC:
        return "public";

      case BT_LE_ADDR_TYPE_RANDOM:
        return "random";

      case BT_LE_ADDR_TYPE_PUBLIC_ID:
        return "public-id";

      case BT_LE_ADDR_TYPE_RANDOM_ID:
        return "random-id";

      case BT_LE_ADDR_TYPE_ANONYMOUS:
        return "anonymous";

      case BT_LE_ADDR_TYPE_UNKNOWN:
        return "unknown";

      default:
        return "invalid";
    }
}

static const char *btle_transport_name(bt_transport_t transport)
{
  switch (transport)
    {
      case BT_TRANSPORT_BLE:
        return "LE";

      case BT_TRANSPORT_BREDR:
        return "BR/EDR";

      default:
        return "UNKNOWN";
    }
}

static const char *btle_conn_state_name(connection_state_t state)
{
  switch (state)
    {
      case CONNECTION_STATE_DISCONNECTED:
        return "DISCONNECTED";

      case CONNECTION_STATE_CONNECTING:
        return "CONNECTING";

      case CONNECTION_STATE_DISCONNECTING:
        return "DISCONNECTING";

      case CONNECTION_STATE_CONNECTED:
        return "CONNECTED";

      case CONNECTION_STATE_ENCRYPTED_BREDR:
        return "ENCRYPTED_BREDR";

      case CONNECTION_STATE_ENCRYPTED_LE:
        return "ENCRYPTED_LE";

      default:
        return "UNKNOWN";
    }
}

static void btle_connection_state_changed(void *cookie, bt_address_t *addr,
                                          bt_transport_t transport,
                                          connection_state_t state)
{
  char addr_str[BT_ADDR_STR_LENGTH];

  (void)cookie;

  if (addr == NULL)
    {
      return;
    }

  bt_addr_ba2str(addr, addr_str);
  printf("btle: connection addr=%s transport=%s state=%d (%s)\n",
         addr_str, btle_transport_name(transport), state,
         btle_conn_state_name(state));

  if (!g_btle_conn_track || transport != BT_TRANSPORT_BLE ||
      bt_addr_compare(&g_btle_conn_target, addr) != 0)
    {
      return;
    }

  g_btle_conn_state = state;
  if (state == CONNECTION_STATE_CONNECTED ||
      state == CONNECTION_STATE_ENCRYPTED_LE)
    {
      g_btle_conn_connected = true;
    }
  else if (state == CONNECTION_STATE_DISCONNECTED)
    {
      g_btle_conn_disconnected = true;
    }
}

static void btle_print_ascii_preview(const uint8_t *data, uint8_t len)
{
  uint8_t i;

  for (i = 0; i < len; i++)
    {
      uint8_t c = data[i];

        putchar(c >= 0x20 && c <= 0x7e ? c : '.');
    }
}

static void btle_print_addr(const char *prefix, const bt_address_t *addr)
{
  char addr_str[BT_ADDR_STR_LENGTH];

  if (addr == NULL)
    {
      printf("%s(null)", prefix);
      return;
    }

  bt_addr_ba2str(addr, addr_str);
  printf("%s%s", prefix, addr_str);
}

static int btle_wait_flag(volatile bool *flag, unsigned int timeout_ms)
{
  unsigned int waited = 0;

  while (waited <= timeout_ms)
    {
      if (*flag)
        {
          return 0;
        }

      usleep(BTLE_STATE_POLL_US);
      waited += BTLE_STATE_POLL_US / 1000;
    }

  return -1;
}

static int btle_wait_gattc_flag(volatile bool *flag, const char *op,
                                unsigned int timeout_ms)
{
  if (btle_wait_flag(flag, timeout_ms) == 0)
    {
      return 0;
    }

  printf("btle: gattc wait %s timeout\n", op);
  return -1;
}

static void btle_print_hex_bytes(const uint8_t *data, uint16_t len)
{
  uint16_t i;
  uint16_t dump_len = MIN(len, (uint16_t)32);

  printf(" hex=");
  for (i = 0; i < dump_len; i++)
    {
      printf("%02x", data[i]);
    }

  if (dump_len < len)
    {
      printf("...");
    }
}

static bool btle_uuid16_value(const bt_uuid_t *uuid, uint16_t *value)
{
  static const uint8_t bt_base_prefix[12] =
  {
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00
  };

  if (uuid == NULL || value == NULL)
    {
      return false;
    }

  if (uuid->type == BT_UUID16_TYPE)
    {
      *value = uuid->val.u16;
      return true;
    }

  if (uuid->type == BT_UUID128_TYPE &&
      memcmp(uuid->val.u128, bt_base_prefix, sizeof(bt_base_prefix)) == 0 &&
      uuid->val.u128[14] == 0 && uuid->val.u128[15] == 0)
    {
      *value = (uint16_t)(uuid->val.u128[12] |
                          (uuid->val.u128[13] << 8));
      return true;
    }

  return false;
}

static void btle_print_uuid_value(const bt_uuid_t *uuid)
{
  char uuid_str[BT_UUID_STR_LENGTH];
  uint16_t uuid16;

  if (uuid == NULL || uuid->type == 0)
    {
      printf("uuid=none");
      return;
    }

  if (bt_uuid_to_string(uuid, uuid_str, sizeof(uuid_str)) == 0)
    {
      printf("uuid=%s", uuid_str);
      if (btle_uuid16_value(uuid, &uuid16))
        {
          printf(" short=0x%04x", uuid16);
        }

      return;
    }

  if (uuid->type == BT_UUID16_TYPE)
    {
      printf("uuid=0x%04x", uuid->val.u16);
    }
  else if (uuid->type == BT_UUID32_TYPE)
    {
      printf("uuid=0x%08lx", (unsigned long)uuid->val.u32);
    }
  else
    {
      printf("uuid128");
    }
}

static const char *btle_gatt_attr_type_name(uint8_t type)
{
  switch (type)
    {
      case GATT_PRIMARY_SERVICE:
        return "primary";

      case GATT_SECONDARY_SERVICE:
        return "secondary";

      case GATT_INCLUDED_SERVICE:
        return "include";

      case GATT_CHARACTERISTIC:
        return "char";

      case GATT_DESCRIPTOR:
        return "desc";

      default:
        return "unknown";
    }
}

static void btle_gattc_print_props(uint32_t props)
{
  printf(" props=0x%lx", (unsigned long)props);
  if (props == 0)
    {
      return;
    }

  printf("(");
  if ((props & GATT_PROP_READ) != 0)
    {
      printf("R");
    }

  if ((props & GATT_PROP_WRITE_NR) != 0)
    {
      printf("Wn");
    }

  if ((props & GATT_PROP_WRITE) != 0)
    {
      printf("W");
    }

  if ((props & GATT_PROP_NOTIFY) != 0)
    {
      printf("N");
    }

  if ((props & GATT_PROP_INDICATE) != 0)
    {
      printf("I");
    }

  printf(")");
}

static void btle_gattc_set_auto_handle(uint16_t *handle, uint32_t *props,
                                       uint16_t attr_handle,
                                       uint32_t attr_props,
                                       const char *profile)
{
  if (*handle == 0)
    {
      *handle = attr_handle;
      *props = attr_props;
    }

  if (g_btle_gattc_auto_profile == NULL)
    {
      g_btle_gattc_auto_profile = profile;
    }
}

static void btle_gattc_note_attr(const gatt_attr_desc_t *attr)
{
  uint16_t uuid16;

  if (attr->type != GATT_CHARACTERISTIC ||
      !btle_uuid16_value(&attr->uuid, &uuid16))
    {
      return;
    }

  if (uuid16 == BTLE_UUID_AIC_TEST_NOTIFY &&
      (attr->properties & GATT_PROP_NOTIFY) != 0)
    {
      btle_gattc_set_auto_handle(&g_btle_gattc_auto_notify_handle,
                                 &g_btle_gattc_auto_notify_props,
                                 attr->handle, attr->properties,
                                 "aic-test");
    }
  else if (uuid16 == BTLE_UUID_AIC_TEST_RW &&
           (attr->properties &
            (GATT_PROP_READ | GATT_PROP_WRITE | GATT_PROP_WRITE_NR)) != 0)
    {
      btle_gattc_set_auto_handle(&g_btle_gattc_auto_rw_handle,
                                 &g_btle_gattc_auto_rw_props,
                                 attr->handle, attr->properties,
                                 "aic-test");
    }
  else if (uuid16 == BTLE_UUID_AIC_TEST_INFO &&
           (attr->properties & GATT_PROP_READ) != 0)
    {
      btle_gattc_set_auto_handle(&g_btle_gattc_auto_info_handle,
                                 &g_btle_gattc_auto_info_props,
                                 attr->handle, attr->properties,
                                 "aic-test");
    }
  else if (uuid16 == BTLE_UUID_PHONE_TEST_WRITE &&
           (attr->properties & (GATT_PROP_WRITE | GATT_PROP_WRITE_NR)) != 0)
    {
      btle_gattc_set_auto_handle(&g_btle_gattc_auto_rw_handle,
                                 &g_btle_gattc_auto_rw_props,
                                 attr->handle, attr->properties,
                                 "phone-test");
    }
  else if (uuid16 == BTLE_UUID_PHONE_TEST_NOTIFY &&
           (attr->properties & GATT_PROP_NOTIFY) != 0)
    {
      btle_gattc_set_auto_handle(&g_btle_gattc_auto_notify_handle,
                                 &g_btle_gattc_auto_notify_props,
                                 attr->handle, attr->properties,
                                 "phone-test");
    }
  else if (uuid16 == BTLE_UUID_PHONE_TEST_READ &&
           (attr->properties & GATT_PROP_READ) != 0)
    {
      btle_gattc_set_auto_handle(&g_btle_gattc_auto_info_handle,
                                 &g_btle_gattc_auto_info_props,
                                 attr->handle, attr->properties,
                                 "phone-test");
    }
}

static void btle_gattc_connected(gattc_handle_t conn_handle,
                                 bt_address_t *addr)
{
  g_btle_gattc_handle = conn_handle;
  if (addr != NULL)
    {
      g_btle_gattc_peer = *addr;
    }

  g_btle_gattc_connected = true;
  g_btle_gattc_disconnected = false;
  printf("btle: gattc connected peer=");
  btle_print_addr("", addr);
  printf("\n");
}

static void btle_gattc_disconnected(gattc_handle_t conn_handle,
                                    bt_address_t *addr)
{
  (void)conn_handle;

  g_btle_gattc_connected = false;
  g_btle_gattc_disconnected = true;
  printf("btle: gattc disconnected peer=");
  btle_print_addr("", addr);
  printf("\n");
}

static void btle_gattc_discovered(gattc_handle_t conn_handle,
                                  gatt_status_t status,
                                  bt_uuid_t *uuid,
                                  uint16_t start_handle,
                                  uint16_t end_handle)
{
  uint32_t handle;

  g_btle_gattc_last_status = status;
  if (status != GATT_STATUS_SUCCESS)
    {
      printf("btle: gattc discover status=%d\n", status);
      g_btle_gattc_discover_done = true;
      return;
    }

  if (uuid == NULL || uuid->type == 0)
    {
      printf("btle: gattc discover done attrs=%u auto notify=0x%04x "
             "rw=0x%04x info=0x%04x\n",
             g_btle_gattc_discovered_attrs,
             g_btle_gattc_auto_notify_handle,
             g_btle_gattc_auto_rw_handle,
             g_btle_gattc_auto_info_handle);
      g_btle_gattc_discover_done = true;
      return;
    }

  printf("btle: gattc service range=0x%04x-0x%04x ",
         start_handle, end_handle);
  btle_print_uuid_value(uuid);
  printf("\n");

  for (handle = start_handle; handle <= end_handle; handle++)
    {
      gatt_attr_desc_t attr;

      if (bt_gattc_get_attribute_by_handle(conn_handle, (uint16_t)handle,
                                           &attr) != BT_STATUS_SUCCESS)
        {
          continue;
        }

      g_btle_gattc_discovered_attrs++;
      btle_gattc_note_attr(&attr);
      printf("btle:   attr handle=0x%04x type=%s",
             attr.handle, btle_gatt_attr_type_name(attr.type));
      btle_gattc_print_props(attr.properties);
      printf(" ");
      btle_print_uuid_value(&attr.uuid);
      printf("\n");

      if (handle == 0xffff)
        {
          break;
        }
    }
}

static void btle_gattc_read(gattc_handle_t conn_handle,
                            gatt_status_t status,
                            uint16_t attr_handle,
                            uint8_t *value,
                            uint16_t length)
{
  (void)conn_handle;

  g_btle_gattc_last_status = status;
  g_btle_gattc_read_done = true;
  printf("btle: gattc read handle=0x%04x status=%d len=%u",
         attr_handle, status, length);
  if (status == GATT_STATUS_SUCCESS && value != NULL && length > 0)
    {
      uint8_t preview_len = (uint8_t)MIN(length, (uint16_t)255);

      btle_print_hex_bytes(value, length);
      printf(" ascii=\"");
      btle_print_ascii_preview(value, preview_len);
      printf("\"");
    }

  printf("\n");
}

static void btle_gattc_written(gattc_handle_t conn_handle,
                               gatt_status_t status,
                               uint16_t attr_handle)
{
  (void)conn_handle;

  g_btle_gattc_last_status = status;
  g_btle_gattc_write_done = true;
  printf("btle: gattc write handle=0x%04x status=%d\n",
         attr_handle, status);
}

static void btle_gattc_subscribed(gattc_handle_t conn_handle,
                                  gatt_status_t status,
                                  uint16_t attr_handle,
                                  bool enable)
{
  (void)conn_handle;

  g_btle_gattc_last_status = status;
  g_btle_gattc_subscribe_done = true;
  printf("btle: gattc subscribe handle=0x%04x status=%d enable=%u\n",
         attr_handle, status, enable ? 1 : 0);
}

static void btle_gattc_notified(gattc_handle_t conn_handle,
                                uint16_t attr_handle,
                                uint8_t *value,
                                uint16_t length)
{
  (void)conn_handle;

  g_btle_gattc_notify_count++;
  printf("btle: gattc notify handle=0x%04x len=%u count=%u",
         attr_handle, length, g_btle_gattc_notify_count);
  if (value != NULL && length > 0)
    {
      uint8_t preview_len = (uint8_t)MIN(length, (uint16_t)255);

      btle_print_hex_bytes(value, length);
      printf(" ascii=\"");
      btle_print_ascii_preview(value, preview_len);
      printf("\"");
    }

  printf("\n");
}

static void btle_gattc_mtu_updated(gattc_handle_t conn_handle,
                                   gatt_status_t status,
                                   uint32_t mtu)
{
  (void)conn_handle;

  g_btle_gattc_last_status = status;
  g_btle_gattc_mtu_done = true;
  printf("btle: gattc mtu status=%d mtu=%lu\n",
         status, (unsigned long)mtu);
}

static void btle_gattc_phy_read(gattc_handle_t conn_handle,
                                ble_phy_type_t tx_phy,
                                ble_phy_type_t rx_phy)
{
  (void)conn_handle;

  printf("btle: gattc phy tx=%d rx=%d\n", tx_phy, rx_phy);
}

static void btle_gattc_phy_updated(gattc_handle_t conn_handle,
                                   gatt_status_t status,
                                   ble_phy_type_t tx_phy,
                                   ble_phy_type_t rx_phy)
{
  (void)conn_handle;

  printf("btle: gattc phy updated status=%d tx=%d rx=%d\n",
         status, tx_phy, rx_phy);
}

static void btle_gattc_rssi_read(gattc_handle_t conn_handle,
                                 gatt_status_t status,
                                 int32_t rssi)
{
  (void)conn_handle;

  printf("btle: gattc rssi status=%d rssi=%ld\n",
         status, (long)rssi);
}

static void btle_gattc_conn_param_updated(gattc_handle_t conn_handle,
                                          bt_status_t status,
                                          uint16_t interval,
                                          uint16_t latency,
                                          uint16_t timeout)
{
  (void)conn_handle;

  printf("btle: gattc conn param status=%d interval=%u latency=%u "
         "timeout=%u\n", status, interval, latency, timeout);
}

static gattc_callbacks_t g_btle_gattc_cbs =
{
  .size = sizeof(g_btle_gattc_cbs),
  .on_connected = btle_gattc_connected,
  .on_disconnected = btle_gattc_disconnected,
  .on_discovered = btle_gattc_discovered,
  .on_read = btle_gattc_read,
  .on_written = btle_gattc_written,
  .on_subscribed = btle_gattc_subscribed,
  .on_notified = btle_gattc_notified,
  .on_mtu_updated = btle_gattc_mtu_updated,
  .on_phy_read = btle_gattc_phy_read,
  .on_phy_updated = btle_gattc_phy_updated,
  .on_rssi_read = btle_gattc_rssi_read,
  .on_conn_param_updated = btle_gattc_conn_param_updated,
};

static void btle_gatts_echo_reset(void)
{
  pthread_mutex_lock(&g_btle_gatts_echo_lock);
  g_btle_gatts_echo_head = 0;
  g_btle_gatts_echo_tail = 0;
  g_btle_gatts_echo_count = 0;
  g_btle_gatts_echo_dropped = 0;
  pthread_mutex_unlock(&g_btle_gatts_echo_lock);
}

static void btle_gatts_echo_snapshot(unsigned int *queued,
                                     unsigned int *dropped)
{
  pthread_mutex_lock(&g_btle_gatts_echo_lock);
  *queued = g_btle_gatts_echo_count;
  *dropped = g_btle_gatts_echo_dropped;
  pthread_mutex_unlock(&g_btle_gatts_echo_lock);
}

static void btle_gatts_stats_reset(void)
{
  g_btle_gatts_connect_total = 0;
  g_btle_gatts_read_total = 0;
  g_btle_gatts_write_total = 0;
  g_btle_gatts_ccc_total = 0;
  g_btle_gatts_cmd_total = 0;
  g_btle_gatts_notify_req_total = 0;
  g_btle_gatts_notify_ok_total = 0;
  g_btle_gatts_notify_fail_total = 0;
  g_btle_gatts_notify_done = 0;
  g_btle_gatts_notify_pending_polls = 0;
}

static void btle_gatts_print_summary(const char *tag)
{
  unsigned int queued;
  unsigned int dropped;

  btle_gatts_echo_snapshot(&queued, &dropped);
  printf("btle: gatts summary %s conn=%u read=%u write=%u ccc=%u "
         "cmd=%u notify-req=%u notify-ok=%u notify-fail=%u queued=%u "
         "dropped=%u ccc-value=0x%04x connected=%u\n",
         tag, g_btle_gatts_connect_total, g_btle_gatts_read_total,
         g_btle_gatts_write_total, g_btle_gatts_ccc_total,
         g_btle_gatts_cmd_total, g_btle_gatts_notify_req_total,
         g_btle_gatts_notify_ok_total, g_btle_gatts_notify_fail_total,
         queued, dropped, g_btle_gatts_cccd,
         g_btle_gatts_connected ? 1 : 0);
}

static uint16_t btle_gatts_notify_payload_max(void)
{
  uint16_t payload = g_btle_gatts_notify_payload;

  if (payload == 0)
    {
      payload = BTLE_GATT_NOTIFY_PAYLOAD_DEFAULT;
    }

  return MIN(payload, (uint16_t)BTLE_GATT_VALUE_MAX);
}

static void btle_gatts_echo_push_one(const uint8_t *value, uint16_t len,
                                     unsigned int *queued,
                                     unsigned int *dropped,
                                     bool *dropped_oldest)
{
  pthread_mutex_lock(&g_btle_gatts_echo_lock);
  *dropped_oldest = false;
  if (g_btle_gatts_echo_count >= BTLE_GATT_ECHO_QUEUE_DEPTH)
    {
      g_btle_gatts_echo_head =
        (g_btle_gatts_echo_head + 1) % BTLE_GATT_ECHO_QUEUE_DEPTH;
      g_btle_gatts_echo_count--;
      g_btle_gatts_echo_dropped++;
      *dropped_oldest = true;
    }

  memcpy(g_btle_gatts_echo_queue[g_btle_gatts_echo_tail].value, value, len);
  g_btle_gatts_echo_queue[g_btle_gatts_echo_tail].len = len;
  g_btle_gatts_echo_tail =
    (g_btle_gatts_echo_tail + 1) % BTLE_GATT_ECHO_QUEUE_DEPTH;
  g_btle_gatts_echo_count++;
  *queued = g_btle_gatts_echo_count;
  *dropped = g_btle_gatts_echo_dropped;
  pthread_mutex_unlock(&g_btle_gatts_echo_lock);
}

static void btle_gatts_echo_push(const uint8_t *value, uint16_t len,
                                 unsigned int *queued,
                                 unsigned int *dropped,
                                 bool *dropped_oldest,
                                 unsigned int *chunks)
{
  uint16_t payload_max = btle_gatts_notify_payload_max();
  uint16_t pos = 0;

  *chunks = 0;
  *dropped_oldest = false;
  while (pos < len)
    {
      bool chunk_dropped = false;
      uint16_t chunk_len = MIN((uint16_t)(len - pos), payload_max);

      btle_gatts_echo_push_one(value + pos, chunk_len, queued, dropped,
                               &chunk_dropped);
      *dropped_oldest = *dropped_oldest || chunk_dropped;
      (*chunks)++;
      pos += chunk_len;
    }
}

static bool btle_gatts_echo_pop_front(uint8_t *value, uint16_t *len,
                                      unsigned int *queued)
{
  bool available;

  pthread_mutex_lock(&g_btle_gatts_echo_lock);
  available = g_btle_gatts_echo_count > 0;
  if (available)
    {
      *len = g_btle_gatts_echo_queue[g_btle_gatts_echo_head].len;
      memcpy(value, g_btle_gatts_echo_queue[g_btle_gatts_echo_head].value,
             *len);
      g_btle_gatts_echo_head =
        (g_btle_gatts_echo_head + 1) % BTLE_GATT_ECHO_QUEUE_DEPTH;
      g_btle_gatts_echo_count--;
    }

  *queued = g_btle_gatts_echo_count;
  pthread_mutex_unlock(&g_btle_gatts_echo_lock);
  return available;
}

static uint16_t btle_gatts_format_response(uint8_t *response,
                                           const char *fmt, ...)
{
  va_list ap;
  int len;

  va_start(ap, fmt);
  len = vsnprintf((char *)response, BTLE_GATT_VALUE_MAX, fmt, ap);
  va_end(ap);

  if (len < 0)
    {
      response[0] = '\0';
      return 0;
    }

  if (len >= BTLE_GATT_VALUE_MAX)
    {
      len = BTLE_GATT_VALUE_MAX - 1;
    }

  return (uint16_t)len;
}

static bool btle_gatts_cmd_match(const char *cmd, size_t cmd_len,
                                 const char *word)
{
  size_t word_len = strlen(word);

  return cmd_len == word_len && memcmp(cmd, word, word_len) == 0;
}

static bool btle_gatts_build_cmd_response(const uint8_t *value,
                                          uint16_t value_len,
                                          uint8_t *response,
                                          uint16_t *response_len)
{
  const char *cmd;
  size_t cmd_len;
  unsigned int queued;
  unsigned int dropped;
  const size_t prefix_len = sizeof(BTLE_GATT_CMD_PREFIX) - 1;

  if (value_len < prefix_len ||
      memcmp(value, BTLE_GATT_CMD_PREFIX, prefix_len) != 0)
    {
      return false;
    }

  cmd = (const char *)value + prefix_len;
  cmd_len = value_len - prefix_len;
  while (cmd_len > 0 && (cmd[cmd_len - 1] == '\r' ||
                         cmd[cmd_len - 1] == '\n' ||
                         cmd[cmd_len - 1] == '\0'))
    {
      cmd_len--;
    }

  g_btle_gatts_cmd_total++;
  if (btle_gatts_cmd_match(cmd, cmd_len, "ping"))
    {
      *response_len = btle_gatts_format_response(response, "pong");
      return true;
    }

  if (btle_gatts_cmd_match(cmd, cmd_len, "info"))
    {
      *response_len = btle_gatts_format_response(response,
        "svc=ff00 rw=ff02");
      return true;
    }

  if (btle_gatts_cmd_match(cmd, cmd_len, "help"))
    {
      *response_len = btle_gatts_format_response(response,
        "ping/info/stats/echo");
      return true;
    }

  if (cmd_len >= 5 && memcmp(cmd, "echo ", 5) == 0)
    {
      uint16_t echo_len = MIN((uint16_t)(cmd_len - 5),
                              (uint16_t)BTLE_GATT_VALUE_MAX);

      if (echo_len == 0)
        {
          *response_len = btle_gatts_format_response(response,
                                                     "err:empty echo");
          return true;
        }

      memcpy(response, cmd + 5, echo_len);
      *response_len = echo_len;
      return true;
    }

  if (btle_gatts_cmd_match(cmd, cmd_len, "stats") ||
      btle_gatts_cmd_match(cmd, cmd_len, "status"))
    {
      btle_gatts_echo_snapshot(&queued, &dropped);
      *response_len = btle_gatts_format_response(response,
        "r%u w%u c%u ok%u f%u q%u d%u",
        g_btle_gatts_read_total, g_btle_gatts_write_total,
        g_btle_gatts_cmd_total, g_btle_gatts_notify_ok_total,
        g_btle_gatts_notify_fail_total, queued, dropped);
      return true;
    }

  *response_len = btle_gatts_format_response(response, "err:unknown cmd");
  return true;
}

static uint16_t btle_gatts_read_value(gatts_handle_t srv_handle,
                                      bt_address_t *addr,
                                      uint16_t attr_handle,
                                      uint32_t req_handle)
{
  bt_status_t status;

  g_btle_gatts_read_total++;
  status = bt_gatts_response(srv_handle, addr, req_handle,
                             (uint8_t *)g_btle_gatt_value,
                             g_btle_gatt_value_len);
  printf("btle: gatts read attr=%u len=%u status=%d peer=",
         attr_handle, g_btle_gatt_value_len, status);
  btle_print_addr("", addr);
  printf(" value=\"");
  btle_print_ascii_preview((const uint8_t *)g_btle_gatt_value,
                           g_btle_gatt_value_len);
  printf("\"\n");

  return status == BT_STATUS_SUCCESS ? 0 : 1;
}

static uint16_t btle_gatts_write_value(gatts_handle_t srv_handle,
                                       bt_address_t *addr,
                                       uint16_t attr_handle,
                                       const uint8_t *value,
                                       uint16_t length,
                                       uint16_t offset)
{
  uint16_t copy_len;
  unsigned int echo_dropped = 0;
  unsigned int echo_queued = 0;
  unsigned int echo_chunks = 0;
  bool echo_dropped_oldest = false;
  uint8_t response[BTLE_GATT_VALUE_MAX];
  const uint8_t *notify_value = value;
  uint16_t notify_len;
  uint16_t response_len;
  bool is_cmd = false;

  (void)srv_handle;

  if (offset >= BTLE_GATT_VALUE_MAX)
    {
      printf("btle: gatts write attr=%u rejected offset=%u len=%u\n",
             attr_handle, offset, length);
      return 0;
    }

  copy_len = MIN(length, (uint16_t)(BTLE_GATT_VALUE_MAX - offset));
  g_btle_gatts_write_total++;
  memcpy(&g_btle_gatt_value[offset], value, copy_len);
  if (offset == 0)
    {
      g_btle_gatt_value_len = copy_len;
    }
  else if ((uint16_t)offset + copy_len > g_btle_gatt_value_len)
    {
      g_btle_gatt_value_len = offset + copy_len;
    }

  printf("btle: gatts write attr=%u offset=%u len=%u copied=%u peer=",
         attr_handle, offset, length, copy_len);
  btle_print_addr("", addr);
  printf(" value=\"");
  btle_print_ascii_preview(value, copy_len);
  printf("\"");
  if (copy_len != length)
    {
      printf(" truncated=1");
    }

  if (copy_len > 0)
    {
      notify_len = copy_len;
      if (offset == 0 &&
          btle_gatts_build_cmd_response(value, copy_len, response,
                                        &response_len))
        {
          notify_value = response;
          notify_len = response_len;
          is_cmd = true;
        }

      btle_gatts_echo_push(notify_value, notify_len, &echo_queued,
                           &echo_dropped,
                           &echo_dropped_oldest,
                           &echo_chunks);
      printf(" %s-queued=%u", is_cmd ? "cmd" : "echo", echo_queued);
      if (echo_chunks > 1)
        {
          printf(" tx-chunks=%u mtu-payload=%u", echo_chunks,
                 btle_gatts_notify_payload_max());
        }

      if (echo_dropped_oldest)
        {
          printf(" tx-drop-oldest=1 tx-dropped=%u", echo_dropped);
        }
    }

  printf("\n");
  return copy_len;
}

static uint16_t btle_gatts_ccc_changed(gatts_handle_t srv_handle,
                                       bt_address_t *addr,
                                       uint16_t attr_handle,
                                       const uint8_t *value,
                                       uint16_t length,
                                       uint16_t offset)
{
  (void)srv_handle;
  (void)offset;

  g_btle_gatts_ccc_total++;
  g_btle_gatts_cccd = 0;
  if (length >= 2)
    {
      g_btle_gatts_cccd = value[0] | ((uint16_t)value[1] << 8);
    }
  else if (length == 1)
    {
      g_btle_gatts_cccd = value[0];
    }

  printf("btle: gatts ccc attr=%u value=0x%04x peer=", attr_handle,
         g_btle_gatts_cccd);
  btle_print_addr("", addr);
  if ((g_btle_gatts_cccd & GATT_CCC_NOTIFY) == 0)
    {
      if (g_btle_gatts_notify_pending)
        {
          printf(" notify-cancel=1");
        }

      g_btle_gatts_notify_pending = false;
      g_btle_gatts_notify_pending_polls = 0;
      btle_gatts_echo_reset();
    }

  printf("\n");
  return length;
}

static void btle_gatts_connected(gatts_handle_t srv_handle,
                                 bt_address_t *addr)
{
  (void)srv_handle;

  g_btle_gatts_peer = *addr;
  g_btle_gatts_connected = true;
  g_btle_gatts_disconnected = false;
  g_btle_gatts_cccd = 0;
  g_btle_gatts_notify_pending = false;
  g_btle_gatts_notify_pending_polls = 0;
  btle_gatts_stats_reset();
  g_btle_gatts_connect_total = 1;
  btle_gatts_echo_reset();
  printf("btle: gatts connected peer=");
  btle_print_addr("", addr);
  printf("\n");
}

static void btle_gatts_disconnected(gatts_handle_t srv_handle,
                                    bt_address_t *addr)
{
  (void)srv_handle;

  btle_gatts_print_summary("disconnect");
  g_btle_gatts_connected = false;
  g_btle_gatts_disconnected = true;
  g_btle_gatts_cccd = 0;
  g_btle_gatts_notify_pending = false;
  g_btle_gatts_notify_pending_polls = 0;
  btle_gatts_echo_reset();
  printf("btle: gatts disconnected peer=");
  btle_print_addr("", addr);
  printf("\n");
}

static void btle_gatts_table_added(gatts_handle_t srv_handle,
                                   gatt_status_t status,
                                   uint16_t attr_handle)
{
  (void)srv_handle;

  g_btle_gatts_table_status = status;
  g_btle_gatts_table_handle = attr_handle;
  g_btle_gatts_table_added = true;
  printf("btle: gatts table added handle=%u status=%d\n",
         attr_handle, status);
}

static void btle_gatts_table_removed(gatts_handle_t srv_handle,
                                     gatt_status_t status,
                                     uint16_t attr_handle)
{
  (void)srv_handle;

  g_btle_gatts_table_status = status;
  g_btle_gatts_table_handle = attr_handle;
  g_btle_gatts_table_removed = true;
  printf("btle: gatts table removed handle=%u status=%d\n",
         attr_handle, status);
}

static void btle_gatts_notify_complete(gatts_handle_t srv_handle,
                                       bt_address_t *addr,
                                       gatt_status_t status,
                                       uint16_t attr_handle)
{
  (void)srv_handle;

  g_btle_gatts_notify_pending = false;
  g_btle_gatts_notify_pending_polls = 0;
  if (status == GATT_STATUS_SUCCESS)
    {
      g_btle_gatts_notify_done++;
      g_btle_gatts_notify_ok_total++;
    }
  else
    {
      g_btle_gatts_notify_fail_total++;
    }

  printf("btle: gatts notify complete attr=%u status=%d peer=",
         attr_handle, status);
  btle_print_addr("", addr);
  printf(" ok=%u fail=%u done=%u\n", g_btle_gatts_notify_ok_total,
         g_btle_gatts_notify_fail_total, g_btle_gatts_notify_done);
}

static void btle_gatts_mtu_changed(gatts_handle_t srv_handle,
                                   bt_address_t *addr,
                                   uint32_t mtu)
{
  (void)srv_handle;

  if (mtu > 0)
    {
      if (mtu > 3)
        {
          mtu -= 3;
        }

      g_btle_gatts_notify_payload =
        MIN((uint16_t)mtu, (uint16_t)BTLE_GATT_VALUE_MAX);
    }

  printf("btle: gatts mtu-payload=%u peer=",
         g_btle_gatts_notify_payload);
  btle_print_addr("", addr);
  printf("\n");
}

static gatts_callbacks_t g_btle_gatts_cbs =
{
  .size = sizeof(g_btle_gatts_cbs),
  .on_connected = btle_gatts_connected,
  .on_disconnected = btle_gatts_disconnected,
  .on_attr_table_added = btle_gatts_table_added,
  .on_attr_table_removed = btle_gatts_table_removed,
  .on_notify_complete = btle_gatts_notify_complete,
  .on_mtu_changed = btle_gatts_mtu_changed,
};

static int btle_gatts_register_test_service(bt_instance_t *ins,
                                            gatts_handle_t *handle)
{
  *handle = NULL;
  g_btle_gatts_handle = NULL;
  g_btle_gatts_connected = false;
  g_btle_gatts_disconnected = false;
  g_btle_gatts_cccd = 0;
  g_btle_gatts_notify_pending = false;
  g_btle_gatts_notify_pending_polls = 0;
  btle_gatts_stats_reset();
  g_btle_gatts_notify_payload = BTLE_GATT_NOTIFY_PAYLOAD_DEFAULT;
  btle_gatts_echo_reset();

  if (aic_ble_gatts_register_table(ins, handle, &g_btle_gatts_cbs,
                                   &g_btle_gatts_service_db,
                                   &g_btle_gatts_table_added,
                                   &g_btle_gatts_table_removed,
                                   &g_btle_gatts_table_status,
                                   &g_btle_gatts_table_handle,
                                   "service=0xff00 notify=0xff01 "
                                   "rw=0xff02 info=0xff03",
                                   1000) < 0)
    {
      g_btle_gatts_handle = NULL;
      return -1;
    }

  g_btle_gatts_handle = *handle;
  return 0;
}

static void btle_gatts_unregister_test_service(gatts_handle_t *handle,
                                               bool cleanup)
{
  if (*handle == NULL)
    {
      return;
    }

  btle_gatts_echo_reset();
  aic_ble_gatts_unregister_table(handle, cleanup,
                                 &g_btle_gatts_table_removed,
                                 &g_btle_gatts_table_handle,
                                 &g_btle_gatts_notify_pending,
                                 1000);
  g_btle_gatts_handle = NULL;
}

static void btle_gatts_disconnect_if_connected(bt_instance_t *ins)
{
  bt_status_t status;

  if (!g_btle_gatts_connected)
    {
      return;
    }

  g_btle_gatts_disconnected = false;
  status = bt_device_disconnect_le(ins, &g_btle_gatts_peer);
  printf("btle: gatts disconnect_le status=%d peer=", status);
  btle_print_addr("", &g_btle_gatts_peer);
  printf("\n");
  if (status == BT_STATUS_SUCCESS || status == BT_STATUS_DONE)
    {
      if (btle_wait_flag(&g_btle_gatts_disconnected,
                         BTLE_DISCONNECT_WAIT_MS) < 0)
        {
          printf("btle: gatts disconnect wait timeout; continue cleanup\n");
        }
    }

  g_btle_gatts_connected = false;
  g_btle_gatts_notify_pending = false;
  g_btle_gatts_notify_pending_polls = 0;
  btle_gatts_echo_reset();
  g_btle_gatts_cccd = 0;
}

static void btle_gatts_check_notify_timeout(void)
{
  if (!g_btle_gatts_notify_pending)
    {
      g_btle_gatts_notify_pending_polls = 0;
      return;
    }

  g_btle_gatts_notify_pending_polls++;
  if (g_btle_gatts_notify_pending_polls >= BTLE_GATT_NOTIFY_TIMEOUT_POLLS)
    {
      g_btle_gatts_notify_pending = false;
      g_btle_gatts_notify_pending_polls = 0;
      g_btle_gatts_notify_fail_total++;
      printf("btle: gatts notify pending timeout; clear pending fail=%u\n",
             g_btle_gatts_notify_fail_total);
    }
}

static void btle_gatts_maybe_echo(gatts_handle_t handle)
{
  uint8_t echo_value[BTLE_GATT_VALUE_MAX];
  uint16_t echo_len;
  unsigned int echo_queued;
  bt_status_t status;

  if (!g_btle_gatts_connected ||
      (g_btle_gatts_cccd & GATT_CCC_NOTIFY) == 0 ||
      g_btle_gatts_notify_pending ||
      !btle_gatts_echo_pop_front(echo_value, &echo_len, &echo_queued))
    {
      return;
    }

  status = bt_gatts_notify(handle, &g_btle_gatts_peer,
                           BTLE_GATTS_NOTIFY_CHR_ID,
                           echo_value, echo_len);
  g_btle_gatts_notify_req_total++;
  if (status == BT_STATUS_SUCCESS)
    {
      g_btle_gatts_notify_pending = true;
      g_btle_gatts_notify_pending_polls = 0;
    }
  else
    {
      g_btle_gatts_notify_fail_total++;
    }

  printf("btle: gatts tx notify len=%u status=%d pending=%u queued=%u "
         "%svalue=\"", echo_len, status,
         g_btle_gatts_notify_pending ? 1 : 0, echo_queued,
         status == BT_STATUS_SUCCESS ? "" : "dropped=1 ");
  btle_print_ascii_preview(echo_value, echo_len);
  printf("\"\n");
}

static void btle_gatts_maybe_notify(gatts_handle_t handle,
                                    unsigned int notify_max,
                                    unsigned int *notify_seq,
                                    bool *notify_disabled_reported,
                                    bool *notify_limit_reported)
{
  char payload[32];
  unsigned int next_seq;
  int len;
  bt_status_t status;

  if (!g_btle_gatts_connected ||
      (g_btle_gatts_cccd & GATT_CCC_NOTIFY) == 0)
    {
      return;
    }

  if (notify_max == 0)
    {
      if (!*notify_disabled_reported)
        {
          printf("btle: gatts notify disabled by default; pass "
                 "notify-count > 0 to test 0xff01 notifications\n");
          *notify_disabled_reported = true;
        }

      return;
    }

  if (*notify_seq >= notify_max)
    {
      if (!*notify_limit_reported)
        {
          printf("btle: gatts notify limit reached max=%u done=%u\n",
                 notify_max, g_btle_gatts_notify_done);
          *notify_limit_reported = true;
        }

      return;
    }

  if (g_btle_gatts_notify_pending)
    {
      printf("btle: gatts notify skip pending done=%u\n",
             g_btle_gatts_notify_done);
      return;
    }

  next_seq = *notify_seq + 1;
  len = snprintf(payload, sizeof(payload), "notify-%u", next_seq);
  status = bt_gatts_notify(handle, &g_btle_gatts_peer,
                           BTLE_GATTS_NOTIFY_CHR_ID,
                           (uint8_t *)payload,
                           (uint16_t)len);
  g_btle_gatts_notify_req_total++;
  if (status == BT_STATUS_SUCCESS)
    {
      *notify_seq = next_seq;
      g_btle_gatts_notify_pending = true;
      g_btle_gatts_notify_pending_polls = 0;
    }
  else
    {
      g_btle_gatts_notify_fail_total++;
    }

  printf("btle: gatts notify seq=%u status=%d pending=%u done=%u\n",
         next_seq, status, g_btle_gatts_notify_pending ? 1 : 0,
         g_btle_gatts_notify_done);
}

static void btle_gatts_poll(gatts_handle_t handle, unsigned int notify_max,
                            unsigned int *notify_seq,
                            unsigned int *notify_polls,
                            bool *notify_disabled_reported,
                            bool *notify_limit_reported)
{
  btle_gatts_check_notify_timeout();
  btle_gatts_maybe_echo(handle);

  if (g_btle_gatts_connected &&
      (g_btle_gatts_cccd & GATT_CCC_NOTIFY) != 0)
    {
      (*notify_polls)++;
      if (*notify_polls >= BTLE_GATT_NOTIFY_POLLS)
        {
          *notify_polls = 0;
          btle_gatts_maybe_notify(handle, notify_max, notify_seq,
                                  notify_disabled_reported,
                                  notify_limit_reported);
        }
    }
  else
    {
      *notify_polls = 0;
    }
}

static void btle_gatts_run(gatts_handle_t handle, unsigned int seconds,
                           unsigned int notify_max)
{
  unsigned int elapsed;
  unsigned int poll;
  unsigned int notify_polls = 0;
  unsigned int notify_seq = 0;
  bool notify_disabled_reported = false;
  bool notify_limit_reported = false;

  for (elapsed = 0; elapsed < seconds; elapsed++)
    {
      for (poll = 0; poll < BTLE_GATT_POLLS_PER_SECOND; poll++)
        {
          if (g_btle_stop_requested)
            {
              printf("btle: stop requested; stop GATT loop\n");
              return;
            }

          usleep(BTLE_GATT_POLL_US);

          if (g_btle_stop_requested)
            {
              printf("btle: stop requested; stop GATT loop\n");
              return;
            }

          btle_gatts_poll(handle, notify_max, &notify_seq, &notify_polls,
                          &notify_disabled_reported,
                          &notify_limit_reported);
        }
    }
}

static void btle_gatts_run_connection(gatts_handle_t handle,
                                      unsigned int notify_max)
{
  unsigned int notify_polls = 0;
  unsigned int notify_seq = 0;
  bool notify_disabled_reported = false;
  bool notify_limit_reported = false;

  while (!g_btle_stop_requested && g_btle_gatts_connected)
    {
      usleep(BTLE_GATT_POLL_US);

      if (g_btle_stop_requested || !g_btle_gatts_connected)
        {
          break;
        }

      btle_gatts_poll(handle, notify_max, &notify_seq, &notify_polls,
                      &notify_disabled_reported,
                      &notify_limit_reported);
    }
}

static const adapter_callbacks_t g_btle_adapter_cbs =
{
  .on_adapter_state_changed = btle_adapter_state_changed,
  .on_connection_state_changed = btle_connection_state_changed,
};

static uint16_t btle_get_le16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void btle_copy_ascii(char *dst, size_t dst_size,
                            const uint8_t *src, uint8_t src_len)
{
  uint8_t copy_len;
  uint8_t i;

  if (dst_size == 0)
    {
      return;
    }

  copy_len = src_len;
  if (copy_len >= dst_size)
    {
      copy_len = dst_size - 1;
    }

  for (i = 0; i < copy_len; i++)
    {
      uint8_t c = src[i];

      dst[i] = c >= 0x20 && c <= 0x7e ? (char)c : '.';
    }

  dst[copy_len] = '\0';
}

static void btle_parse_adv_summary(const uint8_t *data, uint8_t data_len,
                                   struct btle_adv_summary *summary)
{
  uint8_t pos = 0;
  uint8_t dump_len;

  memset(summary, 0, sizeof(*summary));

  dump_len = data_len > BTLE_SCAN_RAW_MAX ? BTLE_SCAN_RAW_MAX : data_len;
  summary->adv_total_len = data_len;
  summary->adv_len = dump_len;
  if (dump_len > 0)
    {
      memcpy(summary->adv_data, data, dump_len);
    }

  while (pos < data_len)
    {
      uint8_t field_len = data[pos++];
      uint8_t type;
      const uint8_t *field;
      uint8_t field_data_len;

      if (field_len == 0)
        {
          break;
        }

      if (pos + field_len > data_len)
        {
          break;
        }

      type = data[pos];
      field = &data[pos + 1];
      field_data_len = field_len > 0 ? field_len - 1 : 0;

      switch (type)
        {
          case 0x08:
          case 0x09:
            if (field_data_len > 0 &&
                (summary->name[0] == '\0' || type == 0x09))
              {
                btle_copy_ascii(summary->name, sizeof(summary->name),
                                field, field_data_len);
              }

            break;

          case 0x02:
          case 0x03:
            {
              uint8_t off;

              for (off = 0; off + 1 < field_data_len &&
                   summary->uuid16_count < BTLE_SCAN_UUID16_MAX; off += 2)
                {
                  summary->uuid16[summary->uuid16_count++] =
                    btle_get_le16(&field[off]);
                }
            }

            break;

          case 0x16:
            if (field_data_len >= 2)
              {
                summary->service_data16 = btle_get_le16(field);
                summary->have_service_data16 = true;
              }

            break;

          case 0xff:
            if (field_data_len >= 2)
              {
                summary->mfg_id = btle_get_le16(field);
                summary->have_mfg_id = true;
              }

            break;

          default:
            break;
        }

      pos += field_len;
    }
}

static const char *btle_adv_type_name(uint8_t adv_type)
{
  switch (adv_type)
    {
      case BT_LE_ADV_IND:
        return "adv-ind";

      case BT_LE_ADV_DIRECT_IND:
        return "direct";

      case BT_LE_ADV_SCAN_IND:
        return "scan-ind";

      case BT_LE_ADV_NONCONN_IND:
        return "nonconn";

      case BT_LE_SCAN_RSP:
        return "scan-rsp";

      case BT_LE_LEGACY_ADV_IND:
        return "legacy-adv-ind";

      case BT_LE_LEGACY_ADV_DIRECT_IND:
        return "legacy-direct";

      case BT_LE_LEGACY_ADV_SCAN_IND:
        return "legacy-scan-ind";

      case BT_LE_LEGACY_ADV_NONCONN_IND:
        return "legacy-nonconn";

      case BT_LE_LEGACY_SCAN_RSP:
        return "legacy-scan-rsp";

      case BT_LE_EXT_ADV_IND:
        return "ext-adv-ind";

      case BT_LE_EXT_ADV_DIRECT_IND:
        return "ext-direct";

      case BT_LE_EXT_ADV_SCAN_IND:
        return "ext-scan-ind";

      case BT_LE_EXT_ADV_NONCONN_IND:
        return "ext-nonconn";

      case BT_LE_EXT_SCAN_RSP:
        return "ext-scan-rsp";

      default:
        return "unknown";
    }
}

static bool btle_adv_type_is_scan_rsp(uint8_t adv_type)
{
  switch (adv_type)
    {
      case BT_LE_SCAN_RSP:
      case BT_LE_LEGACY_SCAN_RSP:
      case BT_LE_EXT_SCAN_RSP:
        return true;

      default:
        return false;
    }
}

static char btle_ascii_tolower(char c)
{
  if (c >= 'A' && c <= 'Z')
    {
      return c - 'A' + 'a';
    }

  return c;
}

static bool btle_ascii_equal_nocase(char a, char b)
{
  return btle_ascii_tolower(a) == btle_ascii_tolower(b);
}

static bool btle_ascii_contains_nocase(const char *haystack,
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
          if (!btle_ascii_equal_nocase(haystack[i + j], needle[j]))
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

static int btle_hex_value(char c)
{
  c = btle_ascii_tolower(c);
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

static bool btle_addr_key_from_text(const char *text, char *key,
                                    size_t key_size)
{
  size_t out = 0;
  size_t i;

  if (text == NULL || text[0] == '\0' || key_size < BTLE_ADDR_KEY_MAX)
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

      if (btle_hex_value(c) < 0)
        {
          return false;
        }

      if (out >= BTLE_ADDR_KEY_LEN)
        {
          return false;
        }

      key[out++] = btle_ascii_tolower(c);
    }

  key[out] = '\0';
  return out >= 4;
}

static void btle_addr_key_reverse(const char *src, char *dst,
                                  size_t dst_size)
{
  size_t i;

  if (dst_size < BTLE_ADDR_KEY_MAX || strlen(src) != BTLE_ADDR_KEY_LEN)
    {
      dst[0] = '\0';
      return;
    }

  for (i = 0; i < BTLE_ADDR_KEY_LEN / 2; i++)
    {
      dst[i * 2] = src[BTLE_ADDR_KEY_LEN - 2 - i * 2];
      dst[i * 2 + 1] = src[BTLE_ADDR_KEY_LEN - 1 - i * 2];
    }

  dst[BTLE_ADDR_KEY_LEN] = '\0';
}

static bool btle_addr_filter_match(const char *filter, const char *addr)
{
  char filter_key[BTLE_ADDR_KEY_MAX];
  char addr_key[BTLE_ADDR_KEY_MAX];
  char addr_key_rev[BTLE_ADDR_KEY_MAX];

  if (!btle_addr_key_from_text(filter, filter_key, sizeof(filter_key)) ||
      !btle_addr_key_from_text(addr, addr_key, sizeof(addr_key)))
    {
      return false;
    }

  if (strstr(addr_key, filter_key) != NULL)
    {
      return true;
    }

  btle_addr_key_reverse(addr_key, addr_key_rev, sizeof(addr_key_rev));
  return addr_key_rev[0] != '\0' &&
         strstr(addr_key_rev, filter_key) != NULL;
}

static bool btle_payload_contains(const uint8_t *payload, uint8_t payload_len,
                                  const char *needle)
{
  size_t needle_len;
  size_t i;
  size_t j;

  if (needle == NULL || needle[0] == '\0')
    {
      return false;
    }

  needle_len = strlen(needle);
  if (needle_len == 0 || needle_len > payload_len)
    {
      return false;
    }

  for (i = 0; i + needle_len <= payload_len; i++)
    {
      for (j = 0; j < needle_len; j++)
        {
          if (!btle_ascii_equal_nocase((char)payload[i + j], needle[j]))
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

static uint8_t btle_scan_filter_match_reason(const char *filter,
                                             const char *addr,
                                             const struct btle_adv_summary
                                             *summary,
                                             const uint8_t *payload,
                                             uint8_t payload_len)
{
  uint8_t reason = 0;

  if (filter == NULL)
    {
      return BTLE_SCAN_MATCH_ALL;
    }

  if (summary->name[0] != '\0' &&
      btle_ascii_contains_nocase(summary->name, filter))
    {
      reason |= BTLE_SCAN_MATCH_NAME;
    }

  if (btle_addr_filter_match(filter, addr))
    {
      reason |= BTLE_SCAN_MATCH_ADDR;
    }

  if (btle_payload_contains(payload, payload_len, filter))
    {
      reason |= BTLE_SCAN_MATCH_PAYLOAD;
    }

  return reason;
}

static bool btle_adv_type_connectable(uint8_t adv_type)
{
  switch (adv_type)
    {
      case BT_LE_ADV_IND:
      case BT_LE_ADV_DIRECT_IND:
      case BT_LE_LEGACY_ADV_IND:
      case BT_LE_LEGACY_ADV_DIRECT_IND:
      case BT_LE_EXT_ADV_IND:
      case BT_LE_EXT_ADV_DIRECT_IND:
        return true;

      default:
        return false;
    }
}

static void btle_print_adv_summary(const struct btle_adv_summary *summary)
{
  uint8_t i;

  if (summary->name[0] != '\0')
    {
      printf(" name=\"%s\"", summary->name);
    }
  else
    {
      printf(" name=<none>");
    }

  if (summary->uuid16_count > 0)
    {
      printf(" uuid16=");
      for (i = 0; i < summary->uuid16_count; i++)
        {
          printf("%s0x%04x", i == 0 ? "" : ",", summary->uuid16[i]);
        }
    }

  if (summary->have_service_data16)
    {
      printf(" svcdata16=0x%04x", summary->service_data16);
    }

  if (summary->have_mfg_id)
    {
      printf(" mfg=0x%04x", summary->mfg_id);
    }
}

static void btle_print_adv_hex_summary(const struct btle_adv_summary *summary)
{
  uint8_t i;

  printf(" adv=");
  for (i = 0; i < summary->adv_len; i++)
    {
      printf("%02x", summary->adv_data[i]);
    }

  if (summary->adv_len < summary->adv_total_len)
    {
      printf("...");
    }
}

static void btle_print_adv_hex(const uint8_t *data, uint8_t data_len)
{
  uint8_t dump_len = data_len > BTLE_SCAN_RAW_MAX ? BTLE_SCAN_RAW_MAX :
                     data_len;
  uint8_t i;

  printf(" adv=");
  for (i = 0; i < dump_len; i++)
    {
      printf("%02x", data[i]);
    }

  if (dump_len < data_len)
    {
      printf("...");
    }
}

static void btle_print_match_reasons(uint8_t reasons)
{
  bool first = true;

  if (reasons == 0)
    {
      printf("none");
      return;
    }

  if ((reasons & BTLE_SCAN_MATCH_ALL) != 0)
    {
      printf("%sall", first ? "" : "|");
      first = false;
    }

  if ((reasons & BTLE_SCAN_MATCH_NAME) != 0)
    {
      printf("%sname", first ? "" : "|");
      first = false;
    }

  if ((reasons & BTLE_SCAN_MATCH_ADDR) != 0)
    {
      printf("%saddr", first ? "" : "|");
      first = false;
    }

  if ((reasons & BTLE_SCAN_MATCH_PAYLOAD) != 0)
    {
      printf("%spayload", first ? "" : "|");
    }
}

static void btle_scan_cache_reset(void)
{
  memset(g_btle_scan_cache, 0, sizeof(g_btle_scan_cache));
  g_btle_scan_cache_count = 0;
  g_btle_scan_cache_dropped = 0;
}

static int btle_scan_cache_find(const bt_address_t *addr, uint8_t addr_type)
{
  unsigned int i;

  for (i = 0; i < g_btle_scan_cache_count; i++)
    {
      if (g_btle_scan_cache[i].addr_type == addr_type &&
          bt_addr_compare(&g_btle_scan_cache[i].addr, addr) == 0)
        {
          return (int)i;
        }
    }

  return -1;
}

static struct btle_scan_entry *btle_scan_cache_alloc(uint8_t match_reasons,
                                                     int8_t rssi)
{
  struct btle_scan_entry *entry;
  unsigned int i;
  unsigned int replace = BTLE_SCAN_CACHE_MAX;
  int8_t worst_rssi = 127;
  bool replace_unmatched = false;

  if (g_btle_scan_cache_count < BTLE_SCAN_CACHE_MAX)
    {
      entry = &g_btle_scan_cache[g_btle_scan_cache_count++];
      memset(entry, 0, sizeof(*entry));
      return entry;
    }

  if (match_reasons == 0 && g_btle_scan_filter != NULL)
    {
      g_btle_scan_cache_dropped++;
      return NULL;
    }

  for (i = 0; i < g_btle_scan_cache_count; i++)
    {
      if (g_btle_scan_filter != NULL && !g_btle_scan_cache[i].matched)
        {
          replace = i;
          replace_unmatched = true;
          break;
        }

      if (g_btle_scan_cache[i].best_rssi < worst_rssi)
        {
          worst_rssi = g_btle_scan_cache[i].best_rssi;
          replace = i;
        }
    }

  if (replace >= g_btle_scan_cache_count ||
      (!replace_unmatched && rssi <= worst_rssi))
    {
      g_btle_scan_cache_dropped++;
      return NULL;
    }

  entry = &g_btle_scan_cache[replace];
  memset(entry, 0, sizeof(*entry));
  return entry;
}

static void btle_scan_cache_update(const ble_scan_result_t *result,
                                   const struct btle_adv_summary *summary,
                                   uint8_t match_reasons)
{
  struct btle_scan_entry *entry;
  bool matched = match_reasons != 0;
  int index;

  index = btle_scan_cache_find(&result->addr, result->addr_type);
  if (index < 0)
    {
      entry = btle_scan_cache_alloc(match_reasons, result->rssi);
      if (entry == NULL)
        {
          return;
        }

      entry->addr = result->addr;
      entry->addr_type = result->addr_type;
      entry->best_rssi = result->rssi;
    }
  else
    {
      entry = &g_btle_scan_cache[index];
      if (result->rssi > entry->best_rssi)
        {
          entry->best_rssi = result->rssi;
        }
    }

  entry->adv_type = result->adv_type;
  entry->rssi = result->rssi;
  entry->connectable = entry->connectable ||
                       btle_adv_type_connectable(result->adv_type);
  entry->matched = entry->matched || matched ||
                   g_btle_scan_filter == NULL;
  entry->match_reasons |= match_reasons;
  entry->scan_rsp_seen = entry->scan_rsp_seen ||
                         btle_adv_type_is_scan_rsp(result->adv_type);
  entry->seen++;
  if (summary->name[0] != '\0' || summary->uuid16_count > 0 ||
      summary->have_mfg_id || summary->have_service_data16 ||
      summary->adv_len > 0)
    {
      struct btle_adv_summary old = entry->adv;

      entry->adv = *summary;
      if (entry->adv.name[0] == '\0' && old.name[0] != '\0')
        {
          snprintf(entry->adv.name, sizeof(entry->adv.name), "%s",
                   old.name);
        }

      if (entry->adv.uuid16_count == 0 && old.uuid16_count > 0)
        {
          memcpy(entry->adv.uuid16, old.uuid16, sizeof(old.uuid16));
          entry->adv.uuid16_count = old.uuid16_count;
        }

      if (!entry->adv.have_service_data16 && old.have_service_data16)
        {
          entry->adv.service_data16 = old.service_data16;
          entry->adv.have_service_data16 = true;
        }

      if (!entry->adv.have_mfg_id && old.have_mfg_id)
        {
          entry->adv.mfg_id = old.mfg_id;
          entry->adv.have_mfg_id = true;
        }
    }
}

static void btle_scan_cache_print(void)
{
  unsigned int i;
  unsigned int visible = 0;
  unsigned int printed = 0;
  bool show_unmatched;

  for (i = 0; i < g_btle_scan_cache_count; i++)
    {
      if (g_btle_scan_filter == NULL || g_btle_scan_cache[i].matched)
        {
          visible++;
        }
    }

  printf("btle: scanlist unique=%u shown=%u reports=%u scan_rsp=%u "
         "name_hits=%u dropped=%u",
         g_btle_scan_cache_count, visible, g_btle_scan_results,
         g_btle_scan_rsp_results, g_btle_scan_name_hits,
         g_btle_scan_cache_dropped);
  if (g_btle_scan_filter != NULL)
    {
      printf(" filter=\"%s\"", g_btle_scan_filter);
    }

  printf("\n");
  show_unmatched = g_btle_scan_filter != NULL && visible == 0 &&
                   g_btle_scan_cache_count > 0;
  if (show_unmatched)
    {
      printf("btle: no filter match; showing up to %u unmatched devices "
             "for address diagnosis\n", BTLE_SCAN_UNMATCHED_MAX);
    }

  for (i = 0; i < g_btle_scan_cache_count; i++)
    {
      char addr[BT_ADDR_STR_LENGTH];
      struct btle_scan_entry *entry = &g_btle_scan_cache[i];

      if (g_btle_scan_filter != NULL && !entry->matched && !show_unmatched)
        {
          continue;
        }

      if (show_unmatched && printed >= BTLE_SCAN_UNMATCHED_MAX)
        {
          continue;
        }

      printed++;
      bt_addr_ba2str(&entry->addr, addr);
      printf("btle: dev[%u] addr=%s type=%u(%s) rssi=%d best=%d "
             "seen=%u adv_type=%u(%s) conn=%u scan_rsp=%u", i + 1, addr,
             entry->addr_type,
             btle_addr_type_name((ble_addr_type_t)entry->addr_type),
             entry->rssi, entry->best_rssi, entry->seen, entry->adv_type,
             btle_adv_type_name(entry->adv_type),
             entry->connectable ? 1 : 0, entry->scan_rsp_seen ? 1 : 0);
      if (g_btle_scan_filter != NULL)
        {
          printf(" match=");
          btle_print_match_reasons(entry->match_reasons);
        }

      btle_print_adv_summary(&entry->adv);
      btle_print_adv_hex_summary(&entry->adv);

      printf("\n");
    }

  if (visible > 0)
    {
      printf("btle: connect hint: btle connect <addr> "
             "<public|random|public-id|random-id> 10\n");
      printf("btle: identity hint: prefer a named target or fixed UUID; "
             "random unnamed addresses may be phone RPA and can change\n");
    }
}

static struct btle_scan_entry *btle_scan_cache_select_best(void)
{
  struct btle_scan_entry *best = NULL;
  unsigned int i;

  for (i = 0; i < g_btle_scan_cache_count; i++)
    {
      struct btle_scan_entry *entry = &g_btle_scan_cache[i];

      if (g_btle_scan_filter != NULL && !entry->matched)
        {
          continue;
        }

      if (!entry->connectable)
        {
          continue;
        }

      if (best == NULL || entry->best_rssi > best->best_rssi)
        {
          best = entry;
        }
    }

  return best;
}

static void btle_scan_result_cb(bt_scanner_t *scanner,
                                ble_scan_result_t *result)
{
  char addr[BT_ADDR_STR_LENGTH];
  struct btle_adv_summary summary;
  uint8_t match_reasons;
  bool matched;

  (void)scanner;

  bt_addr_ba2str(&result->addr, addr);
  btle_parse_adv_summary(result->adv_data, result->length, &summary);
  if (btle_adv_type_is_scan_rsp(result->adv_type))
    {
      g_btle_scan_rsp_results++;
    }

  if (summary.name[0] != '\0')
    {
      g_btle_scan_name_hits++;
    }

  match_reasons = btle_scan_filter_match_reason(g_btle_scan_filter, addr,
                                                &summary, result->adv_data,
                                                result->length);
  matched = match_reasons != 0;

  if (g_btle_scan_list)
    {
      g_btle_scan_results++;
      btle_scan_cache_update(result, &summary, match_reasons);
      return;
    }

  if (!matched && !g_btle_scan_raw)
    {
      return;
    }

  g_btle_scan_results++;
  printf("btle: scan[%u] addr=%s addr_type=%u(%s) rssi=%d "
         "type=%u(%s) conn=%u len=%u",
         g_btle_scan_results, addr, result->addr_type,
         btle_addr_type_name((ble_addr_type_t)result->addr_type),
         result->rssi, result->adv_type, btle_adv_type_name(result->adv_type),
         btle_adv_type_connectable(result->adv_type) ? 1 : 0,
         result->length);
  if (btle_adv_type_is_scan_rsp(result->adv_type))
    {
      printf(" scan_rsp=1");
    }
  btle_print_adv_summary(&summary);

  if (g_btle_scan_filter != NULL)
    {
      printf(" match=");
      btle_print_match_reasons(match_reasons);
    }

  if (g_btle_scan_raw)
    {
      btle_print_adv_hex(result->adv_data, result->length);
    }

  printf("\n");
}

static void btle_scan_start_cb(bt_scanner_t *scanner, uint8_t status)
{
  printf("btle: scan start scanner=%p status=%u\n", scanner, status);
  if (status == BT_SCAN_STATUS_SUCCESS)
    {
      g_btle_scan_started = true;
    }
  else
    {
      g_btle_scan_failed = true;
    }
}

static void btle_scan_stop_cb(bt_scanner_t *scanner)
{
  printf("btle: scan stopped scanner=%p results=%u\n", scanner,
         g_btle_scan_results);
}

static const scanner_callbacks_t g_btle_scan_cbs =
{
  .size = sizeof(g_btle_scan_cbs),
  .on_scan_result = btle_scan_result_cb,
  .on_scan_start_status = btle_scan_start_cb,
  .on_scan_stopped = btle_scan_stop_cb,
};

static bt_instance_t *btle_create_instance(void **adapter_cookie)
{
  bt_instance_t *ins;

  *adapter_cookie = NULL;
  ins = bluetooth_create_instance();
  if (ins == NULL)
    {
      printf("btle: bluetooth_create_instance failed; is bluetoothd "
             "running once?\n");
      printf("btle: normal startup: btstart; bluetoothd &; then run btle; "
             "repeated \"bluetoothd &\" can leave duplicate framework "
             "state\n");
      return NULL;
    }

  *adapter_cookie = bt_adapter_register_callback(ins, &g_btle_adapter_cbs);
  if (*adapter_cookie == NULL)
    {
      printf("btle: adapter callback register failed\n");
    }

  g_btle_state = bt_adapter_get_state(ins);
  printf("btle: current state=%d (%s)\n", g_btle_state,
         btle_state_name(g_btle_state));
  return ins;
}

static void btle_delete_instance(bt_instance_t *ins, void *adapter_cookie)
{
  if (ins == NULL)
    {
      return;
    }

  if (adapter_cookie != NULL)
    {
      bt_adapter_unregister_callback(ins, adapter_cookie);
    }

  bluetooth_delete_instance(ins);
}

static int btle_wait_state(bt_instance_t *ins, bt_adapter_state_t target,
                           unsigned int timeout_ms)
{
  unsigned int waited = 0;

  while (waited <= timeout_ms)
    {
      bt_adapter_state_t state = bt_adapter_get_state(ins);

      g_btle_state = state;
      if (state == target || (target == BT_ADAPTER_STATE_BLE_ON &&
                              state == BT_ADAPTER_STATE_ON))
        {
          printf("btle: reached state=%d (%s)\n", state,
                 btle_state_name(state));
          return 0;
        }

      usleep(BTLE_STATE_POLL_US);
      waited += BTLE_STATE_POLL_US / 1000;
    }

  printf("btle: wait state %d (%s) timeout, last=%d (%s)\n",
         target, btle_state_name(target), g_btle_state,
         btle_state_name(g_btle_state));
  return -1;
}

static int btle_wait_conn(bool wait_connected, unsigned int timeout_ms)
{
  unsigned int waited = 0;

  while (waited <= timeout_ms)
    {
      if (wait_connected)
        {
          if (g_btle_conn_connected)
            {
              return 0;
            }

          if (g_btle_conn_disconnected)
            {
              break;
            }
        }
      else if (g_btle_conn_disconnected)
        {
          return 0;
        }

      usleep(BTLE_STATE_POLL_US);
      waited += BTLE_STATE_POLL_US / 1000;
    }

  printf("btle: wait %s timeout, last state=%d (%s)\n",
         wait_connected ? "connect" : "disconnect", g_btle_conn_state,
         btle_conn_state_name(g_btle_conn_state));
  return -1;
}

static int btle_enable_instance(bt_instance_t *ins)
{
  bt_status_t status;
  bt_adapter_state_t state;

  state = bt_adapter_get_state(ins);
  if (state == BT_ADAPTER_STATE_BLE_ON || state == BT_ADAPTER_STATE_ON)
    {
      printf("btle: BLE already enabled\n");
      return 0;
    }

  status = bt_adapter_enable_le(ins);
  printf("btle: bt_adapter_enable_le status=%d\n", status);
  if (status != BT_STATUS_SUCCESS && status != BT_STATUS_DONE)
    {
      return -1;
    }

  return btle_wait_state(ins, BT_ADAPTER_STATE_BLE_ON, BTLE_STATE_WAIT_MS);
}

static void btle_cleanup_failed_enable(bt_instance_t *ins)
{
  bt_status_t status;

  status = bt_adapter_disable_le(ins);
  printf("btle: cleanup disable_le status=%d\n", status);
  btle_wait_state(ins, BT_ADAPTER_STATE_OFF, BTLE_DISABLE_WAIT_MS);
  printf("btle: keep bluetoothd/HCI alive for later BLE operations; "
         "use btstop --force-daemon only before reboot or diagnostics\n");
}

static int btle_parse_addr_type(const char *s, ble_addr_type_t *type)
{
  char *end;
  long value;

  if (strcmp(s, "public") == 0 || strcmp(s, "pub") == 0)
    {
      *type = BT_LE_ADDR_TYPE_PUBLIC;
      return 0;
    }

  if (strcmp(s, "random") == 0 || strcmp(s, "rand") == 0)
    {
      *type = BT_LE_ADDR_TYPE_RANDOM;
      return 0;
    }

  if (strcmp(s, "public-id") == 0 || strcmp(s, "public_id") == 0)
    {
      *type = BT_LE_ADDR_TYPE_PUBLIC_ID;
      return 0;
    }

  if (strcmp(s, "random-id") == 0 || strcmp(s, "random_id") == 0)
    {
      *type = BT_LE_ADDR_TYPE_RANDOM_ID;
      return 0;
    }

  value = strtol(s, &end, 0);
  if (*s != '\0' && *end == '\0' &&
      (value == BT_LE_ADDR_TYPE_PUBLIC ||
       value == BT_LE_ADDR_TYPE_RANDOM ||
       value == BT_LE_ADDR_TYPE_PUBLIC_ID ||
       value == BT_LE_ADDR_TYPE_RANDOM_ID))
    {
      *type = (ble_addr_type_t)value;
      return 0;
    }

  return -1;
}

static int btle_parse_uint(const char *s, unsigned int *value)
{
  char *end;
  unsigned long parsed;

  if (s == NULL || *s == '\0')
    {
      return -1;
    }

  errno = 0;
  parsed = strtoul(s, &end, 0);
  if (errno != 0 || *end != '\0' || parsed > UINT_MAX)
    {
      return -1;
    }

  *value = (unsigned int)parsed;
  return 0;
}

static int btle_parse_prefixed_uint(const char *s, const char *prefix,
                                    unsigned int *value)
{
  size_t prefix_len = strlen(prefix);

  if (strncmp(s, prefix, prefix_len) != 0)
    {
      return -1;
    }

  return btle_parse_uint(s + prefix_len, value);
}

static int btle_parse_handle_text(const char *s, uint16_t *handle)
{
  char *end;
  unsigned long parsed;

  if (s == NULL || *s == '\0')
    {
      return -1;
    }

  errno = 0;
  parsed = strtoul(s, &end, 0);
  if (errno != 0 || *end != '\0' || parsed == 0 || parsed > 0xffff)
    {
      return -1;
    }

  *handle = (uint16_t)parsed;
  return 0;
}

static int btle_parse_prefixed_handle(const char *s, const char *prefix,
                                      uint16_t *handle)
{
  size_t prefix_len = strlen(prefix);

  if (strncmp(s, prefix, prefix_len) != 0)
    {
      return -1;
    }

  return btle_parse_handle_text(s + prefix_len, handle);
}

static int btle_parse_prefixed_handle_value(const char *s,
                                            const char *prefix,
                                            uint16_t *handle,
                                            const char **value)
{
  char handle_text[16];
  const char *body;
  const char *sep;
  size_t prefix_len = strlen(prefix);
  size_t handle_len;

  if (strncmp(s, prefix, prefix_len) != 0)
    {
      return -1;
    }

  body = s + prefix_len;
  sep = strchr(body, ':');
  if (sep == NULL || sep == body)
    {
      return -1;
    }

  handle_len = (size_t)(sep - body);
  if (handle_len >= sizeof(handle_text))
    {
      return -1;
    }

  memcpy(handle_text, body, handle_len);
  handle_text[handle_len] = '\0';
  if (btle_parse_handle_text(handle_text, handle) < 0)
    {
      return -1;
    }

  *value = sep + 1;
  return **value == '\0' ? -1 : 0;
}

static int btle_parse_gattc_args(int argc, char *argv[],
                                 struct btle_gattc_options *opts)
{
  int i = 4;
  unsigned int parsed;
  bool explicit_auto = false;

  memset(opts, 0, sizeof(*opts));
  opts->hold_seconds = BTLE_CONNECT_SECONDS;
  opts->auto_test = true;
  opts->auto_write_value = BTLE_GATTC_AUTO_WRITE_VALUE;

  if (argc > i && btle_parse_uint(argv[i], &parsed) == 0)
    {
      opts->hold_seconds = parsed;
      i++;
    }

  for (; i < argc; i++)
    {
      const char *arg = argv[i];

      if (strcmp(arg, "auto") == 0)
        {
          opts->auto_test = true;
          explicit_auto = true;
          continue;
        }

      if (strcmp(arg, "no-auto") == 0 || strcmp(arg, "manual") == 0)
        {
          opts->auto_test = false;
          explicit_auto = true;
          continue;
        }

      if (btle_parse_prefixed_uint(arg, "mtu=", &parsed) == 0)
        {
          opts->exchange_mtu = true;
          opts->mtu = parsed;
          continue;
        }

      if (strncmp(arg, "auto-write=", 11) == 0 ||
          strncmp(arg, "value=", 6) == 0)
        {
          const char *value = strchr(arg, '=');

          if (value == NULL || value[1] == '\0')
            {
              printf("btle: empty gattc auto write value\n");
              return -1;
            }

          opts->auto_write_value = value + 1;
          opts->auto_test = true;
          explicit_auto = true;
          continue;
        }

      if (btle_parse_prefixed_handle(arg, "read=", &opts->read_handle) == 0)
        {
          opts->read = true;
          if (!explicit_auto)
            {
              opts->auto_test = false;
            }

          continue;
        }

      if (btle_parse_prefixed_handle(arg, "subscribe=",
                                    &opts->subscribe_handle) == 0 ||
          btle_parse_prefixed_handle(arg, "notify=",
                                    &opts->subscribe_handle) == 0)
        {
          opts->subscribe = true;
          if (!explicit_auto)
            {
              opts->auto_test = false;
            }

          continue;
        }

      if (btle_parse_prefixed_handle_value(arg, "write=",
                                          &opts->write_handle,
                                          &opts->write_value) == 0)
        {
          opts->write = true;
          opts->write_without_response = false;
          if (!explicit_auto)
            {
              opts->auto_test = false;
            }

          continue;
        }

      if (btle_parse_prefixed_handle_value(arg, "write-nr=",
                                          &opts->write_handle,
                                          &opts->write_value) == 0)
        {
          opts->write = true;
          opts->write_without_response = true;
          if (!explicit_auto)
            {
              opts->auto_test = false;
            }

          continue;
        }

      printf("btle: unknown gattc option \"%s\"\n", arg);
      return -1;
    }

  return 0;
}

static void btle_parse_gatt_args(int argc, char *argv[],
                                 unsigned int *seconds,
                                 const char **name,
                                 unsigned int *notify_max,
                                 bool *cleanup)
{
  int i;

  *seconds = BTLE_DEFAULT_SECONDS;
  *name = BTLE_DEFAULT_NAME;
  *notify_max = BTLE_GATT_NOTIFY_DEFAULT_MAX;
  *cleanup = BTLE_GATT_CLEANUP_DEFAULT;

  if (argc >= 3 && btle_parse_uint(argv[2], seconds) == 0 &&
      *seconds == 0)
    {
      *seconds = BTLE_DEFAULT_SECONDS;
    }

  for (i = 3; i < argc; i++)
    {
      unsigned int parsed;

      if (strcmp(argv[i], "cleanup") == 0 ||
          strcmp(argv[i], "unregister") == 0)
        {
          *cleanup = true;
          continue;
        }

      if (strcmp(argv[i], "nocleanup") == 0 ||
          strcmp(argv[i], "keep") == 0)
        {
          *cleanup = false;
          continue;
        }

      if (btle_parse_prefixed_uint(argv[i], "notify=", &parsed) == 0 ||
          btle_parse_prefixed_uint(argv[i], "notify-count=", &parsed) == 0)
        {
          *notify_max = parsed;
          continue;
        }

      if (btle_parse_uint(argv[i], &parsed) == 0)
        {
          *notify_max = parsed;
          continue;
        }

      *name = argv[i];
    }
}

static void btle_parse_gattserver_args(int argc, char *argv[],
                                       const char **name,
                                       unsigned int *notify_max,
                                       bool *cleanup)
{
  int i;

  *name = BTLE_DEFAULT_NAME;
  *notify_max = BTLE_GATT_NOTIFY_DEFAULT_MAX;
  *cleanup = BTLE_GATT_CLEANUP_DEFAULT;

  for (i = 2; i < argc; i++)
    {
      unsigned int parsed;

      if (strcmp(argv[i], "cleanup") == 0 ||
          strcmp(argv[i], "unregister") == 0)
        {
          *cleanup = true;
          continue;
        }

      if (strcmp(argv[i], "nocleanup") == 0 ||
          strcmp(argv[i], "keep") == 0)
        {
          *cleanup = false;
          continue;
        }

      if (btle_parse_prefixed_uint(argv[i], "notify=", &parsed) == 0 ||
          btle_parse_prefixed_uint(argv[i], "notify-count=", &parsed) == 0)
        {
          *notify_max = parsed;
          continue;
        }

      if (btle_parse_uint(argv[i], &parsed) == 0)
        {
          *notify_max = parsed;
          continue;
        }

      *name = argv[i];
    }
}

static int btle_cmd_enable(void)
{
  bt_instance_t *ins;
  void *adapter_cookie;
  int ret;

  ins = btle_create_instance(&adapter_cookie);
  if (ins == NULL)
    {
      return 1;
    }

  ret = btle_enable_instance(ins);
  if (ret < 0)
    {
      btle_cleanup_failed_enable(ins);
    }

  btle_delete_instance(ins, adapter_cookie);
  return ret == 0 ? 0 : 1;
}

static int btle_cmd_state(void)
{
  bt_instance_t *ins;
  void *adapter_cookie;
  bt_adapter_state_t state;
  bool le_enabled;

  ins = btle_create_instance(&adapter_cookie);
  if (ins == NULL)
    {
      return 1;
    }

  state = bt_adapter_get_state(ins);
  le_enabled = bt_adapter_is_le_enabled(ins);
  printf("btle: state=%d (%s), le_enabled=%d\n",
         state, btle_state_name(state), le_enabled);
  btle_delete_instance(ins, adapter_cookie);
  return 0;
}

static int btle_cmd_disable(void)
{
  bt_instance_t *ins;
  void *adapter_cookie;
  bt_status_t status;

  ins = btle_create_instance(&adapter_cookie);
  if (ins == NULL)
    {
      return 1;
    }

  status = bt_adapter_disable_le(ins);
  printf("btle: bt_adapter_disable_le status=%d\n", status);
  btle_wait_state(ins, BT_ADAPTER_STATE_OFF, BTLE_STATE_WAIT_MS);
  btle_delete_instance(ins, adapter_cookie);
  return status == BT_STATUS_SUCCESS || status == BT_STATUS_DONE ? 0 : 1;
}

static int btle_cmd_adv(unsigned int seconds, const char *name)
{
  bt_instance_t *ins;
  void *adapter_cookie;
  struct aic_ble_gap_adv adv;
  ble_adv_params_t params;
  uint8_t adv_data[AIC_BLE_GAP_ADV_DATA_MAX];
  uint8_t scan_rsp_data[AIC_BLE_GAP_ADV_DATA_MAX];
  uint16_t adv_len;
  uint16_t scan_rsp_len;
  int ret = 1;

  ins = btle_create_instance(&adapter_cookie);
  if (ins == NULL)
    {
      return 1;
    }

  if (btle_enable_instance(ins) < 0)
    {
      btle_cleanup_failed_enable(ins);
      goto out;
    }

  aic_ble_gap_default_adv_params(&params);
  adv_len = aic_ble_gap_fill_adv_data(adv_data, sizeof(adv_data), name,
                                      false, 0);
  scan_rsp_len = aic_ble_gap_fill_scan_rsp_data(scan_rsp_data,
                                                sizeof(scan_rsp_data), name);

  printf("btle: start legacy advertising name=\"%s\" seconds=%u\n",
         name, seconds);
  if (aic_ble_gap_start_advertising(ins, &adv, &params, adv_data, adv_len,
                                    scan_rsp_data, scan_rsp_len, 1000) < 0)
    {
      aic_ble_gap_stop_advertising(ins, &adv, 1000);
      goto out;
    }

  printf("btle: advertising handle=%p id=%u status=%u; scan now\n",
         adv.handle, adv.id, adv.status);
  sleep(seconds);
  ret = 0;

  aic_ble_gap_stop_advertising(ins, &adv, 1000);
  usleep(200000);

out:
  btle_delete_instance(ins, adapter_cookie);
  return ret;
}

static int btle_cmd_gatt(unsigned int seconds, const char *name,
                         unsigned int notify_max, bool cleanup)
{
  bt_instance_t *ins;
  void *adapter_cookie;
  struct aic_ble_gap_adv adv;
  gatts_handle_t gatts_handle = NULL;
  ble_adv_params_t params;
  uint8_t adv_data[AIC_BLE_GAP_ADV_DATA_MAX];
  uint8_t scan_rsp_data[AIC_BLE_GAP_ADV_DATA_MAX];
  uint16_t adv_len;
  uint16_t scan_rsp_len;
  void (*old_sigint)(int);
  int ret = 1;

  ins = btle_create_instance(&adapter_cookie);
  if (ins == NULL)
    {
      return 1;
    }

  if (btle_enable_instance(ins) < 0)
    {
      btle_cleanup_failed_enable(ins);
      goto out;
    }

  if (btle_gatts_register_test_service(ins, &gatts_handle) < 0)
    {
      goto out;
    }

  aic_ble_gap_default_adv_params(&params);
  adv_len = aic_ble_gap_fill_adv_data(adv_data, sizeof(adv_data), name,
                                      true, BTLE_UUID_AIC_TEST_SERVICE);
  scan_rsp_len = aic_ble_gap_fill_scan_rsp_data(scan_rsp_data,
                                                sizeof(scan_rsp_data), name);

  printf("btle: start GATT advertising name=\"%s\" service=0x%04x "
         "seconds=%u notify-max=%u cleanup=%u\n", name,
         BTLE_UUID_AIC_TEST_SERVICE, seconds, notify_max,
         cleanup ? 1 : 0);
  if (aic_ble_gap_start_advertising(ins, &adv, &params, adv_data, adv_len,
                                    scan_rsp_data, scan_rsp_len, 1000) < 0)
    {
      aic_ble_gap_stop_advertising(ins, &adv, 1000);
      goto out;
    }

  printf("btle: GATT advertising handle=%p id=%u status=%u; connect now\n",
         adv.handle, adv.id, adv.status);
  printf("btle: nRF service UUID=0xff00 notify=0xff01 rw=0xff02 "
         "info=0xff03\n");
  g_btle_stop_requested = 0;
  old_sigint = signal(SIGINT, btle_signal_stop);
  btle_gatts_run(gatts_handle, seconds, notify_max);
  signal(SIGINT, old_sigint);
  ret = 0;

  btle_gatts_disconnect_if_connected(ins);
  aic_ble_gap_stop_advertising(ins, &adv, 1000);
  usleep(200000);

out:
  btle_gatts_unregister_test_service(&gatts_handle, cleanup);
  btle_delete_instance(ins, adapter_cookie);
  return ret;
}

static int btle_cmd_gattserver(const char *name, unsigned int notify_max,
                               bool cleanup)
{
  bt_instance_t *ins;
  void *adapter_cookie;
  struct aic_ble_gap_adv adv;
  gatts_handle_t gatts_handle = NULL;
  ble_adv_params_t params;
  uint8_t adv_data[AIC_BLE_GAP_ADV_DATA_MAX];
  uint8_t scan_rsp_data[AIC_BLE_GAP_ADV_DATA_MAX];
  uint16_t adv_len;
  uint16_t scan_rsp_len;
  void (*old_sigint)(int);
  int ret = 1;

  ins = btle_create_instance(&adapter_cookie);
  if (ins == NULL)
    {
      return 1;
    }

  if (btle_enable_instance(ins) < 0)
    {
      btle_cleanup_failed_enable(ins);
      goto out;
    }

  if (btle_gatts_register_test_service(ins, &gatts_handle) < 0)
    {
      goto out;
    }

  aic_ble_gap_default_adv_params(&params);
  adv_len = aic_ble_gap_fill_adv_data(adv_data, sizeof(adv_data), name,
                                      true, BTLE_UUID_AIC_TEST_SERVICE);
  scan_rsp_len = aic_ble_gap_fill_scan_rsp_data(scan_rsp_data,
                                                sizeof(scan_rsp_data), name);
  printf("btle: start persistent GATT server name=\"%s\" service=0x%04x "
         "notify-max=%u cleanup=%u\n", name, BTLE_UUID_AIC_TEST_SERVICE,
         notify_max, cleanup ? 1 : 0);
  printf("btle: nRF service UUID=0xff00 notify=0xff01 rw=0xff02 "
         "info=0xff03; press Ctrl+C to stop\n");

  g_btle_stop_requested = 0;
  old_sigint = signal(SIGINT, btle_signal_stop);

  aic_ble_gap_adv_reset(&adv);
  while (!g_btle_stop_requested)
    {
      g_btle_gatts_disconnected = false;

      if (aic_ble_gap_start_advertising(ins, &adv, &params, adv_data,
                                        adv_len, scan_rsp_data,
                                        scan_rsp_len, 1000) < 0)
        {
          printf("btle: gattserver advertising failed status=%u\n",
                 adv.status);
          break;
        }

      printf("btle: gattserver advertising handle=%p id=%u; connect now\n",
             adv.handle, adv.id);
      ret = 0;

      while (!g_btle_stop_requested && !g_btle_gatts_connected)
        {
          sleep(1);
        }

      if (g_btle_stop_requested)
        {
          break;
        }

      printf("btle: gattserver connected; wait for disconnect or Ctrl+C\n");
      btle_gatts_run_connection(gatts_handle, notify_max);

      if (g_btle_stop_requested)
        {
          break;
        }

      printf("btle: gattserver peer disconnected; restart advertising\n");
      aic_ble_gap_stop_advertising(ins, &adv, 1000);
      usleep(200000);
    }

  signal(SIGINT, old_sigint);

  if (g_btle_gatts_connected)
    {
      btle_gatts_disconnect_if_connected(ins);
    }

  aic_ble_gap_stop_advertising(ins, &adv, 1000);
  usleep(200000);

out:
  btle_gatts_unregister_test_service(&gatts_handle, cleanup);
  btle_delete_instance(ins, adapter_cookie);
  return ret;
}

static void btle_gattc_reset(void)
{
  g_btle_gattc_handle = NULL;
  g_btle_gattc_connected = false;
  g_btle_gattc_disconnected = false;
  g_btle_gattc_discover_done = false;
  g_btle_gattc_read_done = false;
  g_btle_gattc_write_done = false;
  g_btle_gattc_subscribe_done = false;
  g_btle_gattc_mtu_done = false;
  g_btle_gattc_notify_count = 0;
  g_btle_gattc_last_status = GATT_STATUS_FAILURE;
  g_btle_gattc_discovered_attrs = 0;
  g_btle_gattc_auto_notify_handle = 0;
  g_btle_gattc_auto_rw_handle = 0;
  g_btle_gattc_auto_info_handle = 0;
  g_btle_gattc_auto_notify_props = 0;
  g_btle_gattc_auto_rw_props = 0;
  g_btle_gattc_auto_info_props = 0;
  g_btle_gattc_auto_profile = NULL;
}

static int btle_gattc_read_handle(gattc_handle_t handle, uint16_t attr)
{
  bt_status_t status;

  g_btle_gattc_read_done = false;
  g_btle_gattc_last_status = GATT_STATUS_FAILURE;
  status = bt_gattc_read(handle, attr);
  printf("btle: gattc read request handle=0x%04x status=%d\n",
         attr, status);
  if (status != BT_STATUS_SUCCESS)
    {
      return -1;
    }

  if (btle_wait_gattc_flag(&g_btle_gattc_read_done, "read",
                           BTLE_GATTC_OP_WAIT_MS) < 0)
    {
      return -1;
    }

  return g_btle_gattc_last_status == GATT_STATUS_SUCCESS ? 0 : -1;
}

static int btle_gattc_write_handle(gattc_handle_t handle, uint16_t attr,
                                   const char *value, bool no_response)
{
  bt_status_t status;
  uint16_t len;

  len = (uint16_t)MIN(strlen(value), (size_t)BTLE_GATT_VALUE_MAX);
  if (no_response)
    {
      status = bt_gattc_write_without_response(handle, attr,
                                               (uint8_t *)value, len);
    }
  else
    {
      g_btle_gattc_write_done = false;
      g_btle_gattc_last_status = GATT_STATUS_FAILURE;
      status = bt_gattc_write(handle, attr, (uint8_t *)value, len);
    }

  printf("btle: gattc write%s request handle=0x%04x len=%u status=%d "
         "value=\"",
         no_response ? "-nr" : "", attr, len, status);
  btle_print_ascii_preview((const uint8_t *)value, (uint8_t)len);
  printf("\"\n");
  if (status != BT_STATUS_SUCCESS)
    {
      return -1;
    }

  if (no_response)
    {
      return 0;
    }

  if (btle_wait_gattc_flag(&g_btle_gattc_write_done, "write",
                           BTLE_GATTC_OP_WAIT_MS) < 0)
    {
      return -1;
    }

  return g_btle_gattc_last_status == GATT_STATUS_SUCCESS ? 0 : -1;
}

static int btle_gattc_subscribe_handle(gattc_handle_t handle, uint16_t attr)
{
  bt_status_t status;

  g_btle_gattc_subscribe_done = false;
  g_btle_gattc_last_status = GATT_STATUS_FAILURE;
  status = bt_gattc_subscribe(handle, attr, GATT_CCC_NOTIFY);
  printf("btle: gattc subscribe request handle=0x%04x status=%d\n",
         attr, status);
  if (status != BT_STATUS_SUCCESS)
    {
      return -1;
    }

  if (btle_wait_gattc_flag(&g_btle_gattc_subscribe_done, "subscribe",
                           BTLE_GATTC_OP_WAIT_MS) < 0)
    {
      return -1;
    }

  return g_btle_gattc_last_status == GATT_STATUS_SUCCESS ? 0 : -1;
}

static void btle_gattc_wait_or_disconnect(unsigned int hold_seconds)
{
  if (hold_seconds == 0)
    {
      void (*old_sigint)(int);

      printf("btle: gattc connected; wait for peer disconnect or Ctrl+C\n");
      g_btle_stop_requested = 0;
      old_sigint = signal(SIGINT, btle_signal_stop);
      while (!g_btle_stop_requested && g_btle_gattc_connected)
        {
          sleep(1);
        }

      signal(SIGINT, old_sigint);
      return;
    }

  printf("btle: gattc keep link for %u seconds, notify-count=%u\n",
         hold_seconds, g_btle_gattc_notify_count);
  sleep(hold_seconds);
}

static int btle_cmd_gattc(const char *addr_arg, const char *type_arg,
                          const struct btle_gattc_options *opts)
{
  bt_instance_t *ins;
  void *adapter_cookie;
  bt_address_t addr;
  ble_addr_type_t addr_type;
  bt_status_t status;
  gattc_handle_t handle = NULL;
  int ret = 1;

  if (bt_addr_str2ba(addr_arg, &addr) < 0 ||
      bt_addr_is_empty(&addr))
    {
      printf("btle: invalid LE address \"%s\"\n", addr_arg);
      return 1;
    }

  if (btle_parse_addr_type(type_arg, &addr_type) < 0)
    {
      printf("btle: invalid addr type \"%s\"; use public/random or 0/1\n",
             type_arg);
      return 1;
    }

  ins = btle_create_instance(&adapter_cookie);
  if (ins == NULL)
    {
      return 1;
    }

  if (btle_enable_instance(ins) < 0)
    {
      btle_cleanup_failed_enable(ins);
      goto out;
    }

  btle_gattc_reset();
  status = bt_gattc_create_connect(ins, &handle, &g_btle_gattc_cbs);
  printf("btle: bt_gattc_create_connect status=%d handle=%p\n",
         status, handle);
  if (status != BT_STATUS_SUCCESS || handle == NULL)
    {
      goto out;
    }

  status = bt_gattc_connect(handle, &addr, addr_type);
  printf("btle: bt_gattc_connect addr=%s type=%u(%s) status=%d\n",
         addr_arg, addr_type, btle_addr_type_name(addr_type), status);
  if (status != BT_STATUS_SUCCESS && status != BT_STATUS_DONE)
    {
      goto delete_gattc;
    }

  if (btle_wait_gattc_flag(&g_btle_gattc_connected, "connect",
                           BTLE_CONNECT_WAIT_MS) < 0)
    {
      goto delete_gattc;
    }

  if (opts->exchange_mtu)
    {
      g_btle_gattc_mtu_done = false;
      status = bt_gattc_exchange_mtu(handle, opts->mtu);
      printf("btle: bt_gattc_exchange_mtu mtu=%u status=%d\n",
             opts->mtu, status);
      if (status == BT_STATUS_SUCCESS)
        {
          btle_wait_gattc_flag(&g_btle_gattc_mtu_done, "mtu",
                               BTLE_GATTC_OP_WAIT_MS);
        }
    }

  g_btle_gattc_discover_done = false;
  status = bt_gattc_discover_service(handle, NULL);
  printf("btle: bt_gattc_discover_service status=%d\n", status);
  if (status == BT_STATUS_SUCCESS)
    {
      btle_wait_gattc_flag(&g_btle_gattc_discover_done, "discover",
                           BTLE_GATTC_DISCOVER_WAIT_MS);
    }

  if (opts->auto_test)
    {
      printf("btle: gattc auto test profile=%s notify=0x%04x"
             "/0x%lx write=0x%04x/0x%lx read=0x%04x/0x%lx\n",
             g_btle_gattc_auto_profile != NULL ?
             g_btle_gattc_auto_profile : "none",
             g_btle_gattc_auto_notify_handle,
             (unsigned long)g_btle_gattc_auto_notify_props,
             g_btle_gattc_auto_rw_handle,
             (unsigned long)g_btle_gattc_auto_rw_props,
             g_btle_gattc_auto_info_handle,
             (unsigned long)g_btle_gattc_auto_info_props);

      if (g_btle_gattc_auto_info_handle != 0 &&
          (g_btle_gattc_auto_info_props & GATT_PROP_READ) != 0)
        {
          btle_gattc_read_handle(handle, g_btle_gattc_auto_info_handle);
        }

      if (g_btle_gattc_auto_notify_handle != 0 &&
          (g_btle_gattc_auto_notify_props & GATT_PROP_NOTIFY) != 0)
        {
          btle_gattc_subscribe_handle(handle,
                                      g_btle_gattc_auto_notify_handle);
        }

      if (g_btle_gattc_auto_rw_handle != 0)
        {
          if ((g_btle_gattc_auto_rw_props & GATT_PROP_READ) != 0)
            {
              btle_gattc_read_handle(handle, g_btle_gattc_auto_rw_handle);
            }

          if ((g_btle_gattc_auto_rw_props &
               (GATT_PROP_WRITE | GATT_PROP_WRITE_NR)) != 0)
            {
              bool no_response =
                (g_btle_gattc_auto_rw_props & GATT_PROP_WRITE) == 0;

              btle_gattc_write_handle(handle, g_btle_gattc_auto_rw_handle,
                                      opts->auto_write_value,
                                      no_response);
            }
        }
      else
        {
          printf("btle: gattc auto write skipped, no writable test "
                 "characteristic\n");
        }

      if (g_btle_gattc_auto_notify_handle == 0)
        {
          printf("btle: gattc auto subscribe skipped, no notify test "
                 "characteristic\n");
        }

      if (g_btle_gattc_auto_info_handle == 0)
        {
          printf("btle: gattc auto read skipped, no readable test "
                 "characteristic\n");
        }
    }

  if (opts->subscribe)
    {
      btle_gattc_subscribe_handle(handle, opts->subscribe_handle);
    }

  if (opts->read)
    {
      btle_gattc_read_handle(handle, opts->read_handle);
    }

  if (opts->write)
    {
      btle_gattc_write_handle(handle, opts->write_handle,
                              opts->write_value,
                              opts->write_without_response);
    }

  btle_gattc_wait_or_disconnect(opts->hold_seconds);
  ret = 0;

  if (g_btle_gattc_connected)
    {
      g_btle_gattc_disconnected = false;
      status = bt_gattc_disconnect(handle);
      printf("btle: bt_gattc_disconnect status=%d\n", status);
      if (status == BT_STATUS_SUCCESS || status == BT_STATUS_DONE)
        {
          btle_wait_gattc_flag(&g_btle_gattc_disconnected, "disconnect",
                               BTLE_DISCONNECT_WAIT_MS);
        }
    }

delete_gattc:
  status = bt_gattc_delete_connect(handle);
  printf("btle: bt_gattc_delete_connect status=%d\n", status);

out:
  btle_delete_instance(ins, adapter_cookie);
  return ret;
}

static uint8_t btle_parse_scan_type(const char *arg, uint8_t fallback)
{
  if (arg == NULL)
    {
      return fallback;
    }

  if (strcmp(arg, "passive") == 0 || strcmp(arg, "0") == 0)
    {
      return BT_LE_SCAN_TYPE_PASSIVE;
    }

  if (strcmp(arg, "active") == 0 || strcmp(arg, "1") == 0)
    {
      return BT_LE_SCAN_TYPE_ACTIVE;
    }

  printf("btle: unknown scan type \"%s\", use %s\n", arg,
         fallback == BT_LE_SCAN_TYPE_ACTIVE ? "active" : "passive");
  return fallback;
}

static const char *btle_parse_scan_filter(const char *arg)
{
  if (arg == NULL || strcmp(arg, "-") == 0 || strcmp(arg, "none") == 0 ||
      strcmp(arg, "null") == 0)
    {
      return NULL;
    }

  return arg;
}

static uint8_t btle_parse_scan_mode(const char *arg, uint8_t fallback)
{
  if (arg == NULL)
    {
      return fallback;
    }

  if (strcmp(arg, "low-power") == 0 || strcmp(arg, "lowpower") == 0 ||
      strcmp(arg, "power") == 0 || strcmp(arg, "0") == 0)
    {
      return BT_SCAN_MODE_LOW_POWER;
    }

  if (strcmp(arg, "balanced") == 0 || strcmp(arg, "balance") == 0 ||
      strcmp(arg, "1") == 0)
    {
      return BT_SCAN_MODE_BALANCED;
    }

  if (strcmp(arg, "low-latency") == 0 || strcmp(arg, "latency") == 0 ||
      strcmp(arg, "fast") == 0 || strcmp(arg, "2") == 0)
    {
      return BT_SCAN_MODE_LOW_LATENCY;
    }

  printf("btle: unknown scan mode \"%s\", use %s\n", arg,
         fallback == BT_SCAN_MODE_LOW_POWER ? "low-power" :
         fallback == BT_SCAN_MODE_BALANCED ? "balanced" : "low-latency");
  return fallback;
}

static int btle_cmd_scan(unsigned int seconds, const char *filter, bool raw,
                         bool list, uint8_t scan_type, uint8_t scan_mode)
{
  bt_instance_t *ins;
  void *adapter_cookie;
  bt_scanner_t *scanner;
  ble_scan_settings_t settings;
  unsigned int waited;
  int ret = 1;

  ins = btle_create_instance(&adapter_cookie);
  if (ins == NULL)
    {
      return 1;
    }

  if (btle_enable_instance(ins) < 0)
    {
      btle_cleanup_failed_enable(ins);
      goto out;
    }

  memset(&settings, 0, sizeof(settings));
  settings.scan_mode = scan_mode;
  settings.legacy = 1;
  settings.scan_type = scan_type;
  settings.scan_phy = BT_LE_1M_PHY;
  settings.policy.policy = BT_LE_SCAN_POLICY_ACCEPT_ALL;

  g_btle_scan_started = false;
  g_btle_scan_failed = false;
  g_btle_scan_results = 0;
  g_btle_scan_rsp_results = 0;
  g_btle_scan_name_hits = 0;
  g_btle_scan_filter = filter;
  g_btle_scan_raw = raw;
  g_btle_scan_list = list;
  btle_scan_cache_reset();

  printf("btle: start %s legacy scan seconds=%u mode=%s",
         settings.scan_type == BT_LE_SCAN_TYPE_ACTIVE ? "active" : "passive",
         seconds,
         settings.scan_mode == BT_SCAN_MODE_LOW_POWER ? "low-power" :
         settings.scan_mode == BT_SCAN_MODE_BALANCED ? "balanced" :
         "low-latency");
  if (filter != NULL)
    {
      char addr_key[BTLE_ADDR_KEY_MAX];

      printf(" filter=\"%s\"", filter);
      if (btle_addr_key_from_text(filter, addr_key, sizeof(addr_key)))
        {
          printf(" addr-key=%s", addr_key);
        }
    }

  if (raw)
    {
      printf(" raw=1");
    }

  if (list)
    {
      printf(" list=1");
    }

  printf("\n");
  scanner = bt_le_start_scan_settings(ins, &settings, &g_btle_scan_cbs);
  if (scanner == NULL)
    {
      printf("btle: bt_le_start_scan_settings returned NULL\n");
      goto out;
    }

  for (waited = 0; waited < 1000; waited += 100)
    {
      if (g_btle_scan_started || g_btle_scan_failed)
        {
          break;
        }

      usleep(100000);
    }

  if (g_btle_scan_failed)
    {
      goto stop_scan;
    }

  sleep(seconds);
  ret = 0;

stop_scan:
  bt_le_stop_scan(ins, scanner);
  usleep(200000);
  if (list)
    {
      btle_scan_cache_print();
    }

out:
  g_btle_scan_filter = NULL;
  g_btle_scan_raw = false;
  g_btle_scan_list = false;
  btle_delete_instance(ins, adapter_cookie);
  return ret;
}

static int btle_connect_with_instance(bt_instance_t *ins, bt_address_t *addr,
                                      ble_addr_type_t addr_type,
                                      unsigned int hold_seconds)
{
  ble_connect_params_t params;
  bt_status_t status;
  char addr_str[BT_ADDR_STR_LENGTH];
  void (*old_sigint)(int) = NULL;
  int ret = 1;

  memset(&params, 0, sizeof(params));
  params.use_default_params = false;
  params.filter_policy = BT_LE_CONNECT_FILTER_POLICY_ADDR;
  params.init_phy = BT_LE_1M_PHY;
  params.scan_interval = 16;
  params.scan_window = 16;
  params.connection_interval_min = 24;
  params.connection_interval_max = 40;
  params.connection_latency = 0;
  params.supervision_timeout = 400;

  g_btle_conn_target = *addr;
  g_btle_conn_track = true;
  g_btle_conn_connected = false;
  g_btle_conn_disconnected = false;
  g_btle_conn_state = CONNECTION_STATE_DISCONNECTED;

  bt_addr_ba2str(addr, addr_str);
  printf("btle: connect addr=%s type=%u(%s) hold=%u seconds\n",
         addr_str, addr_type, btle_addr_type_name(addr_type),
         hold_seconds);
  printf("btle: connect params scan=0x%04x/0x%04x interval=0x%04x-0x%04x "
         "latency=%u timeout=0x%04x\n",
         params.scan_interval, params.scan_window,
         params.connection_interval_min, params.connection_interval_max,
         params.connection_latency, params.supervision_timeout);
  if (hold_seconds == 0)
    {
      printf("btle: hold=0 means keep connected until peer disconnects "
             "or Ctrl+C\n");
    }

  if (addr_type == BT_LE_ADDR_TYPE_RANDOM ||
      addr_type == BT_LE_ADDR_TYPE_RANDOM_ID)
    {
      printf("btle: note: random LE addresses may be RPA; connect only to "
             "an address from the latest named scan result\n");
    }

  if (bt_device_is_connected(ins, addr, BT_TRANSPORT_BLE))
    {
      printf("btle: target is already LE connected\n");
      g_btle_conn_connected = true;
    }
  else
    {
      status = bt_device_connect_le(ins, addr, addr_type, &params);
      printf("btle: bt_device_connect_le status=%d\n", status);
      if (status != BT_STATUS_SUCCESS && status != BT_STATUS_DONE)
        {
          goto clear_conn;
        }

      if (status == BT_STATUS_DONE &&
          bt_device_is_connected(ins, addr, BT_TRANSPORT_BLE))
        {
          g_btle_conn_connected = true;
        }

      if (btle_wait_conn(true, BTLE_CONNECT_WAIT_MS) < 0)
        {
          status = bt_device_disconnect_le(ins, addr);
          printf("btle: cleanup disconnect_le status=%d\n", status);
          btle_wait_conn(false, BTLE_DISCONNECT_WAIT_MS);
          goto clear_conn;
        }
    }

  if (hold_seconds == 0)
    {
      printf("btle: LE connected; waiting for peer disconnect or Ctrl+C\n");
      g_btle_stop_requested = 0;
      old_sigint = signal(SIGINT, btle_signal_stop);
      while (!g_btle_stop_requested && !g_btle_conn_disconnected)
        {
          sleep(1);
        }

      signal(SIGINT, old_sigint);
      old_sigint = NULL;
      if (g_btle_conn_disconnected)
        {
          ret = 0;
          goto clear_conn;
        }

      printf("btle: Ctrl+C requested; disconnect LE link\n");
    }
  else
    {
      printf("btle: LE connected; keep link for %u seconds then "
             "disconnect locally\n", hold_seconds);
      sleep(hold_seconds);
    }

  g_btle_conn_disconnected = false;
  status = bt_device_disconnect_le(ins, addr);
  printf("btle: bt_device_disconnect_le status=%d\n", status);
  if (status == BT_STATUS_SUCCESS || status == BT_STATUS_DONE)
    {
      btle_wait_conn(false, BTLE_DISCONNECT_WAIT_MS);
      ret = 0;
    }

clear_conn:
  if (old_sigint != NULL)
    {
      signal(SIGINT, old_sigint);
    }

  g_btle_conn_track = false;
  return ret;
}

static int btle_cmd_connect(const char *addr_arg, const char *type_arg,
                            unsigned int hold_seconds)
{
  bt_instance_t *ins;
  void *adapter_cookie;
  bt_address_t addr;
  ble_addr_type_t addr_type;
  int ret = 1;

  if (bt_addr_str2ba(addr_arg, &addr) < 0 ||
      bt_addr_is_empty(&addr))
    {
      printf("btle: invalid LE address \"%s\"\n", addr_arg);
      return 1;
    }

  if (btle_parse_addr_type(type_arg, &addr_type) < 0)
    {
      printf("btle: invalid addr type \"%s\"; use public/random or 0/1\n",
             type_arg);
      return 1;
    }

  ins = btle_create_instance(&adapter_cookie);
  if (ins == NULL)
    {
      return 1;
    }

  if (btle_enable_instance(ins) < 0)
    {
      btle_cleanup_failed_enable(ins);
      goto out;
    }

  ret = btle_connect_with_instance(ins, &addr, addr_type, hold_seconds);

out:
  btle_delete_instance(ins, adapter_cookie);
  return ret;
}

static int btle_cmd_scanconnect(const char *filter, unsigned int scan_seconds,
                                unsigned int hold_seconds,
                                uint8_t scan_type, uint8_t scan_mode)
{
  bt_instance_t *ins;
  void *adapter_cookie;
  bt_scanner_t *scanner;
  ble_scan_settings_t settings;
  struct btle_scan_entry *selected;
  bt_address_t addr;
  ble_addr_type_t addr_type;
  char addr_str[BT_ADDR_STR_LENGTH];
  unsigned int waited;
  int ret = 1;

  if (filter == NULL)
    {
      printf("btle: scanconnect requires a name or MAC filter\n");
      return 1;
    }

  ins = btle_create_instance(&adapter_cookie);
  if (ins == NULL)
    {
      return 1;
    }

  if (btle_enable_instance(ins) < 0)
    {
      btle_cleanup_failed_enable(ins);
      goto out;
    }

  memset(&settings, 0, sizeof(settings));
  settings.scan_mode = scan_mode;
  settings.legacy = 1;
  settings.scan_type = scan_type;
  settings.scan_phy = BT_LE_1M_PHY;
  settings.policy.policy = BT_LE_SCAN_POLICY_ACCEPT_ALL;

  g_btle_scan_started = false;
  g_btle_scan_failed = false;
  g_btle_scan_results = 0;
  g_btle_scan_rsp_results = 0;
  g_btle_scan_name_hits = 0;
  g_btle_scan_filter = filter;
  g_btle_scan_raw = false;
  g_btle_scan_list = true;
  btle_scan_cache_reset();

  printf("btle: scanconnect filter=\"%s\" scan=%u hold=%u type=%s "
         "mode=%s\n", filter, scan_seconds, hold_seconds,
         settings.scan_type == BT_LE_SCAN_TYPE_ACTIVE ? "active" :
         "passive",
         settings.scan_mode == BT_SCAN_MODE_LOW_POWER ? "low-power" :
         settings.scan_mode == BT_SCAN_MODE_BALANCED ? "balanced" :
         "low-latency");
  scanner = bt_le_start_scan_settings(ins, &settings, &g_btle_scan_cbs);
  if (scanner == NULL)
    {
      printf("btle: bt_le_start_scan_settings returned NULL\n");
      goto clear_scan;
    }

  for (waited = 0; waited < 1000; waited += 100)
    {
      if (g_btle_scan_started || g_btle_scan_failed)
        {
          break;
        }

      usleep(100000);
    }

  if (!g_btle_scan_failed)
    {
      sleep(scan_seconds);
    }

  bt_le_stop_scan(ins, scanner);
  usleep(200000);
  btle_scan_cache_print();

  if (g_btle_scan_failed)
    {
      printf("btle: scanconnect scan start failed\n");
      goto clear_scan;
    }

  selected = btle_scan_cache_select_best();
  if (selected == NULL)
    {
      printf("btle: scanconnect no connectable matched device; try "
             "active low-latency scan or a full current MAC from nRF\n");
      goto clear_scan;
    }

  addr = selected->addr;
  addr_type = (ble_addr_type_t)selected->addr_type;
  if (addr_type != BT_LE_ADDR_TYPE_PUBLIC &&
      addr_type != BT_LE_ADDR_TYPE_RANDOM &&
      addr_type != BT_LE_ADDR_TYPE_PUBLIC_ID &&
      addr_type != BT_LE_ADDR_TYPE_RANDOM_ID)
    {
      printf("btle: scanconnect selected unsupported addr type=%u(%s)\n",
             addr_type, btle_addr_type_name(addr_type));
      goto clear_scan;
    }

  bt_addr_ba2str(&addr, addr_str);
  printf("btle: scanconnect selected addr=%s type=%u(%s) rssi=%d "
         "seen=%u name=\"%s\"\n", addr_str, addr_type,
         btle_addr_type_name(addr_type), selected->best_rssi,
         selected->seen, selected->adv.name);

  g_btle_scan_filter = NULL;
  g_btle_scan_raw = false;
  g_btle_scan_list = false;
  ret = btle_connect_with_instance(ins, &addr, addr_type, hold_seconds);
  goto out;

clear_scan:
  g_btle_scan_filter = NULL;
  g_btle_scan_raw = false;
  g_btle_scan_list = false;

out:
  btle_delete_instance(ins, adapter_cookie);
  return ret;
}

static void btle_usage(void)
{
  printf("Usage:\n");
  printf("  btle enable\n");
  printf("  btle state\n");
  printf("  btle adv [seconds] [name]\n");
  printf("  btle gatt [seconds] [name] [notify-count|notify=N] "
         "[cleanup|nocleanup]\n");
  printf("  btle gattserver [name] [notify-count|notify=N] "
         "[cleanup|nocleanup]\n");
  printf("    GATT: service=0xff00 notify=0xff01 rw=0xff02 info=0xff03; "
         "write 0xff02 echoes on 0xff01 after subscribe\n");
  printf("    Commands on 0xff02: cmd:ping, cmd:info, cmd:stats/status, "
         "cmd:help, cmd:echo <text>\n");
  printf("    Start bluetoothd once per boot before btle; do not run "
         "duplicate \"bluetoothd &\" instances\n");
  printf("    Normal app stop should stop GAP/GATT activity, not kill "
         "bluetoothd; btstop --force-daemon is diagnostic only\n");
  printf("    GATT unregister cleanup is disabled by default to avoid a "
         "known Vela GATTS cleanup crash; use cleanup/unregister only for "
         "diagnostics\n");
  printf("  btle gattc <addr> <public|random|0|1> [hold-seconds] "
         "[auto|no-auto] [mtu=N] [read=0xH] [subscribe=0xH] "
         "[write=0xH:text] [write-nr=0xH:text] [auto-write=text]\n");
  printf("    Default auto discovers services and tests 0xff01/0xff02/"
         "0xff03 or phone-test 0xfff1/0xfff2/0xfff3 when present\n");
  printf("  btle scan [seconds] [name-or-mac-filter]\n");
  printf("  btle scanlist [seconds] [name-or-mac-filter]\n");
  printf("  btle scanraw [seconds] [name-or-mac-filter]\n");
  printf("  btle scanconnect <name-or-mac-filter> [scan-seconds] "
         "[hold-seconds]\n");
  printf("    Optional: append [active|passive] "
         "[low-power|balanced|low-latency]\n");
  printf("    Filter accepts name, AA:BB:CC:DD:EE:FF, aabbccddeeff, "
         "or a MAC fragment such as DDEEFF\n");
  printf("  btle connect <addr> <public|random|0|1> [hold-seconds]\n");
  printf("    hold-seconds=0 keeps the link until peer disconnects or "
         "Ctrl+C\n");
  printf("  btle disable\n");
}

int btle_main(int argc, char *argv[])
{
  if (argc < 2)
    {
      btle_usage();
      return 1;
    }

  if (strcmp(argv[1], "enable") == 0)
    {
      return btle_cmd_enable();
    }

  if (strcmp(argv[1], "state") == 0)
    {
      return btle_cmd_state();
    }

  if (strcmp(argv[1], "disable") == 0)
    {
      return btle_cmd_disable();
    }

  if (strcmp(argv[1], "adv") == 0)
    {
      unsigned int seconds = argc >= 3 ? (unsigned int)atoi(argv[2]) :
                             BTLE_DEFAULT_SECONDS;

      if (seconds == 0)
        {
          seconds = BTLE_DEFAULT_SECONDS;
        }

      return btle_cmd_adv(seconds, argc >= 4 ? argv[3] :
                          BTLE_DEFAULT_NAME);
    }

  if (strcmp(argv[1], "gatt") == 0)
    {
      unsigned int seconds;
      unsigned int notify_max;
      const char *name;
      bool cleanup;

      btle_parse_gatt_args(argc, argv, &seconds, &name, &notify_max,
                           &cleanup);
      return btle_cmd_gatt(seconds, name, notify_max, cleanup);
    }

  if (strcmp(argv[1], "gattserver") == 0)
    {
      unsigned int notify_max;
      const char *name;
      bool cleanup;

      btle_parse_gattserver_args(argc, argv, &name, &notify_max, &cleanup);
      return btle_cmd_gattserver(name, notify_max, cleanup);
    }

  if (strcmp(argv[1], "gattc") == 0 ||
      strcmp(argv[1], "gattclient") == 0)
    {
      struct btle_gattc_options opts;

      if (argc < 4 || btle_parse_gattc_args(argc, argv, &opts) < 0)
        {
          btle_usage();
          return 1;
        }

      return btle_cmd_gattc(argv[2], argv[3], &opts);
    }

  if (strcmp(argv[1], "scan") == 0)
    {
      unsigned int seconds = argc >= 3 ? (unsigned int)atoi(argv[2]) :
                             BTLE_SCAN_SECONDS;
      uint8_t scan_mode = btle_parse_scan_mode(argc >= 6 ? argv[5] : NULL,
                                               BT_SCAN_MODE_LOW_LATENCY);
      uint8_t scan_type = btle_parse_scan_type(argc >= 5 ? argv[4] : NULL,
                                               BT_LE_SCAN_TYPE_ACTIVE);

      if (seconds == 0)
        {
          seconds = BTLE_SCAN_SECONDS;
        }

      return btle_cmd_scan(seconds,
                           btle_parse_scan_filter(argc >= 4 ? argv[3] :
                                                  NULL),
                           false, false, scan_type, scan_mode);
    }

  if (strcmp(argv[1], "scanlist") == 0)
    {
      unsigned int seconds = argc >= 3 ? (unsigned int)atoi(argv[2]) :
                             BTLE_SCAN_SECONDS;
      uint8_t scan_mode = btle_parse_scan_mode(argc >= 6 ? argv[5] : NULL,
                                               BT_SCAN_MODE_BALANCED);
      uint8_t scan_type = btle_parse_scan_type(argc >= 5 ? argv[4] : NULL,
                                               BT_LE_SCAN_TYPE_ACTIVE);

      if (seconds == 0)
        {
          seconds = BTLE_SCAN_SECONDS;
        }

      return btle_cmd_scan(seconds,
                           btle_parse_scan_filter(argc >= 4 ? argv[3] :
                                                  NULL),
                           false, true, scan_type, scan_mode);
    }

  if (strcmp(argv[1], "scanraw") == 0)
    {
      unsigned int seconds = argc >= 3 ? (unsigned int)atoi(argv[2]) :
                             BTLE_SCAN_SECONDS;
      uint8_t scan_mode = btle_parse_scan_mode(argc >= 6 ? argv[5] : NULL,
                                               BT_SCAN_MODE_LOW_LATENCY);
      uint8_t scan_type = btle_parse_scan_type(argc >= 5 ? argv[4] : NULL,
                                               BT_LE_SCAN_TYPE_ACTIVE);

      if (seconds == 0)
        {
          seconds = BTLE_SCAN_SECONDS;
        }

      return btle_cmd_scan(seconds,
                           btle_parse_scan_filter(argc >= 4 ? argv[3] :
                                                  NULL),
                           true, false, scan_type, scan_mode);
    }

  if (strcmp(argv[1], "scanconnect") == 0)
    {
      const char *filter;
      unsigned int scan_seconds = argc >= 4 ? (unsigned int)atoi(argv[3]) :
                                  BTLE_SCAN_SECONDS;
      unsigned int hold_seconds = argc >= 5 ? (unsigned int)atoi(argv[4]) :
                                  BTLE_CONNECT_SECONDS;
      uint8_t scan_mode = btle_parse_scan_mode(argc >= 7 ? argv[6] : NULL,
                                               BT_SCAN_MODE_LOW_LATENCY);
      uint8_t scan_type = btle_parse_scan_type(argc >= 6 ? argv[5] : NULL,
                                               BT_LE_SCAN_TYPE_ACTIVE);

      if (argc < 3)
        {
          btle_usage();
          return 1;
        }

      if (scan_seconds == 0)
        {
          scan_seconds = BTLE_SCAN_SECONDS;
        }

      filter = btle_parse_scan_filter(argv[2]);
      return btle_cmd_scanconnect(filter, scan_seconds, hold_seconds,
                                  scan_type, scan_mode);
    }

  if (strcmp(argv[1], "connect") == 0)
    {
      unsigned int seconds = argc >= 5 ? (unsigned int)atoi(argv[4]) :
                             BTLE_CONNECT_SECONDS;

      if (argc < 4)
        {
          btle_usage();
          return 1;
        }

      return btle_cmd_connect(argv[2], argv[3], seconds);
    }

  btle_usage();
  return 1;
}
