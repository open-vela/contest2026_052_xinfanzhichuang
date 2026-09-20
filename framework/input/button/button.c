/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <nuttx/config.h>

#include <input/button.h>

#include <aic_core.h>
#include <aic_hal_gpio.h>
#include <hal_psadc.h>

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/compiler.h>

#ifndef CONFIG_AIC_INPUT_BUTTON_PSADC_CH
#define CONFIG_AIC_INPUT_BUTTON_PSADC_CH 2
#endif

#ifndef CONFIG_AIC_INPUT_BUTTON_WAKEUP_PIN
#define CONFIG_AIC_INPUT_BUTTON_WAKEUP_PIN "PD.15"
#endif

#ifndef CONFIG_AIC_INPUT_BUTTON_POLL_INTERVAL_MS
#define CONFIG_AIC_INPUT_BUTTON_POLL_INTERVAL_MS 50
#endif

#ifndef CONFIG_AIC_INPUT_BUTTON_PSADC_READ_TIMEOUT_MS
#define CONFIG_AIC_INPUT_BUTTON_PSADC_READ_TIMEOUT_MS 200
#endif

#ifndef CONFIG_AIC_INPUT_BUTTON_DEBOUNCE_SAMPLES
#define CONFIG_AIC_INPUT_BUTTON_DEBOUNCE_SAMPLES 2
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

#ifndef CONFIG_AIC_INPUT_BUTTON_THREAD_PRIORITY
#define CONFIG_AIC_INPUT_BUTTON_THREAD_PRIORITY 100
#endif

#ifndef CONFIG_AIC_INPUT_BUTTON_THREAD_STACKSIZE
#define CONFIG_AIC_INPUT_BUTTON_THREAD_STACKSIZE 4096
#endif

#ifndef CONFIG_AIC_INPUT_BUTTON_AUTOSTART_DELAY_MS
#define CONFIG_AIC_INPUT_BUTTON_AUTOSTART_DELAY_MS 2000
#endif

#define AIC_BUTTON_QUEUE_SIZE 8
#define AIC_BUTTON_MAX_SUBSCRIBERS 8

struct aic_button_subscriber
{
  aic_button_event_cb_t cb;
  void *arg;
};

struct aic_button_adc_key
{
  enum aic_button_id id;
  int32_t center;
};

struct aic_button_context
{
  bool initialized;
  bool have_state;
  long wakeup_pin;
  uint32_t poll_interval_ms;
  uint8_t debounce_samples;
  uint32_t stable_mask;
  uint32_t pending_mask;
  uint8_t pending_count;
  struct aic_button_sample last_sample;
  bool have_sample;
  struct aic_button_event queue[AIC_BUTTON_QUEUE_SIZE];
  uint8_t queue_head;
  uint8_t queue_tail;
  struct aic_button_subscriber subscribers[AIC_BUTTON_MAX_SUBSCRIBERS];
  volatile bool thread_running;
  volatile bool thread_stop;
  volatile int thread_pid;
};

static pthread_mutex_t g_button_lock = PTHREAD_MUTEX_INITIALIZER;

static struct aic_button_context g_button =
{
  .wakeup_pin = -1,
  .thread_pid = -1,
};

uint32_t aic_button_bit(enum aic_button_id id)
{
  if (id <= AIC_BUTTON_NONE || id > AIC_BUTTON_WAKEUP)
    {
      return 0;
    }

  return 1u << ((uint32_t)id - 1u);
}

const char *aic_button_id_name(enum aic_button_id id)
{
  switch (id)
    {
    case AIC_BUTTON_UP:
      return "up";
    case AIC_BUTTON_DOWN:
      return "down";
    case AIC_BUTTON_LEFT:
      return "left";
    case AIC_BUTTON_RIGHT:
      return "right";
    case AIC_BUTTON_WAKEUP:
      return "wakeup";
    default:
      return "none";
    }
}

const char *aic_button_action_name(enum aic_button_action action)
{
  return action == AIC_BUTTON_PRESSED ? "pressed" : "released";
}

