#include "aic_ble_common.h"

#include <unistd.h>

int aic_ble_wait_bool(volatile bool *flag, unsigned int timeout_ms,
                      unsigned int poll_us)
{
  unsigned int waited = 0;
  unsigned int poll_ms = poll_us / 1000;

  if (poll_ms == 0)
    {
      poll_ms = 1;
    }

  while (waited <= timeout_ms)
    {
      if (*flag)
        {
          return 0;
        }

      usleep(poll_us);
      waited += poll_ms;
    }

  return -1;
}

size_t aic_ble_limited_strlen(const char *s, size_t max_len)
{
  size_t len = 0;

  while (len < max_len && s[len] != '\0')
    {
      len++;
    }

  return len;
}
