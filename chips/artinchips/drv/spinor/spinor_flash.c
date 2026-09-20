/*
 * Copyright (c) 2022-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Wu Dehuang <dehuang.wu@artinchip.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sfud.h>
#include <aic_common.h>
#include <aic_core.h>
#include <aic_soc.h>
#include <aic_log.h>
#include <aic_hal.h>
#include <hal_qspi.h>
#include <spinor_port.h>
#include <hal_dma.h>
#include <aic_dma_id.h>
#include <aic_clk_id.h>
#include <aic_mtd.h>
#include <aic_osal.h>
#include <spienc.h>
#include <debug.h>
// #include <fs.h>

#ifdef IMAGE_CFG_JSON_PARTS_MTD
#define NOR_MTD_PARTS IMAGE_CFG_JSON_PARTS_MTD
#else
#define NOR_MTD_PARTS ""
#endif

#define SFUD_READ_SFDP_FREQ 50000000

#define QSPI_MAX_CNT 4

static struct aic_qspi_bus qspi_bus_arr[] = {
#if defined(CONFIG_AIC_USING_QSPI0) && defined(CONFIG_AIC_QSPI0_DEVICE_SPINOR)
    {
        .name = "qspi0",
        .idx = 0,
        .clk_id = CLK_QSPI0,
        .clk_in_hz = CONFIG_AIC_QSPI0_DEVICE_SPINOR_FREQ,
        .bus_hz = CONFIG_AIC_QSPI0_DEVICE_SPINOR_FREQ,
        .dma_port_id = DMA_ID_SPI0,
        .irq_num = QSPI0_IRQn,
        .dl_width = CONFIG_AIC_QSPI0_BUS_WIDTH,
#if defined(CONFIG_AIC_QSPI_MULTIPLE_CS_NUM)
        .cs_num = AIC_QSPI0_CS_NUM,
#endif
        .rxd_dylmode = CONFIG_AIC_DEV_QSPI0_DELAY_MODE,
#if defined(CONFIG_AIC_QSPI_DRV_V20)
        .txd_dylmode = AIC_DEV_QSPI0_TXD_DELAY_MODE,
        .txc_dylmode = AIC_DEV_QSPI0_TX_CLK_DELAY_MODE,
#endif
    },
#endif
#if defined(CONFIG_AIC_USING_QSPI1) && defined(CONFIG_AIC_QSPI1_DEVICE_SPINOR)
    {
        .name = "qspi1",
        .idx = 1,
        .clk_id = CLK_QSPI1,
        .clk_in_hz = AIC_DEV_QSPI1_MAX_SRC_FREQ_HZ,
        .bus_hz = AIC_QSPI1_DEVICE_SPINOR_FREQ,
        .dma_port_id = DMA_ID_SPI1,
        .irq_num = QSPI1_IRQn,
        .dl_width = AIC_QSPI1_BUS_WIDTH,
#if defined(CONFIG_AIC_QSPI_MULTIPLE_CS_NUM)
        .cs_num = AIC_QSPI1_CS_NUM,
#endif
        .rxd_dylmode = AIC_DEV_QSPI1_DELAY_MODE,
#if defined(CONFIG_AIC_QSPI_DRV_V20)
        .txd_dylmode = AIC_DEV_QSPI1_TXD_DELAY_MODE,
        .txc_dylmode = AIC_DEV_QSPI1_TX_CLK_DELAY_MODE,
#endif
    },
#endif
#if defined(CONFIG_AIC_USING_QSPI2) && defined(CONFIG_AIC_QSPI2_DEVICE_SPINOR)
    {
        .name = "qspi2",
        .idx = 2,
        .clk_id = CLK_QSPI2,
        .clk_in_hz = AIC_DEV_QSPI2_MAX_SRC_FREQ_HZ,
        .bus_hz = AIC_QSPI2_DEVICE_SPINOR_FREQ,
        .dma_port_id = DMA_ID_SPI2,
        .irq_num = QSPI2_IRQn,
        .dl_width = AIC_QSPI2_BUS_WIDTH,
#if defined(CONFIG_AIC_QSPI_MULTIPLE_CS_NUM)
        .cs_num = AIC_QSPI2_CS_NUM,
#endif
        .rxd_dylmode = AIC_DEV_QSPI2_DELAY_MODE,
#if defined(CONFIG_AIC_QSPI_DRV_V20)
        .txd_dylmode = AIC_DEV_QSPI2_TXD_DELAY_MODE,
        .txc_dylmode = AIC_DEV_QSPI2_TX_CLK_DELAY_MODE,
#endif
    },
#endif
#if defined(CONFIG_AIC_USING_QSPI3)
    {
        .name = "qspi3",
        .idx = 3,
        .clk_id = CLK_QSPI3,
        .clk_in_hz = AIC_DEV_QSPI3_MAX_SRC_FREQ_HZ,
        .bus_hz = AIC_QSPI3_DEVICE_SPINOR_FREQ,
        .dma_port_id = DMA_ID_SPI3,
        .irq_num = QSPI3_IRQn,
        .dl_width = AIC_QSPI3_BUS_WIDTH,
#if defined(CONFIG_AIC_QSPI_MULTIPLE_CS_NUM)
        .cs_num = AIC_QSPI3_CS_NUM,
#endif
        .rxd_dylmode = AIC_DEV_QSPI3_DELAY_MODE,
#if defined(CONFIG_AIC_QSPI_DRV_V20)
        .txd_dylmode = AIC_DEV_QSPI3_TXD_DELAY_MODE,
        .txc_dylmode = AIC_DEV_QSPI3_TX_CLK_DELAY_MODE,
#endif
    },
#endif
#if defined(CONFIG_AIC_USING_QSPI4)
    {
        .name = "qspi4",
        .idx = 4,
        .clk_id = CLK_QSPI4,
        .clk_in_hz = AIC_DEV_QSPI4_MAX_SRC_FREQ_HZ,
        .bus_hz = AIC_QSPI4_DEVICE_SPINOR_FREQ,
        .dma_port_id = DMA_ID_SPI4,
        .irq_num = QSPI4_IRQn,
        .dl_width = AIC_QSPI4_BUS_WIDTH,
#if defined(CONFIG_AIC_QSPI_MULTIPLE_CS_NUM)
        .cs_num = AIC_QSPI4_CS_NUM,
#endif
        .rxd_dylmode = AIC_DEV_QSPI4_DELAY_MODE,
#if defined(CONFIG_AIC_QSPI_DRV_V20)
        .txd_dylmode = AIC_DEV_QSPI4_TXD_DELAY_MODE,
        .txc_dylmode = AIC_DEV_QSPI4_TX_CLK_DELAY_MODE,
#endif
    },
#endif
#if defined(CONFIG_AIC_USING_SE_SPI) && defined(CONFIG_AIC_SE_SPI_DEVICE_SPINOR)
    {
        .name = "sespi",
        .idx = 5,
        .clk_id = CLK_SE_SPI,
        .clk_in_hz = AIC_DEV_SE_SPI_MAX_SRC_FREQ_HZ,
        .bus_hz = AIC_SE_SPI_DEVICE_SPINOR_FREQ,
        .dma_port_id = DMA_ID_SE_SPI,
        .irq_num = SE_SPI_IRQn,
        .dl_width = AIC_SE_SPI_BUS_WIDTH,
#if defined(CONFIG_AIC_QSPI_MULTIPLE_CS_NUM)
        .cs_num = AIC_SE_SPI_CS_NUM,
#endif
        .rxd_dylmode = AIC_DEV_SE_SPI_DELAY_MODE,
        .txd_dylmode = AIC_DEV_SE_SPI_TXD_DELAY_MODE,
        .txc_dylmode = AIC_DEV_SE_SPI_TX_CLK_DELAY_MODE,
    },
#endif
};

char *aic_spinor_get_partition_string(struct mtd_dev *mtd);
static void retry_delay_100us(void) { aicos_udelay(100); }

static struct aic_qspi_bus *get_qspi_by_index(unsigned int idx) {
  struct aic_qspi_bus *qspi;
  unsigned int i;

  qspi = NULL;
  for (i = 0; i < ARRAY_SIZE(qspi_bus_arr); i++) {
    if (qspi_bus_arr[i].idx == idx) {
      qspi = &qspi_bus_arr[i];
      break;
    }
  }

  return qspi;
}

static unsigned int address_copy(unsigned int addr, unsigned int size,
                                 unsigned char *dst) {
  unsigned int i;

  i = 0;
  while (size) {
    dst[i++] = (addr >> (8 * (size - 1))) & 0xFF;
    size--;
  }
  return i;
}

static sfud_err qspi_read(const struct __sfud_spi *spi, unsigned int addr,
                          sfud_qspi_read_cmd_format *rd_fmt,
                          unsigned char *read_buf, size_t read_size) {
  struct aic_qspi_bus *qspi;
  struct qspi_transfer t;
  unsigned char cmdbuf1[16];
  unsigned char cmdbuf2[16];
  unsigned int addrsiz, cs_num = 0;
  int ret, single_cnt, rest_cnt, dummy_cnt, rest_buswidth;

  qspi = (struct aic_qspi_bus *)spi->user_data;

  single_cnt = 0;
  rest_cnt = 0;
  rest_buswidth = HAL_QSPI_BUS_WIDTH_SINGLE;
  dummy_cnt = (rd_fmt->address_lines * rd_fmt->dummy_cycles / 8);
  if (rd_fmt->instruction_lines == 1) {
    cmdbuf1[single_cnt] = rd_fmt->instruction;
    single_cnt++;
  } else if (rd_fmt->instruction_lines > 1) {
    cmdbuf2[rest_cnt] = rd_fmt->instruction;
    rest_cnt++;
    if (rd_fmt->instruction_lines > rest_buswidth)
      rest_buswidth = rd_fmt->instruction_lines;
  }

  addrsiz = rd_fmt->address_size / 8;
  if (rd_fmt->address_lines == 1) {
    single_cnt += address_copy(addr, addrsiz, &cmdbuf1[single_cnt]);
    memset(&cmdbuf1[single_cnt], 0, dummy_cnt);
    single_cnt += dummy_cnt;
  } else if (rd_fmt->address_lines > 1) {
    rest_cnt += address_copy(addr, addrsiz, &cmdbuf2[rest_cnt]);
    memset(&cmdbuf2[rest_cnt], 0, dummy_cnt);
    rest_cnt += dummy_cnt;
    if (rd_fmt->address_lines > rest_buswidth)
      rest_buswidth = rd_fmt->address_lines;
  }

#if defined(CONFIG_AIC_QSPI_MULTIPLE_CS_NUM)
  cs_num = qspi->cs_num;
#endif
  hal_qspi_master_set_cs(&qspi->handle, cs_num, true);

  /* Command phase */
  if (single_cnt) {
    hal_qspi_master_set_bus_width(&qspi->handle, HAL_QSPI_BUS_WIDTH_SINGLE);
    t.rx_data = NULL;
    t.tx_data = cmdbuf1;
    t.data_len = single_cnt;
    ret = hal_qspi_master_transfer_sync(&qspi->handle, &t);
    if (ret)
      goto out;
  }
  if (rest_cnt) {
    hal_qspi_master_set_bus_width(&qspi->handle, rest_buswidth);
    t.rx_data = NULL;
    t.tx_data = cmdbuf2;
    t.data_len = rest_cnt;
    ret = hal_qspi_master_transfer_sync(&qspi->handle, &t);
    if (ret)
      goto out;
  }

  /* Read data phase */

  hal_qspi_master_set_bus_width(&qspi->handle, rd_fmt->data_lines);
  t.rx_data = read_buf;
  t.tx_data = NULL;
  t.data_len = read_size;
