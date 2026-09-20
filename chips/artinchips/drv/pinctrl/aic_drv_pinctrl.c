/*
 * Copyright (c) 2024, ArtInChip Technology Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/pinctrl/pinctrl.h>
#include <nuttx/kmalloc.h>

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <debug.h>
#include <errno.h>

#include "aic_core.h"
#include "aic_hal_gpio.h"

#if defined(CONFIG_PINCTRL)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct aic_pinctrl_dev_s {
  struct pinctrl_dev_s pinctrl;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int aic_pinctrl_set_function(FAR struct pinctrl_dev_s *dev, uint32_t pin,
                                    uint32_t function);
static int aic_pinctrl_set_strength(FAR struct pinctrl_dev_s *dev, uint32_t pin,
                                    uint32_t strength);
static int aic_pinctrl_set_driver(FAR struct pinctrl_dev_s *dev, uint32_t pin,
                                  enum pinctrl_drivertype_e type);
static int aic_pinctrl_set_slewrate(FAR struct pinctrl_dev_s *dev, uint32_t pin,
                                    uint32_t slewrate);
static int aic_pinctrl_select_gpio(FAR struct pinctrl_dev_s *dev, uint32_t pin);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct pinctrl_ops_s g_aic_pinctrl_ops = {
    .set_function = aic_pinctrl_set_function,
    .set_strength = aic_pinctrl_set_strength,
    .set_driver = aic_pinctrl_set_driver,
    .set_slewrate = aic_pinctrl_set_slewrate,
    .select_gpio = aic_pinctrl_select_gpio,
};

static struct aic_pinctrl_dev_s g_aic_pinctrl_dev;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int aic_pinctrl_set_function(FAR struct pinctrl_dev_s *dev, uint32_t pin,
                                    uint32_t function) {
  unsigned int group = GPIO_GROUP(pin);
  unsigned int p = GPIO_GROUP_PIN(pin);

  hal_gpio_set_func(group, p, function);
  return OK;
}

static int aic_pinctrl_set_strength(FAR struct pinctrl_dev_s *dev, uint32_t pin,
                                    uint32_t strength) {
  unsigned int group = GPIO_GROUP(pin);
  unsigned int p = GPIO_GROUP_PIN(pin);

  hal_gpio_set_drive_strength(group, p, strength);
  return OK;
}

static int aic_pinctrl_set_driver(FAR struct pinctrl_dev_s *dev, uint32_t pin,
                                  enum pinctrl_drivertype_e type) {
  unsigned int group = GPIO_GROUP(pin);
  unsigned int p = GPIO_GROUP_PIN(pin);
  uint32_t pull;

  switch (type) {
  case BIAS_DISABLE:
    pull = PIN_PULL_DIS;
    break;
  case BIAS_PULLUP:
    pull = PIN_PULL_UP;
    break;
  case BIAS_PULLDOWN:
    pull = PIN_PULL_DOWN;
    break;
  default:
    return -EINVAL;
  }

  hal_gpio_set_bias_pull(group, p, pull);
  return OK;
}

static int aic_pinctrl_set_slewrate(FAR struct pinctrl_dev_s *dev, uint32_t pin,
                                    uint32_t slewrate) {
  /* Not supported by hardware or HAL */
  return -ENOSYS;
}

static int aic_pinctrl_select_gpio(FAR struct pinctrl_dev_s *dev,
                                   uint32_t pin) {
  unsigned int group = GPIO_GROUP(pin);
  unsigned int p = GPIO_GROUP_PIN(pin);

  /* Function 1 is GPIO */
  hal_gpio_set_func(group, p, 1);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int aic_pinctrl_initialize(void) {
  int ret;

  g_aic_pinctrl_dev.pinctrl.ops = &g_aic_pinctrl_ops;

  ret = pinctrl_register(&g_aic_pinctrl_dev.pinctrl, 0);
  if (ret < 0) {
    pinctrlerr("ERROR: pinctrl_register failed: %d\n", ret);
    return ret;
  }

  return OK;
}

FAR struct pinctrl_dev_s *aic_get_pinctrl_dev(void) {
  return &g_aic_pinctrl_dev.pinctrl;
}

#endif /* CONFIG_PINCTRL */
