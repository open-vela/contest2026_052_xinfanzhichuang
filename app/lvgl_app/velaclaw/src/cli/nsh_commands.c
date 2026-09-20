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

#include "cli/nsh_commands.h"
#include "bus/message_bus.h"
#include "config/config_store.h"
#include "cron/cron_service.h"
#include "feishu/feishu_bot.h"
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
#include "tls/vela_tls.h"
#include "tools/tool_get_time.h"
#include "tools/tool_proxyquickapp.h"
#include "tools/tool_web_search.h"
#include "ui/qrcode_display.h"
#include "velaclaw_compat.h"
#include "velaclaw_config.h"
#include "voice/voice_asr.h"
#include "voice/voice_channel.h"
#include "voice/voice_tts.h"
#include "weixin/weixin_channel.h"

#ifdef CONFIG_VELACLAW_TEST
#include "test_vision_integ.h"
#endif

#include <cJSON.h>
#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef CONFIG_BOARDCTL_RESET
#include <sys/boardctl.h>
#endif

static const char* TAG = "cli";

/* ── Helpers ──────────────────────────────────────────────────── */

static void cmd_net_test(void);
static void cmd_net_status(void);
static void cmd_set_feishu_app(int argc, char** argv);
static void cmd_set_feishu_user_token(int argc, char** argv);
static void cmd_set_llm(int argc, char** argv);
static void cmd_memory_read(void);
static void cmd_memory_write(int argc, char** argv);
static void cmd_session_list(void);
static void cmd_session_clear(int argc, char** argv);
static void cmd_session_clear_all(void);
static void cmd_heap_info(void);
static void cmd_set_proxy(int argc, char** argv);
static void cmd_clear_proxy(void);
static void cmd_set_wifi(int argc, char** argv);
static void cmd_wifi_reconnect(void);
static void cmd_set_search_key(int argc, char** argv);
static void cmd_set_exa_key(int argc, char** argv);
static void cmd_set_tavily_key(int argc, char** argv);
static void cmd_set_news_key(int argc, char** argv);
static void cmd_config_show(void);
static void cmd_config_reset(void);
static void cmd_restart(void);
static void cmd_heartbeat_trigger(void);
static void cmd_ask(int argc, char** argv);
static void cmd_cron_start(void);
static void cmd_node_list(void);
static void cmd_set_gateway(int argc, char** argv);
static void cmd_node_start(void);
static void cmd_node_stop(void);
static void cmd_set_mqtt(int argc, char** argv);
static void cmd_list_models(int argc, char** argv);
static void cmd_quit(void);
static void cmd_launch_app(int argc, char** argv);
static void cmd_exit_app(void);
static void cmd_router_status(void);
static void cmd_router_profile(int argc, char** argv);
static void cmd_router_set(int argc, char** argv);
static void cmd_router_clear(int argc, char** argv);
static void cmd_router_model(int argc, char** argv);
static void cmd_set_volc_key(int argc, char** argv);
static void cmd_set_volc_speaker(int argc, char** argv);
static void cmd_set_volc_asr(int argc, char** argv);
static void cmd_voice_start(void);
static void cmd_voice_stop(void);
static void cmd_voice_test_tts(int argc, char** argv);
static void cmd_voice_test_asr(int argc, char** argv);
static void cmd_set_voice_tts(int argc, char** argv);
static void cmd_set_voice_asr(int argc, char** argv);
static void cmd_set_weixin_token(int argc, char** argv);
static void cmd_weixin_login(void);
#ifdef CONFIG_VELACLAW_TEST
static void cmd_claw_test(int argc, char** argv);
#endif

static void cmd_help(void)
{
    printf(
        "Usage from NSH: velaclaw <command> [args]\n"
        "Examples:\n"
        "  velaclaw set_llm mimo <api_key>\n"
        "  velaclaw set_voice_asr mimo\n"
        "  velaclaw config_show\n"
        "\n"
        "Available commands:\n"
        "  net_status           - Show network connection status\n"
        "  net_test             - Test HTTPS connection to Baidu\n"
        "  set_feishu_app <app_id> <app_secret>  - Set Feishu app credentials\n"
        "  set_feishu_user_token <token>  - Set Feishu user_access_token for doc APIs\n"
        "  set_llm <preset|host> [model] [key] - Switch LLM backend (kimi/qwen/deepseek/glm/openai/mimo)\n"
        "  list_models [--free] [keyword] - List available models (openrouter)\n"
        "  memory_read          - Read MEMORY.md\n"
        "  memory_write <text>  - Write MEMORY.md (quote text)\n"
        "  session_list         - List sessions\n"
        "  session_clear <id>   - Clear a session\n"
        "  session_clear_all    - Clear all sessions\n"
        "  heap_info            - Show memory usage\n"
        "  set_proxy <host> <port> - Set HTTP proxy\n"
        "  clear_proxy          - Remove proxy config\n"
        "  set_wifi <ssid> <pw> - Connect to WiFi (real hardware; saved for reboot)\n"
        "  wifi_reconnect       - Re-join WiFi using saved credentials\n"
        "  set_search_key <key> - Set SerpAPI (Google) key\n"
        "  set_exa_key <key>    - Set Exa AI search key\n"
        "  set_tavily_key <key> - Set Tavily search key\n"
        "  set_news_key <key>   - Set NewsAPI key\n"
        "  set_tavily_key <key> - Set Tavily AI search key\n"
        "  config_show          - Show current configuration\n"
        "  config_reset         - Clear all runtime config\n"
        "  ask <text>           - Chat with AI Agent\n"
        "  heartbeat_trigger    - Manually trigger heartbeat check\n"
        "  cron_start           - Start cron scheduler\n"
        "  node_list            - List connected remote Nodes\n"
        "  set_gateway <host> [port] [token] - Set OpenClaw Gateway\n"
        "  node_start          - Connect to OpenClaw Gateway as Node\n"
        "  node_stop           - Disconnect from OpenClaw Gateway\n"
        "  restart              - Restart the device\n"
        "  quit                 - Exit velaclaw\n"
        "  set_mqtt <broker> [client_id] - Set MQTT broker (host:port)\n"
        "  set_volc_key <api_key>       - Set Doubao voice API key\n"
        "  set_volc_asr <id> <tok> <cluster> - Set ASR credentials\n"
        "  set_volc_speaker <id>  - Set TTS voice (e.g. zh_female_cancan)\n"
        "  voice_start            - Start voice channel\n"
        "  voice_stop             - Stop voice channel\n"
        "  voice_test_tts <text> [out.pcm] - Test TTS synthesis\n"
        "  voice_test_asr <file>  - Test ASR recognition\n"
        "  set_voice_tts <name>   - Switch TTS backend\n"
        "  set_voice_asr <name>   - Switch ASR backend\n"
        "  set_weixin_token <tok> - Set WeChat bot token\n"
        "  weixin_login           - QR code login to WeChat\n"
        "  router_status          - Show LLM router status\n"
        "  router_set <preset> <key> - Add LLM backend (deepseek/kimi/qwen/openai...)\n"
        "  router_model <idx> <model> - Change model for a backend\n"
        "  router_profile <p>     - Set routing profile (eco/auto/premium)\n"
        "  router_clear [idx]     - Clear router backends\n"
        "  launch_app <pkg>     - Test launch a QuickApp by package name\n"
        "  exit_app             - Exit current QuickApp and go home\n"
#ifdef CONFIG_VELACLAW_TEST
        "  claw_test [V-XX]    - Run vision integration tests\n"
#endif
        "  help                 - This message\n");
}

