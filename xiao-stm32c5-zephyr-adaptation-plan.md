# XIAO STM32C5 Zephyr Board Adaptation Plan

## 1. Overview

为 XIAO STM32C5 开发板添加 Zephyr 板级定义，参考 ST 官方 `nucleo_c5a3zg` 板级定义，结合原理图解析填充 XIAO connector 引脚映射。

**MCU**: STM32C5A3CG (Cortex-M33, 144MHz, 256KB SRAM, 1MB Flash)
- 封装: UFQFPN48 (48 引脚 + 散热焊盘)，型号后缀 **CG**
- 注意: 与 nucleo_c5a3zg 的 STM32C5A3**ZG**T6 (LQFP144) 是同一 die 的不同封装

**当前状态**:
- ✅ Step 1-3 完成: 板级定义已填充实际引脚映射（原理图解析 + datasheet 验证）
- ✅ 编译验证通过: zephyr-blink [SUCCESS] (Flash 1.6%, RAM 1.7%)
- ✅ nucleo_c5a3zg 编译验证通过 (overlay 适配 nucleo 引脚)
- ⚠️ 硬件勘误: PA15 (D8, SPI_SCK) 无 SPI SCK 复用功能 → 硬件 SPI 暂禁用
- ⏸️ 待硬件: UF2 烧录验证、LED/UART 功能验证（待板到位）

---

## 2. Key Resources

### 2.1 原理图

| 文件 | 内容 |
|------|------|
| `04 XIAO Header&STM32C5.kicad_sch` | XIAO 连接器与 MCU 引脚映射（**最关键**） |
| `03 Power.kicad_sch` | 电源树 |
| `05 Peripherals.kicad_sch` | 外设连接 |
| `02 Block Diagram.kicad_sch` | 系统框图 |
| `01 Descriptions.kicad_sch` | 描述信息 |

路径: `D:\workspace\xiao_skill\ai-skills\pcb\c5\XIAO STM32C5 SCH&PCB_v0.8_260611`

格式: KiCad (`.kicad_sch` / `.kicad_pro`)

### 2.2 Zephyr 官方参考板级定义

路径: `~/.platformio/packages/framework-zephyr/boards/st/nucleo_c5a3zg/`

| 文件 | 说明 |
|------|------|
| `nucleo_c5a3zg.dts` | 设备树（LED/Button/UART/I2C/SPI/ADC/Flash 分区） |
| `nucleo_c5a3zg.yaml` | 板级能力声明 (ram/flash/supported features) |
| `nucleo_c5a3zg_defconfig` | 内核默认配置 |
| `Kconfig.nucleo_c5a3zg` | SOC 选择 (`SOC_STM32C5A3XX`) |
| `board.cmake` | 烧录器配置 (STM32CubeProgrammer) |
| `board.yml` | 板级元数据 |
| `arduino_r3_connector.dtsi` | Arduino R3 连接器映射（参考格式） |

### 2.3 SOC 级设备树（与 nucleo 共享，无需修改）

```
stm32c5.dtsi          → 主 SOC 定义 (Cortex-M33, 所有外设)
  └─ stm32c5a3.dtsi   → Flash/RAM 配置
     └─ stm32c5a3Xg.dtsi → 封装级 (144-pin, SRAM0 256KB, Flash0 1MB)
```

pinctrl: `stm32c5(9-a)3z(e-g)tx-pinctrl.dtsi` — 引脚复用表，XIAO 与 nucleo 完全共享。

### 2.4 Schematic Analyzer Skill

路径: `D:\workspace\xiao_skill\ai-skills\skills\schematic-analyzer`

| 能力 | 说明 |
|------|------|
| KiCad 解析 | 支持 `.kicad_sch` / `.kicad_pro` |
| 组件查询 | `query --component U10` 查询元件引脚与网络 |
| 网络追踪 | `query --net NET_NAME` 追踪信号路径 |
| 页面浏览 | `query --page 4` 查看某页所有组件/网络 |
| 总线模式检测 | `query --pattern i2c.yaml` 识别 I2C/SPI/USB 总线 |
| 依赖 | Python 3.10+, `kicad-cli`（用于 netlist 导出） |

