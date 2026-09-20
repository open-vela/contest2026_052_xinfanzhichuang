#ifndef LVGL_APP_FBDEV_COMPAT_H
#define LVGL_APP_FBDEV_COMPAT_H

#include <stdbool.h>

#include <lvgl/lvgl.h>

bool lvgl_app_fbdev_compat_matches(const char *path);
lv_display_t *lvgl_app_fbdev_compat_create(const char *path);

#endif /* LVGL_APP_FBDEV_COMPAT_H */