/* ── Router presets (shared by set_llm and router_set) ─────────── */

typedef struct {
    const char* name;
    const char* host;
    const char* path;
    const char* model;
    int cost_tier;
} router_preset_t;

static const router_preset_t g_router_presets[] = {
    { "deepseek", "api.deepseek.com", "/v1/chat/completions", "deepseek-chat", 1 },
    { "kimi", "api.moonshot.cn", "/v1/chat/completions", "kimi-k2.5", 2 },
    { "qwen", "dashscope.aliyuncs.com", "/compatible-mode/v1/chat/completions", "qwen-turbo", 1 },
    { "glm", "open.bigmodel.cn", "/api/paas/v4/chat/completions", "glm-4-flash", 1 },
    { "openai", "api.openai.com", "/v1/chat/completions", "gpt-4o", 3 },
    { "claude", "api.anthropic.com", "/v1/messages", "claude-sonnet-4-20250514", 3 },
    { "mimo", VELACLAW_LLM_MIMO_HOST, VELACLAW_LLM_MIMO_PATH, VELACLAW_LLM_MIMO_MODEL, 2 },
    { "openrouter", "openrouter.ai", "/api/v1/chat/completions", "openrouter/hunter-alpha", 2 },
    { NULL, NULL, NULL, NULL, 0 }
};

/* ── Command implementations ──────────────────────────────────── */

static void cmd_net_test(void)
{
    char resp[1024];
    printf("Testing HTTPS to www.baidu.com...\n");
    int status = vela_https_get("www.baidu.com", "443", "/", resp, sizeof(resp));
    if (status > 0) {
        printf("SUCCESS! HTTP Status: %d\n", status);
    } else {
        printf("FAILED! TLS Error: 0x%x\n", -status);
    }
}

static void cmd_net_status(void)
{
    printf("Network connected: %s\n", network_is_connected() ? "yes" : "no");
    printf("IP: %s\n", network_get_ip());
}

static void cmd_set_feishu_app(int argc, char** argv)
{
    if (argc < 3) {
        printf("Usage: set_feishu_app <app_id> <app_secret>\n");
        return;
    }
    feishu_set_app(argv[1], argv[2]);
    printf("Feishu app credentials saved (app_id=%s).\n", argv[1]);
}

static void cmd_set_feishu_user_token(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: set_feishu_user_token <token>\n");
        return;
    }
    feishu_set_user_token(argv[1]);
    printf("Feishu user_access_token saved (%.8s...).\n", argv[1]);
}