### 2.4.1 kicad-cli 环境状态与版本兼容性（已验证）

| 项目 | 值 |
|------|-----|
| WSL kicad-cli 路径 | `/usr/bin/kicad-cli` |
| WSL kicad-cli 版本 | **9.0.8**（apt 可升级到 9.0.9） |
| 原理图 KiCad 格式 | **10.0**（`generator_version "10.0"`, `version 20260306`） |
| 兼容性 | **不兼容** — kicad-cli 9.0.8 无法解析 KiCad 10.0 格式的 `.kicad_sch` |

**已验证**: `kicad-cli sch export netlist` 对 KiCad 10.0 原理图返回 `Failed to load schematic`。

### 2.4.2 原理图解析策略 — 采用方案 C（已确认）

**决定**: 由于 kicad-cli 升级受阻（方案 A 放弃），方案 B 降版本不可行，**采用方案 C**。

#### schematic-analyzer 在方案 C 下的实际能力（已实测）

| 命令 | 是否可用 | 输出质量 |
|------|---------|---------|
| `overview` | **可用** | 正确识别 6 页、120 组件、核心元件（MCU U4、IMU U11、Flash U7、USB、XIAO 连接器 U10） |
| `query --page N` | **可用** | 正确列出每页组件列表（ref/value/mpn） |
| `query --component REF` | **部分可用** | 可拿到 value/mpn/footprint，**但 nets[] 为空** |
| `query --net NAME` | **不可用** | Nets 为空，net 追踪完全失效 |
| `query --pattern xxx.yaml` | **不可用** | 依赖 netlist，无法识别总线 |

**结论**: schematic-analyzer 只能提供**组件清单**（参考位号、型号、封装），**无法提供引脚连接关系**。连接关系必须通过**直接阅读 `.kicad_sch` 文本**获取。

#### 方案 C 的执行路径

```
Step 1a: 用 schematic-analyzer overview 获取组件清单
        ↓ 提取 MCU(U4)/连接器(U10)/LED/按键/晶振等关键元件清单
Step 1b: 直接阅读 .kicad_sch 文本，提取引脚连接关系
        ↓ 主要分析 04 XIAO Header&STM32C5.kicad_sch
Step 1c: 必要时用截图 img_v3_*.png 视觉确认复杂连接
        ↓ 交叉验证
产出: XIAO Pin → STM32C5 GPIO → Zephyr peripheral 映射表
```

#### .kicad_sch 文本结构关键元素

`.kicad_sch` 是 S-expression 文本格式，可通过 grep 提取以下关键信息：

| 元素 | 语法 | 用途 |
|------|------|------|
| `(symbol ... (property "Reference" "U4") ...)` | 元件实例 | 定位 MCU、连接器等 |
| `(pin ... (name "PA0") ...)` | 引脚定义 | MCU 各引脚编号与名称 |
| `(label "NET_NAME" ...)` | 局部网络标签 | 同页内的网络命名 |
| `(hierarchical_label "NET_NAME" ...)` | 层次网络标签 | 跨页网络连接 |
| `(wire (pts (xy x1 y1) (xy x2 y2)) ...)` | 导线 | 引脚到引脚的物理连接 |
| `(global_label "NET_NAME" ...)` | 全局网络 | 电源/地等全局网络 |

#### 方案 C 的优势与局限

| 优势 | 局限 |
|------|------|
| 无需安装任何额外工具 | 网络连接关系需要人工梳理 |
| schematic-analyzer 能快速定位组件 | 复杂层次设计的跨页追踪较费时 |
| `.kicad_sch` 文本可 grep 自动化 | 需要理解 S-expression 语法 |
| 截图可辅助视觉确认 | 截图分辨率可能限制细节识别 |

