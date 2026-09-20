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

#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "velaclaw_compat.h"
#include "velaclaw_config.h"

#include "agent/agent_loop.h"
#include "bus/message_bus.h"
#include "bus/message_bus_tap.h"
#include "cli/nsh_commands.h"
#include "config/config_store.h"
#include "cron/cron_service.h"
#include "feishu/feishu_bot.h"
#include "gateway/ws_server.h"
#include "heartbeat/heartbeat.h"
#include "llm/llm_proxy.h"
#include "llm/llm_router.h"
#include "memory/memory_store.h"
#include "memory/session_mgr.h"
#include "mqtt/mqtt_channel.h"
#include "network/network_manager.h"
#include "node/node_client.h"
#include "node/node_manager.h"
#include "proxy/http_proxy.h"
#include "security/tool_guard.h"
#include "skills/skill_loader.h"
#include "tools/tool_media.h"
#include "tools/tool_registry.h"
#include "velaclaw_service.h"
#include "voice/voice_channel.h"
#include "weixin/weixin_channel.h"

#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
#  include "../../ui_demo/inc/ui_demo_ai.h"
#endif

static const char* TAG = "velaclaw";

/* ── stdout mutex — shared with nsh_commands.c ──────────────── */
/* Prevents concurrent printf from outbound_dispatch_task and cli_thread
 * which causes adbd shell_service_uv assert (wait_ack != 0). */
pthread_mutex_t g_stdout_lock = PTHREAD_MUTEX_INITIALIZER;

/* ── Global shutdown flag ─────────────────────────────────────── */
/* Set by cmd_quit via velaclaw_request_shutdown(); checked by main loop
 * to trigger graceful teardown instead of calling exit(). */
static volatile bool g_shutdown_requested = false;

void velaclaw_request_shutdown(void)
{
    g_shutdown_requested = true;
}

bool velaclaw_shutdown_requested(void)
{
    return g_shutdown_requested;
}

/* ── Network watcher (async) ──────────────────────────────────── */

/**
 * Runs in a background thread: waits for network, then starts all
 * network-dependent services. Main thread is NOT blocked.
 */
static void* network_watch_task(void* arg)
{
    (void)arg;
    syslog(LOG_INFO, "[%s] Network watcher started\n", TAG);
    velaclaw_service_report_starting("AI 正在等待网络连接...");

    network_wifi_reconnect();

    if (network_wait_connected(30000) == OK) {
        int agent_ret;
        char ready_msg[VELACLAW_SERVICE_MESSAGE_LEN];

        syslog(LOG_INFO, "[%s] Network connected: %s\n", TAG, network_get_ip());

        if (feishu_bot_start() != OK)
            syslog(LOG_WARNING, "[%s] feishu_bot_start failed\n", TAG);
        agent_ret = agent_loop_start();
        if (agent_ret != OK)
            syslog(LOG_WARNING, "[%s] agent_loop_start failed\n", TAG);
        if (ws_server_start() != OK)
            syslog(LOG_WARNING, "[%s] ws_server_start failed\n", TAG);
        if (node_client_start() != OK)
            syslog(LOG_WARNING, "[%s] node_client_start failed\n", TAG);
        if (mqtt_channel_start() != OK)
            syslog(LOG_WARNING, "[%s] mqtt_channel_start failed\n", TAG);
        if (weixin_channel_start() != OK)
            syslog(LOG_WARNING, "[%s] weixin_channel_start failed\n", TAG);

        syslog(LOG_INFO, "[%s] All network services started!\n", TAG);
        if (agent_ret == OK) {
            snprintf(ready_msg, sizeof(ready_msg), "AI 连接成功：%s",
                network_get_ip());
            velaclaw_service_report_ready(ready_msg);
        } else {
            velaclaw_service_report_error(
                "AI 连接失败：Agent 任务启动失败，请查看日志");
        }
    } else {
        syslog(LOG_WARNING, "[%s] Network timeout — net services not started.\n", TAG);
        syslog(LOG_WARNING, "[%s] Use 'config_show' / 'set_*' CLI commands to configure.\n", TAG);
        velaclaw_service_report_error(
            "AI 连接失败：WiFi 未连接或未获取 IP");
    }

    return NULL;
}

