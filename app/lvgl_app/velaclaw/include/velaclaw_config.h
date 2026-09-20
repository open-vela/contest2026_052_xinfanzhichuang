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
/*
 * velaclaw_config.h — VelaClaw global configuration (Vela/NuttX version)
 *
 * Build-time secrets are loaded from velaclaw_secrets.h if present.
 * Runtime overrides are stored in the file-based config store
 * (config_store.c) under CONFIG_EXAMPLES_VELACLAW_VELA_DATA_DIR.
 */

#if __has_include("velaclaw_secrets.h")
#include "velaclaw_secrets.h"
#endif

/* Build-time secrets defaults */
#ifndef VELACLAW_SECRET_API_KEY
#define VELACLAW_SECRET_API_KEY ""
#endif
#ifndef VELACLAW_SECRET_MODEL
#define VELACLAW_SECRET_MODEL ""
#endif
#ifndef VELACLAW_SECRET_LLM_HOST
#define VELACLAW_SECRET_LLM_HOST ""
#endif
#ifndef VELACLAW_SECRET_LLM_PATH
#define VELACLAW_SECRET_LLM_PATH ""
#endif
#ifndef VELACLAW_SECRET_LLM_PORT
#define VELACLAW_SECRET_LLM_PORT ""
#endif
#ifndef VELACLAW_SECRET_PROXY_HOST
#define VELACLAW_SECRET_PROXY_HOST ""
#endif
#ifndef VELACLAW_SECRET_PROXY_PORT
#define VELACLAW_SECRET_PROXY_PORT ""
#endif
#ifndef VELACLAW_SECRET_SEARCH_KEY
#define VELACLAW_SECRET_SEARCH_KEY ""
#endif
#ifndef VELACLAW_SECRET_SERP_KEY
#define VELACLAW_SECRET_SERP_KEY ""
#endif
#ifndef VELACLAW_SECRET_EXA_KEY
#define VELACLAW_SECRET_EXA_KEY ""
#endif
#ifndef VELACLAW_SECRET_TAVILY_KEY
#define VELACLAW_SECRET_TAVILY_KEY ""
#endif
#ifndef VELACLAW_SECRET_NEWS_KEY
#define VELACLAW_SECRET_NEWS_KEY ""
#endif
#ifndef VELACLAW_SECRET_FEISHU_APP_ID
#define VELACLAW_SECRET_FEISHU_APP_ID ""
#endif
#ifndef VELACLAW_SECRET_FEISHU_APP_SECRET
#define VELACLAW_SECRET_FEISHU_APP_SECRET ""
#endif

/* ── Data directories ────────────────────────────────────── */
#ifndef CONFIG_EXAMPLES_VELACLAW_VELA_DATA_DIR
#define CONFIG_EXAMPLES_VELACLAW_VELA_DATA_DIR "/data/velaclaw"
#endif

#define VELACLAW_DATA_DIR CONFIG_EXAMPLES_VELACLAW_VELA_DATA_DIR
#define VELACLAW_CONFIG_DIR VELACLAW_DATA_DIR "/config"
#define VELACLAW_MEMORY_DIR VELACLAW_DATA_DIR "/memory"
#define VELACLAW_SESSION_DIR VELACLAW_DATA_DIR "/sessions"
#define VELACLAW_MEMORY_FILE VELACLAW_DATA_DIR "/memory/MEMORY.md"
#define VELACLAW_SOUL_FILE VELACLAW_DATA_DIR "/config/SOUL.md"
#define VELACLAW_USER_FILE VELACLAW_DATA_DIR "/config/USER.md"
#define VELACLAW_CONFIG_FILE VELACLAW_DATA_DIR "/config/config.json"
/* Board resources may provide an editable first-boot configuration. */
#ifndef VELACLAW_DEFAULT_CONFIG_FILE
#define VELACLAW_DEFAULT_CONFIG_FILE "/etc/.velaclaw.config.json"
#endif

/* ── Feishu (Lark) Bot ──────────────────────────────────────── */
#define VELACLAW_FEISHU_POLL_STACK (20 * 1024) /* WS client needs more stack \
                                                */