static enum aic_button_id aic_button_classify_adc(int32_t raw)
{
  static const struct aic_button_adc_key keys[] =
  {
    { AIC_BUTTON_DOWN, CONFIG_AIC_INPUT_BUTTON_ADC_DOWN_CENTER },
    { AIC_BUTTON_UP, CONFIG_AIC_INPUT_BUTTON_ADC_UP_CENTER },
    { AIC_BUTTON_RIGHT, CONFIG_AIC_INPUT_BUTTON_ADC_RIGHT_CENTER },
    { AIC_BUTTON_LEFT, CONFIG_AIC_INPUT_BUTTON_ADC_LEFT_CENTER },
  };
  enum aic_button_id best_id = AIC_BUTTON_NONE;
  int32_t best_delta = CONFIG_AIC_INPUT_BUTTON_ADC_KEY_WINDOW + 1;
  size_t i;

  if (raw < CONFIG_AIC_INPUT_BUTTON_ADC_MIN ||
      raw > CONFIG_AIC_INPUT_BUTTON_ADC_MAX)
    {
      return AIC_BUTTON_NONE;
    }

  for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    {
      int32_t delta = raw - keys[i].center;

      if (delta < 0)
        {
          delta = -delta;
        }

      if (delta <= CONFIG_AIC_INPUT_BUTTON_ADC_KEY_WINDOW &&
          delta < best_delta)
        {
          best_delta = delta;
          best_id = keys[i].id;
        }
    }

  return best_id;
}

static bool aic_button_queue_empty_locked(void)
{
  return g_button.queue_head == g_button.queue_tail;
}

static int aic_button_queue_push_locked(const struct aic_button_event *event)
{
  uint8_t next = (uint8_t)((g_button.queue_head + 1) %
                           AIC_BUTTON_QUEUE_SIZE);

  if (next == g_button.queue_tail)
    {
      return -ENOSPC;
    }

  g_button.queue[g_button.queue_head] = *event;
  g_button.queue_head = next;
  return 0;
}

static int aic_button_queue_pop_locked(struct aic_button_event *event)
{
  if (aic_button_queue_empty_locked())
    {
      return -EAGAIN;
    }

  *event = g_button.queue[g_button.queue_tail];
  g_button.queue_tail = (uint8_t)((g_button.queue_tail + 1) %
                                  AIC_BUTTON_QUEUE_SIZE);
  return 0;
}

static bool aic_button_valid_id(enum aic_button_id id)
{
  return id > AIC_BUTTON_NONE && id <= AIC_BUTTON_WAKEUP;
}

static bool aic_button_service_running_locked(void)
{
  return g_button.thread_running;
}

static size_t aic_button_subscriber_count_locked(void)
{
  size_t count = 0;
  size_t i;

  for (i = 0; i < AIC_BUTTON_MAX_SUBSCRIBERS; i++)
    {
      if (g_button.subscribers[i].cb != NULL)
        {
          count++;
        }
    }

  return count;
}

static size_t aic_button_copy_subscribers(
  struct aic_button_subscriber *subscribers,
  size_t max_count)
{
  size_t count = 0;
  size_t i;

  for (i = 0; i < AIC_BUTTON_MAX_SUBSCRIBERS && count < max_count; i++)
    {
      if (g_button.subscribers[i].cb != NULL)
        {
          subscribers[count++] = g_button.subscribers[i];
        }
    }

  return count;
}

static void aic_button_dispatch_event(const struct aic_button_event *event)
{
  struct aic_button_subscriber subscribers[AIC_BUTTON_MAX_SUBSCRIBERS];
  size_t count;
  size_t i;

  if (event == NULL)
    {
      return;
    }

  pthread_mutex_lock(&g_button_lock);
  count = aic_button_copy_subscribers(subscribers,
                                      AIC_BUTTON_MAX_SUBSCRIBERS);
  pthread_mutex_unlock(&g_button_lock);

  for (i = 0; i < count; i++)
    {
      subscribers[i].cb(event, subscribers[i].arg);
    }
}

#ifdef CONFIG_AIC_INPUT_BUTTON_EVENT_LOG
static void aic_button_log_event(const struct aic_button_event *event)
{
  if (event == NULL)
    {
      return;
    }

  syslog(LOG_INFO,
         "button: event key=%s action=%s adc=%" PRId32
         " mask=0x%08" PRIx32 "\n",
         aic_button_id_name(event->id),
         aic_button_action_name(event->action),
         event->adc_raw,
         event->state_mask);
}
#endif

