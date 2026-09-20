/*
 * Copyright (c) 2022-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: matteo <duanmt@artinchip.com>
 */

#include "aic_core.h"
#include "hal_gpai.h"
#include "aic_hal_clk.h"
#include "hal_dma.h"
#include "aic_osal.h"
#include "aic_soc.h"
#include <syslog.h>

/* Register definition of GPAI Controller */
#if defined(CONFIG_AIC_GPAI_DRV_V10) || defined(CONFIG_AIC_GPAI_DRV_V11)
#define GPAI_CONTROLLER_V1
#endif

void hal_gpai_read_reg(int chan);

extern struct aic_gpai_ch aic_gpai_chs[];
static u32 aic_gpai_ch_num = 0; // the number of available channel

static inline void gpai_writel(u32 val, int reg)
{
    writel(val, GPAI_BASE + reg);
}

static inline u32 gpai_readl(int reg)
{
    return readl(GPAI_BASE + reg);
}

static u16 gpai_vol2data(s32 vol)
{
    return vol;
}

static u32 gpai_ms2itv(u32 pclk_rate, u32 us)
{
    u32 tmp;

    if (unlikely(pclk_rate < 1000000))
        return us;                     /* fallback: 1 count ≈ 1 µs */

    tmp = pclk_rate / 1000000;
    tmp *= us;
    return tmp;
}

static void gpai_reg_enable(int offset, int bit, int enable)
{
    int tmp = gpai_readl(offset);

    if (enable)
        tmp |= bit;
    else
        tmp &= ~bit;

    gpai_writel(tmp, offset);
}

int aich_gpai_is_enable(void)
{
    return gpai_readl(GPAI_MCR)&0x01;
}

void aich_gpai_enable(int enable)
{
    gpai_reg_enable(GPAI_MCR, GPAI_MCR_EN, enable);
}

void aich_gpai_ch_enable(u32 ch, int enable)
{
    gpai_reg_enable(GPAI_MCR, GPAI_MCR_CH_EN(ch), enable);
}

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

static void gpai_int_enable(u32 ch, u32 enable, u32 detail)
{
    u32 val = gpai_readl(GPAI_INTR);
    if (enable) {
        val |= GPAI_INTR_CH_INT_EN(ch);
        gpai_writel(detail, GPAI_CHnINT(ch));
    } else {
        val &= ~GPAI_INTR_CH_INT_EN(ch);
        gpai_writel(0, GPAI_CHnINT(ch));
    }
    gpai_writel(val, GPAI_INTR);
}

static void gpai_fifo_init(u32 ch, u8 thd)
{
    u32 val = 0;
    u8 max_thd = (ch == 0) ? 0x20 : 0x8;

    if (thd > max_thd || thd == 0)
        thd = 1;

    val = (u32)thd << GPAI_CHnFCR_DAT_RDY_THD_SHIFT;
    gpai_writel(val, GPAI_CHnFCR(ch));
}

static void gpai_fifo_flush(u32 ch)
{
    u32 val = gpai_readl(GPAI_CHnFCR(ch));

    if (val & GPAI_CHnFCR_UF_STS)
        pr_err("ch%d FIFO is Underflow!%#x\n", ch, val);
    if (val & GPAI_CHnFCR_OF_STS)
        pr_err("ch%d FIFO is Overflow!%#x\n", ch, val);

    gpai_writel(val | GPAI_CHnFCR_FLUSH, GPAI_CHnFCR(ch));
}

/*
 * Program SBC and ADC_ACQ once into channel CR.
 * Separated from gpai_single_mode() so aich_gpai_ch_init() can
 * call it at init time, avoiding re-program on every read.
 */
static void gpai_set_sbc_acq(u32 ch, struct aic_gpai_ch *chan)
{
    u32 val = gpai_readl(GPAI_CHnCR(ch));
    val &= ~(GPAI_CHnCR_ADC_ACQ_MASK | GPAI_CHnCR_SBC_MASK);
    val |= (GPAI_CHnCR_SBC_8_POINTS << GPAI_CHnCR_SBC_SHIFT)
         | ((u32)(chan->adc_acq) << GPAI_CHnCR_ADC_ACQ_SHIFT);
    gpai_writel(val, GPAI_CHnCR(ch));
}

