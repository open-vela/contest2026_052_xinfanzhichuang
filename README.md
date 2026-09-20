<<<<<<< ours
# contest2026_052_xinfanzhichuang

👋 欢迎参加 **2026 首届 openvela AI 硬件开发者大赛**！

这是组委会为你的队伍创建的**专属参赛仓库**（本仓为样例/模板，队伍编号 `052`；你看到的将是你自己的 `contest2026_<编号>_<队伍名>`）。比赛期间，你的全部参赛代码、打包产物与 AI Coding 日志都提交到这里。

> 本仓既是「代码仓」，又内置了一键拉取整套 openvela 工程的 `repo` 清单（manifest）。你只需跟它打交道，**自始至终只动一个文件夹**。

---

## 一、先读这些官方文档

**通用（所有赛道必读）：**

| 文档                                                                                                                                     | 用途                                           |
| ---------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------- |
| [《大赛总览》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/contest_overview.md)                        | 赛道、流程、评分、资源，建议先通读             |
| [《参赛代码提交指南》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/code_submission_guide.md)           | 仓库获取、提交流程、时间与权限（**以此为准**） |
| [《AI Coding 日志归集与提交手册》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md) | 如何导出 AI 对话日志并提交到 `logs/`           |

**按你的赛道选读（三选一）：**

| 赛道                  | 教程导航                                                                                                                                                 |
| --------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 快应用 / 手表应用创新 | [快应用教程导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/quickapp/quickapp_guide_index.md)                         |
| AI 硬件产品创新       | [AI 硬件赛道教程导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_hardware_guide_index.md)              |
| 新硬件适配            | [新硬件适配赛道教程导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/hardware_porting/hardware_porting_guide_index.md) |

---

## 二、第一步：拉取完整工程

用组委会提供的命令一键拉取「openvela 全量源码 + 你的专属仓」：

```bash
repo init -u https://github.com/open-vela/contest2026_052_xinfanzhichuang \
  -b dev-ai-contest-2026 -m contest2026_052_xinfanzhichuang.xml
repo sync -c -j8
```

同步后，你的整个仓库位于工作区的 `contest2026_052_xinfanzhichuang/`，openvela 全量源码在外层（`nuttx/`、`apps/`、`packages/`、`vendor/` 等）。

---

## 三、第二步：在哪里写代码

**只在自己的仓目录 `contest2026_052_xinfanzhichuang/` 里开发。** 不同作品形态放在对应子目录，manifest 会通过 `<linkfile>` 把它们**软链**到 openvela 编译树该在的位置——你不用手动 copy：

| 作品形态 | 你的代码放这里             | 系统自动映射到                                 |
| -------- | -------------------------- | ---------------------------------------------- |
| 应用     | `app/hello_app/`           | `packages/demos/contest2026_052_hello_app`     |
| 快应用   | `quickapp/hello_quickapp/` | `packages/apps/contest2026_052_hello_quickapp` |
| 板级适配 | `board/contest_board/`     | `vendor/openvela/boards/contest2026_052_board` |

> 用不到的形态目录可以删掉；新增作品时按同样规则加子目录，并在 `contest2026_052_xinfanzhichuang.xml` 里补一条 `<linkfile>` 映射即可。**生产仓库（packages/nuttx/vendor 等）零改动。**

建议仓库目录约定（便于评委定位）：

```text
app/ | quickapp/ | board/   # 你的作品代码
logs/                       # AI Coding 日志（主动导出后提交，格式见 logs/README.md）
README.md                   # 作品说明（提交前请改成你自己的，见第六节）
```

> 仓内附带了一个 `.gitignore.example`，给出了**编译产物**等不需要进仓的文件示例。如需启用，`cp .gitignore.example .gitignore` 后按需增删即可。**注意 `logs/` 下最终导出的 AI Coding 日志必须提交，不要忽略。**
>
> `logs/` 的目录结构与提交格式见 [logs/README.md](logs/README.md)。

---

## 四、第三步：编译与运行

编译/运行步骤随作品形态不同而不同，请参考你所在赛道的教程导航：

