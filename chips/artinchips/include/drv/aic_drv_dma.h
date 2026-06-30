/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __AIC_DRV_DMA_H__
#define __AIC_DRV_DMA_H__

#include <nuttx/dma/dma.h>
#include <hal_dma.h>

struct aic_dma_chan_s {
  struct dma_chan_s chan;
  hal_dma_handle_t hal;
  dma_callback_t callback;
  FAR void *arg;
  size_t callback_len;
  unsigned int direction;
};

struct aic_dma_dev_s {
  struct dma_dev_s dev;
  bool initialized;
};

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
int aic_dma_chan_register(FAR struct dma_chan_s *chan);

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
int aic_dma_chan_unregister(FAR struct dma_chan_s *chan);

#endif /* __AIC_DRV_DMA_H__ */
