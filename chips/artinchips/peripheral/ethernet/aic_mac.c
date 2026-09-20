/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/config.h>
#include <nuttx/signal.h>

#include <debug.h>
#include <errno.h>
#include <string.h>

#include <aic_core.h>
#include <aic_hal.h>
#include <aic_log.h>

#include "aic_mac.h"
#include "aic_phy.h"

unsigned long mac_base[MAX_ETH_MAC_PORT] = { AIC_GMAC0_REG_BASE };
unsigned long mac_irq[MAX_ETH_MAC_PORT] = { AIC_GMAC0_IRQ };

aicmac_dma_desc_ctl_t dctl[MAX_ETH_MAC_PORT];

aicmac_config_t mac_config[MAX_ETH_MAC_PORT] =
{
  {
    .port = 0,
    .phyaddr = CONFIG_AIC_DEV_GMAC0_PHYADDR,
    .rgmii_bus = 0,
    .max_speed = SPEED_100,
    .duplex = 1,
    .flowctl = 1,
    .autonegotiation = 1,
    .coe_tx = 0,
    .coe_rx = 0,
    .dma_rxpbl = DEFAULT_DMA_PB,
    .dma_txpbl = DEFAULT_DMA_PB,
    .dma_pblx8 = 1,
    .dma_fixed_burst = 1,
    .dma_mixed_burst = 0,
    .dma_aal = 1,
    .dma_sf_mode = 1,
    .phyrst_gpio_name = CONFIG_AIC_DEV_GMAC0_PHYRST_GPIO,
  },
};

static void aicmac_mdelay(unsigned int msec)
{
  nxsig_usleep((useconds_t)msec * 1000);
}

static bool aicmac_valid_port(uint32_t port)
{
  return port < MAX_ETH_MAC_PORT;
}

static void aicmac_reset_phy(uint32_t port)
{
  const char *name = mac_config[port].phyrst_gpio_name;
  int pin;
  int group;
  int group_pin;

  if (name == NULL || name[0] == '\0')
    {
      return;
    }

  pin = hal_gpio_name2pin(name);
  if (pin < 0)
    {
      pr_err("GMAC%u invalid PHY reset GPIO %s\n", (unsigned int)port, name);
      return;
    }

  group = GPIO_GROUP(pin);
  group_pin = GPIO_GROUP_PIN(pin);

  hal_gpio_direction_output(group, group_pin);
  hal_gpio_clr_output(group, group_pin);
  aicmac_mdelay(50);
  hal_gpio_set_output(group, group_pin);
  aicmac_mdelay(50);
}

void aicmac_exit(uint32_t port)
{
  if (!aicmac_valid_port(port))
    {
      return;
    }

  aicmac_low_level_init(port, DISABLE);
}

