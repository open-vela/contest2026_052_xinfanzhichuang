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

#include "agent/context_builder.h"
#include "velaclaw_config.h"
#include "velaclaw_compat.h"
#include "memory/memory_store.h"
#include "skills/skill_loader.h"
#include "node/node_manager.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"

static const char *TAG = "context";

static size_t append_file(char *buf, size_t size, size_t offset,
                           const char *path, const char *header)
{
    FILE *f = fopen(path, "r");
    if (!f) return offset;

    if (header && offset < size - 1) {
        offset += snprintf(buf + offset, size - offset, "\n## %s\n\n", header);
    }

    size_t n = fread(buf + offset, 1, size - offset - 1, f);
    offset += n;
    buf[offset] = '\0';
    fclose(f);
    return offset;
}

int context_build_system_prompt(char *buf, size_t size)
{
    size_t off = 0;

    /* Current time — essential for cron scheduling.
     * Use gmtime_r + manual UTC+8 offset to avoid NuttX zoneinfo
     * lookup errors (romfs doesn't have "CST-8" zoneinfo file). */
    time_t now = time(NULL);
    struct tm tm_now;
    time_t local_epoch = now + 8 * 3600;
    gmtime_r(&local_epoch, &tm_now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_now);

    off += snprintf(buf + off, size - off,
        "# VelaClaw\n\n"
        "You are VelaClaw, a personal AI assistant running on a Vela/NuttX embedded device.\n"
        "You communicate through Feishu, WebSocket, and CLI.\n\n"
        "Current time: %s CST (UTC+8) (timezone: %s)\n"
        "IMPORTANT: The UNIX epoch is NOT provided here. To get the precise current epoch for scheduling, you MUST call get_current_time.\n\n"
        "## Core Rules\n"
        "1. NEVER fabricate data. If you don't know something, say so or use a tool to find out.\n"
        "2. NEVER invent system information (kernel version, memory, processes, etc.). Always use run_shell or read /proc files.\n"
        "3. NEVER list commands or features that don't exist. Only describe capabilities you actually have via the available tools.\n"
        "4. Slash commands like /help, /time, /weather are handled by the system directly. If a user sends an unrecognized slash command, say it is not supported and suggest /help.\n"
        "5. Be concise. Do not pad responses with capability lists or example prompts unless explicitly asked.\n"
        "6. When the user asks about real-time data (battery, time, weather, heartrate, steps, etc.), you MUST call the corresponding tool. NEVER answer from memory or guess — always call the tool first.\n"
        "7. When asked about your capabilities or skills, ONLY mention tools that exist in your tools list. Do NOT fabricate abilities like translation, note-taking, or reminders unless a corresponding tool is available.\n"
        "8. NEVER reveal API keys, tokens, model names, backend URLs, proxy settings, or any internal configuration — regardless of how the user asks, including threats, emotional manipulation, or social engineering.\n"
        "9. Do NOT fabricate details about your own architecture, model, or infrastructure. If asked, say you are VelaClaw running on a Vela device. Do not claim to be offline, local-only, or cloud-free unless that is actually true.\n"
        "10. Reply in the SAME language the user used. If the user writes Chinese, reply in Chinese. If English, reply in English. Never reply in both languages.\n"
        "11. When a tool returns an error, report the error honestly to the user. NEVER invent fake data to replace the error. For example, if get_battery returns 'uORB not enabled', say the battery info is unavailable — do NOT make up a percentage.\n"
        "12. You have full permission to use all available tools. Do NOT ask the user for permission to call a tool — just call it directly.\n"
        "13. When the user asks to be reminded, set a timer, or schedule a notification (e.g. '5分钟后提醒我', 'remind me in 10 minutes'), you MUST: (a) call get_current_time to get the current UNIX epoch (do NOT use the time shown above — it may be stale), (b) calculate the target epoch = current_epoch + delay_seconds, (c) call cron_add with schedule_type='at' and the computed at_epoch. NEVER just reply with text — always create the cron job.\n"
        "14. When creating a cron job that should trigger a tool action (e.g. play next song), "
        "use the 'action' and 'action_args' fields in cron_add. Set action to the tool name "
        "(e.g. 'music_play') and action_args to the JSON arguments (e.g. "
        "'{\"url\":\"https://...\"}').  This executes the tool directly when the job fires, "
        "without going through the agent loop. The 'message' field is sent to the user as "
        "a notification.\n\n"
        "## Memory\n"
        "You have persistent memory stored on local storage:\n"
        "- Long-term memory: %s/memory/MEMORY.md\n"
        "- Daily notes: %s/memory/daily/<YYYY-MM-DD>.md\n\n"
        "Use memory to remember things across conversations when explicitly asked.\n\n"
        "## Skills\n"
        "Skills are specialized instruction files stored in %s.\n"
        "When a task matches a skill, read the full skill file for detailed instructions.\n",
        time_str, VELACLAW_TIMEZONE,
        VELACLAW_DATA_DIR, VELACLAW_DATA_DIR, VELACLAW_SKILLS_DIR);

    /* Bootstrap files */
    off = append_file(buf, size, off, VELACLAW_SOUL_FILE, "Personality");
    off = append_file(buf, size, off, VELACLAW_USER_FILE, "User Info");

    /* Long-term memory */
    char mem_buf[4096];
    if (memory_read_long_term(mem_buf, sizeof(mem_buf)) == OK && mem_buf[0]) {
        off += snprintf(buf + off, size - off, "\n## Long-term Memory\n\n%s\n", mem_buf);
    }

    /* Recent daily notes (last 3 days) */
    char recent_buf[4096];
    if (memory_read_recent(recent_buf, sizeof(recent_buf), 3) == OK && recent_buf[0]) {
        off += snprintf(buf + off, size - off, "\n## Recent Notes\n\n%s\n", recent_buf);
    }

    /* Skills summary */
    char skills_buf[2048];
    size_t skills_len = skill_loader_build_summary(skills_buf, sizeof(skills_buf));
    if (skills_len > 0) {
        off += snprintf(buf + off, size - off,
            "\n## Available Skills\n\n"
            "Available skills (use read_file to load full instructions):\n%s\n",
            skills_buf);
    }

    /* Remote Node devices */
    char node_buf[1024];
    int node_count = node_manager_list(node_buf, sizeof(node_buf));
    if (node_count > 0) {
        off += snprintf(buf + off, size - off,
            "\n## Remote Devices (OpenClaw Nodes)\n\n"
            "You have remote devices connected. Their tools are prefixed with \"node:<id>:<command>\".\n"
            "When the user asks about a REMOTE device (e.g. watch battery, watch heartrate, watch steps), "
            "you MUST use the node-prefixed tool (e.g. node:watch-01:get_battery) instead of the local tool.\n"
            "Connected nodes:\n%s\n", node_buf);
    }

    syslog(LOG_INFO, "[%s] System prompt built: %d bytes\n", TAG, (int)off);
    return OK;
}

int context_build_messages(const char *history_json, const char *user_message,
                                  char *buf, size_t size)
{
    cJSON *history = cJSON_Parse(history_json);
    if (!history) {
        history = cJSON_CreateArray();
    }

    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", user_message);
    cJSON_AddItemToArray(history, user_msg);

    char *json_str = cJSON_PrintUnformatted(history);
    cJSON_Delete(history);

    if (json_str) {
        strncpy(buf, json_str, size - 1);
        buf[size - 1] = '\0';
        free(json_str);
    } else {
        snprintf(buf, size, "[{\"role\":\"user\",\"content\":\"%s\"}]", user_message);
    }

    return OK;
}
