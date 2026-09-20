/*
 * RT-Thread wlan_dev.h stub for NuttX
 * Provides minimal type definitions used by wifi_port.c
 */

#ifndef __WLAN_DEVICE_H__
#define __WLAN_DEVICE_H__

#include <stdint.h>
#include <string.h>

/* Forward declaration for opaque WiFi device handle */
struct rt_wlan_device;

typedef enum {
    RT_WLAN_NONE,
    RT_WLAN_STATION,
    RT_WLAN_AP,
    RT_WLAN_MODE_MAX
} rt_wlan_mode_t;

typedef enum {
    RT_WLAN_DEV_EVT_INIT_DONE = 0,
    RT_WLAN_DEV_EVT_CONNECT,
    RT_WLAN_DEV_EVT_CONNECT_FAIL,
    RT_WLAN_DEV_EVT_DISCONNECT,
    RT_WLAN_DEV_EVT_AP_START,
    RT_WLAN_DEV_EVT_AP_STOP,
    RT_WLAN_DEV_EVT_AP_ASSOCIATED,
    RT_WLAN_DEV_EVT_AP_DISASSOCIATED,
} rt_wlan_dev_event_t;

struct rt_wlan_info {
    char ssid[33];
    uint8_t bssid[6];
    uint16_t channel;
    int16_t rssi;
    uint8_t security;
};

struct rt_wlan_buff {
    void *data;
    uint16_t len;
};

/* Stub: event indication (no-op on NuttX) */
static inline void rt_wlan_dev_indicate_event_handle(void *dev, int event, void *buff)
{
    (void)dev;
    (void)event;
    (void)buff;
}

#endif /* __WLAN_DEVICE_H__ */
