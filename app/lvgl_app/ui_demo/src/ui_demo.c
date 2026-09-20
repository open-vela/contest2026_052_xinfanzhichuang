#include "app_main/lvgl_app_backlight.h"
#include "config/lvgl_app_config.h"
#include "ui_demo/inc/ui_demo.h"
#include "ui_demo/inc/ui_demo_ai.h"
#include "ui_demo/inc/ui_demo_bluetooth.h"
#include "ui_demo/inc/ui_demo_netinfo.h"
#include "ui_demo/inc/ui_demo_wifi.h"
#include "ui_demo/resource/ui_demo_font.h"

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <lvgl/lvgl.h>

#define UI_DEMO_SCREEN_W 1024
#define UI_DEMO_SCREEN_H 600

#define UI_DEMO_LEFT_W 220
#define UI_DEMO_CENTER_W 584
#define UI_DEMO_RIGHT_W 220

#define UI_DEMO_LEFT_X 0
#define UI_DEMO_CENTER_X UI_DEMO_LEFT_W
#define UI_DEMO_RIGHT_X (UI_DEMO_LEFT_W + UI_DEMO_CENTER_W)

#define UI_DEMO_PAD 12
#define UI_DEMO_RADIUS 24
#define UI_DEMO_BUTTON_SIZE 68
#define UI_DEMO_MAX_DEVICES 16
#define UI_DEMO_TEXT_NAME_LEN 48
#define UI_DEMO_TEXT_STATUS_LEN 64
#define UI_DEMO_SCROLL_SPACER_H 1
#define UI_DEMO_LEFT_SCROLL_SPACER_Y 640

#define UI_DEMO_FONT (&ui_demo_font_16)

enum ui_demo_function_id
{
  UI_DEMO_FUNCTION_WIFI = 0,
  UI_DEMO_FUNCTION_BLUETOOTH,
  UI_DEMO_FUNCTION_AI,
  UI_DEMO_FUNCTION_LIGHT,
  UI_DEMO_FUNCTION_SECURITY,
  UI_DEMO_FUNCTION_SCENE,
  UI_DEMO_FUNCTION_AC,
  UI_DEMO_FUNCTION_CURTAIN,
  UI_DEMO_FUNCTION_MUSIC,
  UI_DEMO_FUNCTION_MORE,
  UI_DEMO_FUNCTION_COUNT
};

enum ui_demo_wifi_view
{
  UI_DEMO_WIFI_VIEW_LIST = 0,
  UI_DEMO_WIFI_VIEW_CONNECT
};

struct ui_demo_function_dsc
{
  const char *name;
  const char *icon;
};

struct ui_demo_device_record
{
  char name[UI_DEMO_TEXT_NAME_LEN];
  char status[UI_DEMO_TEXT_STATUS_LEN];
  int32_t value;
};

struct ui_demo_state
{
  lv_obj_t *screen;
  lv_obj_t *background;
  lv_obj_t *left_panel;
  lv_obj_t *center_panel;
  lv_obj_t *right_panel;
  lv_obj_t *function_items[UI_DEMO_FUNCTION_COUNT];
  lv_obj_t *function_buttons[UI_DEMO_FUNCTION_COUNT];
  lv_obj_t *function_icons[UI_DEMO_FUNCTION_COUNT];
  lv_obj_t *function_labels[UI_DEMO_FUNCTION_COUNT];
  lv_obj_t *time_label;
  lv_obj_t *temperature_label;
  lv_obj_t *humidity_label;
  lv_obj_t *brightness_slider;
  lv_obj_t *brightness_value_label;
  lv_obj_t *volume_slider;
  lv_obj_t *volume_value_label;
  lv_obj_t *wifi_status_label;
  lv_obj_t *wifi_password_ta;
  lv_obj_t *wifi_password_eye_label;
  lv_obj_t *wifi_keyboard;
  lv_obj_t *bluetooth_status_label;
  lv_obj_t *ai_status_label;
  lv_obj_t *ai_voice_label;
  lv_obj_t *ai_voice_switch;
  lv_obj_t *weather_label;
  enum ui_demo_function_id selected_function;
  enum ui_demo_wifi_view wifi_view;
  bool has_selection;
  bool wifi_restart_after_stop;
  bool function_enabled[UI_DEMO_FUNCTION_COUNT];
  bool function_long_pressed[UI_DEMO_FUNCTION_COUNT];
  char wifi_selected_ssid[UI_DEMO_WIFI_SSID_LEN];
  char time_text[UI_DEMO_TEXT_STATUS_LEN];
  char weather_text[UI_DEMO_TEXT_STATUS_LEN];
  int32_t temperature_c;
  int32_t humidity_percent;
  bool temperature_valid;
  bool humidity_valid;
  int32_t brightness_percent;
  int32_t volume_percent;
  struct ui_demo_device_record wifi_devices[UI_DEMO_MAX_DEVICES];
  size_t wifi_count;
};

static const struct ui_demo_function_dsc g_function_dscs[] =
{
  { "WiFi", "W" },
  { "蓝牙", "蓝" },
  { "AI", "AI" },
  { "灯光", "灯" },
  { "安防", "安" },
  { "场景", "场" },
  { "空调", "空" },
  { "窗帘", "窗" },
  { "音乐", "音" },
  { "更多", "+" },
};

static struct ui_demo_state g_ui;

static const char *ui_demo_function_name(enum ui_demo_function_id id)
{
  if (id >= 0 && id < UI_DEMO_FUNCTION_COUNT)
    {
      return g_function_dscs[id].name;
    }

  return "unknown";
}

static const char *ui_demo_event_name(lv_event_code_t code)
{
  switch (code)
    {
      case LV_EVENT_PRESSED:
        return "pressed";
      case LV_EVENT_PRESSING:
        return "pressing";
      case LV_EVENT_PRESS_LOST:
        return "press_lost";
      case LV_EVENT_CLICKED:
        return "clicked";
      case LV_EVENT_RELEASED:
        return "released";
      case LV_EVENT_LONG_PRESSED:
        return "long_pressed";
      case LV_EVENT_SCROLL_BEGIN:
        return "scroll_begin";
      case LV_EVENT_SCROLL:
        return "scroll";
      case LV_EVENT_SCROLL_END:
        return "scroll_end";
      case LV_EVENT_VALUE_CHANGED:
        return "value_changed";
      default:
        return "event";
    }
}

static void ui_demo_get_active_point(lv_point_t *point)
{
  if (point == NULL)
    {
      return;
    }

  point->x = -1;
  point->y = -1;

  if (lv_indev_active() != NULL)
    {
      lv_indev_get_point(lv_indev_active(), point);
    }
}

static void ui_demo_log_event(const char *tag, lv_event_t *event)
{
  lv_point_t point;
  lv_event_code_t code = lv_event_get_code(event);

  ui_demo_get_active_point(&point);
  syslog(LOG_INFO, "ui_demo: %s %s p=(%ld,%ld) target=%p\n",
         tag, ui_demo_event_name(code), (long)point.x, (long)point.y,
         lv_event_get_target_obj(event));
}

static lv_color_t ui_demo_color(uint32_t rgb)
{
  return lv_color_hex(rgb);
}

static void ui_demo_force_refresh(const char *reason)
{
  if (g_ui.screen != NULL)
    {
      lv_obj_invalidate(g_ui.screen);
    }

  lv_refr_now(NULL);
  syslog(LOG_INFO, "ui_demo: refresh %s\n",
         reason == NULL ? "" : reason);
}

static int32_t ui_demo_clamp_percent(int32_t percent)
{
  if (percent < 0)
    {
      return 0;
    }

  if (percent > 100)
    {
      return 100;
    }

  return percent;
}

static void ui_demo_copy_text(char *dst, size_t dst_len, const char *src)
{
  if (dst_len == 0)
    {
      return;
    }

  if (src == NULL)
    {
      dst[0] = '\0';
      return;
    }

  snprintf(dst, dst_len, "%s", src);
}

