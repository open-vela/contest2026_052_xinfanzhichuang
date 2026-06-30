/*
 * Copyright (c) 2022-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: matteo <duanmt@artinchip.com>
 */

#ifndef _ARTINCHIP_HAL_PWM_D13X_H__
#define _ARTINCHIP_HAL_PWM_D13X_H__

#include "aic_common.h"


#define PWM_ID_MAX_NUM 4



#define AIC_PWM_NAME "aic-pwm"

#ifdef FPGA_BOARD_ARTINCHIP
#define PWM_CLK_RATE 24000000 /* 24 MHz */
#else
#define PWM_CLK_RATE 48000000 /* 48 MHz */
#endif


#endif // end of _ARTINCHIP_HAL_PWM_D13X_H__
