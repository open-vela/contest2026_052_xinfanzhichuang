# Artinchip 镜像打包系统说明 (Packing System)

该目录负责将编译生成的 `nuttx.elf` 转换为可烧录的镜像文件 (.img)。目前系统已重构为支持多板子、多芯片的动态适配架构。

## 1. 目前打包现状

目前的 `pack.sh` 脚本具备以下特性：
- **动态识别**：自动从 `nuttx/.config` 读取 `BOARD_NAME` (如 `demo68-nor`) 和 `CHIP_NAME` (如 `d12x`)。
- **资源同步**：在打包前，自动从 `vendor/artinchip/boards/<CHIP>/<BOARD>/pack/` 目录下同步板级特有的配置文件。
- **配置覆盖**：支持板级配置（如 `image_cfg.json`, `partition.json`, `env.txt`）覆盖通用模版。
- **自动生成**：自动处理 ELF 转 BIN、文件系统镜像（FATFS/LittleFS）生成、以及最终的镜像封装。

## 2. 目录结构

- `pack.sh`: 主打包脚本。
- `prebuilt/`: 打包输出目录，存放编译产物、配置文件和最终生成的 `.img` 镜像。
- `resources/`: 通用的文件系统资源输入目录（`rodata/`, `data/`）。

## 3. 如何拓展支持新板子

若要支持一个新的开发板（例如 `demo68-nand`），请遵循以下步骤：

1. **创建板级打包目录**：
   在您的板子源目录下创建 `pack` 文件夹：
   `vendor/artinchip/boards/d12x/demo68-nand/pack/`

2. **放置板级配置文件**：
   将该板子特有的配置文件放入上述目录：
   - `image_cfg.json`: 镜像烧录布局描述。
   - `partition.json`: 分区表定义。
   - `env.txt`: 环境变量配置。
   - `d12x_os.its`: (可选) 如果需要自定义 ITB 生成逻辑。

3. **执行编译与打包**：
   在 SDK 根目录下运行：
   ```bash
   ./artbuild.sh
   ```
   打包系统检测到配置变化后，会自动加载 `demo68-nand/pack/` 下的配置进行打包，并生成 `d12x_demo68-nand_v1.0.0.img`。

## 4. 注意事项

- **nuttx.bin 兼容性**：脚本会自动创建一个 `nuttx.bin` 的软链接指向最新的芯片 BIN 文件，以确保旧的 `.its` 描述文件无需修改即可工作。
- **工具依赖**：打包依赖于 `vendor/artinchip/tools/scripts` 下的 Python 工具集，请确保环境已安装必要的 Python 依赖（如 `image` 处理库）。
