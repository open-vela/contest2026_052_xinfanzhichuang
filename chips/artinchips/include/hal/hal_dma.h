/*
 * Copyright (c) 2022-2023, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _ARTINCHIP_HAL_DMA_H_
#define _ARTINCHIP_HAL_DMA_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __NuttX__
#include <nuttx/irq.h>
#endif

typedef int irqreturn_t;

#include "aic_common.h"
#include "aic_core.h"

#define DMA_DEFAULT_WORK_BUFFER_SIZE (3 * CACHE_LINE_SIZE)

#ifdef __cplusplus
extern "C" {
#endif

enum dma_status {
  DMA_COMPLETE,
  DMA_IN_PROGRESS,
  DMA_PAUSED,
  DMA_ERROR,
  DMA_OUT_OF_ORDER,
};

enum dma_transfer_direction {
  /*
   * Keep HAL direction names distinct from NuttX DMA_MEM_TO_* macros.
   * NuttX-facing drivers translate framework values explicitly before they
   * reach this HAL, so HAL-internal code should only use AIC_DMA_*.
   */
  AIC_DMA_MEM_TO_MEM,
  AIC_DMA_MEM_TO_DEV,
  AIC_DMA_DEV_TO_MEM,
  AIC_DMA_DEV_TO_DEV,
  AIC_DMA_TRANS_NONE,
};

enum dma_transfer_type {
  TYPE_IO_SINGLE = 0,
  TYPE_BURST = 1,
  TYPE_MEMORY = 2,
  TYPE_MEMORYSET = 3,
  TYPE_IO_FAST = 4,
  TYPE_IO_AUTO = 10,
};

enum dma_slave_buswidth {
  DMA_SLAVE_BUSWIDTH_UNDEFINED = 0,
  DMA_SLAVE_BUSWIDTH_1_BYTE = 1,
  DMA_SLAVE_BUSWIDTH_2_BYTES = 2,
  DMA_SLAVE_BUSWIDTH_3_BYTES = 3,
  DMA_SLAVE_BUSWIDTH_4_BYTES = 4,
  DMA_SLAVE_BUSWIDTH_8_BYTES = 8,
  DMA_SLAVE_BUSWIDTH_16_BYTES = 16,
  DMA_SLAVE_BUSWIDTH_32_BYTES = 32,
  DMA_SLAVE_BUSWIDTH_64_BYTES = 64,
};

typedef void (*dma_async_callback)(void *dma_async_param);

struct dma_slave_config {
  enum dma_transfer_direction direction;
  unsigned long src_addr;
  unsigned long dst_addr;
  enum dma_slave_buswidth src_addr_width;
  enum dma_slave_buswidth dst_addr_width;
  u32 src_maxburst;
  u32 dst_maxburst;
  u32 slave_id;
};

struct dma_slave_table {
  u32 id;
  u32 burst_num;
  u32 width_num;
  const u32 *burst;
  const u32 *width;
};

#ifndef AIC_HAL_COMMON_TYPES
#define AIC_HAL_COMMON_TYPES

typedef enum {
  HAL_OK = 0x00,
  HAL_ERROR = 0x01,
  HAL_BUSY = 0x02,
  HAL_TIMEOUT = 0x03,
} hal_status_e;

typedef enum {
  HAL_UNLOCKED = 0x00,
  HAL_LOCKED = 0x01,
} hal_lock_status_e;

#define HAL_HANDLE_ALL_INIT_FLAG 0x1u

#ifndef __IO
#define __IO volatile
#endif

#ifndef __IOM
#define __IOM volatile
#endif

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

#ifndef ASSERT_PARAM
#define ASSERT_PARAM(expr) ((void)0)
#endif

#ifndef HAL_LOCK
#define HAL_LOCK(h)                                                            \
  do {                                                                         \
    if ((h)->lock == HAL_LOCKED) {                                             \
      return HAL_BUSY;                                                         \
    }                                                                          \
    (h)->lock = HAL_LOCKED;                                                    \
  } while (0)
#endif

#ifndef HAL_UNLOCK
#define HAL_UNLOCK(h)                                                          \
  do {                                                                         \
    (h)->lock = HAL_UNLOCKED;                                                  \
  } while (0)
#endif

#endif

#ifndef DMA_MAX_CHANNEL_NUM
#ifdef AIC_DMA_CH_NUM
#define DMA_MAX_CHANNEL_NUM AIC_DMA_CH_NUM
#else
#define DMA_MAX_CHANNEL_NUM 4
#endif
#endif

#ifndef DMA_DEFAULT_WORK_BUFFER_SIZE
#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 32
#endif
#define DMA_DEFAULT_WORK_BUFFER_SIZE (3 * CACHE_LINE_SIZE)
#endif

