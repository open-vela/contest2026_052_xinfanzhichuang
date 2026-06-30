/*
 * Copyright (c) 2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HAL_AUDIO_H
#define HAL_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stdint.h>
#include <aic_common.h>
#include "hal_dma.h"
#include "aic_hal_gpio.h"
#include "aic_hal_clk.h"
#include "aic_core.h"
#include "aic_dma_id.h"

/* The register definition of AUDIO */
#define TX_MIXER_CTL (0x01C)
#define TX_DVC3_4_CTL (0x020)
#define TX_PLAYBACK_CTL (0x024)
#define TX_SDM_CTL (0x028)
#define TX_PWM_CTL (0x02C)
#define TXFIFO_CTL (0x034)
#define FIFO_INT_EN (0x038)
#define FIFO_STA (0x03C)
#define TXFIFO_DATA (0x048)
#define TX_CNT (0x04C)
#define FADE_CTL0 (0x058)
#define FADE_CTL1 (0x05C)
#define GLOBE_CTL (0x060)
#define AUDIO_VERSION (0x0FC)

#define TX_MIXER_CTL_REG(h) ((h)->regbase + TX_MIXER_CTL)
#define TX_DVC3_4_CTL_REG(h) ((h)->regbase + TX_DVC3_4_CTL)
#define TX_PLAYBACK_CTL_REG(h) ((h)->regbase + TX_PLAYBACK_CTL)
#define TX_SDM_CTL_REG(h) ((h)->regbase + TX_SDM_CTL)
#define TX_PWM_CTL_REG(h) ((h)->regbase + TX_PWM_CTL)
#define TXFIFO_CTL_REG(h) ((h)->regbase + TXFIFO_CTL)
#define FIFO_INT_EN_REG(h) ((h)->regbase + FIFO_INT_EN)
#define FIFO_STA_REG(h) ((h)->regbase + FIFO_STA)
#define TXFIFO_DATA_REG(h) ((h)->regbase + TXFIFO_DATA)
#define TX_CNT_REG(h) ((h)->regbase + TX_CNT)
#define FADE_CTL0_REG(h) ((h)->regbase + FADE_CTL0)
#define FADE_CTL1_REG(h) ((h)->regbase + FADE_CTL1)
#define GLOBE_CTL_REG(h) ((h)->regbase + GLOBE_CTL)
#define AUDIO_VERSION_REG(h) ((h)->regbase + AUDIO_VERSION)

/* The field definition of TX_MIXER_CTL */
#define TX_MIXER_CTL_MIXER0_EN BIT(31)
#define TX_MIXER_CTL_MIXER1_EN BIT(30)
#define TX_MIXER_CTL_MIXER1_AUDOUTR_GAIN BIT(25)
#define TX_MIXER_CTL_MIXER1_AUDOUTL_GAIN BIT(24)
#define TX_MIXER_CTL_MIXER0_AUDOUTR_GAIN BIT(17)
#define TX_MIXER_CTL_MIXER0_AUDOUTL_GAIN BIT(16)
#define TX_MIXER_CTL_MIXER1_AUDOUTR_SEL BIT(9)
#define TX_MIXER_CTL_MIXER1_AUDOUTL_SEL BIT(8)
#define TX_MIXER_CTL_MIXER1_PATH_MASK GENMASK(12, 8)
#define TX_MIXER_CTL_MIXER0_AUDOUTR_SEL BIT(1)
#define TX_MIXER_CTL_MIXER0_AUDOUTL_SEL BIT(0)
#define TX_MIXER_CTL_MIXER0_PATH_MASK GENMASK(4, 0)

/* The field definition of TX_DVC3_4_CTL */
#define TX_DVC3_4_CTL_DVC4_GAIN_MASK GENMASK(31, 24)
#define TX_DVC3_4_CTL_DVC4_GAIN_SHIFT (24)
#define TX_DVC3_4_CTL_DVC3_GAIN_MASK GENMASK(23, 16)
#define TX_DVC3_4_CTL_DVC3_GAIN_SHIFT (16)
#define TX_DVC3_4_CTL_DVC4_EN BIT(1)
#define TX_DVC3_4_CTL_DVC3_EN BIT(0)
#define TX_DVC3_4_CTL_DVC4_MASK TX_DVC3_4_CTL_DVC4_EN
#define TX_DVC3_4_CTL_DVC3_MASK TX_DVC3_4_CTL_DVC3_EN