#### 备选: 后续若需要升级 kicad-cli（非当前路径，仅记录）

若未来恢复方案 A，相关命令已记录：

```bash
# KiCad 10.0 PPA 已确认可达（Release 索引可拉取）
# 但 add-apt-repository 存在 SSL 问题，需手动写入源文件
# 源文件内容（复用同团队 9.0 PPA 签名密钥）：
#   Types: deb
#   URIs: https://ppa.launchpadcontent.net/kicad/kicad-10.0-releases/ubuntu/
#   Suites: noble
#   Components: main
# 写入后执行: sudo apt update && sudo apt install --only-upgrade kicad
```

### 2.5 项目中已有 STM32C5 骨架

路径: `/mnt/d/workspace/platform-seeedboards/`

| 文件 | 状态 |
|------|------|
| `boards/seeed-xiao-stm32c5.json` | 已有基础配置 (MCU/Flash/RAM/UF2) |
| `zephyr/boards/seeed/xiao_stm32c5/*` | 骨架已搭建，引脚映射为 PLACEHOLDER |
| `builder/board_build/stm32/` | STM32 构建脚本（UF2 + STLink 上传） |
| `platform_cfg/stm32_cfg.py` | 平台配置（工具链/调试工具） |
| `examples/seeed-xiao-stm32c5/zephyr-blink/` | 示例工程，**已验证 UF2 烧录流程可用** |

### 2.6 UF2 Bootloader 约束（重要）

XIAO STM32C5 使用 **TinyUF2 bootloader**，这是板级适配的关键约束，所有后续改动必须保证 UF2 框架正常工作。

**Flash 分区布局（1MB Flash，基址 0x08000000）**:

```
0x0800_0000 ┌──────────────────────────┐
            │  uf2-bootloader (32KB)   │  boot_partition
0x0800_8000 ├──────────────────────────┤  ← 应用起始地址
            │  image-0 / slot0 (488KB) │  slot0_partition (zephyr,code-partition)
0x0808_2000 ├──────────────────────────┤
            │  image-1 / slot1 (480KB) │  slot1_partition (OTA 预留)
0x080F_A000 ├──────────────────────────┤
            │  storage (24KB)          │  storage_partition
0x0810_0000 └──────────────────────────┘  Flash 末尾
```

**已验证的 UF2 配置**（来自 board JSON + DTS，zephyr-blink 已成功编译烧录）:

| 配置项 | 值 | 所在文件 |
|--------|-----|---------|
| 应用偏移地址 | `0x08008000` | `boards/seeed-xiao-stm32c5.json` → `upload.offset_address` |
| UF2 family ID | `0x00C5C5C5` | `boards/seeed-xiao-stm32c5.json` → `upload.uf2.family_id` |
| UF2 卷标 | `XIAOC5BOOT` | `boards/seeed-xiao-stm32c5.json` → `upload.uf2.volume_label` |
| DTS chosen code-partition | `&slot0_partition` | `xiao_stm32c5.dts` → `chosen { zephyr,code-partition }` |
| DTS slot0 起始偏移 | `0x8000` (32KB) | `xiao_stm32c5.dts` → `slot0_partition: partition@8000` |
| ROM_START_OFFSET | `CONFIG_ROM_START_OFFSET=0x8000` | `Kconfig.defconfig` |
| 上传协议 | `uf2`（主）, `stlink`（备） | `boards/seeed-xiao-stm32c5.json` → `upload.protocols` |

**适配时的硬性要求**:

1. **`xiao_stm32c5.dts` 中的 flash 分区定义不得改动** — 分区偏移和大小已与 bootloader 对齐，修改会导致无法启动
2. **`Kconfig.defconfig` 中的 `ROM_START_OFFSET=0x8000` 不得改动** — 告诉 Zephyr 从 32KB 偏移处开始链接
3. **`boards/seeed-xiao-stm32c5.json` 中的 `offset_address` 和 UF2 参数不得改动** — 已验证可正常烧录
4. **新增外设节点时不得影响 `chosen { zephyr,flash = &flash0 }` 和分区定义** — 确保链接地址正确

