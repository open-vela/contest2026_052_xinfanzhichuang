/*
 * Copyright (c) 2022-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: dwj <weijie.ding@artinchip.com>
 */

#include <errno.h>
#include <string.h>

#include "hal_audio.h"
#include "aic_soc.h"
#include <debug.h>
#include <nuttx/clock.h>

#define TXFIFO_CH1_EN BIT(1)
#define TXFIFO_CH0_EN BIT(0)
#define TXFIFO_CH_MASK (3)

static uint64_t hal_get_tick(void) { return clock_systime_ticks(); }

static void hal_audio_clock_init(void){
  hal_clk_set_freq(CLK_CODEC, AUDIO_FREQ_24576000);
  hal_clk_set_freq(CLK_AUDIO_SCLK, AUDIO_FREQ_24576000);

  int ret = hal_clk_enable_deassertrst_iter(CLK_CODEC);
  if (ret) {
    hal_log_err("Audio init error!\n");
  }
}
/**
 * @brief Configures AUDIO for PWM0 single output mode.
 *
 *        This function sets up the audio hardware to use PWM0 as the single
 * output channel. It enables the necessary digital volume control, interface,
 * and sigma-delta modulator components, configures the mixer path, and sets
 * PWM0 to single output mode.
 *
 * @param[in,out] haudio Pointer to AUDIO handle structure
 *
 * @note This function is internally used by the audio driver and should not be
 * called directly. It assumes that the AUDIO handle has been properly
 * initialized.
 *
 * @warning Do not call this function from an interrupt context.
 */
static void audio_set_pwm0(hal_audio_handle_t *haudio) {
  uint32_t val;

  if (!haudio)
    return;
  /* Enable DVC3 */
  val = readl(TX_DVC3_4_CTL_REG(haudio));
  val &= ~TX_DVC3_4_CTL_DVC3_MASK;
  val |= TX_DVC3_4_CTL_DVC3_EN;
  writel(val, TX_DVC3_4_CTL_REG(haudio));
  /* Enable IF */
  val = readl(TX_PLAYBACK_CTL_REG(haudio));
  val &= ~TX_PLAYBACK_CTL_IF_CH0_MASK;
  val &= ~TX_PLAYBACK_CTL_IF_CH1_MASK;
  val |= TX_PLAYBACK_CTL_PLAYBACK_IF_EN | TX_PLAYBACK_CTL_IF_CH0_EN |
         TX_PLAYBACK_CTL_IF_CH1_EN;
  writel(val, TX_PLAYBACK_CTL_REG(haudio));
  /* Enable SDM0 */
  val = readl(TX_SDM_CTL_REG(haudio));
  val &= ~TX_SDM_CTL_TX_CH0_SDM_EN_MASK;
  val |= TX_SDM_CTL_TX_CH0_SDM_EN;
  writel(val, TX_SDM_CTL_REG(haudio));
  /* Configure MIXER audio path */
  val = readl(TX_MIXER_CTL_REG(haudio));
  val &= ~TX_MIXER_CTL_MIXER0_PATH_MASK;
  val |= TX_MIXER_CTL_MIXER0_AUDOUTL_SEL | TX_MIXER_CTL_MIXER1_AUDOUTR_SEL;
  writel(val, TX_MIXER_CTL_REG(haudio));
  /* Configure PWM0 single output */
  val = readl(TX_PWM_CTL_REG(haudio));
  val &= ~TX_PWM_CTL_PWM0_MODE_MASK;
  val |= TX_PWM_CTL_PWM0_EN | TX_PWM_CTL_PWM0_FADE_EN;
  /* Check output mode */
  if (haudio->init.out_mode == AUDIO_OUTPUT_MODE_PDM)
    val |= TX_PWM_CTL_PWM0_MODE_PDM;

  writel(val, TX_PWM_CTL_REG(haudio));
}

/**
 * @brief Configures AUDIO for PWM1 single output mode.
 *
 *        This function sets up the audio hardware to use PWM1 as the single
 * output channel. It enables the necessary digital volume control, interface,
 * and sigma-delta modulator components, configures the mixer path, and sets
 * PWM1 to single output mode.
 *
 * @param[in,out] haudio Pointer to AUDIO handle structure
 *
 * @note This function is internally used by the audio driver and should not be
 * called directly. It assumes that the AUDIO handle has been properly
 * initialized.
 *
 * @warning Do not call this function from an interrupt context.
 */
static void audio_set_pwm1(hal_audio_handle_t *haudio) {
  uint32_t val;

  if (!haudio)
    return;
  /* Enable DVC4 */
  val = readl(TX_DVC3_4_CTL_REG(haudio));
  val &= ~TX_DVC3_4_CTL_DVC4_MASK;
  val |= TX_DVC3_4_CTL_DVC4_EN;
  writel(val, TX_DVC3_4_CTL_REG(haudio));
  /* Enable IF */
  val = readl(TX_PLAYBACK_CTL_REG(haudio));
  val &= ~TX_PLAYBACK_CTL_IF_CH0_MASK;
  val &= ~TX_PLAYBACK_CTL_IF_CH1_MASK;
  val |= TX_PLAYBACK_CTL_PLAYBACK_IF_EN | TX_PLAYBACK_CTL_IF_CH0_EN |
         TX_PLAYBACK_CTL_IF_CH1_EN;
  writel(val, TX_PLAYBACK_CTL_REG(haudio));
  /* Enable SDM1 */
  val = readl(TX_SDM_CTL_REG(haudio));
  val &= ~TX_SDM_CTL_TX_CH1_SDM_EN_MASK;
  val |= TX_SDM_CTL_TX_CH1_SDM_EN;
  writel(val, TX_SDM_CTL_REG(haudio));
  /* Configure MIXER audio path */
  val = readl(TX_MIXER_CTL_REG(haudio));
  val &= ~TX_MIXER_CTL_MIXER1_PATH_MASK;
  val |= TX_MIXER_CTL_MIXER0_AUDOUTR_SEL | TX_MIXER_CTL_MIXER1_AUDOUTL_SEL;
  writel(val, TX_MIXER_CTL_REG(haudio));
  /* Configure PWM1 single output */
  val = readl(TX_PWM_CTL_REG(haudio));
  val &= ~TX_PWM_CTL_PWM1_MODE_MASK;
  val |= TX_PWM_CTL_PWM1_EN | TX_PWM_CTL_PWM1_FADE_EN;
  /* Check output mode */
  if (haudio->init.out_mode == AUDIO_OUTPUT_MODE_PDM)
    val |= TX_PWM_CTL_PWM1_MODE_PDM;

  writel(val, TX_PWM_CTL_REG(haudio));
}