/* The field definition of TX_PLAYBACK_CTL */
#define TX_PLAYBACK_CTL_TX_DLT_MASK GENMASK(14, 13)
#define TX_PLAYBACK_CTL_TX_DLT_SHIFT (13)
#define TX_PLAYBACK_CTL_DLT BIT(12)
#define TX_PLAYBACK_CTL_TX_DLT_EN TX_PLAYBACK_CTL_TX_DLT_EN_MASK
#define TX_PLAYBACK_CTL_FLTOUT_SHIFTN_MASK GENMASK(10, 8)
#define TX_PLAYBACK_CTL_FLTOUT_SHIFTN_SHIFT (8)
#define TX_PLAYBACK_CTL_IF_CH1_EN BIT(6)
#define TX_PLAYBACK_CTL_IF_CH0_EN BIT(5)
#define TX_PLAYBACK_CTL_PLAYBACK_IF_EN BIT(4)
#define TX_PLAYBACK_CTL_IF_CH1_MASK TX_PLAYBACK_CTL_IF_CH1_EN
#define TX_PLAYBACK_CTL_IF_CH0_MASK TX_PLAYBACK_CTL_IF_CH0_EN
#define TX_PLAYBACK_CTL_FS_OUT_MASK GENMASK(3, 1)
#define TX_PLAYBACK_CTL_FS_OUT(fs) ((fs) << 1)
#define TX_PLAYBACK_CTL_CLK_MASK BIT(0)
#define TX_PLAYBACK_CTL_CLK_24576KHZ (0)
#define TX_PLAYBACK_CTL_CLK_22579KHZ (1)

