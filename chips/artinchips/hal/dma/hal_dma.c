/*
 * Copyright (c) 2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hal_dma.h"
#include "aic_core.h"
#include <debug.h>
#include <nuttx/clock.h>
#ifdef __cplusplus
extern "C" {
#endif

/* 模拟获取tick函数 */
static inline uint64_t hal_get_tick(void) {
  /* 这里只是一个简单实现，实际平台需要根据硬件特性实现 */
  return clock_systime_ticks();
}

/**
 * @brief Initialize the DMA Channel handle instance with default parameters
 *
 * This function performs default initialization on a DMA Channel handle
 * instance. Users can call this function to perform basic initialization and
 * then customize the handle parameters according to their specific needs.
 *
 * @param hdma Pointer to the DMA handle structure
 *
 * @note This function does not return an error code. If the hdma parameter is
 * NULL, it returns directly
 */
void hal_dma_handle_init(hal_dma_handle_t *hdma) {
  // Parameter check: return directly if hdma is NULL
  if (!hdma)
    return;

  // Initialize basic state and configuration fields
  hdma->lock = HAL_UNLOCKED;
  hdma->regbase = 0;
  hdma->errcode = 0;
  hdma->state = DMA_STATE_RESET;
  hdma->work_buffer = hdma->default_buffer;
  hdma->work_buffer_len = DMA_DEFAULT_WORK_BUFFER_SIZE;
  hdma->cyclic_period_len = 0;
  hdma->work_mode = 0;
  hdma->parent = NULL;

  // Initialize DMA transfer parameter structure
  hdma->init.channel_id = 0;
  hdma->init.direction = 0;
  hdma->init.src_dev = 0;
  hdma->init.src_mode = 0;
  hdma->init.src_burst = 0;
  hdma->init.src_addr_mode = 0;
  hdma->init.src_data_width = 0;
  hdma->init.snk_dev = 0;
  hdma->init.snk_mode = 0;
  hdma->init.snk_burst = 0;
  hdma->init.snk_addr_mode = 0;
  hdma->init.snk_data_width = 0;
  hdma->init.delay = 64;
  hdma->init.flag = HAL_HANDLE_ALL_INIT_FLAG;

  // Initialize callback function pointers to NULL
  hdma->init_cb = NULL;
  hdma->deinit_cb = NULL;
  hdma->xfer_half_task_complete_cb = NULL;
  hdma->xfer_full_task_complete_cb = NULL;
  hdma->xfer_all_tasks_complete_cb = NULL;
  hdma->xfer_error_cb = NULL;
  hdma->xfer_abort_cb = NULL;
}

/**
 * @brief Initialize the DMA channel with specified parameters
 *
 * This function initializes a DMA channel according to the parameters specified
 * in the handle structure. It configures the DMA hardware, and prepares the
 * channel for data transfer operations.
 *
 * @param hdma Pointer to the DMA handle structure containing initialization
 * parameters
 *
 * @return HAL_OK if initialization is successful, HAL_ERROR otherwise
 *
 * @note Before calling this function, hal_dma_handle_init() should be called to
 * initialize the handle with default parameters, and the parameters should be
 * customized as needed. Also, init_cb callback should be provided to configure
 * clock enable and other settings.
 */
hal_status_e hal_dma_init(hal_dma_handle_t *hdma) {

  uint32_t ch, delta, task_cnt, val;

  if (!hdma) {
    return HAL_ERROR;
  }

  ASSERT_PARAM(0 != (hdma->init.flag & HAL_HANDLE_ALL_INIT_FLAG));
  ASSERT_PARAM(hdma->regbase != 0);

  if (!hdma->work_buffer || !hdma->work_buffer_len) {
    hdma->errcode = DMA_ERROR_PARAM;
    return HAL_ERROR;
  }

  /* Calculate the number of available task descriptors in work buffer */
  delta = CACHE_LINE_SIZE - (((uint32_t)hdma->work_buffer) & CACHE_ADDR_MASK);
  task_cnt = (hdma->work_buffer_len - delta) / sizeof(hal_dma_task_desc_t);

  /* Validate work buffer size according to work mode requirements */
  if (hdma->work_mode == DMA_WORK_MODE_CYCLIC) {
    if (!hdma->cyclic_period_len) {
      hdma->errcode = DMA_ERROR_XFER_LEN;
      return HAL_ERROR;
    }

    if (task_cnt < 2) {
      /* DMA_WORK_MODE_CYCLIC at least 2 tasks */
      hdma->errcode = DMA_ERROR_PARAM;
      return HAL_ERROR;
    }
  } else if (task_cnt < 1) {
    hdma->errcode = DMA_ERROR_PARAM;
    return HAL_ERROR;
  }

  /* Source and Sink device ID should match direction */
  if (hdma->init.direction == DMA_MEMORY_TO_MEMORY) {
    if (hdma->init.src_dev != hdma->init.snk_dev) {
      hdma->errcode = DMA_ERROR_PARAM;
      return HAL_ERROR;
    }
  } else if (hdma->init.direction == DMA_MEMORY_TO_DEVICE) {
    if (hdma->init.src_dev != 0 || hdma->init.snk_dev == 0) {
      hdma->errcode = DMA_ERROR_PARAM;
      return HAL_ERROR;
    }
  } else if (hdma->init.direction == DMA_DEVICE_TO_MEMORY) {
    if (hdma->init.src_dev == 0 || hdma->init.snk_dev != 0) {
      hdma->errcode = DMA_ERROR_PARAM;
      return HAL_ERROR;
    }
  }

  hdma->state = DMA_STATE_RESET;
  /* Before configure the DMA, need to enable it */
  if (hdma->init_cb) {
    hdma->init_cb(hdma);
  }

  /* Validate channel ID */
  if (hdma->init.channel_id >= DMA_MAX_CHANNEL_NUM) {
    hdma->errcode = DMA_ERROR_CHAN_NO_EXIST;
    return HAL_ERROR;
  }
  ch = hdma->init.channel_id;

  /* Ensure DMA channel is stopped */
  writel(0, DMA_CH_EN_REG(hdma, ch));
  writel(0, DMA_CH_PAUSE_REG(hdma, ch));
  writel(DMA_MEM_CFG_AHB_BURST_SEL, DMA_MEM_CFG_REG(hdma));

  /* Clear channel's irq enable bits */
  val = readl(DMA_IRQ_EN_REG(hdma));
  val &= ~DMA_IRQ_EN_CH_MASK(ch);
  writel(val, DMA_IRQ_EN_REG(hdma));

  /* Clear channel's irq status bits */
  val = DMA_IRQ_STA_CH_MASK(ch);
  writel(val, DMA_IRQ_STA_REG(hdma));

  hdma->errcode = DMA_ERROR_NONE;
  hdma->state = DMA_STATE_READY;

  return HAL_OK;
}

