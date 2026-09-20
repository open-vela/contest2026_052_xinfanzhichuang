# ArtInChip GMAC0 Ethernet

本目录是 D13x + openvela/NuttX 的 GMAC0 以太网移植代码，当前按
D133CBS-QFN88-V1-2 板卡上的 RTL8201F RMII PHY 配置。

## 当前配置

关键 Kconfig 位于 `chips/artinchips/peripheral/ethernet/Kconfig`，当前
`boards/d13x_demo88-nor/configs/nsh_lvgl/defconfig` 已启用：

```text
CONFIG_AIC_USING_GMAC0=y
CONFIG_AIC_DEV_GMAC0_CLKOUT2_25M=y
CONFIG_AIC_DEV_GMAC0_MACADDR="02:22:44:88:77:66"
CONFIG_AIC_DEV_GMAC0_PHYADDR=0
CONFIG_AIC_DEV_GMAC0_PHYRST_GPIO="PE.6"
CONFIG_NET=y
CONFIG_NETUTILS_DHCPC=y
CONFIG_SYSTEM_PING=y
```

默认时钟/管脚假设：

| 信号 | 管脚 | 说明 |
| --- | --- | --- |
| RXD1 | PE0 | GMAC0 RMII function 2 |
| RXD0 | PE1 | GMAC0 RMII function 2 |
| CRS_DV | PE2 | GMAC0 RMII function 2 |
| REFCLK | PE3 | PHY 输出 50 MHz 给 MAC |
| TXD1 | PE4 | GMAC0 RMII function 2，与 UART1 冲突 |
| TXD0 | PE5 | GMAC0 RMII function 2，与 UART1 冲突 |
| PHY reset | PE6 | GPIO 输出，低有效复位 |
| TXEN | PE7 | GMAC0 RMII function 2 |
| MDC | PE8 | MDIO 时钟 |
| MDIO | PE9 | MDIO 数据 |
| CLK_OUT2 | PE10 | 输出 25 MHz 给 RTL8201F |

开启 `CONFIG_AIC_USING_GMAC0` 后，板级 pinmux 会跳过 UART1 的 PE4/PE5
配置，避免和 RMII TXD1/TXD0 抢管脚。此时 `/dev/ttyS1` 可能仍注册，但
PE4/PE5 不能再当 UART1 外部管脚使用。

## 编译验证

从 openvela 工作区根目录执行：

```bash
cd ~/daichen/Work/Code/d13x_vela_0630
./contest2026_052_xinfanzhichuang/tools/artbuild.sh d13x_demo88-nor nsh_lvgl
```

如果只是复查 NuttX 编译，且已经完成过 configure，也可以执行：

```bash
cd ~/daichen/Work/Code/d13x_vela_0630
make -C nuttx EXTRAFLAGS="-Wno-cpp -Wno-deprecated-declarations" -j8
```

成功时会生成 `nuttx/nuttx.bin` 和 `nuttx/nuttx.elf`。如果构建停在
`mkallsyms.py` 且返回 22，先确认主机 Python 依赖：

```bash
python3 -c "import elftools, cxxfilt"
python3 -m pip install --user pyelftools cxxfilt
```

## 上板前硬件检查

1. PE10 应输出 25 MHz 到 RTL8201F 时钟输入。
2. RTL8201F 应输出 50 MHz RMII REFCLK 到 PE3。
3. PE6 上电后应先拉低再拉高，完成 PHY 复位。
4. PE8/PE9 的 MDC/MDIO 需要能读到 PHY ID，PHY 地址默认为 0。
5. 网口连接到有 DHCP 的路由器/交换机，或连接到配置了静态 IP 的 PC。

如果没有示波器，至少观察网口 LINK/ACT 灯；没有 LINK 灯时先不要排查协议栈，
优先检查 PHY 供电、25 MHz、50 MHz、复位和网线。

## 串口日志验证

