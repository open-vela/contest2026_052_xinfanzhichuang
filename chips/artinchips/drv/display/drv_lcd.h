/*
 * Copyright (C) 2024-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: huahui.mai@artinchip.com
 */

#ifndef _DRV_LCD_H_
#define _DRV_LCD_H_

#include <nuttx/lcd/lcd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LCD device registration */
int lcd_register_device(void);
void lcd_unregister_device(void);
FAR struct lcd_dev_s *aic_lcd_getdev(void);

#ifdef __cplusplus
}
#endif

#endif /* _DRV_LCD_H_ */