int aicmac_init(uint32_t port)
{
  uint32_t tmpreg;
  uint32_t ahbclk;

  if (!aicmac_valid_port(port))
    {
      return -EINVAL;
    }

  memset(&dctl[port], 0, sizeof(dctl[port]));

  aicmac_low_level_init(port, ENABLE);
  aicmac_sw_reset(port);
  aicmac_reset_phy(port);
  nxsig_usleep(1000);

  tmpreg = readl(MAC(port, mdioctl));
  tmpreg &= ~ETH_MDIOCTL_CR_MSK;
  ahbclk = hal_clk_get_freq(CLK_AHB0);
  if (ahbclk >= 20000000 && ahbclk < 35000000)
    {
      tmpreg |= ETH_MDIOCTL_CR_Div16;
    }
  else if (ahbclk >= 35000000 && ahbclk < 60000000)
    {
      tmpreg |= ETH_MDIOCTL_CR_Div26;
    }
  else if (ahbclk >= 60000000 && ahbclk < 100000000)
    {
      tmpreg |= ETH_MDIOCTL_CR_Div42;
    }
  else if (ahbclk >= 100000000 && ahbclk < 150000000)
    {
      tmpreg |= ETH_MDIOCTL_CR_Div62;
    }
  else if (ahbclk >= 150000000 && ahbclk < 250000000)
    {
      tmpreg |= ETH_MDIOCTL_CR_Div102;
    }
  else
    {
      tmpreg |= ETH_MDIOCTL_CR_Div124;
    }

  writel(tmpreg, MAC(port, mdioctl));

  tmpreg = readl(MAC(port, macconf));
  tmpreg &= ~ETH_MACCONF_SPEED_MSK;
  tmpreg |= ETH_MACCONF_SPEED_100M;
  tmpreg |= ETH_MACCONF_DM;
  writel(tmpreg, MAC(port, macconf));

  tmpreg = readl(MAC(port, flowctl));
  tmpreg &= ~ETH_FLOWCTL_PT;
  tmpreg |= (0x4 << ETH_FLOWCTL_PT_SHIFT);
  writel(tmpreg, MAC(port, flowctl));

  tmpreg = readl(MAC(port, dma0conf));
  tmpreg &= ~ETH_DMACONF_PR_MSK;
  tmpreg |= ETH_DMACONF_PR_3_1;
  tmpreg |= ETH_DMACONF_ATDS | ETH_DMACONF_USP;
  tmpreg &= ~(ETH_DMACONF_RPBL_MSK | ETH_DMACONF_PBL_MSK);
  tmpreg |= (mac_config[port].dma_rxpbl << ETH_DMACONF_RPBL_SHIFT);
  tmpreg |= (mac_config[port].dma_txpbl << ETH_DMACONF_PBL_SHIFT);

  if (mac_config[port].dma_pblx8)
    {
      tmpreg |= ETH_DMACONF_PBLx8;
    }

  if (mac_config[port].dma_fixed_burst)
    {
      tmpreg |= ETH_DMACONF_FB;
    }

  if (mac_config[port].dma_mixed_burst)
    {
      tmpreg |= ETH_DMACONF_MB;
    }

  if (mac_config[port].dma_aal)
    {
      tmpreg |= ETH_DMACONF_AAL;
    }

  writel(tmpreg, MAC(port, dma0conf));

  tmpreg = readl(MAC(port, rxdma0ctl));
  tmpreg |= ETH_RXDMACTL_RSF;
  writel(tmpreg, MAC(port, rxdma0ctl));

  tmpreg = readl(MAC(port, txdma0ctl));
  tmpreg |= ETH_TXDMACTL_TSF;
  writel(tmpreg, MAC(port, txdma0ctl));

  tmpreg = readl(MAC(port, dma0inten));
  tmpreg |= ETH_DMAINTEN_NIE | ETH_DMAINTEN_RIE;
  writel(tmpreg, MAC(port, dma0inten));

  return ETH_SUCCESS;
}

void aicmac_start(uint32_t port)
{
  if (!aicmac_valid_port(port))
    {
      return;
    }

  aicmac_set_mac_tx(port, ENABLE);
  aicmac_set_mac_rx(port, ENABLE);
  aicmac_flush_tx_fifo(port);
  aicmac_set_dma_tx(port, ENABLE);
  aicmac_set_dma_rx(port, ENABLE);
}

void aicmac_stop(uint32_t port)
{
  if (!aicmac_valid_port(port))
    {
      return;
    }

  aicmac_set_dma_tx(port, DISABLE);
  aicmac_set_dma_rx(port, DISABLE);
  aicmac_set_mac_rx(port, DISABLE);
  aicmac_flush_tx_fifo(port);
  aicmac_set_mac_tx(port, DISABLE);
}

void aicmac_set_mac_speed(uint32_t port, int speed)
{
  uint32_t tmpreg = readl(MAC(port, macconf));

  tmpreg &= ~ETH_MACCONF_SPEED_MSK;
  if (speed == SPEED_1000)
    {
      tmpreg |= ETH_MACCONF_SPEED_1000M;
    }
  else if (speed == SPEED_100)
    {
      tmpreg |= ETH_MACCONF_SPEED_100M;
    }
  else
    {
      tmpreg |= ETH_MACCONF_SPEED_10M;
    }

  writel(tmpreg, MAC(port, macconf));
}

void aicmac_set_mac_duplex(uint32_t port, bool state)
{
  uint32_t tmpreg = readl(MAC(port, macconf));

  if (state)
    {
      tmpreg |= ETH_MACCONF_DM;
    }
  else
    {
      tmpreg &= ~ETH_MACCONF_DM;
    }

  writel(tmpreg, MAC(port, macconf));
}