static size_t aic_button_queue_change_locked(
  uint32_t old_mask,
  uint32_t new_mask,
  int32_t adc_raw)
{
  size_t count = 0;
  enum aic_button_id id;

  for (id = AIC_BUTTON_UP; id <= AIC_BUTTON_WAKEUP; id++)
    {
      uint32_t bit = aic_button_bit(id);
      bool old_pressed = (old_mask & bit) != 0;
      bool new_pressed = (new_mask & bit) != 0;

      if (old_pressed != new_pressed)
        {
          struct aic_button_event event;

          memset(&event, 0, sizeof(event));
          event.id = id;
          event.action = new_pressed ? AIC_BUTTON_PRESSED :
                         AIC_BUTTON_RELEASED;
          event.state_mask = new_mask;
          event.adc_raw = adc_raw;
          if (aic_button_queue_push_locked(&event) == 0)
            {
              count++;
            }
        }
    }

  return count;
}

static int aic_button_read_adc_raw(int32_t *raw)
{
  u32 value;
  int ret;

  if (raw == NULL)
    {
      return -EINVAL;
    }

  ret = hal_psadc_read_channel_poll(CONFIG_AIC_INPUT_BUTTON_PSADC_CH,
                                    &value,
                                    CONFIG_AIC_INPUT_BUTTON_PSADC_READ_TIMEOUT_MS);
  if (ret < 0)
    {
      return ret;
    }

  *raw = value;
  return 0;
}

static int aic_button_read_wakeup(bool *pressed)
{
  unsigned int group;
  unsigned int pin;
  unsigned int value = 1;
  int ret;

  if (pressed == NULL)
    {
      return -EINVAL;
    }

  if (g_button.wakeup_pin < 0)
    {
      *pressed = false;
      return 0;
    }

  group = GPIO_GROUP(g_button.wakeup_pin);
  pin = GPIO_GROUP_PIN(g_button.wakeup_pin);

  ret = hal_gpio_get_value(group, pin, &value);
  if (ret < 0)
    {
      return ret;
    }

  *pressed = value == 0;
  return 0;
}

static int aic_button_configure_wakeup_pin(const char *pin_name)
{
  unsigned int group;
  unsigned int pin;
  long gpio;
  int ret;

  if (pin_name == NULL || pin_name[0] == '\0')
    {
      g_button.wakeup_pin = -1;
      return 0;
    }

  gpio = hal_gpio_name2pin(pin_name);
  if (gpio < 0)
    {
      return -EINVAL;
    }

  group = GPIO_GROUP(gpio);
  pin = GPIO_GROUP_PIN(gpio);

  ret = hal_gpio_set_func(group, pin, 1);
  if (ret < 0)
    {
      return ret;
    }

  ret = hal_gpio_set_bias_pull(group, pin, PIN_PULL_UP);
  if (ret < 0)
    {
      return ret;
    }

  ret = hal_gpio_direction_input(group, pin);
  if (ret < 0)
    {
      return ret;
    }

  g_button.wakeup_pin = gpio;
  return 0;
}

static int aic_button_read_sample_internal(
  struct aic_button_sample *sample)
{
  enum aic_button_id adc_button;
  bool wakeup_pressed;
  int32_t adc_raw;
  uint32_t mask = 0;
  int ret;

  if (sample == NULL)
    {
      return -EINVAL;
    }

  ret = aic_button_read_adc_raw(&adc_raw);
  if (ret < 0)
    {
      syslog(LOG_ERR, "button: adc read failed: %d\n", ret);
      return ret;
    }

  ret = aic_button_read_wakeup(&wakeup_pressed);
  if (ret < 0)
    {
      syslog(LOG_ERR, "button: wakeup read failed: %d\n", ret);
      return ret;
    }

  adc_button = aic_button_classify_adc(adc_raw);
  mask |= aic_button_bit(adc_button);
  if (wakeup_pressed)
    {
      mask |= aic_button_bit(AIC_BUTTON_WAKEUP);
    }