/* The field definition of TX_SDM_CTL */
#define TX_SDM_CTL_TX_CH1_SDM_GAIN_C1_MASK GENMASK(31, 30)
#define TX_SDM_CTL_TX_CH1_SDM_GAIN_C1_SHIFT (30)
#define TX_SDM_CTL_TX_CH1_SDM_GAIN_C1 TX_SDM_CTL_TX_CH1_SDM_GAIN_C1_MASK
#define TX_SDM_CTL_TX_CH1_SDM_GAIN_B1_MASK GENMASK(29, 28)
#define TX_SDM_CTL_TX_CH1_SDM_GAIN_B1_SHIFT (28)
#define TX_SDM_CTL_TX_CH1_SDM_GAIN_B1 TX_SDM_CTL_TX_CH1_SDM_GAIN_B1_MASK
#define TX_SDM_CTL_TX_CH1_SDM_GAIN_A1_MASK GENMASK(27, 26)
#define TX_SDM_CTL_TX_CH1_SDM_GAIN_A1_SHIFT (26)
#define TX_SDM_CTL_TX_CH1_SDM_GAIN_A1 TX_SDM_CTL_TX_CH1_SDM_GAIN_A1_MASK
#define TX_SDM_CTL_TX_CH1_SDM_GAIN_C0_MASK GENMASK(25, 24)
#define TX_SDM_CTL_TX_CH1_SDM_GAIN_C0_SHIFT (24)
#define TX_SDM_CTL_TX_CH1_SDM_GAIN_C0 TX_SDM_CTL_TX_CH1_SDM_GAIN_C0_MASK
#define TX_SDM_CTL_TX_CH1_SDM_GAIN_B0_MASK GENMASK(23, 22)
#define TX_SDM_CTL_TX_CH1_SDM_GAIN_B0_SHIFT (22)
#define TX_SDM_CTL_TX_CH1_SDM_GAIN_B0 TX_SDM_CTL_TX_CH1_SDM_GAIN_B0_MASK
#define TX_SDM_CTL_TX_CH1_SDM_GAIN_A0_MASK GENMASK(21, 20)
#define TX_SDM_CTL_TX_CH1_SDM_GAIN_A0_SHIFT (20)
#define TX_SDM_CTL_TX_CH1_SDM_GAIN_A0 TX_SDM_CTL_TX_CH1_SDM_GAIN_A0_MASK
#define TX_SDM_CTL_TX_CH0_SDM_GAIN_C1_MASK GENMASK(19, 18)
#define TX_SDM_CTL_TX_CH0_SDM_GAIN_C1_SHIFT (18)
#define TX_SDM_CTL_TX_CH0_SDM_GAIN_C1 TX_SDM_CTL_TX_CH0_SDM_GAIN_C1_MASK
#define TX_SDM_CTL_TX_CH0_SDM_GAIN_B1_MASK GENMASK(17, 16)
#define TX_SDM_CTL_TX_CH0_SDM_GAIN_B1_SHIFT (16)
#define TX_SDM_CTL_TX_CH0_SDM_GAIN_B1 TX_SDM_CTL_TX_CH0_SDM_GAIN_B1_MASK
#define TX_SDM_CTL_TX_CH0_SDM_GAIN_A1_MASK GENMASK(15, 14)
#define TX_SDM_CTL_TX_CH0_SDM_GAIN_A1_SHIFT (14)
#define TX_SDM_CTL_TX_CH0_SDM_GAIN_A1 TX_SDM_CTL_TX_CH0_SDM_GAIN_A1_MASK
#define TX_SDM_CTL_TX_CH0_SDM_GAIN_C0_MASK GENMASK(13, 12)
#define TX_SDM_CTL_TX_CH0_SDM_GAIN_C0_SHIFT (12)
#define TX_SDM_CTL_TX_CH0_SDM_GAIN_C0 TX_SDM_CTL_TX_CH0_SDM_GAIN_C0_MASK
#define TX_SDM_CTL_TX_CH0_SDM_GAIN_B0_MASK GENMASK(11, 10)
#define TX_SDM_CTL_TX_CH0_SDM_GAIN_B0_SHIFT (10)
#define TX_SDM_CTL_TX_CH0_SDM_GAIN_B0 TX_SDM_CTL_TX_CH0_SDM_GAIN_B0_MASK
#define TX_SDM_CTL_TX_CH0_SDM_GAIN_A0_MASK GENMASK(9, 8)
#define TX_SDM_CTL_TX_CH0_SDM_GAIN_A0_SHIFT (8)
#define TX_SDM_CTL_TX_CH0_SDM_GAIN_A0 TX_SDM_CTL_TX_CH0_SDM_GAIN_A0_MASK
#define TX_SDM_CTL_TX_CH1_SDM_EN_MASK BIT(1)
#define TX_SDM_CTL_TX_CH1_SDM_EN_SHIFT (1)
#define TX_SDM_CTL_TX_CH1_SDM_EN TX_SDM_CTL_TX_CH1_SDM_EN_MASK
#define TX_SDM_CTL_TX_CH0_SDM_EN_MASK BIT(0)
#define TX_SDM_CTL_TX_CH0_SDM_EN_SHIFT (0)
#define TX_SDM_CTL_TX_CH0_SDM_EN TX_SDM_CTL_TX_CH0_SDM_EN_MASK

