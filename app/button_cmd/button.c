/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/config.h>

#include <input/button.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef CONFIG_AIC_INPUT_BUTTON_PSADC_CH
#define CONFIG_AIC_INPUT_BUTTON_PSADC_CH 2
#endif

#ifndef CONFIG_AIC_INPUT_BUTTON_WAKEUP_PIN
#define CONFIG_AIC_INPUT_BUTTON_WAKEUP_PIN "PD.15"
#endif

#ifndef CONFIG_AIC_INPUT_BUTTON_POLL_INTERVAL_MS
#define CONFIG_AIC_INPUT_BUTTON_POLL_INTERVAL_MS 50
#endif

#ifndef CONFIG_AIC_INPUT_BUTTON_ADC_MIN
#define CONFIG_AIC_INPUT_BUTTON_ADC_MIN 80
#endif

#ifndef CONFIG_AIC_INPUT_BUTTON_ADC_MAX
#define CONFIG_AIC_INPUT_BUTTON_ADC_MAX 2300
#endif

#ifndef CONFIG_AIC_INPUT_BUTTON_ADC_KEY_WINDOW
#define CONFIG_AIC_INPUT_BUTTON_ADC_KEY_WINDOW 180
#endif

#ifndef CONFIG_AIC_INPUT_BUTTON_ADC_UP_CENTER
#define CONFIG_AIC_INPUT_BUTTON_ADC_UP_CENTER 960
#endif

#ifndef CONFIG_AIC_INPUT_BUTTON_ADC_DOWN_CENTER
#define CONFIG_AIC_INPUT_BUTTON_ADC_DOWN_CENTER 560
#endif

#ifndef CONFIG_AIC_INPUT_BUTTON_ADC_LEFT_CENTER
#define CONFIG_AIC_INPUT_BUTTON_ADC_LEFT_CENTER 1890
#endif

#ifndef CONFIG_AIC_INPUT_BUTTON_ADC_RIGHT_CENTER
#define CONFIG_AIC_INPUT_BUTTON_ADC_RIGHT_CENTER 1550
#endif

static void button_usage(void)
{
  printf("Usage: button sample|raw [count] [interval_ms]|listen [timeout_ms]|"
         "thresholds|status|up|down|left|right|wakeup\n");
}

static void button_print_error(const char *op, int ret)
{
  fprintf(stderr, "button: %s failed: %d\n", op, -ret);
}

static void button_print_sample(const struct aic_button_sample *sample)
{
  printf("adc=%" PRId32 " key=%s wakeup=%s mask=0x%08" PRIx32 "\n",
         sample->adc_raw,
         aic_button_id_name(sample->adc_button),
         sample->wakeup_pressed ? "pressed" : "released",
         sample->state_mask);
  fflush(stdout);
}

static void button_print_event(const struct aic_button_event *event)
{
  printf("event key=%s action=%s adc=%" PRId32 " mask=0x%08" PRIx32 "\n",
         aic_button_id_name(event->id),
         aic_button_action_name(event->action),
         event->adc_raw,
         event->state_mask);
  fflush(stdout);
}

static int button_run_sample(void)
{
  struct aic_button_sample sample;
  int ret;

  ret = aic_button_init(NULL);
  if (ret < 0)
    {
      button_print_error("init", ret);
      return 1;
    }

  ret = aic_button_read_sample(&sample);
  if (ret < 0)
    {
      button_print_error("sample", ret);
      if (!aic_button_service_running())
        {
          aic_button_deinit();
        }
      return 1;
    }

  button_print_sample(&sample);
  if (!aic_button_service_running())
    {
      aic_button_deinit();
    }
  return 0;
}

static int button_run_raw(int argc, char *argv[], int argi)
{
  unsigned long count = 20;
  unsigned long interval_ms = 100;
  int ret;

  if (argc > argi)
    {
      count = strtoul(argv[argi++], NULL, 0);
    }

  if (argc > argi)
    {
      interval_ms = strtoul(argv[argi++], NULL, 0);
    }

  ret = aic_button_init(NULL);
  if (ret < 0)
    {
      button_print_error("init", ret);
      return 1;
    }

  for (unsigned long i = 0; i < count; i++)
    {
      struct aic_button_sample sample;

      ret = aic_button_read_sample(&sample);
      if (ret < 0)
        {
          button_print_error("sample", ret);
          if (!aic_button_service_running())
            {
              aic_button_deinit();
            }
          return 1;
        }

      printf("[%lu] ", i);
      button_print_sample(&sample);
      usleep((useconds_t)interval_ms * 1000u);
    }

  if (!aic_button_service_running())
    {
      aic_button_deinit();
    }
  return 0;
}