烧录启动后，在 NSH 串口观察以下日志：

```text
Start to initialize GMAC0 ethernet
GMAC0 PHY addr 0 id 0x....:0x....
GMAC0 registered as eth0 02:22:44:88:77:66
GMAC0 link UP: 100M full duplex flowctl ...
```

判断标准：

- 能看到 `registered as eth0`：NuttX lower-half 注册成功。
- PHY ID 不是 `0x0000:0x0000` 或 `0xffff:0xffff`：MDIO 基本可用。
- 插上网线后出现 `link UP`：PHY 自协商和 MAC 链路状态基本可用。

## NSH 网络验证

当前 `nsh_lvgl` 配置禁用了 `ifup/ifdown` 命令，驱动初始化时会自动执行
`netdev_ifup()`。进入 NSH 后用 `ifconfig` 和 `ping` 验证。

查看网卡：

```text
nsh> ifconfig
```

DHCP 获取地址：

```text
nsh> ifconfig eth0 dhcp
nsh> ifconfig eth0
nsh> ping <网关IP>
```

如果 DHCP 环境不确定，建议先用 PC 直连做静态 IP 验证：

1. PC 有线网卡配置为 `192.168.10.1/24`。
2. 板端配置同网段静态地址：

```text
nsh> ifconfig eth0 192.168.10.2 netmask 255.255.255.0
nsh> ping 192.168.10.1
```

再从 PC 侧验证：

```bash
ping 192.168.10.2
```

能双向 ping 通，说明 GMAC TX/RX、PHY 链路、ARP 和 ICMP 已经基本正常。

## 常见问题

### 没有 eth0

- 确认 defconfig 中 `CONFIG_AIC_USING_GMAC0=y`。
- 确认 `chips/artinchips/Kconfig` source 了 `peripheral/ethernet/Kconfig`。
- 确认 `chips/artinchips/peripheral/Make.defs` include 了
  `chip/peripheral/ethernet/Make.defs`。
- 确认启动日志里执行了 `Start to initialize GMAC0 ethernet`。

### PHY ID 是 0 或 0xffff，或出现 MDIO timeout

- 检查 PHY 地址，当前默认 `CONFIG_AIC_DEV_GMAC0_PHYADDR=0`。
- 检查 PE6 reset 是否有效释放。
- 检查 PE8/PE9 是否已切到 GMAC0 MDC/MDIO function 2。
- 检查 RTL8201F 25 MHz 输入时钟和供电。

### 有 PHY ID 但没有 link UP

- 检查网线、交换机端口和 RJ45 变压器连接。
- 检查 RTL8201F 是否输出 50 MHz 到 PE3。
- 当前配置默认 `CONFIG_AIC_DEV_GMAC0_PHY_EXTCLK=y`，表示 MAC 使用 PHY 提供的
  RMII 50 MHz REFCLK。

### link UP 但 DHCP 失败

- 先改用静态 IP 和 PC 直连验证，排除 DHCP 服务器问题。
- 用 PC 抓包确认是否看到 DHCP Discover 或 ARP。
- 如果只看到 TX 没有 RX，优先检查 PE0/PE1/PE2/PE3。
- 如果只看到 RX 没有 TX，优先检查 PE4/PE5/PE7。

### ping 丢包或不稳定

- 确认 PE3 REFCLK 质量，RMII 对 50 MHz 时钟比较敏感。
- 确认 PHY reset 后再访问 MDIO，避免复位释放太早。
- 确认网线和交换机端口固定在 10/100M，RTL8201F 不支持千兆。

## 通过标准

建议按以下顺序确认：

1. 编译通过并生成 `nuttx.bin`。
2. 启动日志显示 GMAC0 注册成功。
3. PHY ID 可读。
4. 插线后出现 `link UP`。
5. `ifconfig eth0 dhcp` 能拿到地址，或静态 IP 配置后能 ping 通 PC。
6. PC 和板端可以双向 ping。

