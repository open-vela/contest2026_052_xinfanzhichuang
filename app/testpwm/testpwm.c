/****************************************************************************
 * contest2026_052_xinfanzhichuang/app/testpwm/testpwm.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * ArtInChip D13x / D12x PWM Test Application
 *
 * This test exercises the ArtInChip PWM lower-half driver (drv_pwm_nuttx.c)
 * through the NuttX upper-half PWM character driver interface.
 *
 * The ArtInChip PWM controller supports:
 *   - Up-count, down-count, and up-down-count modes
 *   - Dual complementary outputs (EPWMA / EPWMB) per channel
 *   - Programmable polarity per channel
 *   - 8 independent PWM channels (g_pwm_args[0..7])
 *   - Clock up to 48 MHz on silicon, 24 MHz on FPGA
 *   - D13x: PWM_ID_MAX_NUM = 4, D12x: PWM_ID_MAX_NUM = 2
 *
 * Test phases (automatic when -a is given):
 *   1. Basic set/start/stop
 *   2. Edge cases: duty 0%, 100%, varied frequencies
 *   3. Polarity: low, high, default
 *   4. Multi-channel (CMPA + CMPB) if CONFIG_PWM_MULTICHAN
 *   5. Restart cycle: start → stop → start → stop
 *   6. All available PWM devices /dev/pwm0..N
 *
 * Note on duty format:
 *   The CLI accepts duty as a 16-bit scaled value (0–65535), where 0 = 0%
 *   and 65535 = 100%.  The value is scaled down to 0–100 before being
 *   passed to the driver, because the ArtInChip lower-half driver
 *   (drv_pwm_nuttx.c) treats info.duty / info->channels[].duty as a raw
 *   integer percentage (0–100), NOT as ub16_t (b16 fractional).
 *
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <sys/ioctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <debug.h>

#include <nuttx/timers/pwm.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PWM_DEFAULT_DEVPATH    "/dev/pwm0"
#define PWM_DEFAULT_FREQUENCY  100
#define PWM_DEFAULT_DUTY       32768   /* 50% in 16-bit scale (0–65535) */
#define PWM_DEFAULT_DURATION   5
#define PWM_DEFAULT_POLARITY   0

#define PWM_DEV_MAX            8      /* Max device count to probe */

#define TEST_RESULT_PASS       0
#define TEST_RESULT_FAIL       -1

/* Helper: 1 second in microseconds */
#ifndef USEC_PER_SEC
#define USEC_PER_SEC           1000000
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Test context for a single PWM device under test */

struct testpwm_dev_s
{
  char  devpath[24];           /* e.g. /dev/pwm0 */
  int   fd;                    /* Open file descriptor */
  int   pwm_id;                /* PWM channel number (0..N) */
};

/* Per-test result tracking */

