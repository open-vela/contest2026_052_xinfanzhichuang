/*
 * Copyright (c) 2022-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: Siyao Li <siyao.li@artinchip.com>
 */

#include <stdbool.h>
#include <getopt.h>
#include <string.h>
#include "aic_core.h"
#include "hal_gpai.h"
#include "hal_adcim.h"
#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>
#include <stdint.h>
#include <errno.h>
#include <debug.h>
#include <nuttx/irq.h>
#include <nuttx/clock.h>
#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>
#include <syslog.h>
#include <debug.h>

extern const int aic_gpai_chs_size;
#define AIC_GPADC_DEV_PATH  "/dev/gpadc"

/*
 * GPAI register access.
 *
 * These complement the HAL API for register-level operations that the
 * public interface does not expose (e.g. FIFO threshold tweaks and
 * single-shot re-trigger in the ISR callback).
 */
#define gpai_reg_read(reg)         readl(GPAI_BASE + (reg))
#define gpai_reg_write(val, reg)   writel(val, GPAI_BASE + (reg))

/* Max channels defined by Kconfig (GPAI0-GPAI23).
 * D13X defines AIC_GPAI_CH_NUM=8 via aic_soc.h.
 * Use a fixed maximum that covers both SoC variants.
 */
#ifndef AIC_GPAI_CH_NUM
#define AIC_GPAI_CH_NUM  24
#endif
#define AIC_GPAI_CH_MAX  AIC_GPAI_CH_NUM

/* ------------------------------------------------------------------ */
/*  Lower-half device context                                          */
/*  Modeled after GPADC's lower-half struct — holds per-device state. */
/* ------------------------------------------------------------------ */
struct aic_gpadc_lowerhalf_s
{
    struct adc_dev_s gpadc_s;
    FAR const struct adc_callback_s *cb;
    int am_channel;
    void *arg;
    volatile bool data_delivered; /* Guard: ISR callback sets this after
                                   * au_receive; aic_read_work skips its
                                   * own delivery when true. */
};

/* ------------------------------------------------------------------ */
/*  Forward declarations                                              */
/* ------------------------------------------------------------------ */

static int  aic_gpadc_bind(FAR struct adc_dev_s *dev,
                           FAR const struct adc_callback_s *callback);
static void aic_gpadc_reset(FAR struct adc_dev_s *dev);
static int  aic_gpadc_setup(FAR struct adc_dev_s *dev);
static void aic_gpadc_shutdown(FAR struct adc_dev_s *dev);
static void aic_gpadc_rxint(FAR struct adc_dev_s *dev, bool enable);
static int  aic_gpadc_ioctl(FAR struct adc_dev_s *dev, int cmd,
                            unsigned long arg);
static void aic_read_work(FAR void *param);
static void aic_gpai_isr_callback(void *arg);
static void aic_trigger_conv(FAR struct adc_dev_s *dev);

/* ------------------------------------------------------------------ */
/*  ADC operations table (GPADC pattern: clean ops struct)             */
/* ------------------------------------------------------------------ */

static const struct adc_ops_s g_gpadcops =
{
    .ao_bind      = aic_gpadc_bind,
    .ao_reset     = aic_gpadc_reset,
    .ao_setup     = aic_gpadc_setup,
    .ao_shutdown  = aic_gpadc_shutdown,
    .ao_rxint     = aic_gpadc_rxint,
    .ao_ioctl     = aic_gpadc_ioctl,
};

static struct aic_gpadc_lowerhalf_s g_gpadcdev[AIC_GPAI_CH_MAX];

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */


//
static int drv_gpai_chan_init(int ch, int enabled)
{
    struct aic_gpai_ch *chan;

    chan = hal_gpai_ch_is_valid(ch);
    if (!chan)
        return -EINVAL;

    hal_gpai_clk_get(chan);

    if (enabled) {
        aich_gpai_ch_init(chan, chan->pclk_rate);
        chan->irq_count = 0;
        if (chan->mode == AIC_GPAI_MODE_SINGLE) {
            chan->irq_count++;
            chan->complete = aicos_sem_create(0);
        }
    } else {
        aich_gpai_ch_enable(chan->id, 0);
        if (chan->mode == AIC_GPAI_MODE_SINGLE) {
            aicos_sem_delete(chan->complete);
            chan->complete = NULL;
        }
    }
    return OK;
}