/**
 * @brief Deinitialize the DMA channel
 * @param hdma Pointer to the DMA handle structure
 * @return HAL_OK on success, HAL_ERROR on failure
 *
 * This function deinitializes the DMA channel instance. It stops the DMA
 * channel, releases resources, and calls the deinit callback if provided.
 *
 * The user should perform hardware resource release actions in the deinit_cb()
 * callback, such as disabling DMA clock when all channels are no longer in use.
 */
hal_status_e hal_dma_deinit(hal_dma_handle_t *hdma) {
  uint32_t ch;

  if (!hdma) {
    return HAL_ERROR;
  }
  if ((hdma->init.flag & HAL_HANDLE_ALL_INIT_FLAG) == 0) {
    return HAL_ERROR;
  }
  if (hdma->regbase == 0) {
    return HAL_ERROR;
  }

  if (hdma->init.channel_id >= DMA_MAX_CHANNEL_NUM) {
    hdma->errcode = DMA_ERROR_CHAN_NO_EXIST;
    return HAL_ERROR;
  }
  ch = hdma->init.channel_id;

  /* Ensure DMA Channel is stopped */
  writel(1, DMA_CH_PAUSE_REG(hdma, ch));
  writel(0, DMA_CH_EN_REG(hdma, ch));
  writel(0, DMA_CH_PAUSE_REG(hdma, ch));

  /* Release other resource, and CMU disable DMA */
  if (hdma->deinit_cb) {
    hdma->deinit_cb(hdma);
  }

  hdma->errcode = DMA_ERROR_NONE;
  hdma->state = DMA_STATE_RESET;
  return HAL_OK;
}

/**
 * @brief Configure a DMA task descriptor in normal mode
 *
 * This function sets up a DMA task descriptor for normal mode operation where
 * the DMA channel executes a single task. It configures source and destination
 * parameters along with device-specific settings from the DMA handle.
 *
 * @param hdma Pointer to the DMA handle structure containing initialization
 * parameters
 * @param src Source address for the DMA transfer
 * @param dst Destination address for the DMA transfer
 * @param len Length of data to transfer
 *
 * @return Pointer to the configured task descriptor
 */
static hal_dma_task_desc_t *dma_normal_mode_task_cfg(hal_dma_handle_t *hdma,
                                                     uint32_t src, uint32_t dst,
                                                     uint32_t len) {
  hal_dma_task_desc_t *task;

  /* Get aligned task descriptor from work buffer */
  task = CACHE_ALIGN_UP(hdma->work_buffer);

  /* Set basic transfer parameters */
  task->src = src;
  task->dst = dst;
  task->len = len;

  /* Initialize configuration value */
  task->cfg.val = 0;

  /* Configure source device parameters */
  task->cfg.bits.src_dev = hdma->init.src_dev;
  task->cfg.bits.src_burst = hdma->init.src_burst;
  task->cfg.bits.src_addr_mode = hdma->init.src_addr_mode;
  task->cfg.bits.src_data_width = hdma->init.src_data_width;

  /* Configure sink device parameters */
  task->cfg.bits.snk_dev = hdma->init.snk_dev;
  task->cfg.bits.snk_burst = hdma->init.snk_burst;
  task->cfg.bits.snk_addr_mode = hdma->init.snk_addr_mode;
  task->cfg.bits.snk_data_width = hdma->init.snk_data_width;

  /* Set delay clocks between tasks */
  task->delay = hdma->init.delay;

  /* Mark as last task in chain */
  task->next = DMA_CHANNEL_TASK_END;

  /* Clear padding fields */
  task->pad[0] = 0;
  task->pad[1] = 0;

  /* Flush cache to ensure hardware sees updated values */
    csi_dcache_clean_range((phy_addr_t)task, sizeof(*task));
  return task;
}