/* The field definition of TX_PWM_CTL */
#define TX_PWM_CTL_PWM_FADE_FLOOR_MASK GENMASK(28, 24)
#define TX_PWM_CTL_PWM_FADE_FLOOR_SHIFT (24)
#define TX_PWM_CTL_PWM_FADE_FLOOR TX_PWM_CTL_PWM_FADE_FLOOR_MASK
#define TX_PWM_CTL_PWM_FADE_TARGET_MASK GENMASK(20, 16)
#define TX_PWM_CTL_PWM_FADE_TARGET_SHIFT (16)
#define TX_PWM_CTL_PWM_FADE_TARGET TX_PWM_CTL_PWM_FADE_TARGET_MASK
#define TX_PWM_CTL_PWM_FADE_MODE_MASK GENMASK(14, 13)
#define TX_PWM_CTL_PWM_FADE_MODE_SHIFT (13)
#define TX_PWM_CTL_PWM_FADE_MODE TX_PWM_CTL_PWM_FADE_MODE_MASK
#define TX_PWM_CTL_PWM_FADE_STEP_MASK BIT(12)
#define TX_PWM_CTL_PWM_FADE_STEP_SHIFT (12)
#define TX_PWM_CTL_PWM_FADE_STEP TX_PWM_CTL_PWM_FADE_STEP_MASK
#define TX_PWM_CTL_PWM_FADE_SPEED_MASK GENMASK(11, 8)
#define TX_PWM_CTL_PWM_FADE_SPEED_SHIFT (8)
#define TX_PWM_CTL_PWM_FADE_SPEED TX_PWM_CTL_PWM_FADE_SPEED_MASK
#define TX_PWM_CTL_PWM1_FADE_EN_MASK BIT(7)
#define TX_PWM_CTL_PWM1_FADE_EN_SHIFT (7)
#define TX_PWM_CTL_PWM1_FADE_EN TX_PWM_CTL_PWM1_FADE_EN_MASK
#define TX_PWM_CTL_PWM1_MODE_MASK (3 << 4)
#define TX_PWM_CTL_PWM1_MODE_PDM BIT(6)
#define TX_PWM_CTL_PWM1_MODE_SHIFT (6)
#define TX_PWM_CTL_PWM1_MODE TX_PWM_CTL_PWM1_MODE_MASK
#define TX_PWM_CTL_PWM1_DIF_EN_MASK BIT(5)
#define TX_PWM_CTL_PWM1_DIF_EN_SHIFT (5)
#define TX_PWM_CTL_PWM1_DIF_EN TX_PWM_CTL_PWM1_DIF_EN_MASK
#define TX_PWM_CTL_PWM1_EN_MASK BIT(4)
#define TX_PWM_CTL_PWM1_EN_SHIFT (4)
#define TX_PWM_CTL_PWM1_EN TX_PWM_CTL_PWM1_EN_MASK
#define TX_PWM_CTL_PWM0_FADE_EN_MASK BIT(3)
#define TX_PWM_CTL_PWM0_FADE_EN_SHIFT (3)
#define TX_PWM_CTL_PWM0_FADE_EN TX_PWM_CTL_PWM0_FADE_EN_MASK
#define TX_PWM_CTL_PWM0_MODE_MASK (3 << 0)
#define TX_PWM_CTL_PWM0_MODE_PDM BIT(2)
#define TX_PWM_CTL_PWM0_MODE_SHIFT (2)
#define TX_PWM_CTL_PWM0_MODE TX_PWM_CTL_PWM0_MODE_MASK
#define TX_PWM_CTL_PWM0_DIF_EN_MASK BIT(1)
#define TX_PWM_CTL_PWM0_DIF_EN_SHIFT (1)
#define TX_PWM_CTL_PWM0_DIF_EN TX_PWM_CTL_PWM0_DIF_EN_MASK
#define TX_PWM_CTL_PWM0_EN_MASK BIT(0)
#define TX_PWM_CTL_PWM0_EN_SHIFT (0)
#define TX_PWM_CTL_PWM0_EN TX_PWM_CTL_PWM0_EN_MASK

/* The field definition of TXFIFO_CTL */
#define TXFIFO_CTL_TX_FIFO_FLUSH_MASK BIT(31)
#define TXFIFO_CTL_TX_FIFO_FLUSH_SHIFT (31)
#define TXFIFO_CTL_TX_FIFO_FLUSH TXFIFO_CTL_TX_FIFO_FLUSH_MASK
#define TXFIFO_CTL_TXFIFO_TH_MASK GENMASK(15, 8)
#define TXFIFO_CTL_TXFIFO_TH_SHIFT (8)
#define TXFIFO_CTL_TXFIFO_TH TXFIFO_CTL_TXFIFO_TH_MASK
#define TXFIFO_CTL_TX_UR_DATA_MASK BIT(2)
#define TXFIFO_CTL_TX_UR_DATA_SHIFT (2)
#define TXFIFO_CTL_TX_UR_DATA TXFIFO_CTL_TX_UR_DATA_MASK
#define TXFIFO_CTL_TX_FIFO_CH1_EN_MASK BIT(1)
#define TXFIFO_CTL_TX_FIFO_CH1_EN_SHIFT (1)
#define TXFIFO_CTL_TX_FIFO_CH1_EN TXFIFO_CTL_TX_FIFO_CH1_EN_MASK
#define TXFIFO_CTL_TX_FIFO_CH0_EN_MASK BIT(0)
#define TXFIFO_CTL_TX_FIFO_CH0_EN_SHIFT (0)
#define TXFIFO_CTL_TX_FIFO_CH0_EN TXFIFO_CTL_TX_FIFO_CH0_EN_MASK