---

## 3. Adaptation Steps

### Step 0: 双板验证策略（前置条件）

**背景**: XIAO C5 实板尚未到位，当前手头只有 `nucleo_c5a3zg` 开发板。需要用 nucleo 作为验证平台先行调试板级定义，待 XIAO C5 到手后再迁移验证。

**目标目录结构**:

```
examples/
├── seeed-xiao-stm32c5/          # XIAO C5 专属示例（最终交付用户）
│   ├── zephyr-blink/            # 已有，已验证 UF2 烧录
│   ├── zephyr-xxx/              # 后续新增的示例
│   └── ...
├── nucleo_c5a3zg/               # Nucleo 验证用示例（仅内部验证，不提交给用户）
│   ├── zephyr-blink/            # 基于 nucleo 板级定义 + UF2 分区适配
│   ├── zephyr-xxx/              # 与 xiao_stm32c5 对应的验证示例
│   └── ...
├── zephyr-blink/                # 现有 nRF 等示例，不动
├── zephyr-adc/                  # 现有 nRF 等示例，不动
└── ...                          # 其余 nRF 系列示例一律不动
```

**双板验证要求**:

| 要求 | 说明 |
|------|------|
| nucleo 示例独立目录 | `examples/nucleo_c5a3zg/` 与 `examples/seeed-xiao-stm32c5/` 分离 |
| UF2 分区对齐 | nucleo 示例也需要适配 UF2 bootloader 的 flash 分区（与 zephyr-blink 同模式） |
| 板级定义复用 | nucleo 使用 Zephyr 自带的 `nucleo_c5a3zg` 板级定义，无需创建新 board JSON |
| 不影响现有示例 | 所有 `zephyr-*` 开头的 nRF 系列示例不做任何改动 |
| 最终清理 | 验证完成后 `nucleo_c5a3zg/` 目录不提交，仅保留 `seeed-xiao-stm32c5/` |

**nucleo_c5a3zg 示例的 UF2 适配要点**:

nucleo_c5a3zg 官方板级定义使用 STM32CubeProgrammer 烧录，flash 分区为 mcuboot 方案。要使用 UF2 bootloader 验证，需要通过 DTS overlay 或 defconfig 覆盖分区定义：

```ini
; platformio.ini 示例 (nucleo_c5a3zg zephyr-blink)
[env:nucleo_c5a3zg]
platform = https://github.com/cumin777/platform-seeedboards.git#add_xiao_c5_support
framework = zephyr
board = seeed-xiao-stm32c5  ; 复用 xiao_stm32c5 的板级定义（同一 MCU）
; 或者使用原生 nucleo 定义 + overlay 覆盖分区
```

> **注意**: 两种板子使用同一 MCU (STM32C5A3)，区别仅在外围引脚。如果 UF2 bootloader 已烧录到 nucleo 板上，可以直接复用 `seeed-xiao-stm32c5` 的 board 配置进行验证（flash/RAM 完全一致）。

### Step 1: 解析原理图，提取引脚映射（方案 C）

**目标**: 从原理图中提取 XIAO 14-pin 连接器到 MCU 的完整映射。

**工具组合**:
- **schematic-analyzer**: 提供组件清单（overview / query --page / query --component）
- **直接阅读 `.kicad_sch`**: 提取引脚连接关系（label / wire / hierarchical_label）
- **截图 `img_v3_*.png`**: 视觉辅助确认

**执行顺序**:

1. `schematic-cli.py overview` → 获取 6 页 120 组件清单，定位关键元件
2. `schematic-cli.py query --page 6` → 列出第 4 页所有元件（含 MCU U4、连接器 U10）
3. **直接阅读 `04 XIAO Header&STM32C5.kicad_sch`** → 用 grep 提取 label/wire，梳理连接关系
4. 必要时查 `03 Power.kicad_sch` / `05 Peripherals.kicad_sch` 的 hierarchical_label 跨页连接
5. 截图交叉验证关键引脚

