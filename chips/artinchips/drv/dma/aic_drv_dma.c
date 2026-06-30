/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/dma/dma.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>

#include "aic_common.h"
#include "aic_soc.h"
#include "aic_hal_clk.h"
#include "hal_dma.h"
#include "aic_drv_dma.h"
#include <debug.h>
#include <aic_dma_id.h>

#include <nuttx/irq.h>
#include <debug.h>

#if defined(AIC_USING_DMA0)
#define CLK_DMA CLK_DMA0
#elif defined(AIC_USING_DMA1)
#define CLK_DMA CLK_DMA1
#elif defined(AIC_USING_DMA2)
#define CLK_DMA CLK_DMA2
#endif

#if defined(AIC_DMA_DRV_V21)
#define AIC_DMA_IRQ (R_DMA_IRQn + RISCV_IRQ_ASYNC)
#else
#define AIC_DMA_IRQ (DMA_IRQn + RISCV_IRQ_ASYNC)
#endif

static FAR struct dma_chan_s *aic_dma_get_chan(FAR struct dma_dev_s *dev,
                                               unsigned int ident);
static void aic_dma_put_chan(FAR struct dma_dev_s *dev,
                             FAR struct dma_chan_s *chan);
static int aic_dma_config(FAR struct dma_chan_s *chan,
                          FAR const struct dma_config_s *cfg);
static int aic_dma_start(FAR struct dma_chan_s *chan, dma_callback_t callback,
                         FAR void *arg, uintptr_t dst, uintptr_t src,
                         size_t len);
static int aic_dma_start_cyclic(FAR struct dma_chan_s *chan,
                                dma_callback_t callback, FAR void *arg,
                                uintptr_t dst, uintptr_t src, size_t len,
                                size_t period_len);
static int aic_dma_stop(FAR struct dma_chan_s *chan);
static int aic_dma_pause(FAR struct dma_chan_s *chan);
static int aic_dma_resume(FAR struct dma_chan_s *chan);
static size_t aic_dma_residual(FAR struct dma_chan_s *chan);
static int aic_dma_interrupt(int irq, FAR void *context, FAR void *arg);

static const struct dma_ops_s g_aic_dma_ops = {
    .config = aic_dma_config,
    .start = aic_dma_start,
    .start_cyclic = aic_dma_start_cyclic,
    .stop = aic_dma_stop,
    .pause = aic_dma_pause,
    .resume = aic_dma_resume,
    .residual = aic_dma_residual,
};

#define MAX_DMA_CHANNELS 4

static struct aic_dma_chan_s *g_dma_channels[MAX_DMA_CHANNELS];
static int g_dma_channel_count = 0;

static struct aic_dma_dev_s g_aic_dma_dev = {
    .dev.get_chan = aic_dma_get_chan,
    .dev.put_chan = aic_dma_put_chan,
};

static int aic_dma_hal_status_to_errno(hal_status_e status) {
  switch (status) {
  case HAL_OK:
    return OK;

  case HAL_BUSY:
    return -EBUSY;

  case HAL_TIMEOUT:
    return -ETIMEDOUT;

  case HAL_ERROR:
  default:
    return -EIO;
  }
}

static hal_dma_data_width_e aic_dma_to_hal_width(unsigned int width) {
  switch (width) {
  case 4:
    return DMA_DATA_WIDTH_4_BYTES;

  case 2:
    return DMA_DATA_WIDTH_2_BYTES;

  default:
    return DMA_DATA_WIDTH_1_BYTE;
  }
}

