/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ARTINCHIP_AUDIO_DMA_DRV_H__
#define __ARTINCHIP_AUDIO_DMA_DRV_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <nuttx/audio/audio.h>

FAR struct audio_lowerhalf_s *aic_audio_dma_initialize(void);
int aic_audio_dma_register(const char *name);

#ifdef __cplusplus
}
#endif

#endif