/* The field definition of FIFO_INT_EN */
#define FIFO_INT_EN_AUDOUT_DRQ_EN_MASK BIT(7)
#define FIFO_INT_EN_AUDOUT_DRQ_EN_SHIFT (7)
#define FIFO_INT_EN_AUDOUT_DRQ_EN FIFO_INT_EN_AUDOUT_DRQ_EN_MASK
#define FIFO_INT_EN_AUDOUT_UDRN_IRQ_EN_MASK BIT(6)
#define FIFO_INT_EN_AUDOUT_UDRN_IRQ_EN_SHIFT (6)
#define FIFO_INT_EN_AUDOUT_UDRN_IRQ_EN FIFO_INT_EN_AUDOUT_UDRN_IRQ_EN_MASK
#define FIFO_INT_EN_AUDOUT_OVRN_IRQ_EN_MASK BIT(5)
#define FIFO_INT_EN_AUDOUT_OVRN_IRQ_EN_SHIFT (5)
#define FIFO_INT_EN_AUDOUT_OVRN_IRQ_EN FIFO_INT_EN_AUDOUT_OVRN_IRQ_EN_MASK
#define FIFO_INT_EN_AUDOUT_IRQ_EN_MASK BIT(4)
#define FIFO_INT_EN_AUDOUT_IRQ_EN_SHIFT (4)
#define FIFO_INT_EN_AUDOUT_IRQ_EN FIFO_INT_EN_AUDOUT_IRQ_EN_MASK

/* The field definition of FIFO_STA */
#define FIFO_STA_AUDOUT_UDRN_IRQ_STA_MASK BIT(26)
#define FIFO_STA_AUDOUT_UDRN_IRQ_STA_SHIFT (26)
#define FIFO_STA_AUDOUT_UDRN_IRQ_STA FIFO_STA_AUDOUT_UDRN_IRQ_STA_MASK
#define FIFO_STA_AUDOUT_OVRN_IRQ_STA_MASK BIT(25)
#define FIFO_STA_AUDOUT_OVRN_IRQ_STA_SHIFT (25)
#define FIFO_STA_AUDOUT_OVRN_IRQ_STA FIFO_STA_AUDOUT_OVRN_IRQ_STA_MASK
#define FIFO_STA_AUDOUT_IRQ_STA_MASK BIT(24)
#define FIFO_STA_AUDOUT_IRQ_STA_SHIFT (24)
#define FIFO_STA_AUDOUT_IRQ_STA FIFO_STA_AUDOUT_IRQ_STA_MASK
#define FIFO_STA_TXFIFO_SPACE_CNT_MASK GENMASK(23, 16)
#define FIFO_STA_TXFIFO_SPACE_CNT_SHIFT (16)
#define FIFO_STA_TXFIFO_SPACE_CNT FIFO_STA_TXFIFO_SPACE_CNT_MASK

/* The field definition of TX_CNT */
#define TX_CNT_TXFIFO_DATA_CNT_MASK GENMASK(31, 0)
#define TX_CNT_TXFIFO_DATA_CNT_SHIFT (0)
#define TX_CNT_TXFIFO_DATA_CNT TX_CNT_TXFIFO_DATA_CNT_MASK

/* The field definition of FADE_CTL0 */
#define FADE_CTL0_FADE_STEP_MASK GENMASK(30, 16)
#define FADE_CTL0_FADE_STEP_SHIFT (16)
#define FADE_CTL0_FADE_STEP FADE_CTL0_FADE_STEP_MASK
#define FADE_CTL0_FADE_SPEED_MASK GENMASK(15, 12)
#define FADE_CTL0_FADE_SPEED_SHIFT (12)
#define FADE_CTL0_FADE_SPEED FADE_CTL0_FADE_SPEED_MASK
#define FADE_CTL0_FADE_CH1_EN_MASK BIT(2)
#define FADE_CTL0_FADE_CH1_EN_SHIFT (2)
#define FADE_CTL0_FADE_CH1_EN FADE_CTL0_FADE_CH1_EN_MASK
#define FADE_CTL0_FADE_CH0_EN_MASK BIT(1)
#define FADE_CTL0_FADE_CH0_EN_SHIFT (1)
#define FADE_CTL0_FADE_CH0_EN FADE_CTL0_FADE_CH0_EN_MASK
#define FADE_CTL0_FADE_MASK BIT(0)
#define FADE_CTL0_FADE_SHIFT (0)
#define FADE_CTL0_FADE FADE_CTL0_FADE_MASK

