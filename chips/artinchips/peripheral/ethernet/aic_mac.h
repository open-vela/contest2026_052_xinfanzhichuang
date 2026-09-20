/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __AIC_MAC_H
#define __AIC_MAC_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <nuttx/arch.h>

#include <chip.h>

#include <aic_soc.h>

#include "aic_mac_reg.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAX_ETH_MAC_PORT
#define MAX_ETH_MAC_PORT 1
#endif

#ifndef AIC_GMAC0_REG_BASE
#define AIC_GMAC0_REG_BASE 0x10280000UL
#endif

#ifndef AIC_GMAC0_IRQ
#define AIC_GMAC0_IRQ (RISCV_IRQ_ASYNC + 39)
#endif

#define MAC(port, member) \
  ((unsigned long)&(((aicmac_reg_t *)mac_base[port])->member))

#define ENABLE  true
#define DISABLE false
#define SET     true
#define RESET   false

#define ETH_ERROR   (-1)
#define ETH_SUCCESS 0

#define ETH_MAX_PACKET_SIZE 1524
#define ETH_CRC             4

#ifndef ETH_RX_BUF_SIZE
#define ETH_RX_BUF_SIZE ETH_MAX_PACKET_SIZE
#endif

#ifndef ETH_RXBUFNB
#define ETH_RXBUFNB 8
#endif

#ifndef ETH_TX_BUF_SIZE
#define ETH_TX_BUF_SIZE ETH_MAX_PACKET_SIZE
#endif

#ifndef ETH_TXBUFNB
#define ETH_TXBUFNB 8
#endif

#define AICMAC_ALIGN_UP(x, a) ((((x) + (a) - 1) / (a)) * (a))
#define AICMAC_DMA_BUF_SIZE AICMAC_ALIGN_UP(ETH_MAX_PACKET_SIZE, CACHE_LINE_SIZE)

typedef enum
{
  MODE_RMII = 0,
  MODE_RGMII = 1,
} aicmac_mii_mode_t;

typedef struct
{
  uint32_t autonegotiation : 1;
  uint32_t duplex          : 1;
  uint32_t flowctl         : 1;
  uint32_t mac_loopback    : 1;
  uint32_t phy_loopback    : 1;
  uint32_t coe_rx          : 1;
  uint32_t coe_tx          : 1;
  uint32_t dma_rxpbl       : 6;
  uint32_t dma_txpbl       : 6;
  uint32_t dma_pblx8       : 1;
  uint32_t dma_fixed_burst : 1;
  uint32_t dma_mixed_burst : 1;
  uint32_t dma_aal         : 1;
  uint32_t dma_sf_mode     : 1;
  uint32_t dma_fifo_rxth   : 2;
  uint32_t dma_fifo_txth   : 3;
  uint32_t rgmii_bus       : 1;
  uint16_t phyaddr;
  uint32_t port;
  uint32_t max_speed;
  const char *phyrst_gpio_name;
} aicmac_config_t;

typedef struct
{
  uint32_t length;
  uint32_t buffer;
  aicmac_dma_desc_t *descriptor;
  aicmac_dma_desc_t *last_desc;
} aicmac_frame_t;

typedef struct
{
  aicmac_dma_desc_t *first_desc;
  aicmac_dma_desc_t *last_desc;
  uint32_t seg_cnt;
} aicmac_rx_frame_info_t;

typedef struct
{
  aicmac_dma_desc_t rx_desc_tbl[ETH_RXBUFNB]
    __attribute__((aligned(CACHE_LINE_SIZE)));
  aicmac_dma_desc_t tx_desc_tbl[ETH_TXBUFNB]
    __attribute__((aligned(CACHE_LINE_SIZE)));
  uint8_t rx_buff[ETH_RXBUFNB][AICMAC_DMA_BUF_SIZE]
    __attribute__((aligned(CACHE_LINE_SIZE)));
  uint8_t tx_buff[ETH_TXBUFNB][AICMAC_DMA_BUF_SIZE]
    __attribute__((aligned(CACHE_LINE_SIZE)));

  aicmac_dma_desc_t *tx_desc_p;
  aicmac_dma_desc_t *rx_desc_p;
  aicmac_dma_desc_t *rx_desc_received_p;
  aicmac_dma_desc_t *rx_desc_unrelease_p;

  aicmac_rx_frame_info_t rx_frame_info;
  aicmac_rx_frame_info_t *rx_frame_info_p;
} aicmac_dma_desc_ctl_t;

#define PHY_READ_TO  ((uint32_t)0x0004ffff)
#define PHY_WRITE_TO ((uint32_t)0x0004ffff)

extern unsigned long mac_base[MAX_ETH_MAC_PORT];
extern unsigned long mac_irq[MAX_ETH_MAC_PORT];
extern aicmac_dma_desc_ctl_t dctl[MAX_ETH_MAC_PORT];
extern aicmac_config_t mac_config[MAX_ETH_MAC_PORT];

void aicmac_low_level_init(uint32_t port, bool en);
void aicmac_dcache_clean(uintptr_t addr, uint32_t len);
void aicmac_dcache_invalid(uintptr_t addr, uint32_t len);
void aicmac_gdma_sync(void);

int aicmac_init(uint32_t port);
void aicmac_exit(uint32_t port);
void aicmac_sw_reset(uint32_t port);
int aicmac_write_phy_reg(uint32_t port, uint32_t addr, uint16_t val);
int aicmac_read_phy_reg(uint32_t port, uint32_t addr, uint16_t *val);
void aicmac_set_mac_tx(uint32_t port, bool state);
void aicmac_set_mac_rx(uint32_t port, bool state);
void aicmac_set_mac_speed(uint32_t port, int speed);
void aicmac_set_mac_duplex(uint32_t port, bool state);
void aicmac_set_mac_pause(uint32_t port, bool state);
void aicmac_flush_tx_fifo(uint32_t port);
void aicmac_set_dma_tx(uint32_t port, bool state);
void aicmac_set_dma_rx(uint32_t port, bool state);
bool aicmac_get_dma_int_status(uint32_t port, uint32_t flag);
void aicmac_clear_dma_int_pending(uint32_t port, uint32_t flag);
void aicmac_set_mac_addr(uint32_t port, uint32_t index, uint8_t *addr);
void aicmac_dma_tx_desc_init(uint32_t port);
void aicmac_dma_rx_desc_init(uint32_t port);
void aicmac_resume_dma_rx(uint32_t port);
void aicmac_set_dma_rx_desc_int(uint32_t port, bool en);
void aicmac_start(uint32_t port);
void aicmac_stop(uint32_t port);
void aicmac_confirm_tx_frame(uint32_t port);
int aicmac_submit_tx_frame(uint32_t port, uint16_t frame_len);
aicmac_frame_t aicmac_get_rx_frame_interrupt(uint32_t port);
void aicmac_release_rx_frame(uint32_t port);

#ifdef __cplusplus
}
#endif

#endif /* __AIC_MAC_H */