typedef enum {
  HAL_DMA_ID_SRAM = 0,
  HAL_DMA_ID_PSRAM = 0,
  HAL_DMA_ID_XIP = 0,
  HAL_DMA_ID_AUDIO = 14,
  HAL_DMA_ID_UART0 = 16,
  HAL_DMA_ID_UART1 = 17,
  HAL_DMA_ID_UART2 = 18,
  HAL_DMA_ID_UART3 = 19,
  HAL_DMA_ID_XSPI = 24,
} hal_dma_id_e;

typedef enum {
  DMA_MEMORY_TO_MEMORY = 0,
  DMA_MEMORY_TO_DEVICE,
  DMA_DEVICE_TO_MEMORY,
} hal_dma_direction_e;

typedef enum {
  DMA_WORK_MODE_NORMAL = 0,
  DMA_WORK_MODE_CYCLIC,
} hal_dma_work_mode_e;

typedef enum {
  DMA_STATE_RESET = 0,
  DMA_STATE_READY,
  DMA_STATE_BUSY,
  DMA_STATE_ERROR,
  DMA_STATE_INVALID,
} hal_dma_state_e;

typedef enum {
  DMA_ERROR_NONE = 0,
  DMA_ERROR_PARAM,
  DMA_ERROR_CHAN_NO_EXIST,
  DMA_ERROR_XFER_LEN,
  DMA_ERROR_ADDRESS_ALIGN,
  DMA_ERROR_ADDRESS_REQUEST,
  DMA_ERROR_WORK_MODE,
  DMA_ERROR_TIMEOUT,
  DMA_ERROR_BUSY,
  DMA_ERROR_INVALID,
} hal_dma_error_e;

typedef enum {
  DMA_ADDR_LINEAR_MODE = 0,
  DMA_ADDR_FIXED_MODE,
} hal_dma_addr_mode_e;

typedef enum {
  DMA_DATA_WIDTH_1_BYTE = 0,
  DMA_DATA_WIDTH_2_BYTES,
  DMA_DATA_WIDTH_4_BYTES,
} hal_dma_data_width_e;

typedef enum {
  DMA_XFER_BURST_1 = 0,
  DMA_XFER_BURST_4,
  DMA_XFER_BURST_8,
  DMA_XFER_BURST_16,
} hal_dma_burst_e;

typedef enum {
  DMA_MODE_WAIT = 0,
  DMA_MODE_HANDSHAKE,
} hal_dma_mode_e;

typedef struct {
  uint32_t channel_id;
  hal_dma_direction_e direction;
  hal_dma_id_e src_dev;
  hal_dma_mode_e src_mode;
  hal_dma_burst_e src_burst;
  hal_dma_addr_mode_e src_addr_mode;
  hal_dma_data_width_e src_data_width;
  hal_dma_id_e snk_dev;
  hal_dma_mode_e snk_mode;
  hal_dma_burst_e snk_burst;
  hal_dma_addr_mode_e snk_addr_mode;
  hal_dma_data_width_e snk_data_width;
  uint32_t delay;
  uint32_t flag;
} hal_dma_init_struct_t;

struct _hal_dma_handle;
typedef void (*hal_dma_generic_cb)(struct _hal_dma_handle *hdma);

typedef struct {
  union {
    struct {
      uint32_t src_dev : 6;
      uint32_t src_burst : 2;
      uint32_t src_addr_mode : 1;
      uint32_t src_data_width : 2;
      uint32_t reserved0 : 5;
      uint32_t snk_dev : 6;
      uint32_t snk_burst : 2;
      uint32_t snk_addr_mode : 1;
      uint32_t snk_data_width : 2;
      uint32_t reserved1 : 5;
    } bits;
    uint32_t val;
  } cfg;
  uint32_t src;
  uint32_t dst;
  uint32_t len;
  uint32_t delay;
  uint32_t next;
  uint32_t pad[2];
} hal_dma_task_desc_t;

typedef enum {
  DMA_CB_INIT = 0,
  DMA_CB_DEINIT,
  DMA_CB_XFER_HALF_TASK_COMPLETE_CB,
  DMA_CB_XFER_FULL_TASK_COMPLETE_CB,
  DMA_CB_XFER_ALL_TASKS_COMPLETE_CB,
  DMA_CB_XFER_ERROR_CB,
  DMA_CB_XFER_ABORT_CB,
} hal_dma_callback_id_e;

typedef enum {
  DMA_COMPELE_TYPE_HALF_TASK,
  DMA_COMPELE_TYPE_FULL_TASK,
  DMA_COMPELE_TYPE_ALL_TASKS,
} hal_dma_compele_type_e;

