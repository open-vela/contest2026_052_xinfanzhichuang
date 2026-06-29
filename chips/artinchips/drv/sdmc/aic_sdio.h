/****************************************************************************
 * arch/risc-v/src/artinchip/aic_sdio.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __ARCH_RISCV_SRC_ARTINCHIP_AIC_SDIO_H
#define __ARCH_RISCV_SRC_ARTINCHIP_AIC_SDIO_H
/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <nuttx/config.h>
#include <stdbool.h>
#include <stdint.h>
#include <nuttx/sdio.h>
/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/
#define AIC_SDIO_NSLOTS 2
struct sdio_dev_s *sdio_initialize(int slotno);
void sdio_mediachange(struct sdio_dev_s *dev, bool cardinslot);
void sdio_wrprotect(struct sdio_dev_s *dev, bool wrprotect);
#endif /* __ARCH_RISCV_SRC_ARTINCHIP_AIC_SDIO_H */