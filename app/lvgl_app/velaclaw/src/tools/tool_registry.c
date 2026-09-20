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

#include "tools/tool_registry.h"
#include "node/node_manager.h"
#include "security/tool_guard.h"
#include "tools/mcp_bridge.h"
#include "tools/tool_control.h"
#include "tools/tool_cron.h"
#include "tools/tool_feishu_chat.h"
#include "tools/tool_feishu_doc.h"
#include "tools/tool_fetch_url.h"
#include "tools/tool_files.h"
#include "tools/tool_get_time.h"
#include "tools/tool_health.h"
#include "tools/tool_media.h"
#include "tools/tool_proxyquickapp.h"
#include "tools/tool_shell.h"
#include "tools/tool_system.h"
#include "tools/tool_vision.h"
#include "tools/tool_web_search.h"
#include "velaclaw_compat.h"
#include "velaclaw_config.h"

#include "cJSON.h"
#include <pthread.h>
#include <string.h>

static const char* TAG = "tools";

#define MAX_TOOLS 48

static velaclaw_tool_t s_tools[MAX_TOOLS];
static int s_tool_count;
static char* s_tools_json;
static bool s_tools_dirty = true;
static pthread_mutex_t s_tools_mtx = PTHREAD_MUTEX_INITIALIZER;

static void register_tool(const velaclaw_tool_t* tool)
{
    if (s_tool_count >= MAX_TOOLS) {
        syslog(LOG_ERR, "[%s] Tool registry full\n", TAG);
        return;
    }
    s_tools[s_tool_count++] = *tool;
    syslog(LOG_INFO, "[%s] Registered tool: %s\n", TAG, tool->name);
}

/* Rebuild tools JSON under lock. Only rebuilds when dirty flag is set. */
static void build_tools_json_locked(void)
{
    if (!s_tools_dirty) {
        return;
    }

    cJSON* arr = cJSON_CreateArray();

    for (int i = 0; i < s_tool_count; i++) {
        cJSON* tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", s_tools[i].name);
        cJSON_AddStringToObject(tool, "description", s_tools[i].description);
        cJSON* schema = cJSON_Parse(s_tools[i].input_schema_json);
        if (schema)
            cJSON_AddItemToObject(tool, "input_schema", schema);
        cJSON_AddItemToArray(arr, tool);
    }

    char* mcp_json = mcp_bridge_get_tools_json();
    if (mcp_json) {
        cJSON* mcp_arr = cJSON_Parse(mcp_json);
        if (mcp_arr && cJSON_IsArray(mcp_arr)) {
            cJSON* item = NULL;
            cJSON_ArrayForEach(item, mcp_arr)
            {
                cJSON_AddItemToArray(arr, cJSON_Duplicate(item, 1));
            }
        }
        cJSON_Delete(mcp_arr);
        free(mcp_json);
    }

    char* node_json = node_manager_get_tools_json();
    if (node_json) {
        cJSON* node_arr = cJSON_Parse(node_json);
        if (node_arr && cJSON_IsArray(node_arr)) {
            cJSON* item = NULL;
            cJSON_ArrayForEach(item, node_arr)
            {
                cJSON_AddItemToArray(arr, cJSON_Duplicate(item, 1));
            }
        }
        cJSON_Delete(node_arr);
        free(node_json);
    }

    free(s_tools_json);
    s_tools_json = cJSON_PrintUnformatted(arr);
    s_tools_dirty = false;
    cJSON_Delete(arr);
    syslog(LOG_INFO, "[%s] Tools JSON built (%d builtin + MCP + Node tools)\n",
        TAG, s_tool_count);
}

void tool_registry_rebuild_json(void) { build_tools_json_locked(); }