#if defined(CONFIG_AIC_SPIENC_DRV)
  spienc_set_cfg(qspi->idx, addr, 0, read_size);
  spienc_start();
#endif
  ret = hal_qspi_master_transfer_sync(&qspi->handle, &t);
#if defined(CONFIG_AIC_SPIENC_DRV)
  spienc_stop();
  if (spienc_check_empty())
    memset(read_buf, 0xFF, read_size);
#endif
out:
#if defined(CONFIG_AIC_QSPI_MULTIPLE_CS_NUM)
  cs_num = qspi->cs_num;
#endif
  hal_qspi_master_set_cs(&qspi->handle, cs_num, false);
  return ret;
}

static sfud_err spi_set_speed(const struct __sfud_spi *spi, uint32_t bus_hz) {
  struct aic_qspi_bus *qspi;

  qspi = (struct aic_qspi_bus *)spi->user_data;

  if (qspi == NULL)
    return SFUD_ERR_NOT_FOUND;
  hal_qspi_master_set_bus_freq(&qspi->handle, bus_hz);
  return SFUD_SUCCESS;
}

static sfud_err spi_get_bus_id(const struct __sfud_spi *spi, uint32_t *bus_id) {
  struct aic_qspi_bus *qspi;

  qspi = (struct aic_qspi_bus *)spi->user_data;

  if (qspi == NULL)
    return SFUD_ERR_NOT_FOUND;

  *bus_id = qspi->idx;

  return SFUD_SUCCESS;
}

