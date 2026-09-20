# D13X Button Service

This module provides continuous button detection for the D133CBS board.
Direction keys are read from PSADC channel 2 on `PA.2`; `WAKEUP` is read
from the active-low `PD.15` GPIO.

## Runtime model

The button service is a single background sampler. It performs debounce,
creates `pressed` and `released` events, and publishes each event to every
subscriber. Physical keys and injected NSH keys use the same event path.

New application code should use:

```c
int aic_button_subscribe(aic_button_event_cb_t cb, void *arg);
int aic_button_service_start(void);
void aic_button_unsubscribe(aic_button_event_cb_t cb, void *arg);
```

Callbacks run in the button worker task. They must not call LVGL directly.
Use `lv_async_call()` or an application queue before updating the UI.

## Configuration

The Kconfig symbol is in:

```text
framework/input/button/Kconfig
```

Enable it from menuconfig under:

```text
AIC framework -> Input -> AIC board button API
```

The default board configuration enables:

```text
CONFIG_AIC_INPUT_BUTTON=y
CONFIG_AIC_INPUT_BUTTON_AUTOSTART=y
CONFIG_AIC_INPUT_BUTTON_EVENT_LOG=y
```

The service starts automatically after the configured delay. No `button
listen` command is required for normal detection. With event logging enabled,
each debounced physical key event is printed by the background service.

When a new defconfig is installed, the generated NuttX files must be
refreshed before compiling. In particular, `nuttx/.config` and
`nuttx/include/nuttx/config.h` must contain the `ADC_*_CENTER` symbols below;
old symbols such as `CONFIG_AIC_INPUT_BUTTON_ADC_UP_MAX` indicate that the
previous button configuration is still being used.

The current measured ADC centers are:

```text
up    960
down  560
left  1890
right 1550
```

The matching window is controlled by
`CONFIG_AIC_INPUT_BUTTON_ADC_KEY_WINDOW`.

## NSH commands

```text
button status
button thresholds
button sample
button raw 20 100
button listen 30000
button up
button down
button left
button right
button wakeup
```

The directional and wakeup commands inject one `pressed` plus one
`released` event into the service queue. They therefore exercise the same
subscriber path as physical keys.

`button listen` is only a temporary print subscriber. It does not take
ownership of the sampler and does not disable UI or other subscribers.

## Optional LVGL behavior

The core button bring-up is independent of LVGL. The demo UI integration can
be kept as a separate patch while validating and submitting the low-level
button service first.

With the LVGL patch applied, the demo UI subscribes to the button service
after the screen is created. The callback queues events and schedules an LVGL
asynchronous callback.

On the main two-column function grid:

```text
up/down   move one row
left/right move one column
wakeup    activate the focused function
```

Touch clicks keep their existing behavior. Direction keys only move focus;
they do not start a function until `wakeup` is pressed.

## Validation

After flashing a build generated from the refreshed configuration:

1. Confirm startup logs contain `button: service autostarted`,
   `button: service already running`, or the UI button subscription log.
2. Run `button status`; it should report `service=running`.
3. Run `button listen 30000`, then press each physical key once. Each key
   should produce `pressed` and `released`.
4. Run `button up`, `button down`, `button left`, `button right`, and
   `button wakeup`. The listener should print the corresponding events.
5. Without `button listen`, use the physical direction keys and verify the
   focus border moves. Press `wakeup` and verify the focused function opens.
6. If a key is not recognized, run `button raw 50 100`, record the stable
   ADC value for that key, and update the Kconfig center/window values.

Only the project defconfig should be edited in this repository. Generated
files such as NuttX `.config` and `include/nuttx/config.h` are refreshed by
the local configure/build flow.
