/****************************************************************************
 *  * vendor/artinchip/boards/d13x_rivoride_m4-nor/src/artinchip_boot.c
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/board.h>
#include <nuttx/arch.h>
#include <arch/board/board.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <arch/chip/aic_soc.h>
#include <aic_core.h>
#include <aic_hal.h>
#include <aic_utils.h>
#include <aic_hal_gpio.h>
#include <syslog.h>
#include <nuttx/video/fb.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/timers/pwm.h>
#include <drv/drv_rtc.h>
#include <nuttx/timers/watchdog.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/
#ifdef CONFIG_BOARD_EARLY_INITIALIZE
extern void artinchip_board_initialize(void);
void board_early_initialize(void) { artinchip_board_initialize(); }
#endif

#ifdef CONFIG_BOARD_LATE_INITIALIZE
extern void artinchip_board_late_initialize(void);
void board_late_initialize(void) { artinchip_board_late_initialize(); }
#endif

int board_app_initialize(uintptr_t arg) { return 0; }