/*
 * Configure DMA cyclic mode tasks. The number of tasks is determined by
 * the data length to transfer and the cyclic period length
 * (hdma->cyclic_period_len). At least 2 tasks are required, and they form a
 * circular linked list to execute repeatedly. When the task count exceeds 2, an
 * additional work_buffer must be configured with sufficient length to
 * accommodate the required number of task descriptors.
 *
 * @param hdma Pointer to the DMA handle structure containing initialization
 * parameters
 * @param src Source address for the DMA transfer
 * @param dst Destination address for the DMA transfer
 * @param len Length of data to transfer
 * @return Pointer to the configured task descriptor array, or NULL if
 * configuration failed
 */
static hal_dma_task_desc_t *dma_cyclic_mode_task_cfg(hal_dma_handle_t *hdma,
                                                     uint32_t src, uint32_t dst,
                                                     uint32_t len)

{
  uint32_t i, task_cnt, xfer_len;
  hal_dma_task_desc_t *task;

  /* Validate cyclic period length parameter */
  if (hdma->cyclic_period_len == 0) {
    hdma->errcode = DMA_ERROR_PARAM;
    auderr("Cyclic period length is not configured for cyclic mode\n");
    return NULL;
  }

  /* Check if transfer length is a multiple of cyclic period length */
  if (len % hdma->cyclic_period_len) {
    hdma->errcode = DMA_ERROR_PARAM;
    auderr(
        "Transfer length (%u) is not a multiple of cyclic period length (%u)\n",
        len, hdma->cyclic_period_len);
    return NULL;
  }

  /* Calculate required number of tasks */
  task_cnt = len / hdma->cyclic_period_len;
  if (task_cnt == 0) {
    hdma->errcode = DMA_ERROR_PARAM;
    auderr("Calculated task count is zero, check transfer length and cyclic "
           "period length\n");
    return NULL;
  }

  /* Calculate data length per task */
  xfer_len = hdma->cyclic_period_len;

  /* Get aligned task descriptor from work buffer */
  task = CACHE_ALIGN_UP(hdma->work_buffer);

  /* Configure each task in the cyclic chain */
  for (i = 0; i < task_cnt; i++) {
    /* Set source and destination addresses based on transfer direction */
    if (hdma->init.direction == DMA_MEMORY_TO_DEVICE) {
      task[i].src = src + i * xfer_len;
      task[i].dst = dst;
    } else if (hdma->init.direction == DMA_DEVICE_TO_MEMORY) {
      task[i].src = src;
      task[i].dst = dst + i * xfer_len;
    } else {
      task[i].src = src + i * xfer_len;
      task[i].dst = dst + i * xfer_len;
    }
    task[i].len = xfer_len;

    /* Initialize configuration value */
    task[i].cfg.val = 0;

    /* Configure source device parameters */
    task[i].cfg.bits.src_dev = hdma->init.src_dev;
    task[i].cfg.bits.src_burst = hdma->init.src_burst;
    task[i].cfg.bits.src_addr_mode = hdma->init.src_addr_mode;
    task[i].cfg.bits.src_data_width = hdma->init.src_data_width;

    /* Configure sink device parameters */
    task[i].cfg.bits.snk_dev = hdma->init.snk_dev;
    task[i].cfg.bits.snk_burst = hdma->init.snk_burst;
    task[i].cfg.bits.snk_addr_mode = hdma->init.snk_addr_mode;
    task[i].cfg.bits.snk_data_width = hdma->init.snk_data_width;

    /* Set delay clocks between tasks */
    task[i].delay = hdma->init.delay;

    if ((i + 1) == task_cnt) {
      /* Link to next task - last task points to first to form a circular chain
       */
      task[i].next = (uint32_t)&task[0]; // Last task links to first task,
                                         // forming a circular task chain
    } else {
      task[i].next = (uint32_t)&task[i + 1];
    }

    /* Clear padding fields */
    task[i].pad[0] = 0;
    task[i].pad[1] = 0;
  }

  /* Flush cache to ensure hardware sees updated values */
    csi_dcache_clean_range((phy_addr_t)task, task_cnt * sizeof(*task));
  return task;
}

static hal_status_e dma_xfer_param_check(hal_dma_handle_t *hdma, uint32_t src,
                                         uint32_t dst, uint32_t len) {
  if (!hdma->work_buffer) {
    hdma->errcode = DMA_ERROR_PARAM;
    auderr("DMA work buffer is not configured\n");
    return HAL_ERROR;
  }

  if ((src & (~DMA_ADDR_ALIGN_MASK)) || (dst & (~DMA_ADDR_ALIGN_MASK))) {
    hdma->errcode = DMA_ERROR_ADDRESS_ALIGN;
    auderr("DMA address is not aligned"
           " src=0x%08x dst=0x%08x align_mask=0x%08x\n",
           src, dst, (unsigned int)DMA_ADDR_ALIGN_MASK);
    return HAL_ERROR;
  }

  if (len > DMA_TASK_MAX_XFER_LEN) {
    hdma->errcode = DMA_ERROR_XFER_LEN;
    auderr("DMA transfer length exceeds maximum limit, len=%u max=%u\n", len,
           (unsigned int)DMA_TASK_MAX_XFER_LEN);
    return HAL_ERROR;
  }

  if (hdma->init.channel_id >= DMA_MAX_CHANNEL_NUM) {
    hdma->errcode = DMA_ERROR_CHAN_NO_EXIST;
    auderr("DMA channel does not exist, channel_id=%u\n",
           hdma->init.channel_id);

    return HAL_ERROR;
  }

  return HAL_OK;
}