/**
 * @brief Configures AUDIO output channels based on initialization parameters.
 *
 *        This function selects and configures the appropriate output channels
 *        according to the output_chan parameter in the AUDIO initialization
 * structure. It supports various configurations including single channel, dual
 * channel, and differential outputs.
 *
 * @param[in,out] haudio Pointer to AUDIO handle structure
 *
 * @note This function is internally used by the audio driver and should not be
 * called directly. It assumes that the AUDIO handle has been properly
 * initialized.
 *
 * @warning Do not call this function from an interrupt context.
 */
static void audio_set_channel(hal_audio_handle_t *haudio) {
  uint32_t val;

  if (!haudio)
    return;
  /* Set PWM channel */
  switch (haudio->init.output_chan) {
  case AUDIO_OUTPUT_CHAN_PWM1:
    audio_set_pwm1(haudio);
    break;
  case AUDIO_OUTPUT_CHAN_PWM0_PWM1:
    audio_set_pwm0(haudio);
    audio_set_pwm1(haudio);
    break;
  case AUDIO_OUTPUT_CHAN_PWM0_DIFF:
    audio_set_pwm0(haudio);
    audio_set_pwm1(haudio);
    val = readl(TX_PWM_CTL_REG(haudio));
    val &= ~(TX_PWM_CTL_PWM0_MODE_MASK | TX_PWM_CTL_PWM1_MODE_MASK);
    val |= TX_PWM_CTL_PWM0_EN | TX_PWM_CTL_PWM0_DIF_EN;
    writel(val, TX_PWM_CTL_REG(haudio));
    break;
  case AUDIO_OUTPUT_CHAN_PWM1_DIFF:
    audio_set_pwm0(haudio);
    audio_set_pwm1(haudio);
    val = readl(TX_PWM_CTL_REG(haudio));
    val &= ~(TX_PWM_CTL_PWM0_MODE_MASK | TX_PWM_CTL_PWM1_MODE_MASK);
    val |= TX_PWM_CTL_PWM1_EN | TX_PWM_CTL_PWM1_DIF_EN;
    writel(val, TX_PWM_CTL_REG(haudio));
    break;
  case AUDIO_OUTPUT_CHAN_PWM0:
  default:
    audio_set_pwm0(haudio);
    break;
  }
}

void hal_audio_set_channel(hal_audio_handle_t *haudio) {
  uint32_t reg_val;

  reg_val = readl(TXFIFO_CTL_REG(haudio));
  reg_val &= ~TXFIFO_CH_MASK;
  if (haudio->init.channel == AUDIO_CHANNEL_MONO) {
    reg_val |= TXFIFO_CH0_EN;
  } else if (haudio->init.channel == AUDIO_CHANNEL_STEREO) {
    reg_val |= TXFIFO_CH0_EN | TXFIFO_CH1_EN;
  }
  writel(reg_val, TXFIFO_CTL_REG(haudio));
}

/**
 * @brief Configures AUDIO sampling rate based on initialization parameters.
 *
 *        This function sets the appropriate hardware divider values and clock
 * frequencies according to the samplerate parameter in the AUDIO initialization
 * structure. It ensures that the audio hardware operates at the correct
 * sampling frequency.
 *
 * @param[in,out] haudio Pointer to AUDIO handle structure
 *
 * @note This function is internally used by the audio driver and should not be
 * called directly. It assumes that the AUDIO handle has been properly
 * initialized.
 *
 * @warning Do not call this function from an interrupt context.
 */
void hal_audio_set_samplerate(hal_audio_handle_t *haudio) {
  uint32_t val, module_freq, hw_rate = 0;

  if (!haudio)
    return;
  /* Get frequency division factor */
  switch (haudio->init.samplerate) {
  case AUDIO_SAMPLERATE_48000:
  case AUDIO_SAMPLERATE_44100:
    hw_rate = 0;
    break;
  case AUDIO_SAMPLERATE_32000:
    hw_rate = 1;
    break;
  case AUDIO_SAMPLERATE_24000:
  case ADUIO_SAMPLERATE_22050:
    hw_rate = 2;
    break;
  case ADUIO_SAMPLERATE_16000:
    hw_rate = 3;
    break;
  case ADUIO_SAMPLERATE_11025:
  case ADUIO_SAMPLERATE_12000:
    hw_rate = 4;
    break;
  case ADUIO_SAMPLERATE_8000:
    hw_rate = 5;
    break;
  default:
    break;
  }
  /* Set playback samplerate */
  val = readl(TX_PLAYBACK_CTL_REG(haudio));
  val &= ~TX_PLAYBACK_CTL_FS_OUT_MASK;
  val |= TX_PLAYBACK_CTL_FS_OUT(hw_rate);
  writel(val, TX_PLAYBACK_CTL_REG(haudio));
  /* Get module frequency */
  switch (haudio->init.samplerate) {
  case AUDIO_SAMPLERATE_48000:
  case AUDIO_SAMPLERATE_32000:
  case AUDIO_SAMPLERATE_24000:
  case ADUIO_SAMPLERATE_16000:
  case ADUIO_SAMPLERATE_12000:
  case ADUIO_SAMPLERATE_8000:
    module_freq = AUDIO_FREQ_24576000;
    break;
  case AUDIO_SAMPLERATE_44100:
  case ADUIO_SAMPLERATE_22050:
  case ADUIO_SAMPLERATE_11025:
    module_freq = AUDIO_FREQ_22579200;
    break;
  default:
    module_freq = 0;
  }
  hal_clk_set_freq(CLK_CODEC, module_freq);
  hal_clk_set_freq(CLK_AUDIO_SCLK, module_freq);
}

