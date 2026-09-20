#ifndef __AIC_BLE_GAP_H
#define __AIC_BLE_GAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bt_le_advertiser.h"

#define AIC_BLE_GAP_ADV_DATA_MAX 31

struct aic_ble_gap_adv
{
  bt_advertiser_t *handle;
  volatile bool started;
  volatile bool failed;
  volatile bool stopped;
  volatile uint8_t id;
  volatile uint8_t status;
};

void aic_ble_gap_adv_reset(struct aic_ble_gap_adv *adv);
void aic_ble_gap_default_adv_params(ble_adv_params_t *params);
uint16_t aic_ble_gap_fill_adv_data(uint8_t *data, size_t data_size,
                                   const char *name,
                                   bool include_service_uuid,
                                   uint16_t service_uuid);
uint16_t aic_ble_gap_fill_scan_rsp_data(uint8_t *data, size_t data_size,
                                        const char *name);
int aic_ble_gap_start_advertising(bt_instance_t *ins,
                                  struct aic_ble_gap_adv *adv,
                                  ble_adv_params_t *params,
                                  uint8_t *adv_data,
                                  uint16_t adv_len,
                                  uint8_t *scan_rsp_data,
                                  uint16_t scan_rsp_len,
                                  unsigned int wait_ms);
void aic_ble_gap_stop_advertising(bt_instance_t *ins,
                                  struct aic_ble_gap_adv *adv,
                                  unsigned int wait_ms);

#endif
