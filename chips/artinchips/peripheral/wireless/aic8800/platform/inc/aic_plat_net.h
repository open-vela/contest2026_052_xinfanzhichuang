/*
 * Copyright (C) 2018-2025 AICSemi Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * NuttX compatible version: provides struct netif and lwIP function stubs
 */

#ifndef _AIC_PLAT_NET_H_
#define _AIC_PLAT_NET_H_

#include <nuttx/config.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* err_t is lwIP error type */
#ifndef err_t
typedef int err_t;
#endif

/* Minimal lwIP struct netif compatible with driver usage */
#ifndef NETIF_MAX_HOSTNAME_LEN
#define NETIF_MAX_HOSTNAME_LEN 32
#endif

struct netif {
    struct netif *next;
    uint8_t num;
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gw;
    void *state;
    char name[2];
    uint16_t mtu;
    uint8_t hwaddr[6];
    uint8_t hwaddr_len;
    const char *hostname;
};

/* IP address types */
#ifndef ip_addr_t
typedef uint32_t ip_addr_t;
#endif

/* lwIP netif API stubs - implemented in netif_port.c */
static inline err_t netifapi_netif_set_up(struct netif *n) { (void)n; return 0; }
static inline err_t netifapi_netif_set_down(struct netif *n) { (void)n; return 0; }
static inline err_t netifapi_netif_set_default(struct netif *n) { (void)n; return 0; }
static inline err_t netif_set_addr(struct netif *n, ip_addr_t *ip, ip_addr_t *mask, ip_addr_t *gw)
{
    if (n) {
        if (ip) n->ip_addr = *ip;
        if (mask) n->netmask = *mask;
        if (gw) n->gw = *gw;
    }
    return 0;
}

/* Loopback check */
#define LWIP_HAVE_LOOPIF 1
#define LWIP_NETIF_LOOPBACK 1

#endif /* _AIC_PLAT_NET_H_ */