/**
 * @brief Initializes the AUDIO handle structure with default values.
 *
 *        This function initializes all fields of the AUDIO handle structure to
 * their default values. It prepares the handle for subsequent initialization
 * and usage.
 *
 * @param[in,out] haudio Pointer to AUDIO handle structure to initialize
 *
 * @note This function must be called before any other AUDIO functions.
 *       The application must still set the samplebits, samplerate, and channel
 * parameters.
 *
 * @warning Do not call this function from an interrupt context.
 */
void hal_audio_handle_init(hal_audio_handle_t *haudio) {
  if (!haudio)
    return;
  /* Initialize basic state and configuration of AUDIO handle */
  haudio->regbase = AUDIO_BASE;
  haudio->lock = HAL_UNLOCKED;
  haudio->errcode = 0;
  haudio->state = AUDIO_STATE_RESET;
  haudio->ptxbuffer = NULL;
  haudio->tx_xfer_size = 0;
  haudio->tx_xfer_count = 0;
  haudio->txdma = NULL;
  haudio->init_cb = NULL;
  haudio->deinit_cb = NULL;
  haudio->tx_interrupt_cb = NULL;
  haudio->tx_dma_cb = NULL;
  haudio->xfer_error_cb = NULL;
  haudio->xfer_abort_cb = NULL;

  haudio->init.target_volume = ADUIO_DEFAULT_TAR_VOLUME;
  haudio->init.fade_step = AUDIO_DEFAULT_FADE_STEP;
  haudio->init.fade_speed = AUDIO_DEFAULT_FADE_SPEED;
  haudio->init.dvc_volume = AUDIO_DEFAULT_DVC_VOLUME;
  haudio->init.fltout_shift = AUDIO_FLTOUT_SHIFT_13BIT;
  haudio->init.output_chan = AUDIO_OUTPUT_CHAN_PWM1;
  haudio->init.out_mode = AUDIO_OUTPUT_MODE_PWM;
  haudio->init.sdm_gain = AUDIO_SDM_GAIN_1;
  haudio->init.tx_dlt = AUDIO_TX_DELAY_5MS;
  /* The APP must set the following three parameters */
  haudio->init.samplebits = 0;
  haudio->init.samplerate = 0;
  haudio->init.channel = 0;
  haudio->init.flag = HAL_HANDLE_ALL_INIT_FLAG;
}

/**
 * @brief Initializes the AUDIO peripheral with specified parameters.
 *
 *        This function fully initializes the AUDIO peripheral according to the
 * parameters specified in the AUDIO handle structure. It configures all
 * hardware registers, sets up the audio path, and prepares the peripheral for
 * data transmission.
 *
 * @param[in,out] haudio Pointer to AUDIO handle structure containing
 * initialization parameters
 *
 * @return HAL_OK on successful initialization, HAL_ERROR if parameters are
 * invalid, HAL_BUSY if AUDIO is busy
 *
 * @note This function requires that the init_cb callback is registered to
 * configure clocks. The samplebits, samplerate, and channel parameters must be
 * set by the application.
 *
 * @warning Do not call this function from an interrupt context.
 */
hal_status_e hal_audio_init(hal_audio_handle_t *haudio) {
  uint32_t val, fade_val;
  uint16_t target_vol;

  if (!haudio)
    return HAL_ERROR;

  ASSERT_PARAM(0 != (haudio->init.flag & HAL_HANDLE_ALL_INIT_FLAG));
  ASSERT_PARAM(haudio->regbase != 0);

  HAL_LOCK(haudio);
  if (haudio->init_cb)
    /* Should set input frequency and enable clock for AUDIO in callback */
    haudio->init_cb(haudio);
  else {
    hal_audio_clock_init();
  }
  /* Set audio volume */
  if (haudio->init.target_volume != ADUIO_DEFAULT_TAR_VOLUME) {
    target_vol = ADUIO_DEFAULT_TAR_VOLUME - haudio->init.target_volume;
    if (target_vol == 100)
      fade_val = 0;
    else
      fade_val = AUDIO_DEFAULT_FADE_VOL - (target_vol * AUDIO_DEFAULT_FADE_MAP);
    val = readl(FADE_CTL1_REG(haudio));
    val &= ~FADE_CTL1_TARGET_VOL_MASK;
    val |= fade_val;
    writel(val, FADE_CTL1_REG(haudio));
  }
  /* Set fade step */
  if (haudio->init.fade_step != AUDIO_DEFAULT_FADE_STEP) {
    val = readl(FADE_CTL0_REG(haudio));
    val &= ~FADE_CTL0_FADE_STEP_MASK;
    val |= haudio->init.fade_step;
    writel(val, FADE_CTL0_REG(haudio));
  }
  /* Set fade speed */
  if (haudio->init.fade_speed != AUDIO_DEFAULT_FADE_SPEED) {
    val = readl(FADE_CTL0_REG(haudio));
    val &= ~FADE_CTL0_FADE_SPEED_MASK;
    val |= haudio->init.fade_speed;
    writel(val, FADE_CTL0_REG(haudio));
  }
  /* Set DVC gain */
  if (haudio->init.dvc_volume != AUDIO_DEFAULT_DVC_VOLUME) {
    val = readl(TX_DVC3_4_CTL_REG(haudio));
    val &= ~TX_DVC3_4_CTL_DVC3_GAIN_MASK;
    val &= ~TX_DVC3_4_CTL_DVC4_GAIN_MASK;
    val |= (haudio->init.dvc_volume << TX_DVC3_4_CTL_DVC3_GAIN_SHIFT |
            haudio->init.dvc_volume << TX_DVC3_4_CTL_DVC4_GAIN_SHIFT);
    writel(val, TX_DVC3_4_CTL_REG(haudio));
  }
  /* Set filter shift */
  if (haudio->init.fltout_shift != AUDIO_FLTOUT_SHIFT_13BIT) {
    val = readl(TX_PLAYBACK_CTL_REG(haudio));
    val &= ~TX_PLAYBACK_CTL_FLTOUT_SHIFTN_MASK;
    val |= haudio->init.fltout_shift << TX_PLAYBACK_CTL_FLTOUT_SHIFTN_SHIFT;
    writel(val, TX_PLAYBACK_CTL_REG(haudio));
  }
  /* Set output and check output mode */
  audio_set_channel(haudio);
  /* Set SDM gain */
  if (haudio->init.sdm_gain != AUDIO_SDM_GAIN_1) {
    val = readl(TX_SDM_CTL_REG(haudio));
    val &= ~(TX_SDM_CTL_TX_CH0_SDM_GAIN_A0_MASK |
             TX_SDM_CTL_TX_CH0_SDM_GAIN_B0_MASK |
             TX_SDM_CTL_TX_CH0_SDM_GAIN_C0_MASK |
             TX_SDM_CTL_TX_CH1_SDM_GAIN_A0_MASK |
             TX_SDM_CTL_TX_CH1_SDM_GAIN_B0_MASK |
             TX_SDM_CTL_TX_CH1_SDM_GAIN_C0_MASK);
    val |= (haudio->init.sdm_gain << TX_SDM_CTL_TX_CH0_SDM_GAIN_A0_SHIFT |
            haudio->init.sdm_gain << TX_SDM_CTL_TX_CH0_SDM_GAIN_B0_SHIFT |
            haudio->init.sdm_gain << TX_SDM_CTL_TX_CH0_SDM_GAIN_C0_SHIFT |
            haudio->init.sdm_gain << TX_SDM_CTL_TX_CH1_SDM_GAIN_A0_SHIFT |
            haudio->init.sdm_gain << TX_SDM_CTL_TX_CH1_SDM_GAIN_B0_SHIFT |
            haudio->init.sdm_gain << TX_SDM_CTL_TX_CH1_SDM_GAIN_C0_SHIFT);
    writel(val, TX_SDM_CTL_REG(haudio));
  }
  /* Check samplebits */
  if (haudio->init.samplebits != AUDIO_SAMPLEBITS_16BIT) {
    haudio->errcode = AUDIO_ERROR_INVALID_PARAM;
    return HAL_ERROR;
  }
  /* Set samplerate */
  hal_audio_set_samplerate(haudio);
  /* Set output channel num */
  val = readl(TXFIFO_CTL_REG(haudio));
  val &= ~(TXFIFO_CTL_TX_FIFO_CH0_EN_MASK | TXFIFO_CTL_TX_FIFO_CH1_EN_MASK);
  /* Maybe need to check the channel num, now is all fifo open */
  val |= TXFIFO_CTL_TX_FIFO_CH0_EN | TXFIFO_CTL_TX_FIFO_CH1_EN;
  writel(val, TXFIFO_CTL_REG(haudio));
  /*Flush TXFIFO*/
  val = readl(TXFIFO_CTL_REG(haudio));
  val |= TXFIFO_CTL_TX_FIFO_FLUSH_MASK;
  val = readl(TXFIFO_CTL_REG(haudio));

  haudio->errcode = AUDIO_ERROR_NONE;
  haudio->state = AUDIO_STATE_READY;
  HAL_UNLOCK(haudio);

  return HAL_OK;
}

