/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/config.h>

#include <debug.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/arch.h>
#include <nuttx/audio/audio.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/queue.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>
#include <aic_drv_dma.h>

#include "aic_common.h"
#include "hal_audio.h"
#include <nuttx/mutex.h>

#ifndef CONFIG_AIC_AUDIO_PERIOD_COUNT
#define CONFIG_AIC_AUDIO_PERIOD_COUNT 2
#endif

#ifdef CONFIG_AIC_AUDIO_SPK_0_1
#define AIC_AUDIO_PHY_CHANNELS 2
#else
#define AIC_AUDIO_PHY_CHANNELS 1
#endif

#define AIC_AUDIO_FIFO_CHANNELS 2
#define AIC_AUDIO_TEST_MODE_NONE 0
#define AIC_AUDIO_TEST_MODE_FIFO_TONE 1
#define AIC_AUDIO_TEST_MODE_HW_TONE 2
#define AIC_AUDIO_TEST_MODE_HW_SIGNAL_SCAN 3

#ifndef AIC_AUDIO_TEST_MODE
#define AIC_AUDIO_TEST_MODE AIC_AUDIO_TEST_MODE_NONE
#endif

#define AIC_AUDIO_HW_DEBUG_SIGNAL_COUNT 16
#define AIC_AUDIO_HW_DEBUG_SIGNAL_HOLD_US 1500000
#define AIC_AUDIO_HW_DEBUG_SIGNAL_GAP_US 300000
#define AIC_AUDIO_TEST_TONE_FREQ 1000
#define AIC_AUDIO_TEST_TONE_SAMPLES 48
#define AIC_AUDIO_RT_SDM_TUNING 0x01101100u
#define AIC_AUDIO_RT_PWM_TUNING 0x00130700u

#define AIC_AUDIO_DEFAULT_VOLUME 95
#define AIC_AUDIO_PERIOD_BYTES CONFIG_AUDIO_BUFFER_NUMBYTES

#ifndef CONFIG_AIC_AUDIO_USE_ISR
#define CONFIG_AIC_AUDIO_USE_ISR 0
#endif

#ifndef CONFIG_AIC_AUDIO_USE_CPU
#define CONFIG_AIC_AUDIO_USE_CPU 0
#endif

#ifndef CONFIG_AIC_AUDIO_DMA_WORK_MODE_CYCLIC
#define CONFIG_AIC_AUDIO_DMA_WORK_MODE_CYCLIC 1
#endif

struct aic_audio_s {
  struct audio_lowerhalf_s dev;
  hal_audio_handle_t audio;
  struct aic_dma_chan_s txdma_chan;
  uint8_t volume;
  uint8_t channels;
  uint8_t samplebits;
  uint32_t samplerate;
  uint32_t tone_phase;
  uint32_t tone_step;
  uint8_t *alloc_addr;
  uint8_t alloc_index;
  apb_samp_t buffer_size;
  apb_samp_t buffer_num;
  bool started;
  bool xrun;
  unsigned int gpio_pa;
  FAR struct ap_buffer_s *active;
  struct dq_queue_s pendq;
};

static bool aic_audio_use_cpu_tx(void) {
#ifndef CONFIG_AIC_AUDIO_USE_DMA
  return true;
#endif
  return false;
}

static int aic_audio_hal_status_to_errno(hal_status_e status) {
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

static hal_audio_output_chan_e aic_audio_output_channel(void) {
#ifdef CONFIG_AIC_AUDIO_SPK_0_1
#ifdef CONFIG_AIC_AUDIO_SPK0_OUTPUT_DIFFERENTIAL
  return AUDIO_OUTPUT_CHAN_PWM0_DIFF;
#elif defined(CONFIG_AIC_AUDIO_SPK1_OUTPUT_DIFFERENTIAL)
  return AUDIO_OUTPUT_CHAN_PWM1_DIFF;
#else
  return AUDIO_OUTPUT_CHAN_PWM0_PWM1;
#endif
#elif defined(CONFIG_AIC_AUDIO_SPK_1)
  return AUDIO_OUTPUT_CHAN_PWM1;
#else
  return AUDIO_OUTPUT_CHAN_PWM0;
#endif
}

static int aic_audio_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                             FAR struct audio_caps_s *caps);
static int aic_audio_configure(FAR struct audio_lowerhalf_s *dev,
                               FAR const struct audio_caps_s *caps);
