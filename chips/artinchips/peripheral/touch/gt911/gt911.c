/****************************************************************************
 * vendor/artinchip/chips/artinchips/peripheral/touch/gt911/gt911_nuttx.c
 *
 * Copyright (C) 2023 ArtInChip Technology Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/wqueue.h>
#include <nuttx/clock.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/input/touchscreen.h>

#include <drv/aic_drv_i2c.h>
#include <aic_hal_gpio.h>
#include <aic_drv_irq.h>
#include <aic_core.h>
#include <gt911.h>
/****************************************************************************
 * Pre-Processor Definitions
 ****************************************************************************/

#define GT911_I2C_ADDR 0x5d
#define GT911_I2C_FREQ 400000

#ifndef CONFIG_AIC_TOUCH_X_COORDINATE_RANGE
#define CONFIG_AIC_TOUCH_X_COORDINATE_RANGE 1080
#endif

#ifndef CONFIG_AIC_TOUCH_Y_COORDINATE_RANGE
#define CONFIG_AIC_TOUCH_Y_COORDINATE_RANGE 600
#endif

#ifdef CONFIG_AIC_TOUCH_GT911_FW_MODE_FLAG_RDONLY
#define GT911_FW_DEFAULT_MODE 0x02
#else
#define GT911_FW_DEFAULT_MODE 0x00
#endif

#define GT911_FW_MODE_INT_RX 0x00
#define GT911_FW_MODE_FLAG_RDONLY 0x02

