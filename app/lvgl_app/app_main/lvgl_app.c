#include "app_main/lvgl_app.h"
#include "app_main/lvgl_app_backlight.h"
#include "app_main/lvgl_app_fbdev_compat.h"
#include "config/lvgl_app_config.h"

#ifdef CONFIG_LVGL_APP_UI_DEMO
#include "ui_demo/inc/ui_demo.h"
#endif

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <lvgl/lvgl.h>

#ifdef CONFIG_LV_USE_NUTTX_TOUCHSCREEN
#  include <lvgl/src/drivers/nuttx/lv_nuttx_touchscreen.h>
#endif

#define LVGL_APP_TAG "app"
#define LVGL_APP_INPUT_RETRY_INTERVAL_MS 200u

static lv_nuttx_result_t g_lvgl_result;
static bool g_lvgl_ready;

#ifdef CONFIG_LV_USE_NUTTX_TOUCHSCREEN
static uint32_t g_input_retry_elapsed_ms;
#endif

static bool lvgl_app_wait_path(const char *path, uint32_t timeout_ms)
{
  uint32_t waited_ms = 0;

  if (path == NULL || path[0] == '\0')
    {
      return false;
    }

  while (access(path, F_OK) < 0)
    {
      if (waited_ms >= timeout_ms)
        {
          return false;
        }

      usleep(50u * 1000u);
      waited_ms += 50u;
    }

  return true;
}

static const char *lvgl_app_primary_fb_path(void)
{
  return CONFIG_LVGL_APP_HAL_DISPLAY_FB_PATH;
}

static const char *lvgl_app_input_path(void)
{
#ifdef CONFIG_LV_USE_NUTTX_TOUCHSCREEN
  return CONFIG_LVGL_APP_HAL_INPUT_DEV_PATH;
#else
  return NULL;
#endif
}

static int lvgl_app_try_nuttx_init(const char *fb_path,
                                   const char *input_path)
{
  lv_nuttx_dsc_t dsc;

  memset(&g_lvgl_result, 0, sizeof(g_lvgl_result));
  lv_nuttx_dsc_init(&dsc);
  dsc.fb_path = fb_path;
  dsc.input_path = input_path;

  lv_nuttx_init(&dsc, &g_lvgl_result);
  if (g_lvgl_result.disp == NULL)
    {
      return -ENODEV;
    }

  return 0;
}

static int lvgl_app_try_fbdev_compat_init(const char *fb_path,
                                          const char *input_path)
{
  lv_nuttx_dsc_t dsc;
  lv_display_t *disp;

  memset(&g_lvgl_result, 0, sizeof(g_lvgl_result));

  lv_nuttx_dsc_init(&dsc);
  dsc.fb_path = NULL;
  dsc.input_path = input_path;

  lv_nuttx_init(&dsc, &g_lvgl_result);

  disp = lvgl_app_fbdev_compat_create(fb_path);
  if (disp == NULL)
    {
      lv_nuttx_deinit(&g_lvgl_result);
      memset(&g_lvgl_result, 0, sizeof(g_lvgl_result));
      return -ENODEV;
    }

  g_lvgl_result.disp = disp;
#ifdef CONFIG_LV_USE_NUTTX_TOUCHSCREEN
  if (g_lvgl_result.indev != NULL)
    {
      lv_indev_set_display(g_lvgl_result.indev, disp);
    }
#endif

  return 0;
}

static int lvgl_app_try_display_init(const char *fb_path,
                                     const char *input_path)
{
  if (lvgl_app_fbdev_compat_matches(fb_path))
    {
      syslog(LOG_WARNING, "%s: framebuffer requires RGB565 compat: %s\n",
             LVGL_APP_TAG, fb_path);
      return lvgl_app_try_fbdev_compat_init(fb_path, input_path);
    }

  return lvgl_app_try_nuttx_init(fb_path, input_path);
}

static void lvgl_app_try_attach_input(void)
{
#ifdef CONFIG_LV_USE_NUTTX_TOUCHSCREEN
  const char *input_path = lvgl_app_input_path();
  lv_indev_t *indev;

  if (!g_lvgl_ready || g_lvgl_result.indev != NULL ||
      input_path == NULL || input_path[0] == '\0')
    {
      return;
    }

  if (access(input_path, F_OK) < 0)
    {
      return;
    }

  indev = lv_nuttx_touchscreen_create(input_path);
  if (indev == NULL)
    {
      syslog(LOG_WARNING, "%s: input attach failed: %s\n",
             LVGL_APP_TAG, input_path);
      return;
    }

  if (g_lvgl_result.disp != NULL)
    {
      lv_indev_set_display(indev, g_lvgl_result.disp);
    }

  g_lvgl_result.indev = indev;
  syslog(LOG_INFO, "%s: input attached: %s\n", LVGL_APP_TAG, input_path);
#endif
}