typedef struct _hal_dma_handle {
  uint32_t regbase;
  hal_dma_init_struct_t init;
  hal_dma_work_mode_e work_mode;
  uint8_t *work_buffer;
  uint32_t work_buffer_len;
  uint32_t cyclic_period_len;
  __IOM hal_lock_status_e lock;
  __IOM uint32_t errcode;
  __IOM uint32_t state;
  void *parent;
  void *priv;
  uint8_t default_buffer[DMA_DEFAULT_WORK_BUFFER_SIZE];
  hal_dma_generic_cb init_cb;
  hal_dma_generic_cb deinit_cb;
  hal_dma_generic_cb xfer_half_task_complete_cb;
  hal_dma_generic_cb xfer_full_task_complete_cb;
  hal_dma_generic_cb xfer_all_tasks_complete_cb;
  hal_dma_generic_cb xfer_error_cb;
  hal_dma_generic_cb xfer_abort_cb;
} hal_dma_handle_t;

/* The register definition of DMA */
#define DMA_IRQ_EN (0x000)
#define DMA_IRQ_STA (0x010)
#define DMA_MEM_CFG (0x020)
#define DMA_GATE (0x028)
#define DMA_CH_STA (0x030)
#define DMA_CH_EN(n) (0x100 + 0x40 * (n) + 0x000)
#define DMA_CH_PAUSE(n) (0x100 + 0x40 * (n) + 0x004)
#define DMA_CH_TASK(n) (0x100 + 0x40 * (n) + 0x008)
#define DMA_CH_CFG(n) (0x100 + 0x40 * (n) + 0x00C)
#define DMA_SRC_ADDR(n) (0x100 + 0x40 * (n) + 0x010)
#define DMA_SINK_ADDR(n) (0x100 + 0x40 * (n) + 0x014)
#define DMA_BCNT_LEFT(n) (0x100 + 0x40 * (n) + 0x018)
#define DMA_MODE(n) (0x100 + 0x40 * (n) + 0x028)
#define DMA_FDES_ADDR(n) (0x100 + 0x40 * (n) + 0x02C)
#define DMA_PKG_NUM(n) (0x100 + 0x40 * (n) + 0x030)
#define DMA_MEM_SET(n) (0x100 + 0x40 * (n) + 0x034)

#define DMA_IRQ_EN_REG(h) (h->regbase + DMA_IRQ_EN)
#define DMA_IRQ_STA_REG(h) (h->regbase + DMA_IRQ_STA)
#define DMA_MEM_CFG_REG(h) (h->regbase + DMA_MEM_CFG)
#define DMA_GATE_REG(h) (h->regbase + DMA_GATE)
#define DMA_CH_STA_REG(h) (h->regbase + DMA_CH_STA)
#define DMA_CH_EN_REG(h, n) (h->regbase + DMA_CH_EN(n))
#define DMA_CH_PAUSE_REG(h, n) (h->regbase + DMA_CH_PAUSE(n))
#define DMA_CH_TASK_REG(h, n) (h->regbase + DMA_CH_TASK(n))
#define DMA_CH_CFG_REG(h, n) (h->regbase + DMA_CH_CFG(n))
#define DMA_SRC_ADDR_REG(h, n) (h->regbase + DMA_SRC_ADDR(n))
#define DMA_SINK_ADDR_REG(h, n) (h->regbase + DMA_SINK_ADDR(n))
#define DMA_BCNT_LEFT_REG(h, n) (h->regbase + DMA_BCNT_LEFT(n))
#define DMA_MODE_REG(h, n) (h->regbase + DMA_MODE(n))
#define DMA_FDES_ADDR_REG(h, n) (h->regbase + DMA_FDES_ADDR(n))
#define DMA_PKG_NUM_REG(h, n) (h->regbase + DMA_PKG_NUM(n))
#define DMA_MEM_SET_REG(h, n) (h->regbase + DMA_MEM_SET(n))