**需要提取的信息**:

| 信息项 | 来源 |
|--------|------|
| XIAO 连接器引脚 D0-D15 | `.kicad_sch` 中 U10 的 pin + 连接的 label/wire |
| MCU 各引脚网络 | `.kicad_sch` 中 U4 的 pin + 连接的 label/wire |
| UART TX/RX 网络 | grep `(label "UART*"` 或类似 |
| I2C SCL/SDA 网络 | grep `(label "*SCL*"` / `(label "*SDA*"` |
| SPI 网络 | grep `(label "*MOSI*"` / `"*MISO*"` / `"*SCK*"` / `"*CS*"` |
| LED / 按键 | schematic-analyzer 组件清单 + 网络 |
| USB DP/DM | grep `(label "*D+*"` / `"*D-*"` |
| 晶振 HSE/LSE | 组件清单（overview 已识别晶振） |
| SWD 调试 | grep `(label "*SWDIO*"` / `"*SWCLK*"` |

**产出物**: 一份 XIAO Pin → STM32C5 GPIO → Zephyr peripheral 的完整映射表。

### Step 2: 查阅 STM32C5 Datasheet，确认引脚复用

**目标**: 确认每个引脚支持的 alternate function，选择正确的外设实例。

schematic-analyzer 能告诉你 "D0 连接到 PA0"，但不能告诉你 PA0 可复用为 USART2_TX 还是 TIM2_CH1。

**需要的资料**:
- STM32C5A3 Datasheet — Pinout & Alternate Function Mapping 表
- Zephyr pinctrl 文件 `stm32c5(9-a)3z(e-g)tx-pinctrl.dtsi` — 确认 Zephyr 中定义的 pinctrl 名称

**产出物**: 每个外设对应的 pinctrl 配置确认。

### Step 3: 对照 nucleo_c5a3zg，填充板级定义

#### 3.1 `seeed_xiao_connector.dtsi` — XIAO 14-pin 映射

替换全部 PLACEHOLDER，填入实际 GPIO 引脚号。

**参考 nucleo 的 `arduino_r3_connector.dtsi` 格式**:

```dts
/ {
    xiao_d: connector {
        compatible = "seeed,xiao-gpio";
        #gpio-cells = <2>;
        gpio-map-mask = <0xffffffff 0xffffffc0>;
        gpio-map-pass-thru = <0 0x3f>;
        gpio-map = <0 0 &gpioa X 0>,    /* D0 - 实际引脚 */
                   ...
                   <13 0 &gpiob Y 0>;   /* D13 - 实际引脚 */
    };
};

xiao_serial: &usartX {};   /* 或 &lpuart1 */
xiao_i2c:   &i2cX {};
xiao_spi:   &spiX {};
```

#### 3.2 `xiao_stm32c5.dts` — 主设备树

补充外设节点:

```dts
/* 参考 nucleo_c5a3zg.dts 的写法 */
&i2cX {
    pinctrl-0 = <&i2cX_scl_... &i2cX_sda_...>;
    status = "okay";
};

&spiX {
    pinctrl-0 = <&spiX_mosi_... &spiX_miso_... &spiX_sck_...>;
    status = "okay";
};

&usartX {
    pinctrl-0 = <&usartX_tx_... &usartX_rx_...>;
    status = "okay";
};

/* LED/Button */
leds { ... };
buttons { ... };

/* !! Flash 分区 / chosen code-partition / ROM_START_OFFSET 不得改动 !!
 * 已与 TinyUF2 bootloader 对齐（应用起始于 0x08008000），见 2.6 节
 */
/* 时钟配置 - 根据原理图晶振频率 */
/* ADC - 根据引脚映射 */
```

#### 3.3 `xiao_stm32c5.yaml` — 能力声明