static void cmd_set_llm(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: velaclaw set_llm <preset> [api_key]\n"
               "       velaclaw set_llm <url> <model> [api_key]\n"
               "       velaclaw set_llm <host> <model> [api_key]\n"
               "\n"
               "Presets:\n"
               "  kimi     - api.moonshot.cn  (kimi-k2.5)\n"
               "  qwen     - dashscope.aliyuncs.com  (qwen-turbo)\n"
               "  deepseek - api.deepseek.com  (deepseek-chat)\n"
               "  glm      - open.bigmodel.cn  (glm-4-flash)\n"
               "  openai   - api.openai.com  (gpt-4o)\n"
               "  openrouter - openrouter.ai  (openrouter/hunter-alpha)\n"
               "  mimo     - token-plan-cn.xiaomimimo.com  (mimo-v2.5)\n"
               "\n"
               "URL format (for custom endpoints):\n"
               "  velaclaw set_llm http://host:port/path model key\n"
               "  velaclaw set_llm http://<your-endpoint>/v1 model-name sk-xxx\n"
               "\n"
               "Note: set_llm writes to router slot 0 and applies immediately.\n");
        return;
    }

    const char* arg1 = argv[1];

    if (strcmp(arg1, "model") == 0) {
        if (argc < 3) {
            printf("Usage: velaclaw set_llm model <name>\n");
            return;
        }

        llm_backend_t old;
        if (llm_router_get_backend(0, &old) == 0 && old.host[0]) {
            strncpy(old.model, argv[2], sizeof(old.model) - 1);
            old.model[sizeof(old.model) - 1] = '\0';
            llm_router_set_backend(0, &old);
            llm_router_apply(0);
        } else {
            llm_set_model(argv[2]);
        }

        printf("LLM model: %s\n", argv[2]);
        return;
    }

    const char* host = NULL;
    const char* port = "443";
    const char* path = "/v1/chat/completions";
    const char* model = NULL;
    const char* api_key = NULL;
    int cost_tier = 1;
    static char parsed_host[128];

    /* Check if arg1 is a URL (http:// or https://) */
    if (strncmp(arg1, "http://", 7) == 0 || strncmp(arg1, "https://", 8) == 0) {
        /* Parse URL: scheme://host[:port][/path] */
        int is_https = (strncmp(arg1, "https://", 8) == 0);
        const char* after_scheme = arg1 + (is_https ? 8 : 7);
        port = is_https ? "443" : "80";

        /* Find host end (first '/' or ':' or end) */
        const char* slash = strchr(after_scheme, '/');
        const char* colon = strchr(after_scheme, ':');

        if (colon && (!slash || colon < slash)) {
            /* host:port/path */
            size_t hlen = (size_t)(colon - after_scheme);
            if (hlen >= sizeof(parsed_host))
                hlen = sizeof(parsed_host) - 1;
            memcpy(parsed_host, after_scheme, hlen);
            parsed_host[hlen] = '\0';
            host = parsed_host;

            /* Extract port */
            static char parsed_port[8];
            const char* port_start = colon + 1;
            const char* port_end = slash ? slash : port_start + strlen(port_start);
            size_t plen = (size_t)(port_end - port_start);
            if (plen >= sizeof(parsed_port))
                plen = sizeof(parsed_port) - 1;
            memcpy(parsed_port, port_start, plen);
            parsed_port[plen] = '\0';
            port = parsed_port;
        } else if (slash) {
            /* host/path (no port) */
            size_t hlen = (size_t)(slash - after_scheme);
            if (hlen >= sizeof(parsed_host))
                hlen = sizeof(parsed_host) - 1;
            memcpy(parsed_host, after_scheme, hlen);
            parsed_host[hlen] = '\0';
            host = parsed_host;
        } else {
            /* host only */
            strncpy(parsed_host, after_scheme, sizeof(parsed_host) - 1);
            parsed_host[sizeof(parsed_host) - 1] = '\0';
            host = parsed_host;
        }

        /* Extract path from URL */
        if (slash && slash[1]) {
            path = slash;
        } else {
            path = "/v1/chat/completions";
        }

        /* Append /chat/completions if path is just /v1 */
        static char full_path[256];
        if (strcmp(path, "/v1") == 0 || strcmp(path, "/v1/") == 0) {
            snprintf(full_path, sizeof(full_path), "/v1/chat/completions");
            path = full_path;
        }

        if (argc >= 3)
            model = argv[2];
        if (argc >= 4)
            api_key = argv[3];
    }
    /* Check presets — reuse g_router_presets table */
    else {
        const router_preset_t* preset = NULL;
        for (int i = 0; g_router_presets[i].name; i++) {
            if (strcmp(g_router_presets[i].name, arg1) == 0) {
                preset = &g_router_presets[i];
                break;
            }
        }

        if (preset) {
            host = preset->host;
            path = preset->path;
            model = preset->model;
            cost_tier = preset->cost_tier;
            port = "443";
            if (argc >= 3)
                api_key = argv[2];
            if (strcmp(preset->name, "mimo") == 0 && api_key != NULL &&
                strncmp(api_key, "sk-", 3) == 0) {
                host = "api.xiaomimimo.com";
            }
        } else {
            /* Custom: arg1 is bare host */
            host = arg1;
            port = "443";
            if (argc >= 3)
                model = argv[2];
            if (argc >= 4)
                api_key = argv[3];
        }
    }

    /* Build backend and write to router slot 0 */
    llm_backend_t backend = { 0 };
    strncpy(backend.host, host, sizeof(backend.host) - 1);
    backend.host[sizeof(backend.host) - 1] = '\0';
    strncpy(backend.path, path, sizeof(backend.path) - 1);
    backend.path[sizeof(backend.path) - 1] = '\0';
    strncpy(backend.port, port, sizeof(backend.port) - 1);
    backend.port[sizeof(backend.port) - 1] = '\0';
    if (model) {
        strncpy(backend.model, model, sizeof(backend.model) - 1);
        backend.model[sizeof(backend.model) - 1] = '\0';
    }

    /* Preserve existing API key if not provided */
    if (api_key) {
        strncpy(backend.api_key, api_key, sizeof(backend.api_key) - 1);
        backend.api_key[sizeof(backend.api_key) - 1] = '\0';
    } else {
        llm_backend_t old;
        if (llm_router_get_backend(0, &old) == 0 && old.api_key[0]) {
            strncpy(backend.api_key, old.api_key, sizeof(backend.api_key) - 1);
            backend.api_key[sizeof(backend.api_key) - 1] = '\0';
        }
    }

    backend.priority = 0;
    backend.cost_tier = cost_tier;
    backend.enabled = true;

    /* Write to router slot 0 and apply immediately */
    llm_router_set_backend(0, &backend);
    llm_router_apply(0);

    if (api_key && api_key[0]) {
        claw_config_set(VELACLAW_CFG_KEY_API_KEY, api_key);

        if (strstr(host, "xiaomimimo.com") != NULL ||
            (model != NULL && strncmp(model, "mimo-", 5) == 0)) {
            claw_config_set(VELACLAW_CFG_KEY_MIMO_API_KEY, api_key);
            claw_config_set(VELACLAW_CFG_KEY_MIMO_HOST, host);
            claw_config_set(VELACLAW_CFG_KEY_MIMO_PORT, port);
            claw_config_set(VELACLAW_CFG_KEY_MIMO_PATH, path);
            (void)voice_asr_set_backend("mimo");
            (void)voice_tts_set_backend("mimo");
        }
    }

    printf("LLM backend: %s:%s%s (model: %s) [router slot 0]\n",
        host, port, path, model ? model : "(unchanged)");
    if (api_key) {
        printf("API key saved.\n");
    }
}

