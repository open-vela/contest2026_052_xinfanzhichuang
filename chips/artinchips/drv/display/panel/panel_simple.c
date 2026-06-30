/*
 * Copyright (c) 2023-2025, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "panel_com.h"
#include <aic_hal.h>

#ifdef CONFIG_AIC_SIMPLE_PANEL

#ifdef CONFIG_LVDS_PANEL_MATCH_ID
#define LVDS_PANEL_MATCH_ID CONFIG_LVDS_PANEL_MATCH_ID
#else
#define LVDS_PANEL_MATCH_ID 0
#endif

#ifdef CONFIG_RGB_PANEL_MATCH_ID
#define RGB_PANEL_MATCH_ID CONFIG_RGB_PANEL_MATCH_ID
#else
#define RGB_PANEL_MATCH_ID 0
#endif

#ifdef CONFIG_PANEL_PIXELCLOCK
#define PANEL_PIXELCLOCK CONFIG_PANEL_PIXELCLOCK
#else
#define PANEL_PIXELCLOCK 10
#endif

#ifdef CONFIG_PANEL_HACTIVE
#define PANEL_HACTIVE CONFIG_PANEL_HACTIVE
#else
#define PANEL_HACTIVE 480
#endif

#ifdef CONFIG_PANEL_VACTIVE
#define PANEL_VACTIVE CONFIG_PANEL_VACTIVE
#else
#define PANEL_VACTIVE 272
#endif

#ifdef CONFIG_PANEL_HBP
#define PANEL_HBP CONFIG_PANEL_HBP
#else
#define PANEL_HBP 43
#endif

#ifdef CONFIG_PANEL_HFP
#define PANEL_HFP CONFIG_PANEL_HFP
#else
#define PANEL_HFP 8
#endif

#ifdef CONFIG_PANEL_HSW
#define PANEL_HSW CONFIG_PANEL_HSW
#else
#define PANEL_HSW 1
#endif

#ifdef CONFIG_PANEL_VBP
#define PANEL_VBP CONFIG_PANEL_VBP
#else
#define PANEL_VBP 12
#endif

#ifdef CONFIG_PANEL_VFP
#define PANEL_VFP CONFIG_PANEL_VFP
#else
#define PANEL_VFP 4
#endif

#ifdef CONFIG_PANEL_VSW
#define PANEL_VSW CONFIG_PANEL_VSW
#else
#define PANEL_VSW 10
#endif

static int simple_panel_prepare(void) { return 0; }

static int simple_panel_unprepare(void) { return 0; }

static int simple_panel_enable(struct aic_panel *panel) {
  /* open lcd backlight */
  panel_backlight_enable(panel, 0);
  return 0;
}

static int simple_panel_disable(struct aic_panel *panel) {
  /* close lcd backlight */
  panel_backlight_disable(panel, 0);
  return 0;
}

static int simple_panel_register_callback(struct aic_panel *panel,
                                          struct aic_panel_callbacks *cb) {
  return 0;
}

static struct aic_panel_funcs simple_panel_funcs = {
    .prepare = simple_panel_prepare,
    .unprepare = simple_panel_unprepare,
    .enable = simple_panel_enable,
    .disable = simple_panel_disable,
    .register_callback = simple_panel_register_callback,
};

static struct display_timing simple_rgb_timing = {
    .pixelclock = PANEL_PIXELCLOCK * 1000000,
    .hactive = PANEL_HACTIVE,
    .hback_porch = PANEL_HBP,
    .hfront_porch = PANEL_HFP,
    .hsync_len = PANEL_HSW,
    .vactive = PANEL_VACTIVE,
    .vback_porch = PANEL_VBP,
    .vfront_porch = PANEL_VFP,
    .vsync_len = PANEL_VSW,
};

static struct display_timing simple_lvds_timing = {
    .pixelclock = PANEL_PIXELCLOCK * 1000000,
    .hactive = PANEL_HACTIVE,
    .hback_porch = PANEL_HBP,
    .hfront_porch = PANEL_HFP,
    .hsync_len = PANEL_HSW,
    .vactive = PANEL_VACTIVE,
    .vback_porch = PANEL_VBP,
    .vfront_porch = PANEL_VFP,
    .vsync_len = PANEL_VSW,
};

static struct panel_rgb simple_rgb_config = {
    .mode = PRGB,
#ifdef CONFIG_AIC_PRGB_24BIT
    .format = PRGB_24BIT,
#elif defined(CONFIG_AIC_PRGB_16BIT_LD)
    .format = PRGB_16BIT_LD,
#elif defined(CONFIG_AIC_PRGB_16BIT_HD)
    .format = PRGB_16BIT_HD,
#else
    .format = PRGB_24BIT,
#endif
#ifdef CONFIG_DATA_ORDER_RGB
    .data_order = RGB,
#elif defined(CONFIG_DATA_ORDER_BGR)
    .data_order = BGR,
#elif defined(CONFIG_DATA_ORDER_RBG)
    .data_order = RBG,
#elif defined(CONFIG_DATA_ORDER_BRG)
    .data_order = BRG,
#elif defined(CONFIG_DATA_ORDER_GRB)
    .data_order = GRB,
#elif defined(CONFIG_DATA_ORDER_GBR)
    .data_order = GBR,
#else
    .data_order = RGB,
#endif
    .data_mirror = 1,
    .clock_phase = 0x1,
};

/* RGB Panel for Vela */
struct aic_panel simple_rgb_panel = {
    .name = "simple_rgb",
    .connector_type = AIC_RGB_COM,
    .timings = &simple_rgb_timing,
    .rgb = &simple_rgb_config,
    .funcs = &simple_panel_funcs,
};

/* LVDS Panel for Vela */
struct aic_panel simple_lvds_panel = {
    .name = "simple_lvds",
    .connector_type = AIC_LVDS_COM,
    .timings = &simple_lvds_timing,
    .funcs = &simple_panel_funcs,
};

#endif /* CONFIG_AIC_SIMPLE_PANEL */