static void spi_set_rx_delay(const struct __sfud_spi *spi, uint32_t mode) {
  struct aic_qspi_bus *qspi;

  qspi = (struct aic_qspi_bus *)spi->user_data;

  if (qspi == NULL)
    return;
  hal_qspi_master_set_rxdelay_mode(&qspi->handle, mode);
}

static sfud_err spi_write_read(const struct __sfud_spi *spi,
                               const unsigned char *write_buf,
                               size_t write_size, unsigned char *read_buf,
                               size_t read_size) {
  struct aic_qspi_bus *qspi;
  struct qspi_transfer t;
  int ret = 0;
  unsigned int cs_num = 0;

  qspi = (struct aic_qspi_bus *)spi->user_data;

  hal_qspi_master_set_bus_width(&qspi->handle, HAL_QSPI_BUS_WIDTH_SINGLE);
#if defined(CONFIG_AIC_QSPI_MULTIPLE_CS_NUM)
  cs_num = qspi->cs_num;
#endif
  hal_qspi_master_set_cs(&qspi->handle, cs_num, true);
  if (write_size) {
    t.rx_data = NULL;
    t.tx_data = (unsigned char *)write_buf;
    t.data_len = write_size;
    ret = hal_qspi_master_transfer_sync(&qspi->handle, &t);
    if (ret < 0)
      goto out;
  }
  if (read_size) {
    t.rx_data = read_buf;
    t.tx_data = NULL;
    t.data_len = read_size;
    ret = hal_qspi_master_transfer_sync(&qspi->handle, &t);
  }
out:
#if defined(CONFIG_AIC_QSPI_MULTIPLE_CS_NUM)
  cs_num = qspi->cs_num;
#endif
  hal_qspi_master_set_cs(&qspi->handle, cs_num, false);
  return ret;
}

