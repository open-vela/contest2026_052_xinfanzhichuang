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

#include "velaclaw_compat.h"
#include <pthread.h>

/* Shared stdout mutex — prevents concurrent printf from outbound_dispatch_task
 * and cli_thread which causes adbd shell_service_uv assert. */
extern pthread_mutex_t g_stdout_lock;

/**
 * Register CLI commands (no thread spawned).
 * Safe to call before network is up.
 */
int nsh_commands_init(void);

/**
 * Execute one VelaClaw command from an NSH app invocation.
 * argv[0] is the VelaClaw subcommand name.
 */
int nsh_commands_execute(int argc, char **argv);

/**
 * Legacy compatibility hook. The application no longer reads stdin because
 * that conflicts with the system NSH console.
 */
int nsh_commands_start(void);