/*
 * Trigger a single conversion via register write (non-blocking).
 * Used by ISR callback to re-trigger after each completion.
 */
static void aic_trigger_conv(FAR struct adc_dev_s *dev)
{
    FAR struct aic_gpadc_lowerhalf_s *priv =
        (FAR struct aic_gpadc_lowerhalf_s *)dev->ad_priv;
    uint32_t cr;

    cr = gpai_reg_read(GPAI_CHnCR(priv->am_channel));
    cr &= ~GPAI_CHnCR_PERIOD_SAMPLE_EN;
    cr |=  GPAI_CHnCR_SINGLE_SAMPLE_EN;
    gpai_reg_write(cr, GPAI_CHnCR(priv->am_channel));
}

/*
 * Work function: trigger conversion, read result, deliver to upper half.
 *
 * Handles both interrupt and polling modes (GPADC-style dual path):
 *   Interrupt – hal_gpai_get_data() blocks on semaphore; ISR callback
 *               delivers data first (data_delivered → true), then we skip.
 *   Polling   – hal_gpai_get_data() polls HW and returns value; no ISR
 *               runs, so data_delivered is false → we deliver.
 */
static void aic_read_work(FAR void *param)
{
    int channel_in;
    u16 value = 0;
    FAR struct adc_dev_s *dev = param;
    DEBUGASSERT(dev);
    FAR struct aic_gpadc_lowerhalf_s *priv =
        (FAR struct aic_gpadc_lowerhalf_s *)dev->ad_priv;
    DEBUGASSERT(priv);

    channel_in = priv->am_channel;
    struct aic_gpai_ch *chan = hal_gpai_ch_is_valid(channel_in);
    if (!chan) {
        hal_log_err("gpadc: invalid channel %d\n", channel_in);
        return;
    }

    if (hal_gpai_get_data(chan, &value, AIC_GPAI_TIMEOUT) < 0) {
        hal_log_err("gpadc: read channel %d failed\n", channel_in);
        return;
    }
    syslog(LOG_INFO, "gpadc: read channel %d success, value: %d, data_delivered: %d\n",
           channel_in, value, priv->data_delivered);

    /* Deliver to upper-half FIFO if ISR hasn't already done so.
     * (In polling mode, data_delivered stays false — always deliver.) */
    if (priv->cb && priv->cb->au_receive && !priv->data_delivered) {
        syslog(LOG_INFO, "gpadc: deliver channel %d, value: %d\n",
               channel_in, value);
        priv->cb->au_receive(dev, channel_in, (uint32_t)value);
    }
    priv->data_delivered = false;

    syslog(LOG_INFO, "gpadc: read channel %d success, value: %d\n",
           channel_in, value);
}

/*
 * ISR callback: deliver sample to upper-half FIFO and re-trigger.
 *
 * In interrupt mode, this runs from aich_gpai_isr() after
 * hal_gpai_irq_read_fifo() stores the result in chan->avg_data.
 * In polling mode this callback is never reached (no ISR attached).
 */
static void aic_gpai_isr_callback(void *arg)
{
    FAR struct adc_dev_s *dev = arg;
    FAR struct aic_gpadc_lowerhalf_s *priv =
        (FAR struct aic_gpadc_lowerhalf_s *)dev->ad_priv;
    struct aic_gpai_ch *chan = hal_gpai_ch_is_valid(priv->am_channel);

    if (!chan || !priv->cb || !priv->cb->au_receive)
        return;

    syslog(LOG_INFO, "[%s:%d] gpadc: ISR callback, ch %d, avg_data %d\n",
           __func__, __LINE__, priv->am_channel, chan->avg_data);

    /* Deliver to upper-half FIFO */
    priv->cb->au_receive(dev, priv->am_channel, (uint32_t)chan->avg_data);

    /* Mark so aic_read_work won't double-deliver */
    priv->data_delivered = true;

    /* Re-trigger next conversion for continuous streaming */
    aic_trigger_conv(dev);
}