sfud_err spi_port_init(sfud_flash *flash) {
  sfud_err result = SFUD_SUCCESS;

  /* port SPI device interface */
  flash->spi.wr = spi_write_read;
  flash->spi.set_speed = spi_set_speed;
  flash->spi.get_bus_id = spi_get_bus_id;
  flash->spi.set_rx_delay = spi_set_rx_delay;
#ifdef CONFIG_SFUD_USING_QSPI
  flash->spi.qspi_read = (void *)qspi_read;
#endif
  flash->spi.lock = NULL;
  flash->spi.unlock = NULL;
  flash->spi.user_data = flash->user_data;
  /* 100 microsecond delay */
  flash->retry.delay = retry_delay_100us;
  /* 60 seconds timeout */
  flash->retry.times = 60 * 10000;

  return result;
}

static int sfud_mtd_read(struct mtd_dev *mtd, u32 offset, u8 *data, u32 len) {
  sfud_flash *flash;
  unsigned int start, dolen;

  if ((!mtd) || (!mtd->priv))
    return -1;
  start = mtd->start + offset;
  dolen = len;
  if ((mtd->size - offset) < dolen)
    dolen = mtd->size - offset;
  flash = mtd->priv;
  return sfud_read(flash, start, dolen, data);
}

static int sfud_mtd_erase(struct mtd_dev *mtd, u32 offset, u32 len) {
  sfud_flash *flash;
  unsigned int start, dolen;

  if ((!mtd) || (!mtd->priv))
    return -1;
  start = mtd->start + offset;
  dolen = len;
  if ((mtd->size - offset) < dolen)
    dolen = mtd->size - offset;
  flash = mtd->priv;
  return sfud_erase(flash, start, dolen);
}