  sample->adc_raw = adc_raw;
  sample->adc_button = adc_button;
  sample->wakeup_pressed = wakeup_pressed;
  sample->state_mask = mask;

  return 0;
}

int aic_button_read_sample(struct aic_button_sample *sample)
{
  bool initialized;
  bool service_running;
  int ret;

  if (sample == NULL)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_button_lock);
  service_running = aic_button_service_running_locked();
  if (service_running && g_button.have_sample)
    {
      *sample = g_button.last_sample;
      pthread_mutex_unlock(&g_button_lock);
      return 0;
    }

  initialized = g_button.initialized;
  pthread_mutex_unlock(&g_button_lock);

  if (service_running)
    {
      return -EAGAIN;
    }

  if (!initialized)
    {
      ret = aic_button_init(NULL);
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = aic_button_read_sample_internal(sample);
  if (ret == 0)
    {
      pthread_mutex_lock(&g_button_lock);
      g_button.last_sample = *sample;
      g_button.have_sample = true;
      pthread_mutex_unlock(&g_button_lock);
    }

  return ret;
}

static int aic_button_update(void)
{
  struct aic_button_sample sample;
  int ret;

  ret = aic_button_read_sample_internal(&sample);
  if (ret < 0)
    {
      return ret;
    }

  pthread_mutex_lock(&g_button_lock);
  g_button.last_sample = sample;
  g_button.have_sample = true;

  if (!g_button.have_state)
    {
      g_button.stable_mask = sample.state_mask;
      g_button.pending_mask = sample.state_mask;
      g_button.pending_count = 0;
      g_button.have_state = true;
      pthread_mutex_unlock(&g_button_lock);
      return 0;
    }

  if (sample.state_mask == g_button.stable_mask)
    {
      g_button.pending_mask = sample.state_mask;
      g_button.pending_count = 0;
      pthread_mutex_unlock(&g_button_lock);
      return 0;
    }

  if (sample.state_mask != g_button.pending_mask)
    {
      g_button.pending_mask = sample.state_mask;
      g_button.pending_count = 1;
      if (g_button.debounce_samples <= 1)
        {
          aic_button_queue_change_locked(g_button.stable_mask,
                                         sample.state_mask,
                                         sample.adc_raw);
          g_button.stable_mask = sample.state_mask;
          g_button.pending_count = 0;
        }

      pthread_mutex_unlock(&g_button_lock);
      return 0;
    }

  if (g_button.pending_count < g_button.debounce_samples)
    {
      g_button.pending_count++;
    }

  if (g_button.pending_count >= g_button.debounce_samples)
    {
      uint32_t old_mask = g_button.stable_mask;

      g_button.stable_mask = sample.state_mask;
      g_button.pending_count = 0;
      aic_button_queue_change_locked(old_mask, sample.state_mask,
                                     sample.adc_raw);
    }

  pthread_mutex_unlock(&g_button_lock);
  return 0;
}

int aic_button_init(const struct aic_button_config *config)
{
  const char *wakeup_pin = CONFIG_AIC_INPUT_BUTTON_WAKEUP_PIN;
  uint32_t poll_interval_ms = CONFIG_AIC_INPUT_BUTTON_POLL_INTERVAL_MS;
  uint8_t debounce_samples = CONFIG_AIC_INPUT_BUTTON_DEBOUNCE_SAMPLES;
  int ret;

  pthread_mutex_lock(&g_button_lock);
  if (g_button.initialized)
    {
      pthread_mutex_unlock(&g_button_lock);
      return 0;
    }

  if (config != NULL)
    {
      if (config->wakeup_pin != NULL)
        {
          wakeup_pin = config->wakeup_pin;
        }

      if (config->poll_interval_ms != 0)
        {
          poll_interval_ms = config->poll_interval_ms;
        }

      if (config->debounce_samples != 0)
        {
          debounce_samples = config->debounce_samples;
        }
    }

  ret = hal_psadc_init();
  if (ret < 0)
    {
      pthread_mutex_unlock(&g_button_lock);
      return ret;
    }

  ret = aic_button_configure_wakeup_pin(wakeup_pin);
  if (ret < 0)
    {
      goto fail_psadc;
    }

  g_button.poll_interval_ms = poll_interval_ms;
  g_button.debounce_samples = debounce_samples == 0 ? 1 : debounce_samples;
  g_button.stable_mask = 0;
  g_button.pending_mask = 0;
  g_button.pending_count = 0;
  memset(&g_button.last_sample, 0, sizeof(g_button.last_sample));
  g_button.have_sample = false;
  g_button.queue_head = 0;
  g_button.queue_tail = 0;
  g_button.have_state = false;
  g_button.thread_stop = false;
  g_button.thread_running = false;
  g_button.thread_pid = -1;
  g_button.initialized = true;

  pthread_mutex_unlock(&g_button_lock);
  return 0;

fail_psadc:
  hal_psadc_deinit();
  pthread_mutex_unlock(&g_button_lock);
  return ret;
}

