#include <nuttx/config.h>

#ifdef CONFIG_RTC

#include <errno.h>
#include <stdbool.h>
#include <time.h>
#include <nuttx/arch.h>
#include <drv/drv_rtc.h>

volatile bool g_rtc_enabled = false;

int up_rtc_initialize(void) {
  int ret;

  ret = drv_rtc_init();
  if (ret != RTC_OK) {
    g_rtc_enabled = false;
    return -EIO;
  }

  g_rtc_enabled = true;
  return 0;
}

#if !defined(CONFIG_RTC_HIRES)
time_t up_rtc_time(void) {
  time_t sec = 0;

  if (rtc_get_secs(&sec) != RTC_OK) {
    return 0;
  }

  return sec;
}
#endif

int up_rtc_settime(FAR const struct timespec *tp) {
  time_t sec;

  if (tp == NULL) {
    return -EINVAL;
  }

  sec = tp->tv_sec;
  if (rtc_set_secs(&sec) != RTC_OK) {
    return -EIO;
  }

  return 0;
}

#endif
