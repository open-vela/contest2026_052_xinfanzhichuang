#ifndef __FRAMEWORK_INCLUDE_INPUT_BUTTON_H
#define __FRAMEWORK_INCLUDE_INPUT_BUTTON_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AIC_BUTTON_TIMEOUT_NONE 0
#define AIC_BUTTON_TIMEOUT_FOREVER UINT32_MAX

enum aic_button_id
{
  AIC_BUTTON_NONE = 0,
  AIC_BUTTON_UP,
  AIC_BUTTON_DOWN,
  AIC_BUTTON_LEFT,
  AIC_BUTTON_RIGHT,
  AIC_BUTTON_WAKEUP,
};

enum aic_button_action
{
  AIC_BUTTON_RELEASED = 0,
  AIC_BUTTON_PRESSED = 1,
};

struct aic_button_sample
{
  int32_t adc_raw;
  enum aic_button_id adc_button;
  bool wakeup_pressed;
  uint32_t state_mask;
};

struct aic_button_event
{
  enum aic_button_id id;
  enum aic_button_action action;
  uint32_t state_mask;
  int32_t adc_raw;
};

typedef void (*aic_button_event_cb_t)(const struct aic_button_event *event,
                                      void *arg);

struct aic_button_config
{
  const char *wakeup_pin;
  uint32_t poll_interval_ms;
  uint8_t debounce_samples;
};

int aic_button_init(const struct aic_button_config *config);
void aic_button_deinit(void);

int aic_button_read_sample(struct aic_button_sample *sample);
int aic_button_poll_event(struct aic_button_event *event,
                          uint32_t timeout_ms);

/* Subscribers receive both physical and injected button events. The
 * callback is normally called from the button worker task. */
int aic_button_subscribe(aic_button_event_cb_t cb, void *arg);
void aic_button_unsubscribe(aic_button_event_cb_t cb, void *arg);

int aic_button_service_start(void);
void aic_button_service_stop(void);
bool aic_button_service_running(void);
size_t aic_button_subscriber_count(void);

/* Queue one pressed/released pair through the normal worker/subscriber path. */
int aic_button_inject(enum aic_button_id id);

uint32_t aic_button_bit(enum aic_button_id id);
const char *aic_button_id_name(enum aic_button_id id);
const char *aic_button_action_name(enum aic_button_action action);

#ifdef __cplusplus
}
#endif

#endif /* __FRAMEWORK_INCLUDE_INPUT_BUTTON_H */