static void gpai_single_mode(u32 ch)
{
    u32 val = gpai_readl(GPAI_CHnCR(ch));
    val |= GPAI_CHnCR_SINGLE_SAMPLE_EN;
    gpai_writel(val, GPAI_CHnCR(ch));

#ifdef CONFIG_AIC_GPAI_DRV_POLL
    /* Interrupt mode: enable top-level INTEN + detail IEs.
     * CPU IRQ fires → ISR reads data → gives semaphore. */
    gpai_int_enable(ch, 1,
                    GPAI_CHnINT_DAT_RDY_IE | GPAI_CHnINT_FIFO_ERR_IE);
#else
    /* Polling mode:
     *
     * GPAI_INTR CHn_FLG = OR(event_flag & corresponding_IE).
     * Set detail IEs so CHn_FLG propagates for polling to see.
     * Keep CHn_INTEN = 0 → no CPU IRQ fires.
     *
     * hal_gpai_read_poll() polls GPAI_INTR CHn_FLG and,
     * once detected, reads GPAI_CHnINT and W1C clears it. */
    gpai_writel(GPAI_CHnINT_DAT_RDY_IE | GPAI_CHnINT_FIFO_ERR_IE,
                   GPAI_CHnINT(ch));
#endif
}

/* Only in period mode, HLA and LLA are available */
static void gpai_period_mode(struct aic_gpai_ch *chan, u32 pclk)
{
    u32 val, acr = 0, ch = chan->id;
    u32 detail = GPAI_CHnINT_DAT_RDY_IE | GPAI_CHnINT_FIFO_ERR_IE;

    if (chan->hla_enable) {
        detail |= GPAI_CHnINT_HLA_RM_IE | GPAI_CHnINT_HLA_VALID_IE;
        val = ((gpai_vol2data(chan->hla_rm_thd) << GPAI_CHnLAT_HLLA_RM_THD_SHIFT)
            & GPAI_CHnLAT_HLLA_RM_THD_MASK)
            | (gpai_vol2data(chan->hla_thd) & GPAI_CHnLAT_HLLA_THD_MASK);
        gpai_writel(val, GPAI_CHnHLAT(ch));
        acr |= GPAI_CHnACR_HLA_EN;
    }

    if (chan->lla_enable) {
        detail |= GPAI_CHnINT_LLA_VALID_IE | GPAI_CHnINT_LLA_RM_IE;
        val = ((gpai_vol2data(chan->lla_rm_thd) << GPAI_CHnLAT_HLLA_RM_THD_SHIFT)
            & GPAI_CHnLAT_HLLA_RM_THD_MASK)
            | (gpai_vol2data(chan->lla_thd) & GPAI_CHnLAT_HLLA_THD_MASK);
        gpai_writel(val, GPAI_CHnLLAT(ch));
        acr |= GPAI_CHnACR_LLA_EN;
    }

    if (chan->obtain_data_mode == AIC_GPAI_OBTAIN_DATA_BY_CPU)
        gpai_int_enable(ch, 1, detail);

    gpai_writel(acr, GPAI_CHnACR(ch));

    val = gpai_ms2itv(pclk, chan->smp_period);
    gpai_writel(val, GPAI_CHnPSI(ch));

    val = gpai_readl(GPAI_CHnCR(ch));
    val |= GPAI_CHnCR_SBC_8_POINTS << GPAI_CHnCR_SBC_SHIFT;
    val &= ~GPAI_CHnCR_ADC_ACQ_MASK;
    val |= (u32)(chan->adc_acq) << GPAI_CHnCR_ADC_ACQ_SHIFT;
    gpai_writel(val, GPAI_CHnCR(ch));

    val = gpai_readl(GPAI_CHnCR(ch));
    val |= GPAI_CHnCR_PERIOD_SAMPLE_EN;
    gpai_writel(val, GPAI_CHnCR(ch));
}

/* ------------------------------------------------------------------ */
/*  Public HAL: channel configuration                                  */
/* ------------------------------------------------------------------ */