/* ------------------------------------------------------------------ */
/*  ADC operations implementations (GPADC pattern)                     */
/* ------------------------------------------------------------------ */

static int aic_gpadc_bind(FAR struct adc_dev_s *dev,
                          FAR const struct adc_callback_s *callback)
{
    FAR struct aic_gpadc_lowerhalf_s *priv =
        (FAR struct aic_gpadc_lowerhalf_s *)dev->ad_priv;
    DEBUGASSERT(priv);

    priv->cb = callback;
    return OK;
}

static void aic_gpadc_reset(FAR struct adc_dev_s *dev)
{
    /* Not needed — HW reset is handled by hal_gpai_init() */
}

static int aic_gpadc_setup(FAR struct adc_dev_s *dev)
{
    int ret;
    int channel;
    FAR struct aic_gpadc_lowerhalf_s *priv =
        (FAR struct aic_gpadc_lowerhalf_s *)dev->ad_priv;
    struct aic_gpai_ch *chan = NULL;
    DEBUGASSERT(priv);

    /* Initialize GPAI module if not already enabled */
    int st = aich_gpai_is_enable();
    ainfo("[%s:%d] gpadc_setup, st: %d\n", __func__, __LINE__, st);

    if (!st) {
        ret = hal_gpai_init();
        if (ret < 0) {
            hal_log_err("gpadc hal init failed!\n");
            return -1;
        }
        aich_gpai_enable(1);
        hal_gpai_set_ch_num(aic_gpai_chs_size);
    }

    /* Initialize the requested channel */
    channel = priv->am_channel;
    chan = hal_gpai_ch_is_valid(channel);
    if (!chan) {
        hal_log_err("gpadc channel %d is unavailable!\n", channel);
        return -EINVAL;
    }
    chan->irq_info.callback = aic_gpai_isr_callback;
    chan->irq_info.callback_param = dev;
    ainfo("[%s:%d] gpadc_setup, channel: %d\n", __func__, __LINE__, channel);

    /* Calibrate ADCIM before channel init */
    hal_adcim_auto_calibration();

    ret = drv_gpai_chan_init(channel, true);
    if (ret != OK) {
      if (!aich_gpai_other_chan_status(channel)) {
            ainfo("[%s:%d] drv_gpai_chan_init, channel: failed %d clean gpai\n", __func__, __LINE__, channel);
            hal_gpai_set_ch_num(0);
            drv_gpai_chan_init(channel, false);
            hal_gpai_deinit();
        }
        ainfo("[%s:%d] gpadc_setup, channel failed: %d, ret: %d\n",
              __func__, __LINE__, channel, ret);
        return -1;
    }
    hal_gpai_read_reg(channel);
    return OK;
}

static void aic_gpadc_shutdown(FAR struct adc_dev_s *dev)
{
    FAR struct aic_gpadc_lowerhalf_s *priv =
        (FAR struct aic_gpadc_lowerhalf_s *)dev->ad_priv;
    DEBUGASSERT(priv);

    int channel_in = priv->am_channel;

    /* Shut down GPAI module if no other channel is active */
    if (!aich_gpai_other_chan_status(channel_in)) {
        aich_gpai_enable(0);
        hal_gpai_set_ch_num(0);
        drv_gpai_chan_init(channel_in, false);
        hal_gpai_deinit();
    }
}

