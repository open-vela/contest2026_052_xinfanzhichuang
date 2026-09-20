/****************************************************************************
 * arch/risc-v/src/chip/drv/wdt/aic_drv_wdt.c
 *
 * Copyright (c) 2022-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: matteo <duanmt@artinchip.com>
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/timers/watchdog.h>
#include <errno.h>
#include <debug.h>
#include <syslog.h>

#include <aic_core.h>
#include <aic_hal.h>
#include <hal_wdt.h>
#include <aic_drv_wdt.h>
#include <aic_hal_clk.h>
#include <aic_hal_reset.h>
#include <hal_wri.h>
#include <aic_io.h>

#ifdef CONFIG_WATCHDOG

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define AIC_WDT_DEFAULT_CLR_THD 3
#define NUTTX_WDT_IRQ (RISCV_IRQ_ASYNC + WDT_IRQn)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct aic_wdt_lowerhalf_s {
  const struct watchdog_ops_s *ops;
  struct aic_wdt chan;
  uint32_t cur_chan;
  bool started;
  xcpt_t handler; /* User-space watchdog timeout handler */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int aic_wdt_start(FAR struct watchdog_lowerhalf_s *lower);
static int aic_wdt_stop(FAR struct watchdog_lowerhalf_s *lower);
static int aic_wdt_keepalive(FAR struct watchdog_lowerhalf_s *lower);
static int aic_wdt_getstatus(FAR struct watchdog_lowerhalf_s *lower,
                             FAR struct watchdog_status_s *status);
static int aic_wdt_settimeout(FAR struct watchdog_lowerhalf_s *lower,
                              uint32_t timeout);
static xcpt_t aic_wdt_capture(FAR struct watchdog_lowerhalf_s *lower,
                              xcpt_t handler);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct watchdog_ops_s g_aic_wdt_ops = {
    .start = aic_wdt_start,
    .stop = aic_wdt_stop,
    .keepalive = aic_wdt_keepalive,
    .getstatus = aic_wdt_getstatus,
    .settimeout = aic_wdt_settimeout,
    .capture = aic_wdt_capture,
    .ioctl = NULL,
};