void aic_button_deinit(void)
{
  bool initialized;
  bool thread_running;
  int thread_pid;

  aic_button_service_stop();

  pthread_mutex_lock(&g_button_lock);
  initialized = g_button.initialized;
  thread_running = g_button.thread_running;
  thread_pid = g_button.thread_pid;
  pthread_mutex_unlock(&g_button_lock);

  if (thread_running)
    {
      /* A callback must not deinitialize the module from its own task. */
      if (thread_pid == getpid())
        {
          /* The callback task will return after the current callback. The
           * caller must not use the API again from that callback. */
          return;
        }

      /* Wait until the task has stopped using the PSADC hardware. */
      while (aic_button_service_running())
        {
          usleep(1000);
        }
    }

  if (!initialized)
    {
      return;
    }

  pthread_mutex_lock(&g_button_lock);
  g_button.thread_pid = -1;

  hal_psadc_deinit();

  memset(&g_button, 0, sizeof(g_button));
  g_button.wakeup_pin = -1;
  g_button.thread_pid = -1;
  pthread_mutex_unlock(&g_button_lock);
}

int aic_button_poll_event(struct aic_button_event *event,
                          uint32_t timeout_ms)
{
  uint32_t elapsed_ms = 0;
  uint32_t poll_interval_ms;
  bool initialized;
  bool service_running;
  int ret;

  if (event == NULL)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_button_lock);
  service_running = aic_button_service_running_locked();
  initialized = g_button.initialized;
  poll_interval_ms = g_button.poll_interval_ms == 0 ?
                     CONFIG_AIC_INPUT_BUTTON_POLL_INTERVAL_MS :
                     g_button.poll_interval_ms;
  pthread_mutex_unlock(&g_button_lock);

  if (service_running)
    {
      return -EBUSY;
    }

  if (!initialized)
    {
      ret = aic_button_init(NULL);
      if (ret < 0)
        {
          return ret;
        }

      pthread_mutex_lock(&g_button_lock);
      poll_interval_ms = g_button.poll_interval_ms == 0 ?
                         CONFIG_AIC_INPUT_BUTTON_POLL_INTERVAL_MS :
                         g_button.poll_interval_ms;
      pthread_mutex_unlock(&g_button_lock);
    }

  do
    {
      ret = aic_button_update();
      if (ret < 0)
        {
          return ret;
        }

      pthread_mutex_lock(&g_button_lock);
      ret = aic_button_queue_pop_locked(event);
      pthread_mutex_unlock(&g_button_lock);
      if (ret == 0)
        {
          return 0;
        }

      if (timeout_ms == AIC_BUTTON_TIMEOUT_NONE)
        {
          return -EAGAIN;
        }

      usleep((useconds_t)poll_interval_ms * 1000u);

      if (timeout_ms != AIC_BUTTON_TIMEOUT_FOREVER)
        {
          elapsed_ms += poll_interval_ms;
        }
    }
  while (timeout_ms == AIC_BUTTON_TIMEOUT_FOREVER ||
         elapsed_ms < timeout_ms);

  return -ETIMEDOUT;
}

