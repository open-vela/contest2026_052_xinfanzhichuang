#ifndef __AIC_BLE_COMMON_H
#define __AIC_BLE_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int aic_ble_wait_bool(volatile bool *flag, unsigned int timeout_ms,
                      unsigned int poll_us);
size_t aic_ble_limited_strlen(const char *s, size_t max_len);

#endif
