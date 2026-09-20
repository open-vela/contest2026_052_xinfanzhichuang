/*
 * AIC8800 WiFi NuttX Network Device Driver
 *
 * Registers the AIC8800 WiFi as a NuttX network device using the
 * netdev_lowerhalf API. Based on the realtek_ieee80211 reference.
 */

#include <nuttx/config.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/net/ethernet.h>
#include <nuttx/net/netdev.h>
#include <nuttx/net/netdev_lowerhalf.h>
#include <nuttx/net/net.h>
#include <nuttx/wireless/wireless.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <arpa/inet.h>

#include "aic_plat_log.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WIFI_MTU        1500
#define WIFI_MAX_FRAME  (WIFI_MTU + ETH_HDRLEN)
#define WIFI_TX_QUOTA   4
#define WIFI_RX_QUOTA   8

/* Forward declarations for external functions */
extern int tx_eth_data_process(uint8_t *data, uint32_t len, void *msg);
extern int wifi_if_sdio_init(void);
extern uint8_t *aic_get_mac_address(void);

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct aic_wifi_dev_s
{
  struct netdev_lowerhalf_s dev;    /* NuttX netdev lower-half (must be first) */
  int                       devidx; /* Device index (0=STA) */
  bool                      up;     /* Interface is up */
  uint8_t                   mac[6]; /* MAC address */
  mutex_t                   rx_lock;
  netpkt_queue_t            rx_queue;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct aic_wifi_dev_s g_wifi_dev;
static bool g_wifi_netdev_registered;

static void wifi_rx_queue_free(struct aic_wifi_dev_s *priv)
{
  FAR netpkt_t *pkt;

  nxmutex_lock(&priv->rx_lock);
  while ((pkt = netpkt_remove_queue(&priv->rx_queue)) != NULL)
    {
      netpkt_free(&priv->dev, pkt, NETPKT_RX);
    }

  nxmutex_unlock(&priv->rx_lock);
}

static void wifi_netdev_set_mac(struct aic_wifi_dev_s *priv)
{
  uint8_t *mac = aic_get_mac_address();

  if (mac && (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]))
    {
      memcpy(priv->mac, mac, sizeof(priv->mac));
    }
  else
    {
      memcpy(priv->mac, "\x00\x11\x22\x33\x44\x55", sizeof(priv->mac));
    }

  memcpy(priv->dev.netdev.d_mac.ether.ether_addr_octet, priv->mac,
         sizeof(priv->mac));
}

/****************************************************************************
 * netdev_ops callbacks
 ****************************************************************************/

static int wifi_ifup(FAR struct netdev_lowerhalf_s *dev)
{
  struct aic_wifi_dev_s *priv = (struct aic_wifi_dev_s *)dev;
  int ret;

  AIC_LOG_PRINTF("AIC8800 wifi_ifup[%d]\n", priv->devidx);

  if (priv->up)
    {
      return OK;
    }

  ret = wifi_if_sdio_init();
  if (ret < 0)
    {
      AIC_LOG_PRINTF("wifi_if_sdio_init failed: %d\n", ret);
      return ret;
    }

  priv->up = true;
  return OK;
}

extern void fhost_deinit(void *rwnx_hw);
extern int aic_wifi_close(int mode);
extern int wifi_if_sdio_deinit(void);

static int wifi_ifdown(FAR struct netdev_lowerhalf_s *dev)
{
  struct aic_wifi_dev_s *priv = (struct aic_wifi_dev_s *)dev;
  AIC_LOG_PRINTF("AIC8800 wifi_ifdown[%d]\n", priv->devidx);

  if (!priv->up)
    return OK;

  priv->up = false;
  netdev_lower_carrier_off(&priv->dev);
  wifi_rx_queue_free(priv);

  /* Close WiFi interface (mode 0 = STA) */
  aic_wifi_close(priv->devidx);

  /* Deinitialize SDIO */
  wifi_if_sdio_deinit();

  return OK;
}

static int wifi_transmit(FAR struct netdev_lowerhalf_s *dev,
                         FAR netpkt_t *pkt)
{
  struct aic_wifi_dev_s *priv = (struct aic_wifi_dev_s *)dev;
  unsigned int len;
  uint8_t *data;
  int ret;

  if (!priv->up)
    {
      netpkt_free(dev, pkt, NETPKT_TX);
      return -ENETDOWN;
    }

  len = netpkt_getdatalen(dev, pkt);
  if (len == 0 || len > WIFI_MAX_FRAME)
    {
      netpkt_free(dev, pkt, NETPKT_TX);
      return -EINVAL;
    }

  data = kmm_malloc(len);
  if (!data)
    {
      netpkt_free(dev, pkt, NETPKT_TX);
      return -ENOMEM;
    }

  netpkt_copyout(dev, data, pkt, len, 0);
  netpkt_free(dev, pkt, NETPKT_TX);

  ret = tx_eth_data_process(data, len, NULL);
  kmm_free(data);

  if (ret == 0)
    {
      netdev_lower_txdone(dev);
    }

  return ret;
}

