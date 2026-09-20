#ifndef UI_DEMO_H
#define UI_DEMO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct ui_demo_wireless_device
{
  const char *name;
  const char *status;
  int32_t value;
};

void ui_demo_init(void);
void ui_demo_deinit(void);

void ui_demo_update_time(const char *time_text);
void ui_demo_update_environment(int32_t temperature_c,
                                int32_t humidity_percent);
void ui_demo_update_wifi_devices(
  const struct ui_demo_wireless_device *devices,
  size_t count);
void ui_demo_set_brightness(int32_t percent);
void ui_demo_set_volume(int32_t percent);

#ifdef __cplusplus
}
#endif

#endif /* UI_DEMO_H */