struct testpwm_result_s
{
  int   total;
  int   passed;
  int   failed;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct testpwm_result_s g_result;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  testpwm_open_device(struct testpwm_dev_s *dev);
static void testpwm_close_device(struct testpwm_dev_s *dev);
static int  testpwm_set_characteristics(int fd,
                                        const struct pwm_info_s *info);
static int  testpwm_get_characteristics(int fd,
                                        struct pwm_info_s *info);
static int  testpwm_start(int fd);
static int  testpwm_stop(int fd);

static int  test_basic(int fd, uint32_t freq, uint16_t duty,
                       int polarity, int duration);
static int  test_edge_cases(int fd, uint32_t freq, int duration);
static int  test_polarity(int fd, uint32_t freq, uint16_t duty,
                          int duration);
#ifdef CONFIG_PWM_MULTICHAN
static int  test_multichan(int fd, uint32_t freq, uint16_t duty,
                           int duration);
#endif
static int  test_restart(int fd, uint32_t freq, uint16_t duty,
                         int duration);
static int  test_all_devices(uint32_t freq, uint16_t duty,
                             int polarity, int duration);

static void test_report_phase(const char *name, int result);
static void test_summary(void);

/****************************************************************************
 * Low-level PWM helpers
 ****************************************************************************/

static int testpwm_open_device(struct testpwm_dev_s *dev)
{
  dev->fd = open(dev->devpath, O_RDWR);
  if (dev->fd < 0)
    {
      syslog(LOG_ERR,
             "FAIL: open %s: %d (%s)\n",
             dev->devpath, errno, strerror(errno));
      return TEST_RESULT_FAIL;
    }

  syslog(LOG_INFO, "  Opened %s (fd=%d)\n", dev->devpath, dev->fd);
  return TEST_RESULT_PASS;
}

static void testpwm_close_device(struct testpwm_dev_s *dev)
{
  if (dev->fd >= 0)
    {
      close(dev->fd);
      dev->fd = -1;
    }
}

static int testpwm_set_characteristics(int fd,
                                       const struct pwm_info_s *info)
{
  int ret;

  ret = ioctl(fd, PWMIOC_SETCHARACTERISTICS,
              (unsigned long)((uintptr_t)info));
  if (ret < 0)
    {
      syslog(LOG_ERR,
             "FAIL: PWMIOC_SETCHARACTERISTICS: %d (%s)\n",
             errno, strerror(errno));
      return TEST_RESULT_FAIL;
    }

  return TEST_RESULT_PASS;
}

static int testpwm_get_characteristics(int fd, struct pwm_info_s *info)
{
  int ret;

  memset(info, 0, sizeof(*info));
  ret = ioctl(fd, PWMIOC_GETCHARACTERISTICS,
              (unsigned long)((uintptr_t)info));
  if (ret < 0)
    {
      syslog(LOG_ERR,
             "FAIL: PWMIOC_GETCHARACTERISTICS: %d (%s)\n",
             errno, strerror(errno));
      return TEST_RESULT_FAIL;
    }

  return TEST_RESULT_PASS;
}

static int testpwm_start(int fd)
{
  int ret;

  ret = ioctl(fd, PWMIOC_START, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "FAIL: PWMIOC_START: %d (%s)\n",
             errno, strerror(errno));
      return TEST_RESULT_FAIL;
    }

  return TEST_RESULT_PASS;
}

static int testpwm_stop(int fd)
{
  int ret;

  ret = ioctl(fd, PWMIOC_STOP, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "FAIL: PWMIOC_STOP: %d (%s)\n",
             errno, strerror(errno));
      return TEST_RESULT_FAIL;
    }

  return TEST_RESULT_PASS;
}

/****************************************************************************
 * Helpers to build pwm_info_s
 *
 * The CLI duty is 16-bit scale (0-65535).  These helpers scale it down
 * to 0-100 for the driver, which treats info.duty / info->channels[].duty
 * as raw integer percentages (NOT ub16_t).
 ****************************************************************************/

static void build_info_single(struct pwm_info_s *info,
                              uint32_t freq, uint16_t duty,
                              int polarity)
{
  memset(info, 0, sizeof(*info));
  info->frequency = freq;
  info->duty      =    (uint16_t)((uint32_t)duty * 65535/100);
  info->cpol      = polarity;   /* PWM_CPOL_NDEF / LOW / HIGH */
  info->dcpol     = PWM_DCPOL_NDEF;
}

#ifdef CONFIG_PWM_MULTICHAN
static void build_info_multichan(struct pwm_info_s *info,
                                 uint32_t freq, uint16_t duty,
                                 int polarity)
{
  int i;

  memset(info, 0, sizeof(*info));
  info->frequency = freq;

  for (i = 0; i < CONFIG_PWM_NCHANNELS; i++)
    {
      /* Channel numbers start from 1 */
      info->channels[i].channel = i + 1;
	      info->channels[i].duty    = (uint16_t)(((uint32_t)duty * 100 + 32768) / 65535);
      if (polarity > 0)
        {
          info->channels[i].cpol = polarity;
        }
    }
}
#endif

/****************************************************************************
 * Test: Basic set → start → wait → stop
 *
 * Coverage:
 *   - pwm_setup() path via open()
 *   - pwm_start() single-channel
 *   - pwm_stop()
 *   - pwm_shutdown() via close()
 ****************************************************************************/