static int button_run_listen(int argc, char *argv[], int argi)
{
  uint32_t timeout_ms = AIC_BUTTON_TIMEOUT_FOREVER;
  uint32_t elapsed_ms = 0;
  bool subscribed = false;
  int ret;

  if (argc > argi)
    {
      timeout_ms = (uint32_t)strtoul(argv[argi], NULL, 0);
      if (timeout_ms == 0)
        {
          timeout_ms = AIC_BUTTON_TIMEOUT_FOREVER;
        }
    }

  ret = aic_button_subscribe(button_print_event, NULL);
  if (ret < 0 && ret != -EALREADY)
    {
      button_print_error("subscribe", ret);
      return 1;
    }

  subscribed = ret == 0;
  ret = aic_button_service_start();
  if (ret < 0)
    {
      if (subscribed)
        {
          aic_button_unsubscribe(button_print_event, NULL);
        }

      button_print_error("service_start", ret);
      return 1;
    }

  printf("button: listening, driver=psadc_poll psadc_ch=%d wakeup=%s interval=%dms\n",
         CONFIG_AIC_INPUT_BUTTON_PSADC_CH,
         CONFIG_AIC_INPUT_BUTTON_WAKEUP_PIN,
         CONFIG_AIC_INPUT_BUTTON_POLL_INTERVAL_MS);

  while (timeout_ms == AIC_BUTTON_TIMEOUT_FOREVER ||
         elapsed_ms < timeout_ms)
    {
      usleep((useconds_t)CONFIG_AIC_INPUT_BUTTON_POLL_INTERVAL_MS * 1000u);

      if (timeout_ms != AIC_BUTTON_TIMEOUT_FOREVER)
        {
          if (elapsed_ms >
              UINT32_MAX - CONFIG_AIC_INPUT_BUTTON_POLL_INTERVAL_MS)
            {
              elapsed_ms = UINT32_MAX;
            }
          else
            {
              elapsed_ms += CONFIG_AIC_INPUT_BUTTON_POLL_INTERVAL_MS;
            }
        }
    }

  if (subscribed)
    {
      aic_button_unsubscribe(button_print_event, NULL);
    }

  return 0;
}

static int button_run_thresholds(void)
{
  printf("driver=psadc_poll psadc_ch=%d wakeup_pin=%s\n",
         CONFIG_AIC_INPUT_BUTTON_PSADC_CH,
         CONFIG_AIC_INPUT_BUTTON_WAKEUP_PIN);
  printf("adc valid=[%d,%d] window=%d\n",
         CONFIG_AIC_INPUT_BUTTON_ADC_MIN,
         CONFIG_AIC_INPUT_BUTTON_ADC_MAX,
         CONFIG_AIC_INPUT_BUTTON_ADC_KEY_WINDOW);
  printf("adc centers: up=%d down=%d left=%d right=%d\n",
         CONFIG_AIC_INPUT_BUTTON_ADC_UP_CENTER,
         CONFIG_AIC_INPUT_BUTTON_ADC_DOWN_CENTER,
         CONFIG_AIC_INPUT_BUTTON_ADC_LEFT_CENTER,
         CONFIG_AIC_INPUT_BUTTON_ADC_RIGHT_CENTER);
  return 0;
}

static int button_run_status(void)
{
  struct aic_button_sample sample;
  int ret;

  printf("service=%s subscribers=%lu\n",
         aic_button_service_running() ? "running" : "stopped",
         (unsigned long)aic_button_subscriber_count());

  ret = aic_button_read_sample(&sample);
  if (ret == 0)
    {
      button_print_sample(&sample);
    }
  else if (ret != -EAGAIN)
    {
      button_print_error("status sample", ret);
      if (!aic_button_service_running())
        {
          aic_button_deinit();
        }
      return 1;
    }

  if (!aic_button_service_running())
    {
      aic_button_deinit();
    }

  return 0;
}

static int button_run_inject(enum aic_button_id id)
{
  int ret = aic_button_inject(id);

  if (ret < 0)
    {
      button_print_error("inject", ret);
      return 1;
    }

  printf("button: injected %s pressed/released\n",
         aic_button_id_name(id));
  return 0;
}

int button_main(int argc, char *argv[])
{
  const char *cmd;
  int argi = 1;

  if (argc <= argi)
    {
      button_usage();
      return 1;
    }

  cmd = argv[argi++];

  if (strcmp(cmd, "sample") == 0)
    {
      return button_run_sample();
    }
  else if (strcmp(cmd, "raw") == 0)
    {
      return button_run_raw(argc, argv, argi);
    }
  else if (strcmp(cmd, "listen") == 0)
    {
      return button_run_listen(argc, argv, argi);
    }
  else if (strcmp(cmd, "thresholds") == 0)
    {
      return button_run_thresholds();
    }
  else if (strcmp(cmd, "status") == 0)
    {
      return button_run_status();
    }
  else if (strcmp(cmd, "up") == 0)
    {
      return button_run_inject(AIC_BUTTON_UP);
    }
  else if (strcmp(cmd, "down") == 0)
    {
      return button_run_inject(AIC_BUTTON_DOWN);
    }
  else if (strcmp(cmd, "left") == 0)
    {
      return button_run_inject(AIC_BUTTON_LEFT);
    }
  else if (strcmp(cmd, "right") == 0)
    {
      return button_run_inject(AIC_BUTTON_RIGHT);
    }
  else if (strcmp(cmd, "wakeup") == 0)
    {
      return button_run_inject(AIC_BUTTON_WAKEUP);
    }

  button_usage();
  return 1;
}