int tool_registry_init(void)
{
    s_tool_count = 0;

    if (mcp_bridge_init() != OK) {
        syslog(LOG_WARNING, "[%s] MCP bridge init failed; MCP tools disabled\n",
            TAG);
    }

    tool_web_search_init();

    /* Web / info tools */
    REGISTER_TOOL(
        "web_search",
        "Search the web for current information, facts, news, or real-time data. "
        "Use when you need information that changes frequently or is beyond your "
        "training cutoff. Returns search results with titles, URLs, and "
        "snippets.",
        TOOL_SCHEMA_BEGIN() TOOL_PARAM_STR("query", "The search query")
            TOOL_SCHEMA_END_REQUIRED("\"query\""),
        tool_web_search_execute);

    REGISTER_TOOL(
        "news_search",
        "Search for recent news articles and current events on any topic. "
        "Use when you need breaking news, recent headlines, or current affairs. "
        "Set top_headlines=true for major stories.",
        TOOL_SCHEMA_BEGIN()
            TOOL_PARAM_STR("query", "News search query") "," TOOL_PARAM_BOOL(
                "top_headlines",
                "If true, fetch top headlines instead of everything")
                TOOL_SCHEMA_END_REQUIRED("\"query\""),
        tool_news_search_execute);

    REGISTER_TOOL(
        "get_weather",
        "Get current weather conditions and temperature for any location "
        "worldwide. "
        "Use when users ask about weather, temperature, or atmospheric "
        "conditions.",
        TOOL_SCHEMA_BEGIN() TOOL_PARAM_STR(
            "location", "City name or location, e.g. 'Shanghai' or 'New York'")
            TOOL_SCHEMA_END_REQUIRED("\"location\""),
        tool_get_weather_execute);

    REGISTER_TOOL_NO_PARAMS(
        "get_current_time",
        "Get the current date and time. Use whenever you need to know what time "
        "it is, what date it is, or for any time-related calculations.",
        tool_get_time_execute);

    /* File tools */
    REGISTER_TOOL("read_file",
        "Read a file from the data directory. "
        "Path must start with " VELACLAW_DATA_DIR "/.",
        TOOL_SCHEMA_BEGIN() TOOL_PARAM_STR("path", "Absolute file path")
            TOOL_SCHEMA_END_REQUIRED("\"path\""),
        tool_read_file_execute);

    REGISTER_TOOL(
        "write_file",
        "Write or overwrite a file in the data directory. "
        "Path must start with " VELACLAW_DATA_DIR "/.",
        TOOL_SCHEMA_BEGIN()
            TOOL_PARAM_STR("path", "Absolute file path") "," TOOL_PARAM_STR(
                "content", "File content")
                TOOL_SCHEMA_END_REQUIRED("\"path\",\"content\""),
        tool_write_file_execute);

    REGISTER_TOOL(
        "edit_file", "Find and replace text in a file in the data directory.",
        TOOL_SCHEMA_BEGIN()
            TOOL_PARAM_STR("path", "Absolute file path") "," TOOL_PARAM_STR(
                "old_string",
                "Text to find") "," TOOL_PARAM_STR("new_string",
                "Replacement text")
                TOOL_SCHEMA_END_REQUIRED(
                    "\"path\",\"old_string\",\"new_string\""),
        tool_edit_file_execute);

    REGISTER_TOOL(
        "list_dir",
        "List files in the data directory, optionally filtered by path prefix.",
        TOOL_SCHEMA_BEGIN() TOOL_PARAM_STR("prefix", "Optional path prefix")
            TOOL_SCHEMA_END(),
        tool_list_dir_execute);

    /* Cron tools */
    REGISTER_TOOL(
        "cron_add",
        "Create a scheduled reminder or task. "
        "REQUIRED STEPS: 1) call get_current_time first, "
        "2) compute at_epoch = current_epoch + delay_seconds, "
        "3) call this tool with ALL required params in ONE call. "
        "at_epoch MUST be a plain integer (e.g. 1773403549), NOT an expression. "
        "Example for '2分钟后提醒我开会': "
        "{\"name\":\"meeting\",\"schedule_type\":\"at\","
        "\"at_epoch\":1773403669,\"message\":\"该开会了\"}. "
        "For recurring: schedule_type='every', interval_s=seconds. "
        "To schedule a tool action: set 'action' to tool name and "
        "'action_args' to its JSON args string.",
        TOOL_SCHEMA_BEGIN()
            TOOL_PARAM_STR("name", "Job name") "," TOOL_PARAM_ENUM("schedule_type", "Schedule type", "\"every\",\"at\"") "," TOOL_PARAM_NUM("interval_s", "Interval in seconds (for every)") "," TOOL_PARAM_NUM("at_epoch",
                "Target UNIX timestamp as a plain integer") "," TOOL_PARAM_STR("message",
                "Notification message sent to user when job fires") "," TOOL_PARAM_STR("channel", "Reply channel (default system)") "," TOOL_PARAM_STR("chat_id", "Reply chat_id") "," TOOL_PARAM_STR("action",
                "Tool name to execute when job fires (e.g. music_play)") "," TOOL_PARAM_STR("action_args",
                "JSON arguments for the action tool")
                TOOL_SCHEMA_END_REQUIRED("\"name\",\"schedule_type\",\"message\""),
        tool_cron_add_execute);

    REGISTER_TOOL_NO_PARAMS("cron_list", "List all scheduled cron jobs.",
        tool_cron_list_execute);

    REGISTER_TOOL("cron_remove", "Remove a scheduled cron job by its ID.",
        TOOL_SCHEMA_BEGIN()
            TOOL_PARAM_STR("job_id", "8-char hex job ID")
                TOOL_SCHEMA_END_REQUIRED("\"job_id\""),
        tool_cron_remove_execute);

    /* Fetch URL tool */
    REGISTER_TOOL(
        "fetch_url",
        "Fetch and read content from a specific web page or document. "
        "Use when you have a direct URL and need to read its full content. "
        "Ideal for GitHub repositories, documentation pages, release notes, "
        "or any specific webpage. Only supports HTTPS URLs.",
        TOOL_SCHEMA_BEGIN() TOOL_PARAM_STR("url", "The HTTPS URL to fetch")
            TOOL_SCHEMA_END_REQUIRED("\"url\""),
        tool_fetch_url_execute);

    /* Vision tool */
    REGISTER_TOOL(
        "analyze_image",
        "Capture the device screen and analyze it using a vision LLM. "
        "When called WITHOUT image_path, automatically runs fbcapture to take "
        "a screenshot, then sends it to the vision model for analysis. "
        "This is the ONLY tool you need for screenshot + recognition — do NOT "
        "call run_shell with fbcapture or lvctl snapshot separately. "
        "If image_path is provided, reads that file instead (no auto-capture).",
        TOOL_SCHEMA_BEGIN()
            TOOL_PARAM_STR("image_path", "Optional: path to an existing image "
                                         "file. Omit to auto-capture screen.") "," TOOL_PARAM_STR("prompt", "Question or instruction about the image")
                TOOL_SCHEMA_END(),
        tool_analyze_image_execute);

    /* Shell tool */
#if VELACLAW_SHELL_SECURITY == VELACLAW_SHELL_SECURITY_FULL
    REGISTER_TOOL(
        "run_shell",
        "Execute a NuttX shell (NSH) command and return its output. "
        "SECURITY MODE: FULL — Most commands and shell features are allowed. "
        "You can use pipes (|), redirects (>), command chaining (&&), and most "
        "utilities. "
        "BLOCKED: Only critical system commands (reboot, mkfs, insmod, etc.) are "
        "blocked. "
        "IMPORTANT: This is NuttX RTOS, NOT Linux. Do NOT use Linux-style flags. "
        "Examples: 'free' (not 'free -h'), 'ps' (not 'ps aux'), 'cat "
        "/proc/meminfo'. "
        "lvctl subcommands: 'lvctl snapshot take 1' then 'lvctl snapshot save "
        "/data/velaclaw/screen.png' for screenshots, "
        "'lvctl disp --dump' for display info, 'lvctl obj --dump-tree' for UI "
        "object tree, "
        "'lvctl indev --dump' for input device info, 'lvctl anim --dump' for "
        "animations, "
        "'lvctl refr --now' to force refresh. "
        "Skills can freely compose commands: 'curl wttr.in/Beijing | head -7', "
        "'ps | grep velaclaw', 'lvctl snapshot take 1 && lvctl snapshot save "
        "/data/screen.png'.",
        TOOL_SCHEMA_BEGIN()
            TOOL_PARAM_STR("command", "The NSH command to execute, e.g. 'free' "
                                      "or 'curl wttr.in/Beijing | head -7'")
                TOOL_SCHEMA_END_REQUIRED("\"command\""),
        tool_run_shell_execute);
#elif VELACLAW_SHELL_SECURITY == VELACLAW_SHELL_SECURITY_DENY
    /* Shell tool disabled in DENY mode */
#else
    REGISTER_TOOL(
        "run_shell",
        "Execute a NuttX shell (NSH) command and return its output. "
        "SECURITY MODE: ALLOWLIST — Only pre-approved safe commands are allowed. "
        "IMPORTANT: This is NuttX RTOS, NOT Linux. Do NOT use Linux flags. "
        "Allowed commands: free, ps, df, uptime, uname, cat, ls, echo, date, "
        "env, pwd, "
        "curl, wget, ping, nslookup, lvctl, nxplayer, jq, cut, uniq, head, tail, "
        "tr, "
        "wc, grep, sed, dmesg, hexdump, lua, quickjs. "
        "lvctl subcommands: 'lvctl snapshot take 1' then 'lvctl snapshot save "
        "/data/velaclaw/screen.png' for screenshots, "
        "'lvctl disp --dump' for display info, 'lvctl obj --dump-tree' for UI "
        "object tree, "
        "'lvctl indev --dump' for input device info, 'lvctl anim --dump' for "
        "animations, "
        "'lvctl refr --now' to force refresh. "
        "Pipes and redirects are NOT allowed in this mode. "
        "Dangerous commands (reboot, mkfs, dd, rm, etc.) are blocked.",
        TOOL_SCHEMA_BEGIN() TOOL_PARAM_STR(
            "command", "The NSH command to execute, e.g. 'free' or 'echo hello'")
            TOOL_SCHEMA_END_REQUIRED("\"command\""),
        tool_run_shell_execute);
#endif

    /* System state tools */
    REGISTER_TOOL_NO_PARAMS(
        "get_battery",
        "Get the device battery level (percent), charging status, voltage, "
        "and temperature.",
        tool_get_battery_execute);

    REGISTER_TOOL_NO_PARAMS(
        "get_wear_state",
        "Check whether the wearable device is currently being worn.",
        tool_get_wear_state_execute);

    REGISTER_TOOL_NO_PARAMS(
        "get_screen_state",
        "Check whether the device screen is currently on or off.",
        tool_get_screen_state_execute);

    /* Health / fitness tools */
    REGISTER_TOOL_NO_PARAMS(
        "get_heartrate",
        "Get the latest heart rate measurement in BPM from the wearable sensor.",
        tool_get_heartrate_execute);

    REGISTER_TOOL_NO_PARAMS("get_steps",
        "Get the current step count and step frequency from "
        "the pedometer sensor.",
        tool_get_steps_execute);

    /* Device control tools */
    REGISTER_TOOL(
        "vibrate",
        "Trigger a one-shot vibration on the device. "
        "duration_ms: vibration length in milliseconds (1-5000, default 200). "
        "amplitude: vibration strength 1-255 (default 128).",
        TOOL_SCHEMA_BEGIN() TOOL_PARAM_NUM(
            "duration_ms",
            "Duration in ms (1-5000)") "," TOOL_PARAM_NUM("amplitude",
            "Strength 1-255")
            TOOL_SCHEMA_END(),
        tool_vibrate_execute);

    /* Feishu document tools */
    REGISTER_TOOL(
        "feishu_doc_create",
        "Create a new Feishu (Lark) cloud document. "
        "Returns document_id and URL. "
        "folder_token defaults to the shared workspace folder if not specified.",
        TOOL_SCHEMA_BEGIN()
            TOOL_PARAM_STR("title", "Document title") "," TOOL_PARAM_STR(
                "folder_token", "Target folder token (optional, has default)")
                TOOL_SCHEMA_END_REQUIRED("\"title\""),
        tool_feishu_doc_create_execute);

    REGISTER_TOOL(
        "feishu_doc_write",
        "Write text content into an existing Feishu document. "
        "Each line becomes a paragraph block appended to the document. "
        "Use feishu_doc_create first to get a document_id.",
        TOOL_SCHEMA_BEGIN() TOOL_PARAM_STR(
            "document_id",
            "Feishu document ID") "," TOOL_PARAM_STR("content",
            "Text content (newlines "
            "create paragraphs)")
            TOOL_SCHEMA_END_REQUIRED("\"document_id\",\"content\""),
        tool_feishu_doc_write_execute);

    REGISTER_TOOL(
        "feishu_doc_read",
        "Read the plain-text content of a Feishu document by its document_id.",
        TOOL_SCHEMA_BEGIN() TOOL_PARAM_STR("document_id", "Feishu document ID")
            TOOL_SCHEMA_END_REQUIRED("\"document_id\""),
        tool_feishu_doc_read_execute);

    REGISTER_TOOL("feishu_doc_list",
        "List recent documents in a Feishu folder. "
        "If folder_token is omitted, lists from the root space.",
        TOOL_SCHEMA_BEGIN()
            TOOL_PARAM_STR("folder_token", "Folder token (optional)")
                TOOL_SCHEMA_END(),
        tool_feishu_doc_list_execute);

    /* Feishu group chat tools */
    REGISTER_TOOL(
        "feishu_chat_members",
        "List all members of a Feishu group chat. Returns each member's "
        "name and open_id. Use this to find a person's open_id before "
        "sending them an @mention message with feishu_send_mention.",
        TOOL_SCHEMA_BEGIN()
            TOOL_PARAM_STR("chat_id", "Feishu group chat ID (oc_xxx)")
                TOOL_SCHEMA_END_REQUIRED("\"chat_id\""),
        tool_feishu_chat_members_execute);

    REGISTER_TOOL(
        "feishu_send_mention",
        "Send a message that @mentions a specific person in a Feishu group chat. "
        "The mentioned user will receive a notification. "
        "IMPORTANT: Call this tool ONLY ONCE per request. After a successful "
        "call, "
        "immediately reply to the user confirming the message was sent. Do NOT "
        "repeat. "
        "The chat_id of the current conversation is in the 'Current Session' "
        "section "
        "of the system prompt — use it directly. "
        "If the user's message already contains open_id (e.g. from an @mention), "
        "use it directly without calling feishu_chat_members first.",
        TOOL_SCHEMA_BEGIN() TOOL_PARAM_STR("chat_id", "Feishu group chat ID "
                                                      "(oc_xxx)") "," TOOL_PARAM_STR("open_id",
            "Target user's open_id (ou_xxx)") "," TOOL_PARAM_STR("name",
            "Display name "
            "for the "
            "@mention") "," TOOL_PARAM_STR("text",
            "Message text after the @mention")
            TOOL_SCHEMA_END_REQUIRED("\"chat_id\",\"open_id\",\"text\""),
        tool_feishu_send_mention_execute);

    /* Music playback tools (URL mode) */
    REGISTER_TOOL("music_play",
        "Play a music track from a URL. If already playing, "
        "stops the current track first (cut-song). "
        "Supports local files (/data/...) and network URLs "
        "(http/https). "
        "Set autostart=false to prepare without playing. "
        "For raw PCM files (.pcm), specify format/sample_rate/channels "
        "or pass options string (e.g. "
        "'format=s16le,sample_rate=16000,channels=1'). "
        "If not specified for .pcm files, defaults to "
        "s16le/16kHz/mono.",
        TOOL_SCHEMA_BEGIN()
            TOOL_PARAM_STR("url", "Music URL or local file path") "," TOOL_PARAM_BOOL("autostart", "Start immediately (default true)") "," TOOL_PARAM_NUM("start_position_ms", "Seek position before start (default 0)") "," TOOL_PARAM_STR(
                "options", "Media format options for raw files, e.g. "
                           "format=s16le:sample_rate=16000:ch_layout="
                           "mono") "," TOOL_PARAM_STR("format",
                "PCM sample "
                "format: s16le, "
                "s32le, f32le, "
                "etc. (default "
                "s16le)") "," TOOL_PARAM_NUM("sample_rate",
                "Sample rate in Hz, e.g. 16000, 44100, 48000") "," TOOL_PARAM_NUM("channels",
                "Number of audio channels, 1=mono, 2=stereo")
                TOOL_SCHEMA_END_REQUIRED("\"url\""),
        tool_music_play_execute);

    REGISTER_TOOL_NO_PARAMS("music_pause",
        "Pause the currently playing music track.",
        tool_music_pause_execute);

    REGISTER_TOOL_NO_PARAMS(
        "music_resume", "Resume a paused music track, or start a prepared track.",
        tool_music_resume_execute);

    REGISTER_TOOL_NO_PARAMS("music_stop",
        "Stop music playback and release all resources.",
        tool_music_stop_execute);

    REGISTER_TOOL(
        "music_seek", "Seek to a specific position in the current music track.",
        TOOL_SCHEMA_BEGIN()
            TOOL_PARAM_NUM("position_ms", "Target position in milliseconds")
                TOOL_SCHEMA_END_REQUIRED("\"position_ms\""),
        tool_music_seek_execute);

    REGISTER_TOOL("music_set_volume",
        "Set the music stream volume. Range 0 (mute) to 100 (max).",
        TOOL_SCHEMA_BEGIN()
            TOOL_PARAM_NUM("volume", "Volume level 0-100")
                TOOL_SCHEMA_END_REQUIRED("\"volume\""),
        tool_music_set_volume_execute);

    REGISTER_TOOL_NO_PARAMS("music_status",
        "Get current music playback status including state, "
        "position, duration, volume, and track URL.",
        tool_music_status_execute);

    /* QuickApp launch tool */
    REGISTER_TOOL("launch_quickapp",
        "Launch a QuickApp by its package name. "
        "Use when the user wants to open or start a specific QuickApp.",
        TOOL_SCHEMA_BEGIN()
            TOOL_PARAM_STR("package_name", "The QuickApp package name to launch")
                TOOL_SCHEMA_END_REQUIRED("\"package_name\""),
        tool_launch_quickapp_execute);

    /* QuickApp exit tool */
    REGISTER_TOOL_NO_PARAMS("exit_quickapp",
        "Exit the current QuickApp and return to home screen. "
        "Use when the user wants to close or exit the running QuickApp.",
        tool_exit_quickapp_execute);

    build_tools_json_locked();
    syslog(LOG_INFO, "[%s] Tool registry initialized\n", TAG);
    return OK;
}