/**
 * @brief Start DMA transfer in polling mode
 *
 * This function starts a DMA transfer operation in polling mode. After starting
 * the transfer, the user needs to call hal_dma_poll_for_transfer() to wait for
 * the transfer completion.
 *
 * @param hdma Pointer to the DMA handle structure
 * @param src Source address for the DMA transfer
 * @param dst Destination address for the DMA transfer
 * @param len Length of data to transfer
 *
 * @return HAL_OK if transfer started successfully, HAL_ERROR otherwise
 *
 * @note This function supports both normal and cyclic work modes. In cyclic
 * mode, multiple tasks will be configured to form a circular chain.
 */
hal_status_e hal_dma_start(hal_dma_handle_t *hdma, uint32_t src, uint32_t dst,
                           uint32_t len) {
  hal_dma_task_desc_t *task;
  uint32_t ch, mode, val;
  hal_status_e ret;

  /* Check for valid handle */
  if (!hdma) {
    return HAL_ERROR;
  }

  HAL_LOCK(hdma);

  /* Validate transfer parameters */
  ret = dma_xfer_param_check(hdma, src, dst, len);
  if (ret != HAL_OK) {
    auderr(
        "DMA transfer parameter check failed: src=0x%08x dst=0x%08x len=%u\n",
        src, dst, len);
    goto out;
  }

  /* Check if DMA channel is ready */
  if (hdma->state != DMA_STATE_READY) {
    hdma->errcode = DMA_ERROR_BUSY;
    ret = HAL_ERROR;
    auderr("DMA channel is not ready, current state=%u\n", hdma->state);
    goto out;
  }

  /* Configure task descriptor based on work mode */
  if (hdma->work_mode == DMA_WORK_MODE_NORMAL) {
    task = dma_normal_mode_task_cfg(hdma, src, dst, len);
  } else if (hdma->work_mode == DMA_WORK_MODE_CYCLIC) {
    task = dma_cyclic_mode_task_cfg(hdma, src, dst, len);
    if (!task) {
      auderr("Failed to configure DMA cyclic mode tasks\n");
      ret = HAL_ERROR;
      goto out;
    }
  } else {
    hdma->errcode = DMA_ERROR_WORK_MODE;
    ret = HAL_ERROR;
    auderr("Invalid DMA work mode: %u\n", hdma->work_mode);
    goto out;
  }

  ch = hdma->init.channel_id;

  /* Configure DMA channel mode based on source/sink device modes */
  mode = 0;
  if (hdma->init.src_mode == DMA_MODE_HANDSHAKE) {
    mode |= DMA_MODE_DMA_SRC_MODE_MASK;
    if (hdma->init.src_burst != DMA_XFER_BURST_1)
      mode |= DMA_MODE_HANDSHAKE_ENABLE_MASK;
  }
  if (hdma->init.snk_mode == DMA_MODE_HANDSHAKE) {
    mode |= DMA_MODE_DMA_SNK_MODE_MASK;
    if (hdma->init.snk_burst != DMA_XFER_BURST_1)
      mode |= DMA_MODE_HANDSHAKE_ENABLE_MASK;
  }
  writel(mode, DMA_MODE_REG(hdma, ch));

  /* Clear channel's irq enable bits */
  val = readl(DMA_IRQ_EN_REG(hdma));
  val &= ~DMA_IRQ_EN_CH_MASK(ch);
  writel(val, DMA_IRQ_EN_REG(hdma));

  /* Clear channel's irq status bits */
  val = DMA_IRQ_STA_CH_MASK(ch);
  writel(val, DMA_IRQ_STA_REG(hdma));

  /* Start task */
  writel((uint32_t)task, DMA_CH_TASK_REG(hdma, ch));
  writel(1, DMA_CH_EN_REG(hdma, ch));

  hdma->state = DMA_STATE_BUSY;
  ret = HAL_OK;

out:
  HAL_UNLOCK(hdma);
  return ret;
}

/**
 * @brief Start DMA transfer in interrupt mode
 *
 * This function starts a DMA transfer operation in interrupt mode. After
 * starting the transfer, the result is obtained through interrupt callback
 * functions.
 *
 * @param hdma Pointer to the DMA handle structure
 * @param src Source address for the DMA transfer
 * @param dst Destination address for the DMA transfer
 * @param len Length of data to transfer
 *
 * @return HAL_OK if transfer started successfully, HAL_ERROR otherwise
 *
 * @note This function supports both normal and cyclic work modes. In cyclic
 * mode, multiple tasks will be configured to form a circular chain. The user
 * must register appropriate callback functions to handle transfer completion
 * events.
 */