static void ui_demo_set_panel_base(lv_obj_t *obj)
{
  lv_obj_remove_style_all(obj);
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

static lv_obj_t *ui_demo_create_panel(lv_obj_t *parent,
                                      int32_t x,
                                      int32_t y,
                                      int32_t w,
                                      int32_t h)
{
  lv_obj_t *obj = lv_obj_create(parent);

  ui_demo_set_panel_base(obj);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(obj, LV_DIR_NONE);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

  return obj;
}

static void ui_demo_create_background(lv_obj_t *screen)
{
  lv_obj_t *background = lv_obj_create(screen);

  lv_obj_remove_style_all(background);
  lv_obj_set_pos(background, 0, 0);
  lv_obj_set_size(background, UI_DEMO_SCREEN_W, UI_DEMO_SCREEN_H);
  lv_obj_set_style_bg_color(background, ui_demo_color(0xf8f9fb), 0);
  lv_obj_set_style_bg_opa(background, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(background, 0, 0);
  lv_obj_remove_flag(background, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(background, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(background, LV_SCROLLBAR_MODE_OFF);
  lv_obj_move_to_index(background, 0);

  g_ui.background = background;
}

static lv_obj_t *ui_demo_create_label(lv_obj_t *parent,
                                      const char *text,
                                      int32_t width,
                                      lv_color_t color)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text == NULL ? "" : text);
  lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
  lv_obj_set_width(label, width);
  lv_obj_set_style_text_font(label, UI_DEMO_FONT, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_set_style_text_letter_space(label, 0, 0);

  return label;
}

static void ui_demo_create_scroll_spacer(lv_obj_t *parent, int32_t y)
{
  lv_obj_t *spacer = lv_obj_create(parent);

  ui_demo_set_panel_base(spacer);
  lv_obj_set_pos(spacer, 0, y);
  lv_obj_set_size(spacer, 1, UI_DEMO_SCROLL_SPACER_H);
  lv_obj_remove_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);
}

static void ui_demo_scroll_event(lv_event_t *event)
{
  const char *name = lv_event_get_user_data(event);
  lv_obj_t *obj = lv_event_get_target_obj(event);
  lv_event_code_t code = lv_event_get_code(event);

  if (obj == NULL)
    {
      return;
    }

  syslog(LOG_INFO, "ui_demo: %s %s scroll=(%ld,%ld)\n",
         name == NULL ? "panel" : name,
         ui_demo_event_name(code),
         (long)lv_obj_get_scroll_x(obj),
         (long)lv_obj_get_scroll_y(obj));
}

static void ui_demo_add_scroll_logs(lv_obj_t *obj, const char *name)
{
  lv_obj_add_event_cb(obj, ui_demo_scroll_event, LV_EVENT_SCROLL_BEGIN,
                      (void *)name);
  lv_obj_add_event_cb(obj, ui_demo_scroll_event, LV_EVENT_SCROLL,
                      (void *)name);
  lv_obj_add_event_cb(obj, ui_demo_scroll_event, LV_EVENT_SCROLL_END,
                      (void *)name);
}

static void ui_demo_panel_touch_event(lv_event_t *event)
{
  ui_demo_log_event(lv_event_get_user_data(event), event);
}

#ifdef CONFIG_LVGL_APP_UI_DEMO_STATUS_WIDGETS
static lv_obj_t *ui_demo_create_rounded_box(lv_obj_t *parent,
                                            int32_t x,
                                            int32_t y,
                                            int32_t w,
                                            int32_t h)
{
  lv_obj_t *box = lv_obj_create(parent);

  lv_obj_remove_style_all(box);
  lv_obj_set_pos(box, x, y);
  lv_obj_set_size(box, w, h);
  lv_obj_set_style_radius(box, UI_DEMO_RADIUS, 0);
  lv_obj_set_style_bg_color(box, ui_demo_color(0xf1f3f5), 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(box, 1, 0);
  lv_obj_set_style_border_color(box, ui_demo_color(0xe1e5ea), 0);
  lv_obj_set_style_pad_all(box, UI_DEMO_PAD, 0);
  lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(box, LV_DIR_NONE);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

  return box;
}
#endif

static void ui_demo_update_function_visuals(void)
{
  uint32_t i;

  for (i = 0; i < UI_DEMO_FUNCTION_COUNT; i++)
    {
      const bool enabled = g_ui.function_enabled[i];
      const bool selected = g_ui.has_selection && g_ui.selected_function == i;
      const bool active = enabled || selected;
      lv_color_t icon_color = active ? ui_demo_color(0x242932) :
                                     ui_demo_color(0xf5f7fa);
      lv_color_t label_color = active ? ui_demo_color(0x242932) :
                                      ui_demo_color(0x5d646e);

      if (g_ui.function_buttons[i] != NULL)
        {
          if (active)
            {
              lv_obj_add_state(g_ui.function_buttons[i], LV_STATE_CHECKED);
            }
          else
            {
              lv_obj_remove_state(g_ui.function_buttons[i], LV_STATE_CHECKED);
            }
        }

      if (g_ui.function_items[i] != NULL)
        {
          lv_obj_set_style_radius(g_ui.function_items[i], 0, 0);
          lv_obj_set_style_bg_color(g_ui.function_items[i],
                                    ui_demo_color(0xf8f9fb), 0);
          lv_obj_set_style_bg_opa(g_ui.function_items[i], LV_OPA_TRANSP, 0);
          lv_obj_set_style_border_width(g_ui.function_items[i], 0, 0);
        }

      if (g_ui.function_icons[i] != NULL)
        {
          lv_obj_set_style_text_color(g_ui.function_icons[i], icon_color, 0);
        }

      if (g_ui.function_labels[i] != NULL)
        {
          lv_obj_set_style_text_color(g_ui.function_labels[i], label_color, 0);
        }
    }
}

static void ui_demo_render_center(void);
static void ui_demo_update_center_scroll(void);

static void ui_demo_clear_center_selection(void)
{
  g_ui.has_selection = false;
  g_ui.wifi_view = UI_DEMO_WIFI_VIEW_LIST;
  g_ui.wifi_status_label = NULL;
  g_ui.wifi_password_ta = NULL;
  g_ui.wifi_password_eye_label = NULL;
  g_ui.wifi_keyboard = NULL;
  g_ui.bluetooth_status_label = NULL;
  g_ui.ai_status_label = NULL;
  g_ui.ai_voice_label = NULL;
  g_ui.ai_voice_switch = NULL;

  if (g_ui.center_panel != NULL)
    {
      lv_obj_clean(g_ui.center_panel);
      lv_obj_scroll_to_y(g_ui.center_panel, 0, LV_ANIM_OFF);
      ui_demo_update_center_scroll();
    }
}

static void ui_demo_function_event(lv_event_t *event)
{
  const intptr_t raw_id = (intptr_t)lv_event_get_user_data(event);
  const enum ui_demo_function_id id = (enum ui_demo_function_id)raw_id;
  const lv_event_code_t code = lv_event_get_code(event);

  ui_demo_log_event(ui_demo_function_name(id), event);

  if (code == LV_EVENT_LONG_PRESSED)
    {
      g_ui.function_long_pressed[id] = true;

      if (id == UI_DEMO_FUNCTION_WIFI)
        {
          g_ui.wifi_restart_after_stop = false;
          g_ui.function_enabled[id] = false;
          (void)ui_demo_wifi_stop();
        }
      else if (id == UI_DEMO_FUNCTION_BLUETOOTH)
        {
          g_ui.function_enabled[id] = false;
          (void)ui_demo_bluetooth_stop();
        }
      else if (id == UI_DEMO_FUNCTION_AI)
        {
          g_ui.function_enabled[id] = false;
          (void)ui_demo_ai_stop();
        }
      else
        {
          g_ui.function_enabled[id] = false;
        }

      if (g_ui.has_selection && g_ui.selected_function == id)
        {
          ui_demo_clear_center_selection();
        }

      syslog(LOG_INFO, "ui_demo: function %s disabled\n",
             ui_demo_function_name(id));
      ui_demo_update_function_visuals();
      ui_demo_force_refresh("function-long-press");
      return;
    }

  if (code != LV_EVENT_CLICKED)
    {
      return;
    }

  if (g_ui.function_long_pressed[id])
    {
      g_ui.function_long_pressed[id] = false;
      return;
    }

  g_ui.selected_function = id;
  g_ui.has_selection = true;
  g_ui.function_enabled[id] = true;

  if (id == UI_DEMO_FUNCTION_WIFI)
    {
      g_ui.wifi_view = UI_DEMO_WIFI_VIEW_LIST;
      (void)ui_demo_wifi_start_scan(false);
    }
  else if (id == UI_DEMO_FUNCTION_BLUETOOTH)
    {
      (void)ui_demo_bluetooth_start_server();
    }
  else if (id == UI_DEMO_FUNCTION_AI)
    {
      (void)ui_demo_ai_start();
    }

  syslog(LOG_INFO, "ui_demo: function %s selected\n",
         ui_demo_function_name(id));

  ui_demo_update_function_visuals();
  ui_demo_render_center();
  ui_demo_force_refresh("function");
}

static void ui_demo_create_function_button(uint32_t index)
{
  lv_obj_t *item;
  lv_obj_t *button;
  lv_obj_t *icon;
  lv_obj_t *name;
  const int32_t item_w = 92;
  const int32_t item_h = 104;
  const int32_t gap_x = 12;
  const int32_t gap_y = 8;
  const int32_t col = index % 2;
  const int32_t row = index / 2;
  const int32_t x = UI_DEMO_PAD + col * (item_w + gap_x);
  const int32_t y = UI_DEMO_PAD + row * (item_h + gap_y);

  item = lv_obj_create(g_ui.left_panel);
  ui_demo_set_panel_base(item);
  lv_obj_set_pos(item, x, y);
  lv_obj_set_size(item, item_w, item_h);
  lv_obj_set_scrollbar_mode(item, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(item, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(item, ui_demo_function_event, LV_EVENT_PRESSED,
                      (void *)(intptr_t)index);
  lv_obj_add_event_cb(item, ui_demo_function_event, LV_EVENT_LONG_PRESSED,
                      (void *)(intptr_t)index);
  lv_obj_add_event_cb(item, ui_demo_function_event, LV_EVENT_CLICKED,
                      (void *)(intptr_t)index);

  button = lv_button_create(item);
  lv_obj_remove_style_all(button);
  lv_obj_set_size(button, UI_DEMO_BUTTON_SIZE, UI_DEMO_BUTTON_SIZE);
  lv_obj_set_pos(button, (item_w - UI_DEMO_BUTTON_SIZE) / 2, 0);
  lv_obj_set_style_radius(button, 999, 0);
  lv_obj_set_style_radius(button, 999, LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(button, ui_demo_color(0x3b3f45), 0);
  lv_obj_set_style_bg_color(button, ui_demo_color(0xffffff),
                            LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_CHECKED);
  lv_obj_set_style_border_width(button, 1, 0);
  lv_obj_set_style_border_width(button, 1, LV_STATE_CHECKED);
  lv_obj_set_style_border_color(button, ui_demo_color(0x2f3338), 0);
  lv_obj_set_style_border_color(button, ui_demo_color(0xd7dbe0),
                                LV_STATE_CHECKED);
  lv_obj_set_style_pad_all(button, 0, 0);
  lv_obj_set_scrollbar_mode(button, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);

  icon = ui_demo_create_label(button, g_function_dscs[index].icon,
                              UI_DEMO_BUTTON_SIZE,
                              ui_demo_color(0xf5f7fa));
  lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);

  name = ui_demo_create_label(item, g_function_dscs[index].name, item_w,
                              ui_demo_color(0x323842));
  lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(name, 0, UI_DEMO_BUTTON_SIZE + 8);

  g_ui.function_items[index] = item;
  g_ui.function_buttons[index] = button;
  g_ui.function_icons[index] = icon;
  g_ui.function_labels[index] = name;
}

static void ui_demo_create_left_panel(lv_obj_t *screen)
{
  uint32_t i;

  g_ui.left_panel = ui_demo_create_panel(screen, UI_DEMO_LEFT_X, 0,
                                         UI_DEMO_LEFT_W,
                                         UI_DEMO_SCREEN_H);

#ifdef CONFIG_LVGL_APP_UI_DEMO_LEFT_SCROLL
  lv_obj_add_flag(g_ui.left_panel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(g_ui.left_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(g_ui.left_panel, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(g_ui.left_panel, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_pad_bottom(g_ui.left_panel, UI_DEMO_PAD, 0);
  ui_demo_add_scroll_logs(g_ui.left_panel, "left");
#else
  lv_obj_set_scroll_dir(g_ui.left_panel, LV_DIR_NONE);
  lv_obj_set_scrollbar_mode(g_ui.left_panel, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(g_ui.left_panel, LV_OBJ_FLAG_SCROLLABLE);
#endif

  for (i = 0; i < UI_DEMO_FUNCTION_COUNT; i++)
    {
      ui_demo_create_function_button(i);
    }

#ifdef CONFIG_LVGL_APP_UI_DEMO_LEFT_SCROLL
  ui_demo_create_scroll_spacer(g_ui.left_panel, UI_DEMO_LEFT_SCROLL_SPACER_Y);
#endif
}

static void ui_demo_create_divider(lv_obj_t *parent,
                                   int32_t x,
                                   int32_t y,
                                   int32_t w,
                                   int32_t h)
{
  lv_obj_t *line = lv_obj_create(parent);

  lv_obj_remove_style_all(line);
  lv_obj_set_pos(line, x, y);
  lv_obj_set_size(line, w, h);
  lv_obj_set_style_bg_color(line, ui_demo_color(0xe8ebef), 0);
  lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
  lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
}

static void ui_demo_create_center_panel(lv_obj_t *screen)
{
  g_ui.center_panel = ui_demo_create_panel(screen, UI_DEMO_CENTER_X, 0,
                                           UI_DEMO_CENTER_W,
                                           UI_DEMO_SCREEN_H);

#ifdef CONFIG_LVGL_APP_UI_DEMO_CENTER_SCROLL
  lv_obj_add_flag(g_ui.center_panel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(g_ui.center_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(g_ui.center_panel, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(g_ui.center_panel, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_pad_bottom(g_ui.center_panel, UI_DEMO_PAD, 0);
  ui_demo_add_scroll_logs(g_ui.center_panel, "center");
#else
  lv_obj_set_scroll_dir(g_ui.center_panel, LV_DIR_NONE);
  lv_obj_set_scrollbar_mode(g_ui.center_panel, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(g_ui.center_panel, LV_OBJ_FLAG_SCROLLABLE);
#endif

  ui_demo_create_divider(screen, UI_DEMO_CENTER_X, UI_DEMO_PAD,
                         1, UI_DEMO_SCREEN_H - UI_DEMO_PAD * 2);
  ui_demo_create_divider(screen, UI_DEMO_RIGHT_X - 1, UI_DEMO_PAD,
                         1, UI_DEMO_SCREEN_H - UI_DEMO_PAD * 2);
}

static void ui_demo_set_center_scroll_enabled(bool enabled)
{
#ifdef CONFIG_LVGL_APP_UI_DEMO_CENTER_SCROLL
  if (g_ui.center_panel == NULL)
    {
      return;
    }

  if (enabled)
    {
      lv_obj_add_flag(g_ui.center_panel, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_flag(g_ui.center_panel, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_scroll_dir(g_ui.center_panel, LV_DIR_VER);
      lv_obj_set_scrollbar_mode(g_ui.center_panel, LV_SCROLLBAR_MODE_ACTIVE);
    }
  else
    {
      lv_obj_scroll_to_y(g_ui.center_panel, 0, LV_ANIM_OFF);
      lv_obj_set_scroll_dir(g_ui.center_panel, LV_DIR_NONE);
      lv_obj_set_scrollbar_mode(g_ui.center_panel, LV_SCROLLBAR_MODE_OFF);
      lv_obj_remove_flag(g_ui.center_panel, LV_OBJ_FLAG_SCROLLABLE);
    }
#else
  (void)enabled;
#endif
}

static void ui_demo_update_center_scroll(void)
{
#ifdef CONFIG_LVGL_APP_UI_DEMO_CENTER_SCROLL
  bool needs_scroll;

  if (g_ui.center_panel == NULL)
    {
      return;
    }

  lv_obj_update_layout(g_ui.center_panel);
  lv_obj_scroll_to_y(g_ui.center_panel, 0, LV_ANIM_OFF);
  lv_obj_update_layout(g_ui.center_panel);
  needs_scroll = lv_obj_get_scroll_bottom(g_ui.center_panel) > 0;
  ui_demo_set_center_scroll_enabled(needs_scroll);
#endif
}

static void ui_demo_refresh_environment_labels(void)
{
  if (g_ui.weather_label != NULL)
    {
      lv_label_set_text(g_ui.weather_label,
                        g_ui.weather_text[0] == '\0' ?
                        "天气 未获取" : g_ui.weather_text);
    }

  if (g_ui.temperature_label != NULL)
    {
      if (g_ui.temperature_valid)
        {
          lv_label_set_text_fmt(g_ui.temperature_label, "温度 %ld C",
                                (long)g_ui.temperature_c);
        }
      else
        {
          lv_label_set_text(g_ui.temperature_label, "温度 -- C");
        }
    }

  if (g_ui.humidity_label != NULL)
    {
      if (g_ui.humidity_valid)
        {
          lv_label_set_text_fmt(g_ui.humidity_label, "湿度 %ld%%",
                                (long)g_ui.humidity_percent);
        }
      else
        {
          lv_label_set_text(g_ui.humidity_label, "湿度 --");
        }
    }
}

static void ui_demo_refresh_slider_labels(void)
{
  if (g_ui.brightness_value_label != NULL)
    {
      lv_label_set_text_fmt(g_ui.brightness_value_label, "%ld%%",
                            (long)g_ui.brightness_percent);
    }

  if (g_ui.volume_value_label != NULL)
    {
      lv_label_set_text_fmt(g_ui.volume_value_label, "%ld%%",
                            (long)g_ui.volume_percent);
    }
}

static void ui_demo_apply_brightness(int32_t percent)
{
  int ret;

  percent = ui_demo_clamp_percent(percent);
  g_ui.brightness_percent = percent;

  ret = lvgl_app_backlight_set_percent(percent);
  if (ret < 0)
    {
      syslog(LOG_WARNING,
             "ui_demo: apply brightness %ld%% failed: %d\n",
             (long)percent, ret);
    }
}

#ifdef CONFIG_LVGL_APP_UI_DEMO_VERTICAL_SLIDERS
static void ui_demo_slider_event(lv_event_t *event)
{
  lv_obj_t *slider = lv_event_get_target_obj(event);
  lv_event_code_t code = lv_event_get_code(event);
  int32_t value;

  if (slider == g_ui.brightness_slider)
    {
      value = ui_demo_clamp_percent(lv_slider_get_value(slider));
      if (code == LV_EVENT_VALUE_CHANGED ||
          code == LV_EVENT_PRESSING ||
          code == LV_EVENT_RELEASED)
        {
          ui_demo_apply_brightness(value);
        }
      else
        {
          g_ui.brightness_percent = value;
        }

      syslog(LOG_INFO, "ui_demo: brightness slider %s value=%ld\n",
             ui_demo_event_name(code), (long)g_ui.brightness_percent);
    }
  else if (slider == g_ui.volume_slider)
    {
      g_ui.volume_percent = lv_slider_get_value(slider);
      syslog(LOG_INFO, "ui_demo: volume slider %s value=%ld\n",
             ui_demo_event_name(lv_event_get_code(event)),
             (long)g_ui.volume_percent);
    }

  ui_demo_refresh_slider_labels();
}

static void ui_demo_set_slider_value(lv_obj_t *slider,
                                     int32_t value,
                                     bool apply)
{
  int32_t previous_value;

  value = ui_demo_clamp_percent(value);
  previous_value = slider == g_ui.brightness_slider ?
                   g_ui.brightness_percent : g_ui.volume_percent;

  if (value == previous_value)
    {
      return;
    }

  lv_slider_set_value(slider, value, LV_ANIM_OFF);

  if (slider == g_ui.brightness_slider)
    {
      if (apply)
        {
          ui_demo_apply_brightness(value);
        }
      else
        {
          g_ui.brightness_percent = value;
        }

      syslog(LOG_INFO, "ui_demo: brightness touch value=%ld\n",
             (long)g_ui.brightness_percent);
    }
  else if (slider == g_ui.volume_slider)
    {
      g_ui.volume_percent = value;
      syslog(LOG_INFO, "ui_demo: volume touch value=%ld\n",
             (long)g_ui.volume_percent);
    }

  ui_demo_refresh_slider_labels();
}

static void ui_demo_slider_touch_event(lv_event_t *event)
{
  lv_obj_t *slider = lv_event_get_user_data(event);
  lv_obj_t *hit_area = lv_event_get_target_obj(event);
  lv_area_t coords;
  lv_point_t point;
  int32_t track_h;
  int32_t offset_y;
  int32_t value;
  lv_event_code_t code = lv_event_get_code(event);

  if (slider == NULL || hit_area == NULL)
    {
      syslog(LOG_WARNING, "ui_demo: slider touch ignored slider=%p hit=%p\n",
             slider, hit_area);
      return;
    }

  if (lv_indev_active() == NULL)
    {
      if (code == LV_EVENT_RELEASED && slider == g_ui.brightness_slider)
        {
          ui_demo_apply_brightness(lv_slider_get_value(slider));
          ui_demo_refresh_slider_labels();
        }

      return;
    }

  lv_obj_get_coords(hit_area, &coords);
  lv_indev_get_point(lv_indev_active(), &point);
  syslog(LOG_INFO,
         "ui_demo: slider touch %s p=(%ld,%ld) area=(%ld,%ld,%ld,%ld)\n",
         ui_demo_event_name(lv_event_get_code(event)),
         (long)point.x, (long)point.y,
         (long)coords.x1, (long)coords.y1,
         (long)coords.x2, (long)coords.y2);

  track_h = coords.y2 - coords.y1;
  if (track_h <= 0)
    {
      return;
    }

  offset_y = point.y - coords.y1;
  value = 100 - (offset_y * 100) / track_h;
  ui_demo_set_slider_value(slider, value,
                           slider == g_ui.brightness_slider ||
                           code == LV_EVENT_RELEASED);
}

static lv_obj_t *ui_demo_create_vertical_slider(lv_obj_t *parent,
                                                int32_t x,
                                                int32_t y,
                                                const char *title,
                                                int32_t min_value,
                                                int32_t value,
                                                lv_obj_t **value_label)
{
  lv_obj_t *label;
  lv_obj_t *slider;
  lv_obj_t *hit_area;

  label = ui_demo_create_label(parent, title, 72, ui_demo_color(0x323842));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(label, x, y);

  slider = lv_slider_create(parent);
  lv_obj_remove_style_all(slider);
  lv_obj_set_pos(slider, x + 17, y + 34);
  lv_obj_set_size(slider, 38, 228);
  min_value = ui_demo_clamp_percent(min_value);
  value = ui_demo_clamp_percent(value);
  if (value < min_value)
    {
      value = min_value;
    }

  lv_slider_set_range(slider, min_value, 100);
  lv_slider_set_value(slider, value, LV_ANIM_OFF);
  lv_obj_set_style_radius(slider, 999, LV_PART_MAIN);
  lv_obj_set_style_radius(slider, 999, LV_PART_INDICATOR);
  lv_obj_set_style_radius(slider, 999, LV_PART_KNOB);
  lv_obj_set_style_bg_color(slider, ui_demo_color(0xdfe4ea), LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, ui_demo_color(0x2f7cff),
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, ui_demo_color(0xffffff), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_pad_all(slider, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(slider, 0, LV_PART_INDICATOR);
  lv_obj_set_style_pad_all(slider, 0, LV_PART_KNOB);
  lv_obj_set_style_width(slider, 36, LV_PART_KNOB);
  lv_obj_set_style_height(slider, 36, LV_PART_KNOB);
  lv_obj_set_ext_click_area(slider, 24);
  lv_obj_add_flag(slider, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(slider, ui_demo_slider_event, LV_EVENT_VALUE_CHANGED,
                      NULL);
  lv_obj_add_event_cb(slider, ui_demo_slider_event, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(slider, ui_demo_slider_event, LV_EVENT_PRESSING, NULL);
  lv_obj_add_event_cb(slider, ui_demo_slider_event, LV_EVENT_RELEASED, NULL);

  hit_area = lv_obj_create(parent);
  ui_demo_set_panel_base(hit_area);
  lv_obj_set_pos(hit_area, x + 2, y + 28);
  lv_obj_set_size(hit_area, 68, 242);
  lv_obj_add_flag(hit_area, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(hit_area, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_event_cb(hit_area, ui_demo_slider_touch_event, LV_EVENT_PRESSED,
                      slider);
  lv_obj_add_event_cb(hit_area, ui_demo_slider_touch_event, LV_EVENT_PRESSING,
                      slider);
  lv_obj_add_event_cb(hit_area, ui_demo_slider_touch_event, LV_EVENT_RELEASED,
                      slider);
  lv_obj_move_to_index(hit_area, -1);

  *value_label = ui_demo_create_label(parent, "", 72, ui_demo_color(0x323842));
  lv_obj_set_style_text_align(*value_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(*value_label, x, y + 274);

  return slider;
}
#endif

static void ui_demo_create_right_panel(lv_obj_t *screen)
{
  g_ui.right_panel = ui_demo_create_panel(screen, UI_DEMO_RIGHT_X, 0,
                                          UI_DEMO_RIGHT_W,
                                          UI_DEMO_SCREEN_H);

  lv_obj_add_flag(g_ui.right_panel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_scroll_dir(g_ui.right_panel, LV_DIR_NONE);
  lv_obj_set_scrollbar_mode(g_ui.right_panel, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(g_ui.right_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(g_ui.right_panel, ui_demo_panel_touch_event,
                      LV_EVENT_PRESSED, "right");
  lv_obj_add_event_cb(g_ui.right_panel, ui_demo_panel_touch_event,
                      LV_EVENT_CLICKED, "right");

#ifdef CONFIG_LVGL_APP_UI_DEMO_STATUS_WIDGETS
  lv_obj_t *time_box;
  lv_obj_t *env_box;

  time_box = ui_demo_create_rounded_box(g_ui.right_panel, UI_DEMO_PAD,
                                        UI_DEMO_PAD,
                                        UI_DEMO_RIGHT_W - UI_DEMO_PAD * 2,
                                        58);
  g_ui.time_label = ui_demo_create_label(time_box, g_ui.time_text,
                                         UI_DEMO_RIGHT_W - UI_DEMO_PAD * 4,
                                         ui_demo_color(0x222832));
  lv_obj_set_style_text_align(g_ui.time_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(g_ui.time_label, LV_ALIGN_CENTER, 0, 0);

  env_box = ui_demo_create_rounded_box(g_ui.right_panel, UI_DEMO_PAD,
                                       84,
                                       UI_DEMO_RIGHT_W - UI_DEMO_PAD * 2,
                                       138);
  g_ui.weather_label =
    ui_demo_create_label(env_box, "", UI_DEMO_RIGHT_W - UI_DEMO_PAD * 4,
                         ui_demo_color(0x222832));
  lv_obj_set_pos(g_ui.weather_label, 0, 16);

  g_ui.temperature_label =
    ui_demo_create_label(env_box, "", UI_DEMO_RIGHT_W - UI_DEMO_PAD * 4,
                         ui_demo_color(0x222832));
  lv_obj_set_pos(g_ui.temperature_label, 0, 56);

  g_ui.humidity_label =
    ui_demo_create_label(env_box, "", UI_DEMO_RIGHT_W - UI_DEMO_PAD * 4,
                         ui_demo_color(0x222832));
  lv_obj_set_pos(g_ui.humidity_label, 0, 96);

  ui_demo_refresh_environment_labels();
#endif

#ifdef CONFIG_LVGL_APP_UI_DEMO_VERTICAL_SLIDERS
  g_ui.brightness_slider =
    ui_demo_create_vertical_slider(g_ui.right_panel, 28, 250,
                                   "亮度",
                                   0,
                                   g_ui.brightness_percent,
                                   &g_ui.brightness_value_label);
  g_ui.volume_slider =
    ui_demo_create_vertical_slider(g_ui.right_panel, 120, 250,
                                   "音量",
                                   0,
                                   g_ui.volume_percent,
                                   &g_ui.volume_value_label);
  ui_demo_refresh_slider_labels();
#endif
}

static lv_obj_t *ui_demo_create_action_button(lv_obj_t *parent,
                                              const char *text,
                                              int32_t x,
                                              int32_t y,
                                              int32_t w,
                                              int32_t h,
                                              bool primary)
{
  lv_obj_t *button;
  lv_obj_t *label;

  button = lv_button_create(parent);
  lv_obj_remove_style_all(button);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, w, h);
  lv_obj_set_style_radius(button, 8, 0);
  lv_obj_set_style_bg_color(button,
                            primary ? ui_demo_color(0x242932) :
                                      ui_demo_color(0xe7eaee), 0);
  lv_obj_set_style_bg_color(button,
                            primary ? ui_demo_color(0x383f4a) :
                                      ui_demo_color(0xdde1e6),
                            LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(button, 0, 0);
  lv_obj_set_style_pad_all(button, 0, 0);
  lv_obj_set_scrollbar_mode(button, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);

  label = ui_demo_create_label(button, text, w,
                               primary ? ui_demo_color(0xffffff) :
                                         ui_demo_color(0x242932));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

  return button;
}

static void ui_demo_sync_wifi_function_state(
  const struct ui_demo_wifi_snapshot *snapshot)
{
  if (snapshot == NULL)
    {
      return;
    }

  if (snapshot->enabled ||
      snapshot->scan_in_progress ||
      snapshot->connect_in_progress ||
      snapshot->stop_in_progress)
    {
      g_ui.function_enabled[UI_DEMO_FUNCTION_WIFI] = true;
    }
  else if (snapshot->status == UI_DEMO_WIFI_STATUS_OFF)
    {
      g_ui.function_enabled[UI_DEMO_FUNCTION_WIFI] = false;
    }
}

static void ui_demo_sync_bluetooth_function_state(
  const struct ui_demo_bluetooth_snapshot *snapshot)
{
  if (snapshot == NULL)
    {
      return;
    }

  if (snapshot->start_in_progress ||
      snapshot->server_running ||
      snapshot->connected)
    {
      g_ui.function_enabled[UI_DEMO_FUNCTION_BLUETOOTH] = true;
    }
  else if (snapshot->status == UI_DEMO_BLUETOOTH_STATUS_OFF ||
           snapshot->status == UI_DEMO_BLUETOOTH_STATUS_ERROR)
    {
      g_ui.function_enabled[UI_DEMO_FUNCTION_BLUETOOTH] = false;
    }
}

static void ui_demo_sync_ai_function_state(
  const struct ui_demo_ai_snapshot *snapshot)
{
  if (snapshot == NULL)
    {
      return;
    }

  if (snapshot->enabled ||
      snapshot->start_in_progress ||
      snapshot->stop_in_progress ||
      snapshot->status == UI_DEMO_AI_STATUS_CONNECTED)
    {
      g_ui.function_enabled[UI_DEMO_FUNCTION_AI] = true;
    }
  else if (snapshot->status == UI_DEMO_AI_STATUS_OFF ||
           snapshot->status == UI_DEMO_AI_STATUS_ERROR)
    {
      g_ui.function_enabled[UI_DEMO_FUNCTION_AI] = false;
    }
}

static void ui_demo_render_wifi_connect_page(const char *ssid);

static bool ui_demo_wifi_snapshot_is_connected_to(
  const struct ui_demo_wifi_snapshot *snapshot, const char *ssid)
{
  return snapshot != NULL &&
         ssid != NULL &&
         ssid[0] != '\0' &&
         snapshot->status == UI_DEMO_WIFI_STATUS_CONNECTED &&
         strcmp(snapshot->connected_ssid, ssid) == 0;
}

static void ui_demo_wifi_rescan_event(lv_event_t *event)
{
  if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
      return;
    }

  (void)ui_demo_wifi_start_scan(true);
  g_ui.wifi_view = UI_DEMO_WIFI_VIEW_LIST;
  ui_demo_render_center();
  ui_demo_force_refresh("wifi-rescan");
}

static void ui_demo_wifi_back_event(lv_event_t *event)
{
  if (lv_event_get_code(event) != LV_EVENT_CLICKED &&
      lv_event_get_code(event) != LV_EVENT_CANCEL)
    {
      return;
    }

  g_ui.wifi_view = UI_DEMO_WIFI_VIEW_LIST;
  ui_demo_render_center();
  ui_demo_force_refresh("wifi-back");
}

static void ui_demo_wifi_connect_submit(void)
{
  struct ui_demo_wifi_snapshot snapshot;
  const char *password;

  if (g_ui.wifi_password_ta == NULL)
    {
      return;
    }

  password = lv_textarea_get_text(g_ui.wifi_password_ta);
  (void)ui_demo_wifi_connect(g_ui.wifi_selected_ssid, password);
  ui_demo_wifi_get_snapshot(&snapshot);

  if (g_ui.wifi_status_label != NULL)
    {
      lv_label_set_text(g_ui.wifi_status_label, snapshot.message);
    }

  ui_demo_force_refresh("wifi-connect");
}

static void ui_demo_wifi_connect_event(lv_event_t *event)
{
  lv_event_code_t code = lv_event_get_code(event);

  if (code != LV_EVENT_CLICKED && code != LV_EVENT_READY)
    {
      return;
    }

  ui_demo_wifi_connect_submit();
}

static void ui_demo_wifi_password_eye_event(lv_event_t *event)
{
  lv_event_code_t code = lv_event_get_code(event);
  lv_obj_t *target;
  bool show_password;

  if (code != LV_EVENT_CLICKED || g_ui.wifi_password_ta == NULL)
    {
      return;
    }

  show_password = lv_textarea_get_password_mode(g_ui.wifi_password_ta);
  lv_textarea_set_password_mode(g_ui.wifi_password_ta, !show_password);

  target = lv_event_get_target_obj(event);
  if (target != NULL)
    {
      if (show_password)
        {
          lv_obj_add_state(target, LV_STATE_CHECKED);
        }
      else
        {
          lv_obj_remove_state(target, LV_STATE_CHECKED);
        }
    }

  if (g_ui.wifi_password_eye_label != NULL)
    {
      lv_obj_set_style_text_color(
        g_ui.wifi_password_eye_label,
        show_password ? ui_demo_color(0x1f5eff) : ui_demo_color(0x606875),
        0);
    }

  ui_demo_force_refresh("wifi-password-eye");
}

static void ui_demo_wifi_disconnect_event(lv_event_t *event)
{
  struct ui_demo_wifi_snapshot snapshot;

  if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
      return;
    }

  g_ui.wifi_restart_after_stop = true;
  (void)ui_demo_wifi_stop();
  ui_demo_wifi_get_snapshot(&snapshot);
  g_ui.wifi_view = UI_DEMO_WIFI_VIEW_LIST;
  ui_demo_render_center();

  if (g_ui.wifi_status_label != NULL)
    {
      lv_label_set_text(g_ui.wifi_status_label, snapshot.message);
    }

  ui_demo_force_refresh("wifi-disconnect");
}

static void ui_demo_wifi_password_event(lv_event_t *event)
{
  lv_event_code_t code = lv_event_get_code(event);

  if (code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED)
    {
      if (g_ui.wifi_keyboard != NULL)
        {
          lv_obj_clear_flag(g_ui.wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
          lv_obj_move_foreground(g_ui.wifi_keyboard);
          ui_demo_update_center_scroll();
        }

      return;
    }

  if (code == LV_EVENT_READY)
    {
      ui_demo_wifi_connect_submit();
    }
}

static void ui_demo_wifi_keyboard_event(lv_event_t *event)
{
  if (lv_event_get_code(event) == LV_EVENT_CANCEL)
    {
      if (g_ui.wifi_keyboard != NULL)
        {
          lv_obj_add_flag(g_ui.wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
          ui_demo_update_center_scroll();
        }
    }
}

static void ui_demo_wifi_ap_event(lv_event_t *event)
{
  struct ui_demo_wifi_snapshot snapshot;
  intptr_t raw_index;
  size_t index;

  if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
      return;
    }

  raw_index = (intptr_t)lv_event_get_user_data(event);
  if (raw_index < 0)
    {
      return;
    }

  index = (size_t)raw_index;
  ui_demo_wifi_get_snapshot(&snapshot);
  if (index >= snapshot.ap_count)
    {
      return;
    }

  ui_demo_copy_text(g_ui.wifi_selected_ssid,
                    sizeof(g_ui.wifi_selected_ssid),
                    snapshot.aps[index].ssid);
  ui_demo_render_wifi_connect_page(g_ui.wifi_selected_ssid);
  ui_demo_force_refresh("wifi-ap");
}

static void ui_demo_render_wifi_empty_state(
  const struct ui_demo_wifi_snapshot *snapshot)
{
  lv_obj_t *title;
  lv_obj_t *status;
  const int32_t content_w = UI_DEMO_CENTER_W - UI_DEMO_PAD * 2;

  title = ui_demo_create_label(g_ui.center_panel, "WiFi",
                               content_w, ui_demo_color(0x242932));
  lv_obj_set_pos(title, UI_DEMO_PAD, 138);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

  status = ui_demo_create_label(g_ui.center_panel,
                                snapshot->message[0] == '\0' ?
                                "暂无扫描结果" : snapshot->message,
                                content_w, ui_demo_color(0x606875));
  lv_obj_set_pos(status, UI_DEMO_PAD, 170);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
}

static void ui_demo_render_wifi_row(const struct ui_demo_wifi_ap *ap,
                                    uint32_t index,
                                    int32_t y)
{
  lv_obj_t *row;
  lv_obj_t *name;
  lv_obj_t *status;
  lv_obj_t *signal;
  char signal_text[16];
  const int32_t row_w = UI_DEMO_CENTER_W - UI_DEMO_PAD * 2;
  const int32_t row_h = 64;

  row = lv_obj_create(g_ui.center_panel);
  ui_demo_set_panel_base(row);
  lv_obj_set_pos(row, UI_DEMO_PAD, y);
  lv_obj_set_size(row, row_w, row_h);
  lv_obj_set_style_bg_color(row, ui_demo_color(0xf1f3f5), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_radius(row, 8, LV_STATE_PRESSED);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(row, ui_demo_wifi_ap_event, LV_EVENT_CLICKED,
                      (void *)(intptr_t)index);

  name = ui_demo_create_label(row, ap->ssid, row_w - 120,
                              ui_demo_color(0x222832));
  lv_obj_set_pos(name, 0, 8);

  snprintf(signal_text, sizeof(signal_text), "%ld%%", (long)ap->signal);
  signal = ui_demo_create_label(row, signal_text, 96,
                                ui_demo_color(0x242932));
  lv_obj_set_style_text_align(signal, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(signal, row_w - 96, 8);

  status = ui_demo_create_label(row, ap->status, row_w,
                                ui_demo_color(0x606875));
  lv_obj_set_pos(status, 0, 36);

  ui_demo_create_divider(row, 0, row_h - 1, row_w, 1);
}

static void ui_demo_render_wifi_list(void)
{
  struct ui_demo_wifi_snapshot snapshot;
  lv_obj_t *title;
  lv_obj_t *status;
  lv_obj_t *rescan;
  size_t i;
  const int32_t content_w = UI_DEMO_CENTER_W - UI_DEMO_PAD * 2;
  const int32_t list_y = 86;

  ui_demo_wifi_get_snapshot(&snapshot);
  ui_demo_sync_wifi_function_state(&snapshot);

  g_ui.wifi_view = UI_DEMO_WIFI_VIEW_LIST;
  g_ui.wifi_status_label = NULL;
  g_ui.wifi_password_ta = NULL;
  g_ui.wifi_password_eye_label = NULL;
  g_ui.wifi_keyboard = NULL;

  title = ui_demo_create_label(g_ui.center_panel, "WiFi",
                               content_w - 144,
                               ui_demo_color(0x222832));
  lv_obj_set_pos(title, UI_DEMO_PAD, UI_DEMO_PAD + 4);

  rescan = ui_demo_create_action_button(g_ui.center_panel, "重新扫描",
                                        UI_DEMO_CENTER_W - UI_DEMO_PAD - 118,
                                        UI_DEMO_PAD, 118, 38, false);
  lv_obj_add_event_cb(rescan, ui_demo_wifi_rescan_event, LV_EVENT_CLICKED,
                      NULL);

  status = ui_demo_create_label(g_ui.center_panel,
                                snapshot.message[0] == '\0' ?
                                "选择 WiFi 后输入密码连接" :
                                snapshot.message,
                                content_w, ui_demo_color(0x606875));
  lv_obj_set_pos(status, UI_DEMO_PAD, UI_DEMO_PAD + 42);
  g_ui.wifi_status_label = status;

  ui_demo_create_divider(g_ui.center_panel, UI_DEMO_PAD, 76, content_w, 1);

  if (snapshot.ap_count == 0)
    {
      ui_demo_render_wifi_empty_state(&snapshot);
      return;
    }

  for (i = 0; i < snapshot.ap_count; i++)
    {
      ui_demo_render_wifi_row(&snapshot.aps[i], i,
                              list_y + (int32_t)i * 64);
    }
}

static void ui_demo_render_wifi_connect_page(const char *ssid)
{
  struct ui_demo_wifi_snapshot snapshot;
  lv_obj_t *title;
  lv_obj_t *ssid_label;
  lv_obj_t *hint;
  lv_obj_t *textarea;
  lv_obj_t *eye;
  lv_obj_t *eye_label;
  lv_obj_t *keyboard;
  lv_obj_t *connect;
  lv_obj_t *back;
  bool connected;
  const int32_t content_w = UI_DEMO_CENTER_W - UI_DEMO_PAD * 2;
  const int32_t connect_y = 214;
  const int32_t connect_h = 42;
  const int32_t keyboard_y = connect_y + connect_h + 8;
  const int32_t keyboard_h = UI_DEMO_SCREEN_H - keyboard_y - UI_DEMO_PAD;

  if (g_ui.center_panel == NULL)
    {
      return;
    }

  ui_demo_wifi_get_snapshot(&snapshot);
  connected = ui_demo_wifi_snapshot_is_connected_to(&snapshot, ssid);

  lv_obj_clean(g_ui.center_panel);
  lv_obj_scroll_to_y(g_ui.center_panel, 0, LV_ANIM_OFF);
  g_ui.wifi_view = UI_DEMO_WIFI_VIEW_CONNECT;
  g_ui.wifi_password_ta = NULL;
  g_ui.wifi_password_eye_label = NULL;
  g_ui.wifi_keyboard = NULL;

  title = ui_demo_create_label(g_ui.center_panel, "连接 WiFi",
                               content_w - 120,
                               ui_demo_color(0x222832));
  lv_obj_set_pos(title, UI_DEMO_PAD, UI_DEMO_PAD + 2);

  back = ui_demo_create_action_button(g_ui.center_panel, "返回",
                                      UI_DEMO_CENTER_W - UI_DEMO_PAD - 88,
                                      UI_DEMO_PAD, 88, 38, false);
  lv_obj_add_event_cb(back, ui_demo_wifi_back_event, LV_EVENT_CLICKED, NULL);

  ssid_label = ui_demo_create_label(g_ui.center_panel, ssid,
                                    content_w, ui_demo_color(0x242932));
  lv_obj_set_pos(ssid_label, UI_DEMO_PAD, 58);

  g_ui.wifi_status_label =
    ui_demo_create_label(g_ui.center_panel,
                         snapshot.message[0] == '\0' ?
                         (connected ? "已连接到当前 WiFi" :
                                      "请输入 WiFi 密码") :
                         snapshot.message,
                         content_w, ui_demo_color(0x606875));
  lv_obj_set_pos(g_ui.wifi_status_label, UI_DEMO_PAD, 86);

  if (connected)
    {
      connect = ui_demo_create_action_button(g_ui.center_panel, "关闭连接",
                                             UI_DEMO_PAD, 126, 132, 42,
                                             true);
      lv_obj_add_event_cb(connect, ui_demo_wifi_disconnect_event,
                          LV_EVENT_CLICKED, NULL);
      ui_demo_update_center_scroll();
      return;
    }

  textarea = lv_textarea_create(g_ui.center_panel);
  lv_obj_set_pos(textarea, UI_DEMO_PAD, 126);
  lv_obj_set_size(textarea, content_w, 48);
  lv_obj_set_style_text_font(textarea, UI_DEMO_FONT, 0);
  lv_obj_set_style_text_color(textarea, ui_demo_color(0x222832), 0);
  lv_obj_set_style_bg_color(textarea, ui_demo_color(0xffffff), 0);
  lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(textarea, 1, 0);
  lv_obj_set_style_border_color(textarea, ui_demo_color(0xd8dde4), 0);
  lv_obj_set_style_radius(textarea, 8, 0);
  lv_obj_set_style_pad_all(textarea, 10, 0);
  lv_obj_set_style_pad_right(textarea, 58, 0);
  lv_textarea_set_one_line(textarea, true);
  lv_textarea_set_password_mode(textarea, true);
  lv_textarea_set_password_show_time(textarea, 1000);
  lv_textarea_set_placeholder_text(textarea, "输入密码，开放网络可留空");
  lv_obj_remove_flag(textarea, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_add_event_cb(textarea, ui_demo_wifi_password_event,
                      LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(textarea, ui_demo_wifi_password_event,
                      LV_EVENT_FOCUSED, NULL);
  lv_obj_add_event_cb(textarea, ui_demo_wifi_password_event,
                      LV_EVENT_READY, NULL);
  g_ui.wifi_password_ta = textarea;

  eye = lv_button_create(g_ui.center_panel);
  lv_obj_remove_style_all(eye);
  lv_obj_set_pos(eye, UI_DEMO_PAD + content_w - 46, 130);
  lv_obj_set_size(eye, 42, 40);
  lv_obj_set_style_radius(eye, 8, 0);
  lv_obj_set_style_bg_color(eye, ui_demo_color(0xe7eaee), LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(eye, ui_demo_color(0xeaf2ff), LV_STATE_CHECKED);
  lv_obj_set_style_bg_opa(eye, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, LV_STATE_CHECKED);
  lv_obj_set_style_border_width(eye, 0, 0);
  lv_obj_set_style_pad_all(eye, 0, 0);
  lv_obj_set_scrollbar_mode(eye, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(eye, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_remove_flag(eye, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(eye, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_add_event_cb(eye, ui_demo_wifi_password_eye_event,
                      LV_EVENT_CLICKED, NULL);

  eye_label = ui_demo_create_label(eye, LV_SYMBOL_EYE_OPEN, 42,
                                   ui_demo_color(0x606875));
  lv_obj_set_style_text_align(eye_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(eye_label, LV_ALIGN_CENTER, 0, 0);
  lv_obj_remove_flag(eye_label, LV_OBJ_FLAG_SCROLLABLE);
  g_ui.wifi_password_eye_label = eye_label;

  hint = ui_demo_create_label(g_ui.center_panel,
                              "WPA/WPA2 密码 8-63 位；开放网络留空",
                              content_w, ui_demo_color(0x7b838e));
  lv_obj_set_pos(hint, UI_DEMO_PAD, 184);

  connect = ui_demo_create_action_button(g_ui.center_panel, "确认连接",
                                         UI_DEMO_PAD, connect_y, 132,
                                         connect_h, true);
  lv_obj_add_event_cb(connect, ui_demo_wifi_connect_event,
                      LV_EVENT_CLICKED, NULL);

  keyboard = lv_keyboard_create(g_ui.center_panel);
  lv_obj_set_size(keyboard, content_w, keyboard_h);
  /* lv_keyboard_create() defaults to bottom alignment. Pin it below Connect. */
  lv_obj_align(keyboard, LV_ALIGN_TOP_LEFT, UI_DEMO_PAD, keyboard_y);
  lv_keyboard_set_textarea(keyboard, textarea);
  lv_obj_add_event_cb(keyboard, ui_demo_wifi_keyboard_event,
                      LV_EVENT_CANCEL, NULL);
  lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
  g_ui.wifi_keyboard = keyboard;

  ui_demo_update_center_scroll();
}

static const char *ui_demo_bluetooth_status_text(
  enum ui_demo_bluetooth_status status)
{
  switch (status)
    {
      case UI_DEMO_BLUETOOTH_STATUS_PRESTARTING:
        return "正在执行 btstart";

      case UI_DEMO_BLUETOOTH_STATUS_READY:
        return "底层已启动";

      case UI_DEMO_BLUETOOTH_STATUS_STARTING:
        return "正在设置蓝牙";

      case UI_DEMO_BLUETOOTH_STATUS_ADVERTISING:
        return "等待连接";

      case UI_DEMO_BLUETOOTH_STATUS_CONNECTED:
        return "连接成功";

      case UI_DEMO_BLUETOOTH_STATUS_STOPPING:
        return "正在关闭";

      case UI_DEMO_BLUETOOTH_STATUS_ERROR:
        return "蓝牙失败";

      case UI_DEMO_BLUETOOTH_STATUS_OFF:
      default:
        return "蓝牙已关闭";
    }
}

static lv_color_t ui_demo_bluetooth_status_color(
  enum ui_demo_bluetooth_status status)
{
  if (status == UI_DEMO_BLUETOOTH_STATUS_CONNECTED)
    {
      return ui_demo_color(0x16845b);
    }

  if (status == UI_DEMO_BLUETOOTH_STATUS_ERROR)
    {
      return ui_demo_color(0xc03a2b);
    }

  if (status == UI_DEMO_BLUETOOTH_STATUS_ADVERTISING ||
      status == UI_DEMO_BLUETOOTH_STATUS_STARTING ||
      status == UI_DEMO_BLUETOOTH_STATUS_PRESTARTING)
    {
      return ui_demo_color(0x1f5eff);
    }

  return ui_demo_color(0x606875);
}

static void ui_demo_render_bluetooth_page(void)
{
  struct ui_demo_bluetooth_snapshot snapshot;
  lv_obj_t *title;
  lv_obj_t *name;
  lv_obj_t *state;
  lv_obj_t *message;
  char name_text[96];
  const char *message_text;
  const int32_t content_w = UI_DEMO_CENTER_W - UI_DEMO_PAD * 2;

  ui_demo_bluetooth_get_snapshot(&snapshot);
  ui_demo_sync_bluetooth_function_state(&snapshot);
  g_ui.bluetooth_status_label = NULL;

  title = ui_demo_create_label(g_ui.center_panel, "蓝牙",
                               content_w, ui_demo_color(0x222832));
  lv_obj_set_pos(title, UI_DEMO_PAD, UI_DEMO_PAD + 4);

  snprintf(name_text, sizeof(name_text), "%s",
           snapshot.device_name[0] == '\0' ?
           "AIC8800-Vela" : snapshot.device_name);
  name = ui_demo_create_label(g_ui.center_panel, name_text,
                              content_w, ui_demo_color(0x606875));
  lv_obj_set_pos(name, UI_DEMO_PAD, UI_DEMO_PAD + 42);

  state = ui_demo_create_label(
    g_ui.center_panel,
    ui_demo_bluetooth_status_text(snapshot.status),
    content_w,
    ui_demo_bluetooth_status_color(snapshot.status));
  lv_obj_set_pos(state, UI_DEMO_PAD, UI_DEMO_PAD + 80);

  message_text = snapshot.message[0] == '\0' ?
                 "点击左侧蓝牙后启动 bluetoothd 并等待被动连接" :
                 snapshot.message;
  message = ui_demo_create_label(g_ui.center_panel, message_text,
                                 content_w, ui_demo_color(0x606875));
  lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(message, UI_DEMO_PAD, UI_DEMO_PAD + 116);
  g_ui.bluetooth_status_label = message;
}

static lv_color_t ui_demo_ai_status_color(enum ui_demo_ai_status status)
{
  if (status == UI_DEMO_AI_STATUS_CONNECTED)
    {
      return ui_demo_color(0x16845b);
    }

  if (status == UI_DEMO_AI_STATUS_ERROR)
    {
      return ui_demo_color(0xc03a2b);
    }

  return ui_demo_color(0x606875);
}

struct ui_demo_ai_feature_dsc
{
  const char *name;
  const char *status;
  const char *prompt;
};

static const struct ui_demo_ai_feature_dsc g_ai_features[] =
{
  {
    "智能问答",
    "运行",
    "请讲一个20字以内的中文短笑话，只输出笑话本身。"
  },
  {
    "网页搜索",
    "运行",
    "请用一句话模拟网页搜索结果：适合智能家居屏展示的一条科技新闻摘要。"
  },
  {
    "翻译摘要",
    "运行",
    "把“海内存知己，天涯若比邻”翻译成现代汉语，控制在40字以内。"
  },
};

static const char *ui_demo_ai_feature_reply(size_t index)
{
  switch (index)
    {
      case 0:
        return "程序员去面试，面试官问：你有什么优点？\n"
               "程序员说：我特别擅长把复杂问题变简单。\n"
               "面试官：举个例子？\n"
               "程序员：这个问题很简单。";
      case 1:
        return "网页搜索演示：\n"
               "智能家居正在从“能控制”走向“会理解”。\n"
               "推荐关注：端侧 AI、低功耗传感器和家庭自动化安全。";
      case 2:
        return "现代汉语：四海之内有知心朋友，即使远隔天涯也像近邻一样亲近。\n"
               "出处：《送杜少府之任蜀州》王勃。";
      default:
        return "演示内容已准备好。";
    }
}

static void ui_demo_ai_voice_event(lv_event_t *event)
{
  struct ui_demo_ai_snapshot snapshot;
  lv_obj_t *target;
  bool enabled;

  if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED)
    {
      return;
    }

  target = lv_event_get_target_obj(event);
  enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
  ui_demo_ai_set_voice_enabled(enabled);
  ui_demo_ai_get_snapshot(&snapshot);

  if (g_ui.ai_voice_label != NULL)
    {
      lv_label_set_text(g_ui.ai_voice_label, snapshot.voice_message);
    }

  ui_demo_force_refresh("ai-voice");
}

static void ui_demo_ai_feature_event(lv_event_t *event)
{
  intptr_t raw_index;
  size_t index;
  int ret;

  if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    {
      return;
    }

  raw_index = (intptr_t)lv_event_get_user_data(event);
  if (raw_index < 0)
    {
      return;
    }

  index = (size_t)raw_index;
  if (index >= sizeof(g_ai_features) / sizeof(g_ai_features[0]))
    {
      return;
    }

  ret = ui_demo_ai_send_prompt(g_ai_features[index].name,
                               g_ai_features[index].prompt);
  if (ret == -ENOSYS)
    {
      ui_demo_ai_set_turn(g_ai_features[index].name,
                          g_ai_features[index].prompt,
                          ui_demo_ai_feature_reply(index), false);
    }
  else if (ret < 0 && ret != -EINPROGRESS)
    {
      ui_demo_ai_set_turn(g_ai_features[index].name,
                          g_ai_features[index].prompt,
                          "发送失败，请查看串口日志。", false);
    }

  ui_demo_render_center();
  ui_demo_force_refresh("ai-feature");
}

static void ui_demo_render_ai_feature_row(const char *name,
                                          const char *status,
                                          uint32_t index,
                                          bool ready)
{
  lv_obj_t *row;
  lv_obj_t *name_label;
  lv_obj_t *status_label;
  const int32_t gap = 8;
  const int32_t row_w =
    (UI_DEMO_CENTER_W - UI_DEMO_PAD * 2 - gap * 2) / 3;
  const int32_t row_h = 64;
  const int32_t x = UI_DEMO_PAD + (int32_t)index * (row_w + gap);
  const int32_t y = 210;

  row = lv_obj_create(g_ui.center_panel);
  ui_demo_set_panel_base(row);
  lv_obj_set_pos(row, x, y);
  lv_obj_set_size(row, row_w, row_h);
  lv_obj_set_style_bg_color(row,
                            ready ? ui_demo_color(0xf7f8fa) :
                                    ui_demo_color(0xf1f3f5), 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(row, ui_demo_color(0xf1f3f5),
                            LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_radius(row, 8, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_border_color(row, ui_demo_color(0xe2e5e9), 0);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(row, ui_demo_ai_feature_event, LV_EVENT_CLICKED,
                      (void *)(intptr_t)index);

  name_label = ui_demo_create_label(row, name, row_w - 12,
                                    ready ? ui_demo_color(0x222832) :
                                            ui_demo_color(0x7b838e));
  lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(name_label, 6, 10);

  status_label = ui_demo_create_label(row, ready ? status : "启动",
                                      row_w - 12,
                                      ready ? ui_demo_color(0x1f5eff) :
                                              ui_demo_color(0x8c949f));
  lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(status_label, 6, 38);
}

static lv_obj_t *ui_demo_create_chat_bubble(lv_obj_t *parent,
                                            const char *text,
                                            bool from_user,
                                            int32_t x,
                                            int32_t y,
                                            int32_t w)
{
  lv_obj_t *bubble;

  bubble = lv_label_create(parent);
  lv_label_set_text(bubble, text == NULL ? "" : text);
  lv_label_set_long_mode(bubble, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(bubble, x, y);
  lv_obj_set_width(bubble, w);
  lv_obj_set_style_text_font(bubble, UI_DEMO_FONT, 0);
  lv_obj_set_style_text_color(bubble,
                              from_user ? ui_demo_color(0xffffff) :
                                          ui_demo_color(0x222832), 0);
  lv_obj_set_style_text_align(bubble, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_letter_space(bubble, 0, 0);
  lv_obj_set_style_text_line_space(bubble, 4, 0);
  lv_obj_set_style_bg_color(bubble,
                            from_user ? ui_demo_color(0x242932) :
                                        ui_demo_color(0xf1f3f5), 0);
  lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(bubble, 8, 0);
  lv_obj_set_style_border_width(bubble, 0, 0);
  lv_obj_set_style_pad_all(bubble, 10, 0);

  return bubble;
}

static void ui_demo_render_ai_conversation(
  const struct ui_demo_ai_snapshot *snapshot)
{
  lv_obj_t *panel;
  lv_obj_t *title;
  lv_obj_t *user_label;
  lv_obj_t *ai_label;
  lv_obj_t *bubble;
  const int32_t content_w = UI_DEMO_CENTER_W - UI_DEMO_PAD * 2;
  const int32_t panel_y = 292;
  const int32_t bubble_w = content_w - 56;
  const int32_t user_x = content_w - bubble_w;
  int32_t y = 0;
  const char *user_text;
  const char *reply_text;

  if (snapshot == NULL ||
      (snapshot->last_user[0] == '\0' && snapshot->last_reply[0] == '\0'))
    {
      return;
    }

  ui_demo_create_divider(g_ui.center_panel, UI_DEMO_PAD, panel_y - 10,
                         content_w, 1);

  panel = lv_obj_create(g_ui.center_panel);
  ui_demo_set_panel_base(panel);
  lv_obj_set_pos(panel, UI_DEMO_PAD, panel_y);
  lv_obj_set_size(panel, content_w,
                  UI_DEMO_SCREEN_H - panel_y - UI_DEMO_PAD);
  lv_obj_set_style_pad_all(panel, 0, 0);
  lv_obj_set_style_pad_bottom(panel, 8, 0);
  lv_obj_set_scroll_dir(panel, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  title = ui_demo_create_label(panel, "对话内容", content_w,
                               ui_demo_color(0x222832));
  lv_obj_set_pos(title, 0, y);
  y += 30;

  user_text = snapshot->last_user[0] == '\0' ? "等待输入" : snapshot->last_user;
  user_label = ui_demo_create_label(panel, "我", content_w,
                                    ui_demo_color(0x606875));
  lv_obj_set_style_text_align(user_label, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(user_label, 0, y);
  y += 20;

  bubble = ui_demo_create_chat_bubble(panel, user_text, true,
                                      user_x, y, bubble_w);
  lv_obj_update_layout(bubble);
  y += lv_obj_get_height(bubble) + 18;

  reply_text = snapshot->last_reply[0] == '\0' ?
               "等待 AI 回复..." : snapshot->last_reply;
  ai_label = ui_demo_create_label(panel, "AI", content_w,
                                  ui_demo_color(0x1f5eff));
  lv_obj_set_pos(ai_label, 0, y);
  y += 20;

  bubble = ui_demo_create_chat_bubble(panel, reply_text, false,
                                      0, y, bubble_w);
  lv_obj_update_layout(bubble);
  y += lv_obj_get_height(bubble) + 8;
  lv_obj_scroll_to_y(panel, y, LV_ANIM_OFF);
}

static void ui_demo_render_ai_page(void)
{
  struct ui_demo_ai_snapshot snapshot;
  lv_obj_t *title;
  lv_obj_t *status;
  lv_obj_t *voice_title;
  lv_obj_t *features_title;
  lv_obj_t *voice_switch;
  size_t i;
  bool ready;
  const int32_t content_w = UI_DEMO_CENTER_W - UI_DEMO_PAD * 2;

  ui_demo_ai_get_snapshot(&snapshot);
  ui_demo_sync_ai_function_state(&snapshot);

  g_ui.ai_status_label = NULL;
  g_ui.ai_voice_label = NULL;
  g_ui.ai_voice_switch = NULL;

  title = ui_demo_create_label(g_ui.center_panel, "AI",
                               content_w, ui_demo_color(0x222832));
  lv_obj_set_pos(title, UI_DEMO_PAD, UI_DEMO_PAD + 4);

  status = ui_demo_create_label(g_ui.center_panel,
                                snapshot.message[0] == '\0' ?
                                "AI 未启动" : snapshot.message,
                                content_w,
                                ui_demo_ai_status_color(snapshot.status));
  lv_obj_set_pos(status, UI_DEMO_PAD, UI_DEMO_PAD + 42);
  g_ui.ai_status_label = status;

  ui_demo_create_divider(g_ui.center_panel, UI_DEMO_PAD, 82, content_w, 1);

  voice_title = ui_demo_create_label(g_ui.center_panel, "语音 AI",
                                     content_w - 88,
                                     ui_demo_color(0x222832));
  lv_obj_set_pos(voice_title, UI_DEMO_PAD, 104);

  g_ui.ai_voice_label =
    ui_demo_create_label(g_ui.center_panel,
                         snapshot.voice_message[0] == '\0' ?
                         "语音 AI 未开启" : snapshot.voice_message,
                         content_w - 88, ui_demo_color(0x606875));
  lv_obj_set_pos(g_ui.ai_voice_label, UI_DEMO_PAD, 132);
  lv_label_set_long_mode(g_ui.ai_voice_label, LV_LABEL_LONG_WRAP);

  voice_switch = lv_switch_create(g_ui.center_panel);
  lv_obj_set_pos(voice_switch, UI_DEMO_CENTER_W - UI_DEMO_PAD - 66, 110);
  lv_obj_set_size(voice_switch, 54, 30);
  lv_obj_remove_flag(voice_switch, LV_OBJ_FLAG_SCROLLABLE);
  if (snapshot.voice_enabled)
    {
      lv_obj_add_state(voice_switch, LV_STATE_CHECKED);
    }
  else
    {
      lv_obj_remove_state(voice_switch, LV_STATE_CHECKED);
    }
  lv_obj_add_event_cb(voice_switch, ui_demo_ai_voice_event,
                      LV_EVENT_VALUE_CHANGED, NULL);
  g_ui.ai_voice_switch = voice_switch;

  ui_demo_create_divider(g_ui.center_panel, UI_DEMO_PAD, 176, content_w, 1);

  features_title = ui_demo_create_label(g_ui.center_panel, "AI 功能",
                                        content_w,
                                        ui_demo_color(0x222832));
  lv_obj_set_pos(features_title, UI_DEMO_PAD, 184);

  ready = snapshot.status == UI_DEMO_AI_STATUS_CONNECTED;
  for (i = 0; i < sizeof(g_ai_features) / sizeof(g_ai_features[0]); i++)
    {
      ui_demo_render_ai_feature_row(g_ai_features[i].name,
                                    g_ai_features[i].status,
                                    (uint32_t)i, ready);
    }

  ui_demo_render_ai_conversation(&snapshot);
}

static void ui_demo_render_placeholder(const char *title, const char *status)
{
  lv_obj_t *icon;
  lv_obj_t *title_label;
  lv_obj_t *status_label;
  lv_obj_t *row;
  const int32_t content_w = UI_DEMO_CENTER_W - UI_DEMO_PAD * 2;

  icon = ui_demo_create_label(g_ui.center_panel,
                              title == NULL ? "" : title,
                              56,
                              ui_demo_color(0x1f5eff));
  lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(icon, UI_DEMO_PAD, UI_DEMO_PAD + 8);

  title_label = ui_demo_create_label(g_ui.center_panel, title,
                                     content_w - 68,
                                     ui_demo_color(0x222832));
  lv_obj_set_pos(title_label, UI_DEMO_PAD + 68, UI_DEMO_PAD + 8);

  status_label = ui_demo_create_label(g_ui.center_panel, status,
                                      content_w - 68,
                                      ui_demo_color(0x606875));
  lv_obj_set_pos(status_label, UI_DEMO_PAD + 68, UI_DEMO_PAD + 36);

  ui_demo_create_divider(g_ui.center_panel, UI_DEMO_PAD, UI_DEMO_PAD + 72,
                         content_w, 1);

  row = lv_obj_create(g_ui.center_panel);
  ui_demo_set_panel_base(row);
  lv_obj_set_pos(row, UI_DEMO_PAD, UI_DEMO_PAD + 92);
  lv_obj_set_size(row, content_w, 64);
  lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  status_label = ui_demo_create_label(row, "状态", 96,
                                      ui_demo_color(0x222832));
  lv_obj_set_pos(status_label, 0, 10);

  status_label = ui_demo_create_label(row, "等待接入业务数据",
                                      content_w - 120,
                                      ui_demo_color(0x606875));
  lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(status_label, 120, 10);

  ui_demo_create_divider(row, 0, 63, content_w, 1);
}

static void ui_demo_render_center(void)
{
  if (g_ui.center_panel == NULL)
    {
      return;
    }

  lv_obj_clean(g_ui.center_panel);
  lv_obj_scroll_to_y(g_ui.center_panel, 0, LV_ANIM_OFF);

  if (!g_ui.has_selection)
    {
      syslog(LOG_INFO, "ui_demo: center blank no selection\n");
      ui_demo_update_center_scroll();
      return;
    }

  syslog(LOG_INFO, "ui_demo: render center function=%s\n",
         ui_demo_function_name(g_ui.selected_function));

  if (g_ui.selected_function == UI_DEMO_FUNCTION_WIFI)
    {
      ui_demo_render_wifi_list();
    }
  else if (g_ui.selected_function == UI_DEMO_FUNCTION_AI)
    {
      ui_demo_render_ai_page();
    }
  else if (g_ui.selected_function == UI_DEMO_FUNCTION_BLUETOOTH)
    {
      ui_demo_render_bluetooth_page();
    }
  else
    {
      ui_demo_render_placeholder(ui_demo_function_name(g_ui.selected_function),
                                 "功能占位，等待接入业务数据");
    }

  ui_demo_update_center_scroll();
}

static void ui_demo_store_devices(
  struct ui_demo_device_record *dst,
  size_t *dst_count,
  const struct ui_demo_wireless_device *devices,
  size_t count)
{
  size_t i;
  size_t copy_count = count;

  if (copy_count > UI_DEMO_MAX_DEVICES)
    {
      copy_count = UI_DEMO_MAX_DEVICES;
    }

  for (i = 0; i < copy_count; i++)
    {
      ui_demo_copy_text(dst[i].name, sizeof(dst[i].name), devices[i].name);
      ui_demo_copy_text(dst[i].status, sizeof(dst[i].status),
                        devices[i].status);
      dst[i].value = ui_demo_clamp_percent(devices[i].value);
    }

  *dst_count = copy_count;
}

static void ui_demo_load_mock_data(void)
{
#ifndef CONFIG_LVGL_APP_UI_DEMO_MOCK_WIRELESS_DATA
  g_ui.wifi_count = 0;
#endif
}

static void ui_demo_apply_netinfo_snapshot(
  const struct ui_demo_netinfo_snapshot *snapshot)
{
  if (snapshot == NULL)
    {
      return;
    }

  if (snapshot->time_text[0] != '\0')
    {
      ui_demo_copy_text(g_ui.time_text, sizeof(g_ui.time_text),
                        snapshot->time_text);
    }

  if (snapshot->weather_text[0] != '\0')
    {
      ui_demo_copy_text(g_ui.weather_text, sizeof(g_ui.weather_text),
                        snapshot->weather_text);
    }

  g_ui.temperature_valid = snapshot->temperature_valid;
  g_ui.humidity_valid = snapshot->humidity_valid;
  if (snapshot->temperature_valid)
    {
      g_ui.temperature_c = snapshot->temperature_c;
    }

  if (snapshot->humidity_valid)
    {
      g_ui.humidity_percent =
        ui_demo_clamp_percent(snapshot->humidity_percent);
    }

  if (g_ui.time_label != NULL)
    {
      lv_label_set_text(g_ui.time_label, g_ui.time_text);
    }

  ui_demo_refresh_environment_labels();
}

static void ui_demo_wifi_service_event(enum ui_demo_wifi_event event,
                                       void *user_data)
{
  struct ui_demo_wifi_snapshot snapshot;

  (void)user_data;

  ui_demo_wifi_get_snapshot(&snapshot);
  ui_demo_sync_wifi_function_state(&snapshot);
  ui_demo_update_function_visuals();

  if (event == UI_DEMO_WIFI_EVENT_CONNECT_FINISHED &&
      snapshot.status == UI_DEMO_WIFI_STATUS_CONNECTED)
    {
      (void)ui_demo_netinfo_refresh();
    }

  if (g_ui.has_selection &&
      g_ui.selected_function == UI_DEMO_FUNCTION_WIFI &&
      g_ui.center_panel != NULL)
    {
      if (event == UI_DEMO_WIFI_EVENT_STOP_FINISHED &&
          g_ui.wifi_restart_after_stop)
        {
          g_ui.wifi_restart_after_stop = false;
          g_ui.wifi_view = UI_DEMO_WIFI_VIEW_LIST;

          if (snapshot.status == UI_DEMO_WIFI_STATUS_OFF)
            {
              (void)ui_demo_wifi_start_scan(true);
              ui_demo_render_center();
            }
          else if (g_ui.wifi_status_label != NULL)
            {
              lv_label_set_text(g_ui.wifi_status_label, snapshot.message);
            }
        }
      else if (!snapshot.enabled &&
          !snapshot.scan_in_progress &&
          !snapshot.connect_in_progress &&
          !snapshot.stop_in_progress &&
          snapshot.status == UI_DEMO_WIFI_STATUS_OFF)
        {
          ui_demo_clear_center_selection();
        }
      else if (g_ui.wifi_view == UI_DEMO_WIFI_VIEW_CONNECT)
        {
          if (event == UI_DEMO_WIFI_EVENT_CONNECT_FINISHED &&
              ui_demo_wifi_snapshot_is_connected_to(
                &snapshot, g_ui.wifi_selected_ssid))
            {
              ui_demo_render_wifi_connect_page(g_ui.wifi_selected_ssid);
            }
          else if (g_ui.wifi_status_label != NULL)
            {
              lv_label_set_text(g_ui.wifi_status_label, snapshot.message);
            }
        }
      else
        {
          ui_demo_render_center();
        }
    }

  ui_demo_force_refresh("wifi-event");
}

static void ui_demo_bluetooth_service_event(
  enum ui_demo_bluetooth_event event,
  void *user_data)
{
  struct ui_demo_bluetooth_snapshot snapshot;

  (void)user_data;

  ui_demo_bluetooth_get_snapshot(&snapshot);
  ui_demo_sync_bluetooth_function_state(&snapshot);
  ui_demo_update_function_visuals();

  if (g_ui.has_selection &&
      g_ui.selected_function == UI_DEMO_FUNCTION_BLUETOOTH &&
      g_ui.center_panel != NULL)
    {
      if (event == UI_DEMO_BLUETOOTH_EVENT_STOP_FINISHED &&
          snapshot.status == UI_DEMO_BLUETOOTH_STATUS_OFF)
        {
          ui_demo_clear_center_selection();
          ui_demo_update_function_visuals();
        }
      else
        {
          ui_demo_render_center();
        }
    }

  ui_demo_force_refresh("bluetooth-event");
}

static void ui_demo_netinfo_service_event(enum ui_demo_netinfo_event event,
                                          void *user_data)
{
  struct ui_demo_netinfo_snapshot snapshot;

  (void)event;
  (void)user_data;

  ui_demo_netinfo_get_snapshot(&snapshot);
  ui_demo_apply_netinfo_snapshot(&snapshot);
  ui_demo_force_refresh("netinfo-event");
}

static void ui_demo_ai_service_event(enum ui_demo_ai_event event,
                                     void *user_data)
{
  struct ui_demo_ai_snapshot snapshot;

  (void)user_data;

  ui_demo_ai_get_snapshot(&snapshot);
  ui_demo_sync_ai_function_state(&snapshot);
  ui_demo_update_function_visuals();

  if (g_ui.has_selection &&
      g_ui.selected_function == UI_DEMO_FUNCTION_AI &&
      g_ui.center_panel != NULL)
    {
      if (event == UI_DEMO_AI_EVENT_STOP_FINISHED &&
          snapshot.status == UI_DEMO_AI_STATUS_OFF)
        {
          ui_demo_clear_center_selection();
          ui_demo_update_function_visuals();
        }
      else
        {
          ui_demo_render_center();
        }
    }

  ui_demo_force_refresh("ai-event");
}

void ui_demo_init(void)
{
  lv_obj_t *screen = lv_screen_active();

  memset(&g_ui, 0, sizeof(g_ui));
  g_ui.screen = screen;
  g_ui.selected_function = UI_DEMO_FUNCTION_WIFI;
  g_ui.has_selection = false;
  g_ui.wifi_view = UI_DEMO_WIFI_VIEW_LIST;
  g_ui.temperature_valid = false;
  g_ui.humidity_valid = false;
  g_ui.brightness_percent =
    ui_demo_clamp_percent(CONFIG_LVGL_APP_BACKLIGHT_DEFAULT_PERCENT);
  g_ui.volume_percent = 46;
  ui_demo_copy_text(g_ui.time_text, sizeof(g_ui.time_text), "--:--");
  ui_demo_copy_text(g_ui.weather_text, sizeof(g_ui.weather_text),
                    "天气 未获取");
  ui_demo_ai_init(ui_demo_ai_service_event, NULL);
  ui_demo_bluetooth_init(ui_demo_bluetooth_service_event, NULL);
  ui_demo_netinfo_init(ui_demo_netinfo_service_event, NULL);
  ui_demo_wifi_init(ui_demo_wifi_service_event, NULL);
  ui_demo_load_mock_data();

  if (screen == NULL)
    {
      syslog(LOG_ERR, "ui_demo: init failed, active screen is NULL\n");
      return;
    }

  syslog(LOG_INFO,
         "ui_demo: init screen=%p size=%dx%d selected=%s wifi=%ld\n",
         screen, UI_DEMO_SCREEN_W, UI_DEMO_SCREEN_H,
         g_ui.has_selection ? ui_demo_function_name(g_ui.selected_function) :
         "none",
         (long)g_ui.wifi_count);

  lv_obj_clean(screen);
  lv_obj_set_size(screen, UI_DEMO_SCREEN_W, UI_DEMO_SCREEN_H);
  lv_obj_set_style_bg_color(screen, ui_demo_color(0xf8f9fb), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_text_font(screen, UI_DEMO_FONT, 0);
  lv_obj_set_style_text_letter_space(screen, 0, 0);
  lv_obj_set_scroll_dir(screen, LV_DIR_NONE);
  lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

#ifdef CONFIG_LVGL_APP_UI_DEMO_THREE_COLUMN
  ui_demo_create_background(screen);
  ui_demo_create_left_panel(screen);
  ui_demo_create_center_panel(screen);
  ui_demo_create_right_panel(screen);
  ui_demo_update_function_visuals();
  ui_demo_render_center();
  ui_demo_force_refresh("init");
#endif

  (void)ui_demo_bluetooth_prestart();
}

void ui_demo_deinit(void)
{
  ui_demo_ai_deinit();
  ui_demo_bluetooth_deinit();
  ui_demo_netinfo_deinit();
  ui_demo_wifi_deinit();
  memset(&g_ui, 0, sizeof(g_ui));
}

void ui_demo_update_time(const char *time_text)
{
  ui_demo_copy_text(g_ui.time_text, sizeof(g_ui.time_text),
                    time_text == NULL ? "" : time_text);

  if (g_ui.time_label != NULL)
    {
      lv_label_set_text(g_ui.time_label, g_ui.time_text);
    }
}

void ui_demo_update_environment(int32_t temperature_c,
                                int32_t humidity_percent)
{
  g_ui.temperature_c = temperature_c;
  g_ui.humidity_percent = ui_demo_clamp_percent(humidity_percent);
  g_ui.temperature_valid = true;
  g_ui.humidity_valid = true;
  ui_demo_refresh_environment_labels();
}

void ui_demo_update_wifi_devices(
  const struct ui_demo_wireless_device *devices,
  size_t count)
{
  if (devices == NULL)
    {
      g_ui.wifi_count = 0;
    }
  else
    {
      ui_demo_store_devices(g_ui.wifi_devices, &g_ui.wifi_count,
                            devices, count);
    }

  if (g_ui.has_selection &&
      g_ui.selected_function == UI_DEMO_FUNCTION_WIFI)
    {
      ui_demo_render_center();
    }
}

void ui_demo_set_brightness(int32_t percent)
{
  ui_demo_apply_brightness(percent);

  if (g_ui.brightness_slider != NULL)
    {
      lv_slider_set_value(g_ui.brightness_slider, g_ui.brightness_percent,
                          LV_ANIM_OFF);
    }

  ui_demo_refresh_slider_labels();
}

void ui_demo_set_volume(int32_t percent)
{
  g_ui.volume_percent = ui_demo_clamp_percent(percent);

  if (g_ui.volume_slider != NULL)
    {
      lv_slider_set_value(g_ui.volume_slider, g_ui.volume_percent,
                          LV_ANIM_OFF);
    }

  ui_demo_refresh_slider_labels();
}