int nsh_commands_execute(int argc, char **argv)
{
    const char *cmd;

    if (argc <= 0 || argv == NULL || argv[0] == NULL) {
        cmd_help();
        return ERROR;
    }

    cmd = argv[0];

    if (strcmp(cmd, "help") == 0)
        cmd_help();
    else if (strcmp(cmd, "net_status") == 0)
        cmd_net_status();
    else if (strcmp(cmd, "net_test") == 0)
        cmd_net_test();
    else if (strcmp(cmd, "set_llm") == 0)
        cmd_set_llm(argc, argv);
    else if (strcmp(cmd, "list_models") == 0)
        cmd_list_models(argc, argv);
    else if (strcmp(cmd, "config_show") == 0)
        cmd_config_show();
    else if (strcmp(cmd, "config_reset") == 0)
        cmd_config_reset();
    else if (strcmp(cmd, "router_status") == 0)
        cmd_router_status();
    else if (strcmp(cmd, "router_set") == 0)
        cmd_router_set(argc, argv);
    else if (strcmp(cmd, "router_model") == 0)
        cmd_router_model(argc, argv);
    else if (strcmp(cmd, "router_profile") == 0)
        cmd_router_profile(argc, argv);
    else if (strcmp(cmd, "router_clear") == 0)
        cmd_router_clear(argc, argv);
    else if (strcmp(cmd, "set_voice_tts") == 0)
        cmd_set_voice_tts(argc, argv);
    else if (strcmp(cmd, "set_voice_asr") == 0)
        cmd_set_voice_asr(argc, argv);
    else if (strcmp(cmd, "voice_start") == 0)
        cmd_voice_start();
    else if (strcmp(cmd, "voice_stop") == 0)
        cmd_voice_stop();
    else if (strcmp(cmd, "voice_test_tts") == 0)
        cmd_voice_test_tts(argc, argv);
    else if (strcmp(cmd, "voice_test_asr") == 0)
        cmd_voice_test_asr(argc, argv);
    else if (strcmp(cmd, "set_wifi") == 0)
        cmd_set_wifi(argc, argv);
    else if (strcmp(cmd, "wifi_reconnect") == 0)
        cmd_wifi_reconnect();
    else if (strcmp(cmd, "set_search_key") == 0)
        cmd_set_search_key(argc, argv);
    else if (strcmp(cmd, "set_exa_key") == 0)
        cmd_set_exa_key(argc, argv);
    else if (strcmp(cmd, "set_news_key") == 0)
        cmd_set_news_key(argc, argv);
    else if (strcmp(cmd, "set_tavily_key") == 0)
        cmd_set_tavily_key(argc, argv);
    else if (strcmp(cmd, "set_volc_key") == 0)
        cmd_set_volc_key(argc, argv);
    else if (strcmp(cmd, "set_volc_asr") == 0)
        cmd_set_volc_asr(argc, argv);
    else if (strcmp(cmd, "set_volc_speaker") == 0)
        cmd_set_volc_speaker(argc, argv);
    else if (strcmp(cmd, "set_weixin_token") == 0)
        cmd_set_weixin_token(argc, argv);
    else if (strcmp(cmd, "weixin_login") == 0)
        cmd_weixin_login();
    else if (strcmp(cmd, "set_feishu_app") == 0)
        cmd_set_feishu_app(argc, argv);
    else if (strcmp(cmd, "set_feishu_user_token") == 0)
        cmd_set_feishu_user_token(argc, argv);
    else if (strcmp(cmd, "set_proxy") == 0)
        cmd_set_proxy(argc, argv);
    else if (strcmp(cmd, "clear_proxy") == 0)
        cmd_clear_proxy();
    else if (strcmp(cmd, "ask") == 0)
        cmd_ask(argc, argv);
    else if (strcmp(cmd, "memory_read") == 0)
        cmd_memory_read();
    else if (strcmp(cmd, "memory_write") == 0)
        cmd_memory_write(argc, argv);
    else if (strcmp(cmd, "session_list") == 0)
        cmd_session_list();
    else if (strcmp(cmd, "session_clear") == 0)
        cmd_session_clear(argc, argv);
    else if (strcmp(cmd, "session_clear_all") == 0)
        cmd_session_clear_all();
    else if (strcmp(cmd, "heap_info") == 0)
        cmd_heap_info();
    else if (strcmp(cmd, "heartbeat_trigger") == 0)
        cmd_heartbeat_trigger();
    else if (strcmp(cmd, "cron_start") == 0)
        cmd_cron_start();
    else if (strcmp(cmd, "node_list") == 0)
        cmd_node_list();
    else if (strcmp(cmd, "set_gateway") == 0)
        cmd_set_gateway(argc, argv);
    else if (strcmp(cmd, "node_start") == 0)
        cmd_node_start();
    else if (strcmp(cmd, "node_stop") == 0)
        cmd_node_stop();
    else if (strcmp(cmd, "set_mqtt") == 0)
        cmd_set_mqtt(argc, argv);
    else if (strcmp(cmd, "launch_app") == 0)
        cmd_launch_app(argc, argv);
    else if (strcmp(cmd, "exit_app") == 0)
        cmd_exit_app();
#ifdef CONFIG_VELACLAW_TEST
    else if (strcmp(cmd, "claw_test") == 0)
        cmd_claw_test(argc, argv);
#endif
    else if (strcmp(cmd, "restart") == 0)
        cmd_restart();
    else if (strcmp(cmd, "quit") == 0)
        cmd_quit();
    else {
        printf("Unknown VelaClaw command: %s (try: velaclaw help)\n", cmd);
        return ERROR;
    }

    return OK;
}

static void cmd_memory_read(void)
{
    char* buf = malloc(4096);
    if (!buf) {
        printf("Out of memory.\n");
        return;
    }
    if (memory_read_long_term(buf, 4096) == OK && buf[0]) {
        printf("=== MEMORY.md ===\n%s\n=================\n", buf);
    } else {
        printf("MEMORY.md is empty or not found.\n");
    }
    free(buf);
}

static void cmd_memory_write(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: memory_write <content>\n");
        return;
    }
    memory_write_long_term(argv[1]);
    printf("MEMORY.md updated.\n");
}

static void cmd_session_list(void)
{
    printf("Sessions:\n");
    session_list();
}

static void cmd_session_clear(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: session_clear <chat_id> | session_clear_all\n");
        return;
    }
    if (session_clear(argv[1]) == OK) {
        printf("Session cleared.\n");
    } else {
        printf("Session not found.\n");
    }
}

static void cmd_session_clear_all(void)
{
    session_clear_all();
    printf("All sessions cleared.\n");
}

static void cmd_heap_info(void)
{
    struct mallinfo mi = mallinfo();
    printf("Heap: arena=%d fordblks(free)=%d uordblks(used)=%d\n",
        mi.arena, mi.fordblks, mi.uordblks);
}

static void cmd_set_proxy(int argc, char** argv)
{
    if (argc < 3) {
        printf("Usage: set_proxy <host> <port>\n");
        return;
    }
    uint16_t port = (uint16_t)atoi(argv[2]);
    http_proxy_set(argv[1], port);
    printf("Proxy set to %s:%d.\n", argv[1], port);
}

static void cmd_clear_proxy(void)
{
    http_proxy_clear();
    printf("Proxy cleared. Restart to apply.\n");
}

static void cmd_set_wifi(int argc, char** argv)
{
    if (argc < 3) {
        printf("Usage: set_wifi <ssid> <password>\n");
        return;
    }
    printf("Connecting to '%s' ...\n", argv[1]);
    int err = network_wifi_connect(NULL, argv[1], argv[2]);
    if (err == OK)
        printf("WiFi connected: %s\n", network_get_ip());
    else
        printf("WiFi failed. Check SSID/password or run net_status.\n");
}

