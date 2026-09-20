#include <ctype.h>
#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(CONFIG_NET_IPv4) && defined(CONFIG_NETUTILS_DHCPC)
#include <netutils/netlib.h>
#endif

#include "wifi_aic8800_cmd.h"
#include "wifi_cmd_worker.h"

void set_sta_connect_bssid(uint8_t *addr);

struct wificonnect_args
{
  char ssid[64];
  char password[128];
  int timeout_ms;
};

static int wificonnect_is_number(const char *str)
{
  if (!str || !*str)
    return 0;

  if (str[0] == '-' && str[1] != '\0')
    {
      str++;
    }

  while (*str)
    {
      if (!isdigit((unsigned char)*str++))
        return 0;
    }

  return 1;
}

static void wificonnect_usage(void)
{
  printf("Usage: wificonnect <ssid> [password] [timeout_ms]\n");
  printf("Default timeout_ms is 15000. Use an empty password for open AP.\n");
}

static int wificonnect_worker(void *arg)
{
  struct wificonnect_args *args = (struct wificonnect_args *)arg;
  uint8_t *password = NULL;
  uint8_t empty_bssid[6] = {0};
  int connect_timeout = args->timeout_ms;
  int status;
  int ret;

  if (args->password[0] != '\0')
    {
      password = (uint8_t *)args->password;
    }

  if (connect_timeout >= 0)
    {
      connect_timeout =
        WLAN_CONNECT_TO_CFG_2_PARAM(WLAN_CONNECT_CFG_DISABLE_AUTO_RECONN,
                                    connect_timeout);
    }

  set_sta_connect_bssid(empty_bssid);
  ret = wlan_start_sta((uint8_t *)args->ssid, password, connect_timeout);
  status = wlan_get_connect_status();
  printf("wificonnect: wlan_start_sta ret=%d status=%d\n", ret, status);

#if defined(CONFIG_NET_IPv4) && defined(CONFIG_NETUTILS_DHCPC)
  if (ret == 0 && status != 0)
    {
      printf("wificonnect: DHCP request on wlan0\n");
      ret = netlib_obtain_ipv4addr("wlan0");
      printf("wificonnect: DHCP ret=%d\n", ret);
    }
#endif

  return ret;
}

int wificonnect_main(int argc, char *argv[])
{
  struct wificonnect_args args;
  int timeout_ms = 15000;

  if (argc < 2 || strcmp(argv[1], "-h") == 0)
    {
      wificonnect_usage();
      return argc < 2 ? 1 : 0;
    }

  memset(&args, 0, sizeof(args));
  snprintf(args.ssid, sizeof(args.ssid), "%s", argv[1]);

  if (argc > 2 && argv[2][0] != '\0')
    {
      snprintf(args.password, sizeof(args.password), "%s", argv[2]);
    }

  if (argc > 3 && wificonnect_is_number(argv[3]))
    {
      timeout_ms = atoi(argv[3]);
      if (timeout_ms < 0)
        timeout_ms = -1;
    }

  args.timeout_ms = timeout_ms;

  printf("wificonnect: ssid=\"%s\" password=%s timeout=%d ms\n",
         args.ssid, args.password[0] ? "set" : "open", timeout_ms);

  return wifi_cmd_run_worker("wifi_conn_cmd", 12288, wificonnect_worker, &args);
}
