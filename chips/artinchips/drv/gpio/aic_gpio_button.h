#ifndef __AIC_GPIO_BUTTON_H
#define __AIC_GPIO_BUTTON_H

#include <stdint.h>
#include <stdbool.h>

int aic_gpio_button_register(uint32_t pin, uint32_t id, bool pressed_state,
                             const char *devname);

int aic_gpio_button_unregister(uint32_t pin);

#endif