#define VELACLAW_FEISHU_POLL_PRIO 50
#define VELACLAW_FEISHU_MAX_MSG_LEN 4000 /* Feishu text message limit */

/* ── Agent Loop ─────────────────────────────────────────────── */
#define VELACLAW_AGENT_STACK (32 * 1024)
#define VELACLAW_AGENT_PRIO 60
#define VELACLAW_AGENT_CORE 0
#define VELACLAW_AGENT_MAX_HISTORY 10
#define VELACLAW_AGENT_MAX_TOOL_ITER 10
#define VELACLAW_MAX_TOOL_CALLS 4
#define VELACLAW_TOOL_NAME_REPEAT_MAX 4

/* ── Timezone (POSIX TZ format) ────────────────────────────── */
#define VELACLAW_TIMEZONE "CST-8"

/* ── LLM ────────────────────────────────────────────────────── */
#define VELACLAW_LLM_DEFAULT_MODEL ""
#define VELACLAW_LLM_MAX_TOKENS 4096
#define VELACLAW_LLM_MAX_TOKENS_OPENAI 16384
#define VELACLAW_LLM_MAX_TOKENS_MIMO 128
#define VELACLAW_LLM_API_HOST ""
#define VELACLAW_LLM_API_PATH "/v1/chat/completions"
#ifndef VELACLAW_LLM_API_URL
#define VELACLAW_LLM_API_URL ""
#endif
#define VELACLAW_LLM_API_VERSION "openai"
#define VELACLAW_LLM_MAX_RETRIES 3
#define VELACLAW_LLM_RETRY_BASE_SEC 2
#define VELACLAW_LLM_STREAM_BUF_SIZE (32 * 1024)
#define VELACLAW_LLM_MAX_RESP_SIZE \
    (512 * 1024) /* hard cap for growable resp buffer */

/* ── Qwen (Alibaba DashScope) backend constants ─────────────── */
#define VELACLAW_LLM_QWEN_HOST "dashscope.aliyuncs.com"
#define VELACLAW_LLM_QWEN_PATH "/compatible-mode/v1/chat/completions"
#define VELACLAW_LLM_QWEN_MODEL "qwen-turbo"

/* ── OpenRouter (unified multi-model gateway) ───────────────── */
#define VELACLAW_LLM_OPENROUTER_HOST "openrouter.ai"
#define VELACLAW_LLM_OPENROUTER_PATH "/api/v1/chat/completions"
#define VELACLAW_LLM_OPENROUTER_MODEL "qwen/qwen3-coder:free"

/* ── MiMo Token Plan (Xiaomi MiMo platform) ────────────────── */
#define VELACLAW_LLM_MIMO_HOST "token-plan-cn.xiaomimimo.com"
#define VELACLAW_LLM_MIMO_PATH "/v1/chat/completions"
#define VELACLAW_LLM_MIMO_MODEL "mimo-v2.5"

/* ── Message Bus ────────────────────────────────────────────── */
#define VELACLAW_BUS_QUEUE_LEN 16
#define VELACLAW_BUS_PUSH_TIMEOUT_MS 5000
#define VELACLAW_OUTBOUND_STACK (16 * 1024)
#define VELACLAW_OUTBOUND_PRIO 50
#define VELACLAW_OUTBOUND_CORE 0

/* ── Memory / Context ───────────────────────────────────────── */
#define VELACLAW_CONTEXT_BUF_SIZE (12 * 1024)

/* ── Cron / Heartbeat ──────────────────────────────────────── */
#define VELACLAW_CRON_FILE VELACLAW_DATA_DIR "/cron.json"
#define VELACLAW_CRON_FILE_MAX_SIZE (8 * 1024)
#define VELACLAW_CRON_MAX_JOBS 16
#define VELACLAW_CRON_CHECK_INTERVAL_MS (10 * 1000)
#define VELACLAW_CRON_ID_LEN 9 /* 8 hex chars + NUL */
#define VELACLAW_CRON_STACK (8 * 1024)
#define VELACLAW_CRON_PRIO 40
#define VELACLAW_HEARTBEAT_FILE VELACLAW_DATA_DIR "/HEARTBEAT.md"
#define VELACLAW_HEARTBEAT_INTERVAL_MS (30 * 60 * 1000)