hal_status_e hal_dma_start_it(hal_dma_handle_t *hdma, uint32_t src,
                              uint32_t dst, uint32_t len) {
  hal_dma_task_desc_t *task;
  uint32_t ch, mode, val;
  hal_status_e ret;

  if (!hdma) {
    return HAL_ERROR;
  }

  HAL_LOCK(hdma);

  ret = dma_xfer_param_check(hdma, src, dst, len);
  if (ret != HAL_OK) {
    goto out;
  }

  if (hdma->state != DMA_STATE_READY) {
    hdma->errcode = DMA_ERROR_BUSY;
    ret = HAL_ERROR;
    goto out;
  }

  if (hdma->work_mode == DMA_WORK_MODE_NORMAL) {
    task = dma_normal_mode_task_cfg(hdma, src, dst, len);
  } else if (hdma->work_mode == DMA_WORK_MODE_CYCLIC) {
    task = dma_cyclic_mode_task_cfg(hdma, src, dst, len);
    if (!task) {
      ret = HAL_ERROR;
      goto out;
    }
  } else {
    hdma->errcode = DMA_ERROR_WORK_MODE;
    ret = HAL_ERROR;
    goto out;
  }

  ch = hdma->init.channel_id;

  /* Configure DMA channel mode based on source/sink device modes */
  mode = 0;
  if (hdma->init.src_mode == DMA_MODE_HANDSHAKE) {
    mode |= DMA_MODE_DMA_SRC_MODE_MASK;
    if (hdma->init.src_burst != DMA_XFER_BURST_1)
      mode |= DMA_MODE_HANDSHAKE_ENABLE_MASK;
  }
  if (hdma->init.snk_mode == DMA_MODE_HANDSHAKE) {
    mode |= DMA_MODE_DMA_SNK_MODE_MASK;
    if (hdma->init.snk_burst != DMA_XFER_BURST_1)
      mode |= DMA_MODE_HANDSHAKE_ENABLE_MASK;
  }
  writel(mode, DMA_MODE_REG(hdma, ch));

  /* Clear channel's irq status bits */
  val = DMA_IRQ_STA_CH_MASK(ch);
  writel(val, DMA_IRQ_STA_REG(hdma));

  /* Enable channel's irq enable bits */
  val = readl(DMA_IRQ_EN_REG(hdma));
  val |= DMA_IRQ_EN_CH_MASK(ch);
  writel(val, DMA_IRQ_EN_REG(hdma));

  /* Start task */
  writel((uint32_t)task, DMA_CH_TASK_REG(hdma, ch));
  writel(1, DMA_CH_EN_REG(hdma, ch));

  hdma->state = DMA_STATE_BUSY;
  ret = HAL_OK;

out:
  HAL_UNLOCK(hdma);
  return ret;
}

/**
 * @brief Abort DMA transfer in polling mode
 *
 * This function aborts an ongoing DMA transfer by pausing and disabling
 * the DMA channel.
 *
 * @param hdma Pointer to the DMA handle structure
 *
 * @return HAL_OK if abort successful, HAL_ERROR otherwise
 *
 * @note This function should only be called when DMA is in BUSY state
 */
hal_status_e hal_dma_abort(hal_dma_handle_t *hdma) {
  uint32_t ch;

  if (!hdma) {
    return HAL_ERROR;
  }

  HAL_LOCK(hdma);

  if (hdma->init.channel_id >= DMA_MAX_CHANNEL_NUM) {
    hdma->errcode = DMA_ERROR_CHAN_NO_EXIST;
    HAL_UNLOCK(hdma);
    return HAL_ERROR;
  }

  if (hdma->state != DMA_STATE_BUSY) {
    hdma->errcode = DMA_ERROR_BUSY;
    HAL_UNLOCK(hdma);
    return HAL_ERROR;
  }

  ch = hdma->init.channel_id;
  writel(1, DMA_CH_PAUSE_REG(hdma, ch));
  writel(0, DMA_CH_EN_REG(hdma, ch));

  hdma->errcode = DMA_ERROR_NONE;
  hdma->state = DMA_STATE_READY;

  HAL_UNLOCK(hdma);
  return HAL_OK;
}

/**
 * @brief Abort an ongoing DMA transfer in interrupt mode
 * @param hdma Pointer to a hal_dma_handle_t structure that contains
 *        the configuration information for the specified DMA Channel
 * @retval hal_status_e HAL status
 *         - HAL_OK if the abort operation was successful
 *         - HAL_ERROR if an error occurred
 */
hal_status_e hal_dma_abort_it(hal_dma_handle_t *hdma) {
  uint32_t ch, val;

  /* Check for invalid handle */
  if (!hdma) {
    return HAL_ERROR;
  }

  HAL_LOCK(hdma);

  /* Validate channel ID */
  if (hdma->init.channel_id >= DMA_MAX_CHANNEL_NUM) {
    hdma->errcode = DMA_ERROR_CHAN_NO_EXIST;
    HAL_UNLOCK(hdma);
    return HAL_ERROR;
  }

  /* Check if DMA is busy */
  if (hdma->state != DMA_STATE_BUSY) {
    hdma->errcode = DMA_ERROR_BUSY;
    HAL_UNLOCK(hdma);
    return HAL_ERROR;
  }

  ch = hdma->init.channel_id;

  /* Pause and disable the DMA channel */
  writel(1, DMA_CH_PAUSE_REG(hdma, ch));
  writel(0, DMA_CH_EN_REG(hdma, ch));

  /* Clear channel's irq enable bits */
  val = readl(DMA_IRQ_EN_REG(hdma));
  val &= ~DMA_IRQ_EN_CH_MASK(ch);
  writel(val, DMA_IRQ_EN_REG(hdma));

  /* Clear channel's irq status bits */
  val = DMA_IRQ_STA_CH_MASK(ch);
  writel(val, DMA_IRQ_STA_REG(hdma));

  hdma->errcode = DMA_ERROR_NONE;
  hdma->state = DMA_STATE_READY;

  /* Call user abort callback if registered */
  if (hdma->xfer_abort_cb)
    hdma->xfer_abort_cb(hdma);

  HAL_UNLOCK(hdma);
  return HAL_OK;
}