/* The field definition of DMA_IRQ_EN */
#define DMA_IRQ_EN_CH3_ADDR_REQ_ERR_EN_MASK BIT(15)
#define DMA_IRQ_EN_CH3_ADDR_REQ_ERR_EN_SHIFT (15)
#define DMA_IRQ_EN_CH3_ADDR_REQ_ERR_EN DMA_IRQ_EN_CH3_ADDR_REQ_ERR_EN_MASK
#define DMA_IRQ_EN_CH3_ALL_FINISH_IRQ_EN_MASK BIT(14)
#define DMA_IRQ_EN_CH3_ALL_FINISH_IRQ_EN_SHIFT (14)
#define DMA_IRQ_EN_CH3_ALL_FINISH_IRQ_EN DMA_IRQ_EN_CH3_ALL_FINISH_IRQ_EN_MASK
#define DMA_IRQ_EN_CH3_TASK_FINISH_IRQ_EN_MASK BIT(13)
#define DMA_IRQ_EN_CH3_TASK_FINISH_IRQ_EN_SHIFT (13)
#define DMA_IRQ_EN_CH3_TASK_FINISH_IRQ_EN DMA_IRQ_EN_CH3_TASK_FINISH_IRQ_EN_MASK
#define DMA_IRQ_EN_CH3_TASK_HALF_IRQ_EN_MASK BIT(12)
#define DMA_IRQ_EN_CH3_TASK_HALF_IRQ_EN_SHIFT (12)
#define DMA_IRQ_EN_CH3_TASK_HALF_IRQ_EN DMA_IRQ_EN_CH3_TASK_HALF_IRQ_EN_MASK
#define DMA_IRQ_EN_CH2_ADDR_REQ_ERR_EN_MASK BIT(11)
#define DMA_IRQ_EN_CH2_ADDR_REQ_ERR_EN_SHIFT (11)
#define DMA_IRQ_EN_CH2_ADDR_REQ_ERR_EN DMA_IRQ_EN_CH2_ADDR_REQ_ERR_EN_MASK
#define DMA_IRQ_EN_CH2_ALL_FINISH_IRQ_EN_MASK BIT(10)
#define DMA_IRQ_EN_CH2_ALL_FINISH_IRQ_EN_SHIFT (10)
#define DMA_IRQ_EN_CH2_ALL_FINISH_IRQ_EN DMA_IRQ_EN_CH2_ALL_FINISH_IRQ_EN_MASK
#define DMA_IRQ_EN_CH2_TASK_FINISH_IRQ_EN_MASK BIT(9)
#define DMA_IRQ_EN_CH2_TASK_FINISH_IRQ_EN_SHIFT (9)
#define DMA_IRQ_EN_CH2_TASK_FINISH_IRQ_EN DMA_IRQ_EN_CH2_TASK_FINISH_IRQ_EN_MASK
#define DMA_IRQ_EN_CH2_TASK_HALF_IRQ_EN_MASK BIT(8)
#define DMA_IRQ_EN_CH2_TASK_HALF_IRQ_EN_SHIFT (8)
#define DMA_IRQ_EN_CH2_TASK_HALF_IRQ_EN DMA_IRQ_EN_CH2_TASK_HALF_IRQ_EN_MASK
#define DMA_IRQ_EN_CH1_ADDR_REQ_ERR_EN_MASK BIT(7)
#define DMA_IRQ_EN_CH1_ADDR_REQ_ERR_EN_SHIFT (7)
#define DMA_IRQ_EN_CH1_ADDR_REQ_ERR_EN DMA_IRQ_EN_CH1_ADDR_REQ_ERR_EN_MASK
#define DMA_IRQ_EN_CH1_ALL_FINISH_IRQ_EN_MASK BIT(6)
#define DMA_IRQ_EN_CH1_ALL_FINISH_IRQ_EN_SHIFT (6)
#define DMA_IRQ_EN_CH1_ALL_FINISH_IRQ_EN DMA_IRQ_EN_CH1_ALL_FINISH_IRQ_EN_MASK
#define DMA_IRQ_EN_CH1_TASK_FINISH_IRQ_EN_MASK BIT(5)
#define DMA_IRQ_EN_CH1_TASK_FINISH_IRQ_EN_SHIFT (5)
#define DMA_IRQ_EN_CH1_TASK_FINISH_IRQ_EN DMA_IRQ_EN_CH1_TASK_FINISH_IRQ_EN_MASK
#define DMA_IRQ_EN_CH1_TASK_HALF_IRQ_EN_MASK BIT(4)
#define DMA_IRQ_EN_CH1_TASK_HALF_IRQ_EN_SHIFT (4)
#define DMA_IRQ_EN_CH1_TASK_HALF_IRQ_EN DMA_IRQ_EN_CH1_TASK_HALF_IRQ_EN_MASK
#define DMA_IRQ_EN_CH0_ADDR_REQ_ERR_EN_MASK BIT(3)
#define DMA_IRQ_EN_CH0_ADDR_REQ_ERR_EN_SHIFT (3)
#define DMA_IRQ_EN_CH0_ADDR_REQ_ERR_EN DMA_IRQ_EN_CH0_ADDR_REQ_ERR_EN_MASK
#define DMA_IRQ_EN_CH0_ALL_FINISH_IRQ_EN_MASK BIT(2)
#define DMA_IRQ_EN_CH0_ALL_FINISH_IRQ_EN_SHIFT (2)
#define DMA_IRQ_EN_CH0_ALL_FINISH_IRQ_EN DMA_IRQ_EN_CH0_ALL_FINISH_IRQ_EN_MASK
#define DMA_IRQ_EN_CH0_TASK_FINISH_IRQ_EN_MASK BIT(1)
#define DMA_IRQ_EN_CH0_TASK_FINISH_IRQ_EN_SHIFT (1)
#define DMA_IRQ_EN_CH0_TASK_FINISH_IRQ_EN DMA_IRQ_EN_CH0_TASK_FINISH_IRQ_EN_MASK
#define DMA_IRQ_EN_CH0_TASK_HALF_IRQ_EN_MASK BIT(0)
#define DMA_IRQ_EN_CH0_TASK_HALF_IRQ_EN_SHIFT (0)
#define DMA_IRQ_EN_CH0_TASK_HALF_IRQ_EN DMA_IRQ_EN_CH0_TASK_HALF_IRQ_EN_MASK
#define DMA_IRQ_EN_CH_MASK(ch) (0xF << (ch * 4))