void aicmac_set_mac_pause(uint32_t port, bool state)
{
  uint32_t tmpreg = readl(MAC(port, flowctl));

  if (state)
    {
      tmpreg |= ETH_FLOWCTL_RFE | ETH_FLOWCTL_TFE;
    }
  else
    {
      tmpreg &= ~(ETH_FLOWCTL_RFE | ETH_FLOWCTL_TFE);
    }

  writel(tmpreg, MAC(port, flowctl));
}

void aicmac_set_mac_tx(uint32_t port, bool state)
{
  uint32_t tmpreg = readl(MAC(port, mactxfunc));

  if (state)
    {
      tmpreg |= ETH_MACTXFUNC_TE;
    }
  else
    {
      tmpreg &= ~ETH_MACTXFUNC_TE;
    }

  writel(tmpreg, MAC(port, mactxfunc));
}

void aicmac_set_mac_rx(uint32_t port, bool state)
{
  uint32_t tmpreg = readl(MAC(port, macrxfunc));

  if (state)
    {
      tmpreg |= ETH_MACRXFUNC_RE;
    }
  else
    {
      tmpreg &= ~ETH_MACRXFUNC_RE;
    }

  writel(tmpreg, MAC(port, macrxfunc));
}

void aicmac_set_mac_addr(uint32_t port, uint32_t index, uint8_t *addr)
{
  uint32_t tmpreg;

  if (index >= ETH_MACADDR_MAX_INDEX)
    {
      return;
    }

  tmpreg = ((uint32_t)addr[5] << 8) | (uint32_t)addr[4];
  writel(tmpreg, MAC(port, macaddr0high) + index * 8);

  tmpreg = ((uint32_t)addr[3] << 24) | ((uint32_t)addr[2] << 16) |
           ((uint32_t)addr[1] << 8) | addr[0];
  writel(tmpreg, MAC(port, macaddr0low) + index * 8);
}

void aicmac_resume_dma_rx(uint32_t port)
{
  uint32_t tmpreg = readl(MAC(port, rxdma0ctl));

  tmpreg |= ETH_RXDMACTL_RPD;
  writel(tmpreg, MAC(port, rxdma0ctl));
}

static void aicmac_resume_dma_tx(uint32_t port)
{
  uint32_t tmpreg = readl(MAC(port, txdma0ctl));

  tmpreg |= ETH_TXDMACTL_TPD;
  writel(tmpreg, MAC(port, txdma0ctl));
}

void aicmac_dma_rx_desc_init(uint32_t port)
{
  aicmac_dma_desc_t *rxdesc_tbl = &dctl[port].rx_desc_tbl[0];
  uint8_t *buff = &dctl[port].rx_buff[0][0];
  uint32_t i;

  memset(rxdesc_tbl, 0, sizeof(dctl[port].rx_desc_tbl));

  for (i = 0; i < ETH_RXBUFNB; i++)
    {
      aicmac_dma_desc_t *pdesc = rxdesc_tbl + i;

      pdesc->control = ETH_DMARxDesc_OWN;
      pdesc->buff_size = ETH_DMARxDesc_RCH |
                         (ETH_RX_BUF_SIZE & ETH_DMARxDesc_RBS1);
      pdesc->buff1_addr =
        (uint32_t)(uintptr_t)(&buff[i * AICMAC_DMA_BUF_SIZE]);
      pdesc->reserved1 = i;

      if (i < ETH_RXBUFNB - 1)
        {
          pdesc->buff2_addr = (uint32_t)(uintptr_t)(rxdesc_tbl + i + 1);
        }
      else
        {
          pdesc->buff2_addr = (uint32_t)(uintptr_t)rxdesc_tbl;
        }
    }

  aicmac_dcache_clean((uintptr_t)rxdesc_tbl, sizeof(dctl[port].rx_desc_tbl));
  aicmac_dcache_clean((uintptr_t)buff, sizeof(dctl[port].rx_buff));

  writel((uint32_t)(uintptr_t)rxdesc_tbl, MAC(port, rxdma0descstart));

  dctl[port].rx_desc_p = rxdesc_tbl;
  dctl[port].rx_desc_received_p = rxdesc_tbl;
  dctl[port].rx_desc_unrelease_p = rxdesc_tbl;
  dctl[port].rx_frame_info_p = &dctl[port].rx_frame_info;
}