- 快应用 / 手表应用：[快应用教程导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/quickapp/quickapp_guide_index.md)（含模拟器与开发板部署）。
- AI 硬件产品创新：[AI 硬件赛道教程导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_hardware/ai_hardware_guide_index.md)（环境搭建、编译烧录、Skill 开发）。
- 新硬件适配：[新硬件适配赛道教程导航](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/hardware_porting/hardware_porting_guide_index.md)（BSP 移植、最小 NSH 基线）。

子目录已通过 manifest 中的 `<linkfile>` 软链进 openvela 编译树，因此构建在 openvela 工作区**根目录**（即你这个仓的上一级）进行。openvela 使用 `build.sh` 作为统一入口，接收一个 **board config 路径**作为参数：

```bash
# 进入 openvela 工作区根目录（你的仓的上一级）
cd ..

# 通用语法：第一个参数是 board config 路径，第二个参数可以是 menuconfig / distclean 等
./build.sh <board-config-path> [menuconfig|distclean] [-j8]
```

> 具体的 board config 路径、目标产物、模拟器/真机部署方式请以你所在赛道的教程导航为准。本仓 `app/` `quickapp/` `board/` 三个示例骨架对应的 Kconfig 选项可通过 `menuconfig` 启用。

---

## 五、第四步：提交作品

1. **fork** 你的专属仓 → 开发 → `git commit` 并推送 → 向专属仓发起 **Pull Request**，可**自行 review 并合入**（无需等组委会）。
2. **AI Coding 日志**：与 AI 工具的对话会自动记录到本机 staging（不会自动上传），需你**主动导出/打包**选定会话到仓内 `logs/` 目录后一并提交。详见[《AI Coding 日志归集与提交手册》](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/ai_coding_log_guide.md)。
3. 若需改动 **nuttx 等公共仓库**，不在本仓改，而是 fork 对应公共仓、以 PR 提交到 `dev-ai-contest-2026` 分支，由组委会 review 后合入。

> ⏰ **提交作品截止：9 月 20 日**。截止后统一收回 push 权限，仍可查看 / clone。
>
> 获奖后再按要求将作品 PR 至 openvela 上游对应仓库（走标准 PR + CI 流程）。

### 关于 PR 与 CLA

