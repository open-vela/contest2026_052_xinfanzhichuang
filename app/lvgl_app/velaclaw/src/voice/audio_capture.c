/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * audio_capture.c - DMIC capture through the ArtInChip HAL.
 *
 * The board has a working DMIC HAL/DMA path, while the generic media recorder
 * path depends on CONFIG_MEDIA and does not provide pcm0c on this target.
 * Capture is therefore exposed as a small blocking reader over a two-period
 * cyclic DMA buffer.
 */

#include "voice/audio_capture.h"
#include "velaclaw_config.h"

#include <errno.h>
#include <limits.h>
#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/irq.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <aic_core.h>
#include <aic_drv_dma.h>
#include <drv_dma.h>
#include <hal_audio.h>

static const char *TAG = "audio_cap";

#ifndef CONFIG_AIC_DMIC_CMD_DMA_CHANNEL
#define CONFIG_AIC_DMIC_CMD_DMA_CHANNEL 1
#endif

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 32
#endif

#define CAP_BUFFER_PERIODS 2
#define CAP_PERIOD_MS 100
#define CAP_POLL_US 1000
#define CAP_WAIT_TIMEOUT_US (800 * 1000)
#define CAP_DMIC_VOLUME 160

struct audio_capture
{
  hal_audio_handle_t audio;
  struct aic_dma_chan_s rx_chan;
  unsigned char *dma_buf;
  unsigned char *read_buf;
  uint32_t period_bytes;
  uint32_t buffer_bytes;
  uint32_t next_read_slot;
  volatile uint32_t next_write_slot;
  volatile uint32_t ready[CAP_BUFFER_PERIODS];
  uint32_t consumed[CAP_BUFFER_PERIODS];
  size_t read_offset;
  bool read_pending;
  bool dma_inited;
  bool dma_registered;
  bool audio_inited;
  bool started;
};

static struct audio_capture *g_active_capture;

static uint32_t capture_frame_bytes(unsigned int channels,
                                    unsigned int bits_per_sample)
{
  return channels * (bits_per_sample / 8);
}

static uint32_t capture_period_bytes(unsigned int sample_rate,
                                     unsigned int channels,
                                     unsigned int bits_per_sample)
{
  uint64_t bytes;
  uint32_t frame_bytes;

  frame_bytes = capture_frame_bytes(channels, bits_per_sample);
  bytes = (uint64_t)sample_rate * frame_bytes * CAP_PERIOD_MS / 1000;
  if (bytes == 0 || bytes > UINT32_MAX) {
    return 0;
  }

  /* The DMA buffer must be cache-line aligned and stay on frame boundaries. */
  bytes = (bytes + CACHE_LINE_SIZE - 1) / CACHE_LINE_SIZE;
  bytes *= CACHE_LINE_SIZE;
  bytes = (bytes + frame_bytes - 1) / frame_bytes;
  bytes *= frame_bytes;
  return bytes > UINT32_MAX ? 0 : (uint32_t)bytes;
}

static void capture_period_callback(hal_audio_handle_t *haudio)
{
  struct audio_capture *cap = g_active_capture;
  uint32_t slot;

  if (cap == NULL || haudio != &cap->audio) {
    return;
  }

  slot = cap->next_write_slot % CAP_BUFFER_PERIODS;
  cap->ready[slot]++;
  cap->next_write_slot++;
}

static int capture_dma_prepare(struct audio_capture *cap,
                               unsigned int channels)
{
  hal_dma_handle_t *hdma = &cap->rx_chan.hal;
  hal_dma_data_width_e width;
  int ret;

  ret = drv_dma_init();
  if (ret < 0) {
    syslog(LOG_ERR, "[%s] drv_dma_init failed: %d\n", TAG, ret);
    return ret;
  }

  hal_dma_handle_init(hdma);
  hdma->regbase = DMA_BASE;
  hdma->init.channel_id = CONFIG_AIC_DMIC_CMD_DMA_CHANNEL;
  hdma->work_mode = DMA_WORK_MODE_CYCLIC;
  hdma->cyclic_period_len = cap->period_bytes;
  hdma->init.direction = DMA_DEVICE_TO_MEMORY;
  hdma->init.src_dev = HAL_DMA_ID_AUDIO;
  hdma->init.src_mode = DMA_MODE_HANDSHAKE;
  hdma->init.src_burst = DMA_XFER_BURST_1;
  hdma->init.src_addr_mode = DMA_ADDR_FIXED_MODE;
  width = channels == 1 ? DMA_DATA_WIDTH_2_BYTES : DMA_DATA_WIDTH_4_BYTES;
  hdma->init.src_data_width = width;
  hdma->init.snk_dev = HAL_DMA_ID_SRAM;
  hdma->init.snk_mode = DMA_MODE_WAIT;
  hdma->init.snk_burst = DMA_XFER_BURST_1;
  hdma->init.snk_addr_mode = DMA_ADDR_LINEAR_MODE;
  hdma->init.snk_data_width = width;
  hdma->init.flag = HAL_HANDLE_ALL_INIT_FLAG;

  if (hal_dma_init(hdma) != HAL_OK) {
    syslog(LOG_ERR, "[%s] hal_dma_init failed\n", TAG);
    return -EIO;
  }
  cap->dma_inited = true;

  ret = aic_dma_chan_register(&cap->rx_chan.chan);
  if (ret < 0) {
    syslog(LOG_ERR, "[%s] aic_dma_chan_register failed: %d\n", TAG, ret);
    return ret;
  }
  cap->dma_registered = true;
  return 0;
}