int aic_button_subscribe(aic_button_event_cb_t cb, void *arg)
{
  size_t i;

  if (cb == NULL)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_button_lock);

  for (i = 0; i < AIC_BUTTON_MAX_SUBSCRIBERS; i++)
    {
      if (g_button.subscribers[i].cb == cb &&
          g_button.subscribers[i].arg == arg)
        {
          pthread_mutex_unlock(&g_button_lock);
          return -EALREADY;
        }
    }

  for (i = 0; i < AIC_BUTTON_MAX_SUBSCRIBERS; i++)
    {
      if (g_button.subscribers[i].cb == NULL)
        {
          g_button.subscribers[i].cb = cb;
          g_button.subscribers[i].arg = arg;
          pthread_mutex_unlock(&g_button_lock);
          return 0;
        }
    }

  pthread_mutex_unlock(&g_button_lock);
  return -ENOSPC;
}

void aic_button_unsubscribe(aic_button_event_cb_t cb, void *arg)
{
  size_t i;

  if (cb == NULL)
    {
      return;
    }

  pthread_mutex_lock(&g_button_lock);

  for (i = 0; i < AIC_BUTTON_MAX_SUBSCRIBERS; i++)
    {
      if (g_button.subscribers[i].cb == cb &&
          g_button.subscribers[i].arg == arg)
        {
          memset(&g_button.subscribers[i], 0,
                 sizeof(g_button.subscribers[i]));
        }
    }

  pthread_mutex_unlock(&g_button_lock);
}

bool aic_button_service_running(void)
{
  bool running;

  pthread_mutex_lock(&g_button_lock);
  running = aic_button_service_running_locked();
  pthread_mutex_unlock(&g_button_lock);

  return running;
}

size_t aic_button_subscriber_count(void)
{
  size_t count;

  pthread_mutex_lock(&g_button_lock);
  count = aic_button_subscriber_count_locked();
  pthread_mutex_unlock(&g_button_lock);

  return count;
}

static int aic_button_thread_entry(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  /* Publish the worker PID before callbacks can call stop/deinit. */
  pthread_mutex_lock(&g_button_lock);
  g_button.thread_pid = getpid();
  pthread_mutex_unlock(&g_button_lock);

  while (true)
    {
      struct aic_button_event event;
      uint32_t poll_interval_ms;
      bool thread_stop;

      pthread_mutex_lock(&g_button_lock);
      thread_stop = g_button.thread_stop;
      poll_interval_ms = g_button.poll_interval_ms == 0 ?
                         CONFIG_AIC_INPUT_BUTTON_POLL_INTERVAL_MS :
                         g_button.poll_interval_ms;
      pthread_mutex_unlock(&g_button_lock);

      if (thread_stop)
        {
          break;
        }

      if (aic_button_update() == 0)
        {
          while (true)
            {
              int ret;

              pthread_mutex_lock(&g_button_lock);
              ret = aic_button_queue_pop_locked(&event);
              pthread_mutex_unlock(&g_button_lock);
              if (ret < 0)
                {
                  break;
                }

#ifdef CONFIG_AIC_INPUT_BUTTON_EVENT_LOG
              aic_button_log_event(&event);
#endif
              aic_button_dispatch_event(&event);
            }
        }

      pthread_mutex_lock(&g_button_lock);
      thread_stop = g_button.thread_stop;
      pthread_mutex_unlock(&g_button_lock);
      if (!thread_stop)
        {
          usleep((useconds_t)poll_interval_ms * 1000u);
        }
    }

  pthread_mutex_lock(&g_button_lock);
  g_button.thread_pid = -1;
  g_button.thread_running = false;
  pthread_mutex_unlock(&g_button_lock);
  return 0;
}