static int aic_dma_apply_config(struct aic_dma_chan_s *priv,
                                FAR const struct dma_config_s *cfg) {
  hal_dma_handle_t *hdma;

  if (priv == NULL || cfg == NULL) {
    return -EINVAL;
  }

  if (cfg->direction == DMA_DEV_TO_DEV) {
    syslog(LOG_ERR, "DMA config invalid dir=%u\n", cfg->direction);
    return -ENOSYS;
  }

  hdma = &priv->hal;
  hdma->init.src_data_width = aic_dma_to_hal_width(cfg->src_width);
  hdma->init.snk_data_width = aic_dma_to_hal_width(cfg->dst_width);
  hdma->init.src_burst = DMA_XFER_BURST_1;
  hdma->init.snk_burst = DMA_XFER_BURST_1;
  hdma->init.src_addr_mode = DMA_ADDR_LINEAR_MODE;
  hdma->init.snk_addr_mode = DMA_ADDR_LINEAR_MODE;
  hdma->init.src_mode = DMA_MODE_WAIT;
  hdma->init.snk_mode = DMA_MODE_WAIT;

  switch (cfg->direction) {
  case DMA_MEM_TO_MEM:
    hdma->init.direction = DMA_MEMORY_TO_MEMORY;
    hdma->init.src_dev = HAL_DMA_ID_SRAM;
    hdma->init.snk_dev = HAL_DMA_ID_SRAM;
    break;

  case DMA_MEM_TO_DEV:
    hdma->init.direction = DMA_MEMORY_TO_DEVICE;
    hdma->init.src_dev = HAL_DMA_ID_SRAM;
    hdma->init.src_mode = DMA_MODE_WAIT;
    hdma->init.snk_dev = (hal_dma_id_e)cfg->dst_drq;
    hdma->init.snk_mode = DMA_MODE_HANDSHAKE;
    hdma->init.snk_addr_mode = DMA_ADDR_FIXED_MODE;
    break;

  case DMA_DEV_TO_MEM:
    hdma->init.direction = DMA_DEVICE_TO_MEMORY;
    hdma->init.src_dev = (hal_dma_id_e)cfg->src_drq;
    hdma->init.src_mode = DMA_MODE_HANDSHAKE;
    hdma->init.src_addr_mode = DMA_ADDR_FIXED_MODE;
    hdma->init.snk_dev = HAL_DMA_ID_SRAM;
    hdma->init.snk_mode = DMA_MODE_WAIT;
    break;

  default:
    return -EINVAL;
  }

  priv->direction = cfg->direction;
  return OK;
}

static void aic_dma_callback_adapter(hal_dma_handle_t *hdma) {
  struct aic_dma_chan_s *priv;

  if (hdma == NULL) {
    return;
  }

  priv = hdma->parent;
  if (priv == NULL || priv->callback == NULL) {
    return;
  }

  priv->callback(&priv->chan, priv->arg, priv->callback_len);
}

static int aic_dma_interrupt(int irq, FAR void *context, FAR void *arg) {
  int i;

  UNUSED(context);
  UNUSED(arg);
  /* Process HAL DMA interrupt */
  int ret = IRQ_HANDLED;

  /* Call callbacks for each channel */
  for (i = 0; i < g_dma_channel_count; i++) {
    if (g_dma_channels[i] != NULL) {
      hal_dma_irq_handler(&g_dma_channels[i]->hal);
    }
  }

  return ret;
}

void drv_dma_deinit(void) {
  up_disable_irq(AIC_DMA_IRQ);
  irq_detach(AIC_DMA_IRQ);
  hal_clk_disable_assertrst(CLK_DMA);
  hal_clk_disable(CLK_DMA);
  g_aic_dma_dev.initialized = false;
}

int drv_dma_init(void) {
  s32 ret;

  if (g_aic_dma_dev.initialized) {
    return OK;
  }

  if (hal_clk_is_enabled(CLK_DMA)) {
    drv_dma_deinit();
  }

  ret = hal_clk_enable(CLK_DMA);
  if (ret < 0) {
    syslog(LOG_ERR, "DMA BUS clk enable failed!\n");
    return ret;
  }

  ret = hal_clk_enable_deassertrst(CLK_DMA);
  if (ret < 0) {
    syslog(LOG_ERR, "DMA reset deassert failed!\n");
    return ret;
  }

  ret = irq_attach(AIC_DMA_IRQ, aic_dma_interrupt, NULL);
  if (ret < 0) {
    syslog(LOG_ERR, "DMA irq attach failed!\n");
    return ret;
  }

  up_enable_irq(AIC_DMA_IRQ);

  g_aic_dma_dev.initialized = true;
  syslog(LOG_ERR, "ArtInChip DMA loaded\n");
  return OK;
}

