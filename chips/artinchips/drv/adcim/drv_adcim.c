/*
 * Copyright (c) 2023-2024, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aic_core.h"
#include "hal_adcim.h"

int drv_adcim_init(void)
{
    if (hal_adcim_probe())
        return -DRV_ERROR;
    else
        return EOK;
}