/* The field definition of DMA_IRQ_STA */
#define DMA_IRQ_STA_CH3_ADDR_REQ_ERR_STA_MASK BIT(15)
#define DMA_IRQ_STA_CH3_ADDR_REQ_ERR_STA_SHIFT (15)
#define DMA_IRQ_STA_CH3_ADDR_REQ_ERR_STA DMA_IRQ_STA_CH3_ADDR_REQ_ERR_STA_MASK
#define DMA_IRQ_STA_CH3_ALL_FINISH_IRQ_STA_MASK BIT(14)
#define DMA_IRQ_STA_CH3_ALL_FINISH_IRQ_STA_SHIFT (14)
#define DMA_IRQ_STA_CH3_ALL_FINISH_IRQ_STA                                     \
  DMA_IRQ_STA_CH3_ALL_FINISH_IRQ_STA_MASK
#define DMA_IRQ_STA_CH3_TASK_FINISH_IRQ_STA_MASK BIT(13)
#define DMA_IRQ_STA_CH3_TASK_FINISH_IRQ_STA_SHIFT (13)
#define DMA_IRQ_STA_CH3_TASK_FINISH_IRQ_STA                                    \
  DMA_IRQ_STA_CH3_TASK_FINISH_IRQ_STA_MASK
#define DMA_IRQ_STA_CH3_TASK_HALF_IRQ_STA_MASK BIT(12)
#define DMA_IRQ_STA_CH3_TASK_HALF_IRQ_STA_SHIFT (12)
#define DMA_IRQ_STA_CH3_TASK_HALF_IRQ_STA DMA_IRQ_STA_CH3_TASK_HALF_IRQ_STA_MASK
#define DMA_IRQ_STA_CH2_ADDR_REQ_ERR_STA_MASK BIT(11)
#define DMA_IRQ_STA_CH2_ADDR_REQ_ERR_STA_SHIFT (11)
#define DMA_IRQ_STA_CH2_ADDR_REQ_ERR_STA DMA_IRQ_STA_CH2_ADDR_REQ_ERR_STA_MASK
#define DMA_IRQ_STA_CH2_ALL_FINISH_IRQ_STA_MASK BIT(10)
#define DMA_IRQ_STA_CH2_ALL_FINISH_IRQ_STA_SHIFT (10)
#define DMA_IRQ_STA_CH2_ALL_FINISH_IRQ_STA                                     \
  DMA_IRQ_STA_CH2_ALL_FINISH_IRQ_STA_MASK
#define DMA_IRQ_STA_CH2_TASK_FINISH_IRQ_STA_MASK BIT(9)
#define DMA_IRQ_STA_CH2_TASK_FINISH_IRQ_STA_SHIFT (9)
#define DMA_IRQ_STA_CH2_TASK_FINISH_IRQ_STA                                    \
  DMA_IRQ_STA_CH2_TASK_FINISH_IRQ_STA_MASK
#define DMA_IRQ_STA_CH2_TASK_HALF_IRQ_STA_MASK BIT(8)
#define DMA_IRQ_STA_CH2_TASK_HALF_IRQ_STA_SHIFT (8)
#define DMA_IRQ_STA_CH2_TASK_HALF_IRQ_STA DMA_IRQ_STA_CH2_TASK_HALF_IRQ_STA_MASK
#define DMA_IRQ_STA_CH1_ADDR_REQ_ERR_STA_MASK BIT(7)
#define DMA_IRQ_STA_CH1_ADDR_REQ_ERR_STA_SHIFT (7)
#define DMA_IRQ_STA_CH1_ADDR_REQ_ERR_STA DMA_IRQ_STA_CH1_ADDR_REQ_ERR_STA_MASK
#define DMA_IRQ_STA_CH1_ALL_FINISH_IRQ_STA_MASK BIT(6)
#define DMA_IRQ_STA_CH1_ALL_FINISH_IRQ_STA_SHIFT (6)
#define DMA_IRQ_STA_CH1_ALL_FINISH_IRQ_STA                                     \
  DMA_IRQ_STA_CH1_ALL_FINISH_IRQ_STA_MASK
