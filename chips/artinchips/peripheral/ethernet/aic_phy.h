/*
 * Copyright (c) 2022-2026, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __AIC_PHY_H
#define __AIC_PHY_H

#include <stdbool.h>
#include <stdint.h>

#ifndef MAX_ETH_MAC_PORT
#define MAX_ETH_MAC_PORT 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
  int speed;
  uint8_t autoneg;
  uint8_t duplex;
  uint8_t pause;
  uint8_t asym_pause;
  uint8_t link;
  uint8_t autoneg_complete;
} aic_phy_device_t;

#define SPEED_10      10
#define SPEED_100     100
#define SPEED_1000    1000
#define SPEED_UNKNOWN -1

#define DUPLEX_HALF    0x00
#define DUPLEX_FULL    0x01
#define DUPLEX_UNKNOWN 0xff

#define MII_BMCR      0x00
#define MII_BMSR      0x01
#define MII_PHYSID1   0x02
#define MII_PHYSID2   0x03
#define MII_ADVERTISE 0x04
#define MII_LPA       0x05

#define BMCR_FULLDPLX  0x0100
#define BMCR_ANRESTART 0x0200
#define BMCR_ISOLATE   0x0400
#define BMCR_PDOWN     0x0800
#define BMCR_ANENABLE  0x1000
#define BMCR_SPEED100  0x2000
#define BMCR_LOOPBACK  0x4000
#define BMCR_RESET     0x8000

#define BMSR_LSTATUS      0x0004
#define BMSR_ANEGCAPABLE  0x0008
#define BMSR_ANEGCOMPLETE 0x0020
#define BMSR_10HALF       0x0800
#define BMSR_10FULL       0x1000
#define BMSR_100HALF      0x2000
#define BMSR_100FULL      0x4000

#define ADVERTISE_CSMA       0x0001
#define ADVERTISE_10HALF     0x0020
#define ADVERTISE_10FULL     0x0040
#define ADVERTISE_100HALF    0x0080
#define ADVERTISE_100FULL    0x0100
#define ADVERTISE_PAUSE_CAP  0x0400
#define ADVERTISE_PAUSE_ASYM 0x0800
#define ADVERTISE_ALL \
  (ADVERTISE_10HALF | ADVERTISE_10FULL | ADVERTISE_100HALF | \
   ADVERTISE_100FULL)

#define LPA_10HALF    0x0020
#define LPA_10FULL    0x0040
#define LPA_100HALF   0x0080
#define LPA_100FULL   0x0100
#define LPA_PAUSE_CAP 0x0400

extern aic_phy_device_t phy_device[MAX_ETH_MAC_PORT];

int aicphy_init(uint32_t port);
int aicphy_read_abilities(uint32_t port);
int aicphy_config_aneg(uint32_t port);
int aicphy_read_status(uint32_t port);

#ifdef __cplusplus
}
#endif

#endif /* __AIC_PHY_H */
