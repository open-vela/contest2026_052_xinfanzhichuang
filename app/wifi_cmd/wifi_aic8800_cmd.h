#ifndef WIFI_AIC8800_CMD_H
#define WIFI_AIC8800_CMD_H

#include <stdint.h>

#define AIC_WIFI_MAX_AP_COUNT 32
#define AIC_WIFI_SSID_MAX_LEN 32

#define WLAN_CONNECT_CFG_NONE                   0x00
#define WLAN_CONNECT_CFG_DISABLE_AUTO_RECONN    0xDA
#define WLAN_CONNECT_TO_CFG_2_PARAM(cfg, to_ms) \
  ((int)((((uint32_t)(cfg)) << 24) | ((uint32_t)(to_ms) & 0xFFFFFFu)))

typedef struct wifi_ap_info
{
  unsigned char ssid[AIC_WIFI_SSID_MAX_LEN];
  unsigned char bssid[6];
  unsigned int channel;
  unsigned int rssi;
} wifi_ap_info_t;

typedef struct wifi_ap_list
{
  unsigned short ap_count;
  wifi_ap_info_t ap_info[AIC_WIFI_MAX_AP_COUNT];
} wifi_ap_list_t;

typedef void (*aic_scan_cb_t)(void *result);

void wlan_if_scan_open(void);
void wlan_if_scan(aic_scan_cb_t scan_cb);
void wlan_if_getscan(wifi_ap_list_t *ap_list);
void wlan_if_scan_close(void);
int wlan_start_sta(uint8_t *ssid, uint8_t *pw, int timeout_ms);
int wlan_sta_connect(uint8_t *ssid, uint8_t *pw, int timeout_ms);
int wlan_disconnect_sta(uint8_t idx);
int wlan_get_connect_status(void);
int aic_wifi_scan_direct_start(void);

#endif
