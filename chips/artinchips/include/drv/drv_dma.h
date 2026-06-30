/*
 * Copyright (c) 2022-2023, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ARTINCHIP_DMA_DRV_H_
#define __ARTINCHIP_DMA_DRV_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __NuttX__

#include <nuttx/dma/dma.h>

int drv_dma_init(void);
void drv_dma_deinit(void);
FAR struct dma_dev_s *aic_dma_initialize(void);
FAR struct dma_dev_s *drv_dma_get_dev(void);

#else

#include "hal_dma.h"

struct dma_chan {
  hal_dma_handle_t hal;
  struct dma_slave_config config;
  dma_async_callback callback;
  void *callback_param;
  u32 callback_len;
  u32 pending_src;
  u32 pending_dst;
  u32 pending_len;
  u32 pending_period_len;
  bool cyclic;
};

int drv_dma_init(void);
long drv_dma_ch_alloc(struct dma_chan *dma_ch, int8_t ch_id, int8_t ctrl_id);
void drv_dma_ch_free(struct dma_chan *dma_ch);
long drv_dma_ch_config(struct dma_chan *dma_ch,
                       struct dma_slave_config *config);
long drv_dma_ch_attach_callback(struct dma_chan *dma_ch, void *callback,
                                void *arg);
void drv_dma_ch_detach_callback(struct dma_chan *dma_ch);
void drv_dma_ch_start(struct dma_chan *dma_ch, void *srcaddr, void *dstaddr,
                      u32 length);
void drv_dma_ch_start_cyclic(struct dma_chan *dma_ch, void *srcaddr,
                             void *dstaddr, u32 length, u32 period_len);
void drv_dma_ch_stop(struct dma_chan *dma_ch);

static inline struct dma_chan *dma_request_channel(void) {
  struct dma_chan *chan;

  chan = aicos_malloc(0, sizeof(*chan));
  if (chan == NULL) {
    return NULL;
  }

  memset(chan, 0, sizeof(*chan));
  if (drv_dma_ch_alloc(chan, 0, 0) != 0) {
    aicos_free(0, chan);
    return NULL;
  }

  return chan;
}

static inline void dma_release_channel(struct dma_chan *chan) {
  if (chan == NULL) {
    return;
  }

  drv_dma_ch_free(chan);
  aicos_free(0, chan);
}

static inline enum dma_status dmaengine_tx_status(struct dma_chan *chan,
                                                  u32 *residue) {
  hal_dma_state_e state;

  if (chan == NULL || residue == NULL) {
    return DMA_ERROR;
  }

  if (hal_dma_get_residue(&chan->hal, residue) != HAL_OK) {
    return DMA_ERROR;
  }

  state = hal_dma_get_state(&chan->hal);
  return (*residue == 0 && state == DMA_STATE_READY) ? DMA_COMPLETE
                                                     : DMA_IN_PROGRESS;
}

static inline int dmaengine_prep_dma_memcpy(struct dma_chan *chan, u32 dest,
                                            u32 src, u32 len) {
  if (chan == NULL) {
    return -1;
  }

  chan->config.direction = AIC_DMA_MEM_TO_MEM;
  chan->pending_dst = dest;
  chan->pending_src = src;
  chan->pending_len = len;
  chan->cyclic = false;
  return 0;
}

static inline int dmaengine_prep_dma_memset(struct dma_chan *chan, u32 buf,
                                            u32 val, u32 len) {
  UNUSED(val);

  if (chan == NULL) {
    return -1;
  }

  chan->config.direction = AIC_DMA_MEM_TO_MEM;
  chan->pending_dst = buf;
  chan->pending_src = buf;
  chan->pending_len = len;
  chan->cyclic = false;
  return 0;
}

static inline int dmaengine_prep_dma_device(struct dma_chan *chan, u32 dest,
                                            u32 src, u32 len,
                                            enum dma_transfer_direction dir) {
  if (chan == NULL) {
    return -1;
  }

  chan->config.direction = dir;
  chan->pending_dst = dest;
  chan->pending_src = src;
  chan->pending_len = len;
  chan->cyclic = false;
  return 0;
}

static inline int dmaengine_prep_dma_cyclic(struct dma_chan *chan, u32 buf_addr,
                                            u32 buf_len, u32 period_len,
                                            enum dma_transfer_direction dir) {
  if (chan == NULL) {
    return -1;
  }

  chan->config.direction = dir;
  if (dir == AIC_DMA_MEM_TO_DEV) {
    chan->pending_src = buf_addr;
    chan->pending_dst = chan->config.dst_addr;
  } else {
    chan->pending_dst = buf_addr;
    chan->pending_src = chan->config.src_addr;
  }
  chan->pending_len = buf_len;
  chan->pending_period_len = period_len;
  chan->cyclic = true;
  return 0;
}

static inline int dmaengine_submit(struct dma_chan *chan,
                                   dma_async_callback callback,
                                   void *callback_param) {
  if (chan == NULL) {
    return -1;
  }

  chan->callback = callback;
  chan->callback_param = callback_param;
  return drv_dma_ch_attach_callback(chan, callback, callback_param);
}

static inline int dmaengine_slave_config(struct dma_chan *chan,
                                         struct dma_slave_config *config) {
  if (chan == NULL || config == NULL) {
    return -1;
  }

  memcpy(&chan->config, config, sizeof(*config));
  return drv_dma_ch_config(chan, config);
}

static inline void dma_async_issue_pending(struct dma_chan *chan) {
  if (chan == NULL) {
    return;
  }

  if (chan->cyclic) {
    drv_dma_ch_start_cyclic(chan, (void *)chan->pending_src,
                            (void *)chan->pending_dst, chan->pending_len,
                            chan->pending_period_len);
    return;
  }

  drv_dma_ch_start(chan, (void *)chan->pending_src, (void *)chan->pending_dst,
                   chan->pending_len);
}

static inline int dmaengine_terminate_async(struct dma_chan *chan) {
  drv_dma_ch_stop(chan);
  return 0;
}

static inline int dmaengine_pause(struct dma_chan *chan) {
  return chan != NULL && hal_dma_pause(&chan->hal) == HAL_OK ? 0 : -1;
}

static inline int dmaengine_resume(struct dma_chan *chan) {
  return chan != NULL && hal_dma_resume(&chan->hal) == HAL_OK ? 0 : -1;
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARTINCHIP_DMA_DRV_H_ */