#ifdef CONFIG_AIC_TOUCH_PANEL_GT911

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct aic_gt911_dev_s {
  /* Standard Touchscreen Lower Half */
  struct touch_lowerhalf_s lower;

  /* I2C Interface */
  struct i2c_master_s *i2c;

  /* Work Queue for Bottom Half Handling */
  struct work_s work;

  /* Pin Configurations */
  unsigned int irq_pin;
  unsigned int rst_pin;
  int irq_num;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct aic_gt911_dev_s g_gt911_dev;

#ifdef CONFIG_AIC_TOUCH_GT911_LOAD_FW_AT_BOOT
/* Default GT911 firmware table. */
static uint8_t g_gt911_cfg_tbl[] = {
    0x6b, 0x00, 0x04, 0x58, 0x02, 0x05, 0x0d, 0x00, 0x01, 0x0f, 0x28, 0x0f,
    0x50, 0x32, 0x03, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x8a, 0x2a, 0x0c, 0x45, 0x47, 0x0c, 0x08, 0x00, 0x00,
    0x00, 0x40, 0x03, 0x2c, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x64, 0x32,
    0x00, 0x00, 0x00, 0x28, 0x64, 0x94, 0xd5, 0x02, 0x07, 0x00, 0x00, 0x04,
    0x95, 0x2c, 0x00, 0x8b, 0x34, 0x00, 0x82, 0x3f, 0x00, 0x7d, 0x4c, 0x00,
    0x7a, 0x5b, 0x00, 0x7a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x18, 0x16, 0x14, 0x12, 0x10, 0x0e, 0x0c, 0x0a,
    0x08, 0x06, 0x04, 0x02, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x18,
    0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x24, 0x13, 0x12, 0x10, 0x0f,
    0x0a, 0x08, 0x06, 0x04, 0x02, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x79, 0x01,
};
#endif

/****************************************************************************
 * Private Helper Functions
 ****************************************************************************/

static int gt911_i2c_write(struct aic_gt911_dev_s *priv, uint8_t *buffer,
                           size_t buflen) {
  struct i2c_msg_s msg;

  if (!priv->i2c) {
    return -ENODEV;
  }

  msg.frequency = GT911_I2C_FREQ;
  msg.addr = GT911_I2C_ADDR;
  msg.flags = 0;
  msg.buffer = buffer;
  msg.length = buflen;

  return I2C_TRANSFER(priv->i2c, &msg, 1);
}

static int gt911_i2c_read(struct aic_gt911_dev_s *priv, uint16_t reg,
                          uint8_t *buffer, size_t buflen) {
  struct i2c_msg_s msg[2];
  uint8_t reg_addr[2];

  if (!priv->i2c) {
    return -ENODEV;
  }

  reg_addr[0] = (reg >> 8) & 0xff;
  reg_addr[1] = reg & 0xff;

  msg[0].frequency = GT911_I2C_FREQ;
  msg[0].addr = GT911_I2C_ADDR;
  msg[0].flags = 0; /* Write address */
  msg[0].buffer = reg_addr;
  msg[0].length = 2;

  msg[1].frequency = GT911_I2C_FREQ;
  msg[1].addr = GT911_I2C_ADDR;
  msg[1].flags = I2C_M_READ; /* Read data */
  msg[1].buffer = buffer;
  msg[1].length = buflen;

  return I2C_TRANSFER(priv->i2c, msg, 2);
}

#ifdef CONFIG_AIC_TOUCH_GT911_LOAD_FW_AT_BOOT
static int gt911_modify_firmware(struct aic_gt911_dev_s *priv, uint16_t x_range,
                                 uint16_t y_range, uint8_t mode) {
  uint8_t cfg[GT911_ADDR_LEN + sizeof(g_gt911_cfg_tbl)];
  uint8_t chk[4];
  uint8_t sum = 0;
  size_t cfg_len;
  size_t i;
  int ret;

  if (!priv || !priv->i2c) {
    return -ENODEV;
  }

  cfg[0] = (uint8_t)((GT911_CONFIG_REG >> 8) & 0xff);
  cfg[1] = (uint8_t)(GT911_CONFIG_REG & 0xff);
  memcpy(&cfg[GT911_ADDR_LEN], g_gt911_cfg_tbl, sizeof(g_gt911_cfg_tbl));
  cfg_len = sizeof(g_gt911_cfg_tbl);

  /* Keep default table, only override the requested parameters. */
  cfg[3] = (uint8_t)(x_range & 0xff);
  cfg[4] = (uint8_t)((x_range >> 8) & 0xff);
  cfg[5] = (uint8_t)(y_range & 0xff);
  cfg[6] = (uint8_t)((y_range >> 8) & 0xff);

  cfg[8] &= 0xfc;
  if (mode & 0x02) {
    cfg[8] |= 0x02;
  }

  ret = gt911_i2c_write(priv, cfg, sizeof(cfg));
  if (ret < 0) {
    syslog(LOG_ERR, "GT911 firmware write failed: %d\n", ret);
    return ret;
  }

  chk[0] = (uint8_t)((GT911_CHECK_SUM >> 8) & 0xff);
  chk[1] = (uint8_t)(GT911_CHECK_SUM & 0xff);
  chk[2] = 0;

  /* GT911 checksum excludes the last 2 bytes (checksum + config_fresh). */
  for (i = GT911_ADDR_LEN; i < GT911_ADDR_LEN + cfg_len - 2; i++) {
    sum += cfg[i];
  }

  chk[2] = (uint8_t)(~sum + 1);
  chk[3] = 1;

  ret = gt911_i2c_write(priv, chk, sizeof(chk));
  if (ret < 0) {
    syslog(LOG_ERR, "GT911 firmware checksum write failed: %d\n", ret);
    return ret;
  }

  return OK;
}

static int gt911_load_firmware(struct aic_gt911_dev_s *priv) {
  return gt911_modify_firmware(priv, CONFIG_AIC_TOUCH_X_COORDINATE_RANGE,
                               CONFIG_AIC_TOUCH_X_COORDINATE_RANGE,
                               GT911_FW_DEFAULT_MODE);
}

int gt911_update_firmware(uint16_t x_range, uint16_t y_range, uint8_t mode) {
  uint8_t fw_mode = GT911_FW_MODE_INT_RX;

  if (mode & GT911_FW_MODE_FLAG_RDONLY) {
    fw_mode = GT911_FW_MODE_FLAG_RDONLY;
  }

  return gt911_modify_firmware(&g_gt911_dev, x_range, y_range, fw_mode);
}
#endif

static void gt911_config(struct aic_gt911_dev_s *priv) {
  uint8_t id[4];
  int ret;

  struct i2c_msg_s msg[2];
  uint8_t reg_addr[2] = {(GT911_PRODUCT_ID >> 8) & 0xff,
                         GT911_PRODUCT_ID & 0xff};

  msg[0].frequency = GT911_I2C_FREQ;
  msg[0].addr = GT911_I2C_ADDR;
  msg[0].flags = 0;
  msg[0].buffer = reg_addr;
  msg[0].length = 2;

  msg[1].frequency = GT911_I2C_FREQ;
  msg[1].addr = GT911_I2C_ADDR;
  msg[1].flags = I2C_M_READ;
  msg[1].buffer = id;
  msg[1].length = 3;

  ret = I2C_TRANSFER(priv->i2c, msg, 2);
  if (ret >= 0) {
    id[3] = '\0';
    syslog(LOG_ERR, "GT911 Found at 0x%02x, Product ID: %s (ret: %d)\n",
           GT911_I2C_ADDR, id, ret);
    return;
  }

  syslog(LOG_ERR, "Failed to find GT911 on both 0x5D and 0x14 (ret: %d)\n",
         ret);

  iinfo("Sending GT911 Configuration...\n");
}

/****************************************************************************
 * Interrupt Handling & Worker
 ****************************************************************************/

static void gt911_worker(FAR void *arg) {
  static uint16_t s_last_x;
  static uint16_t s_last_y;
  static bool s_is_pressed;
  FAR struct aic_gt911_dev_s *priv = (FAR struct aic_gt911_dev_s *)arg;
  struct touch_sample_s sample;
  uint8_t status[1];
  uint8_t touch[8]; /* 8 bytes per point: id, xL, xH, yL, yH, wL, wH, rsv */
  uint8_t clear_status[] = {0x81, 0x4e, 0x00};
  uint8_t status_code;
  uint8_t touched_points;
  uint16_t x, y;
  int retry;
  int ret;
  int clear_ret;

  /* Read Status Register */
  ret = -EIO;
  for (retry = 0; retry < 3; retry++) {
    ret = gt911_i2c_read(priv, GT911_READ_STATUS, status, 1);
    if (ret >= 0) {
      break;
    }

    up_udelay(200);
  }

  if (ret < 0) {
    /* Reduce log flood in polling mode while preserving diagnostics. */
    goto errout; /* Re-enable IRQ even on error */
  }

  status_code = status[0] & 0x80;
  touched_points = status[0] & 0x0f;

  if (status_code == 0) {
    goto errout;
  }

  memset(&sample, 0, sizeof(struct touch_sample_s));

  if (touched_points >= 1) {
    /* Read First Touch Point,single point touch */
    ret = -EIO;
    for (retry = 0; retry < 3; retry++) {
      ret = gt911_i2c_read(priv, GT911_POINT1_REG, touch, sizeof(touch));
      if (ret >= 0) {
        break;
      }

      up_udelay(200);
    }

    if (ret < 0) {
      goto errout;
    }

    x = touch[1] + (touch[2] << 8);
    y = touch[3] + (touch[4] << 8);

    sample.npoints = 1;
    sample.point[0].id = touch[0] & 0x0f;
    sample.point[0].x = x;
    sample.point[0].y = y;

    if (!s_is_pressed) {
      sample.point[0].flags = TOUCH_DOWN | TOUCH_ID_VALID | TOUCH_POS_VALID;
      s_is_pressed = true;
      syslog(LOG_ERR, "Touch Down: x=%d, y=%d\n", x, y);
    } else if (x != s_last_x || y != s_last_y) {
      sample.point[0].flags = TOUCH_MOVE | TOUCH_ID_VALID | TOUCH_POS_VALID;
      syslog(LOG_ERR, "Touch Move: x=%d, y=%d\n", x, y);
    } else {
      /* No change in position, skip reporting to avoid redundant LVGL waking */
      goto clear_status;
    }

    s_last_x = x;
    s_last_y = y;
    sample.point[0].timestamp = touch_get_time();

    /* Push event to Upper Half (if registered) */
    if (priv->lower.priv) {
      touch_event(priv->lower.priv, &sample);
    }
  } else {
    /* Touch Up */
    if (s_is_pressed) {
      sample.npoints = 1;
      sample.point[0].id = 0;
      sample.point[0].flags = TOUCH_UP | TOUCH_ID_VALID;
      sample.point[0].timestamp = touch_get_time();
      s_is_pressed = false;

      syslog(LOG_ERR, "Touch Up\n");

      if (priv->lower.priv) {
        touch_event(priv->lower.priv, &sample);
      }
    }
  }

clear_status:
  /* Clear Status Register to allow next interrupt */
  clear_ret = gt911_i2c_write(priv, clear_status, sizeof(clear_status));
  if (clear_ret < 0) {
    syslog(LOG_ERR, "GT911 clear status write failed: %d\n", clear_ret);
  }

errout :
#ifdef CONFIG_AIC_TOUCH_GT911_USE_IRQ
{
  unsigned int pending;
  unsigned int level;

  hal_gpio_get_irq_stat(GPIO_GROUP(priv->irq_pin),
                        GPIO_GROUP_PIN(priv->irq_pin), &pending);
  hal_gpio_get_value(GPIO_GROUP(priv->irq_pin), GPIO_GROUP_PIN(priv->irq_pin),
                     &level);
  syslog(LOG_ERR, "GT911 IRQ worker-exit: pending=%u level=%u\n", pending,
         level);

  /* If INT remains low, schedule one more worker pass to avoid deadlock. */
  if (level == 0) {
    up_disable_irq(priv->irq_num);
    work_queue(HPWORK, &priv->work, gt911_worker, priv, MSEC2TICK(10));
    return;
  }
}

  /* Clear GPIO IRQ pending bit before unmasking CPU IRQ. */
  hal_gpio_clr_irq_stat(GPIO_GROUP(priv->irq_pin),
                        GPIO_GROUP_PIN(priv->irq_pin));

  /* Re-enable Interrupts */
  up_enable_irq(priv->irq_num);
#else
  /* Requeue polling work when IRQ mode is disabled. */
  work_queue(HPWORK, &priv->work, gt911_worker, priv,
             MSEC2TICK(CONFIG_AIC_TOUCH_GT911_POLL_INTERVAL_MS));
#endif
}

#ifdef CONFIG_AIC_TOUCH_GT911_USE_IRQ
static int gt911_isr(int irq, FAR void *context, FAR void *arg) {
  FAR struct aic_gt911_dev_s *priv = (FAR struct aic_gt911_dev_s *)arg;
  /* Disable interrupt and schedule work */
  up_disable_irq(priv->irq_num);
  work_queue(HPWORK, &priv->work, gt911_worker, priv, 0);
  return OK;
}
#endif

/****************************************************************************
 * Initialization Functions
 ****************************************************************************/

static void gt911_hw_reset(struct aic_gt911_dev_s *priv) {
  unsigned int g_rst, p_rst, g_irq, p_irq;

  g_rst = GPIO_GROUP(priv->rst_pin);
  p_rst = GPIO_GROUP_PIN(priv->rst_pin);
  g_irq = GPIO_GROUP(priv->irq_pin);
  p_irq = GPIO_GROUP_PIN(priv->irq_pin);

  /* 1. Ensure RST and INT are low */
  hal_gpio_set_func(g_rst, p_rst, 1);
  hal_gpio_direction_output(g_rst, p_rst);
  hal_gpio_clr_output(g_rst, p_rst);

  hal_gpio_set_func(g_irq, p_irq, 1);
  hal_gpio_direction_output(g_irq, p_irq);
  hal_gpio_clr_output(g_irq, p_irq);

  usleep(10000); /* Keep reset low for >=10ms */

  /* 2. Select Address 0x5D: INT must be low when RST goes High */
  hal_gpio_clr_output(g_irq, p_irq);
  usleep(2000);

  /* 3. Release Reset */
  hal_gpio_set_output(g_rst, p_rst);
  usleep(5000);

  /* 4. Release RST ownership*/
  hal_gpio_direction_input(g_rst, p_rst);

  /* 5. Keep INT low after reset to complete address/sample latch window. */
  hal_gpio_direction_output(g_irq, p_irq);
  hal_gpio_clr_output(g_irq, p_irq);
  usleep(50000);

  /* 6. Release INT line ownership after address latch.
   * In IRQ mode GT911 drives this line as interrupt output.
   * In polling mode keep it input (high-z) and do not drive it.
   */
  hal_gpio_direction_input(g_irq, p_irq);

  usleep(100000); /* Chip internal initialization */

#ifdef CONFIG_AIC_TOUCH_GT911_USE_IRQ
  syslog(LOG_ERR, "GT911 Reset: INT configured for IRQ mode.\n");
#else
  syslog(LOG_ERR, "GT911 Reset: INT released (polling mode).\n");
#endif
}

static int gt911_control(FAR struct touch_lowerhalf_s *lower, int cmd,
                         unsigned long arg) {
  int ret = -ENOTTY;

  switch (cmd) {
  case TSIOC_GETMAXPOINTS: {
    FAR uint8_t *maxpoint = (FAR uint8_t *)((uintptr_t)arg);
    if (maxpoint) {
      *maxpoint = 1; /* Current driver implementation supports 1 point */
      ret = OK;
    } else {
      ret = -EINVAL;
    }
  } break;

  default:
    break;
  }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int gt911_driver_init(FAR struct i2c_master_s *i2c_dev) {
  struct aic_gt911_dev_s *priv = &g_gt911_dev;
  int ret;
#ifdef CONFIG_AIC_TOUCH_GT911_USE_IRQ
  unsigned int g;
  unsigned int p;
#endif

  if (!i2c_dev) {
    iinfo("Initializing GT911 Failed: no I2C device available\n");
    return -ENODEV;
  }

  iinfo("Initializing GT911 Driver...\n");

  /* Initialize Structure */
  memset(priv, 0, sizeof(struct aic_gt911_dev_s));
  priv->i2c = i2c_dev;
  priv->irq_pin = hal_gpio_name2pin(CONFIG_AIC_TOUCH_PANEL_INT_PIN);
  priv->rst_pin = hal_gpio_name2pin(CONFIG_AIC_TOUCH_PANEL_RST_PIN);

  /* lower half initialization if needed (e.g. methods) */
  priv->lower.control = gt911_control;
  priv->lower.maxpoint = 1; /* Current driver implementation supports 1 point */

  if (priv->irq_pin < 0 || priv->rst_pin < 0) {
    ierr("Invalid GPIO Pins for GT911\n");
    return -EINVAL;
  }

  priv->irq_num = AIC_GPIO_TO_IRQ(priv->irq_pin);

  /* Hardware Reset Sequence */
  gt911_hw_reset(priv);

  /* Send Configuration */
  gt911_config(priv);

  /* Optional firmware loading: disable by default for panel compatibility. */
#ifdef CONFIG_AIC_TOUCH_GT911_LOAD_FW_AT_BOOT
  ret = gt911_load_firmware(priv);
  if (ret < 0) {
    ierr("GT911 load firmware failed: %d\n", ret);
    return ret;
  }
#else
  iinfo("GT911 skip firmware load at boot\n");
#endif

  /* Register Touch Device (Upper Half) at /dev/input0 */
  ret = touch_register(&priv->lower, "/dev/input0", 1);
  if (ret < 0) {
    ierr("touch_register failed: %d\n", ret);
    return ret;
  }

#ifdef CONFIG_AIC_TOUCH_GT911_USE_IRQ
  /* Configure Interrupt System */
  g = GPIO_GROUP(priv->irq_pin);
  p = GPIO_GROUP_PIN(priv->irq_pin);

  /* Configure GT911 INT pin as falling-edge interrupt input. */
  hal_gpio_direction_input(g, p);
  hal_gpio_set_bias_pull(g, p, PIN_PULL_UP);
  hal_gpio_set_irq_mode(g, p, PIN_IRQ_MODE_EDGE_FALLING);
  hal_gpio_clr_irq_stat(g, p);

  {
    unsigned int pending;
    unsigned int level;
    unsigned int en;

    hal_gpio_get_irq_stat(g, p, &pending);
    hal_gpio_get_value(g, p, &level);
    hal_gpio_group_get_irq_en(g, &en);
    syslog(LOG_ERR,
           "GT911 IRQ init-before-en: pin=%u.%u irq=%d pending=%u level=%u "
           "enmask=0x%08x\n",
           g, p, priv->irq_num, pending, level, en);
  }

  hal_gpio_enable_irq(g, p);

  {
    unsigned int pending;
    unsigned int level;
    unsigned int en;

    hal_gpio_get_irq_stat(g, p, &pending);
    hal_gpio_get_value(g, p, &level);
    hal_gpio_group_get_irq_en(g, &en);
    syslog(LOG_ERR,
           "GT911 IRQ init-after-en: pin=%u.%u irq=%d pending=%u level=%u "
           "enmask=0x%08x\n",
           g, p, priv->irq_num, pending, level, en);
  }

  syslog(LOG_ERR, "GT911 Interrupt Enabled (IRQ %d)\n", priv->irq_num);
  /* Attach and Enable System IRQ */
  ret = irq_attach(priv->irq_num, gt911_isr, priv);
  if (ret == OK) {
    up_enable_irq(priv->irq_num);
    ierr("GT911 Interrupt Enabled (IRQ %d)\n", priv->irq_num);

    /* If INT is already low at boot, run worker once to clear status. */
    {
      unsigned int level;

      hal_gpio_get_value(g, p, &level);
      if (level == 0) {
        syslog(LOG_ERR,
               "GT911 IRQ line is low at init, queue worker to clear status\n");
        up_disable_irq(priv->irq_num);
        work_queue(HPWORK, &priv->work, gt911_worker, priv, 0);
      }
    }
  } else {
    ierr("irq_attach failed: %d\n", ret);
  }
#else
  /* Polling mode does not use IRQ path; force it disabled as a safety net. */
  up_disable_irq(priv->irq_num);

  /* Start polling mode work queue. */
  ret = work_queue(HPWORK, &priv->work, gt911_worker, priv,
                   MSEC2TICK(CONFIG_AIC_TOUCH_GT911_POLL_INTERVAL_MS));
  if (ret < 0) {
    ierr("GT911 polling work_queue failed: %d\n", ret);
    return ret;
  }

  ierr("GT911 polling mode enabled, interval=%dms\n",
       CONFIG_AIC_TOUCH_GT911_POLL_INTERVAL_MS);
#endif

  ierr("GT911 Driver Initialization Complete\n");
  return ret;
}

#else

int gt911_driver_init(FAR struct i2c_master_s *i2c_dev) { return -ENODEV; }

#endif /* CONFIG_AIC_TOUCH_PANEL_GT911 */
