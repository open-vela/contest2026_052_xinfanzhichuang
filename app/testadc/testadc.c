/****************************************************************************
 * contest2026_052_xinfanzhichuang/app/testadc/testadc.c
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
 * ArtInChip D13x / D12x ADC Test Application
 *
 * This test exercises the ArtInChip ADC driver through the NuttX upper-half
 * ADC character driver interface.
 *
 * Test phases:
 *   1. Basic open/read/close
 *   2. Single channel continuous sampling
 *   3. Multiple channels sampling
 *   4. Polling mode test
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

#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ADC_DEFAULT_DEVPATH    "/dev/gpadc2"
#define ADC_DEFAULT_CHANNEL    0
#define ADC_DEFAULT_SAMPLES    10
#define ADC_DEFAULT_DELAY      1000000

#define ADC_DEV_MAX            8

#ifndef USEC_PER_SEC
#define USEC_PER_SEC           1000000
#endif

#define TEST_RESULT_PASS       0
#define TEST_RESULT_FAIL       1

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct testadc_result_s
{
  int   total;
  int   passed;
  int   failed;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct testadc_result_s g_result;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void show_usage(FAR const char *progname);

/****************************************************************************
 * Test: Basic open → read → close
 ****************************************************************************/



int32_t adc_read_one_sample(int fd)
{
  struct adc_msg_s sample;

  int nbytes;

  /* software trigger to start one ADC conversion */

  ioctl(fd, ANIOC_TRIGGER, 0);
  /* Read one samples */

  nbytes = read(fd, &sample, sizeof(struct adc_msg_s));
  printf("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAnbytes: %d: channel:(%d)  value: %ld\n", nbytes, sample.am_channel, sample.am_data);
  /* Handle unexpected return values */
  return sample.am_data;
}

/****************************************************************************
 * Usage
 ****************************************************************************/

static void show_usage(FAR const char *progname)
{
  syslog(LOG_INFO,
         "Usage: %s [options]\n"
         "\n"
         "Test the ArtInChip ADC driver (D13x / D12x).\n"
         "\n"
         "Options:\n"
         "  -p <devpath>   ADC device path (default: %s)\n"
         "  -c <channel>   ADC channel number (default: %d)\n"
         "  -n <samples>   Number of samples (default: %d)\n"
         "  -d <delay>     Delay between samples in us (default: %d)\n"
         "  -a             Run all test phases\n"
         "  -h             Show this help\n",
         progname,
         ADC_DEFAULT_DEVPATH,
         ADC_DEFAULT_CHANNEL,
         ADC_DEFAULT_SAMPLES,
         ADC_DEFAULT_DELAY);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  const char *devpath = ADC_DEFAULT_DEVPATH;
  int channel = ADC_DEFAULT_CHANNEL;
  int samples = ADC_DEFAULT_SAMPLES;
  int delay = ADC_DEFAULT_DELAY;
  int fd;
  int ch;

  memset(&g_result, 0, sizeof(g_result));

  while ((ch = getopt(argc, argv, "p:c:n:d:ah")) != ERROR)
    {
      switch (ch)
        {
          case 'p':
            devpath = optarg;
            break;

          case 'n':
            samples = (int)strtoul(optarg, NULL, 10);
            if (samples <= 0)
              {
                syslog(LOG_ERR, "Invalid samples (must be > 0): %s\n", optarg);
                show_usage(argv[0]);
                return EXIT_FAILURE;
              }
            break;

          case 'd':
            delay = (int)strtoul(optarg, NULL, 10);
            if (delay < 0)
              {
                syslog(LOG_ERR, "Invalid delay (must be >= 0): %s\n", optarg);
                show_usage(argv[0]);
                return EXIT_FAILURE;
              }
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
  syslog(LOG_INFO, " ArtInChip ADC Test\n");
  syslog(LOG_INFO, "========================================\n");

  fd = open(devpath, O_RDONLY);
  if(fd < 0)
    {
      syslog(LOG_ERR, "FAIL: open %s: %d (%s)\n",
             devpath, errno, strerror(errno));
      return EXIT_FAILURE;
  }
  
  syslog(LOG_INFO, "\nConfiguration:\n");
  syslog(LOG_INFO, "  Device:   %s\n", devpath);
  syslog(LOG_INFO, "  Channel:  %d\n", channel);
  syslog(LOG_INFO, "  Samples:  %d\n", samples);
  syslog(LOG_INFO, "  Delay:    %d us\n", delay);

  fd = open(devpath, O_RDONLY);
  if (fd < 0)
    {
      syslog(LOG_ERR, "FAIL: open %s: %d (%s)\n",
             devpath, errno, strerror(errno));
      return EXIT_FAILURE;
    }

  syslog(LOG_INFO, "\nReading %d samples...\n", samples);
  for (int i = 0; i < samples; i++)
    {
    int32_t value = adc_read_one_sample(fd);

    syslog(LOG_INFO, "  [%d] Value: %ld\n", i, value);

    }

  close(fd);
  syslog(LOG_INFO, "\nADC test completed successfully!\n");
  return EXIT_SUCCESS;
}