static int sfud_mtd_write(struct mtd_dev *mtd, u32 offset, u8 *data, u32 len) {
  sfud_flash *flash;
  unsigned int start, dolen;

  if ((!mtd) || (!mtd->priv))
    return -1;
  start = mtd->start + offset;
  dolen = len;
  if ((mtd->size - offset) < dolen)
    dolen = mtd->size - offset;
  flash = mtd->priv;
  return sfud_write(flash, start, dolen, data);
}

/* NuttX 标准 MTD 操作函数 */
static int sfud_mtd_erase_nuttx(FAR struct mtd_dev_s *dev, off_t startblock,
                                size_t nblocks) {
  struct mtd_dev *aic_mtd = (struct mtd_dev *)dev; // 强制类型转换
  if (!aic_mtd ||
      (aic_mtd->size / aic_mtd->erasesize < (startblock + nblocks))) {
    nor_err("sfud_mtd_erase_nuttx: startblock=%ld, nblocks=%d, size=%d\n",
            startblock, nblocks, aic_mtd->size);
    nor_info("%d sfud_mtd_erase_nuttx: offset=%ld, size=%lu\n", __LINE__,
             startblock * aic_mtd->erasesize, nblocks * aic_mtd->erasesize);
    return -EINVAL;
  }
  sfud_err ret = SFUD_SUCCESS;
  ret = sfud_mtd_erase(aic_mtd, startblock * aic_mtd->erasesize,
                       nblocks * aic_mtd->erasesize);
  if (ret != SFUD_SUCCESS) {
    nor_err("sfud_mtd_erase_nuttx:  error ret=%d\n", ret);
    return -EINVAL;
  } else
    return nblocks;
}

static ssize_t sfud_mtd_bread_nuttx(FAR struct mtd_dev_s *dev, off_t startblock,
                                    size_t nblocks, FAR unsigned char *buffer) {
  struct mtd_dev *aic_mtd = (struct mtd_dev *)dev; // 强制类型转换
  sfud_err ret = SFUD_SUCCESS;
  if (!aic_mtd)
    return -1;
  unsigned int offset = startblock * 512; /* 假设块大小为512字节 */
  unsigned int len = nblocks * 512;
  ret = sfud_mtd_read(aic_mtd, offset, buffer, len);
  if (ret != SFUD_SUCCESS)
    return -1;
  else
    return nblocks;
}

static ssize_t sfud_mtd_bwrite_nuttx(FAR struct mtd_dev_s *dev,
                                     off_t startblock, size_t nblocks,
                                     FAR const unsigned char *buffer) {
  struct mtd_dev *aic_mtd = (struct mtd_dev *)dev; // 强制类型转换
  sfud_err ret = SFUD_SUCCESS;
  if (!aic_mtd)
    return -1;
  unsigned int offset = startblock * 512; /* 假设块大小为512字节 */
  unsigned int len = nblocks * 512;
  ret = sfud_mtd_write(aic_mtd, offset, (unsigned char *)buffer, len);
  if (ret != SFUD_SUCCESS)
    return -1;
  else
    return nblocks;
}

static ssize_t sfud_mtd_read_nuttx(FAR struct mtd_dev_s *dev, off_t offset,
                                   size_t nbytes, FAR unsigned char *buffer) {
  struct mtd_dev *aic_mtd = (struct mtd_dev *)dev; // 强制类型转换
  sfud_err ret = SFUD_SUCCESS;
  if ((!aic_mtd))
    return -1;
  ret = sfud_mtd_read(aic_mtd, offset, buffer, nbytes);
  if (ret != SFUD_SUCCESS)
    return -1;
  else
    return nbytes;
}