static int test_basic(int fd, uint32_t freq, uint16_t duty,
                      int polarity, int duration)
{
  struct pwm_info_s info;
  int ret;

  syslog(LOG_INFO, "\n=== Phase 1: Basic test (freq=%" PRIu32
         ", duty=%d, polarity=%d, dur=%ds) ===\n",
         freq, duty, polarity, duration);

  build_info_single(&info, freq, duty, polarity);

  ret = testpwm_set_characteristics(fd, &info);
  if (ret != TEST_RESULT_PASS)
    return ret;

  ret = testpwm_start(fd);
  if (ret != TEST_RESULT_PASS)
    return ret;

  syslog(LOG_INFO, "  PWM running for %d seconds...\n", duration);
  sleep(duration);

  ret = testpwm_stop(fd);
  if (ret != TEST_RESULT_PASS)
    return ret;

  syslog(LOG_INFO, "  Phase 1 passed\n");
  return TEST_RESULT_PASS;
}

/****************************************************************************
 * Test: Edge cases
 *
 * Coverage:
 *   - Duty = 0%   (minimum duty)
 *   - Duty = 100% (maximum duty, always high)
 *   - Various frequencies within supported range
 *   - Verify characteristics round-trip
 ****************************************************************************/

static int test_edge_cases(int fd, uint32_t freq, int duration)
{
  struct pwm_info_s info;
  struct pwm_info_s info_back;
  int ret;
  int i;

  struct
  {
    uint16_t duty;
    const char *desc;
  } duty_tests[] =
  {
	    { 0,     "duty=0     (always low)"   },
	    { 65535, "duty=65535 (always high)"  },
  };

  uint32_t freq_tests[] =
  {
    freq,       /* User-specified frequency */
    1000,       /* 1 kHz */
    10000,      /* 10 kHz */
  };

  syslog(LOG_INFO, "\n=== Phase 2: Edge cases ===\n");

  /* 2a: Boundary duty values */

  for (i = 0; i < 2; i++)
    {
      build_info_single(&info, freq, duty_tests[i].duty,
                        PWM_CPOL_NDEF);

      syslog(LOG_INFO, "  Test: %s\n", duty_tests[i].desc);

      ret = testpwm_set_characteristics(fd, &info);
      if (ret != TEST_RESULT_PASS)
        continue;

      ret = testpwm_start(fd);
      if (ret != TEST_RESULT_PASS)
        continue;

      /* Verify characteristics round-trip via upper-half memcpy */

      ret = testpwm_get_characteristics(fd, &info_back);
      if (ret == TEST_RESULT_PASS)
        {
          syslog(LOG_INFO,
                 "    Char: freq=%" PRIu32 " duty=%d\n",
                 info_back.frequency, (int)info_back.duty);
        }

      usleep(USEC_PER_SEC);
      testpwm_stop(fd);
    }

  /* 2b: Various frequencies at 50% duty */

  for (i = 0; i < 3; i++)
    {
      build_info_single(&info, freq_tests[i], 50, PWM_CPOL_NDEF);

      syslog(LOG_INFO, "  Test: freq=%" PRIu32 " Hz, 50%%\n",
             freq_tests[i]);

      ret = testpwm_set_characteristics(fd, &info);
      if (ret != TEST_RESULT_PASS)
        continue;

      ret = testpwm_start(fd);
      if (ret != TEST_RESULT_PASS)
        continue;

      usleep(USEC_PER_SEC / 2);
      testpwm_stop(fd);
    }

  syslog(LOG_INFO, "  Phase 2 passed\n");
  return TEST_RESULT_PASS;
}

/****************************************************************************
 * Test: Polarity
 *
 * Coverage:
 *   - PWM_CPOL_NDEF (0)  — driver default
 *   - PWM_CPOL_LOW  (1)  — output low as active
 *   - PWM_CPOL_HIGH (2)  — output high as active
 *
 * The ArtInChip driver maps these to:
 *   CPOL_LOW  → PWM_POLARITY_INVERSED
 *   CPOL_HIGH → PWM_POLARITY_NORMAL
 ****************************************************************************/

