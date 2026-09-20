/*
 * Copyright (c) 2023-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: matteo <duanmt@artinchip.com>
 */

#ifndef _ARTINCHIP_HAL_WRI_H_
#define _ARTINCHIP_HAL_WRI_H_

#include "aic_common.h"
#include "aic_reboot_reason.h"

#ifdef CONFIG_ARCH_CHIP_D12X
#include "../d12x/include/hal_wri.h"
#elif defined(CONFIG_ARCH_CHIP_D13X)
#include "../d13x/include/hal_wri.h"
#endif

struct aic_wri_ops {
  u32 *wri_bit;
  int (*is_wdt_reset)(u8 hw);
  void (*hw_reboot_action)(u8 hw);
  void (*sw_reboot_reason)(u8 sw, enum aic_reboot_reason r);
  int (*hw_reboot_reason)(u8 hw,
                          enum aic_reboot_reason *r, u32 sw);
};

extern const struct aic_wri_ops wri_ops;
u8 aic_wr_type_get(void);
enum aic_reboot_reason aic_judge_reboot_reason(u8 hw,
                                               u32 sw);

#endif
