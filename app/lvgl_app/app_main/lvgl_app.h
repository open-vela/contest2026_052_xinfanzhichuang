#ifndef LVGL_APP_H
#define LVGL_APP_H

#include <stdint.h>

int lvgl_app_init(void);
void lvgl_app_shutdown(void);
uint32_t lvgl_app_run_once(void);

#endif /* LVGL_APP_H */