static struct aic_wdt_lowerhalf_s g_aic_wdt_lower = {
    .ops = &g_aic_wdt_ops,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: aic_wdt_interrupt
 *
 * Description:
 *   WDT interrupt handler.
 *
 ****************************************************************************/

static int aic_wdt_interrupt(int irq, FAR void *context, FAR void *arg) {
  FAR struct aic_wdt_lowerhalf_s *priv = (FAR struct aic_wdt_lowerhalf_s *)arg;

  /* Clear the interrupt status in HW */

  hal_wdt_clr_int();

  /* If a capture handler has been registered, call it now */

  if (priv->handler) {
    priv->handler(irq, context, NULL);
  }

  return OK;
}

static int aic_wdt_start(FAR struct watchdog_lowerhalf_s *lower) {
  FAR struct aic_wdt_lowerhalf_s *priv =
      (FAR struct aic_wdt_lowerhalf_s *)lower;
  u32 cur = 0;

  syslog(LOG_ERR, "aic_wdt_start: chan=%lu, handler=%p\n",
         (unsigned long)priv->cur_chan, priv->handler);
  wdinfo("Start WDT chan %d\n", priv->cur_chan);

  /* Set the reset cause to watchdog reset */
  cur = readl(WRI_BOOT_INFO);
  cur = (cur & ~WRI_REBOOT_REASON_MASK) | (2 << WRI_REBOOT_REASON_SHIFT);
  writel(cur, WRI_BOOT_INFO);

  /* If we have a handler, enable WDT interrupt */

  if (priv->handler) {
    up_enable_irq(NUTTX_WDT_IRQ);
  }

  hal_wdt_enable(1, 0);
  priv->started = true;
  return OK;
}

static int aic_wdt_stop(FAR struct watchdog_lowerhalf_s *lower) {
  FAR struct aic_wdt_lowerhalf_s *priv =
      (FAR struct aic_wdt_lowerhalf_s *)lower;

  syslog(LOG_ERR, "aic_wdt_stop: chan=%lu\n", (unsigned long)priv->cur_chan);
  wdinfo("Stop WDT chan %d\n", priv->cur_chan);
  hal_wdt_enable(0, 0);
  up_disable_irq(NUTTX_WDT_IRQ);
  priv->started = false;
  return OK;
}

static int aic_wdt_keepalive(FAR struct watchdog_lowerhalf_s *lower) {
  FAR struct aic_wdt_lowerhalf_s *priv =
      (FAR struct aic_wdt_lowerhalf_s *)lower;

  syslog(LOG_ERR, "aic_wdt_keepalive: chan=%lu\n",
         (unsigned long)priv->cur_chan);
  hal_wdt_op_clr(priv->chan.clr_thd);
  hal_wdt_clr_int();
  return OK;
}

static int aic_wdt_getstatus(FAR struct watchdog_lowerhalf_s *lower,
                             FAR struct watchdog_status_s *status) {
  FAR struct aic_wdt_lowerhalf_s *priv =
      (FAR struct aic_wdt_lowerhalf_s *)lower;

  status->flags = 0;
  if (priv->started) {
    status->flags |= WDFLAGS_ACTIVE;
  }

  if (priv->handler) {
    status->flags |= WDFLAGS_CAPTURE;
  } else {
    status->flags |= WDFLAGS_RESET;
  }

  status->timeout = priv->chan.timeout * 1000;              /* ms */
  status->timeleft = (uint32_t)hal_wdt_remain(&priv->chan); /* ms */

  return OK;
}

static int aic_wdt_settimeout(FAR struct watchdog_lowerhalf_s *lower,
                              uint32_t timeout) {
  FAR struct aic_wdt_lowerhalf_s *priv =
      (FAR struct aic_wdt_lowerhalf_s *)lower;
  uint32_t sec = (timeout + 500) / 1000;

  syslog(LOG_ERR, "aic_wdt_settimeout: timeout=%lu, sec=%lu\n",
         (unsigned long)timeout, (unsigned long)sec);

  if (sec == 0) {
    sec = 1;
  }

  priv->chan.timeout = sec;
  priv->chan.rst_thd = sec;

  /* If we are using interrupt (capture mode), set the IRQ threshold */

  if (priv->handler) {
    priv->chan.irq_thd = (sec > 1) ? (sec - 1) : 0;
    hal_wdt_irq_thd_set(priv->cur_chan, &priv->chan);
  }

  hal_wdt_rst_thd_set(priv->cur_chan, &priv->chan);
  hal_wdt_op_clr(0);

  return OK;
}

static xcpt_t aic_wdt_capture(FAR struct watchdog_lowerhalf_s *lower,
                              xcpt_t handler) {
  FAR struct aic_wdt_lowerhalf_s *priv =
      (FAR struct aic_wdt_lowerhalf_s *)lower;
  irqstate_t flags;
  xcpt_t oldhandler;
  uint32_t old_rst_thd;

  flags = up_irq_save();
  oldhandler = priv->handler;
  old_rst_thd = priv->chan.rst_thd;
  priv->handler = handler;

  if (priv->started) {
    if (handler) {
      /* Switching to capture mode */
      /* Temporarily increase reset threshold to 5 seconds to avoid reset during
       * test */
      priv->chan.rst_thd = 5;
      /* Set IRQ threshold to 4 seconds to trigger before reset */
      priv->chan.irq_thd = 4;
      hal_wdt_rst_thd_set(priv->cur_chan, &priv->chan);
      hal_wdt_irq_thd_set(priv->cur_chan, &priv->chan);

      /* Clear the counter to start from 0 */
      hal_wdt_op_clr(0);
      /* Enable WDT interrupt in hardware */
      hal_wdt_irq_enable(1);
      /* Enable IRQ in CPU */
      up_enable_irq(NUTTX_WDT_IRQ);
    } else {
      /* Switching to reset mode */
      /* Restore original reset threshold */
      priv->chan.rst_thd = old_rst_thd;
      /* Restore original IRQ threshold */
      priv->chan.irq_thd = (old_rst_thd > 1) ? (old_rst_thd - 1) : 0;
      hal_wdt_rst_thd_set(priv->cur_chan, &priv->chan);
      hal_wdt_irq_thd_set(priv->cur_chan, &priv->chan);
      /* Clear the counter to start from 0 */
      hal_wdt_op_clr(0);
      /* Disable WDT interrupt in hardware */
      hal_wdt_irq_enable(0);
      /* Disable IRQ in CPU */
      up_disable_irq(NUTTX_WDT_IRQ);
    }
  }

  up_irq_restore(flags);
  return oldhandler;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int aic_wdt_initialize(void) {
  int ret = 0;

  syslog(LOG_ERR, "aic_wdt_initialize: entering\n");

  /* Clock & Reset */

  ret = hal_clk_enable(CLK_WDT);
  if (ret < 0) {
    wderr("WDT clk enable failed: %d\n", ret);
    return ret;
  }

  hal_reset_assert(RESET_WDT);
  hal_reset_deassert(RESET_WDT);

  /* Initialize private data */

  g_aic_wdt_lower.cur_chan = 0;
  g_aic_wdt_lower.handler = NULL;
  hal_wdt_thd_get(g_aic_wdt_lower.cur_chan, &g_aic_wdt_lower.chan);

  /* Attach the WDT interrupt */

  irq_attach(NUTTX_WDT_IRQ, aic_wdt_interrupt, &g_aic_wdt_lower);

  /* Default clear threshold - set to 0 to allow immediate counter reset on
   * keepalive */

  g_aic_wdt_lower.chan.clr_thd = 0;
  hal_wdt_clr_thd_set(g_aic_wdt_lower.cur_chan, &g_aic_wdt_lower.chan);

  /* Register the watchdog driver */

  void *wdt_handle = watchdog_register(
      "/dev/watchdog0", (FAR struct watchdog_lowerhalf_s *)&g_aic_wdt_lower);
  if (wdt_handle == NULL) {
    wderr("watchdog_register failed\n");
    return -1;
  }
  return OK;
}

#endif /* CONFIG_WATCHDOG */
