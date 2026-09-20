#include <stdio.h>

extern int wifi_stop(void);

int wifistop_main(int argc, char *argv[])
{
  int ret;

  ret = wifi_stop();
  printf("wifistop: %s (%d)\n", ret == 0 ? "ok" : "failed", ret);

  return ret;
}