void aicmac_dma_tx_desc_init(uint32_t port)
{
  aicmac_dma_desc_t *txdesc_tbl = &dctl[port].tx_desc_tbl[0];
  uint8_t *buff = &dctl[port].tx_buff[0][0];
  uint32_t i;

  memset(txdesc_tbl, 0, sizeof(dctl[port].tx_desc_tbl));

  for (i = 0; i < ETH_TXBUFNB; i++)
    {
      aicmac_dma_desc_t *pdesc = txdesc_tbl + i;

      pdesc->control = ETH_DMATxDesc_TCH;
      pdesc->buff1_addr =
        (uint32_t)(uintptr_t)(&buff[i * AICMAC_DMA_BUF_SIZE]);

      if (i < ETH_TXBUFNB - 1)
        {
          pdesc->buff2_addr = (uint32_t)(uintptr_t)(txdesc_tbl + i + 1);
        }
      else
        {
          pdesc->buff2_addr = (uint32_t)(uintptr_t)txdesc_tbl;
        }
    }

  aicmac_dcache_clean((uintptr_t)txdesc_tbl, sizeof(dctl[port].tx_desc_tbl));
  aicmac_dcache_clean((uintptr_t)buff, sizeof(dctl[port].tx_buff));

  writel((uint32_t)(uintptr_t)txdesc_tbl, MAC(port, txdma0descstart));
  dctl[port].tx_desc_p = txdesc_tbl;
}

aicmac_frame_t aicmac_get_rx_frame_interrupt(uint32_t port)
{
  aicmac_dma_desc_t *pdesc = dctl[port].rx_desc_p;
  aicmac_rx_frame_info_t *pinfo = dctl[port].rx_frame_info_p;
  aicmac_frame_t frame = { 0, 0, 0, 0 };
  uint32_t scan_count = 0;

  aicmac_dcache_invalid((uintptr_t)pdesc, sizeof(aicmac_dma_desc_t));

  while (((pdesc->control & ETH_DMARxDesc_OWN) == RESET) &&
         scan_count < ETH_RXBUFNB)
    {
      scan_count++;

      if ((pdesc->control & ETH_DMARxDesc_FS) &&
          !(pdesc->control & ETH_DMARxDesc_LS))
        {
          pinfo->first_desc = pdesc;
          pinfo->seg_cnt = 1;
          pdesc = (aicmac_dma_desc_t *)(uintptr_t)pdesc->buff2_addr;
          dctl[port].rx_desc_p = pdesc;
        }
      else if (!(pdesc->control & ETH_DMARxDesc_LS) &&
               !(pdesc->control & ETH_DMARxDesc_FS))
        {
          pinfo->seg_cnt++;
          pdesc = (aicmac_dma_desc_t *)(uintptr_t)pdesc->buff2_addr;
          dctl[port].rx_desc_p = pdesc;
        }
      else
        {
          pinfo->last_desc = pdesc;
          pinfo->seg_cnt++;

          if (pinfo->seg_cnt == 1)
            {
              pinfo->first_desc = pdesc;
            }

          frame.length =
            ((pdesc->control & ETH_DMARxDesc_FL) >> ETH_DMARxDesc_FL_SHIFT);
          if (frame.length >= ETH_CRC)
            {
              frame.length -= ETH_CRC;
            }

          frame.buffer = pinfo->first_desc->buff1_addr;
          frame.descriptor = pinfo->first_desc;
          frame.last_desc = pinfo->last_desc;

          pdesc = (aicmac_dma_desc_t *)(uintptr_t)pdesc->buff2_addr;
          dctl[port].rx_desc_p = pdesc;
          return frame;
        }

      aicmac_dcache_invalid((uintptr_t)pdesc, sizeof(aicmac_dma_desc_t));
    }

  return frame;
}