static int capture_audio_prepare(struct audio_capture *cap,
                                 unsigned int sample_rate,
                                 unsigned int channels)
{
  hal_audio_handle_init(&cap->audio);
  cap->audio.init.samplebits = AUDIO_SAMPLEBITS_16BIT;
  cap->audio.init.samplerate = (hal_audio_samplerate_e)sample_rate;
  cap->audio.init.channel = channels == 1
                              ? AUDIO_CHANNEL_MONO
                              : AUDIO_CHANNEL_STEREO;
  cap->audio.rxdma = &cap->rx_chan.hal;

  if (hal_audio_init(&cap->audio) != HAL_OK) {
    syslog(LOG_ERR, "[%s] hal_audio_init failed: state=%u err=%u\n",
           TAG, (unsigned int)hal_audio_get_state(&cap->audio),
           (unsigned int)hal_audio_get_error(&cap->audio));
    return -EIO;
  }
  cap->audio_inited = true;
  hal_audio_set_dmic_volume(&cap->audio, CAP_DMIC_VOLUME);

  if (hal_audio_register_callback(&cap->audio, AUDIO_CB_RX_DMA,
                                  capture_period_callback) != HAL_OK) {
    syslog(LOG_ERR, "[%s] RX callback registration failed\n", TAG);
    return -EIO;
  }
  return 0;
}

static void capture_stop_internal(struct audio_capture *cap)
{
  if (cap == NULL) {
    return;
  }

  if (g_active_capture == cap) {
    g_active_capture = NULL;
  }

  if (cap->started) {
    (void)hal_audio_stop_dmic_dma(&cap->audio);
    cap->started = false;
  }
}

static void capture_cleanup(struct audio_capture *cap)
{
  if (cap == NULL) {
    return;
  }

  capture_stop_internal(cap);

  if (cap->audio_inited) {
    (void)hal_audio_unregister_callback(&cap->audio, AUDIO_CB_RX_DMA, NULL);
    (void)hal_audio_deinit(&cap->audio);
    cap->audio_inited = false;
  }

  if (cap->dma_registered) {
    aic_dma_chan_unregister(&cap->rx_chan.chan);
    cap->dma_registered = false;
  }

  if (cap->dma_inited) {
    (void)hal_dma_deinit(&cap->rx_chan.hal);
    cap->dma_inited = false;
  }

  free(cap->read_buf);
  free(cap->dma_buf);
  cap->read_buf = NULL;
  cap->dma_buf = NULL;
}

audio_capture_t *audio_capture_open(const char *dev_path,
                                     unsigned int sample_rate,
                                     unsigned int channels,
                                     unsigned int bits_per_sample)
{
  struct audio_capture *cap;
  uint32_t period_bytes;
  int ret;

  (void)dev_path;

  if ((channels != 1 && channels != 2) ||
      bits_per_sample != 16 ||
      sample_rate == 0) {
    return NULL;
  }

  period_bytes = capture_period_bytes(sample_rate, channels,
                                      bits_per_sample);
  if (period_bytes == 0) {
    return NULL;
  }

  cap = calloc(1, sizeof(*cap));
  if (cap == NULL) {
    return NULL;
  }

  cap->period_bytes = period_bytes;
  cap->buffer_bytes = period_bytes * CAP_BUFFER_PERIODS;
  cap->dma_buf = memalign(CACHE_LINE_SIZE, cap->buffer_bytes);
  cap->read_buf = memalign(CACHE_LINE_SIZE, period_bytes);
  if (cap->dma_buf == NULL || cap->read_buf == NULL) {
    capture_cleanup(cap);
    free(cap);
    return NULL;
  }

  memset(cap->dma_buf, 0, cap->buffer_bytes);
  memset(cap->read_buf, 0, period_bytes);
  up_invalidate_dcache((uintptr_t)cap->dma_buf,
                       (uintptr_t)cap->dma_buf + cap->buffer_bytes);

  ret = capture_dma_prepare(cap, channels);
  if (ret < 0) {
    capture_cleanup(cap);
    free(cap);
    return NULL;
  }

  ret = capture_audio_prepare(cap, sample_rate, channels);
  if (ret < 0) {
    capture_cleanup(cap);
    free(cap);
    return NULL;
  }

  syslog(LOG_INFO, "[%s] opened DMIC (%uHz %uch %ubit period=%u dma_ch=%d)\n",
         TAG, sample_rate, channels, bits_per_sample,
         (unsigned int)period_bytes, CONFIG_AIC_DMIC_CMD_DMA_CHANNEL);
  return cap;
}

