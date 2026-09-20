/****************************************************************************
 * AIC8800D40L Bluetooth service compatibility for openVela.
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_BLUETOOTH_SERVICE) && \
    !defined(CONFIG_BLUETOOTH_BREDR_SUPPORT)

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef uint8_t bt_controller_id_t;

typedef struct
{
  uint8_t addr[6];
} bt_address_t;

typedef enum
{
  BT_STATUS_SUCCESS,
  BT_STATUS_FAIL,
  BT_STATUS_NOT_READY,
  BT_STATUS_NOMEM,
  BT_STATUS_BUSY,
  BT_STATUS_DONE,
  BT_STATUS_UNSUPPORTED,
  BT_STATUS_PARM_INVALID,
} bt_status_t;

#define BT_STATUS_NOT_SUPPORTED BT_STATUS_UNSUPPORTED

typedef enum
{
  BT_TRANSPORT_BLE,
  BT_TRANSPORT_BREDR,
} bt_transport_t;

typedef void (*aic_sal_func_t)(void *args);

typedef struct
{
  uint16_t max;
  uint16_t min;
  uint16_t attempt;
  uint16_t timeout;
  uint8_t mode;
} aic_bt_pm_mode_t;

typedef struct
{
  bt_transport_t transport;
} aic_sal_adapter_args_t;

typedef struct
{
  bt_controller_id_t id;
  bt_address_t addr;
  aic_sal_func_t func;
  aic_sal_adapter_args_t adpt;
} aic_sal_adapter_req_t;

typedef struct service_work service_work_t;
typedef void (*aic_service_work_cb_t)(service_work_t *work, void *userdata);

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

extern service_work_t *service_loop_work(void *user_data,
                                         aic_service_work_cb_t work_cb,
                                         aic_service_work_cb_t after_work_cb);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void aic_sal_invoke_async(service_work_t *work, void *userdata)
{
  aic_sal_adapter_req_t *req = userdata;

  (void)work;

  if (req != NULL && req->func != NULL)
    {
      req->func(req);
    }

  free(req);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void bt_pm_init(void)
{
}

void bt_pm_cleanup(void)
{
}

void bt_pm_conn_open(uint8_t profile_id, bt_address_t *peer_addr)
{
  (void)profile_id;
  (void)peer_addr;
}

void bt_pm_conn_close(uint8_t profile_id, bt_address_t *peer_addr)
{
  (void)profile_id;
  (void)peer_addr;
}

void bt_pm_app_open(uint8_t profile_id, bt_address_t *peer_addr)
{
  (void)profile_id;
  (void)peer_addr;
}

void bt_pm_app_close(uint8_t profile_id, bt_address_t *peer_addr)
{
  (void)profile_id;
  (void)peer_addr;
}

void bt_pm_sco_open(uint8_t profile_id, bt_address_t *peer_addr)
{
  (void)profile_id;
  (void)peer_addr;
}

void bt_pm_sco_close(uint8_t profile_id, bt_address_t *peer_addr)
{
  (void)profile_id;
  (void)peer_addr;
}

void bt_pm_idle(uint8_t profile_id, bt_address_t *peer_addr)
{
  (void)profile_id;
  (void)peer_addr;
}

void bt_pm_busy(uint8_t profile_id, bt_address_t *peer_addr)
{
  (void)profile_id;
  (void)peer_addr;
}

void bt_pm_remote_link_mode_changed(bt_address_t *addr, uint8_t mode,
                                    uint16_t sniff_interval)
{
  (void)addr;
  (void)mode;
  (void)sniff_interval;
}

void bt_pm_remote_device_connected(bt_address_t *addr)
{
  (void)addr;
}

void bt_pm_remote_device_disconnected(bt_address_t *addr)
{
  (void)addr;
}

bt_status_t bt_pm_set_app_profile_sniff(bt_address_t *peer_addr,
                                        aic_bt_pm_mode_t *sniff_params)
{
  (void)peer_addr;
  (void)sniff_params;
  return BT_STATUS_NOT_SUPPORTED;
}

void *sal_adapter_req(bt_controller_id_t id, bt_address_t *addr,
                      aic_sal_func_t func)
{
  aic_sal_adapter_req_t *req;

  req = calloc(1, sizeof(*req));
  if (req == NULL)
    {
      return NULL;
    }

  req->id = id;
  req->func = func;
  if (addr != NULL)
    {
      memcpy(&req->addr, addr, sizeof(req->addr));
    }

  return req;
}

bt_status_t sal_send_req(void *opaque_req)
{
  if (opaque_req == NULL)
    {
      return BT_STATUS_PARM_INVALID;
    }

  if (service_loop_work(opaque_req, aic_sal_invoke_async, NULL) == NULL)
    {
      free(opaque_req);
      return BT_STATUS_FAIL;
    }

  return BT_STATUS_SUCCESS;
}

#endif