static void cmd_wifi_reconnect(void)
{
    printf("Reconnecting WiFi ...\n");
    int err = network_wifi_reconnect();
    if (err == OK)
        printf("Reconnected: %s\n", network_get_ip());
    else if (err == ERROR)
        printf("No saved credentials. Use: set_wifi <ssid> <pass>\n");
    else
        printf("WiFi reconnect failed.\n");
}

static void cmd_set_search_key(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: set_search_key <key>\n");
        return;
    }
    tool_web_search_set_serp_key(argv[1]);
    printf("SerpAPI (Google) key saved.\n");
}

static void cmd_set_exa_key(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: set_exa_key <key>\n");
        return;
    }
    tool_web_search_set_exa_key(argv[1]);
    printf("Exa AI key saved.\n");
}

static void cmd_set_tavily_key(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: set_tavily_key <key>\n");
        return;
    }
    tool_web_search_set_tavily_key(argv[1]);
    printf("Tavily key saved.\n");
}

static void cmd_set_news_key(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: set_news_key <key>\n");
        return;
    }
    tool_web_search_set_news_key(argv[1]);
    printf("NewsAPI key saved.\n");
}

static void cmd_config_show(void)
{
    char val[128] = { 0 };

    printf("=== Current Configuration ===\n");

#define SHOW_CFG(label, key, mask)                                        \
    do {                                                                  \
        memset(val, 0, sizeof(val));                                      \
        if (claw_config_get((key), val, sizeof(val)) != OK || !val[0])    \
            strcpy(val, "(not set)");                                     \
        if ((mask) && strlen(val) > 6 && strcmp(val, "(not set)") != 0) { \
            char masked[128];                                             \
            snprintf(masked, sizeof(masked), "%.4s****", val);            \
            printf("  %-14s: %s\n", (label), masked);                     \
        } else {                                                          \
            printf("  %-14s: %s\n", (label), val);                        \
        }                                                                 \
    } while (0)

    SHOW_CFG("Feishu AppID", VELACLAW_CFG_KEY_FEISHU_APP_ID, false);
    SHOW_CFG("Feishu Secret", VELACLAW_CFG_KEY_FEISHU_APP_SECRET, true);
    SHOW_CFG("API Key", VELACLAW_CFG_KEY_API_KEY, true);
    SHOW_CFG("Model", VELACLAW_CFG_KEY_MODEL, false);
    SHOW_CFG("LLM Host", VELACLAW_CFG_KEY_LLM_HOST, false);
    SHOW_CFG("LLM Path", VELACLAW_CFG_KEY_LLM_PATH, false);
    SHOW_CFG("Proxy Host", VELACLAW_CFG_KEY_PROXY_HOST, false);
    SHOW_CFG("Proxy Port", VELACLAW_CFG_KEY_PROXY_PORT, false);
    SHOW_CFG("SerpAPI Key", VELACLAW_CFG_KEY_SERP_KEY, true);
    SHOW_CFG("Exa Key", VELACLAW_CFG_KEY_EXA_KEY, true);
    SHOW_CFG("Tavily Key", VELACLAW_CFG_KEY_TAVILY_KEY, true);
    SHOW_CFG("News Key", VELACLAW_CFG_KEY_NEWS_KEY, true);
    SHOW_CFG("Tavily Key", VELACLAW_CFG_KEY_TAVILY_KEY, true);
    SHOW_CFG("Gateway", VELACLAW_CFG_KEY_GATEWAY_HOST, false);
    SHOW_CFG("GW Port", VELACLAW_CFG_KEY_GATEWAY_PORT, false);
    SHOW_CFG("GW Token", VELACLAW_CFG_KEY_GATEWAY_TOKEN, true);
    SHOW_CFG("MQTT Broker", VELACLAW_CFG_KEY_MQTT_BROKER, false);
    SHOW_CFG("Volc AppKey", VELACLAW_CFG_KEY_VOLC_APPKEY, true);
    SHOW_CFG("Volc Token", VELACLAW_CFG_KEY_VOLC_TOKEN, true);
    SHOW_CFG("Volc API Key", VELACLAW_CFG_KEY_VOLC_API_KEY, true);
    SHOW_CFG("Volc Speaker", VELACLAW_CFG_KEY_VOLC_SPEAKER, false);

#undef SHOW_CFG

    printf("Network: %s / %s\n",
        network_is_connected() ? "connected" : "disconnected",
        network_get_ip());
    printf("=============================\n");
}

static void cmd_config_reset(void)
{
    config_erase_all();
    printf("All runtime config cleared. Build-time defaults will be used on restart.\n");
}

static void cmd_restart(void)
{
    printf("Restarting...\n");
    fflush(stdout);
#ifdef CONFIG_BOARDCTL_RESET
    boardctl(BOARDIOC_RESET, 0);
#else
    /* Last resort: just loop forever (caller should call reboot()) */
    while (1)
        sleep(1);
#endif
}

static void cmd_heartbeat_trigger(void)
{
    if (heartbeat_trigger()) {
        printf("Heartbeat: triggered agent check.\n");
    } else {
        printf("Heartbeat: no actionable tasks found.\n");
    }
}

static void cmd_ask(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: ask <message>\n");
        return;
    }

    /* Concatenate all arguments as the message */
    char content[256] = { 0 };
    for (int i = 1; i < argc; i++) {
        strncat(content, argv[i], sizeof(content) - strlen(content) - 1);
        if (i < argc - 1)
            strncat(content, " ", sizeof(content) - strlen(content) - 1);
    }

    velaclaw_msg_t msg = { 0 };
    strncpy(msg.channel, "cli", sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, "console", sizeof(msg.chat_id) - 1);
    msg.content = strdup(content);
    if (msg.content)
        message_bus_push_inbound(&msg);
    printf("Sent to agent: %s\n", content);
}

static void cmd_cron_start(void)
{
    if (cron_service_start() == OK) {
        printf("Cron scheduler started.\n");
    } else {
        printf("Cron scheduler already running or failed to start.\n");
    }
}

static void cmd_node_list(void)
{
    char buf[1024];
    printf("=== Connected Nodes ===\n");
    node_manager_list(buf, sizeof(buf));
    printf("%s", buf);
    printf("=======================\n");
}

static void cmd_set_gateway(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: set_gateway <host> [port] [token]\n");
        printf("  e.g. set_gateway <host> 8080 my-token\n");
        return;
    }
    claw_config_set(VELACLAW_CFG_KEY_GATEWAY_HOST, argv[1]);
    if (argc >= 3)
        claw_config_set(VELACLAW_CFG_KEY_GATEWAY_PORT, argv[2]);
    if (argc >= 4)
        claw_config_set(VELACLAW_CFG_KEY_GATEWAY_TOKEN, argv[3]);
    printf("Gateway set to %s:%s%s. Use 'node_start' to connect.\n",
        argv[1], argc >= 3 ? argv[2] : "8080",
        argc >= 4 ? " (token saved)" : "");
}