#define DMA_IRQ_STA_CH1_TASK_FINISH_IRQ_STA_MASK BIT(5)
#define DMA_IRQ_STA_CH1_TASK_FINISH_IRQ_STA_SHIFT (5)
#define DMA_IRQ_STA_CH1_TASK_FINISH_IRQ_STA                                    \
  DMA_IRQ_STA_CH1_TASK_FINISH_IRQ_STA_MASK
#define DMA_IRQ_STA_CH1_TASK_HALF_IRQ_STA_MASK BIT(4)
#define DMA_IRQ_STA_CH1_TASK_HALF_IRQ_STA_SHIFT (4)
#define DMA_IRQ_STA_CH1_TASK_HALF_IRQ_STA DMA_IRQ_STA_CH1_TASK_HALF_IRQ_STA_MASK
#define DMA_IRQ_STA_CH0_ADDR_REQ_ERR_STA_MASK BIT(3)
#define DMA_IRQ_STA_CH0_ADDR_REQ_ERR_STA_SHIFT (3)
#define DMA_IRQ_STA_CH0_ADDR_REQ_ERR_STA DMA_IRQ_STA_CH0_ADDR_REQ_ERR_STA_MASK
#define DMA_IRQ_STA_CH0_ALL_FINISH_IRQ_STA_MASK BIT(2)
#define DMA_IRQ_STA_CH0_ALL_FINISH_IRQ_STA_SHIFT (2)
#define DMA_IRQ_STA_CH0_ALL_FINISH_IRQ_STA                                     \
  DMA_IRQ_STA_CH0_ALL_FINISH_IRQ_STA_MASK
#define DMA_IRQ_STA_CH0_TASK_FINISH_IRQ_STA_MASK BIT(1)
#define DMA_IRQ_STA_CH0_TASK_FINISH_IRQ_STA_SHIFT (1)
#define DMA_IRQ_STA_CH0_TASK_FINISH_IRQ_STA                                    \
  DMA_IRQ_STA_CH0_TASK_FINISH_IRQ_STA_MASK
#define DMA_IRQ_STA_CH0_TASK_HALF_IRQ_STA_MASK BIT(0)
#define DMA_IRQ_STA_CH0_TASK_HALF_IRQ_STA_SHIFT (0)
#define DMA_IRQ_STA_CH0_TASK_HALF_IRQ_STA DMA_IRQ_STA_CH0_TASK_HALF_IRQ_STA_MASK

#define DMA_IRQ_STA_CH_ADDR_REQ_ERR_STA(ch) (0x1 << (ch * 4 + 3))
#define DMA_IRQ_STA_CH_ALL_FINISH_IRQ_STA(ch) (0x1 << (ch * 4 + 2))
#define DMA_IRQ_STA_CH_TASK_FINISH_IRQ_STA(ch) (0x1 << (ch * 4 + 1))
#define DMA_IRQ_STA_CH_TASK_HALF_IRQ_STA(ch) (0x1 << (ch * 4 + 0))
#define DMA_IRQ_STA_CH_MASK(ch) (0xF << (ch * 4))

/* The field definition of DMA_MEM_CFG */
#define DMA_MEM_CFG_MEM_BURST_SET_MASK BIT(31)
#define DMA_MEM_CFG_MEM_BURST_SET_SHIFT (31)
#define DMA_MEM_CFG_MEM_BURST_SET DMA_MEM_CFG_MEM_BURST_SET_MASK
#define DMA_MEM_CFG_AHB_BURST_SEL_MASK BIT(27)
#define DMA_MEM_CFG_AHB_BURST_SEL_SHIFT (27)
#define DMA_MEM_CFG_AHB_BURST_SEL DMA_MEM_CFG_AHB_BURST_SEL_MASK

/* The field definition of DMA_GATE */
#define DMA_GATE_DMA_GATE_BYPASS_MASK BIT(0)
#define DMA_GATE_DMA_GATE_BYPASS_SHIFT (0)
#define DMA_GATE_DMA_GATE_BYPASS DMA_GATE_DMA_GATE_BYPASS_MASK

