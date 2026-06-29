/*
 * Copyright (c) 2022, Artinchip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __AIC_DRV_GPIO_H__
#define __AIC_DRV_GPIO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "aic_hal_gpio.h"

/* GPIO中断回调函数类型 */
typedef void (*gpio_irq_handler_t)(void *arg);

/* GPIO中断配置结构 */
struct gpio_irq_config_s {
  uint32_t pin;               /* GPIO引脚号 */
  uint32_t intr_type;         /* 中断触发类型 */
  gpio_irq_handler_t handler; /* 中断处理函数 */
  void *arg;                  /* 传递给处理函数的参数 */
  bool enabled;               /* 是否启用 */
};

void drv_pin_bias_set(unsigned int pin, unsigned int pull);
void drv_pin_drive_set(unsigned int pin, unsigned int strength);
void drv_pin_mux_set(unsigned int pin, unsigned int func);
unsigned int drv_pin_mux_get(unsigned int pin);
long drv_pin_get(const char *name);
int drv_pin_init(void);

int gpio_irq_initialize(void);
int gpio_irq_detach(uint32_t irqn);
int gpio_irq_attach(uint32_t irqn, gpio_irq_handler_t handler, void *arg);
int gpio_irq_enable(uint32_t irqn);
int gpio_irq_disable(uint32_t irqn);
int gpio_irq_arch_configure(uint32_t gpio_pin, uint32_t intr_type);
int gpio_get_value(uint32_t pin);
int gpio_set_value(uint32_t pin, bool value);
#ifdef __cplusplus
}
#endif

#endif /* __AIC_DRV_GPIO_H__ */
