/*
 * Copyright (c) 2022, Artinchip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ARTINCHIP_AIC_SOC_H_
#define __ARTINCHIP_AIC_SOC_H_
#ifdef CONFIG_ARCH_CHIP_D12X
#  include "d12x/include/aic_soc.h"
#elif defined(CONFIG_ARCH_CHIP_D13X)
#  include "d13x/include/aic_soc.h"
#endif

#endif /* __ARTINCHIP_AIC_SOC_H_ */