参考 nucleo 补全 supported features:

```yaml
supported:
  - adc
  - gpio
  - i2c
  - pwm
  - spi
  - uart
  - watchdog
  # 根据实际外设添加
```

#### 3.4 其他文件 — 无需大改

| 文件 | 预计改动 |
|------|---------|
| `board.yml` | 无需改动 |
| `Kconfig.xiao_stm32c5` | 无需改动（已选 `SOC_STM32C5A3XX`） |
| `Kconfig.defconfig` | **不得改动**（`ROM_START_OFFSET=0x8000` 已与 UF2 对齐） |
| `board.cmake` | 已有 STLink + UF2 配置，可能微调 |
| `xiao_stm32c5_defconfig` | 可能需要增加外设默认配置 |
| `boards/seeed-xiao-stm32c5.json` | 确认参数，可能微调 |

### Step 4: 验证（双板）

#### 4.1 nucleo_c5a3zg 验证（先行验证，当前阶段）

1. 烧录 UF2 bootloader 到 nucleo 板（若尚未烧录）
2. nucleo zephyr-blink 编译 + UF2 烧录通过
3. 串口输出验证
4. LED / 按键功能验证（注意 nucleo 的 LED/按键引脚与 XIAO 不同，验证 PIO 配置即可）

#### 4.2 xiao_stm32c5 验证（待板子到位）

1. Zephyr blink 示例编译通过
2. UF2 烧录测试
3. 串口输出验证
4. LED / 按键功能验证
5. I2C / SPI / ADC 外设功能验证
6. XIAO 连接器 14-pin 引脚映射验证

#### 4.3 清理

验证完成后，移除 `examples/nucleo_c5a3zg/` 目录，仅保留 `examples/seeed-xiao-stm32c5/` 用于交付。

---

## 4. Feasibility Analysis

### 可行性评估

| 维度 | 评估 | 说明 |
|------|------|------|
| SOC 支持 | **完全可行** | Zephyr 已有完整 STM32C5 SOC 支持，dtsi/pinctrl 均可共享 |
| 板级定义模式 | **成熟可靠** | 项目中已有多个 XIAO 板级定义可参考，骨架已搭好 |
| 原理图解析 | **方案 C** | schematic-analyzer 提供组件清单，`.kicad_sch` 文本直接提取网络连接，截图辅助确认 |
| 引脚复用确认 | **需额外资料** | schematic-analyzer 无法提供，需查 datasheet |
| 构建系统 | **已就绪** | STM32 builder 和 platform_cfg 已实现 |

### 风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| `.kicad_sch` 网络解析工作量大 | 方案 C 下需人工梳理 S-expression 网络连接，复杂层次设计较费时 | 优先聚焦 `04 XIAO Header&STM32C5.kicad_sch` 单页（包含所有 connector 映射）；用 schematic-analyzer 组件清单作为索引；截图交叉验证 |
| 引脚复用选错 | 外设无法正常工作 | 严格对照 datasheet 和 pinctrl dtsi 确认 |
| Flash 分区偏移 | 烧录后无法启动 | 分区已验证（zephyr-blink 通过 UF2 烧录成功），**不得改动** |
| UF2 family_id | UF2 烧录不识别 | `0x00C5C5C5` 为自定义 ID，需确认 bootloader 是否匹配 |

### 建议改进

1. **增加 datasheet 对照环节**: 在解析原理图后、填充板级定义前，查 STM32C5A3 datasheet 确认引脚复用
2. **方案 C 执行顺序**: schematic-analyzer overview → 直接阅读 `04 XIAO Header&STM32C5.kicad_sch` 文本 → 截图交叉验证
3. **优先解析第4页**: `04 XIAO Header&STM32C5.kicad_sch` 包含所有 connector 映射，优先级最高
4. **分阶段填充**: connector 映射 → 基础外设 (UART/I2C/SPI) → 高级外设 (USB/ADC/PWM)，逐步验证

---

## 5. File Structure Summary