/**
 * @brief Deinitializes the AUDIO peripheral and releases resources.
 *
 *        This function resets the AUDIO peripheral to its default state,
 * disables clocks, and releases any allocated resources. It prepares the
 * peripheral for re-initialization.
 *
 * @param[in,out] haudio Pointer to AUDIO handle structure
 *
 * @return HAL_OK on successful deinitialization, HAL_ERROR if parameters are
 * invalid
 *
 * @note This function calls the deinit_cb callback if registered to release
 * hardware resources.
 *
 * @warning Do not call this function from an interrupt context.
 */
hal_status_e hal_audio_deinit(hal_audio_handle_t *haudio) {
  if (!haudio)
    return HAL_ERROR;

  ASSERT_PARAM(haudio->regbase != 0);

  /* Release other resource, and CMU disable AUDIO */
  if (haudio->deinit_cb)
    haudio->deinit_cb(haudio);

  /* Reset AUDIO GLOBE register */
  writel(0x4, GLOBE_CTL_REG(haudio));

  haudio->errcode = AUDIO_ERROR_NONE;
  haudio->state = AUDIO_STATE_RESET;
  HAL_UNLOCK(haudio);

  return HAL_OK;
}

/**
 * @brief AUDIO transmit interrupt service routine for 16-bit data.
 *
 *        This function is called when the AUDIO transmit FIFO has space
 * available. It writes 16-bit audio data from the buffer to the transmit FIFO
 * until completion.
 *
 * @param[in,out] haudio Pointer to AUDIO handle structure
 *
 * @note This function disables further TX interrupts after completing
 * transmission. It handles mono audio data (16 bits per sample).
 *
 * @warning Must only be called from AUDIO interrupt context.
 */
static void audio_tx_isr_16bit(hal_audio_handle_t *haudio) {
  uint32_t i, val, temp = 0;

  if (!haudio->tx_xfer_count) {
    /* Disable TX interrupt */
    writel(0x0, FIFO_INT_EN_REG(haudio));
    haudio->state = AUDIO_STATE_READY;
    if (haudio->tx_interrupt_cb)
      haudio->tx_interrupt_cb(haudio);
  } else {
    /* Get FIFO available space */
    val = ((readl(FIFO_STA_REG(haudio)) >> 16) & 0xff);
    /* Get the minimum value between FIFO available space and transfer count */
    val = (val > haudio->tx_xfer_count) ? haudio->tx_xfer_count : val;
    for (i = 0; i < val; i++) {
      temp = 0;
      temp = (uint32_t)(*haudio->ptxbuffer);
      haudio->ptxbuffer++;
      temp |= ((uint32_t)(*haudio->ptxbuffer) << 8);
      haudio->ptxbuffer++;
      /* Write 32bit data to TX FIFO */
      writel(temp, TXFIFO_DATA_REG(haudio));
      haudio->tx_xfer_count--;
    }
  }
}

/**
 * @brief AUDIO transmit interrupt service routine for 32-bit data.
 *
 *        This function is called when the AUDIO transmit FIFO has space
 * available. It writes 32-bit audio data from the buffer to the transmit FIFO
 * until completion.
 *
 * @param[in,out] haudio Pointer to AUDIO handle structure
 *
 * @note This function disables further TX interrupts after completing
 * transmission. It handles stereo audio data (32 bits per sample).
 *
 * @warning Must only be called from AUDIO interrupt context.
 */
