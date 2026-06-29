/*
 * Copyright (c) 2024, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/arch.h>
#include <nuttx/config.h>
#include <nuttx/input/buttons.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/wqueue.h>

#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "aic_core.h"
#include "aic_drv_irq.h"
#include "aic_hal_gpio.h"
#include "aic_drv_gpio.h"
/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define AIC_GPIO_BUTTON_DEBOUNCE_DELAY_MS 20
#define AIC_GPIO_BUTTON_NAME_PREFIX "gpio_button"

int g_open_irq = 0;
static btn_handler_t g_btnhandler;
static FAR void *g_btnarg;

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct aic_gpio_button_s {
  struct btn_lowerhalf_s lower; /* Button lower half driver */
  uint32_t pin;                 /* GPIO pin number */
  uint32_t id;                  /* Button ID */
  bool pressed_state;           /* True if pressed state is low */
  bool last_state;              /* Last button state for debouncing */
  bool current_state;           /* Current button state */
  struct work_s work;           /* Work queue for debouncing */
  bool work_scheduled;          /* Work queue scheduled flag */
};

struct aic_gpio_button_s *g_priv = NULL;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static gpio_irq_handler_t aic_gpio_button_irq(FAR void *arg);
static void aic_gpio_button_work(FAR void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static btn_buttonset_t
aic_gpio_button_supported(FAR const struct btn_lowerhalf_s *lower) {
  return ~0u;
}

static btn_buttonset_t
aic_gpio_button_buttons(FAR const struct btn_lowerhalf_s *lower) {
  FAR const struct aic_gpio_button_s *priv =
      (FAR const struct aic_gpio_button_s *)lower;
  btn_buttonset_t buttons = 0;

  /* Read all button states */

  unsigned int g = GPIO_GROUP(priv->pin);
  unsigned int p = GPIO_GROUP_PIN(priv->pin);
  unsigned int val = 0;
  bool state;

  /* Read current GPIO state */
  hal_gpio_get_value(g, p, &val);
  state = (val != 0) ^ priv->pressed_state;

  /* Set button state in bitmask */
  if (state) {
    buttons |= (1 << priv->id);
  }

  return buttons;
}

static void aic_gpio_button_enable(FAR const struct btn_lowerhalf_s *lower,
                                   btn_buttonset_t press,
                                   btn_buttonset_t release,
                                   btn_handler_t handler, FAR void *arg) {
  FAR const struct aic_gpio_button_s *priv =
      (FAR const struct aic_gpio_button_s *)lower;

  /* Attach interrupt handler if not already attached */
  if (handler != NULL && !g_open_irq) {
    g_btnhandler = handler;
    g_btnarg = arg;
    gpioinfo("INFO[%d]: aic_gpio_button_enable pin %lu addr: %p\n", __LINE__,
             priv->pin, priv);

    int ret = gpio_irq_attach(priv->pin, (gpio_irq_handler_t)aic_gpio_button_irq, (FAR void *)priv);
    if (ret < 0) {
      gpioerr("ERROR[%d]: Failed to attach GPIO interrupt for pin %ld: %d\n",
              __LINE__, priv->pin, ret);
    }
    /* Enable system interrupt */
    // up_enable_irq(D12X_IRQ_GPIO);
    gpio_irq_enable(priv->pin);

    g_open_irq = 1;
  }
  if (handler == NULL && g_open_irq) {
    gpioinfo("INFO[%d]: aic_gpio_button_disable pin %ld\n", __LINE__,
             priv->pin);
    /* Disable interrupt */
    gpio_irq_disable(priv->pin);
    gpio_irq_detach(priv->pin);
    g_open_irq = 0;
  }
}

/* 按钮下半部驱动操作结构体 */
static const struct btn_lowerhalf_s g_aic_gpio_button_lower = {
    .bl_supported = aic_gpio_button_supported,
    .bl_buttons = aic_gpio_button_buttons,
    .bl_enable = aic_gpio_button_enable,
    .bl_write = NULL, /* 不需要写入功能 */
};

/****************************************************************************
 * Name: aic_gpio_button_irq
 *
 * Description:
 *   GPIO interrupt handler for button state changes.
 *
 ****************************************************************************/

static gpio_irq_handler_t aic_gpio_button_irq(FAR void *arg) {
  FAR struct aic_gpio_button_s *priv = (FAR struct aic_gpio_button_s *)arg;

  /* Process all buttons to find which one triggered the interrupt */

  int val = 0;
  bool state;
  /* Read current GPIO state */
  val = gpio_get_value(priv->pin);

  state = (val != 0) ^ priv->pressed_state;
  gpioinfo(
      "INFO[%d]: addr %p, pin=%lu val=%d pressed_state=%d` status = %d  \n",
      __LINE__, priv, priv->pin, val, priv->pressed_state, state);
  // /* Debouncing: only process if state changed */
  if (state != priv->last_state) {
    priv->last_state = state;

    /* Schedule work for debouncing */
    if (!priv->work_scheduled) {
      int ret = work_queue(HPWORK, &priv->work, aic_gpio_button_work, priv,
                           MSEC2TICK(AIC_GPIO_BUTTON_DEBOUNCE_DELAY_MS));
      if (ret == OK) {
        priv->work_scheduled = true;
      }
    }
  }
  return OK;
}

/****************************************************************************
 * Name: aic_gpio_button_work
 *
 * Description:
 *   Work queue function for button debouncing and event reporting.
 *
 ****************************************************************************/

static void aic_gpio_button_work(FAR void *arg) {
  FAR struct aic_gpio_button_s *priv = (FAR struct aic_gpio_button_s *)arg;

  /* Process all buttons to find which one needs debouncing */

  if (priv->work_scheduled) {
    unsigned int g = GPIO_GROUP(priv->pin);
    unsigned int p = GPIO_GROUP_PIN(priv->pin);
    unsigned int val = 0;
    bool state;

    /* Read current GPIO state */
    hal_gpio_get_value(g, p, &val);
    state = (val != 0) ^ priv->pressed_state;
    gpioerr("line[%d]ERROR: Failed to schedule work for button %ld: \n", __LINE__,
            priv->id);
    /* Check if state is stable */
    if (state == priv->last_state) {
      /* State is stable, report the event */
      if (state != priv->current_state) {
        priv->current_state = state;

        /* Report button event to upper half driver */
        if (g_btnhandler != NULL) {
          g_btnhandler((struct btn_lowerhalf_s *)priv, g_btnarg);

          gpiowarn("Button %ld (pin %ld) event: %s\n", priv->id, priv->pin,
                   state ? "PRESSED" : "RELEASED");
        } else {
          gpioerr("ERROR: Upper half driver not initialized for button %ld\n",
                  priv->id);
        }
      }
    } else {
      /* State changed during debounce period, ignore this event */
      gpiowarn("Button  state changed during debounce, ignoring event\n");
    }

    priv->work_scheduled = false;
  }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: aic_gpio_button_register
 *
 * Description:
 *   Register a GPIO button driver.
 *
 * Input Parameters:
 *   pin          - GPIO pin number
 *   id           - Button ID
 *   pressed_state - True if pressed state is low (active low)
 *   devname      - Device name (optional, NULL for auto-generated)
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int aic_gpio_button_register(uint32_t pin, uint32_t id, bool pressed_state,
                             const char *devname) {
  FAR struct aic_gpio_button_s *priv;
  unsigned int g = GPIO_GROUP(pin);
  unsigned int p = GPIO_GROUP_PIN(pin);

  /* Allocate and initialize button driver instance */
  priv = (FAR struct aic_gpio_button_s *)kmm_zalloc(sizeof(*priv));
  if (!priv) {
    gpioerr("ERROR: Failed to allocate GPIO button driver\n");
    return -ENOMEM;
  }

  /* Initialize private data */
  memset(priv, 0, sizeof(*priv));
  /* 使用标准的下半部驱动结构体 */
  memcpy(&priv->lower, &g_aic_gpio_button_lower,
         sizeof(struct btn_lowerhalf_s));

  /* Copy pin and id arrays */
  gpioinfo("INFO: Failed to allocate GPIO button driver %d %lu  addr: %p\n",
           __LINE__, pin, priv);
  priv->pin = pin;
  priv->id = id;
  priv->pressed_state = pressed_state;

  priv->last_state = false;
  priv->current_state = false;
  priv->work_scheduled = false;
  /* Configure GPIO as input with appropriate pull */
  if (pressed_state) {
    /* Active low: enable pull-up */
    hal_gpio_set_bias_pull(g, p, PIN_PULL_UP);
    gpiowarn("Configured GPIO pin %ld as active low with pull-up\n", pin);
  } else {
    /* Active high: enable pull-down */
    hal_gpio_set_bias_pull(g, p, PIN_PULL_DOWN);
    gpiowarn("Configured GPIO pin %ld as active high with pull-down\n", pin);
  }

  /* Set GPIO function and direction */
  hal_gpio_set_func(g, p, 1); /* GPIO function */
  hal_gpio_direction_input(g, p);

  /* Configure interrupt for both edges */
  hal_gpio_set_irq_mode(g, p, PIN_IRQ_MODE_EDGE_BOTH);

  /* Generate device name if not provided */
  if (devname == NULL) {
    devname = "/dev/buttons";
  }

  /* Register button driver */
  // priv->upper =
  btn_register(devname, &priv->lower);

  gpiowarn("GPIO button registered successfully: pin=%ld, id=%ld, name=%s\n",
           pin, id, devname);
  g_priv = priv;
  return OK;
}

/****************************************************************************
 * Name: aic_gpio_button_unregister
 *
 * Description:
 *   Unregister a GPIO button driver.
 *
 * Input Parameters:
 *   pin - GPIO pin number
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int aic_gpio_button_unregister(uint32_t pin) {
  /* Note: This is a simplified implementation. In a real system,
   * you would need to maintain a list of registered buttons and
   * find the correct instance to unregister.
   *
   * For now, we just disable the interrupt and clean up the GPIO.
   */
  if (!g_priv) {
    gpioerr("ERROR: No GPIO button driver registered\n");
    return OK;
  }
  unsigned int g = GPIO_GROUP(pin);
  unsigned int p = GPIO_GROUP_PIN(pin);

  /* Disable interrupt */
  g = GPIO_GROUP(g_priv->pin);
  p = GPIO_GROUP_PIN(g_priv->pin);
  hal_gpio_disable_irq(g, p);

  // irq_detach(irq);

  gpiowarn("GPIO button unregistered: pin=%ld\n", pin);

  return OK;
}