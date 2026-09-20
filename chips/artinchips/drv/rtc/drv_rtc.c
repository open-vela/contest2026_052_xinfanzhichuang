/*
 * Copyright (c) 2022-2023, ArtInChip Technology Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Authors: matteo <duanmt@artinchip.com>
 */

#include <string.h>
#include <time.h>

#include <drv/drv_rtc.h>

#include "aic_core.h"
#include "hal_rtc.h"

#define AIC_RTC_NAME "aic-rtc"

#undef pr_debug
#ifdef AIC_RTC_DRV_DEBUG
#define pr_debug pr_info
#else
#define pr_debug(fmt, ...)
#endif

static char g_rtc_tz;
static struct tm g_local_tm;
static char g_local_time_str[32];

static int rtc_is_leap(int year) {
  return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

static time_t rtc_timegm(struct tm *const t) {
  int year;
  int mon;
  int64_t days = 0;
  int64_t sec;

  if (t == NULL)
    return (time_t)-1;

  if (t->tm_mon < 0 || t->tm_mon > 11)
    return (time_t)-1;
  if (t->tm_mday < 1 || t->tm_mday > 31)
    return (time_t)-1;
  if (t->tm_hour < 0 || t->tm_hour > 23)
    return (time_t)-1;
  if (t->tm_min < 0 || t->tm_min > 59)
    return (time_t)-1;
  if (t->tm_sec < 0 || t->tm_sec > 59)
    return (time_t)-1;

  year = t->tm_year + 1900;
  mon = t->tm_mon;

  if (year >= 1970) {
    int y;
    for (y = 1970; y < year; y++)
      days += rtc_is_leap(y) ? 366 : 365;
  } else {
    int y;
    for (y = 1969; y >= year; y--)
      days -= rtc_is_leap(y) ? 366 : 365;
  }

  days += __spm[mon];
  if (rtc_is_leap(year) && mon >= 2)
    days += 1;
  days += t->tm_mday - 1;

  sec = days * SPD + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
  return (time_t)sec;
}

struct tm *gmtime_bare(const time_t *timep, struct tm *r) {
  int64_t sec;
  int64_t days;
  int64_t rem;
  int year = 1970;
  int mon;
  int leap;
  int yday;

  if (timep == NULL || r == NULL)
    return NULL;

  sec = (int64_t)(*timep);
  days = sec / SPD;
  rem = sec % SPD;

  if (rem < 0) {
    rem += SPD;
    days -= 1;
  }

  r->tm_hour = (int)(rem / 3600);
  rem %= 3600;
  r->tm_min = (int)(rem / 60);
  r->tm_sec = (int)(rem % 60);

  while (days < 0 || days >= (rtc_is_leap(year) ? 366 : 365)) {
    int ydays = rtc_is_leap(year) ? 366 : 365;
    if (days < 0) {
      year--;
      days += rtc_is_leap(year) ? 366 : 365;
    } else {
      days -= ydays;
      year++;
    }
  }

  yday = (int)days;
  leap = rtc_is_leap(year);

  for (mon = 11; mon > 0; mon--) {
    int mdays = __spm[mon] + ((leap && mon >= 2) ? 1 : 0);
    if (yday >= mdays)
      break;
  }

  r->tm_year = year - 1900;
  r->tm_mon = mon;
  r->tm_mday = yday - (__spm[mon] + ((leap && mon >= 2) ? 1 : 0)) + 1;
  r->tm_yday = yday;
  r->tm_wday = (int)(((days + 4) % 7 + 7) % 7);
  r->tm_isdst = 0;

  return r;
}

void tz_set(char tz) { g_rtc_tz = tz; }

char tz_get(void) { return g_rtc_tz; }

static int rtc_alarm_event(void) {
  pr_debug("alarm interrupt\n");
  return 0;
}

int rtc_get_secs(time_t *sec) {
  struct tm tm;

  if (sec == NULL)
    return RTC_ERR;

  hal_rtc_read_time((u32 *)sec);
  memset(&tm, 0, sizeof(tm));
  if (gmtime_bare(sec, &tm))
    pr_debug("Get RTC time: %04d-%02d-%02d %02d:%02d:%02d\n", tm.tm_year + 1900,
             tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return RTC_OK;
}

int rtc_set_secs(time_t *sec) {
  struct tm tm;

  if (sec == NULL)
    return RTC_ERR;

  memset(&tm, 0, sizeof(tm));
  if (gmtime_bare(sec, &tm) == NULL)
    return RTC_ERR;
  if (tm.tm_year < 100)
    return RTC_ERR;

  hal_rtc_set_time((u32)(*sec));
  pr_debug("Set RTC time: %04d-%02d-%02d %02d:%02d:%02d\n", tm.tm_year + 1900,
           tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return RTC_OK;
}

int rtc_get_alarm(struct rtc_wkalarm *alarm) {
  time_t alarm_sec = 0;
  struct tm tm;

  if (alarm == NULL)
    return RTC_ERR;

  memset(&tm, 0, sizeof(tm));
  alarm->enable = hal_rtc_read_alarm((u32 *)&alarm_sec);
  if (gmtime_bare(&alarm_sec, &tm) == NULL)
    return RTC_ERR;

  alarm->tm_sec = tm.tm_sec;
  alarm->tm_min = tm.tm_min;
  alarm->tm_hour = tm.tm_hour;
  pr_debug("Get alarm time: %04d-%02d-%02d %02d:%02d:%02d\n", tm.tm_year + 1900,
           tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return RTC_OK;
}

int rtc_set_alarm(struct rtc_wkalarm *alarm) {
  time_t cur_sec = 0;
  struct tm tm;
  time_t alarm_sec;

  if (alarm == NULL)
    return RTC_ERR;
  if (!alarm->enable) {
    pr_debug("Need not enable alarm\n");
    return RTC_OK;
  }

  if (rtc_get_secs(&cur_sec) != RTC_OK)
    return RTC_ERR;

  memset(&tm, 0, sizeof(tm));
  if (gmtime_bare(&cur_sec, &tm) == NULL)
    return RTC_ERR;

  tm.tm_hour = alarm->tm_hour;
  tm.tm_min = alarm->tm_min;
  tm.tm_sec = alarm->tm_sec;
  alarm_sec = rtc_timegm(&tm);
  if (alarm_sec <= cur_sec)
    alarm_sec += SPD;

  hal_rtc_register_callback(rtc_alarm_event);
  hal_rtc_set_alarm((u32)alarm_sec);
  if (gmtime_bare(&alarm_sec, &tm))
    pr_debug("Set a alarm(%d): %04d-%02d-%02d %02d:%02d:%02d\n", alarm->enable,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
             tm.tm_min, tm.tm_sec);
  return RTC_OK;
}

struct tm *get_localtime(void) {
  time_t sec = 0;

  if (rtc_get_secs(&sec) != RTC_OK)
    return NULL;

  sec += (time_t)g_rtc_tz * 3600;
  if (gmtime_bare(&sec, &g_local_tm) == NULL)
    return NULL;

  return &g_local_tm;
}

char *local_time(void) {
  struct tm *tm = get_localtime();

  if (tm == NULL)
    return NULL;

  snprintf(g_local_time_str, sizeof(g_local_time_str),
           "%04d-%02d-%02d %02d:%02d:%02d", tm->tm_year + 1900, tm->tm_mon + 1,
           tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
  return g_local_time_str;
}

int drv_rtc_init(void) {
  int ret;

  ret = hal_rtc_init();
  if (ret < 0) {
    pr_err("RTC init failed\n");
    return RTC_ERR;
  }

#ifdef RTC_IRQn
  ret = aicos_request_irq(RTC_IRQn, hal_rtc_irq, 0, AIC_RTC_NAME, NULL);
  if (ret < 0)
    pr_warn("RTC request irq failed: %d\n", ret);
  else
    aicos_irq_enable(RTC_IRQn);
#endif

#ifdef AIC_RTC_CLK_RATE
  hal_rtc_cali(AIC_RTC_CLK_RATE);
#endif

#if defined(AIC_RTC_ALARM_IO_OUTPUT)
  hal_rtc_alarm_io_output();
#elif defined(AIC_RTC_32K_IO_OUTPUT)
  hal_rtc_32k_clk_output();
#endif

  pr_info("ArtInChip RTC loaded\n");
  return RTC_OK;
}