static void audio_tx_isr_32bit(hal_audio_handle_t *haudio) {
  uint32_t i, val, temp = 0;

  if (!haudio->tx_xfer_count) {
    /* Disable TX interrupt */
    writel(0x0, FIFO_INT_EN_REG(haudio));
    haudio->state = AUDIO_STATE_READY;
    if (haudio->tx_interrupt_cb)
      haudio->tx_interrupt_cb(haudio);
  } else {
    /* Get FIFO available space */
    val = ((readl(FIFO_STA_REG(haudio)) >> 16) & 0xff);
    /* Get the minimum value between FIFO available space and transfer count */
    val = (val > haudio->tx_xfer_count) ? haudio->tx_xfer_count : val;
    for (i = 0; i < val; i++) {
      temp = 0;
      temp = (uint32_t)(*haudio->ptxbuffer);
      haudio->ptxbuffer++;
      temp |= ((uint32_t)(*haudio->ptxbuffer) << 8);
      haudio->ptxbuffer++;
      temp |= ((uint32_t)(*haudio->ptxbuffer) << 16);
      haudio->ptxbuffer++;
      temp |= ((uint32_t)(*haudio->ptxbuffer) << 24);
      haudio->ptxbuffer++;
      /* Write 32bit data to TX FIFO */
      writel(temp, TXFIFO_DATA_REG(haudio));
      haudio->tx_xfer_count--;
    }
  }
}

/**
 * @brief AUDIO DMA transfer complete callback.
 *
 *        This function is called when the DMA controller completes the entire
 * data transfer. It invokes the user-registered callback if available.
 *
 * @param[in,out] hdma Pointer to DMA handle structure
 *
 * @note This function is automatically called by the DMA subsystem.
 *
 * @warning Must only be called from DMA interrupt context.
 */
static void audio_dma_half_task_callback(hal_dma_handle_t *hdma) {
    hal_audio_handle_t *haudio = (hal_audio_handle_t *)((hal_dma_handle_t *)hdma)->parent;
  if (haudio->tx_dma_cb)
    haudio->tx_dma_cb(haudio);
}

 __attribute__((unused)) static void audio_dma_full_task_callback(hal_dma_handle_t *hdma) {
    hal_audio_handle_t *haudio = (hal_audio_handle_t *)((hal_dma_handle_t *)hdma)->parent;
    haudio->state = AUDIO_STATE_READY;
    csi_dcache_clean_range((uint32_t)haudio->ptxbuffer, haudio->tx_xfer_size);
    if (haudio->tx_dma_cb)
        haudio->tx_dma_cb(haudio);
}

/**
 * @brief Transmits audio data through AUDIO interface in blocking mode.
 *
 *        This function transmits a buffer of audio data through the AUDIO
 * transmitter in a blocking manner, waiting until all data is transmitted or
 * timeout occurs.
 *
 * @param[in,out] haudio  Pointer to AUDIO handle structure
 * @param[in]     pdata   Pointer to buffer containing data to transmit
 * @param[in]     size    Number of units to transmit
 * @param[in]     timeout Maximum time to wait for transmission completion (in
 * milliseconds)
 *
 * @return HAL_OK on successful transmission, HAL_ERROR if parameters are
 * invalid, HAL_TIMEOUT if transmission times out, HAL_BUSY if AUDIO is busy
 *
 * @note This is a blocking function that waits until all data is transmitted or
 * timeout occurs. For non-blocking operation, consider using interrupt-based or
 * DMA-based transmission.
 *
 * @warning Do not call this function from an interrupt context as it uses
 * polling mechanism.
 */
hal_status_e hal_audio_transmit(hal_audio_handle_t *haudio,
                                const uint8_t *pdata, uint32_t size,
                                uint32_t timeout) {
  uint64_t time_start;
  uint32_t val, temp;
  uint32_t frame_bytes;
  ASSERT_PARAM(haudio);
  ASSERT_PARAM(haudio->regbase != 0);

  if ((!pdata) || (size == 0))
    return HAL_ERROR;

  if (haudio->state != AUDIO_STATE_READY)
    return HAL_BUSY;

  HAL_LOCK(haudio);
  /* Init tickstart for timeout management*/
  time_start = hal_get_tick();
  /* Set state and reset error code */
  haudio->state = AUDIO_STATE_BUSY_TX;
  haudio->errcode = AUDIO_ERROR_NONE;
  haudio->ptxbuffer = pdata;
  haudio->tx_xfer_size = size;
  haudio->tx_xfer_count = size;

  /* Clean TX counter */
  writel(0, TX_CNT_REG(haudio));
  /* Enable TX GLBEN */
  val = readl(GLOBE_CTL_REG(haudio));
  if ((val & GLOBE_CTL_TX_GLBEN) == 0) {
    val |= GLOBE_CTL_TX_GLBEN;
    writel(val, GLOBE_CTL_REG(haudio));
  }

  frame_bytes = haudio->init.channel == AUDIO_CHANNEL_MONO ? 2 : 4;
  if ((size % frame_bytes) != 0) {
    haudio->errcode = AUDIO_ERROR_INVALID_PARAM;
    haudio->state = AUDIO_STATE_READY;
    HAL_UNLOCK(haudio);
    return HAL_ERROR;
  }
  haudio->tx_xfer_count = size / frame_bytes;

  while (haudio->tx_xfer_count > 0) {
    /* Init temp first */
    temp = 0;
    /* Timeout check */
    if ((hal_get_tick() - time_start) > timeout) {
      haudio->errcode = AUDIO_ERROR_TIMEOUT;
      haudio->state = AUDIO_STATE_READY;
      HAL_UNLOCK(haudio);
      return HAL_TIMEOUT;
    }
    /* Check FIFO status */
    val = ((readl(FIFO_STA_REG(haudio)) >> 16) & 0xff);
    if (val == 0)
      continue;
    /* Audio channel check */
    if (haudio->init.channel == AUDIO_CHANNEL_MONO) {
      temp = (uint32_t)(*haudio->ptxbuffer);
      haudio->ptxbuffer++;
      temp |= ((uint32_t)(*haudio->ptxbuffer) << 8);
      haudio->ptxbuffer++;
      /* Write 16bit data to TX FIFO */
      writel(temp, TXFIFO_DATA_REG(haudio));
    } else {
      temp = (uint32_t)(*haudio->ptxbuffer);
      haudio->ptxbuffer++;
      temp |= ((uint32_t)(*haudio->ptxbuffer) << 8);
      haudio->ptxbuffer++;
      temp |= ((uint32_t)(*haudio->ptxbuffer) << 16);
      haudio->ptxbuffer++;
      temp |= ((uint32_t)(*haudio->ptxbuffer) << 24);
      haudio->ptxbuffer++;
      /* Write 32bit data to TX FIFO */
      writel(temp, TXFIFO_DATA_REG(haudio));
    }
    haudio->tx_xfer_count--;
  }

  haudio->state = AUDIO_STATE_READY;
  haudio->errcode = AUDIO_ERROR_NONE;
  HAL_UNLOCK(haudio);

  return HAL_OK;
}

