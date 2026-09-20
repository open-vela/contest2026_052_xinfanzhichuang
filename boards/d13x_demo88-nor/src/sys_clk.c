/*
 * Copyright (c) 2022, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: weilin.peng@artinchip.com
 */

#include <aic_core.h>
#include <aic_hal.h>
#include "board.h"

#if defined(__NuttX__)
#include <nuttx/config.h>

#ifndef AIC_CLK_PLL_INT0_FREQ
#ifdef CONFIG_AIC_CLK_PLL_INT0_FREQ
#define AIC_CLK_PLL_INT0_FREQ CONFIG_AIC_CLK_PLL_INT0_FREQ
#else
#define AIC_CLK_PLL_INT0_FREQ 480000000
#endif
#endif

#ifndef AIC_CLK_PLL_INT1_FREQ
#ifdef CONFIG_AIC_CLK_PLL_INT1_FREQ
#define AIC_CLK_PLL_INT1_FREQ CONFIG_AIC_CLK_PLL_INT1_FREQ
#else
#define AIC_CLK_PLL_INT1_FREQ 1200000000
#endif
#endif

#ifndef AIC_CLK_PLL_FRA0_FREQ
#ifdef CONFIG_AIC_CLK_PLL_FRA0_FREQ
#define AIC_CLK_PLL_FRA0_FREQ CONFIG_AIC_CLK_PLL_FRA0_FREQ
#else
#define AIC_CLK_PLL_FRA0_FREQ 792000000
#endif
#endif

#ifndef AIC_CLK_PLL_FRA2_FREQ
#ifdef CONFIG_AIC_CLK_PLL_FRA2_FREQ
#define AIC_CLK_PLL_FRA2_FREQ CONFIG_AIC_CLK_PLL_FRA2_FREQ
#else
#define AIC_CLK_PLL_FRA2_FREQ 1188000000
#endif
#endif

#ifndef AIC_CLK_CPU_FREQ
#ifdef CONFIG_AIC_CLK_CPU_FREQ
#define AIC_CLK_CPU_FREQ CONFIG_AIC_CLK_CPU_FREQ
#else
#define AIC_CLK_CPU_FREQ 480000000
#endif
#endif

#ifndef AIC_CLK_AXI0_FREQ
#ifdef CONFIG_AIC_CLK_AXI0_FREQ
#define AIC_CLK_AXI0_FREQ CONFIG_AIC_CLK_AXI0_FREQ
#else
#define AIC_CLK_AXI0_FREQ 200000000
#endif
#endif

#ifndef AIC_CLK_AHB0_FREQ
#ifdef CONFIG_AIC_CLK_AHB0_FREQ
#define AIC_CLK_AHB0_FREQ CONFIG_AIC_CLK_AHB0_FREQ
#else
#define AIC_CLK_AHB0_FREQ 200000000
#endif
#endif

#ifndef AIC_CLK_APB0_FREQ
#ifdef CONFIG_AIC_CLK_APB0_FREQ
#define AIC_CLK_APB0_FREQ CONFIG_AIC_CLK_APB0_FREQ
#else
#define AIC_CLK_APB0_FREQ 100000000
#endif
#endif

#ifndef AIC_CLK_APB1_FREQ
#ifdef CONFIG_AIC_CLK_APB1_FREQ
#define AIC_CLK_APB1_FREQ CONFIG_AIC_CLK_APB1_FREQ
#else
#define AIC_CLK_APB1_FREQ 2400000000
#endif
#endif
#endif

struct aic_sysclk {
  unsigned long freq;
  unsigned int clk_id;
  unsigned int parent_clk_id;
};

struct aic_sysclk aic_sysclk_config[] = {
    {AIC_CLK_PLL_INT0_FREQ, CLK_PLL_INT0, 0},        /* 480000000 */
    {AIC_CLK_PLL_INT1_FREQ, CLK_PLL_INT1, 0},        /* 1200000000 */
    {AIC_CLK_PLL_FRA0_FREQ, CLK_PLL_FRA0, 0},        /* 792000000 */
    {AIC_CLK_PLL_FRA2_FREQ, CLK_PLL_FRA2, 0},        /* 1188000000 */
    {AIC_CLK_CPU_FREQ, CLK_CPU, CLK_CPU_SRC1},       /* 480000000 */
    {AIC_CLK_AXI0_FREQ, CLK_AXI0, CLK_AXI_AHB_SRC1}, /* 200000000 */
    {AIC_CLK_AHB0_FREQ, CLK_AHB0, CLK_AXI_AHB_SRC1}, /* 200000000 */
    {AIC_CLK_APB0_FREQ, CLK_APB0, CLK_APB0_SRC1},    /* 100000000 */
    {AIC_CLK_APB1_FREQ, CLK_APB1, CLK_OSC24M},
};

void aic_board_sysclk_init(void) {
  uint32_t i = 0;

  for (i = 0; i < sizeof(aic_sysclk_config) / sizeof(struct aic_sysclk); i++) {
    if (aic_sysclk_config[i].freq == 0)
      continue;

    /* multi parent clk */
    if (aic_sysclk_config[i].parent_clk_id) {
      hal_clk_set_freq(aic_sysclk_config[i].parent_clk_id,
                       aic_sysclk_config[i].freq);
      hal_clk_enable(aic_sysclk_config[i].parent_clk_id);
      hal_clk_set_parent(aic_sysclk_config[i].clk_id,
                         aic_sysclk_config[i].parent_clk_id);
    } else {
      hal_clk_set_freq(aic_sysclk_config[i].clk_id, aic_sysclk_config[i].freq);
      hal_clk_enable(aic_sysclk_config[i].clk_id);
    }
  }

  /* Enable sys clk */
  hal_clk_enable_deassertrst_iter(CLK_GPIO);
  hal_clk_enable_deassertrst_iter(CLK_GTC);
}
