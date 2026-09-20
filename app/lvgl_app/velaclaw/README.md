# VelaClaw

运行在 Vela/NuttX 嵌入式系统上的 AI Agent 框架。

多 LLM 后端 · ReAct 工具调用 · 多渠道接入 · 多节点协作 · 语音交互 · 低内存优化

所有运行 Vela OS 的智能设备（手表、眼镜、音箱、耳机等）都可以部署 VelaClaw，设备间通过 OpenClaw Node 协议互联互通。

## 功能亮点

🧠 **多 LLM 后端** — Kimi / 通义千问 / DeepSeek / GLM / MiMo / Claude / OpenAI 及任意 OpenAI 兼容接口，一条命令切换
🔧 **ReAct Agent** — 33 种内置工具，支持并行执行，自动推理-行动循环
📡 **多渠道接入** — CLI / 飞书 Bot / 微信 Bot / WebSocket / MQTT / 语音（ASR+TTS），统一消息总线
🌐 **多节点协作** — Hub 模式接受设备连接，Node 模式连接 OpenClaw Gateway，跨设备工具调用
🔌 **MCP 协议** — 外部客户端通过 MCP 注册工具，动态扩展 Agent 能力
📝 **Skills 系统** — Markdown 格式可扩展技能，10 个内置 Skill，对话即可创建新技能
🔒 **安全加固** — 工具白名单、敏感工具限流、Shell 三级策略、日志脱敏
💾 **低内存优化** — 堆检测、内存池、流式输出，适配手表级设备（~256KB RAM）
🔀 **智能路由** — 多后端 LLM Router，复杂度感知路由、自动故障转移、成本优化

## 快速开始

### 编译

```bash
# 克隆到 openvela 工程
git clone <your-git-repo-url>/vela_claw.git <openvela>/packages/velaclaw

# 编译并运行 QEMU
./build.sh vela_goldfish-arm64-v8a-ap --cmake
./emulator.sh cmake_out/vela_goldfish-arm64-v8a-ap
```

> 首次编译需在 `apps/CMakeLists.txt` 末尾 `add_subdirectory(builtin)` 之前加入：
> ```cmake
> if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/packages/CMakeLists.txt)
>   add_subdirectory(packages)
> endif()
> ```

### 基本使用

```bash
nsh> velaclaw set_llm kimi sk-xxxxxxxxxxxx       # 设置 LLM
nsh> velaclaw ask 现在几点了？                     # 对话
nsh> velaclaw ask 帮我搜索 NuttX 最新版本          # 工具调用（自动触发 web_search）
nsh> velaclaw set_tavily_key tvly-xxxxxxxxxxxx   # 设置搜索（可选）
nsh> velaclaw set_feishu_app cli_xxx xxxxxxxxxx  # 设置飞书 Bot（可选）
```

## 支持的 LLM 后端

| 预设 | 默认模型 | 说明 |
|------|----------|------|
| `kimi` | kimi-k2.5 | Moonshot AI |
| `qwen` | qwen-turbo | 通义千问 |
| `deepseek` | deepseek-chat | DeepSeek |
| `glm` | glm-4-flash | 智谱 AI |
| `mimo` | mimo-v2.5 | 小米大模型 Token Plan（V2.5 系列，低延迟配置） |
| `openai` | gpt-4o | OpenAI |
| `claude` | claude-sonnet-4-20250514 | Anthropic Claude |
| `openrouter` | openrouter/hunter-alpha | OpenRouter 聚合 |

```bash
nsh> velaclaw set_llm deepseek sk-xxxxxxxxxxxx   # 切换后端
nsh> velaclaw set_llm model mimo-v2.5            # 仅切换模型
nsh> velaclaw list_models --free                 # 列出可用模型（openrouter）
```

### LLM Router（多后端路由）

支持最多 4 个后端同时配置，按任务复杂度自动选择最优后端：

```bash
nsh> velaclaw router_set deepseek sk-xxx         # 添加后端
nsh> velaclaw router_set kimi sk-xxx             # 添加第二个后端
nsh> velaclaw router_profile eco                 # 切换路由策略（eco/auto/premium）
nsh> velaclaw router_status                      # 查看路由状态
```

## 内置工具（33 种）