- 本仓所有改动通过 **Pull Request** 合入（分支保护强制，可自行合入自己的 PR）。
- 首次贡献需在[**官网签署 CLA**](https://openvela.com/#/community/cla)；PR 上会自动跑 `cla/signature` 检查，在官网签署成功后，在 PR 评论 `/check-cla` 复检即可通过。

---

## 六、提交前：把本 README 改成你的作品说明

本文件目前是组委会给的**使用说明书**。**作品提交前，请把它替换成你自己作品的说明**，方便评委快速了解你做了什么、怎么跑起来。建议至少包含以下内容：

```markdown
# <你的作品名>

## 一、作品简介
<一句话/一段话说明这个作品是什么、解决什么问题、亮点在哪>

## 二、选题方向
<快应用 / 手表应用创新 ｜ AI 硬件产品创新 ｜ 新硬件适配 ｜ 自定方向，并简述理由>

## 三、目录结构
<列出你这个仓里各目录/文件的作用，例如：>
- `app/xxx/`        — <说明>
- `board/xxx/`      — <说明>
- `quickapp/xxx/`   — <说明>
- `logs/`           — AI Coding 日志
- `docs/` 或其他    — <说明>

## 四、运行方式
<拉取工程后，如何编译、烧录/部署、运行的完整步骤；最好能让评委照着一步步复现>

## 五、AI Coding 使用说明
<说明本作品如何借助 AI 辅助开发：
- 在需求拆解 / 方案设计 / 编码 / 调试 / 文档等环节如何与 AI 协作；
- AI 对开发效率或质量带来的实际帮助。
完整对话日志见 logs/ 目录>
```

> 提示：将会根据「作品本身 + 你的 README 说明 + `logs/` 里的 AI Coding 日志」来理解和评估你的作品，README 写清楚很重要。

---

## 附：仓库命名规范

`contest2026_<编号>_<队伍名>` — 编号三位零填充；队名 slug（全小写、英文/拼音、连字符）。例：`contest2026_052_xinfanzhichuang`。
（仓库由组委会统一创建，**每队仅一个仓**，无需自行命名。）
=======
# 智慧家庭小助手

## 一、作品简介

“智慧家庭小助手”是一款基于 ArtInChip D13X / D133CBS Demo88 和 openvela 的 AI 硬件产品原型，面向家庭桌面、客厅中控和小型智能终端场景。作品将 1024x600 LVDS 触摸屏、物理按键、WiFi、BLE、经典蓝牙、RMII 以太网、DMIC 语音采集、音频播放和 PWM 背光等硬件能力，与 VelaClaw Agent 和 MiMo AI 服务结合，提供可视化、可触控、可按键和可语音操作的家庭助手体验。

系统支持文本问答、语音唤醒、ASR 语音识别、LLM 对话、TTS 流式播报、连续语音对话、天气/时间/网络信息展示以及工具调用等功能。当前唤醒词支持“你好，openvela”和“Hello, openvela”。系统通过消息总线统一连接 UI、CLI、语音和 Agent 通道，并通过 VAD 分段、SSE/分块响应解析、TTS 播放前暂停 DMIC、播放后重新打开 DMIC、会话 generation/chat_id 校验等机制提升嵌入式连续交互的稳定性。

## 二、选题方向

**AI 硬件产品创新 / 新硬件平台适配**

项目重点验证 openvela 在 D13X 新硬件平台上的综合适配能力，并将显示、输入、网络、音频和 AI Agent 组合为一个可以实际运行和演示的产品原型。端侧负责硬件驱动、UI、音频采集与播放、VAD、会话管理、工具编排和协议处理，云端负责 MiMo LLM、ASR 和 TTS 推理，形成端云协同架构。

## 三、主要功能

- LVGL 1024x600 三栏式主界面，显示设备、网络、时间、天气和 AI 状态。
- GT911 触摸交互，支持功能入口、列表和 AI 页面操作。
- PSADC 方向键和 WAKEUP 按键导航、选择和激活功能。
- AIC8800D40L WiFi STA 扫描、连接和网络状态显示。
- BLE GAP/GATT、外挂 BT8858A 经典蓝牙和 GMAC0 RMII 以太网支持。
- DMIC 16 kHz 单声道 PCM 采集、VAD 语音分段和 DMA 缓冲。
- MiMo ASR 语音识别、MiMo LLM 对话和 MiMo TTS 流式语音合成。
- “你好，openvela”/“Hello, openvela”唤醒和连续语音对话。
- NxPlayer / CPU Audio 音频播放、PWM 背光调节和 ADB over TCP 调试。
- VelaClaw Agent 的 ReAct 工具调用、Session、Memory、MCP 和 Skill 加载。

## 四、目录结构

```text
contest2026_052_xinfanzhichuang/
├── app/lvgl_app/                    # LVGL 应用、AI 页面和 VelaClaw Agent
│   ├── app_main/                    # 应用入口、显示初始化和背光
│   ├── ui_demo/                     # 三栏 UI、网络/蓝牙/AI 页面
│   └── velaclaw/                    # Agent、语音、TLS、工具、MCP、Skill
├── app/wifi_cmd/                    # WiFi 调试命令
├── app/bt_cmd/                      # BLE/GATT/GAP 调试命令
├── app/exbt_cmd/                    # BT8858A 经典蓝牙调试命令
├── app/dmic_cmd/                    # DMIC 采集调试命令
├── app/button_cmd/                  # 按键采样和事件验证命令
├── app/lcd_brightness/              # LCD 背光控制命令
├── boards/d13x_demo88-nor/          # D13X Demo88 板级配置和启动脚本
├── chips/artinchips/                # D13X HAL、显示、音频、网络和无线适配
├── framework/input/button/          # PSADC/WAKEUP 通用按键服务
├── pack/                            # 镜像打包脚本和资源
├── logs/                            # AI Coding 对话日志
├── openvela.xml                     # openvela 工程 manifest
└── contest2026_052_xinfanzhichuang.xml # 参赛仓 manifest
```

## 五、编译、打包与运行

### 5.1 环境位置

本仓通过 manifest 映射到 openvela 全量工程。编译在本仓上一级的 openvela 工作区根目录执行：

```bash
cd /path/proj
```

其中本仓目录为 `contest2026_052_xinfanzhichuang/`，完整工程还包括 `nuttx/`、`apps/`、`packages/`、`vendor/` 等目录。

### 5.2 配置和编译

使用 D13X Demo88 NOR 板卡的 `nsh_lvgl` 配置：

```bash
./build.sh contest2026_052_xinfanzhichuang/boards/d13x_demo88-nor/configs/nsh_lvgl -j8
```

也可以使用仓库提供的自动构建入口：

```bash
./contest2026_052_xinfanzhichuang/autobuild.sh d13x_demo88-nor nsh_lvgl build
```

### 5.3 打包镜像

编译成功后，执行：

```bash
./contest2026_052_xinfanzhichuang/autobuild.sh d13x_demo88-nor nsh_lvgl pack
```

生成的可烧录镜像位于：

```text
contest2026_052_xinfanzhichuang/pack/prebuilt/d13x_demo88-nor_v1.0.0.img
```

部分构建环境也会将镜像复制到 openvela 工作区根目录。将镜像烧录到 D13X Demo88 后，通过串口查看启动日志，系统启动后进入 LVGL 主界面。

### 5.4 运行和演示

1. 烧录 `d13x_demo88-nor_v1.0.0.img` 并启动开发板。
2. 确认 LVDS 屏幕显示 LVGL 三栏主界面，触摸屏和物理按键可用。
3. 在 WiFi 页面完成网络连接，或使用 GMAC0 RMII 以太网接入网络。
4. 在 AI 页面启动 VelaClaw 服务，按页面提示配置 MiMo 服务参数。
5. 通过文本输入或“你好，openvela”/“Hello, openvela”发起对话。
6. 观察 ASR、LLM、TTS 状态和语音播放结果，并进行连续语音交互。
7. 使用方向键、WAKEUP 按键、背光控制、网络页面和设备页面验证非触摸交互。

AI 服务依赖网络和运行时配置。默认配置不包含真实 API Key，密钥应通过设备运行时配置提供，不应提交到代码仓库或日志中。

## 六、AI Coding 使用说明

项目在需求分析、D13X 外设适配、LVGL UI 开发、WiFi/BLE/以太网调试、DMIC 音频链路、VelaClaw Agent 接入、连续语音稳定性优化、问题定位、测试清单和技术文档整理等环节使用 AI 辅助开发。

AI 主要用于：

- 阅读 openvela/NuttX、D13X HAL 和应用接口，生成适配骨架及调试命令框架。
- 协助分析 pinmux、DMA 缓冲、线程生命周期、TLS/HTTP/SSE 和音频播放回调问题。
- 协助设计消息总线、VAD 分段、语音会话状态机和旧响应丢弃机制。
- 生成边界条件、超时、队列背压、内存释放和异常恢复检查项。
- 辅助整理提交记录、测试结果、技术报告和 README。

所有 AI 生成或修改的代码均需结合编译、串口日志和真实硬件验证，特别是板级 pinmux、DMA 缓存一致性、网络连接、蓝牙退出、语音采集和 TTS 播放链路。

AI Coding 日志按参赛要求放在 `logs/` 目录，当前包含以下成员目录：

- `logs/00yearOfChina/`
- `logs/daichen-AS/`

日志清单由 `manifest.json` 管理，每个会话对应一个 JSONL 文件。提交前可使用仓库配套的日志校验工具检查格式。

## 七、提交材料状态

- 代码：当前参赛代码位于本仓库，最终提交前需确认工作区干净并完成最后一次编译/打包验证。
- 详设文档：已在远端完成定稿，内容覆盖摘要及 3.1～3.7 技术报告章节。
- 演示视频：已完成拍摄，按比赛平台要求提交视频文件或视频链接。
- CLA：已完成签署；创建 PR 后检查 `cla/signature`，如检查未刷新可在 PR 评论 `/check-cla`。
- AI Coding 日志：已归集到 `logs/`，提交前再次执行格式校验。

## 八、版本和提交说明

本项目参赛代码基于以下提交范围整理：

```text
起始提交：48d1549c5166190397abe00b08bae6a699c3b6b6
当前基线：ed8a192
```
>>>>>>> theirs