static int aic_audio_shutdown(FAR struct audio_lowerhalf_s *dev);
static int aic_audio_start(FAR struct audio_lowerhalf_s *dev);
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
static int aic_audio_stop(FAR struct audio_lowerhalf_s *dev);
#endif
#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
static int aic_audio_pause(FAR struct audio_lowerhalf_s *dev);
static int aic_audio_resume(FAR struct audio_lowerhalf_s *dev);
#endif
static int aic_audio_allocbuffer(FAR struct audio_lowerhalf_s *dev,
                                 FAR struct audio_buf_desc_s *bufdesc);
static int aic_audio_freebuffer(FAR struct audio_lowerhalf_s *dev,
                                FAR struct audio_buf_desc_s *bufdesc);
static int aic_audio_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                   FAR struct ap_buffer_s *apb);
static int aic_audio_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                           unsigned long arg);
static int aic_audio_reserve(FAR struct audio_lowerhalf_s *dev);
static int aic_audio_release(FAR struct audio_lowerhalf_s *dev);

static const struct audio_ops_s g_aic_audio_ops = {
    .getcaps = aic_audio_getcaps,
    .configure = aic_audio_configure,
    .shutdown = aic_audio_shutdown,
    .start = aic_audio_start,
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
    .stop = aic_audio_stop,
#endif
#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
    .pause = aic_audio_pause,
    .resume = aic_audio_resume,
#endif
    .allocbuffer = aic_audio_allocbuffer,
    .freebuffer = aic_audio_freebuffer,
    .enqueuebuffer = aic_audio_enqueuebuffer,
    .ioctl = aic_audio_ioctl,
    .reserve = aic_audio_reserve,
    .release = aic_audio_release,
};

static struct aic_audio_s g_aic_audio;

static inline void aic_audio_enable_pa(struct aic_audio_s *priv) {
  unsigned int group;
  unsigned int pin;

  group = GPIO_GROUP(priv->gpio_pa);
  pin = GPIO_GROUP_PIN(priv->gpio_pa);

#ifdef CONFIG_AIC_AUDIO_EN_PIN_HIGH
  hal_gpio_set_output(group, pin);
#else
  hal_gpio_clr_output(group, pin);
#endif
}

static inline void aic_audio_disable_pa(struct aic_audio_s *priv) {
  unsigned int group;
  unsigned int pin;

  group = GPIO_GROUP(priv->gpio_pa);
  pin = GPIO_GROUP_PIN(priv->gpio_pa);

#ifdef CONFIG_AIC_AUDIO_EN_PIN_HIGH
  hal_gpio_clr_output(group, pin);
#else
  hal_gpio_set_output(group, pin);
#endif
}

static void aic_audio_complete_apb(struct aic_audio_s *priv,
                                   FAR struct ap_buffer_s *apb) {
  if (!apb) {
    return;
  }
  // csi_dcache_clean_range((uint32_t)apb->samp, apb->nbytes);
  apb->curbyte = 0;
  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK);
}

static void aic_audio_dma_init_callback(hal_dma_handle_t *hdma) {}

static void aic_audio_dma_deinit_callback(hal_dma_handle_t *hdma) {}

static void aic_audio_dump_regs(struct aic_audio_s *priv, FAR const char *tag) {
  static const uint8_t reg_offsets[] = {
      0x00, 0x04, 0x1c, 0x20, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x38,
      0x3c, 0x60, 0x70, 0x74, 0x80, 0x84, 0x88, 0xa0, 0xa4,
  };
  unsigned int index;
  unsigned int pin = hal_gpio_name2pin("PE.12");
  unsigned int group = GPIO_GROUP(pin);
  unsigned int pin_idx = GPIO_GROUP_PIN(pin);
  unsigned int value = 0, func = 0, oe = 0, pull = 0;

  auderr("--- NuttX Audio Register Dump (%s, Base: 0x%08lx) ---\n", tag,
         priv->audio.regbase);

  hal_gpio_get_value(group, pin_idx, &value);
  hal_gpio_get_func(group, pin_idx, &func);
  oe = hal_gpio_get_pincfg(group, pin_idx, GPIO_CHECK_PIN_GEN_OE);
  pull = hal_gpio_get_pincfg(group, pin_idx, GPIO_CHECK_PIN_GEN_PULL);

  auderr("PE12 GPIO status: val=%u, func=%u, oe=%u, pull=%u\n", value, func, oe,
         pull);

  for (index = 0; index < sizeof(reg_offsets) / sizeof(reg_offsets[0]);
       index++) {
    uint32_t offset = reg_offsets[index];
    auderr("REG[0x%02x]: 0x%08x\n", offset,
           (unsigned int)readl(priv->audio.regbase + offset));
  }
}

