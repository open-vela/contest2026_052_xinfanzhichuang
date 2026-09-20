#include "aic_ble_gatt_lifecycle.h"

#include <stdio.h>

#include "aic_ble_common.h"

#define AIC_BLE_GATTS_WAIT_POLL_US 100000

int aic_ble_gatts_register_table(bt_instance_t *ins,
                                 gatts_handle_t *handle,
                                 gatts_callbacks_t *callbacks,
                                 gatt_srv_db_t *service_db,
                                 volatile bool *table_added,
                                 volatile bool *table_removed,
                                 volatile gatt_status_t *table_status,
                                 volatile uint16_t *table_handle,
                                 const char *service_desc,
                                 unsigned int wait_ms)
{
  bt_status_t status;

  *handle = NULL;
  *table_added = false;
  *table_removed = false;
  *table_status = GATT_STATUS_FAILURE;
  *table_handle = 0;

  status = bt_gatts_register_service(ins, handle, callbacks);
  printf("btle: bt_gatts_register_service status=%d handle=%p\n",
         status, *handle);
  if (status != BT_STATUS_SUCCESS)
    {
      return -1;
    }

  status = bt_gatts_add_attr_table(*handle, service_db);
  printf("btle: bt_gatts_add_attr_table status=%d %s\n", status,
         service_desc != NULL ? service_desc : "");
  if (status != BT_STATUS_SUCCESS)
    {
      bt_gatts_unregister_service(*handle);
      *handle = NULL;
      return -1;
    }

  if (aic_ble_wait_bool(table_added, wait_ms,
                        AIC_BLE_GATTS_WAIT_POLL_US) < 0)
    {
      printf("btle: wait gatts table callback timeout; continue\n");
    }
  else if (*table_status != GATT_STATUS_SUCCESS)
    {
      printf("btle: gatts table add failed status=%d\n", *table_status);
      bt_gatts_unregister_service(*handle);
      *handle = NULL;
      return -1;
    }

  return 0;
}

void aic_ble_gatts_unregister_table(gatts_handle_t *handle,
                                    bool cleanup,
                                    volatile bool *table_removed,
                                    volatile uint16_t *table_handle,
                                    volatile bool *notify_pending,
                                    unsigned int wait_ms)
{
  bt_status_t status;
  uint16_t attr_handle;

  if (*handle == NULL)
    {
      return;
    }

  if (!cleanup)
    {
      printf("btle: keep gatts service registered; skip remove/unregister "
             "to avoid Vela GATTS cleanup crash\n");
      *handle = NULL;
      return;
    }

  *table_removed = false;
  *notify_pending = false;

  attr_handle = table_handle != NULL ? *table_handle : 0;
  if (attr_handle != 0)
    {
      printf("btle: begin gatts remove attr table handle=%u\n",
             attr_handle);
      status = bt_gatts_remove_attr_table(*handle, attr_handle);
      printf("btle: bt_gatts_remove_attr_table handle=%u status=%d\n",
             attr_handle, status);
      if (status != BT_STATUS_SUCCESS)
        {
          printf("btle: skip gatts unregister after remove table failure; "
                 "keep bluetoothd alive or reboot before repeated cleanup "
                 "diagnostics\n");
          *handle = NULL;
          return;
        }

      if (aic_ble_wait_bool(table_removed, wait_ms,
                            AIC_BLE_GATTS_WAIT_POLL_US) < 0)
        {
          printf("btle: gatts table removed callback not observed; skip "
                 "service unregister to avoid bluetoothd cleanup crash\n");
          *handle = NULL;
          return;
        }

      if (table_handle != NULL)
        {
          *table_handle = 0;
        }
    }

  status = bt_gatts_unregister_service(*handle);
  printf("btle: bt_gatts_unregister_service status=%d\n", status);
  *handle = NULL;
  if (status != BT_STATUS_SUCCESS)
    {
      return;
    }
}