char* tool_registry_get_tools_json(void)
{
    char* copy;

    pthread_mutex_lock(&s_tools_mtx);
    build_tools_json_locked();
    copy = s_tools_json ? strdup(s_tools_json) : NULL;
    pthread_mutex_unlock(&s_tools_mtx);
    return copy;
}

void tool_registry_invalidate(void)
{
    pthread_mutex_lock(&s_tools_mtx);
    s_tools_dirty = true;
    pthread_mutex_unlock(&s_tools_mtx);
}

int tool_registry_execute(const char *name, const char *input_json,
    char *output, size_t output_size)
{
    /* Security guard check before any tool execution */
    size_t input_len = input_json ? strlen(input_json) : 0;
    tool_guard_result_t guard = tool_guard_check(name, input_json, input_len);

    if (guard != GUARD_ALLOW) {
        const char *reason = "unknown";
        switch (guard) {
        case GUARD_DENY_DISABLED:
            reason = "tool disabled by config";
            break;
        case GUARD_DENY_RATE_LIMIT:
            reason = "rate limit exceeded";
            break;
        case GUARD_DENY_INPUT_SIZE:
            reason = "input too large";
            break;
        case GUARD_DENY_INPUT_INVALID:
            reason = "invalid input";
            break;
        default:
            break;
        }
        syslog(LOG_WARNING, "[%s] Tool '%s' blocked: %s\n",
               TAG, name ? name : "(null)", reason);
        snprintf(output, output_size,
                 "Error: tool '%s' blocked — %s",
                 name ? name : "(null)", reason);
        return ERROR;
    }

    for (int i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            syslog(LOG_INFO, "[%s] Executing tool: %s\n", TAG, name);
            int ret = s_tools[i].execute(input_json, output, output_size);
            if (ret == OK) {
                tool_guard_record_call(name);
            }
            return ret;
        }
    }

    if (mcp_bridge_execute(name, input_json, output, output_size) == OK) {
        syslog(LOG_INFO, "[%s] Executed MCP tool: %s\n", TAG, name);
        tool_guard_record_call(name);
        return OK;
    }

    if (strncmp(name, "node.", 5) == 0) {
        int ret = node_manager_execute(name, input_json, output, output_size);
        if (ret == OK) {
            syslog(LOG_INFO, "[%s] Executed Node tool: %s\n", TAG, name);
            tool_guard_record_call(name);
        }
        return ret;
    }

    syslog(LOG_WARNING, "[%s] Unknown tool: %s\n", TAG, name);
    snprintf(output, output_size, "Error: unknown tool '%s'", name);
    return ERROR;
}