int audio_capture_start(audio_capture_t *cap)
{
  if (cap == NULL || !cap->audio_inited || cap->started) {
    return -EINVAL;
  }

  memset((void *)cap->ready, 0, sizeof(cap->ready));
  memset(cap->consumed, 0, sizeof(cap->consumed));
  cap->next_read_slot = 0;
  cap->next_write_slot = 0;
  cap->read_offset = 0;
  cap->read_pending = false;
  memset(cap->read_buf, 0, cap->period_bytes);
  g_active_capture = cap;

  if (hal_audio_receive_dmic_dma(&cap->audio, cap->dma_buf,
                                 cap->buffer_bytes,
                                 cap->period_bytes) != HAL_OK) {
    g_active_capture = NULL;
    syslog(LOG_ERR, "[%s] hal_audio_receive_dmic_dma failed\n", TAG);
    return -EIO;
  }

  cap->started = true;
  syslog(LOG_INFO, "[%s] capture started\n", TAG);
  return 0;
}

int audio_capture_stop(audio_capture_t *cap)
{
  if (cap == NULL) {
    return -EINVAL;
  }

  capture_stop_internal(cap);
  return 0;
}

int audio_capture_read(audio_capture_t *cap, void *buf, size_t len)
{
  size_t copied = 0;
  unsigned int waited_us = 0;

  if (cap == NULL || buf == NULL || len == 0) {
    return -EINVAL;
  }

  while (copied < len) {
    size_t available;

    if (cap->read_pending) {
      available = cap->period_bytes - cap->read_offset;
      if (available > len - copied) {
        available = len - copied;
      }
      memcpy((unsigned char *)buf + copied,
             cap->read_buf + cap->read_offset, available);
      cap->read_offset += available;
      copied += available;
      if (cap->read_offset == cap->period_bytes) {
        cap->read_offset = 0;
        cap->read_pending = false;
      }
      continue;
    }

    if (!cap->started) {
      return copied > 0 ? (int)copied : -EPIPE;
    }

    {
      uint32_t slot = cap->next_read_slot % CAP_BUFFER_PERIODS;
      uint32_t ready = cap->ready[slot];
      uint32_t consumed = cap->consumed[slot];

      if (ready != consumed) {
        if (ready - consumed > 1) {
          /* The reader fell behind; keep the newest complete period. */
          consumed = ready - 1;
          cap->consumed[slot] = consumed;
          syslog(LOG_WARNING, "[%s] capture overrun, dropping old period\n",
                 TAG);
        }

        up_invalidate_dcache(
            (uintptr_t)(cap->dma_buf + slot * cap->period_bytes),
            (uintptr_t)(cap->dma_buf + (slot + 1) * cap->period_bytes));
        memcpy(cap->read_buf,
               cap->dma_buf + slot * cap->period_bytes,
               cap->period_bytes);
        cap->consumed[slot] = consumed + 1;
        cap->next_read_slot++;
        cap->read_pending = true;
        waited_us = 0;
        continue;
      }
    }

    if (waited_us >= CAP_WAIT_TIMEOUT_US) {
      syslog(LOG_ERR, "[%s] timeout waiting for DMIC period\n", TAG);
      capture_stop_internal(cap);
      return copied > 0 ? (int)copied : -ETIMEDOUT;
    }

    usleep(CAP_POLL_US);
    waited_us += CAP_POLL_US;
  }

  return (int)copied;
}

void audio_capture_close(audio_capture_t *cap)
{
  if (cap == NULL) {
    return;
  }

  capture_cleanup(cap);
  free(cap);
}
