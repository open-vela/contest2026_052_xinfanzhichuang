#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wifi_aic8800_cmd.h"
#include "wifi_cmd_worker.h"

struct aic_wifi_mac_addr
{
  uint16_t array[3];
};

struct aic_wifi_mac_ssid
{
  uint8_t length;
  uint8_t array[AIC_WIFI_SSID_MAX_LEN];
};

struct aic_wifi_mac_chan_def
{
  uint16_t freq;
  uint8_t band;
  uint8_t flags;
  int8_t tx_power;
};

struct aic_wifi_mac_scan_result
{
  bool valid_flag;
  struct aic_wifi_mac_addr bssid;
  struct aic_wifi_mac_ssid ssid;
  uint16_t bsstype;
  struct aic_wifi_mac_chan_def *chan;
  uint16_t beacon_period;
  uint16_t cap_info;
  uint32_t akm;
  uint16_t group_cipher;
  uint16_t pairwise_cipher;
  int8_t rssi;
  uint8_t mluti_bssid_index;
  uint8_t max_bssid_indicator;
};

int fhost_get_scan_results(void *link, int result_idx, int max_nb_result,
                           struct aic_wifi_mac_scan_result *results);

struct wifiscan_args
{
  int wait_ms;
  int scan_link;
};

static int wifiscan_is_number(const char *str)
{
  if (!str || !*str)
    return 0;

  while (*str)
    {
      if (!isdigit((unsigned char)*str++))
        return 0;
    }

  return 1;
}

static int wifiscan_ssid_len(const unsigned char ssid[AIC_WIFI_SSID_MAX_LEN])
{
  int i;

  for (i = 0; i < AIC_WIFI_SSID_MAX_LEN; i++)
    {
      if (ssid[i] == '\0')
        return i;
    }

  return AIC_WIFI_SSID_MAX_LEN;
}

static unsigned int wifiscan_freq_to_channel(unsigned int freq)
{
  if (freq >= 2412 && freq <= 2472)
    {
      return (freq - 2407) / 5;
    }

  if (freq == 2484)
    {
      return 14;
    }

  if (freq >= 5000 && freq <= 5900)
    {
      return (freq - 5000) / 5;
    }

  return freq;
}

static void wifiscan_mac_to_bytes(const struct aic_wifi_mac_addr *mac,
                                  unsigned char out[6])
{
  const unsigned char *raw = (const unsigned char *)mac->array;

  memcpy(out, raw, 6);
}

static int wifiscan_read_fhost_results(wifi_ap_list_t *ap_list)
{
  struct aic_wifi_mac_scan_result result;
  int idx;
  int copied = 0;

  memset(ap_list, 0, sizeof(*ap_list));

  for (idx = 0; idx < AIC_WIFI_MAX_AP_COUNT; idx++)
    {
      wifi_ap_info_t *ap = &ap_list->ap_info[copied];
      unsigned int ssid_len;
      int ret;

      memset(&result, 0, sizeof(result));
      ret = fhost_get_scan_results(NULL, idx, 1, &result);
      if (ret <= 0)
        {
          break;
        }

      if (!result.valid_flag)
        {
          continue;
        }

      ssid_len = result.ssid.length;
      if (ssid_len > AIC_WIFI_SSID_MAX_LEN)
        {
          ssid_len = AIC_WIFI_SSID_MAX_LEN;
        }

      memcpy(ap->ssid, result.ssid.array, ssid_len);
      if (ssid_len < AIC_WIFI_SSID_MAX_LEN)
        {
          ap->ssid[ssid_len] = '\0';
        }

      wifiscan_mac_to_bytes(&result.bssid, ap->bssid);
      ap->rssi = (unsigned int)(int)result.rssi;
      ap->channel = result.chan ?
                    wifiscan_freq_to_channel(result.chan->freq) : 0;
      copied++;
    }

  ap_list->ap_count = copied;
  return copied;
}

static void wifiscan_usage(void)
{
  printf("Usage: wifiscan [--direct|--standalone] [wait_ms]\n");
  printf("Default wait_ms is 3000.\n");
  printf("Run wifistart first. Default keeps WiFi initialized.\n");
  printf("--standalone uses SCAN_OPEN/SCAN/GET_SCAN/SCAN_CLOSE and may deinit WiFi.\n");
}

static int wifiscan_worker(void *arg)
{
  const struct wifiscan_args *args = (const struct wifiscan_args *)arg;
  wifi_ap_list_t ap_list;
  int i;

  memset(&ap_list, 0, sizeof(ap_list));

  if (args->scan_link)
    {
      printf("wifiscan: scan_open\n");
      wlan_if_scan_open();
    }

  printf("wifiscan: scan start, wait %d ms\n", args->wait_ms);
  if (args->scan_link)
    {
      wlan_if_scan(NULL);
    }
  else
    {
      int ret = aic_wifi_scan_direct_start();

      if (ret < 0)
        {
          printf("wifiscan: direct scan start failed %d\n", ret);
          return ret;
        }
    }

  if (args->wait_ms > 0)
    {
      usleep((useconds_t)args->wait_ms * 1000);
    }

  wifiscan_read_fhost_results(&ap_list);
  printf("wifiscan: ap_count=%u\n", ap_list.ap_count);
  if (ap_list.ap_count == 0)
    {
      printf("wifiscan: no cached result; try longer wait or check scan done log\n");
    }

  for (i = 0; i < ap_list.ap_count && i < AIC_WIFI_MAX_AP_COUNT; i++)
    {
      wifi_ap_info_t *ap = &ap_list.ap_info[i];
      int ssid_len = wifiscan_ssid_len(ap->ssid);

      printf("wifiscan: [%02d] ch=%u rssi=%d bssid=%02x:%02x:%02x:%02x:%02x:%02x ssid=\"%.*s\"\n",
             i, ap->channel, (int)ap->rssi, ap->bssid[0], ap->bssid[1],
             ap->bssid[2], ap->bssid[3], ap->bssid[4], ap->bssid[5],
             ssid_len, ap->ssid);
    }

  if (args->scan_link)
    {
      printf("wifiscan: scan_close\n");
      wlan_if_scan_close();
    }

  printf("wifiscan: done, keep WiFi initialized\n");
  return 0;
}

int wifiscan_main(int argc, char *argv[])
{
  struct wifiscan_args args;
  int wait_ms = 3000;
  int scan_link = 0;
  int arg = 1;

  if (argc > 1 && strcmp(argv[1], "-h") == 0)
    {
      wifiscan_usage();
      return 0;
    }

  if (argc > 1 && strcmp(argv[1], "--standalone") == 0)
    {
      scan_link = 1;
      arg = 2;
    }
  else if (argc > 1 && strcmp(argv[1], "--direct") == 0)
    {
      scan_link = 0;
      arg = 2;
    }

  if (argc > arg && wifiscan_is_number(argv[arg]))
    {
      wait_ms = atoi(argv[arg]);
      if (wait_ms < 0)
        wait_ms = 0;
      if (wait_ms > 15000)
        wait_ms = 15000;
    }

  args.wait_ms = wait_ms;
  args.scan_link = scan_link;

  return wifi_cmd_run_worker("wifi_scan_cmd", 8192, wifiscan_worker, &args);
}