/**
 * @brief Poll for the completion of a DMA transfer
 *
 * This function polls the DMA controller status registers to check for transfer
 * completion or errors. It waits for a specific type of completion event (half
 * task, full task, or all tasks) within a specified timeout period.
 *
 * @param hdma Pointer to the DMA handle structure
 * @param type Type of completion to wait for:
 *             - DMA_COMPELE_TYPE_HALF_TASK: Wait for half task completion
 *             - DMA_COMPELE_TYPE_FULL_TASK: Wait for full task completion
 *             - DMA_COMPELE_TYPE_ALL_TASKS: Wait for all tasks completion
 * @param timeout Maximum time to wait for completion in milliseconds
 *
 * @return HAL_OK if the expected completion event occurred
 *         HAL_TIMEOUT if the operation timed out
 *         HAL_ERROR if an error occurred or invalid parameters were provided
 *
 * @note This function should be called after starting a DMA transfer with
 * hal_dma_start()
 */
hal_status_e hal_dma_poll_for_transfer(hal_dma_handle_t *hdma,
                                       hal_dma_compele_type_e type,
                                       uint32_t timeout) {
  uint64_t start_ms, cur_ms;
  uint32_t ch, val;

  /* Check for valid handle */
  if (!hdma) {
    return HAL_ERROR;
  }

  /* Validate channel ID */
  if (hdma->init.channel_id >= DMA_MAX_CHANNEL_NUM) {
    hdma->errcode = DMA_ERROR_CHAN_NO_EXIST;
    return HAL_ERROR;
  }
  ch = hdma->init.channel_id;

  start_ms = hal_get_tick();

  /* Poll for half task completion */
  if (type == DMA_COMPELE_TYPE_HALF_TASK) {
    while (1) {
      val = readl(DMA_IRQ_STA_REG(hdma));

      /* Check for address request error */
      if (val & DMA_IRQ_STA_CH_ADDR_REQ_ERR_STA(ch)) {
        hdma->state = DMA_STATE_ERROR;
        hdma->errcode = DMA_ERROR_ADDRESS_REQUEST;
        return HAL_ERROR;
      }

      /* Check for half task completion interrupt */
      if (val & DMA_IRQ_STA_CH_TASK_HALF_IRQ_STA(ch))
        break;

      cur_ms = hal_get_tick();

      /* Check for timeout */
      if ((cur_ms - start_ms) > timeout) {
        hdma->errcode = DMA_ERROR_TIMEOUT;
        return HAL_TIMEOUT;
      }
    }

  } else if (type == DMA_COMPELE_TYPE_FULL_TASK) {
    /* Poll for full task completion */
    while (1) {
      val = readl(DMA_IRQ_STA_REG(hdma));

      /* Check for address request error */
      if (val & DMA_IRQ_STA_CH_ADDR_REQ_ERR_STA(ch)) {
        hdma->state = DMA_STATE_ERROR;
        hdma->errcode = DMA_ERROR_ADDRESS_REQUEST;
        return HAL_ERROR;
      }

      /* Check for full task completion interrupt */
      if (val & DMA_IRQ_STA_CH_TASK_FINISH_IRQ_STA(ch))
        break;

      cur_ms = hal_get_tick();

      /* Check for timeout */
      if ((cur_ms - start_ms) > timeout) {
        hdma->errcode = DMA_ERROR_TIMEOUT;
        return HAL_TIMEOUT;
      }
    }

  } else if (type == DMA_COMPELE_TYPE_ALL_TASKS) {
    /* Poll for all tasks completion */
    while (1) {
      val = readl(DMA_IRQ_STA_REG(hdma));

      /* Check for address request error */
      if (val & DMA_IRQ_STA_CH_ADDR_REQ_ERR_STA(ch)) {
        hdma->state = DMA_STATE_ERROR;
        hdma->errcode = DMA_ERROR_ADDRESS_REQUEST;
        return HAL_ERROR;
      }

      /* Check for all tasks completion interrupt */
      if (val & DMA_IRQ_STA_CH_ALL_FINISH_IRQ_STA(ch))
        break;

      cur_ms = hal_get_tick();

      /* Check for timeout */
      if ((cur_ms - start_ms) > timeout) {
        hdma->errcode = DMA_ERROR_TIMEOUT;
        return HAL_TIMEOUT;
      }
    }

    hdma->state = DMA_STATE_READY;
  } else {
    /* Invalid completion type */
    hdma->errcode = DMA_ERROR_PARAM;
    return HAL_ERROR;
  }

  return HAL_OK;
}

/**
 * @brief  Register callback function for DMA operations, used in interrupt mode
 * @param  hdma: Pointer to DMA handle structure
 * @param  id: Callback identifier specifying which callback to register
 * @param  cb: Callback function pointer to register
 * @retval hal_status_e: HAL status result (HAL_OK or HAL_ERROR)
 */