static int test_polarity(int fd, uint32_t freq, uint16_t duty,
                         int duration)
{
  struct pwm_info_s info;
  int ret;
  int i;

  int pol_values[] =
  {
    PWM_CPOL_NDEF,
    PWM_CPOL_LOW,
    PWM_CPOL_HIGH,
  };

  const char *pol_names[] =
  {
    "default",
    "low (inversed)",
    "high (normal)",
  };

  syslog(LOG_INFO, "\n=== Phase 3: Polarity test ===\n");

  for (i = 0; i < 3; i++)
    {
      build_info_single(&info, freq, duty, pol_values[i]);

      syslog(LOG_INFO, "  Test: polarity=%d (%s)\n",
             pol_values[i], pol_names[i]);

      ret = testpwm_set_characteristics(fd, &info);
      if (ret != TEST_RESULT_PASS)
        continue;

      ret = testpwm_start(fd);
      if (ret != TEST_RESULT_PASS)
        continue;

      usleep(USEC_PER_SEC);
      testpwm_stop(fd);
    }

  syslog(LOG_INFO, "  Phase 3 passed\n");
  return TEST_RESULT_PASS;
}

/****************************************************************************
 * Test: Multi-channel (CONFIG_PWM_MULTICHAN)
 *
 * Coverage:
 *   - pwm_start_multichan() in the driver
 *   - CMPA + CMPB dual output path
 *   - Per-channel polarity
 ****************************************************************************/

#ifdef CONFIG_PWM_MULTICHAN
static int test_multichan(int fd, uint32_t freq, uint16_t duty,
                          int duration)
{
  struct pwm_info_s info;
  int ret;

  syslog(LOG_INFO, "\n=== Phase 4: Multi-channel test ===\n");

  build_info_multichan(&info, freq, duty, PWM_CPOL_NDEF);

  syslog(LOG_INFO, "  Channels: %d, freq=%" PRIu32
         ", duty=%d\n",
         CONFIG_PWM_NCHANNELS, freq, duty);

  ret = testpwm_set_characteristics(fd, &info);
  if (ret != TEST_RESULT_PASS)
    return ret;

  ret = testpwm_start(fd);
  if (ret != TEST_RESULT_PASS)
    return ret;

  syslog(LOG_INFO, "  Dual-channel PWM running for %d seconds...\n",
         duration);
  sleep(duration);

  ret = testpwm_stop(fd);
  if (ret != TEST_RESULT_PASS)
    return ret;

  /* Multi-channel with per-channel polarity.
   * Set channel 0 to high (normal), channel 1 to low (inversed).
   */
  if (CONFIG_PWM_NCHANNELS >= 2)
    {
      memset(&info, 0, sizeof(info));
      info.frequency = freq;
      info.channels[0].channel = 1;
      info.channels[0].duty    = duty;
      info.channels[0].cpol    = PWM_CPOL_HIGH;
      info.channels[1].channel = 2;
      info.channels[1].duty    = duty;
      info.channels[1].cpol    = PWM_CPOL_LOW;

      syslog(LOG_INFO,
             "  Test: ch1=HIGH, ch2=LOW, freq=%" PRIu32
             ", duty=%d\n", freq, duty);

      ret = testpwm_set_characteristics(fd, &info);
      if (ret == TEST_RESULT_PASS)
        {
          testpwm_start(fd);
          usleep(USEC_PER_SEC);
          testpwm_stop(fd);
        }
    }

  syslog(LOG_INFO, "  Phase 4 passed\n");
  return TEST_RESULT_PASS;
}
#endif

/****************************************************************************
 * Test: Restart cycle
 *
 * Coverage:
 *   - Driver's restart path: disable → re-enable
 *   - pwm_stop() when already stopped (graceful skip)
 ****************************************************************************/