static void cmd_node_start(void)
{
    int ret = node_client_start();
    if (ret == OK) {
        printf("Node client started (check logs for connection status).\n");
    } else {
        printf("Node client start failed.\n");
    }
}

static void cmd_node_stop(void)
{
    node_client_stop();
    printf("Node client stopped.\n");
}

static void cmd_set_mqtt(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: set_mqtt <host:port> [client_id]\n"
               "  e.g. set_mqtt <broker-host>:1883\n"
               "       set_mqtt <broker-host>:1883 my-device\n");
        return;
    }

    claw_config_set(VELACLAW_CFG_KEY_MQTT_BROKER, argv[1]);
    if (argc >= 3) {
        claw_config_set(VELACLAW_CFG_KEY_MQTT_CLIENT_ID, argv[2]);
    }

    /* Re-init and start immediately so no restart is needed */
    mqtt_channel_stop();
    mqtt_channel_init();
    mqtt_channel_start();
    printf("MQTT broker set to %s and started.\n", argv[1]);
}

/* ── list_models helpers ──────────────────────────────────────── */

#define LIST_MODELS_BUF_SIZE (256 * 1024)
#define LIST_MODELS_PATH "/api/v1/models"

static int list_models_fetch(const char* api_key, char** out, size_t* out_len)
{
    char* buf = malloc(LIST_MODELS_BUF_SIZE);

    if (!buf) {
        printf("Error: OOM allocating response buffer\n");
        return ERROR;
    }

    vela_header_t hdrs[3];
    char auth[160];

    snprintf(auth, sizeof(auth), "Bearer %s", api_key);
    hdrs[0].name = "Authorization";
    hdrs[0].value = auth;
    hdrs[1].name = "Accept";
    hdrs[1].value = "application/json";
    hdrs[2].name = NULL;
    hdrs[2].value = NULL;

    size_t body_len;
    int status = vela_https_request(
        VELACLAW_LLM_OPENROUTER_HOST, "443", "GET",
        LIST_MODELS_PATH, hdrs, NULL, 0,
        buf, LIST_MODELS_BUF_SIZE, &body_len);

    if (status < 200 || status >= 300) {
        printf("Error: HTTP %d from models API\n", status);
        free(buf);
        return ERROR;
    }

    *out = buf;
    *out_len = body_len;
    return OK;
}

static int list_models_fetch_proxy(const char* api_key,
    char** out, size_t* out_len)
{
    proxy_conn_t* conn = proxy_conn_open(
        VELACLAW_LLM_OPENROUTER_HOST, 443, 30000);

    if (!conn) {
        printf("Error: proxy connection failed\n");
        return ERROR;
    }

    char hdr[512];
    int hlen = snprintf(hdr, sizeof(hdr),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Accept: application/json\r\n"
        "Connection: close\r\n\r\n",
        LIST_MODELS_PATH, VELACLAW_LLM_OPENROUTER_HOST, api_key);

    if (proxy_conn_write(conn, hdr, hlen) < 0) {
        proxy_conn_close(conn);
        printf("Error: write failed\n");
        return ERROR;
    }

    char* buf = malloc(LIST_MODELS_BUF_SIZE);

    if (!buf) {
        proxy_conn_close(conn);
        printf("Error: OOM\n");
        return ERROR;
    }

    size_t total = 0;
    char tmp[4096];

    while (total < LIST_MODELS_BUF_SIZE - 1) {
        int n = proxy_conn_read(conn, tmp, sizeof(tmp), 60000);

        if (n <= 0) {
            break;
        }
        size_t avail = LIST_MODELS_BUF_SIZE - 1 - total;
        size_t copy = (size_t)n < avail ? (size_t)n : avail;

        memcpy(buf + total, tmp, copy);
        total += copy;
    }
    proxy_conn_close(conn);
    buf[total] = '\0';

    /* Strip HTTP header */
    char* body = strstr(buf, "\r\n\r\n");

    if (body) {
        body += 4;
        size_t blen = total - (size_t)(body - buf);
        memmove(buf, body, blen);
        buf[blen] = '\0';
        *out_len = blen;
    } else {
        *out_len = total;
    }

    *out = buf;
    return OK;
}

static bool model_is_free(cJSON* item)
{
    cJSON* pricing = cJSON_GetObjectItem(item, "pricing");

    if (!pricing) {
        return false;
    }

    const cJSON* prompt = cJSON_GetObjectItem(pricing, "prompt");
    const cJSON* completion = cJSON_GetObjectItem(pricing, "completion");

    if (!prompt || !completion) {
        return false;
    }

    const char* p = prompt->valuestring;
    const char* c = completion->valuestring;

    if (!p || !c) {
        return false;
    }
    return (atof(p) == 0.0 && atof(c) == 0.0);
}

static void list_models_print(cJSON* data, bool free_only,
    const char* keyword)
{
    int count = 0;
    int total = cJSON_GetArraySize(data);

    for (int i = 0; i < total; i++) {
        cJSON* item = cJSON_GetArrayItem(data, i);
        cJSON* id = cJSON_GetObjectItem(item, "id");

        if (!id || !id->valuestring) {
            continue;
        }

        bool is_free = model_is_free(item);

        if (free_only && !is_free) {
            continue;
        }
        if (keyword && !strstr(id->valuestring, keyword)) {
            continue;
        }

        printf("  %-45s %s\n", id->valuestring,
            is_free ? "[FREE]" : "[PAID]");
        count++;
    }

    printf("\n%d model(s) shown (total: %d)\n", count, total);
}

