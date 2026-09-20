#include <stdio.h>

#include "wifi_aic8800_cmd.h"
#include "wifi_cmd_worker.h"

static int wifidisconnect_worker(void *arg)
{
  (void)arg;
  return wlan_disconnect_sta(0);
}

int wifidisconnect_main(int argc, char *argv[])
{
  int ret;

  ret = wifi_cmd_run_worker("wifi_disc_cmd", 4096,
                            wifidisconnect_worker, NULL);
  printf("wifidisconnect: %s (%d)\n", ret == 0 ? "ok" : "failed", ret);

  return ret;
}