hal_status_e hal_dma_registger_callback(hal_dma_handle_t *hdma,
                                        hal_dma_callback_id_e id,
                                        hal_dma_generic_cb cb) {
  if (!hdma) {
    return HAL_ERROR;
  }

  HAL_LOCK(hdma);
  switch (id) {
  case DMA_CB_INIT:
    hdma->init_cb = cb;
    break;
  case DMA_CB_DEINIT:
    hdma->deinit_cb = cb;
    break;
  case DMA_CB_XFER_HALF_TASK_COMPLETE_CB:
    hdma->xfer_half_task_complete_cb = cb;
    break;
  case DMA_CB_XFER_FULL_TASK_COMPLETE_CB:
    hdma->xfer_full_task_complete_cb = cb;
    break;
  case DMA_CB_XFER_ALL_TASKS_COMPLETE_CB:
    hdma->xfer_all_tasks_complete_cb = cb;
    break;
  case DMA_CB_XFER_ERROR_CB:
    hdma->xfer_error_cb = cb;
    break;
  case DMA_CB_XFER_ABORT_CB:
    hdma->xfer_abort_cb = cb;
    break;
  default:
    hdma->errcode = DMA_ERROR_PARAM;
    HAL_UNLOCK(hdma);
    return HAL_ERROR;
  }

  HAL_UNLOCK(hdma);
  return HAL_OK;
}

/**
 * @brief Unregister (clear) callback function for DMA operations
 *
 * This function clears a previously registered callback function for the
 * specified DMA event by setting the corresponding function pointer to NULL.
 *
 * @param hdma Pointer to DMA handle structure
 * @param id Callback identifier specifying which callback to unregister:
 *
 * @retval HAL_OK if the callback was successfully unregistered
 * @retval HAL_ERROR if an invalid parameter was provided
 *
 * @note This function is typically used in interrupt mode to remove callbacks
 *       that are no longer needed.
 */
hal_status_e hal_dma_unregistger_callback(hal_dma_handle_t *hdma,
                                          hal_dma_callback_id_e id) {
  if (!hdma) {
    return HAL_ERROR;
  }

  HAL_LOCK(hdma);
  switch (id) {
  case DMA_CB_INIT:
    hdma->init_cb = NULL;
    break;
  case DMA_CB_DEINIT:
    hdma->deinit_cb = NULL;
    break;
  case DMA_CB_XFER_HALF_TASK_COMPLETE_CB:
    hdma->xfer_half_task_complete_cb = NULL;
    break;
  case DMA_CB_XFER_FULL_TASK_COMPLETE_CB:
    hdma->xfer_full_task_complete_cb = NULL;
    break;
  case DMA_CB_XFER_ALL_TASKS_COMPLETE_CB:
    hdma->xfer_all_tasks_complete_cb = NULL;
    break;
  case DMA_CB_XFER_ERROR_CB:
    hdma->xfer_error_cb = NULL;
    break;
  case DMA_CB_XFER_ABORT_CB:
    hdma->xfer_abort_cb = NULL;
    break;
  default:
    hdma->errcode = DMA_ERROR_PARAM;
    HAL_UNLOCK(hdma);
    return HAL_ERROR;
  }

  HAL_UNLOCK(hdma);
  return HAL_OK;
}

/**
 * @brief Handle DMA interrupt requests.
 *
 * This function processes DMA interrupts for a specific channel, checking the
 * interrupt status and calling the appropriate callback functions based on the
 * type of interrupt received.
 *
 * @param hdma Pointer to the DMA handle structure containing channel
 * configuration and callbacks
 */
void hal_dma_irq_handler(hal_dma_handle_t *hdma) {
  hal_dma_generic_cb cb;
  uint32_t ch, val;

  /* Validate input parameter */
  if (!hdma) {
    return;
  }

  /* Check if channel ID is valid */
  if (hdma->init.channel_id >= DMA_MAX_CHANNEL_NUM) {
    hdma->errcode = DMA_ERROR_CHAN_NO_EXIST;
    return;
  }
  ch = hdma->init.channel_id;

  /* Read and mask interrupt status for this channel */
  val = readl(DMA_IRQ_STA_REG(hdma));
  val &= DMA_IRQ_STA_CH_MASK(ch);
  if (val == 0) {
    /* IRQ not for this channel */
    return;
  }

  /* Process half task completion interrupt */
  if (val & DMA_IRQ_STA_CH_TASK_HALF_IRQ_STA(ch)) {
    cb = hdma->xfer_half_task_complete_cb;
    if (cb)
      cb(hdma);
  }

  /* Process full task completion interrupt */
  if (val & DMA_IRQ_STA_CH_TASK_FINISH_IRQ_STA(ch)) {
    cb = hdma->xfer_full_task_complete_cb;
    if (cb)
      cb(hdma);
  }

  /* Process all tasks completion interrupt */
  if (val & DMA_IRQ_STA_CH_ALL_FINISH_IRQ_STA(ch)) {
    hdma->state = DMA_STATE_READY;
    cb = hdma->xfer_all_tasks_complete_cb;
    if (cb)
      cb(hdma);
  }

  /* Process address request error interrupt */
  if (val & DMA_IRQ_STA_CH_ADDR_REQ_ERR_STA(ch)) {
    hdma->state = DMA_STATE_ERROR;
    hdma->errcode = DMA_ERROR_ADDRESS_REQUEST;
    cb = hdma->xfer_error_cb;
    if (cb)
      cb(hdma);
  }

  /* Clear interrupt status */
  writel(val, DMA_IRQ_STA_REG(hdma));
}

