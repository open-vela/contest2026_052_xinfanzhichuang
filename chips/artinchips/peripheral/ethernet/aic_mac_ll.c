/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/config.h>

#include <stdint.h>

#include <aic_core.h>
#include <aic_hal.h>

#include "aic_mac.h"

#define GMAC0_CFG                         (SYSCFG_BASE + 0x410)
#define GMAC0_CFG_PHY_RGMII_SEL          BIT(0)
#define GMAC0_CFG_RMII_EXTCLK_SEL        BIT(1)
#define GMAC0_CFG_GMAC_TXDLY_SEL_SHIFT   12
#define GMAC0_CFG_GMAC_TXDLY_SEL_MASK    GENMASK(16, 12)
#define GMAC0_CFG_GMAC_RXDLY_SEL_SHIFT   18
#define GMAC0_CFG_GMAC_RXDLY_SEL_MASK    GENMASK(22, 18)
#define GMAC0_CFG_GMAC_TXDLY_EN_VAL(v) \
  (((v) << GMAC0_CFG_GMAC_TXDLY_SEL_SHIFT) & GMAC0_CFG_GMAC_TXDLY_SEL_MASK)
#define GMAC0_CFG_GMAC_RXDLY_EN_VAL(v) \
  (((v) << GMAC0_CFG_GMAC_RXDLY_SEL_SHIFT) & GMAC0_CFG_GMAC_RXDLY_SEL_MASK)

#define AIC_GMAC_REFCLK_FREQ 50000000UL
#define AIC_PHY_CLKOUT_FREQ  25000000UL

static void aicmac_syscfg_init(uint32_t port)
{
  uint32_t val;

  if (port != 0)
    {
      return;
    }

  hal_clk_enable_deassertrst(CLK_SYSCFG);

  val = readl(GMAC0_CFG);

  /* D133CBS-QFN88-V1-2 uses GMAC0 RMII. */
  val &= ~GMAC0_CFG_PHY_RGMII_SEL;

#ifdef CONFIG_AIC_DEV_GMAC0_PHY_EXTCLK
  val |= GMAC0_CFG_RMII_EXTCLK_SEL;
#else
  val &= ~GMAC0_CFG_RMII_EXTCLK_SEL;
#endif

#if defined(CONFIG_AIC_DEV_GMAC0_TXDELAY) && CONFIG_AIC_DEV_GMAC0_TXDELAY
  val &= ~GMAC0_CFG_GMAC_TXDLY_SEL_MASK;
  val |= GMAC0_CFG_GMAC_TXDLY_EN_VAL(CONFIG_AIC_DEV_GMAC0_TXDELAY);
#endif

#if defined(CONFIG_AIC_DEV_GMAC0_RXDELAY) && CONFIG_AIC_DEV_GMAC0_RXDELAY
  val &= ~GMAC0_CFG_GMAC_RXDLY_SEL_MASK;
  val |= GMAC0_CFG_GMAC_RXDLY_EN_VAL(CONFIG_AIC_DEV_GMAC0_RXDELAY);
#endif

  writel(val, GMAC0_CFG);
}

void aicmac_low_level_init(uint32_t port, bool en)
{
  uint32_t id = CLK_GMAC0 + port;

  if (en)
    {
      aicmac_syscfg_init(port);

#ifdef CONFIG_AIC_DEV_GMAC0_CLKOUT2_25M
      if (port == 0)
        {
          hal_clk_set_freq(CLK_OUT2, AIC_PHY_CLKOUT_FREQ);
          hal_clk_enable_iter(CLK_OUT2);
        }
#endif

      hal_clk_set_freq(id, AIC_GMAC_REFCLK_FREQ);
      hal_clk_enable_deassertrst_iter(id);
    }
  else
    {
      hal_clk_disable_assertrst(id);
    }
}

static void aicmac_align_cache_range(uintptr_t addr, uint32_t len,
                                     uintptr_t *aligned_addr,
                                     uint32_t *aligned_len)
{
  uintptr_t start = addr & ~(uintptr_t)(CACHE_LINE_SIZE - 1);
  uintptr_t end = (addr + len + CACHE_LINE_SIZE - 1) &
                  ~(uintptr_t)(CACHE_LINE_SIZE - 1);

  *aligned_addr = start;
  *aligned_len = (uint32_t)(end - start);
}

void aicmac_dcache_clean(uintptr_t addr, uint32_t len)
{
  uintptr_t aligned_addr;
  uint32_t aligned_len;

  if (len == 0)
    {
      return;
    }

  aicmac_align_cache_range(addr, len, &aligned_addr, &aligned_len);
  aicos_dcache_clean_range((unsigned long *)aligned_addr, aligned_len);
}

void aicmac_dcache_invalid(uintptr_t addr, uint32_t len)
{
  uintptr_t aligned_addr;
  uint32_t aligned_len;

  if (len == 0)
    {
      return;
    }

  aicmac_align_cache_range(addr, len, &aligned_addr, &aligned_len);
  aicos_dcache_invalid_range((unsigned long *)aligned_addr, aligned_len);
}

void aicmac_gdma_sync(void)
{
  aicos_dma_sync();
}