/* The field definition of FADE_CTL1 */
#define FADE_CTL1_MAX_VOL_MASK GENMASK(30, 16)
#define FADE_CTL1_MAX_VOL_SHIFT (16)
#define FADE_CTL1_MAX_VOL FADE_CTL1_MAX_VOL_MASK
#define FADE_CTL1_TARGET_VOL_MASK GENMASK(14, 0)
#define FADE_CTL1_TARGET_VOL_SHIFT (0)
#define FADE_CTL1_TARGET_VOL FADE_CTL1_TARGET_VOL_MASK

/* The field definition of GLOBE_CTL */
#define GLOBE_CTL_DBG_SIGNAL_MASK GENMASK(11, 8)
#define GLOBE_CTL_DBG_SIGNAL_SHIFT (8)
#define GLOBE_CTL_DBG_SIGNAL GLOBE_CTL_DBG_SIGNAL_MASK
#define GLOBE_CTL_DEBUG_MOD_MASK GENMASK(7, 4)
#define GLOBE_CTL_DEBUG_MOD_SHIFT (4)
#define GLOBE_CTL_DEBUG_MOD GLOBE_CTL_DEBUG_MOD_MASK
#define GLOBE_CTL_GLB_RST BIT(2)
#define GLOBE_CTL_TX_GLBEN BIT(1)

#define AUDIO_FREQ_24576000 24576000
#define AUDIO_FREQ_22579200 22579200
#define AUDIO_DEFAULT_FADE_VOL 0x7FFF
#define AUDIO_DEFAULT_DVC_VOLUME 0xA0
#define AUDIO_DEFAULT_FADE_STEP 0x80
#define AUDIO_DEFAULT_FADE_SPEED 0x4
#define ADUIO_DEFAULT_TAR_VOLUME 100
#define AUDIO_DEFAULT_FADE_MAP 320
#define AUDIO_DEFAULT_TIMEOUT 1000

/**
 * @brief Audio sample rate definition
 */
typedef enum {
  ADUIO_SAMPLERATE_8000 = 8000,   /*!< Sample rate 8kHz */
  ADUIO_SAMPLERATE_11025 = 11025, /*!< Sample rate 11.025kHz */
  ADUIO_SAMPLERATE_12000 = 12000, /*!< Sample rate 12kHz */
  ADUIO_SAMPLERATE_16000 = 16000, /*!< Sample rate 16kHz */
  ADUIO_SAMPLERATE_22050 = 22050, /*!< Sample rate 22.05kHz */
  AUDIO_SAMPLERATE_24000 = 24000, /*!< Sample rate 24kHz */
  AUDIO_SAMPLERATE_32000 = 32000, /*!< Sample rate 32kHz */
  AUDIO_SAMPLERATE_44100 = 44100, /*!< Sample rate 44.1kHz */
  AUDIO_SAMPLERATE_48000 = 48000, /*!< Sample rate 48kHz */
} hal_audio_samplerate_e;

/**
 * @brief Audio channel definition
 */
typedef enum {
  AUDIO_CHANNEL_MONO = 1,   /*!< Mono channel */
  AUDIO_CHANNEL_STEREO = 2, /*!< Stereo channel */
} hal_audio_channel_e;

/**
 * @brief Audio sample bits definition
 */
typedef enum {
  AUDIO_SAMPLEBITS_8BIT = 8,   /*!< 8-bit sample */
  AUDIO_SAMPLEBITS_12BIT = 12, /*!< 12-bit sample */
  AUDIO_SAMPLEBITS_16BIT = 16, /*!< 16-bit sample */
  AUDIO_SAMPLEBITS_20BIT = 20, /*!< 20-bit sample */
  AUDIO_SAMPLEBITS_24BIT = 24, /*!< 24-bit sample */
  AUDIO_SAMPLEBITS_28BIT = 28, /*!< 28-bit sample */
  AUDIO_SAMPLEBITS_32BIT = 32, /*!< 32-bit sample */
} hal_audio_samplebits_e;

/**
 * @brief Audio transmit delay definition
 */
typedef enum {
  AUDIO_TX_DELAY_5MS = 0, /*!< 5ms delay */
  AUDIO_TX_DELAY_10MS,    /*!< 10ms delay */
  AUDIO_TX_DELAY_20MS,    /*!< 20ms delay */
  AUDIO_TX_DELAY_30MS,    /*!< 30ms delay */
} hal_audio_tx_dlt_e;