void aicmac_release_rx_frame(uint32_t port)
{
  aicmac_dma_desc_t *pdesc = dctl[port].rx_desc_unrelease_p;
  aicmac_dma_desc_t *pend = dctl[port].rx_desc_received_p;

  while (pdesc != pend)
    {
      pdesc->control = ETH_DMARxDesc_OWN;
      aicmac_dcache_clean((uintptr_t)&pdesc->control, sizeof(uint32_t));

      pdesc = (aicmac_dma_desc_t *)(uintptr_t)pdesc->buff2_addr;
      dctl[port].rx_desc_unrelease_p = pdesc;
    }

  aicmac_resume_dma_rx(port);
}

int aicmac_submit_tx_frame(uint32_t port, uint16_t frame_len)
{
  aicmac_dma_desc_t *pdesc = dctl[port].tx_desc_p;
  uint32_t buf_count;
  uint32_t i;

  aicmac_dcache_invalid((uintptr_t)pdesc, sizeof(*pdesc));
  if (pdesc->control & ETH_DMATxDesc_OWN)
    {
      return ETH_ERROR;
    }

  buf_count = DIV_ROUND_UP(frame_len, ETH_TX_BUF_SIZE);
  if (buf_count == 0 || buf_count > ETH_TXBUFNB)
    {
      return ETH_ERROR;
    }

  for (i = 0; i < buf_count; i++)
    {
      uint32_t size = ETH_TX_BUF_SIZE;
      uint32_t ctl = ETH_DMATxDesc_TCH;

      aicmac_dcache_invalid((uintptr_t)pdesc, sizeof(*pdesc));
      if (pdesc->control & ETH_DMATxDesc_OWN)
        {
          return ETH_ERROR;
        }

      if (i == 0)
        {
          ctl |= ETH_DMATxDesc_FS;
        }

      if (i == buf_count - 1)
        {
          size = frame_len - i * ETH_TX_BUF_SIZE;
          ctl |= ETH_DMATxDesc_LS;
        }

      pdesc->buff_size = size & ETH_DMATxDesc_TBS1;

      if (mac_config[port].coe_tx)
        {
          ctl |= ETH_DMATxDesc_CIC_TCPUDPICMP_Full;
        }

      pdesc->control = ctl | ETH_DMATxDesc_OWN;
      aicmac_dcache_clean((uintptr_t)pdesc, sizeof(*pdesc));
      pdesc = (aicmac_dma_desc_t *)(uintptr_t)pdesc->buff2_addr;
    }

  dctl[port].tx_desc_p = pdesc;
  aicmac_resume_dma_tx(port);
  return ETH_SUCCESS;
}

void aicmac_confirm_tx_frame(uint32_t port)
{
  aicmac_resume_dma_tx(port);
}

void aicmac_set_dma_rx_desc_int(uint32_t port, bool en)
{
  uint32_t i;

  for (i = 0; i < ETH_RXBUFNB; i++)
    {
      aicmac_dma_desc_t *pdesc = &dctl[port].rx_desc_tbl[i];

      if (en)
        {
          pdesc->buff_size &= ~ETH_DMARxDesc_DIC;
        }
      else
        {
          pdesc->buff_size |= ETH_DMARxDesc_DIC;
        }
    }

  aicmac_dcache_clean((uintptr_t)&dctl[port].rx_desc_tbl[0],
                      sizeof(dctl[port].rx_desc_tbl));
}

void aicmac_sw_reset(uint32_t port)
{
  uint32_t tmpreg = readl(MAC(port, macconf));
  uint32_t timeout = 0;

  tmpreg |= ETH_MACCONF_SWR;
  writel(tmpreg, MAC(port, macconf));

  do
    {
      timeout++;
      tmpreg = readl(MAC(port, macconf));
    }
  while ((tmpreg & ETH_MACCONF_SWR) && timeout < PHY_WRITE_TO);

  if (timeout == PHY_WRITE_TO)
    {
      pr_err("GMAC%u software reset timeout\n", (unsigned int)port);
    }
}

bool aicmac_get_dma_int_status(uint32_t port, uint32_t flag)
{
  return (readl(MAC(port, dma0intsts)) & flag) ? SET : RESET;
}

