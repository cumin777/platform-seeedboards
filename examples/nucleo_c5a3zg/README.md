# Nucleo C5A3ZG Validation Examples

> **内部验证用，不提交给最终用户。** 用于在 XIAO STM32C5 实板到位前，使用 Nucleo C5A3ZG 验证 Zephyr 板级定义和 UF2 烧录流程。

## 背景

XIAO STM32C5 与 Nucleo C5A3ZG 使用同一颗 MCU die（STM32C5A3），但封装不同：

| 项目 | XIAO STM32C5 | Nucleo C5A3ZG |
|------|-------------|---------------|
| MCU 型号 | STM32C5A3CG | STM32C5A3ZGT6 |
| 封装 | UFQFPN48 (48 pin) | LQFP144 (144 pin) |
| Flash | 1MB | 1MB |
| SRAM | 256KB | 256KB |
| UF2 分区 | 0x08008000 偏移 | 相同（同一 die） |

由于 flash 布局完全一致，UF2 bootloader 和应用固件在两块板之间二进制兼容。区别仅在外围引脚映射。

## 验证策略

使用 `board = seeed-xiao-stm32c5`（同一 MCU），通过 DTS overlay 覆盖引脚映射到 nucleo 硬件：

| 外设 | XIAO C5 | Nucleo (overlay) |
|------|---------|------------------|
| LED | PB12 (active-low) | PA5 (active-high, LD1) |
| Console | USART1 PA9/PA10 | LPUART1 PA3/PA2 (STLink VCP) |
| 用户按键 | 无 | PC13 (B1) |

## 目录结构

```
nucleo_c5a3zg/
└── zephyr-blink/
    ├── platformio.ini        # board = seeed-xiao-stm32c5
    ├── src/main.c            # 通用 blink 代码 (DT_ALIAS(led0))
    └── zephyr/
        ├── CMakeLists.txt
        ├── prj.conf
        └── app.overlay       # 覆盖 LED/console 到 nucleo 引脚
```

## 使用方法

### 1. 编译

```bash
cd examples/nucleo_c5a3zg/zephyr-blink
pio run -e nucleo_c5a3zg
```

### 2. UF2 烧录（需要先烧录 UF2 bootloader）

前提条件：Nucleo 板已烧录 TinyUF2 bootloader。

```bash
# 生成 UF2 固件
pio run -e nucleo_c5a3zg --target uf2

# 按下 nucleo reset 按钮两次进入 UF2 模式
# 将 .pio/build/nucleo_c5a3zg/firmware.uf2 拷贝到 NUCELOBOOT 卷
```

### 3. 验证

- **LED**: LD1 (绿色) 应每秒闪烁
- **Console**: 连接 STLink USB，波特率 115200，应看到 "LED state: ON/OFF" 输出

## 清理

XIAO C5 实板到位并通过验证后，整个 `nucleo_c5a3zg/` 目录应删除。
