#include "ui_demo/resource/ui_demo_background.h"

#include <stddef.h>
#include <stdint.h>

extern const uint8_t ui_demo_background_rgb565_data[];

const lv_image_dsc_t ui_demo_background =
{
  .header =
  {
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_RGB565,
    .flags = 0,
    .w = 1024,
    .h = 600,
    .stride = 1024 * 2,
    .reserved_2 = 0,
  },
  .data_size = 1024 * 600 * 2,
  .data = ui_demo_background_rgb565_data,
  .reserved = NULL,
  .reserved_2 = NULL,
};