/* ── Outbound dispatch ────────────────────────────────────────── */

/**
 * Reads messages from the outbound queue and dispatches them to the
 * appropriate channel (飞书, WebSocket, CLI, etc.).
 */
static void* outbound_dispatch_task(void* arg)
{
    (void)arg;
    syslog(LOG_INFO, "[%s] Outbound dispatch started\n", TAG);

    while (!g_shutdown_requested) {
        velaclaw_msg_t msg;
        if (message_bus_pop_outbound(&msg, 1000) != OK)
            continue;
        msg.channel[sizeof(msg.channel) - 1] = '\0';
        msg.chat_id[sizeof(msg.chat_id) - 1] = '\0';

        if (!msg.content) {
            syslog(LOG_WARNING, "[%s] Skip outbound message with NULL content\n", TAG);
            continue;
        }

        syslog(LOG_INFO, "[%s] Dispatching response → %s:%s\n", TAG, msg.channel, msg.chat_id);

        /* Let registered taps intercept before normal dispatch */
        if (mbus_tap_try_deliver(&msg)) {
            free(msg.content);
            continue;
        }

        if (strcmp(msg.channel, VELACLAW_CHAN_FEISHU) == 0) {
            /* Try local feishu bot first — this is the common case for
             * standalone bots.  Only forward via gateway (node_client)
             * when local feishu is not configured. */
            const char* app_id = feishu_get_app_id();
            if (app_id && app_id[0] != '\0') {
                feishu_send_message(msg.chat_id, msg.content);
            } else if (node_client_send_chat_message(msg.channel,
                           msg.chat_id, msg.content)
                != OK) {
                syslog(LOG_WARNING,
                    "[%s] Feishu message dropped: no local config "
                    "and no gateway\n",
                    TAG);
            }
        } else if (strcmp(msg.channel, VELACLAW_CHAN_WEBSOCKET) == 0) {
            ws_server_send(msg.chat_id, msg.content);
        } else if (strcmp(msg.channel, VELACLAW_CHAN_MQTT) == 0) {
            mqtt_channel_send(msg.chat_id, msg.content);
        } else if (strcmp(msg.channel, VELACLAW_CHAN_VOICE) == 0) {
            if (!voice_channel_accept_response(msg.chat_id)) {
                free(msg.content);
                continue;
            }
#ifdef CONFIG_EXAMPLES_VELACLAW_VELA
            ui_demo_ai_set_reply(msg.content);
#endif
            int vret = voice_channel_speak(msg.content);
            if (vret != 0) {
                syslog(LOG_ERR, "[%s] voice_channel_speak failed: %d\n", TAG, vret);
            }
        } else if (strcmp(msg.channel, VELACLAW_CHAN_WEIXIN) == 0) {
            /* chat_id format: "from_user_id|context_token" */
            char uid[64] = "";
            const char *ctx = "";
            char *sep = strchr(msg.chat_id, '|');
            if (sep) {
                size_t ulen = (size_t)(sep - msg.chat_id);
                if (ulen >= sizeof(uid)) ulen = sizeof(uid) - 1;
                memcpy(uid, msg.chat_id, ulen);
                uid[ulen] = '\0';
                ctx = sep + 1;
            } else {
                strncpy(uid, msg.chat_id, sizeof(uid) - 1);
            }
            weixin_channel_send(uid, ctx, msg.content);
        } else if (strcmp(msg.channel, VELACLAW_CHAN_SYSTEM) == 0) {
            syslog(LOG_INFO, "[%s] System message [%s]: %.128s\n", TAG, msg.chat_id, msg.content);
        } else if (strcmp(msg.channel, "cli") == 0) {
            pthread_mutex_lock(&g_stdout_lock);
            printf("\n[Agent]: %s\nvela> ", msg.content);
            fflush(stdout);
            pthread_mutex_unlock(&g_stdout_lock);
        } else {
            syslog(LOG_WARNING, "[%s] Unknown channel: %s\n", TAG, msg.channel);
        }

        free(msg.content);
    }

    syslog(LOG_INFO, "[%s] Outbound dispatch exiting\n", TAG);
    return NULL;
}

