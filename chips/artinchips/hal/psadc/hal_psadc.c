/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aic_core.h"
#include "aic_hal_clk.h"
#include "aic_time.h"
#include "hal_psadc.h"

#include <errno.h>
#include <syslog.h>

/* D13X PSADC controller register map. */
#define AIC_PSADC_CLK_RATE      40000000UL

#define PSADC_MCR               0x000
#define PSADC_TCR               0x004
#define PSADC_NODE1             0x008
#define PSADC_MSR               0x010
#define PSADC_Q1FCR             0x020
#define PSADC_Q1FDR             0x040

#define PSADC_MCR_Q1_TRIGS      BIT(22)
#define PSADC_MCR_Q1_INTE       BIT(18)
#define PSADC_MCR_QUE_COMB      BIT(1)
#define PSADC_MCR_EN            BIT(0)

#define PSADC_MSR_Q1_FERR       BIT(2)
#define PSADC_MSR_Q1_INT        BIT(0)

#define PSADC_FCR_DCNT_MASK     GENMASK(28, 24)
#define PSADC_FCR_DCNT_SHIFT    24
#define PSADC_FCR_DRTH_SHIFT    8
#define PSADC_FCR_FIFO_ERRIE    BIT(3)
#define PSADC_FCR_FIFO_RDYIE    BIT(1)
#define PSADC_FCR_FIFO_FLUSH    BIT(0)
#define PSADC_FCR_UF_STS        BIT(17)
#define PSADC_FCR_OF_STS        BIT(16)

#define PSADC_Q1FDR_DATA_MASK   GENMASK(11, 0)
#define PSADC_Q1_NODE_MASK      GENMASK(3, 0)

static bool g_psadc_initialized;

static inline void psadc_writel(u32 value, u32 offset)
{
  writel(value, PSADC_BASE + offset);
}

static inline u32 psadc_readl(u32 offset)
{
  return readl(PSADC_BASE + offset);
}

static void psadc_fifo_flush(void)
{
  u32 value = psadc_readl(PSADC_Q1FCR);

  if (value & PSADC_FCR_UF_STS)
    {
      syslog(LOG_ERR, "PSADC Q1 FIFO underflow: FCR=%#x\n", value);
    }

  if (value & PSADC_FCR_OF_STS)
    {
      syslog(LOG_ERR, "PSADC Q1 FIFO overflow: FCR=%#x\n", value);
    }

  psadc_writel(value | PSADC_FCR_FIFO_FLUSH, PSADC_Q1FCR);
}

static void psadc_clear_status(void)
{
  u32 status = psadc_readl(PSADC_MSR);

  if (status != 0)
    {
      psadc_writel(status, PSADC_MSR);
    }
}

static void psadc_set_q1_channel(u32 channel)
{
  psadc_writel(channel & PSADC_Q1_NODE_MASK, PSADC_NODE1);
}

static void psadc_dump_timeout(void)
{
  syslog(LOG_ERR,
         "PSADC Q1 timeout: MCR=%#x MSR=%#x NODE1=%#x TCR=%#x "
         "Q1FCR=%#x Q1DCNT=%u\n",
         psadc_readl(PSADC_MCR),
         psadc_readl(PSADC_MSR),
         psadc_readl(PSADC_NODE1),
         psadc_readl(PSADC_TCR),
         psadc_readl(PSADC_Q1FCR),
         (psadc_readl(PSADC_Q1FCR) & PSADC_FCR_DCNT_MASK) >>
         PSADC_FCR_DCNT_SHIFT);
}

int hal_psadc_init(void)
{
  int ret;

  if (g_psadc_initialized)
    {
      return 0;
    }

  ret = hal_clk_set_freq(CLK_PSADC, AIC_PSADC_CLK_RATE);
  if (ret < 0)
    {
      return ret;
    }

  ret = hal_clk_enable_deassertrst(CLK_PSADC);
  if (ret < 0)
    {
      return ret;
    }

  /*
   * QC is configured as one Q1 node. The threshold is one sample, matching
   * the RT-Thread test path used on D13X.
   */
  psadc_writel(0, PSADC_NODE1);
  psadc_writel(0, PSADC_TCR);
  psadc_writel(PSADC_FCR_FIFO_ERRIE |
               PSADC_FCR_FIFO_RDYIE |
               (1U << PSADC_FCR_DRTH_SHIFT), PSADC_Q1FCR);
  psadc_fifo_flush();
  psadc_clear_status();
  psadc_writel(PSADC_MCR_QUE_COMB | PSADC_MCR_EN |
               PSADC_MCR_Q1_INTE, PSADC_MCR);

  g_psadc_initialized = true;
  return 0;
}

void hal_psadc_deinit(void)
{
  if (!g_psadc_initialized)
    {
      return;
    }

  psadc_writel(0, PSADC_MCR);
  psadc_fifo_flush();
  psadc_clear_status();
  (void)hal_clk_disable_assertrst(CLK_PSADC);
  g_psadc_initialized = false;
}

int hal_psadc_read_channel_poll(u32 channel, u32 *value, u32 timeout_ms)
{
  u32 status;
  u64 start_us;
  u64 timeout_us;
  int ret;

  if (value == NULL)
    {
      return -EINVAL;
    }

  if (channel >= AIC_PSADC_CHANNEL_COUNT)
    {
      return -EINVAL;
    }

  ret = hal_psadc_init();
  if (ret < 0)
    {
      return ret;
    }

  /*
   * The button service uses one Q1 node. Rewriting NODE1 on each read keeps
   * this helper deterministic if another caller changes the selected input.
   */
  psadc_set_q1_channel(channel);
  psadc_writel(0, PSADC_TCR);
  psadc_fifo_flush();
  psadc_clear_status();

  status = psadc_readl(PSADC_MCR);
  psadc_writel(status | PSADC_MCR_Q1_TRIGS, PSADC_MCR);

  start_us = aic_get_time_us();
  timeout_us = (u64)timeout_ms * 1000ULL;
  for (;;)
    {
      status = psadc_readl(PSADC_MSR);

      if (status != 0)
        {
          psadc_writel(status, PSADC_MSR);
        }

      if (status & PSADC_MSR_Q1_INT)
        {
          *value = psadc_readl(PSADC_Q1FDR) & PSADC_Q1FDR_DATA_MASK;
          return 0;
        }

      if (status & PSADC_MSR_Q1_FERR)
        {
          psadc_fifo_flush();
        }

      if (timeout_ms != 0 &&
          (aic_get_time_us() - start_us) >= timeout_us)
        {
          psadc_dump_timeout();
          return -ETIMEDOUT;
        }

      aic_udelay(1);
    }
}