/* ── Skills ─────────────────────────────────────────────────── */
#define VELACLAW_SKILLS_DIR VELACLAW_DATA_DIR "/skills/"
#define VELACLAW_SESSION_MAX_MSGS 20

/* ── WebSocket Gateway ──────────────────────────────────────── */
#define VELACLAW_WS_PORT 28789
#define VELACLAW_WS_MAX_CLIENTS 8

/* ── WS client thread stack ──────────────────────────────────── */
#define VELACLAW_WS_CLIENT_STACK (12 * 1024)

/* ── Serial CLI ─────────────────────────────────────────────── */
#define VELACLAW_CLI_STACK (16 * 1024)
#define VELACLAW_CLI_PRIO 30
#define VELACLAW_CLI_CORE 0

/* ── Config store keys (config store key strings) ─── */
#define VELACLAW_CFG_KEY_API_KEY "api_key"
#define VELACLAW_CFG_KEY_MODEL "model"
#define VELACLAW_CFG_KEY_PROXY_HOST "proxy_host"
#define VELACLAW_CFG_KEY_PROXY_PORT "proxy_port"
#define VELACLAW_CFG_KEY_SEARCH_KEY "search_key"
#define VELACLAW_CFG_KEY_SERP_KEY "serp_key"
#define VELACLAW_CFG_KEY_EXA_KEY "exa_key"
#define VELACLAW_CFG_KEY_TAVILY_KEY "tavily_key"
#define VELACLAW_CFG_KEY_NEWS_KEY "news_key"
#define VELACLAW_CFG_KEY_WIFI_SSID "wifi_ssid"
#define VELACLAW_CFG_KEY_WIFI_PASS "wifi_pass"
#define VELACLAW_CFG_KEY_FEISHU_APP_ID "feishu_app_id"
#define VELACLAW_CFG_KEY_FEISHU_APP_SECRET "feishu_app_secret"
#define VELACLAW_CFG_KEY_FEISHU_USER_TOKEN "feishu_user_token"
#define VELACLAW_CFG_KEY_LLM_HOST "llm_host"
#define VELACLAW_CFG_KEY_LLM_PATH "llm_path"
#define VELACLAW_CFG_KEY_GATEWAY_HOST "gateway_host"
#define VELACLAW_CFG_KEY_GATEWAY_PORT "gateway_port"
#define VELACLAW_CFG_KEY_GATEWAY_TOKEN "gateway_token"

/* ── Vision / Multimodal ─────────────────────────────────────── */
#define VELACLAW_VISION_MAX_IMAGE_SIZE \
    (256 * 1024) /* max JPEG file size: 256 KB */
#define VELACLAW_VISION_MAX_B64_SIZE (350 * 1024) /* base64 ≈ 4/3 × raw */
#define VELACLAW_VISION_MAX_TOKENS 4096
#define VELACLAW_VISION_DEFAULT_PROMPT                                      \
    "Please analyze this image in detail. If it contains text, tables, or " \
    "documents, extract and describe the content accurately."
#define VELACLAW_CAPTURE_PATH VELACLAW_DATA_DIR "/capture.jpg"
#define VELACLAW_TEST_IMAGE_PATH VELACLAW_DATA_DIR "/test.jpg"

/* ── Integration test image directory (Kconfig overridable) ──── */
#ifdef CONFIG_VELACLAW_TEST_IMAGE_DIR
#define VELACLAW_TEST_IMAGE_DIR CONFIG_VELACLAW_TEST_IMAGE_DIR
#else
#define VELACLAW_TEST_IMAGE_DIR VELACLAW_DATA_DIR "/test"
#endif