```
最终需更新的文件:
├── boards/seeed-xiao-stm32c5.json                          # PIO 板级配置
├── zephyr/boards/seeed/xiao_stm32c5/
│   ├── seeed_xiao_connector.dtsi                           # XIAO 14-pin 映射 [核心]
│   ├── xiao_stm32c5.dts                                    # 主设备树 [核心]
│   ├── xiao_stm32c5.yaml                                   # 能力声明
│   ├── xiao_stm32c5_defconfig                              # 内核配置
│   ├── Kconfig.defconfig                                   # 可能微调
│   ├── Kconfig.xiao_stm32c5                                # 无需改动
│   ├── board.cmake                                         # 可能微调
│   ├── board.yml                                           # 无需改动
│   └── pre_dt_board.cmake                                  # 无需改动
├── builder/board_build/stm32/                              # 可能微调
├── platform_cfg/stm32_cfg.py                               # 可能微调
└── examples/
    ├── seeed-xiao-stm32c5/                                 # XIAO C5 示例（最终交付）
    │   ├── zephyr-blink/                                   # 已有，已验证
    │   └── zephyr-xxx/                                     # 后续新增示例
    └── nucleo_c5a3zg/                                      # Nucleo 验证示例（仅内部，不提交）
        ├── zephyr-blink/                                   # UF2 分区对齐适配
        └── zephyr-xxx/                                     # 与 XIAO 对应的验证示例

不动文件:
├── examples/zephyr-*                                       # 所有 nRF 系列示例
├── examples/arduino-*                                      # 所有 Arduino 示例
└── 其他与 STM32C5 无关的板级定义

参考文件:
├── ~/.platformio/packages/framework-zephyr/boards/st/nucleo_c5a3zg/  # 官方板级定义
└── 原理图: 04 XIAO Header&STM32C5.kicad_sch                          # 引脚映射来源
```

---

## 6. Project Management Rules

### 6.1 开发规范

- **全程使用 SuperPower MCP 作为规范指导**: 所有代码编写、文件结构、提交信息等均遵循 SuperPower MCP 的规范要求

### 6.2 Git 提交策略

- **仓库**: `https://github.com/cumin777/platform-seeedboards`
- **分支**: `add_xiao_c5_support`
- **提交节奏**: 每完成一个验证通过的子任务，立即提交。不积攒大量未提交的改动
- **提交粒度**: 按功能模块拆分提交，每个提交聚焦一个明确的改动点

**预期提交序列**:

| 提交点 | 内容 | 触发条件 | 状态 |
|--------|------|---------|------|
| 1 | 原理图引脚映射表完成 | Step 1 完成，映射表文档产出 | ✅ (folded into commit dc04403) |
| 2-4 | `seeed_xiao_connector.dtsi` + `xiao_stm32c5.dts` + `xiao_stm32c5.yaml` | Step 3.1-3.3 完成，编译通过 | ✅ commit dc04403 |
| 5 | nucleo_c5a3zg 验证示例 | zephyr-blink 编译 + UF2 烧录通过 | ⏸️ 编译通过，待 UF2 烧录验证 (无硬件) |
| 6 | xiao_stm32c5 示例补全 | 待板到位验证通过 | ⏸️ 待硬件 |
| 7 | 最终清理（移除 nucleo 示例） | 所有验证完成 | ⏸️ 待 Step 6 |

### 6.3 持续开发原则

- **不停顿**: 从开始到本阶段所有开发目标完成，持续开发不中断
- **仅在有歧义时询问**: 只有在遇到需求不明确、方案有多种合理选择、或发现与用户预期不符时，才中断询问用户
- **自主推进**: 技术实施细节（引脚复用选择、pinctrl 配置、分区对齐等）按文档中的方案和参考规范自主决策执行

---

## 7. Hardware Erratum: PA15 SPI SCK

### 7.1 问题描述

