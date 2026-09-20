#include <stdio.h>

extern int wifi_start(void);

int wifistart_main(int argc, char *argv[])
{
  int ret;

  ret = wifi_start();
  printf("wifistart: %s (%d)\n", ret == 0 ? "ok" : "failed", ret);

  return ret;
}