/* ── Node client identity ─────────────────────────────────── */
#ifndef CONFIG_EXAMPLES_VELACLAW_VELA_NODE_ID
#define CONFIG_EXAMPLES_VELACLAW_VELA_NODE_ID "watch-01"
#endif
#ifndef CONFIG_EXAMPLES_VELACLAW_VELA_NODE_DISPLAY_NAME
#define CONFIG_EXAMPLES_VELACLAW_VELA_NODE_DISPLAY_NAME "Smart Watch"
#endif

#define VELACLAW_NODE_ID CONFIG_EXAMPLES_VELACLAW_VELA_NODE_ID
#define VELACLAW_NODE_DISPLAY_NAME \
    CONFIG_EXAMPLES_VELACLAW_VELA_NODE_DISPLAY_NAME

/* ── Channel identifiers ────────────────────────────────────── */
#define VELACLAW_CHAN_WEBSOCKET "websocket"
#define VELACLAW_CHAN_CLI "cli"
#define VELACLAW_CHAN_SYSTEM "system"
#define VELACLAW_CHAN_FEISHU "feishu"
#define VELACLAW_CHAN_MQTT "mqtt"
#define VELACLAW_CHAN_WEIXIN "weixin"

/* ── WeChat Channel ─────────────────────────────────────────── */
#define VELACLAW_WEIXIN_STACK (12 * 1024)
#define VELACLAW_WEIXIN_PRIO 45

/* ── MQTT Channel ───────────────────────────────────────────── */
#define VELACLAW_MQTT_DEFAULT_PORT 1883
#define VELACLAW_MQTT_KEEPALIVE 60
#define VELACLAW_MQTT_BUF_SIZE 1024
#define VELACLAW_MQTT_STACK (12 * 1024)
#define VELACLAW_MQTT_PRIO 45
#define VELACLAW_MQTT_SYNC_INTERVAL_MS 200
#define VELACLAW_MQTT_TOPIC_IN_DEFAULT "velaclaw/in"
#define VELACLAW_MQTT_TOPIC_OUT_DEFAULT "velaclaw/out"

#define VELACLAW_CFG_KEY_MQTT_BROKER "mqtt_broker"
#define VELACLAW_CFG_KEY_MQTT_CLIENT_ID "mqtt_client_id"
#define VELACLAW_CFG_KEY_MQTT_TOPIC_IN "mqtt_topic_in"
#define VELACLAW_CFG_KEY_MQTT_TOPIC_OUT "mqtt_topic_out"
#define VELACLAW_CFG_KEY_MQTT_USERNAME "mqtt_username"
#define VELACLAW_CFG_KEY_MQTT_PASSWORD "mqtt_password"

/* ── Voice Channel ──────────────────────────────────────────── */
#define VELACLAW_CHAN_VOICE "voice"

#define VELACLAW_VOICE_STACK (16 * 1024)
#define VELACLAW_VOICE_PRIO 50

#ifndef CONFIG_EXAMPLES_VELACLAW_VOICE_BACKEND_MIMO
#define CONFIG_EXAMPLES_VELACLAW_VOICE_BACKEND_MIMO 1
#endif
#ifndef CONFIG_EXAMPLES_VELACLAW_VOICE_BACKEND_VOLC
#define CONFIG_EXAMPLES_VELACLAW_VOICE_BACKEND_VOLC 1
#endif
#ifndef CONFIG_EXAMPLES_VELACLAW_VOICE_ASR_DEFAULT_BACKEND
#define CONFIG_EXAMPLES_VELACLAW_VOICE_ASR_DEFAULT_BACKEND "mimo"
#endif
#ifndef CONFIG_EXAMPLES_VELACLAW_VOICE_TTS_DEFAULT_BACKEND
#define CONFIG_EXAMPLES_VELACLAW_VOICE_TTS_DEFAULT_BACKEND "mimo"
#endif