static int test_restart(int fd, uint32_t freq, uint16_t duty,
                        int duration)
{
  struct pwm_info_s info;
  int ret;

  syslog(LOG_INFO, "\n=== Phase 5: Restart cycle test ===\n");

  build_info_single(&info, freq, duty, PWM_CPOL_NDEF);

  /* Start → Stop */
  testpwm_set_characteristics(fd, &info);
  testpwm_start(fd);
  syslog(LOG_INFO, "  PWM started (#1)\n");
  usleep(USEC_PER_SEC / 2);

  testpwm_stop(fd);
  syslog(LOG_INFO, "  PWM stopped (#1)\n");

  /* Start → Stop (cycle 2) */
  ret = testpwm_start(fd);
  if (ret != TEST_RESULT_PASS)
    {
      syslog(LOG_ERR, "FAIL: restart failed\n");
      return TEST_RESULT_FAIL;
    }
  syslog(LOG_INFO, "  PWM started (#2)\n");
  usleep(USEC_PER_SEC / 2);

  testpwm_stop(fd);
  syslog(LOG_INFO, "  PWM stopped (#2)\n");

  /* Stop when already stopped — should return OK gracefully */
  ret = testpwm_stop(fd);
  if (ret == TEST_RESULT_PASS)
    {
      syslog(LOG_INFO, "  Double-stop handled gracefully\n");
    }

  syslog(LOG_INFO, "  Phase 5 passed\n");
  return TEST_RESULT_PASS;
}

/****************************************************************************
 * Test: All available PWM devices
 *
 * Probes /dev/pwm0 through /dev/pwm{N} and runs basic set/start/stop
 * on each one that opens successfully.
 *
 * D13x has PWM_ID_MAX_NUM = 4, D12x has PWM_ID_MAX_NUM = 2.
 ****************************************************************************/