/* The field definition of DMA_CH_STA */
#define DMA_CH_STA_DMA_CH3_STATUS_MASK BIT(3)
#define DMA_CH_STA_DMA_CH3_STATUS_SHIFT (3)
#define DMA_CH_STA_DMA_CH3_STATUS DMA_CH_STA_DMA_CH3_STATUS_MASK
#define DMA_CH_STA_DMA_CH2_STATUS_MASK BIT(2)
#define DMA_CH_STA_DMA_CH2_STATUS_SHIFT (2)
#define DMA_CH_STA_DMA_CH2_STATUS DMA_CH_STA_DMA_CH2_STATUS_MASK
#define DMA_CH_STA_DMA_CH1_STATUS_MASK BIT(1)
#define DMA_CH_STA_DMA_CH1_STATUS_SHIFT (1)
#define DMA_CH_STA_DMA_CH1_STATUS DMA_CH_STA_DMA_CH1_STATUS_MASK
#define DMA_CH_STA_DMA_CH0_STATUS_MASK BIT(0)
#define DMA_CH_STA_DMA_CH0_STATUS_SHIFT (0)
#define DMA_CH_STA_DMA_CH0_STATUS DMA_CH_STA_DMA_CH0_STATUS_MASK

/* The field definition of DMA_CH_EN */
#define DMA_CH_EN_DMA_CH_EN_MASK BIT(0)
#define DMA_CH_EN_DMA_CH_EN_SHIFT (0)
#define DMA_CH_EN_DMA_CH_EN DMA_CH_EN_DMA_CH_EN_MASK

/* The field definition of DMA_CH_PAUSE */
#define DMA_CH_PAUSE_MEM_BYTE_ENABLE_MASK BIT(5)
#define DMA_CH_PAUSE_MEM_BYTE_ENABLE_SHIFT (5)
#define DMA_CH_PAUSE_MEM_BYTE_ENABLE DMA_CH_PAUSE_MEM_BYTE_ENABLE_MASK
#define DMA_CH_PAUSE_MEM_SET_ENABLE_MASK BIT(4)
#define DMA_CH_PAUSE_MEM_SET_ENABLE_SHIFT (4)
#define DMA_CH_PAUSE_MEM_SET_ENABLE DMA_CH_PAUSE_MEM_SET_ENABLE_MASK
#define DMA_CH_PAUSE_DMA_CH_PAUSE_MASK BIT(0)
#define DMA_CH_PAUSE_DMA_CH_PAUSE_SHIFT (0)
#define DMA_CH_PAUSE_DMA_CH_PAUSE DMA_CH_PAUSE_DMA_CH_PAUSE_MASK

/* The field definition of DMA_CH_TASK */
#define DMA_CH_TASK_DMA_TASK_ADDR_MASK GENMASK(31, 0)
#define DMA_CH_TASK_DMA_TASK_ADDR_SHIFT (0)
#define DMA_CH_TASK_DMA_TASK_ADDR DMA_CH_TASK_DMA_TASK_ADDR_MASK

/* The field definition of DMA_CH_CFG */
#define DMA_CH_CFG_SNK_DATA_WIDTH_MASK GENMASK(26, 25)
#define DMA_CH_CFG_SNK_DATA_WIDTH_SHIFT (25)
#define DMA_CH_CFG_SNK_DATA_WIDTH DMA_CH_CFG_SNK_DATA_WIDTH_MASK
#define DMA_CH_CFG_SNK_ADDR_MODE_MASK BIT(24)
#define DMA_CH_CFG_SNK_ADDR_MODE_SHIFT (24)
#define DMA_CH_CFG_SNK_ADDR_MODE DMA_CH_CFG_SNK_ADDR_MODE_MASK
#define DMA_CH_CFG_SNK_BURST_MASK GENMASK(23, 22)
#define DMA_CH_CFG_SNK_BURST_SHIFT (22)
#define DMA_CH_CFG_SNK_BURST DMA_CH_CFG_SNK_BURST_MASK
#define DMA_CH_CFG_SNK_DEV_MASK GENMASK(21, 16)
#define DMA_CH_CFG_SNK_DEV_SHIFT (16)
#define DMA_CH_CFG_SNK_DEV DMA_CH_CFG_SNK_DEV_MASK
#define DMA_CH_CFG_SRC_DATA_WIDTH_MASK GENMASK(10, 9)
#define DMA_CH_CFG_SRC_DATA_WIDTH_SHIFT (9)
#define DMA_CH_CFG_SRC_DATA_WIDTH DMA_CH_CFG_SRC_DATA_WIDTH_MASK
#define DMA_CH_CFG_SRC_ADDR_MODE_MASK BIT(8)
#define DMA_CH_CFG_SRC_ADDR_MODE_SHIFT (8)
#define DMA_CH_CFG_SRC_ADDR_MODE DMA_CH_CFG_SRC_ADDR_MODE_MASK
#define DMA_CH_CFG_SRC_BURST_MASK GENMASK(7, 6)
#define DMA_CH_CFG_SRC_BURST_SHIFT (6)
#define DMA_CH_CFG_SRC_BURST DMA_CH_CFG_SRC_BURST_MASK
#define DMA_CH_CFG_SRC_DEV_MASK GENMASK(5, 0)
#define DMA_CH_CFG_SRC_DEV_SHIFT (0)
#define DMA_CH_CFG_SRC_DEV DMA_CH_CFG_SRC_DEV_MASK

