/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "network_manager.h"
#include "velaclaw_compat.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static const char* TAG = "netmgr";

static char s_ip_str[INET_ADDRSTRLEN] = "0.0.0.0";

bool network_is_connected(void)
{
    struct ifaddrs* ifa_list = NULL;
    if (getifaddrs(&ifa_list) == 0) {
        for (struct ifaddrs* ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr)
                continue;
            if (ifa->ifa_addr->sa_family != AF_INET)
                continue;
            if (ifa->ifa_name && strncmp(ifa->ifa_name, "lo", 2) == 0)
                continue;

            struct sockaddr_in* sin = (struct sockaddr_in*)ifa->ifa_addr;
            uint32_t addr = ntohl(sin->sin_addr.s_addr);
            if ((addr >> 24) == 127)
                continue; /* 127.x.x.x loopback */

            /* Skip 0.0.0.0 — interface exists but has no IP yet */
            if (addr == 0)
                continue;

            inet_ntop(AF_INET, &sin->sin_addr, s_ip_str, sizeof(s_ip_str));
            syslog(LOG_INFO, "[%s] Found iface %s addr %s\n", TAG,
                ifa->ifa_name ? ifa->ifa_name : "?", s_ip_str);
            freeifaddrs(ifa_list);
            return true;
        }
        freeifaddrs(ifa_list);
    }

    return false;
}

int network_wait_connected(uint32_t timeout_ms)
{
    if (timeout_ms == 0) {
        return network_is_connected() ? OK : ERROR;
    }

    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += (time_t)(timeout_ms / 1000);
    deadline.tv_nsec += (long)((timeout_ms % 1000) * 1000000L);
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    while (1) {
        if (network_is_connected()) {
            syslog(LOG_INFO, "[%s] Network connected: %s\n", TAG, s_ip_str);
            return OK;
        }

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        if (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
            syslog(LOG_WARNING, "[%s] Timed out waiting for network\n", TAG);
            return ERROR;
        }

        usleep(500000); /* poll every 500 ms */
    }
}

const char* network_get_ip(void)
{
    network_is_connected(); /* refresh */
    return s_ip_str;
}

/* ── WiFi connect (real hardware only) ───────────────────────── */

#if defined(CONFIG_ARCH_CHIP_GOLDFISH_ARM64) || defined(CONFIG_ARCH_CHIP_QEMU_ARM)
/* On QEMU, virtio-net provides connectivity automatically — but needs
 * ifup/renew */
#include <net/if.h>
#include <nuttx/net/netconfig.h>
#include <sys/ioctl.h>

int network_wifi_connect(const char* iface, const char* ssid,
    const char* pass)
{
    (void)iface;
    (void)ssid;
    (void)pass;
    syslog(LOG_INFO,
        "[%s] QEMU: skipping wifi_connect (virtio-net handles networking)\n",
        TAG);
    return OK;
}

int network_wifi_reconnect(void)
{
    syslog(LOG_INFO, "[%s] QEMU: Initializing eth0...\n", TAG);

    /* Bring up eth0 interface */
    int ret = system("ifup eth0");
    if (ret != 0) {
        syslog(LOG_WARNING, "[%s] ifup eth0 failed: %d\n", TAG, ret);
    }

    /* Request DHCP lease */
    ret = system("renew eth0");
    if (ret != 0) {
        syslog(LOG_WARNING, "[%s] renew eth0 failed: %d\n", TAG, ret);
    }

    /* Wait a bit for network to come up */
    usleep(500000);

    return network_wait_connected(5000);
}

#else
/* Real hardware: use NuttX wapi shell command to join WiFi */
#include "config/config_store.h"
#include "velaclaw_config.h"
#include <stdlib.h>

int network_wifi_connect(const char* iface, const char* ssid,
    const char* pass)
{
    if (!ssid || ssid[0] == '\0') {
        syslog(LOG_ERR, "[%s] wifi_connect: SSID required\n", TAG);
        return ERROR;
    }

    const char* dev = (iface && iface[0]) ? iface : "wlan0";
    char cmd[320];

    syslog(LOG_INFO, "[%s] Connecting WiFi SSID=%s on %s\n", TAG, ssid, dev);

    snprintf(cmd, sizeof(cmd), "ifup %s", dev); /* bring up iface first */
    system(cmd);
    usleep(500000); /* wait for driver ready */

    snprintf(cmd, sizeof(cmd), "wapi mode %s 2", dev); /* MANAGED */
    system(cmd);

    if (pass && pass[0]) {
        snprintf(cmd, sizeof(cmd), "wapi psk %s %s 3", dev, pass);
        system(cmd);
    }

    snprintf(cmd, sizeof(cmd), "wapi essid %s %s 1", dev, ssid);
    system(cmd);
    usleep(2000000); /* wait for AP association */

    /* Verify WiFi association before requesting DHCP.
     * If the interface has no carrier (association failed), calling
     * renew triggers a DHCP client task that panics during TLS
     * cleanup when the underlying socket fails immediately.
     */

    snprintf(cmd, sizeof(cmd), "wapi status %s", dev);
    int assoc_ret = system(cmd);
    if (assoc_ret != 0)
      {
        syslog(LOG_ERR,
               "[%s] WiFi association failed (ret=%d), skipping DHCP\n",
               TAG, assoc_ret);
        return ERROR;
      }

    snprintf(cmd, sizeof(cmd), "renew %s", dev); /* DHCP */
    system(cmd);

    /* Persist credentials */
    velaclaw_config_set(VELACLAW_CFG_KEY_WIFI_SSID, ssid);
    if (pass && pass[0])
        velaclaw_config_set(VELACLAW_CFG_KEY_WIFI_PASS, pass);

    int err = network_wait_connected(15000);
    if (err == OK)
        syslog(LOG_INFO, "[%s] WiFi connected: %s\n", TAG, network_get_ip());
    else
        syslog(LOG_WARNING, "[%s] No IP after 15s — check SSID/password\n", TAG);
    return err;
}

int network_wifi_reconnect(void)
{
    char ssid[64] = { 0 };
    char pass[128] = { 0 };
    if (velaclaw_config_get(VELACLAW_CFG_KEY_WIFI_SSID, ssid, sizeof(ssid)) != OK || !ssid[0]) {
        syslog(LOG_WARNING,
            "[%s] No saved WiFi credentials. Use CLI: set_wifi <ssid> <pass>\n",
            TAG);
        return ERROR;
    }
    velaclaw_config_get(VELACLAW_CFG_KEY_WIFI_PASS, pass, sizeof(pass));
    return network_wifi_connect(NULL, ssid, pass);
}
#endif
