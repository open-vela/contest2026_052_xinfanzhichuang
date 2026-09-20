/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/config.h>

#include <stdbool.h>
#include <string.h>

#include <aic_hal_gpio.h>
#include <aic_time.h>

#include "board_exbt.h"

#ifdef CONFIG_AIC_EXBT

#define AIC_EXBT_GPIO_FUNC 1
#define AIC_EXBT_PIN_DRV 3

enum aic_board_exbt_mode {
  AIC_BOARD_EXBT_MODE_UNKNOWN = 0,
  AIC_BOARD_EXBT_MODE_DEBUG_UART0,
  AIC_BOARD_EXBT_MODE_UART0_BT,
};

static enum aic_board_exbt_mode g_exbt_mode =
    AIC_BOARD_EXBT_MODE_UNKNOWN;
static bool g_exbt_detecting;

static int aic_board_exbt_pin_input(const char *pin_name,
                                    unsigned int *value)
{
  int pin;
  unsigned int group;
  unsigned int group_pin;

  pin = hal_gpio_name2pin(pin_name);
  if (pin < 0)
    {
      return pin;
    }

  group = GPIO_GROUP(pin);
  group_pin = GPIO_GROUP_PIN(pin);

  hal_gpio_set_func(group, group_pin, AIC_EXBT_GPIO_FUNC);
  hal_gpio_set_bias_pull(group, group_pin, PIN_PULL_UP);
  hal_gpio_set_drive_strength(group, group_pin, AIC_EXBT_PIN_DRV);
  hal_gpio_direction_input(group, group_pin);
  aic_udelay(50);

  return hal_gpio_get_value(group, group_pin, value);
}

static void aic_board_exbt_hold_i2c0_gpio_input(void)
{
  unsigned int value;

  (void)aic_board_exbt_pin_input(CONFIG_AIC_EXBT_I2C0_SCL_PIN, &value);
  (void)aic_board_exbt_pin_input(CONFIG_AIC_EXBT_I2C0_SDA_PIN, &value);
}

void aic_board_exbt_detect(void)
{
  unsigned int value = 1;

  if (g_exbt_mode != AIC_BOARD_EXBT_MODE_UNKNOWN || g_exbt_detecting)
    {
      return;
    }

  g_exbt_detecting = true;

  if (aic_board_exbt_pin_input(CONFIG_AIC_EXBT_DETECT_PIN, &value) < 0)
    {
      g_exbt_mode = AIC_BOARD_EXBT_MODE_DEBUG_UART0;
    }
  else if (value == 0)
    {
      g_exbt_mode = AIC_BOARD_EXBT_MODE_UART0_BT;
      aic_board_exbt_hold_i2c0_gpio_input();
    }
  else
    {
      g_exbt_mode = AIC_BOARD_EXBT_MODE_DEBUG_UART0;
    }

  g_exbt_detecting = false;
}

bool aic_board_exbt_uart0_selected(void)
{
  aic_board_exbt_detect();
  return g_exbt_mode == AIC_BOARD_EXBT_MODE_UART0_BT;
}

bool aic_board_exbt_i2c0_available(void)
{
  aic_board_exbt_detect();
  return g_exbt_mode != AIC_BOARD_EXBT_MODE_UART0_BT;
}

bool aic_board_exbt_pinmux_skip(const char *pin_name)
{
  if (pin_name == NULL || aic_board_exbt_i2c0_available())
    {
      return false;
    }

  return strcmp(pin_name, CONFIG_AIC_EXBT_I2C0_SCL_PIN) == 0 ||
         strcmp(pin_name, CONFIG_AIC_EXBT_I2C0_SDA_PIN) == 0;
}

const char *aic_board_exbt_mode_name(void)
{
  aic_board_exbt_detect();

  if (g_exbt_mode == AIC_BOARD_EXBT_MODE_UART0_BT)
    {
      return "external-bt-uart0";
    }

  return "debug-uart0";
}

#endif