__attribute__((unused)) static ssize_t
sfud_mtd_write_nuttx(FAR struct mtd_dev_s *dev, off_t offset, size_t nbytes,
                     FAR const uint8_t *buffer) {
  struct mtd_dev *aic_mtd = (struct mtd_dev *)dev; // 强制类型转换
  sfud_err ret = SFUD_SUCCESS;
  if (!aic_mtd)
    return -1;
  ret = sfud_mtd_write(aic_mtd, offset, (u8 *)buffer, nbytes);
  if (ret != SFUD_SUCCESS)
    return -1;
  else
    return nbytes;
}

static int sfud_mtd_ioctl_nuttx(FAR struct mtd_dev_s *dev, int cmd,
                                unsigned long arg) {
  struct mtd_dev *aic_mtd = (struct mtd_dev *)dev; // 强制类型转换
  switch (cmd) {
  case MTDIOC_GEOMETRY: {
    FAR struct mtd_geometry_s *geo = (FAR struct mtd_geometry_s *)arg;
    if (geo) {
      geo->blocksize = 512;
      geo->erasesize = aic_mtd->erasesize;
      geo->neraseblocks = aic_mtd->size / aic_mtd->erasesize;
      return OK;
    }
    break;
  }
  case MTDIOC_BULKERASE:
    return sfud_mtd_erase(aic_mtd, 0x00, aic_mtd->size);
  case MTDIOC_ERASESTATE: {
    unsigned char *buf = (unsigned char *)arg;
    sfud_mtd_read_nuttx(dev, 0x00, 1, buf);
    return OK;
  }
  default:
    break;
  }
  return -ENOTTY;
}

static int sfud_mtd_isbad_nuttx(FAR struct mtd_dev_s *dev, off_t block) {
  sfud_flash *flash;
  struct mtd_dev *aic_mtd = (struct mtd_dev *)dev;
  if (!aic_mtd || !aic_mtd->priv) {
    return -EINVAL;
  }

  flash = (sfud_flash *)aic_mtd->priv;

  /* SPI NOR 闪存通常没有坏块概念，直接返回 GOODBLOCK */
  /* 如果需要实现坏块检测，可以检查特定地址的坏块标记 */

  /* 检查闪存是否支持坏块检测 */
  if (flash->chip.capacity <= 0) {
    return -EIO;
  }

  /* 计算物理块地址 */
  // off_t physical_block = block;

  /* SPI NOR 闪存通常没有坏块，直接返回 GOODBLOCK */
  /* 如果闪存支持坏块管理，可以在这里实现检测逻辑 */

  /* 示例：检查特定块是否为坏块 */
  /*
  unsigned char buf[256];
  off_t offset = physical_block * flash->chip.erase_gran;

  if (sfud_read(flash, offset, 256, buf) != SFUD_SUCCESS) {
      return -EIO;  // 读取失败，可能是坏块
  }

  // 检查坏块标记（具体标记取决于闪存型号）
  // if (buf[0] == 0xFF && buf[1] == 0xFF) {  // 示例：全FF表示好块
  //     return 0;  // GOODBLOCK
  // } else {
  //     return 1;  // BADBLOCK
  // }
  */

  /* 默认返回 GOODBLOCK */
  return 0; // GOODBLOCK
}

__attribute__((unused)) static int
sfud_mtd_markbad_nuttx(FAR struct mtd_dev_s *dev, off_t block) {
  sfud_flash *flash;
  struct mtd_dev *aic_mtd = (struct mtd_dev *)dev;
  if (!aic_mtd || !aic_mtd->priv) {
    return -EINVAL;
  }

  flash = (sfud_flash *)aic_mtd->priv;

  /* SPI NOR 闪存通常不支持坏块标记，但可以模拟实现 */
  /* 检查闪存是否有效 */
  if (flash->chip.capacity <= 0) {
    return -EIO;
  }

  /* 计算物理块地址 */
  off_t physical_block = block;
  off_t offset = physical_block * flash->chip.erase_gran;

  /* 检查块地址是否有效 */
  if (offset >= flash->chip.capacity) {
    return -EINVAL;
  }

  /* SPI NOR 闪存通常没有坏块标记功能，这里模拟实现 */
  /* 实际应用中，SPI NOR 闪存很少需要坏块标记 */

  /* 可选：在特定位置写入坏块标记 */
  /*
  unsigned char bad_block_marker[] = {0xBA, 0xDB, 0xAD, 0x00};  // 坏块标记

  // 尝试在块的开头写入坏块标记
  sfud_err result = sfud_write(flash, offset, sizeof(bad_block_marker),
  bad_block_marker); if (result != SFUD_SUCCESS) { nor_err("Failed to mark block
  %ld as bad at offset 0x%lx\n", block, offset); return -EIO;
  }

  nor_info("Block %ld marked as bad at offset 0x%lx\n", block, offset);
  */

  /* 对于 SPI NOR 闪存，通常只需要记录坏块信息，不需要物理标记 */
  /* 可以在这里实现坏块信息记录逻辑 */

  nor_info("Block %ld marked as bad (simulated for SPI NOR)\n", block);

  return 0; // 成功
}

