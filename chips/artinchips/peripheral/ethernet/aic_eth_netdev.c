/*
 * Copyright (c) 2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/config.h>

#include <syslog.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/irq.h>
#include <nuttx/kthread.h>
#include <nuttx/mutex.h>
#include <nuttx/net/ethernet.h>
#include <nuttx/net/net.h>
#include <nuttx/net/netdev.h>
#include <nuttx/net/netdev_lowerhalf.h>
#include <nuttx/signal.h>

#include <net/if.h>
#include <net/if_arp.h>

#include "aic_eth.h"
#include "aic_mac.h"
#include "aic_phy.h"

#define AIC_ETH_PORT       0
#define AIC_ETH_TX_QUOTA   ETH_TXBUFNB
#define AIC_ETH_RX_QUOTA   ETH_RXBUFNB
#define AIC_ETH_STACKSIZE  2048
#define AIC_ETH_PRIORITY   100

struct aic_eth_dev_s
{
  struct netdev_lowerhalf_s lower;
  uint32_t port;
  uint8_t mac[6];
  mutex_t lock;
  bool registered;
  bool up;
  bool link_up;
  bool hw_inited;
  bool irq_attached;
  bool phy_thread_started;
  pid_t phy_thread;
};

static struct aic_eth_dev_s g_aic_eth;

static int aic_eth_hexval(char c)
{
  if (c >= '0' && c <= '9')
    {
      return c - '0';
    }

  if (c >= 'A' && c <= 'F')
    {
      return c - 'A' + 10;
    }

  if (c >= 'a' && c <= 'f')
    {
      return c - 'a' + 10;
    }

  return -1;
}

static bool aic_eth_parse_mac(const char *str, uint8_t mac[6])
{
  uint8_t tmp[6];
  int count = 0;

  if (str == NULL)
    {
      return false;
    }

  while (*str != '\0' && count < 6)
    {
      int hi;
      int lo;

      while (*str == ':' || *str == '-' || *str == ' ')
        {
          str++;
        }

      hi = aic_eth_hexval(str[0]);
      lo = aic_eth_hexval(str[1]);
      if (hi < 0 || lo < 0)
        {
          return false;
        }

      tmp[count++] = (uint8_t)((hi << 4) | lo);
      str += 2;

      if (*str == ':' || *str == '-' || *str == ' ')
        {
          str++;
        }
    }

  while (*str == ':' || *str == '-' || *str == ' ')
    {
      str++;
    }

  if (count != 6 || *str != '\0')
    {
      return false;
    }

  memcpy(mac, tmp, 6);
  return true;
}

static void aic_eth_set_default_mac(struct aic_eth_dev_s *priv)
{
  static const uint8_t fallback[6] = { 0x02, 0x22, 0x44, 0x88, 0x77, 0x66 };

  if (!aic_eth_parse_mac(CONFIG_AIC_DEV_GMAC0_MACADDR, priv->mac))
    {
      memcpy(priv->mac, fallback, sizeof(priv->mac));
    }

  if ((priv->mac[0] & 0x01) || !(priv->mac[0] | priv->mac[1] |
      priv->mac[2] | priv->mac[3] | priv->mac[4] | priv->mac[5]))
    {
      memcpy(priv->mac, fallback, sizeof(priv->mac));
    }

  priv->mac[0] &= ~0x01;
  priv->mac[0] |= 0x02;

  memcpy(priv->lower.netdev.d_mac.ether.ether_addr_octet, priv->mac,
         sizeof(priv->mac));
}

static int aic_eth_interrupt(int irq, void *context, void *arg)
{
  struct aic_eth_dev_s *priv = arg;
  uint32_t port = priv->port;
  bool rxready = false;

  if (aicmac_get_dma_int_status(port, ETH_DMAINTSTS_RI))
    {
      aicmac_clear_dma_int_pending(port, ETH_DMAINTSTS_RI);
      rxready = true;
    }

  if (aicmac_get_dma_int_status(port, ETH_DMAINTSTS_RU))
    {
      aicmac_clear_dma_int_pending(port, ETH_DMAINTSTS_RU);
      aicmac_resume_dma_rx(port);
    }

  if (aicmac_get_dma_int_status(port, ETH_DMAINTSTS_TI))
    {
      aicmac_clear_dma_int_pending(port, ETH_DMAINTSTS_TI);
      aicmac_confirm_tx_frame(port);
    }

  if (aicmac_get_dma_int_status(port, ETH_DMAINTSTS_AIS))
    {
      aicmac_clear_dma_int_pending(port, ETH_DMAINTSTS_AIS);
    }

  aicmac_clear_dma_int_pending(port, ETH_DMAINTSTS_NIS);

  if (rxready)
    {
      netdev_lower_rxready(&priv->lower);
    }

  return OK;
}

static void aic_eth_apply_link(struct aic_eth_dev_s *priv, bool link)
{
  aic_phy_device_t *phydev = &phy_device[priv->port];

  if (priv->link_up == link)
    {
      return;
    }

  priv->link_up = link;

  if (link)
    {
      aicmac_set_mac_speed(priv->port, phydev->speed);
      aicmac_set_mac_duplex(priv->port, phydev->duplex == DUPLEX_FULL);
      aicmac_set_mac_pause(priv->port, phydev->pause);
      aicmac_start(priv->port);
      netdev_lower_carrier_on(&priv->lower);
      syslog(LOG_INFO,
             "GMAC%u link UP: %dM %s duplex flowctl %s\n",
             (unsigned int)priv->port, phydev->speed,
             phydev->duplex == DUPLEX_FULL ? "full" : "half",
             phydev->pause ? "on" : "off");
    }
  else
    {
      aicmac_stop(priv->port);
      netdev_lower_carrier_off(&priv->lower);
      syslog(LOG_INFO, "GMAC%u link DOWN\n", (unsigned int)priv->port);
    }
}

static int aic_eth_phy_thread(int argc, char *argv[])
{
  struct aic_eth_dev_s *priv = &g_aic_eth;

  while (true)
    {
      nxsig_usleep((useconds_t)CONFIG_AIC_DEV_GMAC0_PHY_POLL_MS * 1000);

      if (!priv->up || !priv->hw_inited)
        {
          continue;
        }

      nxmutex_lock(&priv->lock);
      if (aicphy_read_status(priv->port) == ETH_SUCCESS)
        {
          aic_eth_apply_link(priv, phy_device[priv->port].link);
        }
      else
        {
          syslog(LOG_ERR, "GMAC%u PHY status read failed\n",
                 (unsigned int)priv->port);
        }

      nxmutex_unlock(&priv->lock);
    }

  return OK;
}

static int aic_eth_start_phy_thread(struct aic_eth_dev_s *priv)
{
  if (priv->phy_thread_started)
    {
      return OK;
    }

  priv->phy_thread = kthread_create("aic_eth_phy", AIC_ETH_PRIORITY,
                                    AIC_ETH_STACKSIZE, aic_eth_phy_thread,
                                    NULL);
  if (priv->phy_thread < 0)
    {
      return priv->phy_thread;
    }

  priv->phy_thread_started = true;
  return OK;
}

static int aic_eth_hw_init(struct aic_eth_dev_s *priv)
{
  int ret;

  ret = aicmac_init(priv->port);
  if (ret != ETH_SUCCESS)
    {
      return -EIO;
    }

  aicmac_set_mac_addr(priv->port, 0, priv->mac);
  aicmac_dma_tx_desc_init(priv->port);
  aicmac_dma_rx_desc_init(priv->port);
  aicmac_set_dma_rx_desc_int(priv->port, ENABLE);

  if (!priv->irq_attached)
    {
      ret = irq_attach(mac_irq[priv->port], aic_eth_interrupt, priv);
      if (ret < 0)
        {
          syslog(LOG_ERR, "GMAC%u irq_attach failed: %d\n",
                 (unsigned int)priv->port, ret);
          return ret;
        }

      priv->irq_attached = true;
    }

  up_enable_irq(mac_irq[priv->port]);

  ret = aicphy_init(priv->port);
  if (ret != ETH_SUCCESS)
    {
      up_disable_irq(mac_irq[priv->port]);
      return -EIO;
    }

  priv->hw_inited = true;
  return aic_eth_start_phy_thread(priv);
}

static int aic_eth_ifup(FAR struct netdev_lowerhalf_s *dev)
{
  struct aic_eth_dev_s *priv = (struct aic_eth_dev_s *)dev;
  int ret = OK;

  nxmutex_lock(&priv->lock);

  if (!priv->up)
    {
      ret = aic_eth_hw_init(priv);
      if (ret == OK)
        {
          priv->up = true;
          priv->link_up = false;
          netdev_lower_carrier_off(&priv->lower);
        }
    }

  nxmutex_unlock(&priv->lock);
  return ret;
}

static int aic_eth_ifdown(FAR struct netdev_lowerhalf_s *dev)
{
  struct aic_eth_dev_s *priv = (struct aic_eth_dev_s *)dev;

  nxmutex_lock(&priv->lock);

  if (priv->up)
    {
      priv->up = false;
      priv->link_up = false;
      up_disable_irq(mac_irq[priv->port]);
      aicmac_stop(priv->port);
      netdev_lower_carrier_off(&priv->lower);
    }

  nxmutex_unlock(&priv->lock);
  return OK;
}

static int aic_eth_transmit(FAR struct netdev_lowerhalf_s *dev,
                            FAR netpkt_t *pkt)
{
  struct aic_eth_dev_s *priv = (struct aic_eth_dev_s *)dev;
  aicmac_dma_desc_t *pdesc;
  uint8_t *buffer;
  unsigned int len;
  int ret;

  if (!priv->up || !priv->link_up)
    {
      return -ENETDOWN;
    }

  len = netpkt_getdatalen(dev, pkt);
  if (len == 0 || len > ETH_TX_BUF_SIZE)
    {
      return -EMSGSIZE;
    }

  nxmutex_lock(&priv->lock);

  pdesc = dctl[priv->port].tx_desc_p;
  aicmac_dcache_invalid((uintptr_t)pdesc, sizeof(*pdesc));
  if (pdesc->control & ETH_DMATxDesc_OWN)
    {
      nxmutex_unlock(&priv->lock);
      return -EBUSY;
    }

  buffer = (uint8_t *)(uintptr_t)pdesc->buff1_addr;
  ret = netpkt_copyout(dev, buffer, pkt, len, 0);
  if (ret < 0)
    {
      nxmutex_unlock(&priv->lock);
      return ret;
    }

  aicmac_dcache_clean((uintptr_t)buffer, len);

  ret = aicmac_submit_tx_frame(priv->port, len);
  if (ret != ETH_SUCCESS)
    {
      nxmutex_unlock(&priv->lock);
      return -EIO;
    }

  nxmutex_unlock(&priv->lock);

  netpkt_free(dev, pkt, NETPKT_TX);
  netdev_lower_txdone(dev);
  return OK;
}

static void aic_eth_finish_rx(struct aic_eth_dev_s *priv,
                              const aicmac_frame_t *frame)
{
  dctl[priv->port].rx_desc_received_p =
    (aicmac_dma_desc_t *)(uintptr_t)frame->last_desc->buff2_addr;
  dctl[priv->port].rx_frame_info.seg_cnt = 0;
  aicmac_release_rx_frame(priv->port);
}

static FAR netpkt_t *aic_eth_receive(FAR struct netdev_lowerhalf_s *dev)
{
  struct aic_eth_dev_s *priv = (struct aic_eth_dev_s *)dev;
  aicmac_frame_t frame;
  FAR netpkt_t *pkt;
  aicmac_dma_desc_t *pdesc;
  uint32_t remaining;
  uint32_t copied = 0;
  int ret = OK;

  if (!priv->up)
    {
      return NULL;
    }

  nxmutex_lock(&priv->lock);
  frame = aicmac_get_rx_frame_interrupt(priv->port);
  if (frame.descriptor == NULL || frame.last_desc == NULL)
    {
      nxmutex_unlock(&priv->lock);
      return NULL;
    }

  if (frame.length == 0 || frame.length > ETH_MAX_PACKET_SIZE)
    {
      aic_eth_finish_rx(priv, &frame);
      nxmutex_unlock(&priv->lock);
      return NULL;
    }

  pkt = netpkt_alloc(dev, NETPKT_RX);
  if (pkt == NULL)
    {
      aic_eth_finish_rx(priv, &frame);
      nxmutex_unlock(&priv->lock);
      return NULL;
    }

  pdesc = frame.descriptor;
  remaining = frame.length;

  while (remaining > 0)
    {
      uint32_t chunk = remaining > ETH_RX_BUF_SIZE ?
                       ETH_RX_BUF_SIZE : remaining;
      uint8_t *buffer = (uint8_t *)(uintptr_t)pdesc->buff1_addr;

      aicmac_gdma_sync();
      aicmac_dcache_invalid((uintptr_t)buffer, chunk);

      ret = netpkt_copyin(dev, pkt, buffer, chunk, copied);
      if (ret < 0)
        {
          break;
        }

      copied += chunk;
      remaining -= chunk;

      if (pdesc == frame.last_desc)
        {
          break;
        }

      pdesc = (aicmac_dma_desc_t *)(uintptr_t)pdesc->buff2_addr;
    }

  aic_eth_finish_rx(priv, &frame);
  nxmutex_unlock(&priv->lock);

  if (ret < 0 || copied != frame.length)
    {
      netpkt_free(dev, pkt, NETPKT_RX);
      return NULL;
    }

  return pkt;
}

#ifdef CONFIG_NETDEV_IOCTL
static int aic_eth_ioctl(FAR struct netdev_lowerhalf_s *dev, int cmd,
                         unsigned long arg)
{
  struct aic_eth_dev_s *priv = (struct aic_eth_dev_s *)dev;
  int ret = -ENOTTY;

  switch (cmd)
    {
      case SIOCGIFHWADDR:
        {
          struct ifreq *ifr = (struct ifreq *)(uintptr_t)arg;

          if (ifr != NULL)
            {
              memcpy(ifr->ifr_hwaddr.sa_data, priv->mac, sizeof(priv->mac));
              ifr->ifr_hwaddr.sa_family = ARPHRD_ETHER;
              ret = OK;
            }
        }
        break;

      case SIOCSIFHWADDR:
        {
          struct ifreq *ifr = (struct ifreq *)(uintptr_t)arg;

          if (ifr != NULL)
            {
              memcpy(priv->mac, ifr->ifr_hwaddr.sa_data, sizeof(priv->mac));
              priv->mac[0] &= ~0x01;
              memcpy(priv->lower.netdev.d_mac.ether.ether_addr_octet,
                     priv->mac, sizeof(priv->mac));

              if (priv->hw_inited)
                {
                  aicmac_set_mac_addr(priv->port, 0, priv->mac);
                }

              ret = OK;
            }
        }
        break;

      default:
        break;
    }

  return ret;
}
#endif

static const struct netdev_ops_s g_aic_eth_ops =
{
  .ifup = aic_eth_ifup,
  .ifdown = aic_eth_ifdown,
  .transmit = aic_eth_transmit,
  .receive = aic_eth_receive,
#ifdef CONFIG_NETDEV_IOCTL
  .ioctl = aic_eth_ioctl,
#endif
};

int aic_eth_initialize(void)
{
  struct aic_eth_dev_s *priv = &g_aic_eth;
  int ret;

  if (priv->registered)
    {
      return OK;
    }

  memset(priv, 0, sizeof(*priv));
  priv->port = AIC_ETH_PORT;
  priv->lower.ops = &g_aic_eth_ops;
  priv->lower.quota[NETPKT_TX] = AIC_ETH_TX_QUOTA;
  priv->lower.quota[NETPKT_RX] = AIC_ETH_RX_QUOTA;
  priv->lower.rxtype = NETDEV_RX_WORK;
  nxmutex_init(&priv->lock);

  aic_eth_set_default_mac(priv);

  ret = netdev_lower_register(&priv->lower, NET_LL_ETHERNET);
  if (ret < 0)
    {
      nxmutex_destroy(&priv->lock);
      syslog(LOG_ERR, "GMAC0 netdev register failed: %d\n", ret);
      return ret;
    }

  priv->registered = true;

  ret = netdev_ifup(&priv->lower.netdev);
  if (ret < 0)
    {
      syslog(LOG_ERR, "GMAC0 ifup failed: %d\n", ret);
      netdev_lower_unregister(&priv->lower);
      nxmutex_destroy(&priv->lock);
      priv->registered = false;
      return ret;
    }

  syslog(LOG_INFO, "GMAC0 registered as eth0 %02x:%02x:%02x:%02x:%02x:%02x\n",
         priv->mac[0], priv->mac[1], priv->mac[2],
         priv->mac[3], priv->mac[4], priv->mac[5]);
  return OK;
}
