/*
 * Copyright (c) 2022-2024, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ARTINCHIP_AIC_GPIO_ID_H__
#define __ARTINCHIP_AIC_GPIO_ID_H__

#ifdef CONFIG_ARCH_CHIP_D12X
#  include "../d12x/include/aic_gpio_id.h"
#elif defined(CONFIG_ARCH_CHIP_D13X)
#  include "../d13x/include/aic_gpio_id.h"
#endif

#endif /* __ARTINCHIP_AIC_GPIO_ID_H__ */
