#include "app_main/lvgl_app_backlight.h"
#include "config/lvgl_app_config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/config.h>

#ifdef CONFIG_PWM
#  include <nuttx/timers/pwm.h>
#endif

#define LVGL_APP_BACKLIGHT_TAG "backlight"

static int g_backlight_fd = -1;
static bool g_backlight_applied;
static bool g_backlight_warned;
static int32_t g_backlight_percent = -1;

static int32_t lvgl_app_backlight_clamp(int32_t percent)
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

#ifdef CONFIG_PWM
static int lvgl_app_backlight_open(void)
{
  if (g_backlight_fd >= 0)
    {
      return 0;
    }

  if (CONFIG_LVGL_APP_BACKLIGHT_PWM_PATH[0] == '\0')
    {
      return -ENODEV;
    }

  g_backlight_fd = open(CONFIG_LVGL_APP_BACKLIGHT_PWM_PATH, O_RDWR);
  if (g_backlight_fd < 0)
    {
      if (!g_backlight_warned)
        {
          syslog(LOG_WARNING, "%s: open %s failed errno=%d\n",
                 LVGL_APP_BACKLIGHT_TAG,
                 CONFIG_LVGL_APP_BACKLIGHT_PWM_PATH, errno);
          g_backlight_warned = true;
        }

      return -errno;
    }

  g_backlight_warned = false;
  return 0;
}
#endif

int lvgl_app_backlight_set_percent(int32_t percent)
{
#ifdef CONFIG_PWM
  struct pwm_info_s info;
  uint32_t duty;
  bool start_pwm;
  int ret;

  percent = lvgl_app_backlight_clamp(percent);
  if (g_backlight_applied && g_backlight_percent == percent)
    {
      return 0;
    }

  ret = lvgl_app_backlight_open();
  if (ret < 0)
    {
      return ret;
    }

  if (CONFIG_LVGL_APP_BACKLIGHT_PWM_FREQUENCY <= 0)
    {
      if (!g_backlight_warned)
        {
          syslog(LOG_WARNING, "%s: invalid pwm frequency %d\n",
                 LVGL_APP_BACKLIGHT_TAG,
                 CONFIG_LVGL_APP_BACKLIGHT_PWM_FREQUENCY);
          g_backlight_warned = true;
        }

      return -EINVAL;
    }

  start_pwm = !g_backlight_applied;
  memset(&info, 0, sizeof(info));
  info.frequency = CONFIG_LVGL_APP_BACKLIGHT_PWM_FREQUENCY;

#  ifdef CONFIG_PWM_MULTICHAN
  info.channels[0].channel = CONFIG_LVGL_APP_BACKLIGHT_PWM_CHANNEL;
  duty = (percent * 0xffffu + 50u) / 100u;
  info.channels[0].duty = duty == 0 ? 1 : duty;
  info.channels[0].cpol = PWM_CPOL_NDEF;
  info.channels[0].dcpol = PWM_DCPOL_NDEF;
#  else
  duty = (percent * 0xffffu + 50u) / 100u;
  info.duty = duty == 0 ? 1 : duty;
  info.cpol = PWM_CPOL_NDEF;
  info.dcpol = PWM_DCPOL_NDEF;
#  endif

  ret = ioctl(g_backlight_fd, PWMIOC_SETCHARACTERISTICS,
              (unsigned long)((uintptr_t)&info));
  if (ret < 0)
    {
      syslog(LOG_WARNING, "%s: set pwm failed errno=%d\n",
             LVGL_APP_BACKLIGHT_TAG, errno);
      return -errno;
    }

  if (start_pwm)
    {
      ret = ioctl(g_backlight_fd, PWMIOC_START, 0);
      if (ret < 0)
        {
          syslog(LOG_WARNING, "%s: start pwm failed errno=%d\n",
                 LVGL_APP_BACKLIGHT_TAG, errno);
          return -errno;
        }
    }

  g_backlight_applied = true;
  g_backlight_percent = percent;
  return 0;
#else
  (void)percent;
  return -ENODEV;
#endif
}

void lvgl_app_backlight_shutdown(void)
{
#ifdef CONFIG_PWM
  int ret;

  if (g_backlight_fd >= 0)
    {
      if (g_backlight_applied)
        {
          ret = ioctl(g_backlight_fd, PWMIOC_STOP, 0);
          if (ret < 0)
            {
              syslog(LOG_WARNING, "%s: stop pwm failed errno=%d\n",
                     LVGL_APP_BACKLIGHT_TAG, errno);
            }
        }

      close(g_backlight_fd);
      g_backlight_fd = -1;
    }

  g_backlight_applied = false;
  g_backlight_percent = -1;
#endif
}