/* ── Entry point ──────────────────────────────────────────────── */

/* ── Startup timing helpers ───────────────────────────────────── */

static inline long boot_ms(struct timespec* t0)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long)((now.tv_sec - t0->tv_sec) * 1000L + (now.tv_nsec - t0->tv_nsec) / 1000000L);
}

#define BOOT_LOG(t0, phase, msg) \
    syslog(LOG_INFO, "[%s] [boot +%ldms] " phase ": " msg "\n", TAG, boot_ms(t0))

#define BOOT_LOG_RC(t0, phase, msg, rc) \
    syslog(LOG_INFO, "[%s] [boot +%ldms] " phase ": " msg " (rc=%d)\n", TAG, boot_ms(t0), (rc))

int velaclaw_main(int argc, char* argv[])
{
    /*
     * The system NSH is the only owner of the console. Configuration and
     * diagnostics are exposed as normal builtin-app subcommands:
     *   velaclaw set_llm mimo <key>
     */
    if (argc > 1 && argv != NULL && argv[1] != NULL) {
        int rc;

        rc = config_store_init();
        if (rc != OK) {
            printf("VelaClaw config init failed: %d\n", rc);
            return rc;
        }

        (void)llm_proxy_init();
        (void)llm_router_init();
        (void)voice_channel_init();
        return nsh_commands_execute(argc - 1, argv + 1);
    }

    g_shutdown_requested = false;
    velaclaw_service_report_starting("AI 正在初始化 VelaClaw...");

    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    syslog(LOG_INFO, "[%s] ========================================\n", TAG);
    syslog(LOG_INFO, "[%s]   VelaClaw - Vela AI Agent\n", TAG);
    syslog(LOG_INFO, "[%s] ========================================\n", TAG);
    syslog(LOG_INFO, "[%s] [boot +0ms] startup timer armed\n", TAG);

    /* ── Phase 0: Timezone ──────────────────────────────────── */
    /* Avoid TZ=CST-8 entirely: NuttX libc may lazily probe romfs zoneinfo and
     * log ERROR lines. VelaClaw formats UTC+8 timestamps manually. */
    unsetenv("TZ");
    BOOT_LOG(&t0, "P0", "timezone env cleared");

    /* ── Phase 0: Storage Bootstrapping (Auto-mount & Mkdir) ── */

    struct stat st;
    if (stat("/data", &st) != 0) {
        syslog(LOG_INFO, "[%s] Mounting /data as tmpfs for simulation...\n", TAG);
        mount(NULL, "/data", "tmpfs", 0, NULL);
    }

    /* Ensure directory structure exists (No more manual mkdir needed!) */
    mkdir("/data/velaclaw", 0755);
    mkdir("/data/velaclaw/config", 0755);
    mkdir("/data/velaclaw/memory", 0755);
    mkdir("/data/velaclaw/sessions", 0755);
    mkdir("/data/velaclaw/skills", 0755);
    BOOT_LOG(&t0, "P0", "storage ready");

    /* Memory info */
    {
        struct mallinfo mi = mallinfo();
        syslog(LOG_INFO, "[%s] [boot +%ldms] heap: arena=%d free=%d used=%d\n",
            TAG, boot_ms(&t0), mi.arena, mi.fordblks, mi.uordblks);
    }

    /* ── Phase 1: Core infrastructure ──────────────────────── */
    {
        int rc;
        rc = config_store_init();
        BOOT_LOG_RC(&t0, "P1", "config_store_init", rc);
        if (rc != OK)
            syslog(LOG_WARNING, "[%s] config_store_init failed; continuing with defaults\n", TAG);

        rc = message_bus_init();
        BOOT_LOG_RC(&t0, "P1", "message_bus_init", rc);
        if (rc != OK) {
            syslog(LOG_ERR, "[%s] message_bus_init failed\n", TAG);
            velaclaw_service_report_error(
                "AI 启动失败：消息总线初始化失败");
            return -1;
        }

        rc = memory_store_init();
        BOOT_LOG_RC(&t0, "P1", "memory_store_init", rc);
        if (rc != OK)
            syslog(LOG_WARNING, "[%s] memory_store_init failed\n", TAG);

        rc = session_mgr_init();
        BOOT_LOG_RC(&t0, "P1", "session_mgr_init", rc);
        if (rc != OK)
            syslog(LOG_WARNING, "[%s] session_mgr_init failed\n", TAG);
    }

    /* ── Phase 2: Proxy / networking ───────────────────────── */
    {
        int rc = http_proxy_init();
        BOOT_LOG_RC(&t0, "P2", "http_proxy_init", rc);
        if (rc != OK)
            syslog(LOG_WARNING, "[%s] http_proxy_init failed; proxy disabled\n", TAG);
    }

    /* ── Phase 3: Application services ─────────────────────── */
    {
        int rc;
        rc = feishu_bot_init();
        BOOT_LOG_RC(&t0, "P3", "feishu_bot_init", rc);
        if (rc != OK)
            syslog(LOG_WARNING, "[%s] feishu_bot_init failed\n", TAG);

        rc = llm_proxy_init();
        BOOT_LOG_RC(&t0, "P3", "llm_proxy_init", rc);
        if (rc != OK)
            syslog(LOG_WARNING, "[%s] llm_proxy_init failed\n", TAG);

        rc = llm_router_init();
        BOOT_LOG_RC(&t0, "P3", "llm_router_init", rc);
        if (rc != OK) syslog(LOG_WARNING, "[%s] llm_router_init failed\n", TAG);

        rc = tool_registry_init();
        BOOT_LOG_RC(&t0, "P3", "tool_registry_init", rc);
        if (rc != OK)
            syslog(LOG_WARNING, "[%s] tool_registry_init failed\n", TAG);

        rc = tool_guard_init();
        BOOT_LOG_RC(&t0, "P3", "tool_guard_init", rc);
        if (rc != OK)
            syslog(LOG_WARNING, "[%s] tool_guard_init failed\n", TAG);

        /* Skills must init AFTER tool_registry so executable skills
         * can register themselves as tools. */
        rc = skill_loader_init();
        BOOT_LOG_RC(&t0, "P3", "skill_loader_init", rc);
        if (rc != OK)
            syslog(LOG_WARNING, "[%s] skill_loader_init failed\n", TAG);

        /* skill_register_tools() not available in this build */

        rc = agent_loop_init();
        BOOT_LOG_RC(&t0, "P3", "agent_loop_init", rc);
        if (rc != OK)
            syslog(LOG_WARNING, "[%s] agent_loop_init failed\n", TAG);

        rc = cron_service_init();
        BOOT_LOG_RC(&t0, "P3", "cron_service_init", rc);
        if (rc != OK)
            syslog(LOG_WARNING, "[%s] cron_service_init failed\n", TAG);

        rc = heartbeat_init();
        BOOT_LOG_RC(&t0, "P3", "heartbeat_init", rc);
        if (rc != OK)
            syslog(LOG_WARNING, "[%s] heartbeat_init failed\n", TAG);

        rc = node_manager_init();
        BOOT_LOG_RC(&t0, "P3", "node_manager_init", rc);
        if (rc != OK)
            syslog(LOG_WARNING, "[%s] node_manager_init failed\n", TAG);

        rc = node_client_init();
        BOOT_LOG_RC(&t0, "P3", "node_client_init", rc);
        if (rc != OK)
            syslog(LOG_WARNING, "[%s] node_client_init failed\n", TAG);

        rc = mqtt_channel_init();
        BOOT_LOG_RC(&t0, "P3", "mqtt_channel_init", rc);
        if (rc != OK)
            syslog(LOG_WARNING, "[%s] mqtt_channel_init failed\n", TAG);

        rc = voice_channel_init();
        BOOT_LOG_RC(&t0, "P3", "voice_channel_init", rc);
        if (rc != OK) syslog(LOG_WARNING, "[%s] voice_channel_init failed\n", TAG);

        rc = weixin_channel_init();
        BOOT_LOG_RC(&t0, "P3", "weixin_channel_init", rc);
        if (rc != OK) syslog(LOG_WARNING, "[%s] weixin_channel_init failed\n", TAG);
    }

    /* ── Phase 4: CLI — register commands only, no thread yet ── */
    {
        int rc = nsh_commands_init();
        BOOT_LOG_RC(&t0, "P4", "nsh_commands_init", rc);
        if (rc != OK)
            syslog(LOG_WARNING, "[%s] nsh_commands_init failed\n", TAG);
    }

    /* ── Phase 5: Network — async, does NOT block ready ────── */

    /* Kick off WiFi reconnect and start outbound dispatch immediately.
     * Network-dependent services (feishu, agent, ws, node) are started
     * inside the network_watch thread once the link comes up. */

    /* Outbound dispatch thread — start before network so queued messages
     * are drained even during the connection window. */
    if (velaclaw_task_create(outbound_dispatch_task, "outbound",
            VELACLAW_OUTBOUND_STACK, NULL,
            VELACLAW_OUTBOUND_PRIO)
        != OK) {
        syslog(LOG_ERR, "[%s] Failed to start outbound dispatch thread\n", TAG);
        velaclaw_service_report_error(
            "AI 启动失败：响应分发任务创建失败");
        return -1;
    }
    BOOT_LOG(&t0, "P5", "outbound dispatch thread started");

    /* Cron + heartbeat don't need network */
    cron_service_start();
    heartbeat_start();
    BOOT_LOG(&t0, "P5", "cron + heartbeat started");

    /* Network watcher thread: reconnect + wait + start net services */
    if (velaclaw_task_create(network_watch_task, "net_watch",
            VELACLAW_OUTBOUND_STACK, NULL,
            VELACLAW_OUTBOUND_PRIO)
        != OK) {
        syslog(LOG_WARNING, "[%s] Failed to start network_watch thread\n", TAG);
    }
    BOOT_LOG(&t0, "P5", "network_watch thread started (async)");

    /* ── Phase 6: CLI compatibility hook ───────────────────── */
    {
        int rc = nsh_commands_start();
        BOOT_LOG_RC(&t0, "P6", "nsh_commands_start", rc);
        if (rc != OK)
            syslog(LOG_WARNING, "[%s] nsh_commands_start failed\n", TAG);
    }

    syslog(LOG_INFO, "[%s] [boot +%ldms] VelaClaw ready. Type 'help' in NSH for commands.\n",
        TAG, boot_ms(&t0));

    /* Block main thread until shutdown is requested */
    while (!g_shutdown_requested) {
        sleep(1);
    }

    syslog(LOG_INFO, "[%s] Shutdown requested — stopping services...\n", TAG);

    /* Wake all threads blocked on message bus before stopping services */
    message_bus_wakeup();

    /* ── Graceful teardown (reverse of startup order) ──────── */

    /* Phase 5 services (network-dependent) */
    weixin_channel_stop();
    node_client_stop();
    mqtt_channel_stop();
    ws_server_stop();
    /* feishu_bot and agent_loop have no _stop(); they will exit
     * naturally once their blocking I/O returns an error after
     * the network sockets are closed below, or they can check
     * velaclaw_shutdown_requested() in their loops. */

    /* Phase 5 services (non-network) */
    cron_service_stop();
    heartbeat_stop();

    /* Give threads a moment to notice and exit */
    usleep(500 * 1000);

    /* Cleanup */
    tool_media_cleanup();
    message_bus_destroy();

    syslog(LOG_INFO, "[%s] Shutdown complete.\n", TAG);
    velaclaw_service_report_stopped("AI 已退出");

    return 0;
}