/* MiMo audio endpoints use the OpenAI-compatible chat completions API. */
#ifndef VELACLAW_MIMO_HOST
#define VELACLAW_MIMO_HOST "token-plan-cn.xiaomimimo.com"
#endif
#ifndef VELACLAW_MIMO_PORT
#define VELACLAW_MIMO_PORT "443"
#endif
#ifndef VELACLAW_MIMO_PATH
#define VELACLAW_MIMO_PATH VELACLAW_LLM_MIMO_PATH
#endif
#ifndef VELACLAW_MIMO_ASR_MODEL
#define VELACLAW_MIMO_ASR_MODEL "mimo-v2.5-asr"
#endif
#ifndef VELACLAW_MIMO_TTS_MODEL
#define VELACLAW_MIMO_TTS_MODEL "mimo-v2.5-tts"
#endif
#ifndef VELACLAW_MIMO_TTS_VOICE
#define VELACLAW_MIMO_TTS_VOICE "mimo_default"
#endif
#ifndef VELACLAW_MIMO_TTS_SAMPLE_RATE
#define VELACLAW_MIMO_TTS_SAMPLE_RATE 24000
#endif
#ifndef VELACLAW_VOICE_WAKE_WORD
#define VELACLAW_VOICE_WAKE_WORD "你好，哦盆喂啦/哈喽，哦盆喂啦"
#endif
#define VELACLAW_VOICE_IDLE_TIMEOUT_MS 60000
#define VELACLAW_VOICE_MAX_UTTERANCE_MS 6000
#define VELACLAW_VOICE_MIN_UTTERANCE_MS 500
#define VELACLAW_VOICE_WAKE_MAX_UTTERANCE_MS 3000
#define VELACLAW_VOICE_WAKE_MIN_UTTERANCE_MS 500
#define VELACLAW_VOICE_WAKE_VAD_SILENCE_MS 800
#define VELACLAW_VOICE_VAD_SILENCE_MS 800
#define VELACLAW_VOICE_VAD_START_CHUNKS 2
#define VELACLAW_VOICE_VAD_MIN_ENERGY 500
#define VELACLAW_VOICE_TTS_PIPE_QUEUE_SIZE (64 * 1024)
#define VELACLAW_VOICE_TTS_PIPE_STALL_MS 300

/* Doubao TTS V3 API endpoint (HTTP Chunked, x-api-key auth) */
#define VELACLAW_DOUBAO_TTS_HOST "openspeech.bytedance.com"
#define VELACLAW_DOUBAO_TTS_PORT "443"
#define VELACLAW_DOUBAO_TTS_V3_PATH "/api/v3/tts/unidirectional"
#define VELACLAW_DOUBAO_TTS_RESOURCE "volc.service_type.10029"

/* Doubao streaming ASR WebSocket API (V2) */
#define VELACLAW_DOUBAO_ASR_HOST "openspeech.bytedance.com"
#define VELACLAW_DOUBAO_ASR_PORT "443"
#define VELACLAW_DOUBAO_ASR_WS_PATH "/api/v2/asr"

/* Default ASR cluster (streaming, common Chinese) */
#define VELACLAW_DOUBAO_ASR_CLUSTER "volcengine_streaming_common"

/* Audio device paths (platform-specific) */
#ifndef VELACLAW_AUDIO_CAPTURE_DEV
#define VELACLAW_AUDIO_CAPTURE_DEV "/dev/audio/pcm0c"
#endif
#ifndef VELACLAW_AUDIO_PLAYBACK_DEV
#define VELACLAW_AUDIO_PLAYBACK_DEV "/dev/audio/pcm0p"
#endif

/* Audio chunk size for streaming: 3200 bytes = 100ms at 16kHz/16bit/mono */
#define VELACLAW_ASR_CHUNK_SIZE 3200

/* Audio parameters: PCM 16-bit signed LE, 16kHz, mono */
#define VELACLAW_VOICE_SAMPLE_RATE 16000
#define VELACLAW_VOICE_CHANNELS 1
#define VELACLAW_VOICE_BITS 16
#define VELACLAW_VOICE_FRAME_MS 20

/* Buffer sizes — keep small to avoid OOM during concurrent agent+voice.
 * 128KB PCM ≈ 4s at 16kHz/16bit/mono; enough for typical TTS output.
 * Recording uses the same cap; 128KB ≈ 4s of audio which is fine for
 * short voice commands.  For longer recordings, increase as needed. */