static void aic_audio_dump_runtime_config(struct aic_audio_s *priv) {
  uint32_t residue = 0;

  auderr("audio dump dma state=%u residue=%u\n",
         (unsigned int)hal_dma_get_state(&priv->txdma_chan.hal),
         (unsigned int)residue);
}

static uint32_t aic_audio_frame_bytes(struct aic_audio_s *priv) {
  uint32_t channels = priv->channels == 0 ? 2 : priv->channels;
  uint32_t samplebits = priv->samplebits == 0 ? 16 : priv->samplebits;

  return (samplebits / 8) * channels;
}

static int aic_audio_start_transfer(struct aic_audio_s *priv,
                                    const uint8_t *buffer, uint32_t bytes) {
  hal_status_e status;

  if (aic_audio_use_cpu_tx()) {
    status = hal_audio_transmit(&priv->audio, buffer, bytes, UINT32_MAX);
  } else {
    if (priv->audio.state != AUDIO_STATE_READY) {
      return -EBUSY;
    }
    status = hal_audio_transmit_dma(&priv->audio, buffer, bytes);
  }

  return aic_audio_hal_status_to_errno(status);
}

static int aic_audio_pause_transfer(struct aic_audio_s *priv) {
  if (!aic_audio_use_cpu_tx()) {
    return aic_audio_hal_status_to_errno(hal_audio_dma_pause(&priv->audio));
  }

  return -ENOSYS;
}

static int aic_audio_resume_transfer(struct aic_audio_s *priv) {
  if (!aic_audio_use_cpu_tx()) {
    return aic_audio_hal_status_to_errno(hal_audio_dma_resume(&priv->audio));
  }

  return -ENOSYS;
}

static int aic_audio_start_next_buffer(struct aic_audio_s *priv) {
  irqstate_t flags;
  FAR struct ap_buffer_s *apb;
  uint32_t bytes;
  bool final;

  for (;;) {
    flags = enter_critical_section();
    if (priv->active != NULL) {
      leave_critical_section(flags);
      return OK;
    }

    apb = (FAR struct ap_buffer_s *)dq_remfirst(&priv->pendq);
    priv->active = apb;
    leave_critical_section(flags);

    if (apb == NULL) {
      priv->xrun = true;
      return -EAGAIN;
    }

    bytes = apb->nbytes - apb->curbyte;
    if (bytes == 0) {
      flags = enter_critical_section();
      priv->active = NULL;
      leave_critical_section(flags);
      if (apb->flags & AUDIO_APB_FINAL) {
        aic_audio_complete_apb(priv, apb);
        priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK);
        return OK;
      } else {
        aic_audio_complete_apb(priv, apb);
        continue;
      }
    }

    if (aic_audio_start_transfer(
            priv, (const uint8_t *)apb->samp + apb->curbyte, bytes) < 0) {
      flags = enter_critical_section();
      priv->active = NULL;
      dq_addfirst(&apb->dq_entry, &priv->pendq);
      leave_critical_section(flags);
      return -EIO;
    }

    if (!aic_audio_use_cpu_tx()) {
      return OK;
    }

    final = (apb->flags & AUDIO_APB_FINAL) != 0;
    flags = enter_critical_section();
    priv->active = NULL;
    leave_critical_section(flags);
    aic_audio_complete_apb(priv, apb);

    if (final) {
      priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK);
      return OK;
    }
  }
}

