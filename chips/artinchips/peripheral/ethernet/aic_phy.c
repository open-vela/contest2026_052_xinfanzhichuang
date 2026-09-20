/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/config.h>

#include <string.h>

#include <aic_core.h>
#include <aic_log.h>

#include "aic_mac.h"
#include "aic_phy.h"

aic_phy_device_t phy_device[MAX_ETH_MAC_PORT];

static int phy_modify(uint32_t port, uint32_t regnum, uint16_t mask,
                      uint16_t set)
{
  uint16_t val;
  uint16_t newval;
  int ret;

  ret = aicmac_read_phy_reg(port, regnum, &val);
  if (ret)
    {
      return ETH_ERROR;
    }

  newval = (val & ~mask) | set;
  if (newval == val)
    {
      return ETH_SUCCESS;
    }

  ret = aicmac_write_phy_reg(port, regnum, newval);
  return ret ? ETH_ERROR : ETH_SUCCESS;
}

int aicphy_read_abilities(uint32_t port)
{
  aic_phy_device_t *phydev = &phy_device[port];
  uint16_t bmsr;
  int ret;

  ret = aicmac_read_phy_reg(port, MII_BMSR, &bmsr);
  if (ret)
    {
      return ETH_ERROR;
    }

  phydev->autoneg = (bmsr & BMSR_ANEGCAPABLE) ? 1 : 0;
  return ETH_SUCCESS;
}

int aicphy_config_aneg(uint32_t port)
{
  aic_phy_device_t *phydev = &phy_device[port];
  uint16_t adv = ADVERTISE_CSMA | ADVERTISE_PAUSE_CAP | ADVERTISE_ALL;
  int ret;

  if (!phydev->autoneg)
    {
      return ETH_SUCCESS;
    }

  ret = aicmac_write_phy_reg(port, MII_ADVERTISE, adv);
  if (ret)
    {
      return ETH_ERROR;
    }

  return phy_modify(port, MII_BMCR, BMCR_ISOLATE,
                    BMCR_ANENABLE | BMCR_ANRESTART);
}

static int aicphy_read_link(uint32_t port, uint16_t *bmsr)
{
  int ret;

  ret = aicmac_read_phy_reg(port, MII_BMSR, bmsr);
  if (ret)
    {
      return ret;
    }

  /* BMSR link status is latch-low, so read twice for current state. */

  return aicmac_read_phy_reg(port, MII_BMSR, bmsr);
}

static int aicphy_read_fixed(uint32_t port)
{
  aic_phy_device_t *phydev = &phy_device[port];
  uint16_t bmcr;
  int ret;

  ret = aicmac_read_phy_reg(port, MII_BMCR, &bmcr);
  if (ret)
    {
      return ETH_ERROR;
    }

  phydev->duplex = (bmcr & BMCR_FULLDPLX) ? DUPLEX_FULL : DUPLEX_HALF;
  phydev->speed = (bmcr & BMCR_SPEED100) ? SPEED_100 : SPEED_10;
  phydev->pause = 0;
  phydev->asym_pause = 0;
  return ETH_SUCCESS;
}

static int aicphy_resolve_aneg(uint32_t port)
{
  aic_phy_device_t *phydev = &phy_device[port];
  uint16_t adv;
  uint16_t lpa;
  uint16_t common;
  int ret;

  ret = aicmac_read_phy_reg(port, MII_ADVERTISE, &adv);
  if (ret)
    {
      return ETH_ERROR;
    }

  ret = aicmac_read_phy_reg(port, MII_LPA, &lpa);
  if (ret)
    {
      return ETH_ERROR;
    }

  common = adv & lpa;

  if (common & LPA_100FULL)
    {
      phydev->speed = SPEED_100;
      phydev->duplex = DUPLEX_FULL;
    }
  else if (common & LPA_100HALF)
    {
      phydev->speed = SPEED_100;
      phydev->duplex = DUPLEX_HALF;
    }
  else if (common & LPA_10FULL)
    {
      phydev->speed = SPEED_10;
      phydev->duplex = DUPLEX_FULL;
    }
  else
    {
      phydev->speed = SPEED_10;
      phydev->duplex = DUPLEX_HALF;
    }

  phydev->pause = (common & LPA_PAUSE_CAP) ? 1 : 0;
  phydev->asym_pause = 0;
  return ETH_SUCCESS;
}

int aicphy_read_status(uint32_t port)
{
  aic_phy_device_t *phydev = &phy_device[port];
  uint16_t bmcr;
  uint16_t bmsr = 0;
  int ret;

  ret = aicmac_read_phy_reg(port, MII_BMCR, &bmcr);
  if (ret)
    {
      return ETH_ERROR;
    }

  if (!(bmcr & BMCR_ANRESTART))
    {
      ret = aicphy_read_link(port, &bmsr);
      if (ret)
        {
          return ETH_ERROR;
        }
    }

  phydev->link = (bmsr & BMSR_LSTATUS) ? 1 : 0;
  phydev->autoneg_complete = (bmsr & BMSR_ANEGCOMPLETE) ? 1 : 0;

  if (phydev->autoneg && !phydev->autoneg_complete)
    {
      phydev->link = 0;
    }

  if (!phydev->link)
    {
      phydev->speed = SPEED_UNKNOWN;
      phydev->duplex = DUPLEX_UNKNOWN;
      phydev->pause = 0;
      phydev->asym_pause = 0;
      return ETH_SUCCESS;
    }

  if (phydev->autoneg && phydev->autoneg_complete)
    {
      return aicphy_resolve_aneg(port);
    }

  return aicphy_read_fixed(port);
}

int aicphy_init(uint32_t port)
{
  aic_phy_device_t *phydev = &phy_device[port];
  uint16_t id1 = 0;
  uint16_t id2 = 0;
  int ret;

  memset(phydev, 0, sizeof(*phydev));
  phydev->autoneg = mac_config[port].autonegotiation;
  phydev->speed = SPEED_UNKNOWN;
  phydev->duplex = DUPLEX_UNKNOWN;

  aicmac_read_phy_reg(port, MII_PHYSID1, &id1);
  aicmac_read_phy_reg(port, MII_PHYSID2, &id2);
  pr_info("GMAC%u PHY addr %u id 0x%04x:0x%04x\n", (unsigned int)port,
          (unsigned int)mac_config[port].phyaddr, id1, id2);

  ret = aicphy_read_abilities(port);
  if (ret)
    {
      return ret;
    }

  return aicphy_config_aneg(port);
}
