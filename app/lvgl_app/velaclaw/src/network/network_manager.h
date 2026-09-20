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

#pragma once
/**
 * network_manager.h — Vela network status helper
 *
 * managed by the system (netinit / board BSP).  This module simply
 * waits for any non-loopback interface to get an IPv4 address.
 */

#include "velaclaw_compat.h"

/** Check whether the system has a routable IPv4 address. */
bool network_is_connected(void);

/**
 * Block until connected (or timeout).
 * @param timeout_ms  0 = check once, UINT32_MAX = wait forever.
 * @return OK on success, ERROR otherwise.
 */
int network_wait_connected(uint32_t timeout_ms);

/** Return a static string with the first non-loopback IPv4 address. */
const char *network_get_ip(void);

/**
 * Connect to a WiFi network using the NuttX wapi tool.
 * On QEMU this is a no-op (network is provided by virtio-net).
 * Saves credentials to config_store for persistence across reboots.
 * @param iface  WiFi interface name e.g. "wlan0" (NULL = use "wlan0")
 */
int network_wifi_connect(const char *iface, const char *ssid, const char *pass);

/** Re-connect using credentials saved in config_store (called at startup). */
int network_wifi_reconnect(void);