static void aic_audio_dma_callback(hal_audio_handle_t *haudio) {
  struct aic_audio_s *priv;
  FAR struct ap_buffer_s *apb;
  bool final;
  irqstate_t flags;

  priv = container_of(haudio, struct aic_audio_s, audio);

  if (!priv->started) {
    return;
  }

  flags = enter_critical_section();
  apb = priv->active;
  priv->active = NULL;
  leave_critical_section(flags);

  if (apb == NULL) {
    priv->xrun = true;
    auderr("audio tx xrun\n");
    return;
  }

  final = (apb->flags & AUDIO_APB_FINAL) != 0;

  aic_audio_complete_apb(priv, apb);

  if (final) {
    priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK);
    return;
  }

  if (aic_audio_start_next_buffer(priv) < 0) {
    priv->xrun = true;
  }
}

static int aic_audio_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                             FAR struct audio_caps_s *caps) {
  struct aic_audio_s *priv = (struct aic_audio_s *)dev;

  DEBUGASSERT(caps != NULL && caps->ac_len >= sizeof(struct audio_caps_s));

  caps->ac_format.hw = 0;
  caps->ac_controls.w = 0;

  switch (caps->ac_type) {
  case AUDIO_TYPE_QUERY:
    if (caps->ac_subtype == AUDIO_TYPE_QUERY) {
      caps->ac_controls.b[0] = AUDIO_TYPE_OUTPUT | AUDIO_TYPE_FEATURE;
      caps->ac_format.hw = 1 << (AUDIO_FMT_PCM - 1);
    } else if (caps->ac_subtype == AUDIO_FMT_PCM) {
      caps->ac_controls.b[0] = AUDIO_SUBFMT_PCM_S16_LE;
      caps->ac_controls.b[1] = AUDIO_SUBFMT_END;
    } else {
      caps->ac_controls.b[0] = AUDIO_SUBFMT_END;
    }
    break;

  case AUDIO_TYPE_OUTPUT:
    if (caps->ac_subtype == AUDIO_TYPE_QUERY) {
      caps->ac_channels = 0x12;
      caps->ac_controls.hw[0] = AUDIO_SAMP_RATE_8K | AUDIO_SAMP_RATE_11K |
                                AUDIO_SAMP_RATE_16K | AUDIO_SAMP_RATE_22K |
                                AUDIO_SAMP_RATE_32K | AUDIO_SAMP_RATE_44K |
                                AUDIO_SAMP_RATE_48K;
      caps->ac_controls.b[2] = priv->samplebits;
    }
    break;

  case AUDIO_TYPE_FEATURE:
    if (caps->ac_subtype == AUDIO_FU_UNDEF) {
      caps->ac_controls.b[0] = AUDIO_FU_VOLUME;
    }
    break;

  default:
    break;
  }

  return caps->ac_len;
}

static int aic_audio_configure(FAR struct audio_lowerhalf_s *dev,
                               FAR const struct audio_caps_s *caps) {
  struct aic_audio_s *priv = (struct aic_audio_s *)dev;
  uint32_t samplerate;
  uint8_t channels;
  uint8_t samplebits;

  if (caps->ac_type == AUDIO_TYPE_OUTPUT) {
    samplerate =
        caps->ac_controls.hw[0] | (((uint32_t)caps->ac_controls.b[3]) << 16);
    channels = (caps->ac_channels & 0x0f);
    samplebits = caps->ac_controls.b[2];

    if (samplerate == 0) {
      samplerate = 48000;
    }

    if (channels == 0) {
      channels = 2;
    }

    if (samplebits == 0) {
      samplebits = 16;
    }

    if (channels != 1 && channels != 2) {
      return -EINVAL;
    }

    if (samplebits != 16) {
      return -EINVAL;
    }

    priv->samplerate = samplerate;
    priv->channels = channels;
    priv->samplebits = samplebits;

    audwarn("configure output rate=%u req_channels=%u hw_channels=%u "
            "samplebits=%u\n",
            (unsigned int)samplerate, channels,
            (unsigned int)AIC_AUDIO_PHY_CHANNELS, samplebits);

    priv->audio.init.samplerate = (hal_audio_samplerate_e)samplerate;
    priv->audio.init.channel =
        channels == 1 ? AUDIO_CHANNEL_MONO : AUDIO_CHANNEL_STEREO;
    priv->audio.init.samplebits = AUDIO_SAMPLEBITS_16BIT;

    hal_audio_set_samplerate(&priv->audio);
    hal_audio_set_channel(&priv->audio);
    return OK;
  }

  if (caps->ac_type == AUDIO_TYPE_FEATURE &&
      caps->ac_format.hw == AUDIO_FU_VOLUME) {
    uint32_t value;

    value = caps->ac_controls.hw[0];
    if (value > 100) {
      value = 100;
    }

    // if (hal_audio_set_volume(&priv->audio, reg_volume) != HAL_OK) {
    //   return -EIO;
    // }
    priv->volume = value;
    audwarn("configure volume=%u\n", (unsigned int)value);
    return OK;
  }

  return -ENOTTY;
}