static void cmd_list_models(int argc, char** argv)
{
    char host[128];
    char api_key[128];

    claw_config_get(VELACLAW_CFG_KEY_LLM_HOST, host, sizeof(host));
    claw_config_get(VELACLAW_CFG_KEY_API_KEY, api_key, sizeof(api_key));

    if (!strstr(host, "openrouter.ai")) {
        printf("list_models only works with openrouter.ai backend.\n"
               "Use: velaclaw set_llm openrouter\n");
        return;
    }
    if (!api_key[0]) {
        printf("No API key set. Use: velaclaw set_llm <preset> <key>\n");
        return;
    }

    bool free_only = false;
    const char* keyword = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--free") == 0) {
            free_only = true;
        } else {
            keyword = argv[i];
        }
    }

    printf("Fetching models from %s ...\n", VELACLAW_LLM_OPENROUTER_HOST);

    char* buf;
    size_t buf_len;
    int ret;

    if (http_proxy_is_enabled()) {
        ret = list_models_fetch_proxy(api_key, &buf, &buf_len);
    } else {
        ret = list_models_fetch(api_key, &buf, &buf_len);
    }
    if (ret != OK) {
        return;
    }

    cJSON* root = cJSON_Parse(buf);

    free(buf);
    buf = NULL;

    if (!root) {
        printf("Error: failed to parse JSON response\n");
        return;
    }

    cJSON* data = cJSON_GetObjectItem(root, "data");

    if (!data || !cJSON_IsArray(data)) {
        printf("Error: unexpected response format\n");
        cJSON_Delete(root);
        return;
    }

    list_models_print(data, free_only, keyword);
    cJSON_Delete(root);
}

#ifdef CONFIG_VELACLAW_TEST
static void cmd_claw_test(int argc, char** argv)
{
    const char* filter = argc >= 2 ? argv[1] : NULL;
    int rc = test_vision_integ_run(filter);
    printf("Test suite %s\n", rc == 0 ? "PASSED" : "FAILED");
}
#endif

static void cmd_quit(void)
{
    printf("Exiting velaclaw...\n");
    fflush(stdout);
    velaclaw_request_shutdown();
}

static void cmd_launch_app(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: launch_app <package_name>\n");
        return;
    }

    char input[256];
    snprintf(input, sizeof(input), "{\"package_name\":\"%s\"}", argv[1]);

    char output[512];
    int ret = tool_launch_quickapp_execute(input, output, sizeof(output));
    printf("Result(%d): %s\n", ret, output);
}

static void cmd_exit_app(void)
{
    char output[512];
    int ret = tool_exit_quickapp_execute("{}", output, sizeof(output));
    printf("Result(%d): %s\n", ret, output);
}

/* ── Router commands ──────────────────────────────────────────── */

static void cmd_router_status(void)
{
    char* json = llm_router_status_json();
    if (json) {
        printf("=== LLM Router Status ===\n%s\n=========================\n", json);
        free(json);
    } else {
        printf("Failed to get router status.\n");
    }
}

static void cmd_router_profile(int argc, char** argv)
{
    if (argc < 2) {
        llm_route_profile_t p = llm_router_get_profile();
        const char* name = "auto";
        if (p == LLM_ROUTE_ECO)
            name = "eco";
        else if (p == LLM_ROUTE_PREMIUM)
            name = "premium";
        printf("Current profile: %s\n", name);
        printf("Usage: router_profile <eco|auto|premium>\n");
        return;
    }

    llm_route_profile_t profile = LLM_ROUTE_AUTO;
    const char* profile_name = "auto";
    if (strcmp(argv[1], "eco") == 0) {
        profile = LLM_ROUTE_ECO;
        profile_name = "eco";
    } else if (strcmp(argv[1], "premium") == 0) {
        profile = LLM_ROUTE_PREMIUM;
        profile_name = "premium";
    } else if (strcmp(argv[1], "auto") != 0) {
        printf("Unknown profile: %s (valid: eco, auto, premium)\n", argv[1]);
        return;
    }

    llm_router_set_profile(profile);
    printf("Router profile set to: %s\n", profile_name);
}

static void cmd_router_set(int argc, char** argv)
{
    if (argc < 3) {
        printf("Usage: router_set <preset> <api_key>\n\n"
               "Presets:\n"
               "  deepseek  - DeepSeek (cheap, fast)\n"
               "  kimi      - Moonshot Kimi (mid-tier)\n"
               "  qwen      - Alibaba Qwen (cheap)\n"
               "  glm       - Zhipu GLM-4 (cheap)\n"
               "  openai    - OpenAI GPT-4o (premium)\n"
               "  claude    - Anthropic Claude (premium)\n"
               "  mimo      - Xiaomi MiMo Token Plan (premium)\n\n"
               "Example:\n"
               "  router_set deepseek sk-xxx\n"
               "  router_set openai sk-xxx\n");
        return;
    }

    const char* preset_name = argv[1];
    const char* api_key = argv[2];
    const router_preset_t* preset = NULL;

    for (int i = 0; g_router_presets[i].name; i++) {
        if (strcmp(g_router_presets[i].name, preset_name) == 0) {
            preset = &g_router_presets[i];
            break;
        }
    }

    if (!preset) {
        printf("Unknown preset: %s\n", preset_name);
        return;
    }

    /* Find first empty slot or reuse existing */
    int slot = -1;
    for (int i = 0; i < LLM_ROUTER_MAX_BACKENDS; i++) {
        llm_backend_t b;
        if (llm_router_get_backend(i, &b) != 0 || !b.enabled) {
            slot = i;
            break;
        }
        if (strcmp(b.host, preset->host) == 0) {
            slot = i; /* Update existing */
            break;
        }
    }

    if (slot < 0) {
        printf("Error: All 4 slots full. Use router_clear first.\n");
        return;
    }

    llm_backend_t backend = { 0 };
    strncpy(backend.host, preset->host, sizeof(backend.host) - 1);
    strncpy(backend.path, preset->path, sizeof(backend.path) - 1);
    strncpy(backend.port, "443", sizeof(backend.port) - 1);
    strncpy(backend.model, preset->model, sizeof(backend.model) - 1);
    strncpy(backend.api_key, api_key, sizeof(backend.api_key) - 1);
    backend.priority = slot;
    backend.cost_tier = preset->cost_tier;
    backend.enabled = true;

    if (llm_router_set_backend(slot, &backend) == 0) {
        printf("Added [%d]: %s (%s, tier=%d)\n",
            slot, preset_name, preset->model, preset->cost_tier);
    } else {
        printf("Failed to add backend.\n");
    }
}

