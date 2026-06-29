/*
 * Copyright (c) 2022-2024, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: matteo <duanmt@artinchip.com>
 */

#ifndef _ARTINCHIP_HAL_WDT_D13X_H__
#define _ARTINCHIP_HAL_WDT_D13X_H__

#ifdef __cplusplus
extern "C" {
#endif


#define AIC_WDT_NAME "aic-wdt"
#define AIC_WDT_CHAN_NUM 1

#define WDT_MAX_TIMEOUT (60 * 60)
#define WDT_MIN_TIMEOUT 1
#define WDT_DEFAULT_TIMEOUT 10

#define WREG_PROTECT_EN 1
#define WREG_PROTECT_DIS 0

#ifdef AIC_WDT_DRV_V11
#define RST_CPU 1
#define RST_SYS 0

#endif





#ifdef __cplusplus
}
#endif

#endif
