/*
 * Copyright (c) 2022, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: weilin.peng@artinchip.com
 */

#ifndef __AIC_BOARD_H__
#define __AIC_BOARD_H__
#if defined(__NuttX__)
#include <nuttx/config.h>
#else
 #include <rtconfig.h>
#endif
#if defined(KERNEL_RTTHREAD)
#elif defined(KERNEL_FREERTOS)
#elif defined(KERNEL_BAREMETAL)
void aic_hw_board_init(void);
#endif

#endif /* __AIC_BOARD_H__ */
