/****************************************************************************
 * arch/risc-v/src/artinchip/aic_sdio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
/****************************************************************************
 * Included Files
 ****************************************************************************/
#include "aic_sdio.h"
#include "aic_clk_id.h"
#include "aic_core.h"
#include "aic_hal.h"
#include "aic_soc.h"
#include "hal_sdmc.h"
#include <debug.h>
#include <errno.h>
#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/config.h>
#include <nuttx/irq.h>
#include <nuttx/sdio.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>
#include <syslog.h>
/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
#define AIC_SDIO_CMD_TIMEOUT_MS 2000
#define AIC_SDIO_BUSY_TIMEOUT_MS 2000
#define AIC_SDIO_DATA_TIMEOUT_MS 2000
#define AIC_SDIO_POLL_TIMEOUT_MS 1000

#define AIC_SDIO_MMC_FREQ 25000000
#define AIC_SDIO_SD1_FREQ 25000000
#define AIC_SDIO_SD4_FREQ 50000000

// #define AIC_SDIO_DEBUG

#ifdef AIC_SDIO_DEBUG
#define AIC_SDIO_LOG_ARGS(fmt, ...)                                            \
  syslog(LOG_ERR, "[aic_sdio] %s: " fmt "\n", __func__, ##__VA_ARGS__)
#else
#define AIC_SDIO_LOG_ARGS(fmt, ...)
#endif
/****************************************************************************
 * Private Types
 ****************************************************************************/

struct aic_sdio_pdata_s {
  uintptr_t base;
  int irq;
  int clk;
  int clk_freq;
  uint8_t id;
  uint8_t buswidth;
  uint8_t drv_phase;
  uint8_t smp_phase;
  uint8_t data_rate;
  bool is_sdio;
  bool is_hotplug;
};

struct aic_sdio_priv_s {
  struct sdio_dev_s dev;
  struct work_s cbwork;
  struct aic_sdmc_host host;
  const struct aic_sdio_pdata_s *pdata;
  worker_t callback;
  void *cbarg;
  sdio_eventset_t cbevents;
  sdio_eventset_t waitevents;
  sdio_eventset_t wkupevent;
  sdio_statset_t cdstatus;
  uint32_t sclk_rate;
  uint32_t clock;
  uint32_t wait_timeout_ms;
  int cmd_ret;
  uint32_t cmd_stat;
  bool cmd_has_data;
  uint16_t blocklen;
  uint16_t nblocks;
  uint8_t *buffer;
  size_t remaining_words;
  bool write_xfr;
  bool data_active;
  bool initialized;
  bool mutex_ready;
  bool irq_attached;
  bool ddr_mode;
};
/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

char *get_cmd_name(uint32_t cmd) {
  switch (cmd) {
  case MMCSD_CMD0:
    return "CMD0";
  case MMC_CMD1:
    return "CMD1";
  case MMCSD_CMD2:
    return "CMD2";
  case MMC_CMD3:
    return "CMD3";
  case SD_CMD3:
    return "CMD3(SD)";
  case MMCSD_CMD4:
    return "CMD4";
  case MMC_CMD5:
    return "CMD5";
  case SDIO_CMD5:
    return "CMD5(SDIO)";
  case MMCSD_CMD6:
    return "CMD6";
  case MMCSD_CMD7S:
    return "CMD7S";
  case MMCSD_CMD7D:
    return "CMD7D";
  case MMC_CMD8:
    return "CMD8";
  case SD_CMD8:
    return "CMD8(SD)";
  case MMCSD_CMD9:
    return "CMD9";
  case MMCSD_CMD10:
    return "CMD10";
  case MMC_CMD11:
    return "CMD11";
  case SD_CMD11:
    return "CMD11(SD)";
  case MMCSD_CMD12:
    return "CMD12";
  case MMCSD_CMD13:
    return "CMD13";
  case MMCSD_CMD14:
    return "CMD14";
  case MMCSD_CMD15:
    return "CMD15";
  case MMCSD_CMD16:
    return "CMD16";
  case MMCSD_CMD17:
    return "CMD17";
  case MMCSD_CMD18:
    return "CMD18";
  case SD_ACMD51:
    return "ACMD51";
  case MMC_CMD19:
    return "CMD19";
  case SD_CMD19:
    return "CMD19(SD)";
  case MMC_CMD20:
    return "CMD20";
  case SD_CMD20:
    return "CMD20(SD)";
  case MMC_CMD21:
    return "CMD21";
  case SD_CMD22:
    return "CMD22(SD)";
  case MMCSD_CMD23:
    return "CMD23";
  case MMCSD_CMD24:
    return "CMD24";
  case MMCSD_CMD25:
    return "CMD25";
  case MMCSD_CMD26:
    return "CMD26(SD)";
  case MMCSD_CMD27:
    return "CMD27";
  case MMCSD_CMD28:
    return "CMD28";
  case MMCSD_CMD29:
    return "CMD29";
  case MMCSD_CMD30:
    return "CMD30";
  case MMC_CMD31:
    return "CMD31";
  case SD_CMD32:
    return "CMD32(SD)";
  case SD_CMD33:
    return "CMD33(SD)";
  case MMC_CMD34:
    return "CMD34";
  case MMC_CMD35:
    return "CMD35";
  case MMC_CMD36:
    return "CMD36";
  case MMC_CMD37:
    return "CMD37";
  case MMCSD_CMD38:
    return "CMD38";
  case MMC_CMD39:
    return "CMD39";
  case MMC_CMD40:
    return "CMD40";
  case SD_CMD40:
    return "CMD40(SD)";
  case MMCSD_CMD42:
    return "CMD42";
  case MMC_CMD44:
    return "CMD44";
  case MMC_CMD45:
    return "CMD45";
  case MMC_CMD46:
    return "CMD46";
  case MMC_CMD47:
    return "CMD47";
  case MMC_CMD48:
    return "CMD48";
  case MMC_CMD49:
    return "CMD49";
  case SDIO_CMD52:
    return "CMD52(SDIO)";
  case MMC_CMD53:
    return "CMD53";
  case SDIO_CMD53RD:
    return "CMD53RD(SDIO)";
  case SDIO_CMD53WR:
    return "CMD53WR(SDIO)";
  case MMC_CMD54:
    return "CMD54";
  case SD_CMD55:
    return "CMD55(SD)";
  case MMCSD_CMD56RD:
    return "CMD56RD";
  case MMCSD_CMD56WR:
    return "CMD56WR";
  case SD_ACMD6:
    return "ACMD6";
  case SD_ACMD13:
    return "ACMD13";
  case SD_ACMD18:
    return "ACMD18";
  case SD_ACMD22:
    return "ACMD22";
  case SD_ACMD25:
    return "ACMD25";
  case SD_ACMD38:
    return "ACMD38";
  case SD_ACMD41:
    return "ACMD41";
  case SD_ACMD42:
    return "ACMD42";
  case SD_ACMD43:
    return "ACMD43";
  case SD_ACMD46:
    return "ACMD46";
  case SD_ACMD47:
    return "ACMD47";
  case SD_ACMD48:
    return "ACMD48";
  case SD_ACMD49:
    return "ACMD49";
  case SD_ACMD52ABRT:
    return "ACMD52ABRT";
  default:
    return "UNKNOWN_CMD";
  }
}

static int aic_sdio_hwinit(struct aic_sdio_priv_s *priv);
static int aic_sdio_setup_bus(struct aic_sdio_priv_s *priv, uint32_t freq);
static int aic_sdio_wait_cmd(struct aic_sdio_priv_s *priv, uint32_t *mask);
static int aic_sdio_wait_data(struct aic_sdio_priv_s *priv,
                              sdio_eventset_t *event);
static int aic_sdio_prepare_transfer(struct aic_sdio_priv_s *priv,
                                     uint8_t *buffer, size_t nbytes,
                                     bool write);
static void aic_sdio_finish_transfer(struct aic_sdio_priv_s *priv);
static void aic_sdio_media_callback(void *arg);
static int aic_sdio_interrupt(int irq, void *context, void *arg);

static void aic_sdio_reset(struct sdio_dev_s *dev);
static sdio_capset_t aic_sdio_capabilities(struct sdio_dev_s *dev);
static sdio_statset_t aic_sdio_status(struct sdio_dev_s *dev);
static void aic_sdio_widebus(struct sdio_dev_s *dev, bool enable);
static void aic_sdio_clock(struct sdio_dev_s *dev, enum sdio_clock_e rate);
static int aic_sdio_attach(struct sdio_dev_s *dev);
static int aic_sdio_sendcmd(struct sdio_dev_s *dev, uint32_t cmd, uint32_t arg);
#ifdef CONFIG_SDIO_BLOCKSETUP
static void aic_sdio_blocksetup(struct sdio_dev_s *dev, unsigned int blocklen,
                                unsigned int nblocks);
#endif
static int aic_sdio_recvsetup(struct sdio_dev_s *dev, uint8_t *buffer,
                              size_t nbytes);
static int aic_sdio_sendsetup(struct sdio_dev_s *dev, const uint8_t *buffer,
                              size_t nbytes);
static int aic_sdio_cancel(struct sdio_dev_s *dev);
static int aic_sdio_waitresponse(struct sdio_dev_s *dev, uint32_t cmd);
static int aic_sdio_recv_r1(struct sdio_dev_s *dev, uint32_t cmd, uint32_t *r1);
static int aic_sdio_recv_r2(struct sdio_dev_s *dev, uint32_t cmd,
                            uint32_t r2[4]);
static int aic_sdio_recv_r3(struct sdio_dev_s *dev, uint32_t cmd, uint32_t *r3);
static int aic_sdio_recv_r4(struct sdio_dev_s *dev, uint32_t cmd, uint32_t *r4);
static int aic_sdio_recv_r5(struct sdio_dev_s *dev, uint32_t cmd, uint32_t *r5);
static int aic_sdio_recv_r6(struct sdio_dev_s *dev, uint32_t cmd, uint32_t *r6);
static int aic_sdio_recv_r7(struct sdio_dev_s *dev, uint32_t cmd, uint32_t *r7);
static void aic_sdio_waitenable(struct sdio_dev_s *dev,
                                sdio_eventset_t eventset, uint32_t timeout);
static sdio_eventset_t aic_sdio_eventwait(struct sdio_dev_s *dev);
static void aic_sdio_callbackenable(struct sdio_dev_s *dev,
                                    sdio_eventset_t eventset);
#if defined(CONFIG_SCHED_WORKQUEUE) && defined(CONFIG_SCHED_HPWORK)
static int aic_sdio_registercallback(struct sdio_dev_s *dev, worker_t callback,
                                     void *arg);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct aic_sdio_pdata_s g_aic_sdio_pdata[AIC_SDIO_NSLOTS] = {
#ifdef CONFIG_AIC_USING_SDMC0
    [0] =
        {
            .base = SDMC0_BASE,
            .irq = SDMC0_IRQn + RISCV_IRQ_ASYNC,
            .clk = CLK_SDMC0,
            .clk_freq = CONFIG_AIC_SDMC0_CLK_FREQ,
            .id = 0,
#ifdef CONFIG_AIC_SDMC0_BUSWIDTH8
            .buswidth = SDMC_CTYPE_8BIT,
#elif defined(CONFIG_AIC_SDMC0_BUSWIDTH4)
            .buswidth = SDMC_CTYPE_4BIT,
#else
            .buswidth = SDMC_CTYPE_1BIT,
#endif
            .drv_phase = CONFIG_AIC_SDMC0_DRV_PHASE,
            .smp_phase = CONFIG_AIC_SDMC0_SMP_PHASE,
#ifdef CONFIG_AIC_SDMC0_DDR_MODE
            .data_rate = SDMC_DDR_MODE,
#else
            .data_rate = SDMC_SDR_MODE,
#endif
#ifdef CONFIG_AIC_SDMC0_IS_SDIO
            .is_sdio = true,
#endif
        },
#endif
#ifdef CONFIG_AIC_USING_SDMC1
    [1] = {
        .base = SDMC1_BASE,
        .irq = SDMC1_IRQn + RISCV_IRQ_ASYNC,
        .clk = CLK_SDMC1,
        .clk_freq = CONFIG_AIC_SDMC1_CLK_FREQ * 1000,
        .id = 1,
#ifdef CONFIG_AIC_SDMC1_BUSWIDTH8
        .buswidth = SDMC_CTYPE_8BIT,
#elif defined(CONFIG_AIC_SDMC1_BUSWIDTH4)
        .buswidth = SDMC_CTYPE_4BIT,
#else
        .buswidth = SDMC_CTYPE_1BIT,
#endif
        .drv_phase = CONFIG_AIC_SDMC1_DRV_PHASE,
        .smp_phase = CONFIG_AIC_SDMC1_SMP_PHASE,
#ifdef CONFIG_AIC_SDMC1_DDR_MODE
        .data_rate = SDMC_DDR_MODE,
#else
        .data_rate = SDMC_SDR_MODE,
#endif
#ifdef CONFIG_AIC_SDMC1_IS_SDIO
            .is_sdio = true,
#endif
#ifdef CONFIG_AIC_SDMC1_USING_HOTPLUG
            .is_hotplug = true,
#endif
        },
#endif
};

static struct aic_sdio_priv_s g_sdio_priv;
void aic_initial_sdio_func(struct sdio_dev_s *dev) {
  dev->reset = aic_sdio_reset;
  dev->capabilities = aic_sdio_capabilities;
  dev->status = aic_sdio_status;
  dev->widebus = aic_sdio_widebus;
  dev->clock = aic_sdio_clock;
  dev->attach = aic_sdio_attach;
  dev->sendcmd = aic_sdio_sendcmd;
#ifdef CONFIG_SDIO_BLOCKSETUP
  dev->blocksetup = aic_sdio_blocksetup;
#endif
  dev->recvsetup = aic_sdio_recvsetup;
  dev->sendsetup = aic_sdio_sendsetup;
  dev->cancel = aic_sdio_cancel;
  dev->waitresponse = aic_sdio_waitresponse;
  dev->recv_r1 = aic_sdio_recv_r1;
  dev->recv_r2 = aic_sdio_recv_r2;
  dev->recv_r3 = aic_sdio_recv_r3;
  dev->recv_r4 = aic_sdio_recv_r4;
  dev->recv_r5 = aic_sdio_recv_r5;
  dev->recv_r6 = aic_sdio_recv_r6;
  dev->recv_r7 = aic_sdio_recv_r7;
  dev->waitenable = aic_sdio_waitenable;
  dev->eventwait = aic_sdio_eventwait;
  dev->callbackenable = aic_sdio_callbackenable;
#if defined(CONFIG_SCHED_WORKQUEUE) && defined(CONFIG_SCHED_HPWORK)
  dev->registercallback = aic_sdio_registercallback;
#endif
#ifdef CONFIG_SDIO_DMA
  dev->dmarecvsetup = aic_sdio_recvsetup;
  dev->dmasendsetup = aic_sdio_sendsetup;
#endif
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline struct aic_sdio_priv_s *aic_priv(struct sdio_dev_s *dev) {
  DEBUGASSERT(dev != NULL);
  return (struct aic_sdio_priv_s *)dev;
}

static inline const struct aic_sdio_pdata_s *aic_slot_pdata(int slotno) {
  AIC_SDIO_LOG_ARGS("slotno=%d", slotno);
  if (slotno < 0 || slotno >= AIC_SDIO_NSLOTS) {
    return NULL;
  }

  if (g_aic_sdio_pdata[slotno].base == 0) {
    return NULL;
  }

  return &g_aic_sdio_pdata[slotno];
}

static uint32_t aic_sdio_get_best_div(uint32_t sclk, uint32_t target_freq) {
  AIC_SDIO_LOG_ARGS("sclk=%" PRIu32 " target_freq=%" PRIu32, sclk, target_freq);
  uint32_t down;
  uint32_t up;
  uint32_t f_down;
  uint32_t f_up;

  down = sclk / target_freq;
  up = DIV_ROUND_UP(sclk, target_freq);

  f_down = down == 0 ? sclk : sclk / down;
  f_up = up == 0 ? sclk : sclk / up;

  if ((f_down - target_freq) < (target_freq - f_up)) {
    return down;
  }

  return up;
}

static bool aic_sdio_resp_crc_needed(uint32_t cmd) {
  AIC_SDIO_LOG_ARGS("cmd=%s", get_cmd_name(cmd));
  uint32_t resp = cmd & MMCSD_RESPONSE_MASK;

  return resp == MMCSD_R1_RESPONSE || resp == MMCSD_R1B_RESPONSE ||
         resp == MMCSD_R2_RESPONSE || resp == MMCSD_R5_RESPONSE ||
         resp == MMCSD_R6_RESPONSE || resp == MMCSD_R7_RESPONSE;
}

static uint32_t aic_sdio_data_timeout(struct aic_sdio_priv_s *priv) {
  AIC_SDIO_LOG_ARGS("priv=%p", priv);
  uint32_t total;
  uint32_t lanes;
  uint32_t timeout;

  total = priv->blocklen * priv->nblocks;
  lanes = priv->pdata->buswidth == SDMC_CTYPE_8BIT
              ? 8
              : (priv->pdata->buswidth == SDMC_CTYPE_4BIT ? 4 : 1);
  timeout = total * 8;
  timeout /= lanes;
  timeout *= 10;

  if (priv->clock == 0) {
    return AIC_SDIO_POLL_TIMEOUT_MS;
  }

  timeout /= MAX(priv->clock / 1000, 1);
  if (priv->ddr_mode) {
    timeout /= 2;
  }

  return MAX(timeout, 1000u);
}

static uint32_t aic_sdio_build_cmdflags(uint32_t cmd) {
  AIC_SDIO_LOG_ARGS("cmd=%s", get_cmd_name(cmd));
  uint32_t flags = 0;
  uint32_t cmdidx = (cmd & MMCSD_CMDIDX_MASK) >> MMCSD_CMDIDX_SHIFT;
  uint32_t xfr = cmd & MMCSD_DATAXFR_MASK;

  if (xfr != MMCSD_NODATAXFR) {
    flags |= SDMC_CMD_DAT_EXP;
    if ((xfr & MMCSD_WRXFR) != 0) {
      flags |= SDMC_CMD_DAT_WR;
    }
  }

  if ((cmd & MMCSD_RESPONSE_MASK) != MMCSD_NO_RESPONSE) {
    flags |= SDMC_CMD_RESP_EXP;
    if ((cmd & MMCSD_RESPONSE_MASK) == MMCSD_R2_RESPONSE) {
      flags |= SDMC_CMD_RESP_LEN;
    }
  }

  if (aic_sdio_resp_crc_needed(cmd)) {
    flags |= SDMC_CMD_RESP_CRC;
  }

  if (cmdidx == MMCSD_CMDIDX0) {
    flags |= SDMC_CMD_INIT;
  }

  if (cmd & MMCSD_STOPXFR) {
    flags |= SDMC_CMD_STOP;
  } else {
    flags |= SDMC_CMD_PRV_DAT_WAIT;
  }
  AIC_SDIO_LOG_ARGS("flags=0x%08" PRIx32 " cmdidx=0x%08" PRIx32 " \
      cmd_start=0x%08" PRIx32 " use_hold_reg=0x%08" PRIx32,
                    flags, cmdidx, SDMC_CMD_START, SDMC_CMD_USE_HOLD_REG);
  return flags | cmdidx | SDMC_CMD_START | SDMC_CMD_USE_HOLD_REG;
}

static void aic_sdio_media_callback(void *arg) {
  AIC_SDIO_LOG_ARGS("arg=%p", arg);
  struct aic_sdio_priv_s *priv = (struct aic_sdio_priv_s *)arg;

  if (priv == NULL || priv->callback == NULL) {
    return;
  }

  if ((priv->cdstatus & SDIO_STATUS_PRESENT) != 0) {
    if ((priv->cbevents & SDIOMEDIA_INSERTED) == 0) {
      return;
    }
  } else if ((priv->cbevents & SDIOMEDIA_EJECTED) == 0) {
    return;
  }

  priv->cbevents = 0;

  if (up_interrupt_context()) {
    work_queue(HPWORK, &priv->cbwork, priv->callback, priv->cbarg, 0);
  } else {
    if (priv->callback)
      priv->callback(priv->cbarg);
  }
}

static int aic_sdio_clk_init(struct aic_sdio_priv_s *priv) {
  AIC_SDIO_LOG_ARGS("priv=%p", priv);
  int ret;

  hal_clk_disable_assertrst(priv->pdata->clk);
  hal_clk_disable(priv->pdata->clk);
  hal_clk_set_freq(priv->pdata->clk, priv->pdata->clk_freq);
  priv->sclk_rate = hal_clk_get_freq(priv->pdata->clk);
  mcinfo("Line:%d  sclk_rate=%lu buswidth=%d clk_freq=%lu", __LINE__, priv->sclk_rate, priv->pdata->buswidth, priv->pdata->clk_freq);
  ret = hal_clk_enable(priv->pdata->clk);
  if (ret < 0) {
    mcerr("ERROR: SDMC%d clk enable failed\n", priv->pdata->id);
    return ret;
  }

  ret = hal_clk_enable_deassertrst(priv->pdata->clk);
  if (ret < 0) {
    mcerr("ERROR: SDMC%d reset deassert failed\n", priv->pdata->id);
    return ret;
  }

  return OK;
}

static int aic_sdio_setup_bus(struct aic_sdio_priv_s *priv, uint32_t freq) {
  mcinfo("priv=%p freq=%d", priv, freq);
  uint32_t mux;
  uint32_t div;
  uint32_t actual;

  if (freq == 0) {
    return OK;
  }

  if ((freq == priv->clock) && !priv->ddr_mode) {
    return OK;
  }

  if (priv->sclk_rate == freq) {
    mux = 1;
    div = 0;
  } else {
    div = aic_sdio_get_best_div(priv->sclk_rate, freq);
    if (div <= 4) {
      mux = DIV_ROUND_UP(div, 2);
    } else {
      mux = (div % 8) ? 2 : 4;
    }

    div /= mux * 2;
    if (div > SDMC_CLKCTRL_DIV_MAX) {
      div = SDMC_CLKCTRL_DIV_MAX;
    }
  }

  if (priv->ddr_mode) {
    div = 0;
    mux = aic_sdio_get_best_div(priv->sclk_rate * 2, freq);
  }

  actual = div ? priv->sclk_rate / mux / div / 2 : priv->sclk_rate / mux;
  mcinfo(" SDMC%d, sclk %u KHz, clk expt %u KHz(act %u KHz), div %u-%u\n",
         priv->pdata->id, priv->sclk_rate / 1000, freq / 1000, actual / 1000,
         mux, div * 2);

  aic_sdmc_set_ext_clk_mux(&priv->host, mux);
  hal_sdmc_set_div(&priv->host, div);
  hal_sdmc_set_cmd(&priv->host,
                   SDMC_CMD_PRV_DAT_WAIT | SDMC_CMD_UPD_CLK | SDMC_CMD_START);
  if (hal_sdmc_wait_cmd_started(&priv->host) != 0) {
    return -ETIMEDOUT;
  }

  hal_sdmc_clk_enable(&priv->host);
  hal_sdmc_set_cmd(&priv->host,
                   SDMC_CMD_PRV_DAT_WAIT | SDMC_CMD_UPD_CLK | SDMC_CMD_START);
  if (hal_sdmc_wait_cmd_started(&priv->host) != 0) {
    return -ETIMEDOUT;
  }

  /* ID mode is sensitive to sampling edge on some boards/cards.
   * Use a conservative sampling phase at very low clock and restore
   * board-configured phases for transfer clocks.
   */

  if (freq <= SDMC_CLOCK_MIN) {
    hal_sdmc_set_phase(&priv->host, priv->pdata->drv_phase, 1);
  } else {
    hal_sdmc_set_phase(&priv->host, priv->pdata->drv_phase,
                       priv->pdata->smp_phase);
  }

  priv->clock = freq;
  return OK;
}

static int aic_sdio_hwinit(struct aic_sdio_priv_s *priv) {
  int ret;

  if (priv->initialized) {
    return OK;
  }
  ret = aic_sdio_clk_init(priv);
  if (ret < 0) {
    return ret;
  }

  priv->host.base = (volatile void *)priv->pdata->base;
  priv->host.is_sdio = priv->pdata->is_sdio ? 1 : 0;
  priv->host.fifoth_val = MSIZE(2) | RX_WMARK(7) | TX_WMARK(8);

  ret = hal_sdmc_reset(&priv->host, SDMC_HCTRL1_RESET_ALL);
  if (ret != 0) {
    return -EIO;
  }

  priv->ddr_mode = priv->pdata->data_rate == SDMC_DDR_MODE;
  priv->blocklen = 512;
  priv->nblocks = 1;
  priv->clock = 0;
  AIC_SDIO_LOG_ARGS("ddr_mode=%d ,", priv->ddr_mode ? 1 : 0);

  ret = aic_sdio_setup_bus(priv, SDMC_CLOCK_MIN);
  if (ret < 0) {
    return ret;
  }

  hal_sdmc_init(&priv->host);
  hal_sdmc_fifo_init(&priv->host, &priv->host.fifoth_val);
  hal_sdmc_set_phase(&priv->host, priv->pdata->drv_phase,
                     priv->pdata->smp_phase);
  hal_sdmc_set_ddrmode(&priv->host, priv->ddr_mode ? 1 : 0);
  /* During identification/init sequence the card is in 1-bit mode. */
  hal_sdmc_set_buswidth(&priv->host, priv->pdata->buswidth);
  hal_sdmc_clk_enable(&priv->host);

  priv->initialized = true;
  AIC_SDIO_LOG_ARGS("aic_sdio_hwinit END OK");
  return OK;
}

static int aic_sdio_wait_cmd(struct aic_sdio_priv_s *priv, uint32_t *mask) {
  AIC_SDIO_LOG_ARGS("priv=%p mask=%p", priv, mask);
  clock_t start = clock_systime_ticks();
  clock_t timeout = MSEC2TICK(AIC_SDIO_CMD_TIMEOUT_MS);

  for (;;) {
    uint32_t stat = hal_sdmc_int_stat(&priv->host);

    if (stat &
        (SDMC_INT_RTO | SDMC_INT_RESP_ERR | SDMC_INT_RCRC | SDMC_INT_HLE)) {
      *mask = stat;
      return -EIO;
    }

    if (stat & SDMC_INT_CMD_DONE) {
      *mask = stat;
      return OK;
    }

    /* Some controllers may not raise CMD_DONE for data commands, but
     * RXDR/TXDR/DAT_DONE indicates command/data path has progressed.
     */

    if (priv->cmd_has_data &&
        (stat & (SDMC_INT_RXDR | SDMC_INT_TXDR | SDMC_INT_DAT_DONE)) != 0) {
      *mask = stat;
      return OK;
    }

    if ((clock_systime_ticks() - start) > timeout) {
      *mask = stat;
      return -ETIMEDOUT;
    }
  }
}

static int aic_sdio_wait_data(struct aic_sdio_priv_s *priv,
                              sdio_eventset_t *event) {
  AIC_SDIO_LOG_ARGS("priv=%p event=%p", priv, event);
  clock_t start = clock_systime_ticks();
  clock_t timeout =
      MSEC2TICK(priv->wait_timeout_ms == 0 ? aic_sdio_data_timeout(priv)
                                           : priv->wait_timeout_ms);

  for (;;) {
    uint32_t stat = hal_sdmc_int_stat(&priv->host);
    size_t old_remaining = priv->remaining_words;

    if ((stat & SDMC_DATA_TOUT) != 0) {
      AIC_SDIO_LOG_ARGS("data timeout stat=0x%08" PRIx32 " rem_words=%zu", stat,
                        priv->remaining_words);
      *event = SDIOWAIT_TIMEOUT;
      return -ETIMEDOUT;
    }

    if ((stat & SDMC_DATA_ERR) != 0) {
      AIC_SDIO_LOG_ARGS("data error stat=0x%08" PRIx32 " rem_words=%zu", stat,
                        priv->remaining_words);
      *event = SDIOWAIT_ERROR;
      return -EIO;
    }

    if (priv->remaining_words > 0) {
      if (!priv->write_xfr && (stat & (SDMC_INT_RXDR | SDMC_INT_DAT_DONE))) {
        priv->remaining_words = hal_sdmc_data_rx(
            &priv->host, (u32 *)priv->buffer, priv->remaining_words);
      } else if (priv->write_xfr && (stat & SDMC_INT_TXDR)) {
        priv->remaining_words = hal_sdmc_data_tx(
            &priv->host, (u32 *)priv->buffer, priv->remaining_words);
      }

      priv->buffer +=
          (old_remaining - priv->remaining_words) * sizeof(uint32_t);
    }

    if (stat & SDMC_INT_DAT_DONE) {
      *event = SDIOWAIT_TRANSFERDONE;
      return OK;
    }

    if ((clock_systime_ticks() - start) > timeout) {
      *event = SDIOWAIT_TIMEOUT;
      return -ETIMEDOUT;
    }
  }
}

static int aic_sdio_prepare_transfer(struct aic_sdio_priv_s *priv,
                                     uint8_t *buffer, size_t nbytes,
                                     bool write) {
  AIC_SDIO_LOG_ARGS("priv=%p buffer=%p nbytes=%zu write=%d", priv, buffer,
                    nbytes, write ? 1 : 0);
  if ((nbytes == 0) || ((nbytes & 3) != 0)) {
    return -EINVAL;
  }

  if (priv->blocklen == 0) {
    priv->blocklen = 512;
  }

  if (priv->nblocks == 0) {
    priv->nblocks = 1;
  }

  hal_sdmc_set_blk(&priv->host, priv->blocklen, priv->nblocks);
  hal_sdmc_reset(&priv->host, SDMC_HCTRL1_FIFO_RESET);
  priv->buffer = buffer;
  priv->remaining_words = nbytes / sizeof(uint32_t);
  priv->write_xfr = write;
  priv->data_active = true;
  return OK;
}

static void aic_sdio_finish_transfer(struct aic_sdio_priv_s *priv) {
  AIC_SDIO_LOG_ARGS("priv=%p", priv);
  priv->buffer = NULL;
  priv->remaining_words = 0;
  priv->write_xfr = false;
  priv->data_active = false;
}

static void aic_sdio_reset(struct sdio_dev_s *dev) {
  AIC_SDIO_LOG_ARGS("dev=%p", dev);
  struct aic_sdio_priv_s *priv = aic_priv(dev);
  int ret;

  if (aic_sdio_hwinit(priv) < 0) {
    return;
  }

  ret = hal_sdmc_reset(&priv->host, SDMC_HCTRL1_RESET_ALL);
  if (ret != 0) {
    mcerr("ERROR: SDMC%d reset failed\n", priv->pdata->id);
    return;
  }

  priv->clock = 0;
  priv->blocklen = 512;
  priv->nblocks = 1;
  priv->ddr_mode = priv->pdata->data_rate == SDMC_DDR_MODE;

  ret = aic_sdio_setup_bus(priv, SDMC_CLOCK_MIN);
  if (ret < 0) {
    mcerr("ERROR: SDMC%d setup ID clock failed: %d\n", priv->pdata->id, ret);
    return;
  }

  hal_sdmc_init(&priv->host);
  hal_sdmc_fifo_init(&priv->host, &priv->host.fifoth_val);
  hal_sdmc_set_phase(&priv->host, priv->pdata->drv_phase,
                     priv->pdata->smp_phase);
  hal_sdmc_set_ddrmode(&priv->host, priv->ddr_mode ? 1 : 0);
  /* Reset to 1-bit; upper layer switches to wide bus after SCR/EXT_CSD. */
  hal_sdmc_set_buswidth(&priv->host, SDMC_CTYPE_1BIT);
  hal_sdmc_clk_enable(&priv->host);
  hal_sdmc_int_clr(&priv->host, SDMC_INT_ALL);
}

static sdio_capset_t aic_sdio_capabilities(struct sdio_dev_s *dev) {
  AIC_SDIO_LOG_ARGS("dev=%p buswidth=%d", dev, aic_priv(dev)->pdata->buswidth);
  struct aic_sdio_priv_s *priv = aic_priv(dev);
  sdio_capset_t caps = 0;

  if (priv->pdata->buswidth == SDMC_CTYPE_8BIT) {
    caps |= SDIO_CAPS_8BIT | SDIO_CAPS_4BIT;
  } else if (priv->pdata->buswidth == SDMC_CTYPE_4BIT) {
    caps |= SDIO_CAPS_4BIT_ONLY;
  } else {
    caps |= SDIO_CAPS_1BIT_ONLY;
  }

  caps |= SDIO_CAPS_MMC_HS_MODE;
  return caps;
}

static sdio_statset_t aic_sdio_status(struct sdio_dev_s *dev) {
  AIC_SDIO_LOG_ARGS("dev=%p status=0x%08" PRIx32, dev, aic_priv(dev)->cdstatus);
  return aic_priv(dev)->cdstatus;
}

static void aic_sdio_widebus(struct sdio_dev_s *dev, bool enable) {

  struct aic_sdio_priv_s *priv = aic_priv(dev);
  uint32_t width = SDMC_CTYPE_1BIT;

  if (enable) {
    if (priv->pdata->buswidth == SDMC_CTYPE_8BIT) {
      width = SDMC_CTYPE_8BIT;
    } else if (priv->pdata->buswidth == SDMC_CTYPE_4BIT) {
      width = SDMC_CTYPE_4BIT;
    }
  }
  AIC_SDIO_LOG_ARGS("dev=%p enable=%d width=%d", dev, enable ? 1 : 0, width);
  hal_sdmc_set_buswidth(&priv->host, width);
}

static void aic_sdio_clock(struct sdio_dev_s *dev, enum sdio_clock_e rate) {
  struct aic_sdio_priv_s *priv = aic_priv(dev);
  mcinfo("Line:%d dev=%p rate=%d sclk_rate=%lu", __LINE__, dev, (int)rate, priv->sclk_rate);
  uint32_t freq;

  switch (rate) {
  case CLOCK_SDIO_DISABLED:
    hal_sdmc_clk_disable(&priv->host);
    priv->clock = 0;
    return;

  case CLOCK_IDMODE:
    freq = SDMC_CLOCK_MIN;
    break;

  case CLOCK_MMC_TRANSFER:
    freq = MIN(priv->sclk_rate, (uint32_t)AIC_SDIO_MMC_FREQ);
    break;

  case CLOCK_SD_TRANSFER_1BIT:
    freq = MIN(priv->sclk_rate, (uint32_t)AIC_SDIO_SD1_FREQ);
    break;

  case CLOCK_SD_TRANSFER_4BIT:
  default:
    freq = MIN(priv->sclk_rate, (uint32_t)AIC_SDIO_SD4_FREQ);
    break;
  }

  aic_sdio_setup_bus(priv, freq);
}

static int aic_sdio_interrupt(int irq, void *context, void *arg) {
  struct aic_sdio_priv_s *priv = (struct aic_sdio_priv_s *)arg;
  uint32_t stat;

  UNUSED(irq);
  UNUSED(context);

  if (priv == NULL) {
    return OK;
  }

  stat = hal_sdmc_int_stat(&priv->host);
  if (stat == 0) {
    return OK;
  }

  if ((stat & SDMC_INT_FROM_SDIO) != 0) {
    hal_sdmc_int_clr(&priv->host, SDMC_INT_FROM_SDIO);
  }

  if ((stat & SDMC_INT_DAT_DONE) != 0) {
    hal_sdmc_int_clr(&priv->host, SDMC_INT_DAT_DONE);
  }

  return OK;
}

static int aic_sdio_attach(struct sdio_dev_s *dev) {
  struct aic_sdio_priv_s *priv = aic_priv(dev);
  int ret;

  AIC_SDIO_LOG_ARGS("dev=%p", dev);

  ret = aic_sdio_hwinit(priv);
  if (ret < 0) {
    return ret;
  }

  if (!priv->irq_attached) {
    ret = irq_attach(priv->pdata->irq, aic_sdio_interrupt, priv);
    if (ret < 0) {
      return ret;
    }

    hal_sdmc_int_clr(&priv->host, SDMC_INT_ALL);
    up_enable_irq(priv->pdata->irq);
    priv->irq_attached = true;
  }

  if (priv->pdata->is_sdio) {
    hal_sdmc_sdio_irq_enable(&priv->host, 1);
  }

  return OK;
}

static int aic_sdio_sendcmd(struct sdio_dev_s *dev, uint32_t cmd,
                            uint32_t arg) {
  AIC_SDIO_LOG_ARGS(" START dev=%p cmd=%s(0x%08" PRIx32 ") arg=0x%08" PRIx32,
                    dev, get_cmd_name(cmd), cmd, arg);
  struct aic_sdio_priv_s *priv = aic_priv(dev);
  clock_t start;

  if (aic_sdio_hwinit(priv) < 0) {
    return -ENODEV;
  }

  if (((cmd & MMCSD_CMDIDX_MASK) >> MMCSD_CMDIDX_SHIFT) != MMCSD_CMDIDX12) {
    start = clock_systime_ticks();
    while (hal_sdmc_is_busy(&priv->host) != 0) {
      if ((clock_systime_ticks() - start) >
          MSEC2TICK(AIC_SDIO_BUSY_TIMEOUT_MS)) {
        return -EBUSY;
      }
    }
  }

  hal_sdmc_int_clr(&priv->host, SDMC_INT_ALL);
  priv->cmd_ret = -EINPROGRESS;
  priv->cmd_stat = 0;
  priv->cmd_has_data = ((cmd & MMCSD_DATAXFR_MASK) != MMCSD_NODATAXFR);
  hal_sdmc_set_arg(&priv->host, arg);
  hal_sdmc_set_cmd(&priv->host, aic_sdio_build_cmdflags(cmd));
  AIC_SDIO_LOG_ARGS(" END dev=%p cmd=%s arg=0x%08" PRIx32, dev,
                    get_cmd_name(cmd), arg);
  return OK;
}

#ifdef CONFIG_SDIO_BLOCKSETUP
static void aic_sdio_blocksetup(struct sdio_dev_s *dev, unsigned int blocklen,
                                unsigned int nblocks) {
  AIC_SDIO_LOG_ARGS("dev=%p blocklen=%u nblocks=%u", dev, blocklen, nblocks);
  struct aic_sdio_priv_s *priv = aic_priv(dev);

  priv->blocklen = (uint16_t)blocklen;
  priv->nblocks = (uint16_t)nblocks;
}
#endif

static int aic_sdio_recvsetup(struct sdio_dev_s *dev, uint8_t *buffer,
                              size_t nbytes) {
  AIC_SDIO_LOG_ARGS("dev=%p buffer=%p nbytes=%zu", dev, buffer, nbytes);
  return aic_sdio_prepare_transfer(aic_priv(dev), buffer, nbytes, false);
}

static int aic_sdio_sendsetup(struct sdio_dev_s *dev, const uint8_t *buffer,
                              size_t nbytes) {
  AIC_SDIO_LOG_ARGS("dev=%p buffer=%p nbytes=%zu", dev, buffer, nbytes);
  return aic_sdio_prepare_transfer(aic_priv(dev), (uint8_t *)buffer, nbytes,
                                   true);
}

static int aic_sdio_cancel(struct sdio_dev_s *dev) {
  AIC_SDIO_LOG_ARGS("dev=%p", dev);
  struct aic_sdio_priv_s *priv = aic_priv(dev);

  hal_sdmc_int_clr(&priv->host, SDMC_INT_ALL);
  aic_sdio_finish_transfer(priv);
  priv->waitevents = 0;
  priv->wkupevent = 0;
  return OK;
}

static int aic_sdio_waitresponse(struct sdio_dev_s *dev, uint32_t cmd) {
  AIC_SDIO_LOG_ARGS("dev=%p cmd=%s", dev, get_cmd_name(cmd));
  struct aic_sdio_priv_s *priv = aic_priv(dev);
  uint32_t mask = 0;
  uint32_t clear = 0;
  int ret;

  UNUSED(cmd);

  ret = aic_sdio_wait_cmd(priv, &mask);
  priv->cmd_ret = ret;
  priv->cmd_stat = mask;
  clear = mask & (SDMC_INT_CMD_DONE | SDMC_INT_RTO | SDMC_INT_RESP_ERR |
                  SDMC_INT_RCRC | SDMC_INT_HLE);
  if (clear != 0) {
    hal_sdmc_int_clr(&priv->host, clear);
  }

  if (ret < 0) {
    if ((mask & SDMC_INT_RTO) != 0) {
      return -ETIMEDOUT;
    }

    return ret;
  }

  return OK;
}

static int aic_sdio_recv_r1(struct sdio_dev_s *dev, uint32_t cmd,
                            uint32_t *r1) {
  // AIC_SDIO_LOG_ARGS("dev=%p cmd=%s r1=%p", dev, get_cmd_name(cmd), r1);
  struct aic_sdio_priv_s *priv = aic_priv(dev);

  UNUSED(cmd);
  if (priv->cmd_ret < 0) {
    AIC_SDIO_LOG_ARGS("reject recv_r1 due to cmd_ret=%d cmd_stat=0x%08" PRIx32,
                      priv->cmd_ret, priv->cmd_stat);
    return priv->cmd_ret;
  }

  hal_sdmc_get_rsp(&priv->host, (u32 *)r1, 0);
  AIC_SDIO_LOG_ARGS("dev=%p cmd=%s r1=0x%08" PRIx32, dev, get_cmd_name(cmd),
                    *r1);

  return OK;
}

static int aic_sdio_recv_r2(struct sdio_dev_s *dev, uint32_t cmd,
                            uint32_t r2[4]) {
  // AIC_SDIO_LOG_ARGS("dev=%p cmd=%s r2=%p", dev, get_cmd_name(cmd), r2);
  struct aic_sdio_priv_s *priv = aic_priv(dev);

  UNUSED(cmd);
  hal_sdmc_get_rsp(&priv->host, (u32 *)r2, 1);
  AIC_SDIO_LOG_ARGS("dev=%p cmd=%s r2=0x%08" PRIx32, dev, get_cmd_name(cmd),
                    r2[0]);

  return OK;
}

static int aic_sdio_recv_r3(struct sdio_dev_s *dev, uint32_t cmd,
                            uint32_t *r3) {
  // AIC_SDIO_LOG_ARGS("dev=%p cmd=%s r3=%p", dev, get_cmd_name(cmd), r3);

  struct aic_sdio_priv_s *priv = aic_priv(dev);

  UNUSED(cmd);
  if (priv->cmd_ret < 0) {
    AIC_SDIO_LOG_ARGS("reject recv_r3 due to cmd_ret=%d cmd_stat=0x%08" PRIx32,
                      priv->cmd_ret, priv->cmd_stat);
    return priv->cmd_ret;
  }

  hal_sdmc_get_rsp(&priv->host, (u32 *)r3, 0);
  AIC_SDIO_LOG_ARGS("dev=%p cmd=%s r3=0x%08" PRIx32, dev, get_cmd_name(cmd),
                    *r3);
  return OK;
}

static int aic_sdio_recv_r4(struct sdio_dev_s *dev, uint32_t cmd,
                            uint32_t *r4) {
  AIC_SDIO_LOG_ARGS("dev=%p cmd=%s r4=%p", dev, get_cmd_name(cmd), r4);
  struct aic_sdio_priv_s *priv = aic_priv(dev);

  UNUSED(cmd);
  if (priv->cmd_ret < 0) {
    AIC_SDIO_LOG_ARGS("reject recv_r1 due to cmd_ret=%d cmd_stat=0x%08" PRIx32,
                      priv->cmd_ret, priv->cmd_stat);
    return priv->cmd_ret;
  }

  hal_sdmc_get_rsp(&priv->host, (u32 *)r4, 0);
  AIC_SDIO_LOG_ARGS("dev=%p cmd=%s r4=0x%08" PRIx32, dev, get_cmd_name(cmd),
                    *r4);
  return OK;
}

static int aic_sdio_recv_r5(struct sdio_dev_s *dev, uint32_t cmd,
                            uint32_t *r5) {
  AIC_SDIO_LOG_ARGS("dev=%p cmd=%s r5=%p", dev, get_cmd_name(cmd), r5);
  struct aic_sdio_priv_s *priv = aic_priv(dev);

  UNUSED(cmd);
  if (priv->cmd_ret < 0) {
    AIC_SDIO_LOG_ARGS("reject recv_r1 due to cmd_ret=%d cmd_stat=0x%08" PRIx32,
                      priv->cmd_ret, priv->cmd_stat);
    return priv->cmd_ret;
  }

  hal_sdmc_get_rsp(&priv->host, (u32 *)r5, 0);
  AIC_SDIO_LOG_ARGS("dev=%p cmd=%s r5=0x%08" PRIx32, dev, get_cmd_name(cmd),
                    *r5);
  return OK;
}

static int aic_sdio_recv_r6(struct sdio_dev_s *dev, uint32_t cmd,
                            uint32_t *r6) {
  AIC_SDIO_LOG_ARGS("dev=%p cmd=%s r6=%p", dev, get_cmd_name(cmd), r6);
  struct aic_sdio_priv_s *priv = aic_priv(dev);

  UNUSED(cmd);
  if (priv->cmd_ret < 0) {
    AIC_SDIO_LOG_ARGS("reject recv_r6 due to cmd_ret=%d cmd_stat=0x%08" PRIx32,
                      priv->cmd_ret, priv->cmd_stat);
    return priv->cmd_ret;
  }

  hal_sdmc_get_rsp(&priv->host, (u32 *)r6, 0);
  AIC_SDIO_LOG_ARGS("dev=%p cmd=%s r6=0x%08" PRIx32, dev, get_cmd_name(cmd),
                    *r6);
  return OK;
}

static int aic_sdio_recv_r7(struct sdio_dev_s *dev, uint32_t cmd,
                            uint32_t *r7) {
  AIC_SDIO_LOG_ARGS("dev=%p cmd=%s r7=%p", dev, get_cmd_name(cmd), r7);
  struct aic_sdio_priv_s *priv = aic_priv(dev);

  UNUSED(cmd);
  if (priv->cmd_ret < 0) {
    AIC_SDIO_LOG_ARGS("reject recv_r7 due to cmd_ret=%d cmd_stat=0x%08" PRIx32,
                      priv->cmd_ret, priv->cmd_stat);
    return priv->cmd_ret;
  }

  hal_sdmc_get_rsp(&priv->host, (u32 *)r7, 0);
  AIC_SDIO_LOG_ARGS("dev=%p cmd=%s r7=0x%08" PRIx32, dev, get_cmd_name(cmd),
                    *r7);
  return OK;
}

static void aic_sdio_waitenable(struct sdio_dev_s *dev,
                                sdio_eventset_t eventset, uint32_t timeout) {
  AIC_SDIO_LOG_ARGS("dev=%p eventset=0x%08" PRIx32 " timeout=%" PRIu32, dev,
                    (uint32_t)eventset, timeout);
  struct aic_sdio_priv_s *priv = aic_priv(dev);

  priv->waitevents = eventset;
  priv->wkupevent = 0;
  priv->wait_timeout_ms = timeout;
}

static sdio_eventset_t aic_sdio_eventwait(struct sdio_dev_s *dev) {
  AIC_SDIO_LOG_ARGS("dev=%p", dev);
  struct aic_sdio_priv_s *priv = aic_priv(dev);
  sdio_eventset_t event = 0;

  if (!priv->data_active) {
    return 0;
  }

  if (aic_sdio_wait_data(priv, &event) < 0 && event == 0) {
    event = SDIOWAIT_ERROR;
  }

  if ((event & SDIOWAIT_TRANSFERDONE) != 0) {
    hal_sdmc_int_clr(&priv->host, SDMC_INT_DAT_DONE);
  } else {
    hal_sdmc_int_clr(&priv->host, SDMC_INT_ALL);
  }

  aic_sdio_finish_transfer(priv);
  priv->wkupevent = event;
  priv->waitevents = 0;
  return event;
}

static void aic_sdio_callbackenable(struct sdio_dev_s *dev,
                                    sdio_eventset_t eventset) {
  AIC_SDIO_LOG_ARGS("dev=%p eventset=0x%08" PRIx32, dev, (uint32_t)eventset);
  struct aic_sdio_priv_s *priv = aic_priv(dev);

  priv->cbevents = eventset;
  aic_sdio_media_callback(priv);
}

#if defined(CONFIG_SCHED_WORKQUEUE) && defined(CONFIG_SCHED_HPWORK)
static int aic_sdio_registercallback(struct sdio_dev_s *dev, worker_t callback,
                                     void *arg) {
  AIC_SDIO_LOG_ARGS("dev=%p callback=%p arg=%p", dev, callback, arg);
  struct aic_sdio_priv_s *priv = aic_priv(dev);

  priv->callback = callback;
  priv->cbarg = arg;
  priv->cbevents = 0;
  return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

struct sdio_dev_s *sdio_initialize(int slotno) {
  AIC_SDIO_LOG_ARGS("slotno=%d", slotno);
  const struct aic_sdio_pdata_s *pdata;
  struct aic_sdio_priv_s *priv = &g_sdio_priv;
  if (priv == NULL) {
    return NULL;
  }

  pdata = aic_slot_pdata(slotno);
  if (pdata == NULL) {
    return NULL;
  }
  aic_initial_sdio_func(&priv->dev);
  priv->pdata = pdata;
  priv->cdstatus = SDIO_STATUS_PRESENT;
  priv->blocklen = 512;
  priv->nblocks = 1;

  if (!priv->mutex_ready) {
    nxmutex_init(&priv->dev.mutex);
    priv->mutex_ready = true;
  }

  if (aic_sdio_hwinit(priv) < 0) {
    return NULL;
  }

  return &priv->dev;
}

void sdio_mediachange(struct sdio_dev_s *dev, bool cardinslot) {
  AIC_SDIO_LOG_ARGS("dev=%p cardinslot=%d", dev, cardinslot ? 1 : 0);
  struct aic_sdio_priv_s *priv = aic_priv(dev);
  irqstate_t flags;
  sdio_statset_t oldstatus;

  flags = enter_critical_section();
  oldstatus = priv->cdstatus;

  if (cardinslot) {
    priv->cdstatus |= SDIO_STATUS_PRESENT;
  } else {
    priv->cdstatus &= ~SDIO_STATUS_PRESENT;
  }

  leave_critical_section(flags);

  if (oldstatus != priv->cdstatus) {
    aic_sdio_media_callback(priv);
  }
}

void sdio_wrprotect(struct sdio_dev_s *dev, bool wrprotect) {
  AIC_SDIO_LOG_ARGS("dev=%p wrprotect=%d", dev, wrprotect ? 1 : 0);
  struct aic_sdio_priv_s *priv = aic_priv(dev);
  irqstate_t flags;

  flags = enter_critical_section();
  if (wrprotect) {
    priv->cdstatus |= SDIO_STATUS_WRPROTECTED;
  } else {
    priv->cdstatus &= ~SDIO_STATUS_WRPROTECTED;
  }
  leave_critical_section(flags);
}