static void cmd_router_clear(int argc, char** argv)
{
    if (argc >= 2) {
        /* Validate numeric input */
        const char* arg = argv[1];
        if (arg[0] < '0' || arg[0] > '9') {
            printf("Error: index must be a number (0-%d)\n", LLM_ROUTER_MAX_BACKENDS - 1);
            return;
        }
        int idx = atoi(arg);
        if (idx >= 0 && idx < LLM_ROUTER_MAX_BACKENDS) {
            llm_backend_t empty = { 0 };
            llm_router_set_backend(idx, &empty);
            printf("Slot %d cleared.\n", idx);
        } else {
            printf("Error: index must be 0-%d\n", LLM_ROUTER_MAX_BACKENDS - 1);
        }
    } else {
        for (int i = 0; i < LLM_ROUTER_MAX_BACKENDS; i++) {
            llm_backend_t empty = { 0 };
            llm_router_set_backend(i, &empty);
        }
        printf("All router backends cleared.\n");
    }
}

static void cmd_router_model(int argc, char** argv)
{
    if (argc < 3) {
        printf("Usage: router_model <index> <model_name>\n"
               "Example: router_model 1 gpt-4o-mini\n");
        return;
    }

    int idx = atoi(argv[1]);
    if (idx < 0 || idx >= LLM_ROUTER_MAX_BACKENDS) {
        printf("Error: index must be 0-%d\n", LLM_ROUTER_MAX_BACKENDS - 1);
        return;
    }

    llm_backend_t b;
    if (llm_router_get_backend(idx, &b) != 0 || !b.enabled) {
        printf("Error: slot %d is empty\n", idx);
        return;
    }

    strncpy(b.model, argv[2], sizeof(b.model) - 1);
    b.model[sizeof(b.model) - 1] = '\0';

    if (llm_router_set_backend(idx, &b) == 0) {
        printf("Slot %d model changed to: %s\n", idx, argv[2]);
    } else {
        printf("Failed to update model.\n");
    }
}

static void cmd_set_volc_key(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: set_volc_key <api_key>\n");
        return;
    }
    claw_config_set(VELACLAW_CFG_KEY_VOLC_API_KEY, argv[1]);
    printf("Doubao voice API key saved.\n");
}

static void cmd_set_volc_speaker(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: set_volc_speaker <speaker_id>\n");
        return;
    }
    claw_config_set(VELACLAW_CFG_KEY_VOLC_SPEAKER, argv[1]);
    printf("TTS speaker set to: %s\n", argv[1]);
}

static void cmd_set_volc_asr(int argc, char** argv)
{
    if (argc < 4) {
        printf("Usage: set_volc_asr <app_id> <token> <cluster>\n");
        return;
    }
    claw_config_set(VELACLAW_CFG_KEY_VOLC_APPKEY, argv[1]);
    claw_config_set(VELACLAW_CFG_KEY_VOLC_TOKEN, argv[2]);
    claw_config_set(VELACLAW_CFG_KEY_VOLC_ASR_CLUSTER, argv[3]);
    printf("ASR credentials saved (app_id=%s, cluster=%s).\n",
        argv[1], argv[3]);
}

static void cmd_voice_start(void)
{
    voice_channel_start();
}

static void cmd_voice_stop(void)
{
    voice_channel_stop();
}

static void cmd_voice_test_tts(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: voice_test_tts <text> [output.pcm]\n");
        return;
    }

    const char* out = (argc >= 3)
        ? argv[2]
        : VELACLAW_DATA_DIR "/tts_out.pcm";

    voice_channel_test_tts(argv[1], out);
}

static void cmd_voice_test_asr(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: voice_test_asr <pcm_file>\n");
        return;
    }
    voice_channel_test_asr(argv[1]);
}

static void cmd_set_voice_tts(int argc, char** argv)
{
    if (argc < 2) {
        const char* cur = voice_tts_get_backend();

        printf("Current TTS backend: %s\n",
            cur ? cur : "(none)");
        printf("Usage: velaclaw set_voice_tts <backend_name>\n");
        return;
    }

    int ret = voice_tts_set_backend(argv[1]);

    if (ret == 0) {
        printf("TTS backend set to: %s\n", argv[1]);
    } else {
        printf("TTS backend '%s' not found.\n", argv[1]);
    }
}

static void cmd_set_voice_asr(int argc, char** argv)
{
    if (argc < 2) {
        const char* cur = voice_asr_get_backend();

        printf("Current ASR backend: %s\n",
            cur ? cur : "(none)");
        printf("Usage: velaclaw set_voice_asr <backend_name>\n");
        return;
    }

    int ret = voice_asr_set_backend(argv[1]);

    if (ret == 0) {
        printf("ASR backend set to: %s\n", argv[1]);
    } else {
        printf("ASR backend '%s' not found.\n", argv[1]);
    }
}

/* ── WeChat commands ───────────────────────────────────────── */

static void cmd_set_weixin_token(int argc, char** argv)
{
    if (argc < 2) {
        printf("Usage: set_weixin_token <bot_token>\n");
        return;
    }
    weixin_channel_set_token(argv[1], 0);
    printf("WeChat bot token saved.\n");
}

static void cmd_weixin_login(void)
{
    char qr_url[512] = "";
    char qrcode_id[128] = "";

    printf("Requesting QR code from iLink Bot API...\n");
    int rc = weixin_channel_login(qr_url, sizeof(qr_url),
        qrcode_id, sizeof(qrcode_id));
    if (rc != 0) {
        printf("Failed to get QR code (rc=%d). Check network.\n", rc);
        return;
    }

    printf("QR URL: %s\n", qr_url);
    claw_show_qrcode(qr_url);
    printf("Scan the QR code with WeChat, then waiting...\n");

    for (int i = 0; i < 60; i++) {
        sleep(2);
        int status = weixin_channel_poll_login(qrcode_id);
        if (status == 1) {
            printf("Login confirmed! Token saved.\n");
            return;
        } else if (status == 2) {
            printf("Scanned, waiting for confirm...\n");
        } else if (status == -3) {
            printf("QR code expired. Run weixin_login again.\n");
            return;
        } else if (status < 0) {
            printf("Poll error (rc=%d).\n", status);
            return;
        }
    }
    printf("Timeout waiting for scan.\n");
}

/* ── Init / Start ─────────────────────────────────────────────── */

int nsh_commands_init(void)
{
    /* Pure registration — no thread spawned here.
     * Commands are statically defined; nothing to do at the moment.
     * Kept as a hook for future dynamic command registration. */
    syslog(LOG_INFO, "NSH commands registered");
    return OK;
}

int nsh_commands_start(void)
{
    /* NSH owns stdin/stdout. Do not start a second reader on the console. */
    syslog(LOG_INFO, "[%s] VelaClaw CLI thread disabled; use 'velaclaw <cmd>'\n",
        TAG);
    return OK;
}