#define VELACLAW_VOICE_PCM_BUF_SIZE (128 * 1024)
#define VELACLAW_VOICE_RESP_BUF_SIZE (128 * 1024)
#define VELACLAW_VOICE_B64_BUF_SIZE (96 * 1024)

/* Config store keys */
#define VELACLAW_CFG_KEY_VOICE_ASR_BACKEND "voice_asr_backend"
#define VELACLAW_CFG_KEY_VOICE_TTS_BACKEND "voice_tts_backend"
#define VELACLAW_CFG_KEY_VOICE_WAKE_WORD "voice_wake_word"
#define VELACLAW_CFG_KEY_VOICE_IDLE_TIMEOUT_MS "voice_idle_timeout_ms"
#define VELACLAW_CFG_KEY_VOICE_MAX_UTTERANCE_MS "voice_max_utterance_ms"
#define VELACLAW_CFG_KEY_VOICE_VAD_MIN_ENERGY "voice_vad_min_energy"
#define VELACLAW_CFG_KEY_MIMO_API_KEY "mimo_api_key"
#define VELACLAW_CFG_KEY_MIMO_HOST "mimo_host"
#define VELACLAW_CFG_KEY_MIMO_PORT "mimo_port"
#define VELACLAW_CFG_KEY_MIMO_PATH "mimo_path"
#define VELACLAW_CFG_KEY_MIMO_ASR_MODEL "mimo_asr_model"
#define VELACLAW_CFG_KEY_MIMO_TTS_MODEL "mimo_tts_model"
#define VELACLAW_CFG_KEY_MIMO_TTS_VOICE "mimo_tts_voice"
#define VELACLAW_CFG_KEY_VOLC_APPKEY "volc_appkey"
#define VELACLAW_CFG_KEY_VOLC_TOKEN "volc_token"
#define VELACLAW_CFG_KEY_VOLC_API_KEY "volc_api_key"
#define VELACLAW_CFG_KEY_VOLC_SPEAKER "volc_speaker"
#define VELACLAW_CFG_KEY_VOLC_CLUSTER "volc_cluster"
#define VELACLAW_CFG_KEY_VOLC_ASR_CLUSTER "volc_asr_cluster"

/* Default TTS speaker and cluster */
#define VELACLAW_VOICE_DEFAULT_SPEAKER "zh_male_beijingxiaoye_emo_v2_mars_bigtts"
#define VELACLAW_VOICE_DEFAULT_CLUSTER "volcano_tts"

/* ── Tool Guard (security) ──────────────────────────────────── */
#define VELACLAW_TOOL_MAX_INPUT_LEN       (32 * 1024)
#define VELACLAW_TOOL_RATE_LIMIT_WINDOW_SEC 60
#define VELACLAW_TOOL_RATE_LIMIT_MAX_CALLS  10

/* ── Shell security policy ──────────────────────────────────── */
#define VELACLAW_SHELL_SECURITY_ALLOWLIST 0 /* Whitelist mode (production) */
#define VELACLAW_SHELL_SECURITY_FULL 1 /* Full access (development/skills) */
#define VELACLAW_SHELL_SECURITY_DENY 2 /* Disabled */

#ifdef CONFIG_EXAMPLES_VELACLAW_VELA_SHELL_ALLOWLIST
#define VELACLAW_SHELL_SECURITY VELACLAW_SHELL_SECURITY_ALLOWLIST
#elif defined(CONFIG_EXAMPLES_VELACLAW_VELA_SHELL_FULL)
#define VELACLAW_SHELL_SECURITY VELACLAW_SHELL_SECURITY_FULL
#elif defined(CONFIG_EXAMPLES_VELACLAW_VELA_SHELL_DENY)
#define VELACLAW_SHELL_SECURITY VELACLAW_SHELL_SECURITY_DENY
#else
#define VELACLAW_SHELL_SECURITY VELACLAW_SHELL_SECURITY_ALLOWLIST /* default \
                                                                   */
#endif
