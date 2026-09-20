# 架构

## 架构图

![VelaClaw Architecture](velaclaw-arch.png)

## 模块概览

```
velaclaw/
├── include/                  # 公共头文件
├── src/
│   ├── velaclaw_main.c       # 入口
│   ├── agent/                # ReAct 循环、内存池、上下文构建
│   ├── bus/                  # 消息总线
│   ├── cli/                  # NSH 命令
│   ├── config/               # 配置存储
│   ├── cron/                 # 定时任务
│   ├── feishu/               # 飞书 Bot（WS/HTTP/Protobuf）
│   ├── gateway/              # WebSocket 服务器
│   ├── heartbeat/            # 心跳检查
│   ├── llm/                  # LLM 代理（多后端）
│   ├── mcp/                  # MCP 服务器 + 工具注册
│   ├── memory/               # 长期记忆 + 会话管理
│   ├── mqtt/                 # MQTT 通道
│   ├── voice/                # 语音通道（ASR/TTS 多后端）
│   ├── security/             # 工具安全加固
│   ├── network/              # 网络管理
│   ├── node/                 # Node Client + Hub Manager
│   ├── proxy/                # HTTP 代理
│   ├── skills/               # 技能加载
│   ├── tls/                  # mbedTLS HTTPS
│   ├── tools/                # 33 种内置工具
│   ├── ui/                   # QR 码显示
│   └── weixin/               # 微信 Bot（iLink Bot API）
├── CMakeLists.txt
└── Kconfig
```

## 数据流

```
用户输入 (CLI/飞书/WS/MQTT/Voice)
    ↓
message_bus (inbound)
    ↓
agent_loop (ReAct 循环)
    ├── context_builder → 系统提示 + Skills 摘要
    ├── llm_proxy → LLM API 调用
    ├── tool_registry → 工具执行
    │   ├── 内置工具（直接调用）
    │   ├── mcp_bridge（外部 MCP）
    │   └── node_manager（远程 Node）
    └── session_mgr → 会话持久化
    ↓
message_bus (outbound)
    ↓
用户输出 (CLI/飞书/WS/MQTT/TTS)
```

## 内存管理

| 机制 | 说明 |
|------|------|
| 堆检测 | `mallinfo()` 实时查询，内存不足时拒绝请求 |
| 内存池 | 预分配工具输出缓冲区，避免频繁 malloc |
| 流式输出 | 1KB 起步按需扩展到 64KB |
| 安全分配 | 动态计算安全大小，保留 32KB 给其他子系统 |

## 安全机制

| 机制 | 说明 |
|------|------|
| 工具白名单 | `tool_enabled_<name>` 持久化开关 |
| 敏感工具限流 | 60s 窗口内最多 10 次调用 |
| 输入大小检查 | 单次工具调用上限 32KB |
| Shell 三级策略 | Allowlist / Full / Deny |
| 日志脱敏 | API Key 等敏感信息不打印 |