| 分类 | 工具 | 说明 |
|------|------|------|
| 🔍 搜索 | `web_search` `news_search` `fetch_url` | Tavily/SerpAPI/Exa/NewsAPI |
| 📁 文件 | `read_file` `write_file` `edit_file` `list_dir` | 限制在 /data/velaclaw/ |
| ⏰ 定时 | `cron_add` `cron_list` `cron_remove` | 定时任务调度 |
| 🖼️ 视觉 | `analyze_image` | Vision LLM 图片识别/OCR/截屏分析 |
| 🐚 Shell | `run_shell` | NuttX 命令（三级安全策略） |
| ⌚ 设备 | `get_battery` `get_wear_state` `get_screen_state` `get_heartrate` `get_steps` `vibrate` | 传感器与控制 |
| 📄 飞书文档 | `feishu_doc_create` `feishu_doc_write` `feishu_doc_read` `feishu_doc_list` | 飞书云文档 CRUD |
| 💬 飞书群聊 | `feishu_chat_members` `feishu_send_mention` | 群成员查询、@提醒 |
| 🎵 音乐 | `music_play` `music_pause` `music_resume` `music_stop` `music_seek` `music_set_volume` `music_status` | PCM/WAV/MP3 |
| 📱 QuickApp | `launch_quickapp` `exit_quickapp` | 启动/退出快应用 |
| 🌤️ 其他 | `get_weather` `get_current_time` | 天气、时间 |

## 接入通道

| 通道 | 协议 | 说明 |
|------|------|------|
| CLI | NSH Shell | `ask <text>` 直接对话 |
| 飞书 Bot | WebSocket | 群聊/私聊，长连接 |
| 微信 Bot | HTTPS (iLink Bot) | QR 码登录，长轮询收消息 |
| WebSocket | WS (28789) | 外部客户端接入，Hub 模式 |
| MQTT | MQTT-C | IoT 设备集成 |
| Voice | PCM/ASR/TTS | 火山引擎后端，支持多厂商切换 |
| Node | OpenClaw | 多设备工具互调 |

详见 [接入通道文档](docs/channels.md)。

## Skills 系统

10 个内置 Skill，Markdown 格式，对话即可创建新技能：

| Skill | 说明 |
|-------|------|
| `weather` | 天气查询和预报 |
| `daily-briefing` | 个性化每日简报 |
| `skill-creator` | 教 Agent 创建新 Skill |
| `reminder` | 定时提醒 |
| `note-taker` | 快速笔记 |
| `translate` | 文本翻译 |
| `news-digest` | 新闻摘要 |
| `task-manager` | TODO 任务管理 |
| `system-health` | 系统状态检查 |
| `feishu-test` | 飞书集成测试 |

```bash
nsh> velaclaw ask 帮我创建一个翻译技能    # 对话创建
# 或手动放 .md 文件到 /data/velaclaw/skills/
```

## 配置

配置文件位于 `/data/velaclaw/config/`：

| 文件 | 说明 |
|------|------|
| `SOUL.md` | Agent 人格设定 |
| `USER.md` | 用户信息 |
| `config.json` | 运行时配置（MiMo 对话和语音服务 Key 等，CLI 命令自动持久化） |

板级 ROMFS 会提供隐藏模板 `/etc/.velaclaw.config.json`，首次启动时复制到
`/data/velaclaw/config/config.json`，之后可通过 CLI 或直接修改运行时配置。
其中 `api_key`、`model`、`llm_host` 等字段用于 MiMo 对话；语音输入和输出默认
使用 MiMo ASR/TTS，可通过 `mimo_api_key` 覆盖语音 Key，留空则复用 `api_key`。

## 文档

| 文档 | 说明 |
|------|------|
| [架构](docs/architecture.md) | 模块概览、数据流、内存管理、安全机制 |
| [接入通道](docs/channels.md) | 飞书 / 微信 / MQTT / Voice / WebSocket / 多节点 |
| [内置工具](docs/tools.md) | 33 种工具列表、Shell 安全策略 |
| [CLI 命令](docs/cli.md) | 完整命令参考（50+ 条） |
| [Skills 系统](docs/skills.md) | 内置 Skill、自定义 Skill、文件格式 |

## License

Apache-2.0
