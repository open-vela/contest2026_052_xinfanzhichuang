#ifndef __AIC_BLE_GATT_LIFECYCLE_H
#define __AIC_BLE_GATT_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>

#include "bt_gatts.h"

int aic_ble_gatts_register_table(bt_instance_t *ins,
                                 gatts_handle_t *handle,
                                 gatts_callbacks_t *callbacks,
                                 gatt_srv_db_t *service_db,
                                 volatile bool *table_added,
                                 volatile bool *table_removed,
                                 volatile gatt_status_t *table_status,
                                 volatile uint16_t *table_handle,
                                 const char *service_desc,
                                 unsigned int wait_ms);
void aic_ble_gatts_unregister_table(gatts_handle_t *handle,
                                    bool cleanup,
                                    volatile bool *table_removed,
                                    volatile uint16_t *table_handle,
                                    volatile bool *notify_pending,
                                    unsigned int wait_ms);

#endif
