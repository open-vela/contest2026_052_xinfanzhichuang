#include <stdio.h>

#include "wifi_aic8800_cmd.h"

int wifistatus_main(int argc, char *argv[])
{
  int status;

  status = wlan_get_connect_status();
  printf("wifistatus: status=%d\n", status);

  return 0;
}
