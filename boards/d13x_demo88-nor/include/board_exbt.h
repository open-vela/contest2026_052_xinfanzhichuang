/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __BOARDS_D13X_DEMO88_NOR_INCLUDE_BOARD_EXBT_H
#define __BOARDS_D13X_DEMO88_NOR_INCLUDE_BOARD_EXBT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void aic_board_exbt_detect(void);
bool aic_board_exbt_uart0_selected(void);
bool aic_board_exbt_i2c0_available(void);
bool aic_board_exbt_pinmux_skip(const char *pin_name);
const char *aic_board_exbt_mode_name(void);

#ifdef __cplusplus
}
#endif

#endif