static FAR netpkt_t *wifi_receive(FAR struct netdev_lowerhalf_s *dev)
{
  struct aic_wifi_dev_s *priv = (struct aic_wifi_dev_s *)dev;
  FAR netpkt_t *pkt;

  nxmutex_lock(&priv->rx_lock);
  pkt = netpkt_remove_queue(&priv->rx_queue);
  nxmutex_unlock(&priv->rx_lock);

  return pkt;
}

#ifdef CONFIG_NETDEV_IOCTL
static int wifi_ioctl(FAR struct netdev_lowerhalf_s *dev, int cmd,
                      unsigned long arg)
{
  struct aic_wifi_dev_s *priv = (struct aic_wifi_dev_s *)dev;
  int ret = -ENOTTY;

  switch (cmd)
    {
      case SIOCGIFHWADDR:
        {
          struct ifreq *ifr = (struct ifreq *)(uintptr_t)arg;
          if (ifr)
            {
              memcpy(ifr->ifr_hwaddr.sa_data, priv->mac, 6);
              ifr->ifr_hwaddr.sa_family = ARPHRD_ETHER;
              ret = OK;
            }
          break;
        }

      default:
        break;
    }

  return ret;
}
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct netdev_ops_s g_wifi_ops =
{
  wifi_ifup,
  wifi_ifdown,
  wifi_transmit,
  wifi_receive,
#ifdef CONFIG_NETDEV_IOCTL
  wifi_ioctl
#endif
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int wifi_netdev_init(void)
{
  struct aic_wifi_dev_s *priv = &g_wifi_dev;
  int ret;

  if (g_wifi_netdev_registered)
    {
      AIC_LOG_PRINTF("AIC8800 WiFi netdev already registered\n");
      return OK;
    }

  memset(priv, 0, sizeof(*priv));
  priv->devidx  = 0;
  priv->dev.ops = &g_wifi_ops;
  priv->dev.quota[NETPKT_TX] = WIFI_TX_QUOTA;
  priv->dev.quota[NETPKT_RX] = WIFI_RX_QUOTA;
  nxmutex_init(&priv->rx_lock);
  wifi_netdev_set_mac(priv);

  ret = netdev_lower_register(&priv->dev, NET_LL_IEEE80211);
  if (ret < 0)
    {
      AIC_LOG_PRINTF("netdev_lower_register wlan0 failed: %d\n", ret);
      nxmutex_destroy(&priv->rx_lock);
      return ret;
    }

  ret = netdev_ifup(&priv->dev.netdev);
  if (ret < 0)
    {
      AIC_LOG_PRINTF("netdev_ifup wlan0 failed: %d\n", ret);
      netdev_lower_unregister(&priv->dev);
      nxmutex_destroy(&priv->rx_lock);
      return ret;
    }

  netdev_lower_carrier_on(&priv->dev);

  g_wifi_netdev_registered = true;
  AIC_LOG_PRINTF("AIC8800 WiFi netdev registered as wlan0 "
                 "%02x:%02x:%02x:%02x:%02x:%02x\n",
                 priv->mac[0], priv->mac[1], priv->mac[2],
                 priv->mac[3], priv->mac[4], priv->mac[5]);
  return OK;
}

int wifi_netdev_deinit(void)
{
  struct aic_wifi_dev_s *priv = &g_wifi_dev;
  int ret;

  if (!g_wifi_netdev_registered)
    {
      AIC_LOG_PRINTF("AIC8800 WiFi netdev already unregistered\n");
      return OK;
    }

  if (priv->up)
    {
      priv->up = false;
    }

  netdev_lower_carrier_off(&priv->dev);
  wifi_rx_queue_free(priv);

  ret = netdev_lower_unregister(&priv->dev);
  if (ret < 0)
    {
      AIC_LOG_PRINTF("netdev_lower_unregister wlan0 failed: %d\n", ret);
      return ret;
    }

  g_wifi_netdev_registered = false;
  nxmutex_destroy(&priv->rx_lock);
  memset(priv, 0, sizeof(*priv));

  AIC_LOG_PRINTF("AIC8800 WiFi netdev unregistered\n");
  return OK;
}

int aic_wifi_netdev_receive_frame(const uint8_t *data, unsigned int len)
{
  struct aic_wifi_dev_s *priv = &g_wifi_dev;
  FAR netpkt_t *pkt;
  int ret;

  if (!g_wifi_netdev_registered || !priv->up)
    {
      return -ENETDOWN;
    }

  if (data == NULL || len == 0 || len > WIFI_MAX_FRAME)
    {
      return -EINVAL;
    }

  pkt = netpkt_alloc(&priv->dev, NETPKT_RX);
  if (pkt == NULL)
    {
      return -ENOMEM;
    }

  ret = netpkt_copyin(&priv->dev, pkt, data, len, 0);
  if (ret < 0)
    {
      netpkt_free(&priv->dev, pkt, NETPKT_RX);
      return ret;
    }

  nxmutex_lock(&priv->rx_lock);
  ret = netpkt_tryadd_queue(pkt, &priv->rx_queue);
  nxmutex_unlock(&priv->rx_lock);

  if (ret < 0)
    {
      netpkt_free(&priv->dev, pkt, NETPKT_RX);
      return ret;
    }

  netdev_lower_rxready(&priv->dev);
  return OK;
}
