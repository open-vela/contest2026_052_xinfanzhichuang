#ifdef CONFIG_USING_MTD
#include "aic_mtd.h"
#endif

#include "drv_qspi.h"
#ifdef CONFIG_USING_SFUD
#include "sfud_def.h"
#endif
#include <aic_drv_i2c.h>

#ifdef CONFIG_AIC_SDMC_DRV
#include <nuttx/sdio.h>
#endif

#include <aic_soc.h>
#include <nuttx/arch.h>
#include <nuttx/config.h>
#include <nuttx/init.h>
#include <syslog.h>
#include <sys/mount.h>
#include <nuttx/video/fb.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/timers/pwm.h>
#include <drv_rtc.h>
#include <nuttx/timers/watchdog.h>
/* Include panel header for backlight control */
#include <drv/display/panel/panel_com.h>
/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: riscv_initial_extctx_state
 *
 * Description:
 *   Initialize the chip-specific portion of the TCB's exception context.
 *
 ****************************************************************************/

#ifdef CONFIG_ARCH_RISCV_INTXCPT_EXTENSIONS
void riscv_initial_extctx_state(struct tcb_s *tcb) {
  /* Add custom logic here to initialize registers from vendor code */
  register uintptr_t gp asm("gp");
  tcb->xcp.regs[REG_GP] = gp;
}
#endif

extern int gt911_driver_init(FAR struct i2c_master_s *i2c_dev);
extern void aic_board_sysclk_init(void);
extern void aic_board_pinmux_init(void);
#ifdef CONFIG_PWM
extern int drv_pwm_nuttx_init(void);
#endif

void artinchip_board_initialize(void) {
  syslog(LOG_ERR, "artinchip_board_initialize, revision: %s\n",
         ARTINCHIP_GIT_REVERSION);
  aic_board_pinmux_init();

#ifdef CONFIG_AIC_USING_RTC
  drv_rtc_init();
#endif

#ifdef CONFIG_AIC_USING_WDT
  extern int aic_wdt_initialize(void);
  aic_wdt_initialize();
#endif
}

void artinchip_board_late_initialize(void) {
  int ret = 0;
#ifdef CONFIG_AIC_USING_DMA
  extern int aic_dma_initialize(void);
  syslog(LOG_INFO, "Start to initialize DMA\n");
  ret = aic_dma_initialize();
  if (ret < 0) {
    syslog(LOG_ERR, "Failed to initialize DMA: %d\n", ret);
  }
#endif

#if defined(CONFIG_USING_SFUD) && defined(CONFIG_AIC_SPINOR_DRV)
  extern sfud_flash *spinor_init(unsigned int spi_bus);
  sfud_flash *sfud = spinor_init(0);
  if (sfud == NULL) {
    syslog(LOG_ERR, "Failed to probe spinor flash.\n");
  }
#endif

#ifdef CONFIG_FS_PROCFS
  /* Mount the procfs file system */
  ret = mount(NULL, "/proc", "procfs", 0, NULL);
  if (ret < 0) {
    syslog(LOG_ERR, "ERROR: Failed to mount procfs at /proc: %d\n", ret);
  }
#endif

#ifdef CONFIG_AIC_USING_I2C0
  FAR struct i2c_master_s *i2c0 = aic_i2cbus_initialize(0);

#ifdef CONFIG_AIC_TOUCH_PANEL_GT911
  syslog(LOG_INFO, "Start to initialize GT911 driver\n");
  ret = gt911_driver_init(i2c0);
  if (ret < 0) {
    syslog(LOG_ERR, "Failed to initialize GT911 driver: %d\n", ret);
  }
#endif
#endif

#ifdef CONFIG_AIC_USING_AUDIO
  extern int aic_audio_register(const char *name);
  syslog(LOG_INFO, "Start to register audio driver\n");
  ret = aic_audio_register("pcm0p");
  if (ret < 0) {
    syslog(LOG_ERR, "Failed to register audio driver: %d\n", ret);
  }
#endif

#ifdef CONFIG_AIC_USING_PWM
  syslog(LOG_INFO, "Start to initialize PWM driver\n");
  ret = drv_pwm_nuttx_init();
  if (ret < 0) {
    syslog(LOG_ERR, "Failed to initialize PWM driver: %d\n", ret);
  }
#endif

#ifdef CONFIG_VIDEO_FB
  syslog(LOG_INFO, "Start to initialize framebuffer\n");
  ret = up_fbinitialize(0);
  if (ret < 0) {
    syslog(LOG_ERR, "Failed to initialize framebuffer: %d\n", ret);
  }
#endif

#ifdef CONFIG_AIC_SDMC_DRV
  extern int aic_mmcsd_initialize(int);
  syslog(LOG_INFO, "Start to initialize SDMC driver\n");
  aic_mmcsd_initialize(1);
#endif
}

/**
 * Stub functions for address environment translation.
 * These can be overridden by board-specific implementations if needed.
 * By default, they perform identity mapping.
 */
uintptr_t up_addrenv_va_to_pa(void *va) { return (uintptr_t)va; }

void *up_addrenv_pa_to_va(uintptr_t pa) { return (void *)pa; }