int aic_button_service_start(void)
{
  static char * const button_argv[] =
  {
    "aic_button",
    NULL
  };
  bool running;
  int ret;

  ret = aic_button_init(NULL);
  if (ret < 0)
    {
      return ret;
    }

  pthread_mutex_lock(&g_button_lock);
  running = aic_button_service_running_locked();
  if (running)
    {
      pthread_mutex_unlock(&g_button_lock);
      return 0;
    }

  /* Discard events and sampling state left by a previous run. */
  g_button.queue_head = 0;
  g_button.queue_tail = 0;
  g_button.have_state = false;
  g_button.stable_mask = 0;
  g_button.pending_mask = 0;
  g_button.pending_count = 0;
  memset(&g_button.last_sample, 0, sizeof(g_button.last_sample));
  g_button.have_sample = false;
  g_button.thread_pid = -1;
  g_button.thread_stop = false;
  g_button.thread_running = true;
  pthread_mutex_unlock(&g_button_lock);

  ret = task_create("aic_button",
                    CONFIG_AIC_INPUT_BUTTON_THREAD_PRIORITY,
                    CONFIG_AIC_INPUT_BUTTON_THREAD_STACKSIZE,
                    aic_button_thread_entry,
                    button_argv);
  if (ret < 0)
    {
      pthread_mutex_lock(&g_button_lock);
      g_button.thread_running = false;
      g_button.thread_stop = false;
      g_button.thread_pid = -1;
      pthread_mutex_unlock(&g_button_lock);
      return ret;
    }

  return 0;
}

void aic_button_service_stop(void)
{
  int thread_pid;
  bool running;

  pthread_mutex_lock(&g_button_lock);
  g_button.thread_stop = true;
  thread_pid = g_button.thread_pid;
  running = g_button.thread_running;
  pthread_mutex_unlock(&g_button_lock);

  if (!running || thread_pid == getpid())
    {
      return;
    }

  /* Do not close the ADC descriptor while the worker may still be sampling. */
  while (aic_button_service_running())
    {
      usleep(1000);
    }

  pthread_mutex_lock(&g_button_lock);
  g_button.thread_pid = -1;
  pthread_mutex_unlock(&g_button_lock);
}

int aic_button_inject(enum aic_button_id id)
{
  struct aic_button_event event;
  uint32_t bit;
  uint32_t base_mask;
  uint8_t queue_count;
  int ret;

  if (!aic_button_valid_id(id))
    {
      return -EINVAL;
    }

  ret = aic_button_service_start();
  if (ret < 0)
    {
      return ret;
    }

  bit = aic_button_bit(id);

  pthread_mutex_lock(&g_button_lock);
  base_mask = g_button.have_state ? g_button.stable_mask :
              (g_button.have_sample ? g_button.last_sample.state_mask : 0);

  queue_count = g_button.queue_head >= g_button.queue_tail ?
                g_button.queue_head - g_button.queue_tail :
                AIC_BUTTON_QUEUE_SIZE + g_button.queue_head -
                g_button.queue_tail;
  if (queue_count > AIC_BUTTON_QUEUE_SIZE - 1 - 2)
    {
      pthread_mutex_unlock(&g_button_lock);
      return -ENOSPC;
    }

  memset(&event, 0, sizeof(event));
  event.id = id;
  event.adc_raw = -1;
  event.action = AIC_BUTTON_PRESSED;
  event.state_mask = base_mask | bit;
  (void)aic_button_queue_push_locked(&event);

  event.action = AIC_BUTTON_RELEASED;
  event.state_mask = base_mask & ~bit;
  (void)aic_button_queue_push_locked(&event);
  pthread_mutex_unlock(&g_button_lock);
  return 0;
}

#ifdef CONFIG_AIC_INPUT_BUTTON_AUTOSTART
static int aic_button_autostart_entry(int argc, char *argv[])
{
  int ret;

  (void)argc;
  (void)argv;

  usleep(CONFIG_AIC_INPUT_BUTTON_AUTOSTART_DELAY_MS * 1000);

  if (aic_button_service_running())
    {
      syslog(LOG_INFO, "button: service already running\n");
      return 0;
    }

  ret = aic_button_service_start();
  if (ret < 0)
    {
      syslog(LOG_ERR, "button: autostart failed: %d\n", ret);
    }
  else
    {
      syslog(LOG_INFO, "button: service autostarted\n");
    }

  return ret;
}

constructor_fuction static void aic_button_autostart_init(void)
{
  char *argv[] =
  {
    "aic_button_autostart",
    NULL
  };

  (void)task_create("aic_button_autostart",
                    CONFIG_AIC_INPUT_BUTTON_THREAD_PRIORITY,
                    2048,
                    aic_button_autostart_entry,
                    argv);
}
#endif