static int lvgl_app_init_display(void)
{
  const char *input;
  const char *primary;
  const char *fallback;
  bool primary_ready;
  bool fallback_ready;
  int ret;

  if (g_lvgl_ready)
    {
      return 0;
    }

  if (!lv_is_initialized())
    {
      lv_init();
    }

  primary = lvgl_app_primary_fb_path();
  input = lvgl_app_input_path();
  fallback = NULL;
  fallback_ready = false;
  primary_ready =
    lvgl_app_wait_path(primary, CONFIG_LVGL_APP_HAL_DISPLAY_DEVICE_WAIT_MS);
  if (!primary_ready)
    {
      syslog(LOG_WARNING, "%s: display node not ready: %s\n",
             LVGL_APP_TAG, primary);
    }

  if (input != NULL && input[0] != '\0' &&
      !lvgl_app_wait_path(input, CONFIG_LVGL_APP_HAL_DISPLAY_DEVICE_WAIT_MS))
    {
      syslog(LOG_WARNING, "%s: input node not ready: %s\n",
             LVGL_APP_TAG, input);
    }

  if (primary_ready)
    {
      ret = lvgl_app_try_display_init(primary, input);
    }
  else
    {
      ret = -ENODEV;
    }

  if (ret < 0)
    {
      lv_nuttx_deinit(&g_lvgl_result);
      memset(&g_lvgl_result, 0, sizeof(g_lvgl_result));

      fallback = strcmp(primary, "/dev/fb0") == 0 ? NULL : "/dev/fb0";
      if (fallback == NULL)
        {
          ret = -ENODEV;
        }
      else
        {
          fallback_ready = lvgl_app_wait_path(
            fallback, CONFIG_LVGL_APP_HAL_DISPLAY_DEVICE_WAIT_MS);
          if (fallback_ready)
            {
              ret = lvgl_app_try_display_init(fallback, input);
            }
        }
    }

  if (ret < 0)
    {
      lv_nuttx_deinit(&g_lvgl_result);
      memset(&g_lvgl_result, 0, sizeof(g_lvgl_result));

      syslog(LOG_WARNING,
             "%s: standard fb init failed: %d, trying RGB565 compat\n",
             LVGL_APP_TAG, ret);

      if (primary_ready)
        {
          ret = lvgl_app_try_fbdev_compat_init(primary, input);
        }

      if (ret < 0 && fallback != NULL && fallback_ready)
        {
          ret = lvgl_app_try_fbdev_compat_init(fallback, input);
        }
    }

  if (ret < 0)
    {
      lv_nuttx_deinit(&g_lvgl_result);
      memset(&g_lvgl_result, 0, sizeof(g_lvgl_result));
      syslog(LOG_ERR, "%s: display init failed: %d\n", LVGL_APP_TAG, ret);
      return ret;
    }

  g_lvgl_ready = true;

  if (input != NULL && input[0] != '\0' && g_lvgl_result.indev == NULL)
    {
      syslog(LOG_WARNING, "%s: input init deferred: %s\n",
             LVGL_APP_TAG, input);
    }

  return 0;
}

#ifndef CONFIG_LVGL_APP_UI_DEMO
static void lvgl_app_create_blank_screen(void)
{
  lv_obj_t *screen = lv_screen_active();

  if (screen != NULL)
    {
      lv_obj_clean(screen);
      lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
      lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    }
}
#endif

static void lvgl_app_create_ui(void)
{
#ifdef CONFIG_LVGL_APP_UI_DEMO
  ui_demo_init();
#else
  lvgl_app_create_blank_screen();
#endif
}

int lvgl_app_init(void)
{
  int ret;

  ret = lvgl_app_init_display();
  if (ret < 0)
    {
      return ret;
    }

  (void)lvgl_app_backlight_set_percent(
    CONFIG_LVGL_APP_BACKLIGHT_DEFAULT_PERCENT);
  lvgl_app_create_ui();
  syslog(LOG_INFO, "%s: ready\n", LVGL_APP_TAG);
  return 0;
}

void lvgl_app_shutdown(void)
{
#ifdef CONFIG_LVGL_APP_UI_DEMO
  ui_demo_deinit();
#endif

  lvgl_app_backlight_shutdown();

  if (g_lvgl_ready)
    {
      lv_nuttx_deinit(&g_lvgl_result);
      memset(&g_lvgl_result, 0, sizeof(g_lvgl_result));
      g_lvgl_ready = false;
#ifdef CONFIG_LV_USE_NUTTX_TOUCHSCREEN
      g_input_retry_elapsed_ms = 0;
#endif
    }
}

uint32_t lvgl_app_run_once(void)
{
  uint32_t idle_ms;

  if (!lv_is_initialized())
    {
      return CONFIG_LVGL_APP_APP_MAIN_LOOP_MAX_SLEEP_MS;
    }

  idle_ms = lv_timer_handler();
#ifdef CONFIG_LV_USE_NUTTX_TOUCHSCREEN
  if (g_lvgl_result.indev == NULL)
    {
      g_input_retry_elapsed_ms += idle_ms == 0 ? 1 : idle_ms;
      if (g_input_retry_elapsed_ms >= LVGL_APP_INPUT_RETRY_INTERVAL_MS)
        {
          g_input_retry_elapsed_ms = 0;
          lvgl_app_try_attach_input();
        }
    }
#endif

  return idle_ms == 0 ? 1 : idle_ms;
}
