#ifndef LVGL_APP_BACKLIGHT_H
#define LVGL_APP_BACKLIGHT_H

#include <stdint.h>

int lvgl_app_backlight_set_percent(int32_t percent);
void lvgl_app_backlight_shutdown(void);

#endif /* LVGL_APP_BACKLIGHT_H */