/**
 * @brief Fill memory with a specific 32-bit value using DMA
 *
 * This function uses DMA to initialize a specific memory space with a 32-bit
 * value. The value is filled in 32-bit units. If the length of the filled space
 * is not 32-bit aligned, the tail is filled byte by byte in little-endian
 * order.
 *
 * Requirements:
 * - start address must be 8-byte aligned
 * - length can be unaligned
 *
 * @param hdma Pointer to DMA handle structure
 * @param start Start address of memory region to fill
 * @param value 32-bit value to fill memory with
 * @param bytes Number of bytes to fill
 * @return HAL_OK on success, HAL_ERROR on failure
 *
 * @note This function is a synchronous interface that blocks until the
 * operation completes
 */
hal_status_e hal_dma_memset(hal_dma_handle_t *hdma, void *start, uint32_t value,
                            uint32_t bytes) {
  hal_dma_task_desc_t *task;
  uint32_t ch, addr, val;
  hal_status_e ret;

  if (!hdma || !start) {
    return HAL_ERROR;
  }

  addr = (uint32_t)start;
  if (addr & (~DMA_ADDR_ALIGN_MASK)) {
    hdma->errcode = DMA_ERROR_ADDRESS_ALIGN;
    return HAL_ERROR;
  }

  if (bytes > DMA_TASK_MAX_XFER_LEN) {
    hdma->errcode = DMA_ERROR_XFER_LEN;
    return HAL_ERROR;
  }

  HAL_LOCK(hdma);
  ch = hdma->init.channel_id;

  task = CACHE_ALIGN_UP(hdma->work_buffer);
  task->src = addr;
  task->dst = addr;
  task->len = bytes;

  /* Initialize all bits to 0 */
  task->cfg.val = 0;

  /* Set source bits */
  task->cfg.bits.src_dev = HAL_DMA_ID_SRAM;
  task->cfg.bits.src_burst = DMA_XFER_BURST_16;
  task->cfg.bits.src_addr_mode = DMA_ADDR_LINEAR_MODE;
  task->cfg.bits.src_data_width = DMA_DATA_WIDTH_4_BYTES;

  /* Set sink bits */
  task->cfg.bits.snk_dev = HAL_DMA_ID_SRAM;
  task->cfg.bits.snk_burst = DMA_XFER_BURST_16;
  task->cfg.bits.snk_addr_mode = DMA_ADDR_LINEAR_MODE;
  task->cfg.bits.snk_data_width = DMA_DATA_WIDTH_4_BYTES;
  task->delay = 0;
  task->next = DMA_CHANNEL_TASK_END;

  /* Clear channel's irq enable bits */
  val = readl(DMA_IRQ_EN_REG(hdma));
  val &= ~DMA_IRQ_EN_CH_MASK(ch);
  writel(val, DMA_IRQ_EN_REG(hdma));

  /* Clear channel's irq status bits */
  val = DMA_IRQ_STA_CH_MASK(ch);
  writel(val, DMA_IRQ_STA_REG(hdma));

  /* Enable DMA memset mode */
  writel(DMA_CH_PAUSE_MEM_SET_ENABLE, DMA_CH_PAUSE_REG(hdma, ch));
  writel(value, DMA_MEM_SET_REG(hdma, ch));

    csi_dcache_clean_range((phy_addr_t)task, sizeof(*task));
  writel((uint32_t)task, DMA_CH_TASK_REG(hdma, ch));

  /* Start task */
  writel(1, DMA_CH_EN_REG(hdma, ch));
  hdma->state = DMA_STATE_BUSY;

  ret = hal_dma_poll_for_transfer(hdma, DMA_COMPELE_TYPE_ALL_TASKS, 5);

  hdma->state = DMA_STATE_READY;
  HAL_UNLOCK(hdma);
  return ret;
}

/**
 * @brief Get the current state of the DMA channel
 *
 * This function returns the current state of the DMA channel.
 *
 * @param hdma Pointer to DMA handle structure
 * @return DMA_STATE_READY if the DMA channel is ready, DMA_STATE_BUSY if the
 * DMA channel is busy, DMA_STATE_ERROR if an error has occurred
 */
hal_dma_state_e hal_dma_get_state(hal_dma_handle_t *hdma) {
  if (!hdma) {
    return DMA_STATE_INVALID;
  }

  return hdma->state;
}

/**
 * @brief Get the error code of the DMA channel
 *
 * This function returns the error code of the DMA channel.
 *
 * @param hdma Pointer to DMA handle structure
 * @return Error code of the DMA channel
 */
hal_dma_error_e hal_dma_get_error(hal_dma_handle_t *hdma) {
  if (!hdma) {
    return DMA_ERROR_INVALID;
  }

  return hdma->errcode;
}

hal_status_e hal_dma_get_residue(hal_dma_handle_t *hdma, uint32_t *left_size) {
  uint32_t ch;

  if (!hdma || !hdma->regbase || !left_size) {
    return HAL_ERROR;
  }

  HAL_LOCK(hdma);
  ch = hdma->init.channel_id;

  *left_size = readl(DMA_BCNT_LEFT_REG(hdma, ch));

  HAL_UNLOCK(hdma);
  return HAL_OK;
}

#ifdef __cplusplus
}
#endif