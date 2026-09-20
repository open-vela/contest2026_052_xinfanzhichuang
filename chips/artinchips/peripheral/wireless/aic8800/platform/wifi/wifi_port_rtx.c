/*
 * WiFi port RTX stub for NuttX/Vela
 *
 * Provides global variables required by wifi_port.c and netif_port.c.
 * RT-Thread WiFi framework operations are not needed on NuttX.
 */

#include <nuttx/config.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "wlan_dev.h"

#ifdef WIFI_USING_LOOPBACK_NETDEV
bool loop_dev_reg_flag = 0;
#endif

struct rt_wlan_device *s_wlan_dev = NULL;
struct rt_wlan_device *s_ap_dev   = NULL;
