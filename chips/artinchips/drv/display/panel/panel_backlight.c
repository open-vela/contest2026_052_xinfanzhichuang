/*
 * Copyright (c) 2022-2024, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: matteo <duanmt@artinchip.com>
 */

#include <nuttx/timers/pwm.h>
#include <errno.h>
#include <debug.h>

#include "panel_com.h"

static int pwm_fd = -1;

static int panel_backlight_init(void) {
  if (pwm_fd < 0) {
    pwm_fd = open("/dev/pwm0", O_RDWR);
    if (pwm_fd < 0) {
      pr_err("Failed to open PWM0: %d\n", errno);
      return -1;
    }
    pr_info("PWM0 opened successfully\n");
  }
  return 0;
}

void panel_backlight_enable(struct aic_panel *panel, u32 ms) {
  struct pwm_info_s info;
  int ret;

  pr_info("Starting backlight enable...\n");

  if (panel) {
    pr_info("Panel: %s\n", panel->name);
  }

  if (panel_backlight_init() < 0) {
    pr_err("Backlight init failed\n");
    return;
  }

  info.frequency = 1000;
  info.duty = 0xffff; /* 100% duty cycle */
  pr_info("Setting PWM frequency: %d, duty: 0x%x\n", info.frequency, info.duty);

  ret = ioctl(pwm_fd, PWMIOC_SETCHARACTERISTICS, (unsigned long)&info);
  if (ret < 0) {
    pr_err("Failed to set PWM characteristics: %d\n", errno);
    return;
  }
  pr_info("PWM characteristics set successfully\n");

  ret = ioctl(pwm_fd, PWMIOC_START, 0);
  if (ret < 0) {
    pr_err("Failed to start PWM: %d\n", errno);
    return;
  }
  pr_info("PWM started successfully, backlight enabled\n");
}

void panel_backlight_disable(struct aic_panel *panel, u32 ms) {
  int ret;

  pr_info("Starting backlight disable...\n");

  if (pwm_fd < 0) {
    pr_err("PWM not initialized\n");
    return;
  }

  ret = ioctl(pwm_fd, PWMIOC_STOP, 0);
  if (ret < 0) {
    pr_err("Failed to stop PWM: %d\n", errno);
    return;
  }
  pr_info("PWM stopped successfully, backlight disabled\n");
}