原理图将 XIAO connector D8/D9/D10 标注为 SPI_SCK/SPI_MISO/SPI_MOSI，对应 PA15/PB0/PB15。但 PA15 在 STM32C5A3 上**没有任何 SPI SCK alternate function**。

### 7.2 证据

| 来源 | 位置 | 关键发现 |
|------|------|---------|
| DS15137-Rev1 Table 14 | 第 59 页 | PA15 的 14 个 AF 中无 SPI SCK（仅有 SPI1_NSS / SPI3_NSS） |
| Zephyr pinctrl dtsi | `stm32c5(9-a)3z(e-g)tx-pinctrl.dtsi` | 无 `spi*_sck_pa15` 条目 |

### 7.3 详细分析文档

详见: `D:\workspace\aaamemory\xiao\c5\environment\zephyr\adapt\board\spi-pin-issue.md`

该文档包含完整的引脚复用证据、SPI SCK 候选引脚分析、三种修改策略（PB1/PB3/新增测试点），以及原理图修订后恢复 SPI 的步骤。

### 7.4 当前处理

- 硬件 SPI 禁用；D8/D9/D10 保留为普通 GPIO
- `xiao_stm32c5.yaml` 不声明 `spi` 能力
- 待原理图修订后恢复（详见 spi-pin-issue.md 第 5.2 节）

---

## 8. Step Completion Log

### Step 1: 解析原理图 ✅

- 方法: 方案 C（schematic-analyzer overview + 直接阅读 .kicad_sch 文本）
- 产出: 完整 XIAO 14-pin → STM32C5 GPIO 映射表

| XIAO Pin | STM32C5 GPIO | Function |
|----------|-------------|----------|
| D0 | PA0 | GPIO / ADC1_IN0 |
| D1 | PA1 | GPIO / ADC1_IN1 |
| D2 | PA2 | GPIO / ADC1_IN2 |
| D3 | PA3 | GPIO / ADC1_IN3 |
| D4 | PB7 | I2C1_SDA (AF4) |
| D5 | PB6 | I2C1_SCL (AF4) |
| D6 | PA9 | USART1_TX (AF7) |
| D7 | PA10 | USART1_RX (AF7) |
| D8 | PA15 | GPIO only (erratum: no SPI SCK AF) |
| D9 | PB0 | GPIO (SPI3_MISO exists but SCK missing) |
| D10 | PB15 | GPIO only |
| LED | PB12 | User LED (active-low) |

### Step 2: 查阅 datasheet / pinctrl ✅

- 所有引脚复用已对照 DS15137-Rev1 Table 14 和 Zephyr pinctrl dtsi 验证
- 发现 PA15 SPI SCK 缺失问题
- 确认 SPI3 是唯一与 PB0 (MISO) + PB15 (MOSI) 兼容的 SPI 实例
- USART1 (PA9/PA10) 确认为 console

### Step 3: 填充板级定义 ✅

- 3.1: `seeed_xiao_connector.dtsi` — D0-D10 实际映射，xiao_serial → usart1, xiao_i2c → i2c1
- 3.2: `xiao_stm32c5.dts` — console USART1, LED PB12 active-low, I2C1, 移除 user_button, UF2 分区不变
- 3.3: `xiao_stm32c5.yaml` — supported: gpio/i2c/uart/watchdog/adc
- 3.4: `xiao_stm32c5_defconfig` — 无需改动

### Step 4: 编译验证 ✅ (compile only)

- xiao_stm32c5 zephyr-blink: `[SUCCESS] Took 51.61s`, Flash 1.6%, RAM 1.7%
- nucleo_c5a3zg zephyr-blink (overlay): `[SUCCESS] Took 23.59s`, overlay 正确覆盖 LED→PA5, console→LPUART1

### Step 4 硬件验证 ⏸️ (blocked)

待 XIAO C5 实板到位或 nucleo + UF2 bootloader 后执行:
- UF2 烧录测试
- LED 闪烁验证
- 串口 console 输出验证
- I2C 外设功能验证