static int aic_audio_shutdown(FAR struct audio_lowerhalf_s *dev) {
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
  return aic_audio_stop(dev);
#else
  return OK;
#endif
}

static int aic_audio_start(FAR struct audio_lowerhalf_s *dev) {
  struct aic_audio_s *priv = (struct aic_audio_s *)dev;

  if (priv->started) {
    return OK;
  }

  priv->tone_phase = 0;
  priv->xrun = false;

  if (priv->alloc_addr == NULL) {
    auderr("audio start failed: no dma buffers allocated\n");
    return -ENOMEM;
  }

  if (dq_empty(&priv->pendq)) {
    auderr("audio start failed: no queued buffers\n");
    return -EAGAIN;
  }

  audwarn("audio start rate=%u channels=%u hw_channels=%u samplebits=%u "
          "period=%u fifo=%u\n",
          (unsigned int)priv->samplerate, priv->channels,
          (unsigned int)AIC_AUDIO_PHY_CHANNELS, priv->samplebits,
          (unsigned int)priv->buffer_size,
          (unsigned int)(priv->buffer_size * priv->buffer_num));

  priv->audio.init.samplerate = (hal_audio_samplerate_e)priv->samplerate;
  priv->audio.init.channel =
      priv->channels == 1 ? AUDIO_CHANNEL_MONO : AUDIO_CHANNEL_STEREO;
  priv->audio.init.samplebits = AUDIO_SAMPLEBITS_16BIT;
  priv->audio.init.output_chan = aic_audio_output_channel();
  priv->audio.txdma = aic_audio_use_cpu_tx() ? NULL : &priv->txdma_chan.hal;

  if (!aic_audio_use_cpu_tx()) {
    hal_dma_handle_t *hdma = priv->audio.txdma;
    hdma->parent = &priv->audio;
    hdma->regbase = DMA_BASE;
    hdma->init.channel_id = 0;
    hdma->work_mode = DMA_WORK_MODE_NORMAL;
    hdma->cyclic_period_len = 0;
    hdma->init.direction = DMA_MEMORY_TO_DEVICE;
    hdma->init.src_dev = DMA_ID_DRAM;
    hdma->init.src_mode = DMA_MODE_HANDSHAKE;
    hdma->init.src_burst = DMA_XFER_BURST_16;
    hdma->init.src_addr_mode = DMA_ADDR_LINEAR_MODE;
    hdma->init.src_data_width = (priv->audio.init.channel == AUDIO_CHANNEL_MONO)
                                    ? DMA_DATA_WIDTH_2_BYTES
                                    : DMA_DATA_WIDTH_4_BYTES;
    hdma->init.snk_dev = HAL_DMA_ID_AUDIO;
    hdma->init.snk_mode = DMA_MODE_HANDSHAKE;
    hdma->init.snk_burst = DMA_XFER_BURST_1;
    hdma->init.snk_addr_mode = DMA_ADDR_FIXED_MODE;
    hdma->init.snk_data_width = hdma->init.src_data_width;
    hdma->init.flag = HAL_HANDLE_ALL_INIT_FLAG;

    hal_audio_register_callback(&priv->audio, AUDIO_CB_TX_DMA,
                                aic_audio_dma_callback);
    if (hal_dma_init(hdma) != HAL_OK) {
      return -EIO;
    }
  }

  if (hal_audio_init(&priv->audio) != HAL_OK) {
    return -EIO;
  }

  // if (hal_audio_set_volume(&priv->audio, priv->volume) != HAL_OK) {
  //   hal_audio_deinit(&priv->audio);
  //   return -EIO;
  // }

  aic_audio_dump_regs(priv, "before-playback-start");
  auderr("audio dump pre-playback-start\n");
  aic_audio_dump_runtime_config(priv);

  aic_audio_enable_pa(priv);

  priv->started = true;
  aic_audio_start_next_buffer(priv);
  return OK;
}