/**
 * @brief Transmits audio data through AUDIO interface using interrupt mode.
 *
 *        This function initiates transmission of audio data through the AUDIO
 * transmitter using interrupt-driven operation. It returns immediately after
 * setting up the transfer.
 *
 * @param[in,out] haudio Pointer to AUDIO handle structure
 * @param[in]     pdata  Pointer to buffer containing data to transmit
 * @param[in]     size   Number of units to transmit
 *
 * @return HAL_OK on successful initiation, HAL_ERROR if parameters are invalid,
 *         HAL_BUSY if AUDIO is busy
 *
 * @note This is a non-blocking function that returns immediately.
 *       Completion is indicated by the tx_interrupt_cb callback.
 *
 * @warning Do not call this function from an interrupt context.
 */
hal_status_e hal_audio_transmit_it(hal_audio_handle_t *haudio,
                                   const uint8_t *pdata, uint32_t size) {
  uint32_t val;

  ASSERT_PARAM(haudio);
  ASSERT_PARAM(haudio->regbase != 0);

  if ((!pdata) || (size == 0))
    return HAL_ERROR;

  if (haudio->state != AUDIO_STATE_READY)
    return HAL_BUSY;

  HAL_LOCK(haudio);
  /* Set state and reset error code */
  haudio->state = AUDIO_STATE_BUSY_TX;
  haudio->errcode = AUDIO_ERROR_NONE;
  haudio->ptxbuffer = pdata;
  haudio->tx_xfer_size = size;
  haudio->tx_xfer_count = size;
  /* Clean TX counter */
  writel(0x0, TX_CNT_REG(haudio));

  if (haudio->init.channel == AUDIO_CHANNEL_MONO) {
    haudio->tx_isr = audio_tx_isr_16bit;
  } else {
    haudio->tx_isr = audio_tx_isr_32bit;
  }
  /* Enable TX interrupt */
  val = readl(FIFO_INT_EN_REG(haudio));
  // val |= FIFO_INT_EN_AUDOUT_IRQ_EN | FIFO_INT_EN_AUDOUT_OVRN_IRQ_EN |
  //        FIFO_INT_EN_AUDOUT_UDRN_IRQ_EN;
  val |= FIFO_INT_EN_AUDOUT_IRQ_EN;
  writel(val, FIFO_INT_EN_REG(haudio));
  /* Enable TX GLBEN */
  val = readl(GLOBE_CTL_REG(haudio));
  if ((val & GLOBE_CTL_TX_GLBEN) == 0) {
    val |= GLOBE_CTL_TX_GLBEN;
    writel(val, GLOBE_CTL_REG(haudio));
  }

  HAL_UNLOCK(haudio);

  return HAL_OK;
}

/**
 * @brief Transmits audio data through AUDIO interface using DMA mode.
 *
 *        This function initiates transmission of audio data through the AUDIO
 * transmitter using DMA operation. It returns immediately after setting up the
 * transfer.
 *
 * @param[in,out] haudio Pointer to AUDIO handle structure
 * @param[in]     pdata  Pointer to buffer containing data to transmit
 * @param[in]     size   Number of units to transmit
 *
 * @return HAL_OK on successful initiation, HAL_ERROR if parameters are invalid,
 *         HAL_BUSY if AUDIO is busy, HAL_TIMEOUT if FIFO setup times out
 *
 * @note This is a non-blocking function that returns immediately.
 *       Completion is indicated by the tx_dma_cb callback.
 *       The function waits until FIFO is not empty to ensure DMA starts
 * correctly.
 *
 * @warning Do not call this function from an interrupt context.
 */
