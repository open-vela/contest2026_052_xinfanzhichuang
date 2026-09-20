/*
 * Copyright (C) 2018-2025 AICSemi Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Adapted for NuttX: use native BSD socket API
 */

#ifndef _AIC_PLAT_SOCK_H_
#define _AIC_PLAT_SOCK_H_

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

typedef socklen_t netal_socklen_t;

#endif /* _AIC_PLAT_SOCK_H_ */
