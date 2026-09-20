#
# Copyright (C) 2026 Xiaomi Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

############################################################################
# app/lvgl_app/velaclaw/Makefile
############################################################################

include $(APPDIR)/Make.defs

MODULE = $(CONFIG_EXAMPLES_VELACLAW_VELA)
VELACLAW_ROOT = $(APPDIR)/../contest2026_052_xinfanzhichuang/app/lvgl_app/velaclaw
AI_AGENT_ROOT = $(APPDIR)/../packages/ai_agent
AIC_CHIP_ROOT = $(APPDIR)/../contest2026_052_xinfanzhichuang/chips/artinchips

# Include paths

CFLAGS += ${INCDIR_PREFIX}$(VELACLAW_ROOT)/include
CFLAGS += ${INCDIR_PREFIX}$(VELACLAW_ROOT)/src
CFLAGS += ${INCDIR_PREFIX}$(VELACLAW_ROOT)/tests
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../contest2026_052_xinfanzhichuang/framework/include
CFLAGS += ${INCDIR_PREFIX}$(AI_AGENT_ROOT)/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/netutils/cjson/cJSON
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/crypto/mbedtls/mbedtls/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../frameworks/system/topics/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../frameworks/system/vibrator
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/../frameworks/multimedia/media/include
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/netutils/mqttc/MQTT-C/include
CFLAGS += ${INCDIR_PREFIX}$(AIC_CHIP_ROOT)/include
CFLAGS += ${INCDIR_PREFIX}$(AIC_CHIP_ROOT)/include/drv
CFLAGS += ${INCDIR_PREFIX}$(AIC_CHIP_ROOT)/include/hal
CFLAGS += ${INCDIR_PREFIX}$(AIC_CHIP_ROOT)/common/include
CFLAGS += ${INCDIR_PREFIX}$(AIC_CHIP_ROOT)/hal/include

# Source files

CSRCS += src/bus/message_bus.c
CSRCS += src/agent/agent_loop.c
CSRCS += src/agent/agent_trace.c
CSRCS += src/agent/context_builder.c
CSRCS += src/llm/llm_proxy.c
CSRCS += src/llm/llm_router.c
CSRCS += src/llm/llm_cache.c
CSRCS += src/memory/memory_store.c
CSRCS += src/memory/session_mgr.c
CSRCS += src/proxy/http_proxy.c
CSRCS += src/tls/vela_tls.c
CSRCS += src/feishu/feishu_bot.c
CSRCS += src/feishu/feishu_ws.c
CSRCS += src/feishu/feishu_http.c
CSRCS += src/feishu/feishu_proto.c
CSRCS += src/feishu/feishu_recv.c
CSRCS += src/feishu/feishu_send.c
CSRCS += src/tools/tool_registry.c
CSRCS += src/tools/tool_files.c
CSRCS += src/tools/tool_get_time.c
CSRCS += src/tools/tool_web_search.c
CSRCS += src/tools/tool_cron.c
CSRCS += src/tools/tool_fetch_url.c
CSRCS += src/tools/tool_feishu_doc.c
CSRCS += src/tools/tool_feishu_chat.c
CSRCS += src/tools/tool_vision.c
CSRCS += src/tools/tool_shell.c
CSRCS += src/tools/tool_system.c
CSRCS += src/tools/tool_health.c
CSRCS += src/tools/tool_control.c
CSRCS += src/tools/tool_media.c
CSRCS += src/tools/tool_proxyquickapp.c
CSRCS += src/tools/mcp_bridge.c
CSRCS += src/security/tool_guard.c
CSRCS += src/mcp/mcp_server.c
CSRCS += src/mcp/mcp_tool_registry.c
CSRCS += src/mcp/mcp_builtin_tools.c
CSRCS += src/node/node_client.c
CSRCS += src/node/node_manager.c
CSRCS += src/mqtt/mqtt_channel.c
CSRCS += src/gateway/ws_server.c
CSRCS += src/cli/nsh_commands.c
CSRCS += src/network/network_manager.c
CSRCS += src/config/config_store.c
CSRCS += src/cron/cron_service.c
CSRCS += src/heartbeat/heartbeat.c
CSRCS += src/skills/local_skill.c
CSRCS += src/skills/skill_loader.c
CSRCS += src/velaclaw_service.c
CSRCS += src/voice/voice_channel.c
CSRCS += src/voice/voice_tts.c
CSRCS += src/voice/voice_asr.c
CSRCS += src/voice/voice_vad.c
CSRCS += src/voice/voice_wake.c
CSRCS += src/voice/mimo_asr.c
CSRCS += src/voice/mimo_tts.c
CSRCS += src/voice/volc_tts.c
CSRCS += src/voice/volc_asr.c
CSRCS += src/voice/audio_capture.c
CSRCS += src/voice/audio_playback.c
CSRCS += src/weixin/weixin_channel.c
CSRCS += src/stubs.c
CSRCS += src/bus/message_bus_tap.c
VPATH += :$(AI_AGENT_ROOT)/src/ui
CSRCS += qrcode_display.c

ifeq ($(CONFIG_VELACLAW_TEST),y)
CSRCS += tests/test_vision_integ.c
endif

MAINSRC = src/velaclaw_main.c
MAINSRC += src/velaclaw_ask_main.c

PROGNAME  = $(CONFIG_EXAMPLES_VELACLAW_VELA_PROGNAME)
PROGNAME  += ask
PRIORITY  = $(CONFIG_EXAMPLES_VELACLAW_VELA_PRIORITY)
PRIORITY  += $(CONFIG_EXAMPLES_VELACLAW_VELA_PRIORITY)
STACKSIZE = $(CONFIG_EXAMPLES_VELACLAW_VELA_STACKSIZE)
STACKSIZE += 12288

include $(APPDIR)/Application.mk
