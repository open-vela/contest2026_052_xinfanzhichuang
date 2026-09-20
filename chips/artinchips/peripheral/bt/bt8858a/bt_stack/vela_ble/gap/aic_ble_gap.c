#include "aic_ble_gap.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "aic_ble_common.h"

#define AIC_BLE_GAP_ADV_WAIT_POLL_US 100000

static struct aic_ble_gap_adv *g_aic_ble_gap_active_adv;

static bool aic_ble_gap_add_adv_field(uint8_t *data, size_t data_size,
                                      size_t *pos, uint8_t type,
                                      const uint8_t *value,
                                      size_t value_len)
{
  if (*pos + value_len + 2 > data_size || value_len > 0xfe)
    {
      return false;
    }

  data[(*pos)++] = (uint8_t)(value_len + 1);
  data[(*pos)++] = type;
  memcpy(&data[*pos], value, value_len);
  *pos += value_len;
  return true;
}

static void aic_ble_gap_adv_start_cb(bt_advertiser_t *handle,
                                     uint8_t adv_id, uint8_t status)
{
  struct aic_ble_gap_adv *adv = g_aic_ble_gap_active_adv;

  (void)handle;

  if (adv == NULL)
    {
      printf("btle: advertising start callback without active context\n");
      return;
    }

  adv->id = adv_id;
  adv->status = status;
  if (status == BT_ADV_STATUS_SUCCESS)
    {
      adv->started = true;
      printf("btle: advertising started id=%u\n", adv_id);
    }
  else
    {
      adv->failed = true;
      printf("btle: advertising start failed status=%u\n", status);
    }
}

static void aic_ble_gap_adv_stop_cb(bt_advertiser_t *handle, uint8_t adv_id)
{
  struct aic_ble_gap_adv *adv = g_aic_ble_gap_active_adv;

  (void)handle;

  if (adv != NULL)
    {
      adv->stopped = true;
    }

  printf("btle: advertising stopped id=%u\n", adv_id);
}

static advertiser_callback_t g_aic_ble_gap_adv_cbs =
{
  .size = sizeof(g_aic_ble_gap_adv_cbs),
  .on_advertising_start = aic_ble_gap_adv_start_cb,
  .on_advertising_stopped = aic_ble_gap_adv_stop_cb,
};

void aic_ble_gap_adv_reset(struct aic_ble_gap_adv *adv)
{
  memset(adv, 0, sizeof(*adv));
  adv->status = 0xff;
}

void aic_ble_gap_default_adv_params(ble_adv_params_t *params)
{
  memset(params, 0, sizeof(*params));
  params->adv_type = BT_LE_LEGACY_ADV_IND;
  params->peer_addr_type = BT_LE_ADDR_TYPE_PUBLIC;
  params->own_addr_type = BT_LE_ADDR_TYPE_PUBLIC;
  params->interval = 320;
  params->tx_power = 0;
  params->channel_map = BT_LE_ADV_CHANNEL_DEFAULT;
  params->filter_policy = BT_LE_ADV_FILTER_WHITE_LIST_FOR_NONE;
}

uint16_t aic_ble_gap_fill_adv_data(uint8_t *data, size_t data_size,
                                   const char *name,
                                   bool include_service_uuid,
                                   uint16_t service_uuid)
{
  static const uint8_t flags[] = {0x06};
  uint8_t uuid16[2];
  size_t name_max;
  size_t name_len;
  size_t pos = 0;

  memset(data, 0, data_size);
  aic_ble_gap_add_adv_field(data, data_size, &pos, 0x01, flags,
                            sizeof(flags));

  if (include_service_uuid)
    {
      uuid16[0] = (uint8_t)service_uuid;
      uuid16[1] = (uint8_t)(service_uuid >> 8);
      aic_ble_gap_add_adv_field(data, data_size, &pos, 0x03, uuid16,
                                sizeof(uuid16));
    }

  name_max = data_size > pos + 2 ? data_size - pos - 2 : 0;
  name_len = aic_ble_limited_strlen(name, name_max);
  if (name_len > 0)
    {
      aic_ble_gap_add_adv_field(data, data_size, &pos,
                                name[name_len] == '\0' ? 0x09 : 0x08,
                                (const uint8_t *)name, name_len);
    }

  return (uint16_t)pos;
}

uint16_t aic_ble_gap_fill_scan_rsp_data(uint8_t *data, size_t data_size,
                                        const char *name)
{
  size_t name_max = data_size > 2 ? data_size - 2 : 0;
  size_t name_len = aic_ble_limited_strlen(name, name_max);
  size_t pos = 0;

  memset(data, 0, data_size);
  if (name_len > 0)
    {
      aic_ble_gap_add_adv_field(data, data_size, &pos,
                                name[name_len] == '\0' ? 0x09 : 0x08,
                                (const uint8_t *)name, name_len);
    }

  return (uint16_t)pos;
}

int aic_ble_gap_start_advertising(bt_instance_t *ins,
                                  struct aic_ble_gap_adv *adv,
                                  ble_adv_params_t *params,
                                  uint8_t *adv_data,
                                  uint16_t adv_len,
                                  uint8_t *scan_rsp_data,
                                  uint16_t scan_rsp_len,
                                  unsigned int wait_ms)
{
  aic_ble_gap_adv_reset(adv);
  g_aic_ble_gap_active_adv = adv;

  adv->handle = bt_le_start_advertising(ins, params, adv_data, adv_len,
                                        scan_rsp_data, scan_rsp_len,
                                        &g_aic_ble_gap_adv_cbs);
  if (adv->handle == NULL)
    {
      printf("btle: bt_le_start_advertising returned NULL\n");
      g_aic_ble_gap_active_adv = NULL;
      return -1;
    }

  while (wait_ms > 0)
    {
      if (adv->started || adv->failed)
        {
          break;
        }

      usleep(AIC_BLE_GAP_ADV_WAIT_POLL_US);
      wait_ms = wait_ms > AIC_BLE_GAP_ADV_WAIT_POLL_US / 1000 ?
                wait_ms - AIC_BLE_GAP_ADV_WAIT_POLL_US / 1000 : 0;
    }

  if (adv->failed || !adv->started)
    {
      if (!adv->failed)
        {
          printf("btle: advertising start wait timeout\n");
        }

      return -1;
    }

  return 0;
}

void aic_ble_gap_stop_advertising(bt_instance_t *ins,
                                  struct aic_ble_gap_adv *adv,
                                  unsigned int wait_ms)
{
  if (adv->handle == NULL)
    {
      return;
    }

  if (adv->failed || adv->stopped)
    {
      printf("btle: skip stop advertising failed=%u stopped=%u\n",
             adv->failed ? 1 : 0, adv->stopped ? 1 : 0);
      adv->handle = NULL;
      if (g_aic_ble_gap_active_adv == adv)
        {
          g_aic_ble_gap_active_adv = NULL;
        }

      return;
    }

  adv->stopped = false;
  printf("btle: stop advertising by id=%u handle=%p\n",
         adv->id, adv->handle);
  if (adv->id != 0)
    {
      bt_le_stop_advertising_id(ins, adv->id);
    }
  else
    {
      bt_le_stop_advertising(ins, adv->handle);
    }

  if (aic_ble_wait_bool(&adv->stopped, wait_ms,
                        AIC_BLE_GAP_ADV_WAIT_POLL_US) < 0)
    {
      printf("btle: advertising stop wait timeout; continue cleanup\n");
    }

  adv->handle = NULL;
  if (g_aic_ble_gap_active_adv == adv)
    {
      g_aic_ble_gap_active_adv = NULL;
    }
}