#ifndef CONFIG_AUDIO_EXCLUDE_STOP
static int aic_audio_stop(FAR struct audio_lowerhalf_s *dev) {
  struct aic_audio_s *priv = (struct aic_audio_s *)dev;
  FAR struct ap_buffer_s *apb;
  irqstate_t flags;

  aic_audio_dump_regs(priv, "before-playback-stop");
  if (priv->started) {
    priv->started = false;
    aic_audio_disable_pa(priv);
    hal_audio_abort(&priv->audio);
    hal_audio_deinit(&priv->audio);
    nxsig_usleep(50000);
  }
  audwarn("audio stop\n");
  flags = enter_critical_section();
  apb = priv->active;
  priv->active = NULL;
  leave_critical_section(flags);
  aic_audio_complete_apb(priv, apb);

  while (!dq_empty(&priv->pendq)) {
    flags = enter_critical_section();
    apb = (FAR struct ap_buffer_s *)dq_remfirst(&priv->pendq);
    leave_critical_section(flags);
    aic_audio_complete_apb(priv, apb);
  }

  priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK);
  return OK;
}
#endif

#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
static int aic_audio_pause(FAR struct audio_lowerhalf_s *dev) {
  struct aic_audio_s *priv = (struct aic_audio_s *)dev;

  if (!priv->started) {
    return -EINVAL;
  }

  audinfo("audio pause\n");
  return aic_audio_pause_transfer(priv);
}

static int aic_audio_resume(FAR struct audio_lowerhalf_s *dev) {
  struct aic_audio_s *priv = (struct aic_audio_s *)dev;

  if (!priv->started) {
    return -EINVAL;
  }

  audinfo("audio resume\n");
  return aic_audio_resume_transfer(priv);
}
#endif

static int aic_audio_allocbuffer(FAR struct audio_lowerhalf_s *dev,
                                 FAR struct audio_buf_desc_s *bufdesc) {
  struct aic_audio_s *priv = (struct aic_audio_s *)dev;
  FAR struct ap_buffer_s *apb;
  audwarn("audio alloc buffer numbytes=%u\n", (unsigned int)bufdesc->numbytes);
  if (bufdesc->numbytes != priv->buffer_size) {
    auderr("audio alloc buffer failed: requested buffer size %u does not match "
           "period size %u\n",
           (unsigned int)bufdesc->numbytes, (unsigned int)priv->buffer_size);
    return -EINVAL;
  }

  if (priv->alloc_index == priv->buffer_num) {
    auderr("audio alloc buffer failed: all buffers allocated\n");
    return -ENOMEM;
  }

  if (priv->alloc_addr == NULL) {
    priv->alloc_addr = kumm_memalign(32, priv->buffer_num * priv->buffer_size);
    if (priv->alloc_addr == NULL) {
      auderr("audio alloc buffer failed: memory allocation error\n");
      return -ENOMEM;
    }
  }

  apb = kumm_zalloc(sizeof(struct ap_buffer_s));
  *bufdesc->u.pbuffer = apb;
  if (apb == NULL) {
    auderr("audio alloc buffer failed: memory allocation error\n");
    return -ENOMEM;
  }

  apb->i.channels = priv->channels == 0 ? 2 : priv->channels;
  apb->crefs = 1;
  apb->nmaxbytes = priv->buffer_size;
  apb->samp = priv->alloc_addr + (priv->alloc_index * priv->buffer_size);
  priv->alloc_index++;
  nxmutex_init(&apb->lock);

  return sizeof(struct audio_buf_desc_s);
}

static int aic_audio_freebuffer(FAR struct audio_lowerhalf_s *dev,
                                FAR struct audio_buf_desc_s *bufdesc) {
  struct aic_audio_s *priv = (struct aic_audio_s *)dev;
  FAR struct ap_buffer_s *apb = bufdesc->u.buffer;
  audwarn("audio free buffer apb=%p\n", apb);
  if (priv->alloc_index > 0) {
    priv->alloc_index--;
  }

  nxmutex_destroy(&apb->lock);
  kumm_free(apb);

  if (priv->alloc_index == 0) {
    kumm_free(priv->alloc_addr);
    priv->alloc_addr = NULL;
  }

  return OK;
}

