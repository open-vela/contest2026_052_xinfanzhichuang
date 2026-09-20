/*
 * Copyright (C) 2018-2024 AICSemi Ltd.
 * All Rights Reserved
 *
 * Adapted for NuttX/Vela
 */

#ifndef _PLAT_PORT_H_
#define _PLAT_PORT_H_

#include <nuttx/config.h>

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif

#define PLATFORM_CACHE_LINE_SIZE        (CACHE_LINE_SIZE)

void platform_pwr_wifi_pin_init(void);
void platform_pwr_wifi_pin_enable(void);
void platform_pwr_wifi_pin_disable(void);
void platform_rst_wifi_pin_init(void);
void platform_rst_wifi_pin_enable(void);
void platform_rst_wifi_pin_disable(void);
void platform_rst_bt_pin_init(void);
void platform_rst_bt_pin_enable(void);
void platform_rst_bt_pin_disable(void);
int platform_wifi_power_reset_shared(void);

#endif /* _PLAT_PORT_H_ */
