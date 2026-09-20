/*
 * Copyright (c) 2024, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aic_drv_irq.h"
#include "aic_gpio_button.h"
#include <debug.h>
#include <errno.h>
#include <fcntl.h>
#include <nuttx/config.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <syslog.h>
#include <unistd.h>

#include "aic_hal_gpio.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BUTTON_DEVICE "/dev/buttons"
#define BUTTON_PIN1 "PA.0" /* 使用GPIO0作为示例，请根据实际硬件修改 */
#define BUTTON_PIN2 "PA.1" /* 使用GPIO0作为示例，请根据实际硬件修改 */
#define BUTTON_ID1 0
#define BUTTON_ID2 1

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int button_test_poll(void) {
  struct pollfd fds;
  int fd;
  int ret;
  uint8_t buffer[16];

  /* 打开按钮设备 */
  fd = open(BUTTON_DEVICE, O_RDONLY);
  if (fd < 0) {
    syslog(LOG_INFO, "ERROR: Failed to open %s: %d\n", BUTTON_DEVICE, errno);
    return -1;
  }

  syslog(LOG_INFO, "Button test started. Press and release the button...\n");
  syslog(LOG_INFO, "Press Ctrl+C to exit.\n");

  /* 配置poll */
  fds.fd = fd;
  fds.events = POLLIN;
  fds.revents = 0;

  while (1) {
    /* 等待按钮事件 */
    ret = poll(&fds, 1, -1);
    if (ret < 0) {
      if (errno == EINTR) {
        syslog(LOG_INFO, "Test interrupted by user.\n");
        break;
      }
      syslog(LOG_INFO, "ERROR: poll failed: %d\n", errno);
      break;
    }

    if (fds.revents & POLLIN) {
      /* 读取按钮状态 */
      ret = read(fd, buffer, sizeof(buffer));
      if (ret > 0) {
        /* 按钮状态在buffer中，格式为按钮ID和状态 */
        if (ret >= 2) {
          uint8_t state = (buffer[0] >> BUTTON_ID1) & 0x01;

          syslog(LOG_INFO, "Button %s\n", state ? "PRESSED" : "RELEASED");
        } else {
          syslog(LOG_INFO, "Received button event with %d bytes\n", ret);
        }
      } else if (ret < 0) {
        syslog(LOG_INFO, "ERROR: read failed: %d\n", errno);
        break;
      }
    }
  }

  close(fd);
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])

{
  int ret;
  int pin;
  syslog(LOG_INFO, "=== ArtInChip GPIO Button Driver Test ===\n");
  syslog(LOG_INFO, "This test demonstrates GPIO button event reporting.\n");

  /* 检查参数 */
  if (argc > 1) {
    syslog(LOG_INFO, "Usage: %s\n", argv[0]);
    syslog(LOG_INFO, "No arguments required.\n");
    return 0;
  }

  pin = hal_gpio_name2pin(BUTTON_PIN1);
  /* 注册GPIO按钮驱动 */
  /* 注意：这里假设按钮按下时为低电平（active low） */
  ret = aic_gpio_button_register(pin, BUTTON_ID1, true, NULL);
  if (ret < 0) {
    syslog(LOG_INFO, "ERROR: Failed to register GPIO button: %d\n", ret);
    syslog(LOG_INFO, "Please check:\n");
    syslog(LOG_INFO, "1. GPIO pin configuration\n");
    syslog(LOG_INFO, "2. Input subsystem support (CONFIG_INPUT_BUTTONS)\n");
    syslog(LOG_INFO, "3. GPIO driver support (CONFIG_DEV_GPIO)\n");
    return -1;
  }

  // ret = aic_gpio_button_register(pin2, BUTTON_ID2, true, NULL);
  //   if (ret < 0)
  //   {
  //     syslog(LOG_INFO,"ERROR: Failed to register GPIO button: %d\n", ret);
  //     syslog(LOG_INFO,"Please check:\n");
  //     syslog(LOG_INFO,"1. GPIO pin configuration\n");
  //     syslog(LOG_INFO,"2. Input subsystem support (CONFIG_INPUT_BUTTONS)\n");
  //     syslog(LOG_INFO,"3. GPIO driver support (CONFIG_DEV_GPIO)\n");
  //     return -1;
  //   }

  syslog(LOG_INFO, "GPIO button registered successfully.\n");
  syslog(LOG_INFO,
         "Pin=%s, ID=%d, Active Low=%s Pin=%s, ID=%d, Active Low=%s\n",
         BUTTON_PIN1, BUTTON_ID1, "true", BUTTON_PIN2, BUTTON_ID2, "true");

  /* 运行按钮测试 */
  ret = button_test_poll();

  /* 注销按钮驱动 */
  aic_gpio_button_unregister(pin);
  // aic_gpio_button_unregister(pin2);
  syslog(LOG_INFO, "GPIO button unregistered.\n");

  if (ret == 0) {
    syslog(LOG_INFO, "Button test completed successfully.\n");
  } else {
    syslog(LOG_INFO, "Button test completed with errors.\n");
  }

  return ret;
}