FAR struct dma_dev_s *aic_dma_initialize(void) {
  if (drv_dma_init() < 0) {
    return NULL;
  }

  return &g_aic_dma_dev.dev;
}

FAR struct dma_dev_s *drv_dma_get_dev(void) { return aic_dma_initialize(); }

static FAR struct dma_chan_s *aic_dma_get_chan(FAR struct dma_dev_s *dev,
                                               unsigned int ident) {
  struct aic_dma_chan_s *priv;

  UNUSED(dev);
  UNUSED(ident);

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL) {
    return NULL;
  }

  hal_dma_handle_init(&priv->hal);
  priv->hal.parent = priv;

  if (hal_dma_init(&priv->hal) != HAL_OK) {
    kmm_free(priv);
    return NULL;
  }

  priv->chan.ops = &g_aic_dma_ops;
  priv->direction = DMA_MEM_TO_MEM;

  return &priv->chan;
}

static void aic_dma_put_chan(FAR struct dma_dev_s *dev,
                             FAR struct dma_chan_s *chan) {
  struct aic_dma_chan_s *priv = (struct aic_dma_chan_s *)chan;

  UNUSED(dev);

  if (priv == NULL) {
    return;
  }

  if (hal_dma_get_state(&priv->hal) != DMA_STATE_RESET) {
    hal_dma_abort(&priv->hal);
    hal_dma_deinit(&priv->hal);
  }

  kmm_free(priv);
}

static int aic_dma_config(FAR struct dma_chan_s *chan,
                          FAR const struct dma_config_s *cfg) {
  struct aic_dma_chan_s *priv = (struct aic_dma_chan_s *)chan;

  if (priv == NULL || cfg == NULL) {
    return -EINVAL;
  }

  return aic_dma_apply_config(priv, cfg);
}

static int aic_dma_start(FAR struct dma_chan_s *chan, dma_callback_t callback,
                         FAR void *arg, uintptr_t dst, uintptr_t src,
                         size_t len) {
  struct aic_dma_chan_s *priv = (struct aic_dma_chan_s *)chan;
  hal_status_e ret;

  if (priv == NULL) {
    return -EINVAL;
  }

  priv->callback = callback;
  priv->arg = arg;
  priv->callback_len = len;

  priv->hal.parent = priv;
  priv->hal.work_mode = DMA_WORK_MODE_NORMAL;
  hal_dma_registger_callback(&priv->hal, DMA_CB_XFER_HALF_TASK_COMPLETE_CB,
                             NULL);
  hal_dma_registger_callback(&priv->hal, DMA_CB_XFER_ALL_TASKS_COMPLETE_CB,
                             callback != NULL ? aic_dma_callback_adapter
                                              : NULL);

  ret = callback != NULL
            ? hal_dma_start_it(&priv->hal, (u32)src, (u32)dst, (u32)len)
            : hal_dma_start(&priv->hal, (u32)src, (u32)dst, (u32)len);
  if (ret != HAL_OK) {
    return -EIO;
  }

  return OK;
}