hal_status_e hal_audio_transmit_dma(hal_audio_handle_t *haudio,
                                    const uint8_t *pdata, uint32_t size) {
  hal_status_e ret = HAL_OK;
  uint32_t val;

  ASSERT_PARAM(haudio);
  ASSERT_PARAM(haudio->regbase != 0);

  if ((!pdata) || (size == 0))
    return HAL_ERROR;

  /* Clean TX counter */
  writel(0x0, TX_CNT_REG(haudio));

  if (haudio->state == AUDIO_STATE_READY) {
    HAL_LOCK(haudio);

    haudio->state = AUDIO_STATE_BUSY_TX;
    haudio->errcode = AUDIO_ERROR_NONE;
    haudio->ptxbuffer = pdata;
    haudio->tx_xfer_size = size;
    haudio->tx_xfer_count = size;
    /* Set the DMA Tx abort callback */
    haudio->txdma->xfer_abort_cb = NULL;

    ret = hal_dma_registger_callback(haudio->txdma,
                     DMA_CB_XFER_HALF_TASK_COMPLETE_CB,
                     NULL);
    if (ret != HAL_OK) {
      haudio->state = AUDIO_STATE_ERROR;
      haudio->errcode = AUDIO_ERROR_DMA;
      HAL_UNLOCK(haudio);
      return ret;
    }

    ret = hal_dma_registger_callback(haudio->txdma,
                                     DMA_CB_XFER_FULL_TASK_COMPLETE_CB,
                                     NULL);
    if (ret != HAL_OK) {
      haudio->state = AUDIO_STATE_ERROR;
      haudio->errcode = AUDIO_ERROR_DMA;
      HAL_UNLOCK(haudio);
      return ret;
    }

    ret = hal_dma_registger_callback(haudio->txdma,
                                     DMA_CB_XFER_ALL_TASKS_COMPLETE_CB,
                                     audio_dma_full_task_callback);
    if (ret != HAL_OK) {
      haudio->state = AUDIO_STATE_ERROR;
      haudio->errcode = AUDIO_ERROR_DMA;
      HAL_UNLOCK(haudio);
      return ret;
    }
    /* Enable the Tx DMA Stream */
    ret = hal_dma_start_it(haudio->txdma, (uint32_t)haudio->ptxbuffer,
                           TXFIFO_DATA_REG(haudio), size);
    if (ret != HAL_OK) {
      auderr("Failed to start AUDIO DMA transmission");
      haudio->state = AUDIO_STATE_ERROR;
      haudio->errcode = AUDIO_ERROR_DMA;
      HAL_UNLOCK(haudio);
      return ret;
    }
    /* Enable Audio Tx DMA request */
    val = readl(FIFO_INT_EN_REG(haudio));
    val |= FIFO_INT_EN_AUDOUT_DRQ_EN;
    writel(val, FIFO_INT_EN_REG(haudio));
    /* Check if the Audio is already enabled */
    val = readl(GLOBE_CTL_REG(haudio));
    if ((val & GLOBE_CTL_TX_GLBEN) == 0) {
      val |= GLOBE_CTL_TX_GLBEN;
      writel(val, GLOBE_CTL_REG(haudio));
    }
    HAL_UNLOCK(haudio);

    return HAL_OK;
  } else {
    auderr("AUDIO is busy, cannot start DMA transmission");
    return HAL_BUSY;
  }
}

/**
 * @brief Aborts ongoing AUDIO transmission.
 *
 *        This function aborts any ongoing audio transmission, whether using
 * polling, interrupt, or DMA methods. It resets the AUDIO peripheral to ready
 * state.
 *
 * @param[in,out] haudio Pointer to AUDIO handle structure
 *
 * @return HAL_OK on successful abortion, HAL_ERROR if parameters are invalid or
 * DMA abort fails
 *
 * @note This function disables all AUDIO interrupts and flushes the transmit
 * FIFO.
 *
 * @warning Do not call this function from an interrupt context.
 */
hal_status_e hal_audio_abort(hal_audio_handle_t *haudio) {
  hal_status_e status = HAL_OK;
  uint32_t val;

  if (!haudio)
    return HAL_ERROR;

  HAL_LOCK(haudio);
  /* Disabled Audio playback */
  val = readl(GLOBE_CTL_REG(haudio));
  val &= ~GLOBE_CTL_TX_GLBEN;
  writel(val, GLOBE_CTL_REG(haudio));
  /* Check Audio DMA is enabled or not */
  val = readl(FIFO_INT_EN_REG(haudio));
  if (val & FIFO_INT_EN_AUDOUT_DRQ_EN) {
    /* Disable Audio DMA request */
    val &= ~FIFO_INT_EN_AUDOUT_DRQ_EN;
    writel(val, FIFO_INT_EN_REG(haudio));
    /* Abort the Audio Tx DMA stream */
    if ((haudio->state == AUDIO_STATE_BUSY_TX) && (haudio->txdma != NULL)) {
      if (hal_dma_abort_it(haudio->txdma) != HAL_OK) {
        haudio->errcode |= AUDIO_ERROR_DMA;
        status = HAL_ERROR;
      }
    }
  }
  /* Disabled all interrupt and clear all the flag */
  writel(0x0, FIFO_INT_EN_REG(haudio));
  val = readl(FIFO_STA_REG(haudio));
  val &= ~(FIFO_STA_AUDOUT_IRQ_STA_MASK | FIFO_STA_AUDOUT_OVRN_IRQ_STA_MASK |
           FIFO_STA_AUDOUT_UDRN_IRQ_STA_MASK);
  writel(val, FIFO_STA_REG(haudio));
  /*Flush TXFIFO*/
  val = readl(TXFIFO_CTL_REG(haudio));
  val |= TXFIFO_CTL_TX_FIFO_FLUSH_MASK;
  val = readl(TXFIFO_CTL_REG(haudio));
  /* Set Audio state to ready */
  haudio->state = AUDIO_STATE_READY;

  HAL_UNLOCK(haudio);

  return status;
}

/**
 * @brief Pauses ongoing AUDIO DMA transmission.
 *
 *        This function temporarily pauses an ongoing DMA-based audio
 * transmission. The transmission can be resumed later using
 * hal_audio_dma_resume().
 *
 * @param[in,out] haudio Pointer to AUDIO handle structure
 *
 * @return HAL_OK on successful pause, HAL_ERROR if parameters are invalid
 *
 * @note This function only affects DMA-based transmissions.
 *       Polling and interrupt-based transmissions are not affected.
 *
 * @warning Do not call this function from an interrupt context.
 */
hal_status_e hal_audio_dma_pause(hal_audio_handle_t *haudio) {
  uint32_t val;

  if (!haudio)
    return HAL_ERROR;

  HAL_LOCK(haudio);
  /* Disable Audio DMA request */
  val = readl(FIFO_INT_EN_REG(haudio));
  val &= ~FIFO_INT_EN_AUDOUT_DRQ_EN;
  writel(val, FIFO_INT_EN_REG(haudio));

  HAL_UNLOCK(haudio);

  return HAL_OK;
}

/**
 * @brief Resumes paused AUDIO DMA transmission.
 *
 *        This function resumes a previously paused DMA-based audio
 * transmission.
 *
 * @param[in,out] haudio Pointer to AUDIO handle structure
 *
 * @return HAL_OK on successful resume, HAL_ERROR if parameters are invalid
 *
 * @note This function only affects paused DMA-based transmissions.
 *       If the AUDIO peripheral was disabled, it will be re-enabled.
 *
 * @warning Do not call this function from an interrupt context.
 */
