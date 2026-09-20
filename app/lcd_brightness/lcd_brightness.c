/*
 * D133CBS LVDS panel backlight control through NuttX PWM.
 *
 * The schematic routes LCD_PWM to PE13/PWM2_A.  Each command invocation
 * opens the device only for its ioctl operations, avoiding stale file
 * descriptors when NSH starts the command as a separate task.
 */

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/timers/pwm.h>

#ifndef CONFIG_AIC_LCD_BRIGHTNESS_PWM_PATH
#  define CONFIG_AIC_LCD_BRIGHTNESS_PWM_PATH "/dev/pwm2"
#endif

#ifndef CONFIG_AIC_LCD_BRIGHTNESS_PWM_FREQUENCY
#  define CONFIG_AIC_LCD_BRIGHTNESS_PWM_FREQUENCY 1000
#endif

#ifndef CONFIG_AIC_LCD_BRIGHTNESS_PWM_CHANNEL
#  define CONFIG_AIC_LCD_BRIGHTNESS_PWM_CHANNEL 1
#endif

static void lcd_brightness_usage(void)
{
  printf("Usage: lcd_brightness <0-100>\n");
  printf("       lcd_brightness set <0-100>\n");
  printf("       lcd_brightness get\n");
}

static int lcd_brightness_open(void)
{
  int fd;

  if (CONFIG_AIC_LCD_BRIGHTNESS_PWM_PATH[0] == '\0')
    {
      return -ENODEV;
    }

  fd = open(CONFIG_AIC_LCD_BRIGHTNESS_PWM_PATH, O_RDWR);
  if (fd < 0)
    {
      return -errno;
    }

  return fd;
}

static int lcd_brightness_parse_percent(const char *text, int *percent)
{
  char *endptr;
  long value;

  if (text == NULL || percent == NULL || text[0] == '\0')
    {
      return -EINVAL;
    }

  errno = 0;
  value = strtol(text, &endptr, 10);
  if (errno != 0 || endptr == text || *endptr != '\0' ||
      value < 0 || value > 100)
    {
      return -EINVAL;
    }

  *percent = (int)value;
  return 0;
}

static uint16_t lcd_brightness_percent_to_duty(int percent)
{
  uint32_t duty;

  duty = ((uint32_t)percent * 0xffffu + 50u) / 100u;

  /* The NuttX PWM ABI documents 1/65536 as its minimum non-zero duty. */
  return (uint16_t)(duty == 0 ? 1u : duty);
}

static uint32_t lcd_brightness_get_duty(const struct pwm_info_s *info)
{
#ifdef CONFIG_PWM_MULTICHAN
  return info->channels[0].duty;
#else
  return info->duty;
#endif
}

static int lcd_brightness_set_percent(int percent)
{
  struct pwm_info_s info;
  int fd;
  int ret;
  int err;

  if (CONFIG_AIC_LCD_BRIGHTNESS_PWM_FREQUENCY <= 0)
    {
      return -EINVAL;
    }

  fd = lcd_brightness_open();
  if (fd < 0)
    {
      return fd;
    }

  memset(&info, 0, sizeof(info));
  info.frequency = CONFIG_AIC_LCD_BRIGHTNESS_PWM_FREQUENCY;

#ifdef CONFIG_PWM_MULTICHAN
  info.channels[0].channel = CONFIG_AIC_LCD_BRIGHTNESS_PWM_CHANNEL;
  info.channels[0].duty = lcd_brightness_percent_to_duty(percent);
  info.channels[0].cpol = PWM_CPOL_NDEF;
  info.channels[0].dcpol = PWM_DCPOL_NDEF;
#else
  info.duty = lcd_brightness_percent_to_duty(percent);
  info.cpol = PWM_CPOL_NDEF;
  info.dcpol = PWM_DCPOL_NDEF;
#endif

  ret = ioctl(fd, PWMIOC_SETCHARACTERISTICS,
              (unsigned long)((uintptr_t)&info));
  if (ret < 0)
    {
      err = errno;
      close(fd);
      fprintf(stderr, "lcd_brightness: set failed errno=%d (%s)\n",
              err, strerror(err));
      return -err;
    }

  ret = ioctl(fd, PWMIOC_START, 0);
  if (ret < 0)
    {
      err = errno;
      close(fd);
      fprintf(stderr, "lcd_brightness: start failed errno=%d (%s)\n",
              err, strerror(err));
      return -err;
    }

  close(fd);

  printf("lcd_brightness: %d%%, duty=%u/65535, frequency=%d Hz\n",
         percent, (unsigned int)lcd_brightness_get_duty(&info),
         CONFIG_AIC_LCD_BRIGHTNESS_PWM_FREQUENCY);
  return 0;
}

static int lcd_brightness_get_percent(void)
{
  struct pwm_info_s info;
  uint32_t duty;
  int fd;
  int ret;
  int err;

  fd = lcd_brightness_open();
  if (fd < 0)
    {
      fprintf(stderr, "lcd_brightness: open failed errno=%d (%s)\n",
              -fd, strerror(-fd));
      return fd;
    }

  memset(&info, 0, sizeof(info));
  ret = ioctl(fd, PWMIOC_GETCHARACTERISTICS,
              (unsigned long)((uintptr_t)&info));
  if (ret < 0)
    {
      err = errno;
      close(fd);
      fprintf(stderr, "lcd_brightness: get failed errno=%d (%s)\n",
              err, strerror(err));
      return -err;
    }

  close(fd);

  duty = lcd_brightness_get_duty(&info);
  printf("lcd_brightness: %u%%, duty=%u/65535, frequency=%" PRIu32 " Hz\n",
         (unsigned int)((duty * 100u + 32767u) / 65535u),
         (unsigned int)duty, (uint32_t)info.frequency);
  return 0;
}

int lcd_brightness_main(int argc, char *argv[])
{
  int percent;
  int ret;

  if (argc == 2 && strcmp(argv[1], "get") == 0)
    {
      ret = lcd_brightness_get_percent();
      return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

  if (argc == 2)
    {
      ret = lcd_brightness_parse_percent(argv[1], &percent);
    }
  else if (argc == 3 && strcmp(argv[1], "set") == 0)
    {
      ret = lcd_brightness_parse_percent(argv[2], &percent);
    }
  else
    {
      lcd_brightness_usage();
      return EXIT_FAILURE;
    }

  if (ret < 0)
    {
      lcd_brightness_usage();
      return EXIT_FAILURE;
    }

  ret = lcd_brightness_set_percent(percent);
  return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