void hal_gpai_set_high_priority(u32 ch)
{
    u32 val = gpai_readl(GPAI_CHnCR(ch));
    val |= GPAI_CHnCR_HIGH_ADC_PRIORITY;
    gpai_writel(val, GPAI_CHnCR(ch));
}

int aich_gpai_ch_init(struct aic_gpai_ch *chan, u32 pclk)
{
    if (!chan)
        return -EINVAL;

    aich_gpai_ch_enable(chan->id, 1);
    gpai_fifo_init(chan->id, chan->fifo_depth);
    if (chan->mode == AIC_GPAI_MODE_PERIOD) {
        gpai_period_mode(chan, pclk);
    } else {
        /* Program SBC and ADC_ACQ once at init time for single mode,
         * rather than re-programming them in every gpai_single_mode()
         * call. */
        gpai_set_sbc_acq(chan->id, chan);
    }

    gpai_int_enable(chan->id, 1,
            GPAI_CHnINT_DAT_RDY_IE | GPAI_CHnINT_FIFO_ERR_IE);   //使能对应通道


    // gpadc_compare_select(channal);      
    // gpadc_channel_enable_lowirq(channal);
    // gpadc_channel_compare_lowdata(channal, COMPARE_LOWDATA);
    // gpadc_channel_compare_highdata(channal, COMPARE_HIGDATA);


    return 0;
}

int aich_gpai_other_chan_status(u32 ch)
{
    u32 mcr = gpai_readl(GPAI_MCR);
    mcr &= ~GPAI_MCR_CH_EN(ch);
    mcr &= 0xff;
    return (int)mcr;
}

void aich_gpai_status_show(struct aic_gpai_ch *chan)
{
    u32 version = gpai_readl(GPAI_VERSION);
    u32 mcr     = gpai_readl(GPAI_MCR);

    printf("In GPAI V%s:\n"
           "Ch Mode Enable Value  LTA  HTA\n"
           "%2d %4s %6d %5d %4d %4d\n",
           EXPAND_BCD_VER(version),
           chan->id, chan->mode ? "P" : "S",
           (mcr & GPAI_MCR_CH_EN(chan->id)) ? 1 : 0,
           chan->avg_data, chan->lla_thd, chan->hla_thd);
}

/* ------------------------------------------------------------------ */
/*  Internal: FIFO read                                                */
/* ------------------------------------------------------------------ */