void aicmac_clear_dma_int_pending(uint32_t port, uint32_t flag)
{
  uint32_t tmpreg = readl(MAC(port, dma0intsts));

  tmpreg |= flag;
  writel(tmpreg, MAC(port, dma0intsts));
}

void aicmac_flush_tx_fifo(uint32_t port)
{
  uint32_t tmpreg = readl(MAC(port, txdma0ctl));
  uint32_t timeout = 0;

  tmpreg |= ETH_TXDMACTL_FTF;
  writel(tmpreg, MAC(port, txdma0ctl));

  do
    {
      timeout++;
      tmpreg = readl(MAC(port, txdma0ctl));
    }
  while ((tmpreg & ETH_TXDMACTL_FTF) && timeout < PHY_WRITE_TO);
}

void aicmac_set_dma_tx(uint32_t port, bool state)
{
  uint32_t tmpreg = readl(MAC(port, txdma0ctl));

  if (state)
    {
      tmpreg |= ETH_TXDMACTL_ST;
    }
  else
    {
      tmpreg &= ~ETH_TXDMACTL_ST;
    }

  writel(tmpreg, MAC(port, txdma0ctl));
}

void aicmac_set_dma_rx(uint32_t port, bool state)
{
  uint32_t tmpreg = readl(MAC(port, rxdma0ctl));

  if (state)
    {
      tmpreg |= ETH_RXDMACTL_SR;
    }
  else
    {
      tmpreg &= ~ETH_RXDMACTL_SR;
    }

  writel(tmpreg, MAC(port, rxdma0ctl));
}

int aicmac_read_phy_reg(uint32_t port, uint32_t addr, uint16_t *val)
{
  uint32_t tmpreg;
  uint32_t timeout = 0;

  tmpreg = readl(MAC(port, mdioctl));
  tmpreg &= ETH_MDIOCTL_CR_MSK;
  tmpreg |= ((uint32_t)mac_config[port].phyaddr << 11) & ETH_MDIOCTL_PA;
  tmpreg |= ((uint32_t)addr << 6) & ETH_MDIOCTL_MR;
  tmpreg &= ~ETH_MDIOCTL_MW;
  tmpreg |= ETH_MDIOCTL_MB;
  writel(tmpreg, MAC(port, mdioctl));

  do
    {
      timeout++;
      tmpreg = readl(MAC(port, mdioctl));
    }
  while ((tmpreg & ETH_MDIOCTL_MB) && timeout < PHY_READ_TO);

  if (timeout == PHY_READ_TO)
    {
      pr_err("GMAC%u read PHY%u reg%u timeout\n", (unsigned int)port,
             (unsigned int)mac_config[port].phyaddr, (unsigned int)addr);
      return ETH_ERROR;
    }

  tmpreg = readl(MAC(port, mdiodata));
  *val = tmpreg & 0xffff;
  return ETH_SUCCESS;
}

int aicmac_write_phy_reg(uint32_t port, uint32_t addr, uint16_t val)
{
  uint32_t tmpreg;
  uint32_t timeout = 0;

  tmpreg = readl(MAC(port, mdioctl));
  tmpreg &= ETH_MDIOCTL_CR_MSK;
  tmpreg |= ((uint32_t)mac_config[port].phyaddr << 11) & ETH_MDIOCTL_PA;
  tmpreg |= ((uint32_t)addr << 6) & ETH_MDIOCTL_MR;
  tmpreg |= ETH_MDIOCTL_MW | ETH_MDIOCTL_MB;
  writel(val, MAC(port, mdiodata));
  writel(tmpreg, MAC(port, mdioctl));

  do
    {
      timeout++;
      tmpreg = readl(MAC(port, mdioctl));
    }
  while ((tmpreg & ETH_MDIOCTL_MB) && timeout < PHY_WRITE_TO);

  if (timeout == PHY_WRITE_TO)
    {
      pr_err("GMAC%u write PHY%u reg%u timeout\n", (unsigned int)port,
             (unsigned int)mac_config[port].phyaddr, (unsigned int)addr);
      return ETH_ERROR;
    }

  return ETH_SUCCESS;
}