static int aic_audio_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                   FAR struct ap_buffer_s *apb) {
  struct aic_audio_s *priv = (struct aic_audio_s *)dev;
  irqstate_t flags;

  apb_reference(apb);
  apb->flags |= AUDIO_APB_OUTPUT_ENQUEUED;

  flags = enter_critical_section();
  dq_addlast(&apb->dq_entry, &priv->pendq);
  leave_critical_section(flags);

  up_clean_dcache((uintptr_t)apb->samp, (uintptr_t)apb->samp + apb->nbytes);

  audinfo("audio enqueue apb=%p nbytes=%d curbyte=%d\n", apb, apb->nbytes,
          apb->curbyte);
  if (priv->xrun && priv->started && priv->active == NULL) {
    priv->xrun = false;
    aic_audio_start_next_buffer(priv);
  }

  return OK;
}

static int aic_audio_ioctl(FAR struct audio_lowerhalf_s *dev, int cmd,
                           unsigned long arg) {
  struct aic_audio_s *priv = (struct aic_audio_s *)dev;

  audwarn("audio ioctl cmd=%d arg=%lu\n", cmd, arg);

  switch (cmd) {
  case AUDIOIOC_GETBUFFERINFO: {
    FAR struct ap_buffer_info_s *info = (FAR struct ap_buffer_info_s *)arg;
    info->nbuffers = CONFIG_AIC_AUDIO_PERIOD_COUNT;
    info->buffer_size = AIC_AUDIO_PERIOD_BYTES;
    return OK;
  }

  case AUDIOIOC_SETBUFFERINFO: {
    FAR struct ap_buffer_info_s *info = (FAR struct ap_buffer_info_s *)arg;
    if (info->buffer_size != AIC_AUDIO_PERIOD_BYTES) {
      return -EINVAL;
    }

    return OK;
  }

  case AUDIOIOC_GETLATENCY: {
    FAR long *latency = (FAR long *)arg;
    uint32_t residue = 0;
    uint32_t queued_bytes = 0;
    apb_samp_t frame_bytes;
    irqstate_t flags;
    FAR struct ap_buffer_s *apb;

    frame_bytes = (apb_samp_t)((priv->samplebits / 8) * priv->channels);
    if (frame_bytes == 0) {
      frame_bytes = 4;
    }

    flags = enter_critical_section();
    apb = (FAR struct ap_buffer_s *)dq_peek(&priv->pendq);
    while (apb) {
      if (apb->nbytes > apb->curbyte) {
        queued_bytes += apb->nbytes - apb->curbyte;
      }

      apb = (FAR struct ap_buffer_s *)dq_next(&apb->dq_entry);
    }
    leave_critical_section(flags);

    if (priv->active != NULL) {
      if (!aic_audio_use_cpu_tx() &&
          hal_dma_get_residue(&priv->txdma_chan.hal, &residue) == HAL_OK) {
        queued_bytes += residue;
      } else if (aic_audio_use_cpu_tx()) {
        queued_bytes += priv->audio.tx_xfer_count * aic_audio_frame_bytes(priv);
      } else if (priv->active->nbytes > priv->active->curbyte) {
        queued_bytes += priv->active->nbytes - priv->active->curbyte;
      }
    }

    *latency = queued_bytes / frame_bytes;
    audwarn("audio latency frames=%ld queued_bytes=%u residue=%u\n", *latency,
            (unsigned int)queued_bytes, (unsigned int)residue);
    return OK;
  }

  case AUDIOIOC_FLUSH:
#ifndef CONFIG_AUDIO_EXCLUDE_STOP
    return aic_audio_stop(dev);
#else
    return OK;
#endif

  default:
    return -ENOTTY;
  }
}

static int aic_audio_reserve(FAR struct audio_lowerhalf_s *dev) { return OK; }

static int aic_audio_release(FAR struct audio_lowerhalf_s *dev) { return OK; }