static int aic_dma_start_cyclic(FAR struct dma_chan_s *chan,
                                dma_callback_t callback, FAR void *arg,
                                uintptr_t dst, uintptr_t src, size_t len,
                                size_t period_len) {
  struct aic_dma_chan_s *priv = (struct aic_dma_chan_s *)chan;
  hal_status_e ret;

  if (priv == NULL || period_len == 0) {
    return -EINVAL;
  }

  if (priv->direction == DMA_MEM_TO_MEM || priv->direction == DMA_DEV_TO_DEV) {
    return -ENOSYS;
  }

  priv->callback = callback;
  priv->arg = arg;
  priv->callback_len = period_len;

  priv->hal.parent = priv;
  priv->hal.work_mode = DMA_WORK_MODE_CYCLIC;
  priv->hal.cyclic_period_len = (u32)period_len;
  hal_dma_registger_callback(&priv->hal, DMA_CB_XFER_ALL_TASKS_COMPLETE_CB,
                             NULL);
  hal_dma_registger_callback(&priv->hal, DMA_CB_XFER_HALF_TASK_COMPLETE_CB,
                             callback != NULL ? aic_dma_callback_adapter
                                              : NULL);

  ret = hal_dma_start_it(&priv->hal, (u32)src, (u32)dst, (u32)len);
  if (ret != HAL_OK) {
    return -EIO;
  }

  return OK;
}

static int aic_dma_stop(FAR struct dma_chan_s *chan) {
  struct aic_dma_chan_s *priv = (struct aic_dma_chan_s *)chan;

  if (priv == NULL) {
    return -EINVAL;
  }

  return aic_dma_hal_status_to_errno(hal_dma_abort(&priv->hal));
}

static int aic_dma_pause(FAR struct dma_chan_s *chan) {
  struct aic_dma_chan_s *priv = (struct aic_dma_chan_s *)chan;
  hal_status_e ret = HAL_OK;

  if (priv == NULL) {
    return -EINVAL;
  }

  // hal_dma_pause(&priv->hal);
  return aic_dma_hal_status_to_errno(ret);
}

static int aic_dma_resume(FAR struct dma_chan_s *chan) {
  struct aic_dma_chan_s *priv = (struct aic_dma_chan_s *)chan;
  hal_status_e ret = HAL_OK;
  if (priv == NULL) {
    return -EINVAL;
  }
  // hal_dma_resume(&priv->hal);
  return aic_dma_hal_status_to_errno(ret);
}

static size_t aic_dma_residual(FAR struct dma_chan_s *chan) {
  struct aic_dma_chan_s *priv = (struct aic_dma_chan_s *)chan;
  uint32_t residue = 0;

  if (priv == NULL) {
    return 0;
  }

  hal_dma_get_residue(&priv->hal, &residue);
  return residue;
}

/************************************************************************************
 * Name: aic_dma_chan_register
 *
 * Description:
 *   Register a DMA channel for external drivers
 *
 * Input Parameters:
 *   chan - DMA channel to register
 *
 * Returned Value:
 *   OK on success, negative error code on failure
 ************************************************************************************/
int aic_dma_chan_register(FAR struct dma_chan_s *chan) {
  struct aic_dma_chan_s *priv = (struct aic_dma_chan_s *)chan;
  int i;

  if (priv == NULL) {
    return -EINVAL;
  }

  /* Find an empty slot in the channel array */
  for (i = 0; i < MAX_DMA_CHANNELS; i++) {
    if (g_dma_channels[i] == NULL) {
      g_dma_channels[i] = priv;
      g_dma_channel_count++;
      return OK;
    }
  }

  return -ENOMEM;
}

/************************************************************************************
 * Name: aic_dma_chan_unregister
 *
 * Description:
 *   Unregister a DMA channel
 *
 * Input Parameters:
 *   chan - DMA channel to unregister
 *
 * Returned Value:
 *   OK on success, negative error code on failure
 ************************************************************************************/
int aic_dma_chan_unregister(FAR struct dma_chan_s *chan) {
  struct aic_dma_chan_s *priv = (struct aic_dma_chan_s *)chan;
  int i;

  if (priv == NULL) {
    return -EINVAL;
  }

  /* Find and remove the channel from the array */
  for (i = 0; i < MAX_DMA_CHANNELS; i++) {
    if (g_dma_channels[i] == priv) {
      g_dma_channels[i] = NULL;
      g_dma_channel_count--;
      return OK;
    }
  }

  return -ENOENT;
}