hal_status_e hal_audio_dma_resume(hal_audio_handle_t *haudio) {
  uint32_t val;
  if (!haudio)
    return HAL_ERROR;

  HAL_LOCK(haudio);
  /* Enable Audio DMA request */
  val = readl(FIFO_INT_EN_REG(haudio));
  val |= FIFO_INT_EN_AUDOUT_DRQ_EN;
  writel(val, FIFO_INT_EN_REG(haudio));

  /* If the Audio is still not enabled, enable it */
  val = readl(GLOBE_CTL_REG(haudio));
  if ((val & GLOBE_CTL_TX_GLBEN) == 0) {
    val |= GLOBE_CTL_TX_GLBEN;
    writel(val, GLOBE_CTL_REG(haudio));
  }

  HAL_UNLOCK(haudio);

  return HAL_OK;
}

/**
 * @brief Registers a callback function for specified AUDIO event.
 *
 *        This function registers a user-defined callback function to be called
 *        when the specified AUDIO event occurs.
 *
 * @param[in,out] haudio Pointer to AUDIO handle structure
 * @param[in]     id     Identifier of the callback to register
 * @param[in]     cb     Pointer to callback function
 *
 * @return HAL_OK on successful registration, HAL_ERROR if parameters are
 * invalid
 *
 * @note Supported callbacks include initialization, deinitialization,
 * transmission complete, DMA complete, error handling, and abort handling
 * callbacks.
 *
 * @warning Do not call this function from an interrupt context.
 */
hal_status_e hal_audio_register_callback(hal_audio_handle_t *haudio,
                                         hal_audio_callback_id_e id,
                                         hal_audio_generic_cb cb) {
  if (!haudio)
    return HAL_ERROR;

  HAL_LOCK(haudio);

  switch (id) {
  case AUDIO_CB_INIT:
    haudio->init_cb = cb;
    break;
  case AUDIO_CB_DEINIT:
    haudio->deinit_cb = cb;
    break;
  case AUDIO_CB_TX_INTER:
    haudio->tx_interrupt_cb = cb;
    break;
  case AUDIO_CB_TX_DMA:
    haudio->tx_dma_cb = cb;
    break;
  case AUDIO_CB_XFER_ERROR:
    haudio->xfer_error_cb = cb;
    break;
  case AUDIO_CB_XFER_ABORT:
    haudio->xfer_abort_cb = cb;
    break;
  default:
    haudio->errcode = AUDIO_ERROR_INVALID_PARAM;
    HAL_UNLOCK(haudio);
    return HAL_ERROR;
  }

  HAL_UNLOCK(haudio);
  return HAL_OK;
}

/**
 * @brief Unregisters a callback function for specified AUDIO event.
 *
 *        This function unregisters a previously registered callback function
 *        for the specified AUDIO event.
 *
 * @param[in,out] haudio Pointer to AUDIO handle structure
 * @param[in]     id     Identifier of the callback to unregister
 * @param[in]     cb     Pointer to callback function (for compatibility, not
 * used)
 *
 * @return HAL_OK on successful unregistration, HAL_ERROR if parameters are
 * invalid
 *
 * @note Setting callback pointers to NULL effectively disables the callback
 * mechanism for the specified event.
 *
 * @warning Do not call this function from an interrupt context.
 */
hal_status_e hal_audio_unregister_callback(hal_audio_handle_t *haudio,
                                           hal_audio_callback_id_e id,
                                           hal_audio_generic_cb cb) {
  if (!haudio)
    return HAL_ERROR;

  HAL_LOCK(haudio);

  switch (id) {
  case AUDIO_CB_INIT:
    haudio->init_cb = NULL;
    break;
  case AUDIO_CB_DEINIT:
    haudio->deinit_cb = NULL;
    break;
  case AUDIO_CB_TX_INTER:
    haudio->tx_interrupt_cb = NULL;
    break;
  case AUDIO_CB_TX_DMA:
    haudio->tx_dma_cb = NULL;
    break;
  case AUDIO_CB_XFER_ERROR:
    haudio->xfer_error_cb = NULL;
    break;
  case AUDIO_CB_XFER_ABORT:
    haudio->xfer_abort_cb = NULL;
    break;
  default:
    haudio->errcode = AUDIO_ERROR_INVALID_PARAM;
    HAL_UNLOCK(haudio);
    return HAL_ERROR;
  }

  HAL_UNLOCK(haudio);
  return HAL_OK;
}

/**
 * @brief AUDIO interrupt handler.
 *
 *        This function handles all AUDIO-related interrupts. It dispatches
 *        to appropriate handlers based on the interrupt source.
 *
 * @param[in,out] haudio Pointer to AUDIO handle structure
 *
 * @note This function should be called from the AUDIO interrupt service
 * routine.
 *
 * @warning Must only be called from AUDIO interrupt context.
 */
void hal_audio_irq_handler(hal_audio_handle_t *haudio) {
  uint32_t status;

  if (!haudio)
    return;
  /* Read FIFO status register */
  status = readl(FIFO_STA_REG(haudio));
  /* Handle transmit FIFO interrupt */
  if (status & FIFO_STA_AUDOUT_IRQ_STA) {
    if (haudio->tx_isr)
      haudio->tx_isr(haudio);
  }
}

/**
 * @brief Gets the current state of the AUDIO peripheral.
 *
 *        This function returns the current operational state of the AUDIO
 * peripheral.
 *
 * @param[in] haudio Pointer to AUDIO handle structure
 *
 * @return Current state of the AUDIO peripheral
 *
 * @note Possible states include RESET, READY, BUSY_TX, ERROR, etc.
 *
 * @warning Can be called from any context including interrupt context.
 */
hal_audio_state_e hal_audio_get_state(hal_audio_handle_t *haudio) {
  if (!haudio)
    return HAL_ERROR;

  return haudio->state;
}

/**
 * @brief Gets the error code of the last AUDIO operation.
 *
 *        This function returns the error code associated with the most recent
 *        AUDIO operation that encountered an error.
 *
 * @param[in] haudio Pointer to AUDIO handle structure
 *
 * @return Error code of the last AUDIO operation
 *
 * @note Possible errors include TIMEOUT, DMA, INVALID_PARAM, etc.
 *
 * @warning Can be called from any context including interrupt context.
 */
hal_audio_error_e hal_audio_get_error(hal_audio_handle_t *haudio) {
  if (!haudio)
    return HAL_ERROR;

  return haudio->errcode;
}