FAR struct audio_lowerhalf_s *aic_audio_initialize(void) {
  struct aic_audio_s *priv = &g_aic_audio;
  unsigned int group;
  unsigned int pin;

  memset(priv, 0, sizeof(*priv));

  priv->dev.ops = &g_aic_audio_ops;
  priv->volume = AIC_AUDIO_DEFAULT_VOLUME;
  priv->samplerate = 48000;
  priv->channels = 2;
  priv->samplebits = 16;
  priv->buffer_size = CONFIG_AUDIO_BUFFER_NUMBYTES;
  priv->buffer_num = CONFIG_AUDIO_NUM_BUFFERS;
  dq_init(&priv->pendq);

  hal_audio_handle_init(&priv->audio);
  auderr("audio init ,dma mode %d\n", !aic_audio_use_cpu_tx());
  if (!aic_audio_use_cpu_tx()) {
    hal_dma_handle_init(&(priv->txdma_chan.hal));
    priv->txdma_chan.hal.regbase = DMA_BASE;
    priv->txdma_chan.hal.init.channel_id = 0;
    priv->txdma_chan.hal.parent = (void *)&priv->audio;
    priv->txdma_chan.hal.work_mode = DMA_WORK_MODE_NORMAL;
    priv->txdma_chan.hal.cyclic_period_len = 0;
    priv->txdma_chan.hal.init.direction = DMA_MEMORY_TO_DEVICE;
    priv->txdma_chan.hal.init.src_dev = DMA_ID_DRAM;
    priv->txdma_chan.hal.init.src_mode = DMA_MODE_HANDSHAKE;
    priv->txdma_chan.hal.init.src_burst = DMA_XFER_BURST_16;
    priv->txdma_chan.hal.init.src_addr_mode = DMA_ADDR_LINEAR_MODE;
    priv->txdma_chan.hal.init.src_data_width = DMA_DATA_WIDTH_4_BYTES;

    priv->txdma_chan.hal.init.snk_dev = HAL_DMA_ID_AUDIO;
    priv->txdma_chan.hal.init.snk_mode = DMA_MODE_HANDSHAKE;
    priv->txdma_chan.hal.init.snk_burst = DMA_XFER_BURST_1;
    priv->txdma_chan.hal.init.snk_addr_mode = DMA_ADDR_FIXED_MODE;
    priv->txdma_chan.hal.init.snk_data_width = DMA_DATA_WIDTH_4_BYTES;
    priv->txdma_chan.hal.init_cb = aic_audio_dma_init_callback;
    priv->txdma_chan.hal.deinit_cb = aic_audio_dma_deinit_callback;
    priv->txdma_chan.hal.state = DMA_STATE_RESET;
    priv->txdma_chan.hal.init.flag = HAL_HANDLE_ALL_INIT_FLAG;
    hal_audio_register_callback(&priv->audio, AUDIO_CB_TX_DMA,
                                aic_audio_dma_callback);
    aic_dma_chan_register(&priv->txdma_chan.chan);
  }
  priv->audio.init.channel = AUDIO_CHANNEL_STEREO;
  priv->audio.init.samplebits = AUDIO_SAMPLEBITS_16BIT;
  priv->audio.init.samplerate = AUDIO_SAMPLERATE_48000;
  priv->audio.init.output_chan = aic_audio_output_channel();

  if (hal_audio_init(&priv->audio) != HAL_OK) {
    return NULL;
  }

#ifdef CONFIG_AIC_AUDIO_PA_ENABLE_GPIO
  priv->gpio_pa = hal_gpio_name2pin(CONFIG_AIC_AUDIO_PA_ENABLE_GPIO);
  group = GPIO_GROUP(priv->gpio_pa);
  pin = GPIO_GROUP_PIN(priv->gpio_pa);
  hal_gpio_direction_output(group, pin);
  aic_audio_disable_pa(priv);
#endif

  // if (hal_audio_set_volume(&priv->audio, priv->volume) != HAL_OK) {
  //   return NULL;
  // }
  audinfo("audio init default rate=%u channels=%u samplebits=%u volume=%u\n",
          (unsigned int)priv->samplerate, priv->channels, priv->samplebits,
          priv->volume);
  return &priv->dev;
}

int aic_audio_register(const char *name) {
  FAR struct audio_lowerhalf_s *lower;

  lower = aic_audio_initialize();
  if (!lower) {
    return -ENODEV;
  }

  return audio_register(name, lower);
}