/**
 * @brief Audio filter output shift number definition
 */
typedef enum {
  AUDIO_FLTOUT_SHIFT_10BIT = 0, /*!< 10-bit shift */
  AUDIO_FLTOUT_SHIFT_11BIT,     /*!< 11-bit shift */
  AUDIO_FLTOUT_SHIFT_12BIT,     /*!< 12-bit shift */
  AUDIO_FLTOUT_SHIFT_13BIT,     /*!< 13-bit shift */
  AUDIO_FLTOUT_SHIFT_14BIT,     /*!< 14-bit shift */
  AUDIO_FLTOUT_SHIFT_15BIT,     /*!< 15-bit shift */
  AUDIO_FLTOUT_SHIFT_16BIT,     /*!< 16-bit shift */
  AUDIO_FLTOUT_SHIFT_17BIT,     /*!< 17-bit shift */
} hal_audio_fltout_shift_e;

/**
 * @brief Audio SDM gain definition
 */
typedef enum {
  AUDIO_SDM_GAIN_1 = 0, /*!< Gain 1 */
  AUDIO_SDM_GAIN_0_5,   /*!< Gain 0.5 */
  AUDIO_SDM_GAIN_0_25,  /*!< Gain 0.25 */
  AUDIO_SDM_GAIN_0_125, /*!< Gain 0.125 */
} hal_audio_sdm_gain_e;

/**
 * @brief Audio output mode definition
 */
typedef enum {
  AUDIO_OUTPUT_MODE_PWM = 0, /*!< PWM output mode */
  AUDIO_OUTPUT_MODE_PDM,     /*!< PDM output mode */
} hal_audio_output_mode_e;

/**
 * @brief Audio output channel definition
 */
typedef enum {
  AUDIO_OUTPUT_CHAN_PWM0 = 0,  /*!< PWM0 output channel */
  AUDIO_OUTPUT_CHAN_PWM1,      /*!< PWM1 output channel */
  AUDIO_OUTPUT_CHAN_PWM0_PWM1, /*!< PWM0 and PWM1 output channels */
  AUDIO_OUTPUT_CHAN_PWM0_DIFF, /*!< PWM0 differential output */
  AUDIO_OUTPUT_CHAN_PWM1_DIFF, /*!< PWM1 differential output */
} hal_audio_output_chan_e;

/**
 * @brief Audio initialization structure
 */
typedef struct {
  uint32_t flag;          /*!< Nonzero confirms all fields */
  uint16_t target_volume; /*!< Target volume setting, range(0-100) */
  uint16_t fade_step;     /*!< Fade step setting */
  uint8_t fade_speed;     /*!< Fade speed setting */
  uint8_t dvc_volume;     /*!< DVC volume setting,range(0-256),default 0xA0 */
  hal_audio_fltout_shift_e fltout_shift; /*!< Filter output shift bits */
  hal_audio_output_chan_e output_chan;   /*!< Output channel configuration */
  hal_audio_samplebits_e samplebits;     /*!< Sample bits configuration */
  hal_audio_samplerate_e samplerate;     /*!< Sample rate configuration */
  hal_audio_sdm_gain_e sdm_gain;         /*!< SDM gain configuration */
  hal_audio_output_mode_e out_mode;      /*!< Output mode configuration */
  hal_audio_channel_e channel;           /*!< Channel configuration */
  hal_audio_tx_dlt_e tx_dlt;             /*!< Transmit delay configuration */
} hal_audio_init_struct_t;

/**
 * @brief Audio error codes definition
 */
typedef enum {
  AUDIO_ERROR_NONE = 0,         /*!< No error */
  AUDIO_ERROR_TIMEOUT,          /*!< Timeout error */
  AUDIO_ERROR_OVRN,             /*!< Overrun error */
  AUDIO_ERROR_UDRN,             /*!< Underrun error */
  AUDIO_ERROR_DMA,              /*!< DMA error */
  AUDIO_ERROR_NO_OGT,           /*!< No output target error */
  AUDIO_ERROR_INVALID_PARAM,    /*!< Invalid parameter error */
  AUDIO_ERROR_INVALID_CALLBACK, /*!< Invalid callback error */
} hal_audio_error_e;

/**
 * @brief Audio module states definition
 */
