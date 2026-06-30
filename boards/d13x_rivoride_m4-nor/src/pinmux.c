/*
 * Copyright (c) 2022-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: weilin.peng@artinchip.com
 */

#include <aic_core.h>
#include <aic_hal.h>
#include "board.h"
#include <aic_utils.h>

struct aic_pinmux aic_pinmux_config[] = {
#ifdef CONFIG_AIC_USING_UART0
    /* uart0 */
    {5, PIN_PULL_DIS, 3, "PA.0"},
    {5, PIN_PULL_UP, 3, "PA.1"},
#endif

#ifdef CONFIG_AIC_USING_UART5
    /* uart5 */
    {5, PIN_PULL_DIS, 3, "PE.4"},
    {5, PIN_PULL_UP, 3, "PE.5"},
#endif
    
#ifdef CONFIG_AIC_USING_CAN0
    /* can0 */
    {4, PIN_PULL_DIS, 3, "PA.4"},
    {4, PIN_PULL_DIS, 3, "PA.5"},
#endif

#ifdef CONFIG_AIC_USING_GPAI2
    {2, PIN_PULL_DIS, 3, "PA.2"},
#endif
#ifdef CONFIG_AIC_USING_GPAI3
    {2, PIN_PULL_DIS, 3, "PA.3"},
#endif


#ifdef CONFIG_AIC_USING_RTP
    {2, PIN_PULL_DIS, 3, "PA.8"},
    {2, PIN_PULL_DIS, 3, "PA.9"},
    {2, PIN_PULL_DIS, 3, "PA.10"},
    {2, PIN_PULL_DIS, 3, "PA.11"},
#endif
#ifdef CONFIG_AIC_USING_I2C0
    {4, PIN_PULL_DIS, 3, "PA.8"}, // SCK
    {4, PIN_PULL_DIS, 3, "PA.9"}, // SDA
#endif
#ifdef CONFIG_AIC_USING_I2C1
    {4, PIN_PULL_DIS, 3, "PA.2"}, // SCK
    {4, PIN_PULL_DIS, 3, "PA.3"}, // SDA
#endif
#ifdef CONFIG_AIC_USING_QSPI0
#ifndef CONFIG_AIC_SYSCFG_SIP_FLASH_ENABLE
    /* qspi0 */
    {2, PIN_PULL_UP, 3, "PB.0"},
    {2, PIN_PULL_UP, 3, "PB.1"},
    {2, PIN_PULL_UP, 3, "PB.2"},
    {2, PIN_PULL_UP, 3, "PB.3"},
    {2, PIN_PULL_UP, 3, "PB.4"},
    {2, PIN_PULL_UP, 3, "PB.5"},
#else
    {8, PIN_PULL_UP, 3, "PB.12"},
    {8, PIN_PULL_UP, 3, "PB.13"},
    {8, PIN_PULL_UP, 3, "PB.14"},
    {8, PIN_PULL_UP, 3, "PB.15"},
    {8, PIN_PULL_UP, 3, "PB.16"},
    {8, PIN_PULL_UP, 3, "PB.17"},
#endif
#endif
#if defined(CONFIG_AIC_USING_QSPI1) &&                                         \
    defined(CONFIG_AIC_SYSCFG_SIP_FLASH_ENABLE)
    /* qspi1 */
    {3, PIN_PULL_DIS, 3, "PB.0"},
    {3, PIN_PULL_DIS, 3, "PB.1"},
    {3, PIN_PULL_DIS, 3, "PB.2"},
    {3, PIN_PULL_DIS, 3, "PB.3"},
    {3, PIN_PULL_DIS, 3, "PB.4"},
    {3, PIN_PULL_DIS, 3, "PB.5"},
#endif
#ifdef CONFIG_AIC_USING_SDMC0
    {2, PIN_PULL_UP, 7, "PB.6"},
    {2, PIN_PULL_UP, 7, "PB.7"},
    {2, PIN_PULL_UP, 7, "PB.8"},
    {2, PIN_PULL_UP, 7, "PB.9"},
    {2, PIN_PULL_UP, 7, "PB.10"},
    {2, PIN_PULL_UP, 7, "PB.11"},
#endif
#ifdef CONFIG_AIC_WIRELESS_LAN
    {1, PIN_PULL_DIS, 3, "PD.1"}, // WIFI_PWR_ON
#endif
#ifdef CONFIG_AIC_USING_SDMC1
    {2, PIN_PULL_UP, 3, "PC.0"},
    {2, PIN_PULL_UP, 3, "PC.1"},
    {2, PIN_PULL_UP, 3, "PC.2"},
    {2, PIN_PULL_UP, 3, "PC.3"},
    {2, PIN_PULL_UP, 3, "PC.4"},
    {2, PIN_PULL_UP, 3, "PC.5"},
    {2, PIN_PULL_UP, 3, "PC.6"},
#endif
#ifdef CONFIG_AIC_PANEL_ENABLE_GPIO
    {1, PIN_PULL_DIS, 3, CONFIG_AIC_PANEL_ENABLE_GPIO},
#endif
#ifdef CONFIG_AIC_PRGB_24BIT
    {2, PIN_PULL_DIS, 3, "PD.0"},
    {2, PIN_PULL_DIS, 3, "PD.1"},
    {2, PIN_PULL_DIS, 3, "PD.2"},
    {2, PIN_PULL_DIS, 3, "PD.3"},
    {2, PIN_PULL_DIS, 3, "PD.4"},
    {2, PIN_PULL_DIS, 3, "PD.5"},
    {2, PIN_PULL_DIS, 3, "PD.6"},
    {2, PIN_PULL_DIS, 3, "PD.7"},
    {2, PIN_PULL_DIS, 3, "PD.8"},
    {2, PIN_PULL_DIS, 3, "PD.9"},
    {2, PIN_PULL_DIS, 3, "PD.10"},
    {2, PIN_PULL_DIS, 3, "PD.11"},
    {2, PIN_PULL_DIS, 3, "PD.12"},
    {2, PIN_PULL_DIS, 3, "PD.13"},
    {2, PIN_PULL_DIS, 3, "PD.14"},
    {2, PIN_PULL_DIS, 3, "PD.15"},
    {2, PIN_PULL_DIS, 3, "PD.16"},
    {2, PIN_PULL_DIS, 3, "PD.17"},
    {2, PIN_PULL_DIS, 3, "PD.18"},
    {2, PIN_PULL_DIS, 3, "PD.19"},
    {2, PIN_PULL_DIS, 3, "PD.20"},
    {2, PIN_PULL_DIS, 3, "PD.21"},
    {2, PIN_PULL_DIS, 3, "PD.22"},
    {2, PIN_PULL_DIS, 3, "PD.23"},
    {2, PIN_PULL_DIS, 3, "PD.24"},
    {2, PIN_PULL_DIS, 3, "PD.25"},
    {2, PIN_PULL_DIS, 3, "PD.26"},
    {2, PIN_PULL_DIS, 3, "PD.27"},
#endif
#ifdef CONFIG_AIC_PRGB_16BIT_LD
    {2, PIN_PULL_DIS, 3, "PD.8"},
    {2, PIN_PULL_DIS, 3, "PD.9"},
    {2, PIN_PULL_DIS, 3, "PD.10"},
    {2, PIN_PULL_DIS, 3, "PD.11"},
    {2, PIN_PULL_DIS, 3, "PD.12"},
    {2, PIN_PULL_DIS, 3, "PD.13"},
    {2, PIN_PULL_DIS, 3, "PD.14"},
    {2, PIN_PULL_DIS, 3, "PD.15"},
    {2, PIN_PULL_DIS, 3, "PD.16"},
    {2, PIN_PULL_DIS, 3, "PD.17"},
    {2, PIN_PULL_DIS, 3, "PD.18"},
    {2, PIN_PULL_DIS, 3, "PD.19"},
    {2, PIN_PULL_DIS, 3, "PD.20"},
    {2, PIN_PULL_DIS, 3, "PD.21"},
    {2, PIN_PULL_DIS, 3, "PD.22"},
    {2, PIN_PULL_DIS, 3, "PD.23"},
    {2, PIN_PULL_DIS, 3, "PD.24"},
    {2, PIN_PULL_DIS, 3, "PD.25"},
    {2, PIN_PULL_DIS, 3, "PD.26"},
    {2, PIN_PULL_DIS, 3, "PD.27"},
#endif
#ifdef CONFIG_AIC_PRGB_16BIT_HD
    {2, PIN_PULL_DIS, 3, "PD.0"},
    {2, PIN_PULL_DIS, 3, "PD.1"},
    {2, PIN_PULL_DIS, 3, "PD.2"},
    {2, PIN_PULL_DIS, 3, "PD.3"},
    {2, PIN_PULL_DIS, 3, "PD.4"},
    {2, PIN_PULL_DIS, 3, "PD.5"},
    {2, PIN_PULL_DIS, 3, "PD.6"},
    {2, PIN_PULL_DIS, 3, "PD.7"},
    {2, PIN_PULL_DIS, 3, "PD.8"},
    {2, PIN_PULL_DIS, 3, "PD.9"},
    {2, PIN_PULL_DIS, 3, "PD.10"},
    {2, PIN_PULL_DIS, 3, "PD.11"},
    {2, PIN_PULL_DIS, 3, "PD.12"},
    {2, PIN_PULL_DIS, 3, "PD.13"},
    {2, PIN_PULL_DIS, 3, "PD.14"},
    {2, PIN_PULL_DIS, 3, "PD.15"},
    {2, PIN_PULL_DIS, 3, "PD.24"},
    {2, PIN_PULL_DIS, 3, "PD.25"},
    {2, PIN_PULL_DIS, 3, "PD.26"},
    {2, PIN_PULL_DIS, 3, "PD.27"},
#endif
#ifdef CONFIG_AIC_SRGB_8BIT
    {2, PIN_PULL_DIS, 3, "PD.16"},
    {2, PIN_PULL_DIS, 3, "PD.17"},
    {2, PIN_PULL_DIS, 3, "PD.18"},
    {2, PIN_PULL_DIS, 3, "PD.19"},
    {2, PIN_PULL_DIS, 3, "PD.20"},
    {2, PIN_PULL_DIS, 3, "PD.21"},
    {2, PIN_PULL_DIS, 3, "PD.22"},
    {2, PIN_PULL_DIS, 3, "PD.23"},
    {2, PIN_PULL_DIS, 3, "PD.24"},
    {2, PIN_PULL_DIS, 3, "PD.25"},
    {2, PIN_PULL_DIS, 3, "PD.26"},
    {2, PIN_PULL_DIS, 3, "PD.27"},
#endif
#ifdef CONFIG_AIC_SRGB_6BIT
    {2, PIN_PULL_DIS, 3, "PD.16"},
    {2, PIN_PULL_DIS, 3, "PD.17"},
    {2, PIN_PULL_DIS, 3, "PD.18"},
    {2, PIN_PULL_DIS, 3, "PD.19"},
    {2, PIN_PULL_DIS, 3, "PD.20"},
    {2, PIN_PULL_DIS, 3, "PD.21"},
    {2, PIN_PULL_DIS, 3, "PD.24"},
    {2, PIN_PULL_DIS, 3, "PD.25"},
    {2, PIN_PULL_DIS, 3, "PD.26"},
    {2, PIN_PULL_DIS, 3, "PD.27"},
#endif
#ifdef CONFIG_AIC_PANEL_DBI_ILI9488
    /* i8080 16 bits */
    {2, PIN_PULL_DIS, 3, "PD.8"},
    {2, PIN_PULL_DIS, 3, "PD.9"},
    {2, PIN_PULL_DIS, 3, "PD.10"},
    {2, PIN_PULL_DIS, 3, "PD.11"},
    {2, PIN_PULL_DIS, 3, "PD.12"},
    {2, PIN_PULL_DIS, 3, "PD.13"},
    {2, PIN_PULL_DIS, 3, "PD.14"},
    {2, PIN_PULL_DIS, 3, "PD.15"},
    {2, PIN_PULL_DIS, 3, "PD.16"},
    {2, PIN_PULL_DIS, 3, "PD.17"},
    {2, PIN_PULL_DIS, 3, "PD.18"},
    {2, PIN_PULL_DIS, 3, "PD.19"},
    {2, PIN_PULL_DIS, 3, "PD.20"},
    {2, PIN_PULL_DIS, 3, "PD.21"},
    {2, PIN_PULL_DIS, 3, "PD.22"},
    {2, PIN_PULL_DIS, 3, "PD.23"},
    {2, PIN_PULL_DIS, 3, "PD.24"},
    {2, PIN_PULL_DIS, 3, "PD.25"},
    {2, PIN_PULL_DIS, 3, "PD.26"},
    {2, PIN_PULL_DIS, 3, "PD.27"},
#endif
#ifdef CONFIG_AIC_PANEL_SRGB_HX8238
    {2, PIN_PULL_DIS, 3, "PD.16"},
    {2, PIN_PULL_DIS, 3, "PD.17"},
    {2, PIN_PULL_DIS, 3, "PD.18"},
    {2, PIN_PULL_DIS, 3, "PD.19"},
    {2, PIN_PULL_DIS, 3, "PD.20"},
    {2, PIN_PULL_DIS, 3, "PD.21"},
    {2, PIN_PULL_DIS, 3, "PD.22"},
    {2, PIN_PULL_DIS, 3, "PD.23"},
    {2, PIN_PULL_DIS, 3, "PD.24"},
    {2, PIN_PULL_DIS, 3, "PD.25"},
    {2, PIN_PULL_DIS, 3, "PD.26"},
    {2, PIN_PULL_DIS, 3, "PD.27"},
    {1, PIN_PULL_DIS, 3, "PD.0"},
    {1, PIN_PULL_DIS, 3, "PD.4"},
    {1, PIN_PULL_DIS, 3, "PD.5"},
    {1, PIN_PULL_DIS, 3, "PE.18"},
#endif
#ifdef CONFIG_AIC_PANEL_LCOS_HX7033
    {2, PIN_PULL_DIS, 3, "PD.0"},
    {2, PIN_PULL_DIS, 3, "PD.1"},
    {2, PIN_PULL_DIS, 3, "PD.2"},
    {2, PIN_PULL_DIS, 3, "PD.3"},
    {2, PIN_PULL_DIS, 3, "PD.4"},
    {2, PIN_PULL_DIS, 3, "PD.5"},
    {2, PIN_PULL_DIS, 3, "PD.8"},
    {2, PIN_PULL_DIS, 3, "PD.9"},
    {2, PIN_PULL_DIS, 3, "PD.10"},
    {2, PIN_PULL_DIS, 3, "PD.11"},
    {2, PIN_PULL_DIS, 3, "PD.12"},
    {2, PIN_PULL_DIS, 3, "PD.13"},
    {2, PIN_PULL_DIS, 3, "PD.16"},
    {2, PIN_PULL_DIS, 3, "PD.17"},
    {2, PIN_PULL_DIS, 3, "PD.18"},
    {2, PIN_PULL_DIS, 3, "PD.19"},
    {2, PIN_PULL_DIS, 3, "PD.20"},
    {2, PIN_PULL_DIS, 3, "PD.21"},
    {2, PIN_PULL_DIS, 3, "PD.24"},
    {2, PIN_PULL_DIS, 3, "PD.27"},
    {1, PIN_PULL_DIS, 3, "PA.4"},
    {1, PIN_PULL_DIS, 3, "PA.5"},
#endif
#ifdef CONFIG_AIC_PANEL_DBI_ILI9486L
    {2, PIN_PULL_DIS, 3, "PD.16"},
    {2, PIN_PULL_DIS, 3, "PD.17"},
    {2, PIN_PULL_DIS, 3, "PD.18"},
    {2, PIN_PULL_DIS, 3, "PD.19"},
    {2, PIN_PULL_DIS, 3, "PD.20"},
    {2, PIN_PULL_DIS, 3, "PD.21"},
    {2, PIN_PULL_DIS, 3, "PD.22"},
    {2, PIN_PULL_DIS, 3, "PD.23"},
    {2, PIN_PULL_DIS, 3, "PD.24"},
    {2, PIN_PULL_DIS, 3, "PD.25"},
    {2, PIN_PULL_DIS, 3, "PD.26"},
    {2, PIN_PULL_DIS, 3, "PD.27"},
#endif
#ifdef CONFIG_AIC_PANEL_DBI_ILI9341
    /* spi 4 line */
    {2, PIN_PULL_DIS, 3, "PD.20"},
    {2, PIN_PULL_DIS, 3, "PD.21"},
    {2, PIN_PULL_DIS, 3, "PD.24"},
    {2, PIN_PULL_DIS, 3, "PD.26"},
    {2, PIN_PULL_DIS, 3, "PD.27"},
    {1, PIN_PULL_DIS, 3, "PD.23"},
#endif
#ifdef CONFIG_AIC_PANEL_DBI_ILI9327
    /* spi 4 line */
    {2, PIN_PULL_DIS, 3, "PD.20"},
    {2, PIN_PULL_DIS, 3, "PD.21"},
    {2, PIN_PULL_DIS, 3, "PD.24"},
    {2, PIN_PULL_DIS, 3, "PD.26"},
    {2, PIN_PULL_DIS, 3, "PD.27"},
    {1, PIN_PULL_DIS, 3, "PD.23"},
#endif
#ifdef CONFIG_AIC_PANEL_DBI_ST77903
    /* spi 4 sda */
    {2, PIN_PULL_DIS, 3, "PD.20"},
    {2, PIN_PULL_DIS, 3, "PD.21"},
    {2, PIN_PULL_DIS, 3, "PD.22"},
    {2, PIN_PULL_DIS, 3, "PD.23"},
    {2, PIN_PULL_DIS, 3, "PD.24"},
    {2, PIN_PULL_DIS, 3, "PD.26"},
    {2, PIN_PULL_DIS, 3, "PD.27"},
#endif

#ifdef CONFIG_AIC_USING_PWM0
    {4, PIN_PULL_DIS, 3, "PE.0"},
    {4, PIN_PULL_DIS, 3, "PE.1"},
#endif
#ifdef CONFIG_AIC_USING_PWM1
    /* FIX: PE.11(3)=I2S0_DIN, PE.12(3)=SPI3_CLK - NOT PWM.
     * Use PC.1(7)=PWM1 for correct PWM output. */
    // {3, PIN_PULL_DIS, 7, "PC.1"},
    {3, PIN_PULL_DOWN, 0, "PE.12"},
#endif
#ifdef CONFIG_AIC_USING_PWM2
    {3, PIN_PULL_DOWN, 0, "PE.13"},
#endif
#ifdef CONFIG_AIC_USING_PWM3
    /* FIX: PE.16(3)=SPI0_CLK, PE.17(3)=SPI0_CS - NOT PWM.
     * Use PE.3(8)=PWM3 for correct PWM output. */
    {3, PIN_PULL_DIS, 8, "PE.3"},
#endif

    
#ifdef CONFIG_AIC_USING_AUDIO
#ifdef CONFIG_AIC_AUDIO_PLAYBACK
    // {5, PIN_PULL_DIS, 3, "PE.12"},
    {1, PIN_PULL_DIS, 3, CONFIG_AIC_AUDIO_PA_ENABLE_GPIO},
#endif
#endif
#ifdef CONFIG_AIC_USING_CTP
    {1, PIN_PULL_DIS, 3, CONFIG_AIC_TOUCH_PANEL_RST_PIN},
#ifdef CONFIG_AIC_TOUCH_PANEL_WAKE_UP
    {1, PIN_PULL_DIS, 3, CONFIG_AIC_TOUCH_PANEL_INT_PIN, FLAG_WAKEUP_SOURCE},
#else
    {1, PIN_PULL_DIS, 3, CONFIG_AIC_TOUCH_PANEL_INT_PIN},
#endif
#endif
#ifdef CONFIG_AIC_USING_PM
    {1, PIN_PULL_DIS, 3, CONFIG_AIC_BOARD_LEVEL_POWER_PIN, FLAG_POWER_PIN},
#ifdef CONFIG_AIC_PM_DEMO
    {1, PIN_PULL_UP, 3, CONFIG_AIC_PM_POWER_KEY_GPIO, FLAG_WAKEUP_SOURCE},
#endif
#endif
};

uint32_t aic_pinmux_config_size = ARRAY_SIZE(aic_pinmux_config);

/*
 * pinmux.c in target directories usually only contains configuration data.
 * We implement the initialization logic here, which iterates over the
 * configuration array linked from the selected target.
 */
void aic_board_pinmux_init(void) {
  uint32_t i = 0;
  int pin = 0;
  unsigned int g;
  unsigned int p;

  for (i = 0; i < aic_pinmux_config_size; i++) {
    pin = hal_gpio_name2pin(aic_pinmux_config[i].name);
    if (pin < 0)
      continue;

    g = GPIO_GROUP(pin);
    p = GPIO_GROUP_PIN(pin);

    hal_gpio_set_func(g, p, aic_pinmux_config[i].func);
    hal_gpio_set_bias_pull(g, p, aic_pinmux_config[i].bias);
    hal_gpio_set_drive_strength(g, p, aic_pinmux_config[i].drive);
  }
}