static void aic_gpadc_rxint(FAR struct adc_dev_s *dev, bool enable)
{
    FAR struct aic_gpadc_lowerhalf_s *priv =
        (FAR struct aic_gpadc_lowerhalf_s *)dev->ad_priv;
    DEBUGASSERT(priv);

    int channel = priv->am_channel;
    uint32_t val;

    if (enable) {
        /*
         * Flush any stale FIFO state: reading DATA while the FIFO is
         * empty sets UF_STS.  If we don't clear this first, a spurious
         * FIFO_ERR fires before DRDY.
         */
        val = gpai_reg_read(GPAI_CHnFCR(channel));
        if (val & GPAI_CHnFCR_UF_STS)
            gpai_reg_write(val | GPAI_CHnFCR_FLUSH, GPAI_CHnFCR(channel));

        /* Set data-ready threshold to 1 */
        val &= ~GPAI_CHnFCR_DAT_RDY_THD_MASK;
        val |= (1 << GPAI_CHnFCR_DAT_RDY_THD_SHIFT);
        gpai_reg_write(val, GPAI_CHnFCR(channel));

        /* Clear any stale FIFO_ERR / DRDY flags */
        val = gpai_reg_read(GPAI_CHnINT(channel));
        gpai_reg_write(val | GPAI_CHnINT_FIFO_ERR_FLAG
                            | GPAI_CHnINT_DRDY_FLG,
                       GPAI_CHnINT(channel));

#ifdef CONFIG_AIC_GPAI_DRV_POLL
        /* Interrupt mode: enable DRDY and FIFO_ERR detail interrupts */
        val |= GPAI_CHnINT_DAT_RDY_IE | GPAI_CHnINT_FIFO_ERR_IE;
        gpai_reg_write(val, GPAI_CHnINT(channel));

        /* Enable channel interrupt in the top-level GPAI_INTR */
        val = gpai_reg_read(GPAI_INTR);
        val |= GPAI_INTR_CH_INT_EN(channel);
        gpai_reg_write(val, GPAI_INTR);
#endif

        /*
         * Trigger first conversion and deliver data to upper half.
         *   Interrupt mode: blocks until ISR delivers, then returns.
         *   Polling mode:  polls HW, delivers, returns.
         */
        // aic_read_work(dev);
        // hal_

    } else {
#ifdef CONFIG_AIC_GPAI_DRV_POLL
        /* Disable channel interrupt in the top-level GPAI_INTR */
        val = gpai_reg_read(GPAI_INTR);
        val &= ~GPAI_INTR_CH_INT_EN(channel);
        gpai_reg_write(val, GPAI_INTR);

        /* Disable detail interrupts */
        val = gpai_reg_read(GPAI_CHnINT(channel));
        val &= ~(GPAI_CHnINT_DAT_RDY_IE | GPAI_CHnINT_FIFO_ERR_IE);
        gpai_reg_write(val, GPAI_CHnINT(channel));
#endif
    }
}

static int aic_gpadc_ioctl(FAR struct adc_dev_s *dev, int cmd,
                           unsigned long arg)
{
    FAR struct aic_gpadc_lowerhalf_s *priv =
        (FAR struct aic_gpadc_lowerhalf_s *)dev->ad_priv;
    DEBUGASSERT(priv);

    switch (cmd) {
    case ANIOC_TRIGGER:
      pr_info("ANIOC_TRIGGER\n");
        aic_read_work(dev);
        break;

    default:
        wdinfo("Unsupported command: %d\n", cmd);
        return -ENOTTY;
    }

    return OK;
}

/* ------------------------------------------------------------------ */
/*  Public: initialization                                             */
/* ------------------------------------------------------------------ */

int aic_gpadc_initialize(int channel_id)
{
    int ret;
    struct aic_gpadc_lowerhalf_s *aic_dev;
    char dev_path[16];

    snprintf(dev_path, sizeof(dev_path), "%s%d",
             AIC_GPADC_DEV_PATH, channel_id);



    aic_dev = kmm_zalloc(sizeof(struct aic_gpadc_lowerhalf_s));
    if (!aic_dev) {
        hal_log_err("gpadc alloc failed!\n");
        return -ENOMEM;
    }

    /* Store channel ID in both device instance and persistent state */
    g_gpadcdev[channel_id].am_channel = channel_id;
    aic_dev->am_channel = channel_id;

    aic_dev->gpadc_s.ad_ops  = &g_gpadcops;
    aic_dev->gpadc_s.ad_priv = &g_gpadcdev[channel_id];

    ret = adc_register(dev_path, &aic_dev->gpadc_s);
    if (ret < 0) {
        hal_log_err("gpadc register failed!\n");
        syslog(LOG_ERR, "gpadc register failed!\n");
        kmm_free(aic_dev);
        return ret;
    }

    return OK;
}