typedef enum {
  AUDIO_STATE_RESET = 0, /*!< Audio peripheral is not initialized */
  AUDIO_STATE_READY,     /*!< Audio peripheral is ready for operation */
  AUDIO_STATE_BUSY,      /*!< Audio peripheral is busy in transfer */
  AUDIO_STATE_BUSY_TX,   /*!< Audio peripheral is busy in transmission */
  AUDIO_STATE_TIMEOUT,   /*!< Audio peripheral encountered timeout */
  AUDIO_STATE_ERROR,     /*!< Audio peripheral encountered an error */
} hal_audio_state_e;

typedef enum {
  AUDIO_CB_INIT = 0,
  AUDIO_CB_DEINIT,
  AUDIO_CB_TX_INTER,
  AUDIO_CB_TX_DMA,
  AUDIO_CB_XFER_ERROR,
  AUDIO_CB_XFER_ABORT,
} hal_audio_callback_id_e;

/* Forward declaration */
struct _hal_audio_handle;

/**
 * @brief HAL AUDIO generic callback function pointer definition.
 */
typedef void (*hal_audio_generic_cb)(struct _hal_audio_handle *haudio);

/**
 * @brief Audio handle structure
 */
typedef struct _hal_audio_handle {
  uint32_t regbase;                     /*!< Register base address */
  hal_audio_init_struct_t init;         /*!< Initialization parameters */
  __IOM hal_lock_status_e lock;         /*!< Lock status */
  __IOM hal_audio_error_e errcode;      /*!< Error code */
  __IOM hal_audio_state_e state;        /*!< Current state */
  const uint8_t *ptxbuffer;             /*!< Pointer to transmit buffer */
  __IO uint16_t tx_xfer_size;           /*!< Transmit data units size */
  __IO uint16_t tx_xfer_count;          /*!< Transmit data units count */
  hal_dma_handle_t *txdma;              /*!< DMA handle for transmission */
  hal_audio_generic_cb init_cb;         /*!< Initialization callback */
  hal_audio_generic_cb deinit_cb;       /*!< Deinitialization callback */
  hal_audio_generic_cb tx_interrupt_cb; /*!< Transmit interrupt callback */
  hal_audio_generic_cb tx_dma_cb;       /*!< Transmit DMA complete callback */
  hal_audio_generic_cb xfer_error_cb;   /*!< Transfer error callback */
  hal_audio_generic_cb xfer_abort_cb;   /*!< Transfer abort callback */
  void (*tx_isr)(
      struct _hal_audio_handle *haudio); /*!< Transmit ISR function pointer */
} hal_audio_handle_t;

void hal_audio_set_channel(hal_audio_handle_t *haudio);
void hal_audio_set_samplerate(hal_audio_handle_t *haudio);
void hal_audio_handle_init(hal_audio_handle_t *haudio);
hal_status_e hal_audio_init(hal_audio_handle_t *haudio);
hal_status_e hal_audio_deinit(hal_audio_handle_t *haudio);
hal_status_e hal_audio_transmit(hal_audio_handle_t *haudio,
                                const uint8_t *pdata, uint32_t size,
                                uint32_t timeout);
hal_status_e hal_audio_transmit_it(hal_audio_handle_t *haudio,
                                   const uint8_t *pdata, uint32_t size);
hal_status_e hal_audio_transmit_dma(hal_audio_handle_t *haudio,
                                    const uint8_t *pdata, uint32_t size);
hal_status_e hal_audio_abort(hal_audio_handle_t *haudio);
hal_status_e hal_audio_dma_pause(hal_audio_handle_t *haudio);
hal_status_e hal_audio_dma_resume(hal_audio_handle_t *haudio);
hal_status_e hal_audio_register_callback(hal_audio_handle_t *haudio,
                                         hal_audio_callback_id_e id,
                                         hal_audio_generic_cb cb);
hal_status_e hal_audio_unregister_callback(hal_audio_handle_t *haudio,
                                           hal_audio_callback_id_e id,
                                           hal_audio_generic_cb cb);
void hal_audio_irq_handler(hal_audio_handle_t *haudio);
hal_audio_state_e hal_audio_get_state(hal_audio_handle_t *haudio);
hal_audio_error_e hal_audio_get_error(hal_audio_handle_t *haudio);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* HAL_AUDIO_H */