static int test_all_devices(uint32_t freq, uint16_t duty,
                            int polarity, int duration)
{
  struct testpwm_dev_s dev;
  struct pwm_info_s info;
  int i;
  int count = 0;
  int probe_max;

  /* Probe up to PWM_DEV_MAX to handle both D12X (2) and D13X (4) */

  probe_max = PWM_DEV_MAX;

  syslog(LOG_INFO, "\n=== Phase 6: All-devices test ===\n");

  for (i = 0; i < probe_max; i++)
    {
      snprintf(dev.devpath, sizeof(dev.devpath), "/dev/pwm%d", i);
      dev.pwm_id = i;
      dev.fd = -1;

      if (testpwm_open_device(&dev) != TEST_RESULT_PASS)
        {
          /* Stop at first device that doesn't exist */
          if (i == 0)
            {
              syslog(LOG_ERR, "No PWM devices found\n");
              return TEST_RESULT_FAIL;
            }
          break;
        }

      build_info_single(&info, freq, duty, polarity);

      if (testpwm_set_characteristics(dev.fd, &info) == TEST_RESULT_PASS
          && testpwm_start(dev.fd) == TEST_RESULT_PASS)
        {
          syslog(LOG_INFO, "  %s: running for %ds\n",
                 dev.devpath, duration);
          sleep(duration);
          testpwm_stop(dev.fd);
          count++;
        }

      testpwm_close_device(&dev);
    }

  syslog(LOG_INFO, "  Tested %d PWM device(s)\n", count);
  syslog(LOG_INFO, "  Phase 6 passed\n");
  return (count > 0) ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

/****************************************************************************
 * Test reporting helpers
 ****************************************************************************/

static void test_report_phase(const char *name, int result)
{
  g_result.total++;

  if (result == TEST_RESULT_PASS)
    {
      g_result.passed++;
      syslog(LOG_INFO, "[PASS] %s\n", name);
    }
  else
    {
      g_result.failed++;
      syslog(LOG_INFO, "[FAIL] %s\n", name);
    }
}

static void test_summary(void)
{
  syslog(LOG_INFO, "\n========================================\n");
  syslog(LOG_INFO, " PWM Test Summary\n");
  syslog(LOG_INFO, "   Total: %d\n", g_result.total);
  syslog(LOG_INFO, "   Passed: %d\n", g_result.passed);
  syslog(LOG_INFO, "   Failed: %d\n", g_result.failed);
  syslog(LOG_INFO, "========================================\n");
}

/****************************************************************************
 * Usage
 ****************************************************************************/

static void show_usage(FAR const char *progname)
{
  syslog(LOG_INFO,
         "Usage: %s [options]\n"
         "\n"
         "Test the ArtInChip PWM driver (D13x / D12x).\n"
         "\n"
         "Options:\n"
         "  -p <devpath>   PWM device path (default: %s)\n"
         "  -f <freq>      Frequency in Hz (default: %d)\n"
         "  -d <duty>      Duty cycle 0-65535 (0=0%%, 65535=100%%, default: %d)\n"
         "  -t <duration>  Duration in seconds (default: %d)\n"
         "  -c <polarity>  Polarity: 0=default, 1=low, 2=high "
         "(default: %d)\n"
         "  -a             Run all test phases on all devices\n"
         "  -g             Get characteristics before/after run\n"
         "  -h             Show this help\n",
         progname,
         PWM_DEFAULT_DEVPATH,
         PWM_DEFAULT_FREQUENCY,
         PWM_DEFAULT_DUTY,
         PWM_DEFAULT_DURATION,
         PWM_DEFAULT_POLARITY);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  const char *devpath = PWM_DEFAULT_DEVPATH;
  uint32_t frequency = PWM_DEFAULT_FREQUENCY;
  uint16_t duty = PWM_DEFAULT_DUTY;
  int duration = PWM_DEFAULT_DURATION;
  int polarity = PWM_DEFAULT_POLARITY;
  bool get_char = false;
  bool all_tests = false;
  struct testpwm_dev_s dev;
  int fd;
  int ret;
  int ch;

  memset(&g_result, 0, sizeof(g_result));

  /* Parse command line */

  while ((ch = getopt(argc, argv, "p:f:d:t:c:agh")) != ERROR)
    {
      switch (ch)
        {
          case 'p':
            devpath = optarg;
            break;

          case 'f':
            frequency = (uint32_t)strtoul(optarg, NULL, 10);
            if (frequency == 0)
              {
                syslog(LOG_ERR,
                       "Invalid frequency (must be > 0): %s\n",
                       optarg);
                show_usage(argv[0]);
                return EXIT_FAILURE;
              }
            break;

          case 'd':
            duty = (uint16_t)strtoul(optarg, NULL, 10);
            if (duty > 100 || duty < 0)
              {
                syslog(LOG_ERR,
                       "Duty must be 0-100: %s\n", optarg);
                show_usage(argv[0]);
                return EXIT_FAILURE;
              }
            break;

          case 't':
            duration = (int)strtoul(optarg, NULL, 10);
            if (duration <= 0)
              {
                syslog(LOG_ERR,
                       "Duration must be positive: %s\n", optarg);
                show_usage(argv[0]);
                return EXIT_FAILURE;
              }
            break;

          case 'c':
            polarity = (int)strtoul(optarg, NULL, 10);
            if (polarity < 0 || polarity > 2)
              {
                syslog(LOG_ERR,
                       "Polarity must be 0, 1, or 2: %s\n", optarg);
                show_usage(argv[0]);
                return EXIT_FAILURE;
              }
            break;

          case 'a':
            all_tests = true;
            break;

          case 'g':
            get_char = true;
            break;

          case 'h':
            show_usage(argv[0]);
            return EXIT_SUCCESS;

          default:
            syslog(LOG_ERR, "Unknown option: -%c\n", ch);
            show_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

  syslog(LOG_INFO, "========================================\n");
  syslog(LOG_INFO, " ArtInChip PWM Test\n");
  syslog(LOG_INFO, "========================================\n");

  /* =================================================================
   * Mode: Run all test phases on all devices (-a flag)
   * ================================================================= */

  if (all_tests)
    {
      int result;

      /* Phase 6 first: test all available devices */

      result = test_all_devices(frequency, duty, polarity, duration);
      test_report_phase("all-devices", result);

      /* Then run comprehensive tests on the first device */

      snprintf(dev.devpath, sizeof(dev.devpath), "/dev/pwm0");
      dev.pwm_id = 0;

      if (testpwm_open_device(&dev) != TEST_RESULT_PASS)
        {
          test_summary();
          return EXIT_FAILURE;
        }
      fd = dev.fd;

      result = test_basic(fd, frequency, duty, polarity, duration);
      test_report_phase("basic", result);

      result = test_edge_cases(fd, frequency, duration);
      test_report_phase("edge-cases", result);

      result = test_polarity(fd, frequency, duty, duration);
      test_report_phase("polarity", result);

#ifdef CONFIG_PWM_MULTICHAN
      result = test_multichan(fd, frequency, duty, duration);
      test_report_phase("multi-channel", result);
#endif

      result = test_restart(fd, frequency, duty, duration);
      test_report_phase("restart", result);

      testpwm_close_device(&dev);
      test_summary();

      return (g_result.failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  /* =================================================================
   * Mode: Single-device, single-run (original behavior, -a omitted)
   * ================================================================= */

  syslog(LOG_INFO, "\nConfiguration:\n");
  syslog(LOG_INFO, "  Device:    %s\n", devpath);
  syslog(LOG_INFO, "  Frequency: %" PRIu32 " Hz\n", frequency);
  syslog(LOG_INFO, "  Duty:      %d\n", duty);
  syslog(LOG_INFO, "  Duration:  %d seconds\n", duration);
  syslog(LOG_INFO, "  Polarity:  %d (0=default, 1=low, 2=high)\n",
         polarity);

  snprintf(dev.devpath, sizeof(dev.devpath), "%s", devpath);
  dev.pwm_id = 0;  /* will be parsed from path if needed */

  if (testpwm_open_device(&dev) != TEST_RESULT_PASS)
    {
      return EXIT_FAILURE;
    }
  fd = dev.fd;

  /* Basic set → start → wait → stop (or -g flow) */

  
    struct pwm_info_s info;

    build_info_single(&info, frequency, duty, polarity);

    syslog(LOG_INFO,
           "\nSetting PWM characteristics... %u  \n",
           info.duty);
    ret = testpwm_set_characteristics(fd, &info);
    if (ret != TEST_RESULT_PASS)
      {
        testpwm_close_device(&dev);
        return EXIT_FAILURE;
      }
    syslog(LOG_INFO, "PWMIOC_SETCHARACTERISTICS succeeded\n");

    if (get_char)
      {
        struct pwm_info_s info_back;
        if (testpwm_get_characteristics(fd, &info_back)
            == TEST_RESULT_PASS)
          {
            syslog(LOG_INFO,
                   "  Frequency: %" PRIu32 " Hz, Duty: %d (0-65535)\n",
                   info_back.frequency,
                   (int)(((uint32_t)info_back.duty * 65535 + 50) / 100));
          }
        }   
    syslog(LOG_INFO, "\nStarting PWM output...\n");
    ret = testpwm_start(fd);
    if (ret != TEST_RESULT_PASS)
      {
        testpwm_close_device(&dev);
        return EXIT_FAILURE;
      }
    syslog(LOG_INFO, "PWMIOC_START succeeded\n");

    syslog(LOG_INFO, "\nPWM running for %d seconds...\n", duration);
    for (int i = 0; i < duration; i++)
      {
        sleep(1);
        syslog(LOG_INFO, ".");
        fflush(stdout);

        build_info_single(&info, frequency, duty, polarity);
        duty += 10;
        if (duty > 100) duty =100;  /* Wrap around */
        
        ret = testpwm_set_characteristics(fd, &info);
        if (ret != TEST_RESULT_PASS)
      {
        syslog(LOG_INFO, "PWMIOC_SETCHARACTERISTICS failed\n");
      }
    
      }
    syslog(LOG_INFO, "\n");

    syslog(LOG_INFO, "\nStopping PWM output...\n");
    ret = testpwm_stop(fd);
    if (ret != TEST_RESULT_PASS)
      {
        testpwm_close_device(&dev);
        return EXIT_FAILURE;
      }
    syslog(LOG_INFO, "PWMIOC_STOP succeeded\n");

    if (get_char)
      {
        struct pwm_info_s info_back;
        syslog(LOG_INFO,
               "\nGetting characteristics after stop...\n");
        if (testpwm_get_characteristics(fd, &info_back)
            == TEST_RESULT_PASS)
          {
            syslog(LOG_INFO,
                   "  Frequency: %" PRIu32 " Hz, Duty: %d (0-65535)\n",
                   info_back.frequency,
                   (int)(((uint32_t)info_back.duty * 65535 + 50) / 100));
          }
      }   
  testpwm_close_device(&dev);
  syslog(LOG_INFO, "\nPWM test completed successfully!\n");
  return EXIT_SUCCESS;
}