static int hal_gpai_irq_read_fifo(struct aic_gpai_ch *chan)
{
    u32 i, ch = chan->id;
    u32 cnt = (gpai_readl(GPAI_CHnFCR(ch)) & GPAI_CHnFCR_DAT_CNT_MASK)
              >> GPAI_CHnFCR_DAT_CNT_SHIFT;

    chan->avg_data = 0;

    if (unlikely(cnt == 0 || cnt > GPAI_CHnFCR_DAT_CNT_MAX(ch))) {
        pr_err("ch%d invalid data count %d\n", ch, cnt);
        return -1;
    }

    for (i = 0; i < cnt; i++) {
        chan->fifo_data[i] = (u16)gpai_readl(GPAI_CHnDATA(ch));
        if (chan->mode == AIC_GPAI_MODE_SINGLE)
            chan->avg_data += chan->fifo_data[i];
    }

    chan->fifo_valid_cnt = (u8)cnt;
    if (chan->mode == AIC_GPAI_MODE_SINGLE) {
        chan->avg_data /= cnt;
        syslog(LOG_INFO,
               "There are %d data ready in ch%d, last %d  cnt=%d\n",
               cnt, ch, chan->avg_data, cnt);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Public HAL: data read (poll / interrupt)                           */
/* ------------------------------------------------------------------ */

int hal_gpai_read_poll(u32 ch, u16 *val, u32 timeout)
{
    u32 ch_flag, ch_int;
    struct aic_gpai_ch *chan = NULL;

    while (timeout--) {
        ch_flag = gpai_readl(GPAI_INTR);
        if (!(ch_flag & GPAI_INTR_CH_INT_FLAG(ch))) {
            aicos_mdelay(1);
            continue;
        }

        chan = hal_gpai_ch_is_valid(ch);
        if (!chan)
            return -ENODEV;

        ch_int = gpai_readl(GPAI_CHnINT(ch));
        gpai_writel(ch_int, GPAI_CHnINT(ch));

        if (ch_int & GPAI_CHnINT_DRDY_FLG) {
            hal_gpai_irq_read_fifo(chan);
            if (val)
                val[0] = chan->avg_data;
            break;
        }
    }

    pr_err("Ch%d poll read timeout!\n", ch);
    return -ETIMEDOUT;
}

int hal_gpai_get_data(struct aic_gpai_ch *chan, u16 *val, u32 timeout)
{
    int ret = 0;
    u32 ch;

    if (!chan || !val)
        return -EINVAL;

    ch = chan->id;

    if (!chan->available) {
        syslog(LOG_ERR, "%s:%d] Ch%d is unavailable!\n",
               __func__, __LINE__, chan->id);
        return -ENODATA;
    }

    /* Period mode: data arrives via ISR into FIFO, just copy it out */
    if (chan->mode == AIC_GPAI_MODE_PERIOD) {
        syslog(LOG_INFO, "Ch%d read AIC_GPAI_MODE_PERIOD! %d\n",
               ch, chan->avg_data);
        memcpy(val, chan->fifo_data,
               chan->fifo_valid_cnt * sizeof(chan->fifo_data[0]));
        return 0;
    }


    /* Single mode: trigger a conversion */
    gpai_single_mode(ch);

#ifdef CONFIG_AIC_GPAI_DRV_POLL
    /*
     * Interrupt-driven path (note: Kconfig name is historical).
     *
     * ISR hal_gpai_irq_read_fifo() populates chan->avg_data and
     * gives chan->complete; the ISR callback (aic_gpai_isr_callback)
     * delivers to the upper half and sets data_delivered=true.
     *
     * After we return the driver skips the dupe au_receive().
     */
    syslog(LOG_INFO, "Ch%d read interrupt-driven, timeout %d\n",
           ch, timeout);
    ret = aicos_sem_take(chan->complete, timeout);
    if (ret < 0) {
        syslog(LOG_ERR, "[%s:%d] Ch%d read timeout!\n",
               __func__, __LINE__, ch);
        aich_gpai_ch_enable(ch, 0);
        return -ETIMEDOUT;
    }
#else
    /*
     * Pure polling path: poll the hardware interrupt flags directly.
     * No ISR involvement — hal_gpai_read_poll() reads the FIFO and
     * returns chan->avg_data.  data_delivered stays false and the
     * caller's adc_read_work() delivers via au_receive().
     */
    syslog(LOG_INFO, "Ch%d read polling, timeout %d\n", ch, timeout);
    ret = hal_gpai_read_poll(ch, val, timeout);
    if (ret < 0) {
        syslog(LOG_ERR, "[%s:%d] Ch%d read timeout!\n",
               __func__, __LINE__, ch);
        return ret;
    }
#endif

    syslog(LOG_INFO, "Ch%d read success! %d\n", ch, *val);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Public HAL: channel lookup                                         */
/* ------------------------------------------------------------------ */

struct aic_gpai_ch *hal_gpai_ch_is_valid(u32 ch)
{
    u32 i;

    if (ch >= AIC_GPAI_CH_NUM) {
        pr_err("Invalid channel %d\n", ch);
        return NULL;
    }

    for (i = 0; i < aic_gpai_ch_num; i++) {
        if (aic_gpai_chs[i].id != ch)
            continue;

        if (aic_gpai_chs[i].available)
            return &aic_gpai_chs[i];
        else
            break;
    }
    pr_warn("[%s:%d] Ch%d is unavailable!\n", __func__, __LINE__, ch);
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Interrupt Service Routine                                          */
/*  Following the GPADC pattern: read + clear IRQ status at entry,    */
/*  then iterate pending channels.                                     */
/* ------------------------------------------------------------------ */

int aich_gpai_isr(int irq, FAR void *context, FAR void *arg)
{
    u32 ch_flag = 0, ch_int = 0;
    int i;
    struct aic_gpai_ch *chan = NULL;

    ch_flag = gpai_readl(GPAI_INTR);

    for (i = 0; i < AIC_GPAI_CH_NUM; i++) {
        if (!(ch_flag & GPAI_INTR_CH_INT_FLAG(i)))
            continue;

        chan = hal_gpai_ch_is_valid(i);
        if (!chan)
            return -1;

        ch_int = gpai_readl(GPAI_CHnINT(i));
        if (ch_int & GPAI_CHnINT_DRDY_FLG) {
            hal_gpai_irq_read_fifo(chan);
            chan->irq_count++;
            if (chan->irq_info.callback) {
              pr_debug("irq_info.callback %#x, detail %#x, ch: %d\n", ch_flag, ch_int, i);
                chan->irq_info.callback(chan->irq_info.callback_param);
            }
        }
        gpai_writel(ch_int, GPAI_CHnINT(i));
        pr_debug("IRQ flag %#x, detail %#x, ch: %d\n", ch_flag, ch_int, i);
        if (ch_int & GPAI_CHnINT_LLA_VALID_FLAG)
            pr_warn("LLA: ch%d %d!\n", i, chan->avg_data);
        if (ch_int & GPAI_CHnINT_LLA_RM_FLAG)
            pr_warn("LLA removed: ch%d %d\n", i,
                 chan->avg_data);
        if (ch_int & GPAI_CHnINT_HLA_VALID_FLAG)
            pr_warn("HLA: ch%d %d!\n", i, chan->avg_data);
        if (ch_int & GPAI_CHnINT_HLA_RM_FLAG)
            pr_warn("HLA removed: ch%d %d\n", i,
                 chan->avg_data);
        if (ch_int & GPAI_CHnINT_FIFO_ERR_FLAG)
            gpai_fifo_flush(i);
    }
    // pr_debug("IRQ flag %#x, detail %#x, ch: %d\n", ch_flag, ch_int, i);

    return IRQ_HANDLED;

}

/* ------------------------------------------------------------------ */
/*  PM: register save / restore (GPADC pattern)                        */
/* ------------------------------------------------------------------ */

#ifdef CONFIG_COMPONENTS_PM

/*
 * Ordered list of offsets that must be saved/restored across PM
 * transitions.  Channel-specific registers are saved per-channel;
 * global registers appear once.
 */
static const u32 gpai_pm_regs_offsets[] = {
    GPAI_MCR,
    GPAI_INTR,
};

/* Max channels supported for PM — covers up to AIC_GPAI_CH_NUM */
#define GPAI_PM_CH_REGS    8   /* CHnCR, INT, PSI, HLAT, LLAT, ACR, FCR, DATA */

static u32 gpai_pm_regs_ch[GPAI_PM_CH_REGS];

static void hal_gpai_save_regs(void)
{
    u32 i;
    struct aic_gpai_dev *dev = &s_gpai_dev;

    if (!dev->regs_backup)
        return;

    for (i = 0; i < ARRAY_SIZE(gpai_pm_regs_offsets); i++)
        dev->regs_backup[i] = gpai_readl(gpai_pm_regs_offsets[i]);

    /* Save per-channel registers for channel 0 (representative) */
    for (i = 0; i < GPAI_PM_CH_REGS; i++)
        gpai_pm_regs_ch[i] = gpai_readl(GPAI_CHnCR(0) + i * 4);
}

static void hal_gpai_restore_regs(void)
{
    u32 i;
    struct aic_gpai_dev *dev = &s_gpai_dev;

    if (!dev->regs_backup)
        return;

    for (i = 0; i < ARRAY_SIZE(gpai_pm_regs_offsets); i++)
        gpai_writel(dev->regs_backup[i], gpai_pm_regs_offsets[i]);

    /* Restore per-channel registers */
    for (i = 0; i < GPAI_PM_CH_REGS; i++)
        gpai_writel(gpai_pm_regs_ch[i], GPAI_CHnCR(0) + i * 4);
}

static int hal_gpai_resume(struct pm_device *dev, suspend_mode_t mode)
{
    /* Re-init HW first, then restore register state */
    hal_gpai_init();
    hal_gpai_restore_regs();
    GPADC_INFO("hal gpai resume\n");
    return 0;
}

static int hal_gpai_suspend(struct pm_device *dev, suspend_mode_t mode)
{
    hal_gpai_save_regs();
    hal_gpai_deinit();
    GPADC_INFO("hal gpai suspend\n");
    return 0;
}

struct pm_devops pm_gpai_ops = {
    .suspend = hal_gpai_suspend,
    .resume  = hal_gpai_resume,
};

struct pm_device pm_gpai = {
    .name = "artinchip_gpai",
    .ops  = &pm_gpai_ops,
};

#endif /* CONFIG_COMPONENTS_PM */

/* ------------------------------------------------------------------ */
/*  Public HAL: init / deinit (ref-counted, GPADC pattern)             */
/* ------------------------------------------------------------------ */

s32 hal_gpai_init(void)
{
    s32 ret = 0;

    ret = hal_clk_enable_deassertrst(CLK_GPAI);
    if (ret < 0) {
        pr_err("GPAI clock/reset enable failed!");
        return -1;
    }

#ifdef CONFIG_COMPONENTS_PM
    /* Allocate PM backup buffer on first init */
    if (!dev->regs_backup) {
        dev->regs_backup = (u32 *)aicos_zalloc(sizeof(u32) *
                            ARRAY_SIZE(gpai_pm_regs_offsets));
    }

    ret = pm_devops_register(&pm_gpai);
    if (ret < 0)
        pr_warn("GPAI PM register failed: %d\n", ret);
#endif
    ret = aicos_request_irq(GPAI_IRQn, aich_gpai_isr, "gpai", NULL);

    return 0;
}

s32 hal_gpai_deinit(void)
{
    s32 ret = 0;


#ifdef CONFIG_COMPONENTS_PM
    pm_devops_unregister(&pm_gpai);
#endif

    ret = hal_clk_disable_assertrst(CLK_GPAI);
    if (ret < 0) {
        pr_err("GPAI clock/reset disable failed!");
        return -1;
    }

    return ret;
}

/* ------------------------------------------------------------------ */
/*  Public HAL: misc                                                   */
/* ------------------------------------------------------------------ */

void hal_gpai_clk_get(struct aic_gpai_ch *chan)
{
    if (chan)
        chan->pclk_rate = hal_clk_get_freq(hal_clk_get_parent(CLK_GPAI));
}

void hal_gpai_set_ch_num(u32 num)
{
    aic_gpai_ch_num = num;
}

void hal_gpai_read_reg(int chan)
{
    uint32_t cr         = gpai_readl(GPAI_MCR);
    uint32_t intr       = gpai_readl(GPAI_INTR);
    uint32_t int_val    = gpai_readl(GPAI_CHnINT(chan));
    uint32_t cr_val     = gpai_readl(GPAI_CHnCR(chan));
    uint32_t psi_val    = gpai_readl(GPAI_CHnPSI(chan));
    uint32_t hlat_val   = gpai_readl(GPAI_CHnHLAT(chan));
    uint32_t llat_val   = gpai_readl(GPAI_CHnLLAT(chan));
    uint32_t acr_val    = gpai_readl(GPAI_CHnACR(chan));
    uint32_t fcr_val    = gpai_readl(GPAI_CHnFCR(chan));
    // uint32_t data_val = gpai_readl(GPAI_CHnDATA(chan));

    syslog(LOG_INFO,
           "[%s:%d] CR(%#x) %#lx, INTR(%#x) %#lx, "
           "CRn(%#x) %#lx, INTn(%#x) %#lx, PSIn(%#x) %#lx, "
           "HLATn(%#x) %#lx, LLATn(%#x) %#lx, ACRn(%#x) %#lx, "
           "FCR(%#x) %#lx\n",
           __func__, __LINE__,
           GPAI_MCR, cr, GPAI_INTR, intr,
           GPAI_CHnCR(chan), cr_val, GPAI_CHnINT(chan), int_val,
           GPAI_CHnPSI(chan), psi_val, GPAI_CHnHLAT(chan), hlat_val,
           GPAI_CHnLLAT(chan), llat_val, GPAI_CHnACR(chan), acr_val,
           GPAI_CHnFCR(chan), fcr_val);
}