/* The field definition of DMA_BCNT_LEFT */
#define DMA_BCNT_LEFT_VAL_MASK GENMASK(24, 0)
#define DMA_BCNT_LEFT_VAL_SHIFT (0)
#define DMA_BCNT_LEFT_VAL DMA_BCNT_LEFT_VAL_MASK

/* The field definition of DMA_MODE */
#define DMA_MODE_HANDSHAKE_ENABLE_MASK BIT(4)
#define DMA_MODE_HANDSHAKE_ENABLE_SHIFT (4)
#define DMA_MODE_HANDSHAKE_ENABLE DMA_MODE_HANDSHAKE_ENABLE_MASK
#define DMA_MODE_DMA_SNK_MODE_MASK BIT(3)
#define DMA_MODE_DMA_SNK_MODE_SHIFT (3)
#define DMA_MODE_DMA_SNK_MODE DMA_MODE_DMA_SNK_MODE_MASK
#define DMA_MODE_DMA_SRC_MODE_MASK BIT(2)
#define DMA_MODE_DMA_SRC_MODE_SHIFT (2)
#define DMA_MODE_DMA_SRC_MODE DMA_MODE_DMA_SRC_MODE_MASK

/* The field definition of DMA_FDES_ADDR */
#define DMA_FDES_ADDR_VAL_MASK GENMASK(31, 0)
#define DMA_FDES_ADDR_VAL_SHIFT (0)
#define DMA_FDES_ADDR_VAL DMA_FDES_ADDR_VAL_MASK

/* The field definition of DMA_PKG_NUM */
#define DMA_PKG_NUM_VAL_MASK GENMASK(31, 0)
#define DMA_PKG_NUM_VAL_SHIFT (0)
#define DMA_PKG_NUM_VAL DMA_PKG_NUM_VAL_MASK

#define DMA_CHANNEL_TASK_END (0xFFFFF800UL)

#define DMA_ADDR_ALIGN_MASK (0xFFFFFFF8UL)
#define DMA_TASK_MAX_XFER_LEN (0x01FFFFFFUL)

void hal_dma_handle_init(hal_dma_handle_t *hdma);
hal_status_e hal_dma_init(hal_dma_handle_t *hdma);
hal_status_e hal_dma_deinit(hal_dma_handle_t *hdma);
hal_status_e hal_dma_start(hal_dma_handle_t *hdma, uint32_t src, uint32_t dst,
                           uint32_t len);
hal_status_e hal_dma_start_it(hal_dma_handle_t *hdma, uint32_t src,
                              uint32_t dst, uint32_t len);
hal_status_e hal_dma_abort(hal_dma_handle_t *hdma);
hal_status_e hal_dma_abort_it(hal_dma_handle_t *hdma);
hal_status_e hal_dma_poll_for_transfer(hal_dma_handle_t *hdma,
                                       hal_dma_compele_type_e type,
                                       uint32_t timeout);
hal_status_e hal_dma_registger_callback(hal_dma_handle_t *hdma,
                                        hal_dma_callback_id_e id,
                                        hal_dma_generic_cb cb);
hal_status_e hal_dma_unregistger_callback(hal_dma_handle_t *hdma,
                                          hal_dma_callback_id_e id);
hal_status_e hal_dma_memset(hal_dma_handle_t *hdma, void *start, uint32_t value,
                            uint32_t bytes);
hal_status_e hal_dma_pause(hal_dma_handle_t *hdma);
hal_status_e hal_dma_resume(hal_dma_handle_t *hdma);
hal_status_e hal_dma_get_residue(hal_dma_handle_t *hdma, uint32_t *left_size);
int hal_dma_dump(int channel_id);
irqreturn_t hal_dma_irq(int irq, void *arg);
void hal_dma_irq_handler(hal_dma_handle_t *hdma);
hal_dma_state_e hal_dma_get_state(hal_dma_handle_t *hdma);
hal_dma_error_e hal_dma_get_error(hal_dma_handle_t *hdma);

#ifdef __cplusplus
}
#endif

#endif
