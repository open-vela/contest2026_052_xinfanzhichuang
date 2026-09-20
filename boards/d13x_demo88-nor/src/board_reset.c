/*
 * Copyright (c) 2022-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: weilin.peng@artinchip.com
 */

#include <nuttx/config.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <sys/boardctl.h>

#include <aic_io.h>
#include <aic_core.h>
#include <hal_wdt.h>

/* WRI (Watchdog and Reset Interface) registers */
#define WRI_BASE 0x1900F000UL
#define WRI_BOOT_INFO (WRI_BASE + 0x100)
#define WRI_REBOOT_REASON_MASK 0xF0
#define WRI_REBOOT_REASON_SHIFT 4

#ifdef CONFIG_BOARDCTL_RESET_CAUSE
int board_reset_cause(struct boardioc_reset_cause_s *cause) {
  uint32_t boot_info;
  uint32_t reboot_reason;

  /* Read the BOOT_INFO register */
  boot_info = readl(WRI_BOOT_INFO);

  /* Extract the REBOOT_REASON field (bits 7-4) */
  reboot_reason =
      (boot_info & WRI_REBOOT_REASON_MASK) >> WRI_REBOOT_REASON_SHIFT;

  /* Map artinchip reboot reasons to NuttX reset causes */
  switch (reboot_reason) {
  case 0: /* NONE */
  case 1: /* 芯片上电 */
    cause->cause = BOARDIOC_RESETCAUSE_SYS_CHIPPOR;
    break;
  case 2: /* 实时时钟看门狗系统复位 */
    cause->cause = BOARDIOC_RESETCAUSE_SYS_RWDT;
    break;
  case 3: /* 低电压系统复位 */
    cause->cause = BOARDIOC_RESETCAUSE_SYS_BOR;
    break;
  case 4: /* 软件核心复位 */
    cause->cause = BOARDIOC_RESETCAUSE_CORE_SOFT;
    break;
  case 5: /* 深度睡眠核心复位 */
    cause->cause = BOARDIOC_RESETCAUSE_CORE_DPSP;
    break;
  case 6: /* 主看门狗核心复位 */
    cause->cause = BOARDIOC_RESETCAUSE_CORE_MWDT;
    break;
  case 7: /* 实时时钟看门狗核心复位 */
    cause->cause = BOARDIOC_RESETCAUSE_CORE_RWDT;
    break;
  case 8: /* 主看门狗 CPU 复位 */
    cause->cause = BOARDIOC_RESETCAUSE_CPU_MWDT;
    break;
  case 9: /* 软件 CPU 复位 */
    cause->cause = BOARDIOC_RESETCAUSE_CPU_SOFT;
    break;
  case 10: /* 实时时钟看门狗 CPU 复位 */
    cause->cause = BOARDIOC_RESETCAUSE_CPU_RWDT;
    break;
  case 11: /* 引脚复位 */
    cause->cause = BOARDIOC_RESETCAUSE_PIN;
    break;
  case 12: /* 低功耗复位 */
    cause->cause = BOARDIOC_RESETCAUSE_LOWPOWER;
    break;
  default:
    /* 未知复位原因 */
    cause->cause = BOARDIOC_RESETCAUSE_UNKOWN;
    break;
  }

  cause->flag = reboot_reason;

  return 0;
}
#endif /* CONFIG_BOARDCTL_RESET_CAUSE */

#ifdef CONFIG_BOARDCTL_RESET
int board_reset(int status) {
  struct aic_wdt wdt;
  uint32_t boot_info;

  /* Set reset reason to software reset */
  boot_info = readl(WRI_BOOT_INFO);
  boot_info =
      (boot_info & ~WRI_REBOOT_REASON_MASK) | (9 << WRI_REBOOT_REASON_SHIFT);
  writel(boot_info, WRI_BOOT_INFO);

  /* Configure watchdog for immediate reset */
  wdt.clr_thd = 0;
  wdt.irq_thd = 0;
  wdt.rst_thd = 1; /* 1 second reset */

  /* Set watchdog thresholds */
  hal_wdt_clr_thd_set(0, &wdt);
  hal_wdt_irq_thd_set(0, &wdt);
  hal_wdt_rst_thd_set(0, &wdt);

  /* Clear watchdog counter */
  hal_wdt_op_clr(0);

  /* Enable watchdog */
  hal_wdt_enable(1, 0);

  while (1) {
    /* wait for reset */
  }

  return OK; /* Should never reach here */
}
#endif /* CONFIG_BOARDCTL_RESET */