sfud_flash *spinor_init(unsigned int spi_bus) {
  sfud_err result = SFUD_SUCCESS;
  struct aic_qspi_bus *qspi;
  struct mtd_dev *mtd;
  struct mtd_partition *part, *p;
  int ret;
  struct qspi_master_config cfg = {0};
  char *partstr;
  nor_info("spinor_init is start.");
  qspi = get_qspi_by_index(spi_bus);
  if (!qspi) {
    nor_err("spi bus is invalid: %d\n", spi_bus);
    return NULL;
  }

  if ((qspi->probe_flag) && (qspi->attached_flash.init_ok))
    return &qspi->attached_flash;

  memset(&cfg, 0, sizeof(cfg));
  cfg.idx = qspi->idx;
  cfg.clk_in_hz = qspi->clk_in_hz;
  cfg.clk_id = qspi->clk_id;
  cfg.cpol = HAL_QSPI_CPOL_ACTIVE_HIGH;
  cfg.cpha = HAL_QSPI_CPHA_FIRST_EDGE;
  cfg.cs_polarity = HAL_QSPI_CS_POL_VALID_LOW;
  cfg.rx_dlymode = qspi->rxd_dylmode;
  cfg.tx_dlymode = aic_convert_tx_dlymode(qspi->txc_dylmode, qspi->txd_dylmode);

  ret = hal_qspi_master_init(&qspi->handle, &cfg);
  if (ret < 0) {
    nor_err("hal_qspi_master_init failed. ret %d\n", ret);
    return NULL;
  }

#ifdef CONFIG_AIC_DMA_DRV
  struct qspi_master_dma_config dmacfg;
  memset(&dmacfg, 0, sizeof(dmacfg));
  dmacfg.port_id = qspi->dma_port_id;

  ret = hal_qspi_master_dma_config(&qspi->handle, &dmacfg);
  if (ret) {
    nor_err("qspi dma config failed.\n");
    return NULL;
  }

  qspi->probe_flag = true;
#endif
  qspi->attached_flash.user_data = (void *)qspi;
#ifdef CONFIG_SFUD_USING_SFDP
  qspi->attached_flash.init_hz = SFUD_READ_SFDP_FREQ;
#endif
  qspi->attached_flash.bus_hz = qspi->bus_hz;

  result = spi_port_init(&qspi->attached_flash);
  if (result != SFUD_SUCCESS) {
    nor_err("spi_port_init failed: ret %d\n", result);
    return NULL;
  }

  result = sfud_device_init(&qspi->attached_flash);
  if (result != SFUD_SUCCESS) {
    nor_err("sfud_device_init failed: ret %d\n", result);
    return NULL;
  }
  nor_info("spinor_init success. get %d %d\n",
           qspi->attached_flash.chip.capacity,
           qspi->attached_flash.chip.erase_gran);

#ifdef CONFIG_SFUD_USING_QSPI
  sfud_qspi_fast_read_enable(&qspi->attached_flash, qspi->dl_width);
#endif

  mtd = malloc(sizeof(*mtd));
  mtd->name = strdup("/dev/nor0");
  mtd->name[3] += spi_bus;
  mtd->start = 0;
  mtd->size = qspi->attached_flash.chip.capacity;
  mtd->erasesize = qspi->attached_flash.chip.erase_gran;
  mtd->dev_ops.bread = sfud_mtd_bread_nuttx;

  mtd->dev_ops.bwrite = sfud_mtd_bwrite_nuttx;
  mtd->dev_ops.read = sfud_mtd_read_nuttx;
#ifdef CONFIG_MTD_BYTE_WRITE
  mtd->dev_ops.write = sfud_mtd_write_nuttx;
#endif
  mtd->dev_ops.ioctl = sfud_mtd_ioctl_nuttx;
  mtd->dev_ops.erase = sfud_mtd_erase_nuttx;
  mtd->dev_ops.isbad = sfud_mtd_isbad_nuttx;
  mtd->ops.erase = sfud_mtd_erase;
  mtd->ops.read = sfud_mtd_read;
  mtd->ops.write = sfud_mtd_write;
  mtd->priv = &qspi->attached_flash;
  ret = mtd_add_device(mtd);
  ret = register_mtddriver(mtd->name, &mtd->dev_ops, 0666, mtd);
  if (ret < 0) {
    nor_err("register_mtddriver failed: ret %d\n", ret);
    free(mtd->name);
    free(mtd);
    return NULL;
  }
  partstr = aic_spinor_get_partition_string(mtd);
  part = mtd_parts_parse(partstr, spi_bus);
  free(partstr);
  p = part;
  while (p) {
#ifdef CONFIG_IMAGE_CFG_JSON_PARTS_LEVELX
    if (partition_levelx_is_exist(p->name, CONFIG_IMAGE_CFG_JSON_PARTS_LEVELX))
      p->attr = PART_ATTR_LEVELX;
    else
#endif
      p->attr = PART_ATTR_MTD;
    mtd = malloc(sizeof(*mtd));
    mtd->name = strdup(p->name);
    mtd->start = p->start;
    mtd->size = p->size;
    mtd->erasesize = qspi->attached_flash.chip.erase_gran;
    if (p->size == 0)
      mtd->size = qspi->attached_flash.chip.capacity - p->start;

    mtd->dev_ops.bread = sfud_mtd_bread_nuttx;
    mtd->dev_ops.bwrite = sfud_mtd_bwrite_nuttx;
    mtd->dev_ops.read = sfud_mtd_read_nuttx;
#ifdef CONFIG_MTD_BYTE_WRITE
    mtd->dev_ops.write = sfud_mtd_write_nuttx;
#endif
    mtd->dev_ops.ioctl = sfud_mtd_ioctl_nuttx;
    mtd->dev_ops.erase = sfud_mtd_erase_nuttx;
    mtd->dev_ops.isbad = sfud_mtd_isbad_nuttx;
    mtd->ops.erase = sfud_mtd_erase;
    mtd->ops.read = sfud_mtd_read;
    mtd->ops.write = sfud_mtd_write;
    mtd->priv = &qspi->attached_flash;
    mtd->attr = p->attr;

    // 修正方案：增大缓冲区并优化处理
    char devname[MAX_MTD_NAME+6]; // 增大缓冲区到32字节

    // 计算最大可用的分区名长度
    int max_name_len = sizeof(devname) - strlen("/dev/") - 1; // 减1为null终止符

    if (strlen(p->name) >= max_name_len) {
      // 分区名过长，进行截断处理
      snprintf(devname, sizeof(devname), "/dev/%.*s", max_name_len, p->name);
      nor_info("Partition name truncated: original='%s', used='%s'\n", p->name,
               devname);
    } else {
      snprintf(devname, sizeof(devname), "/dev/%s", p->name);
    }

    ret = mtd_add_device(mtd);
    ret = register_mtddriver(devname, &mtd->dev_ops, 0666, mtd);
    nor_info("register_mtddriver %s success ret = %d. start 0x%x, size %d "
             "erase_gran = %d %p\n",
             devname, ret, mtd->start, mtd->size,
             qspi->attached_flash.chip.erase_gran, mtd);
    if (ret < 0) {
      nor_err("mtd_add_device failed: ret %d\n", ret);
      free(mtd->name);
      free(mtd);
      continue;
    }
    p = p->next;
  }
  nor_info("mtd_add_device success. total %s\n", part);
  if (part)
    mtd_parts_free(part);

  qspi->probe_flag = true;
  return &qspi->attached_flash;
}