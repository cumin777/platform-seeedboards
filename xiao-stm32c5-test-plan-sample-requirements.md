# XIAO STM32C5 Test Plan Sample Requirements

## 1. Document Purpose

本文档用于梳理 XIAO STM32C5 `test plan` 样例开发需求，作为后续实际编码 agent 的唯一高优先级输入之一。

目标不是一次性做一个“大而全”的测试固件，而是分两层推进：

1. 先完成多个**单功能 sample**
2. 再汇总一个**多功能 AT 控制 sample**
3. 用最终 sample 覆盖测试表中的主要测试项，并保留量产测试可扩展性

本文档强调：

- 样例边界要清晰
- 每个 sample 只做一个主功能
- 公共能力要抽成可复用模块
- 最终聚合 sample 只负责“命令调度 + 状态切换 + 结果输出”

---

## 2. Current Development Baseline

结合当前仓库现状，已经具备以下基础：

- 已存在板级定义：`zephyr/boards/seeed/xiao_stm32c5/`
- 已存在 STM32C5 专属样例目录：`examples/seeed-xiao-stm32c5/`
- 已有可参考 sample：
  - `zephyr-blink`
  - `zephyr-gpio`
  - `zephyr-i2c`
  - `zephyr-uart-echo`
  - `zephyr-adc`
  - `zephyr-rtc`
- 当前板级 DTS 已确认可用外设基础：
  - `USART1`: `PA9/PA10`
  - `I2C1`: `PB6/PB7`
  - `ADC1`: `PA0/PA1/PA2/PA3`
  - `FDCAN1`: `PB8/PB9`
  - `USB FS`: `PA11/PA12`
  - `User LED`: `PB12`
- 当前已知硬件限制：
  - SPI 暂不纳入本轮 test plan sample 范围
  - 原因是 `PA15` 无法作为 SPI SCK 复用

这意味着本轮文档应优先围绕已经明确映射且适合快速落地验证的外设展开。

---

## 3. Product Goal

本轮 sample 体系的产品目标如下：

1. 支撑研发自测
2. 支撑硬件工程测试
3. 支撑工厂/FAE 快速验证
4. 保证每个测试项既能单独验证，也能被最终聚合样例统一调度

最终交付物应包含两类内容：

1. **单功能 sample 集合**
2. **一个 AT 控制的综合 sample**

---

## 4. Target Structure

建议目录结构如下：

```text
examples/seeed-xiao-stm32c5/
├── zephyr-blink/
├── zephyr-gpio-matrix/
├── zephyr-i2c-dps318/
├── zephyr-uart-max-rate/
├── zephyr-usb-cdc-acm/
├── zephyr-adc-multi-channel/
├── zephyr-pwm-output/
├── zephyr-battery-voltage/
├── zephyr-flash-rw/
├── zephyr-imu-basic/
├── zephyr-imu-heat-pwm/
├── zephyr-can-fd-read/
└── zephyr-testhub-at/
```

说明：

- 目录名尽量体现“功能 + 测试目的”
- 单功能 sample 优先使用独立目录
- 最终聚合 sample 命名建议固定为 `zephyr-testhub-at`

---

## 5. Architecture Strategy

### 5.1 两阶段实现策略

阶段 1：单功能 sample

- 每个 sample 只验证一个主能力
- 允许少量串口日志，但不引入复杂命令框架
- 要求代码直接、稳定、易对照测试步骤

阶段 2：AT 聚合 sample

- 复用阶段 1 的底层驱动封装
- 加入统一 AT 指令入口
- 通过串口命令切换当前测试模式
- 输出统一格式结果，方便人工和脚本读取

### 5.2 不建议的做法

- 不建议一开始就直接开发“大一统”测试固件
- 不建议每个 sample 各自重复实现日志、设备探测、错误码
- 不建议把功能逻辑直接写死在 `main.c`，导致后续难以聚合

### 5.3 推荐的软件分层

建议最终抽象为三层：

1. Board-facing HAL wrapper
2. Feature service
3. Sample entry / AT dispatcher

建议公共代码后续沉淀到类似结构：

```text
examples/seeed-xiao-stm32c5/common/
├── include/
│   ├── xiao_test_log.h
│   ├── xiao_test_status.h
│   ├── xiao_gpio_service.h
│   ├── xiao_i2c_service.h
│   ├── xiao_uart_service.h
│   ├── xiao_adc_service.h
│   ├── xiao_pwm_service.h
│   ├── xiao_battery_service.h
│   ├── xiao_flash_service.h
│   ├── xiao_imu_service.h
│   ├── xiao_can_service.h
│   └── xiao_at_parser.h
└── src/
```

本阶段先写文档，不强制马上创建该目录，但后续 agent 开发时应按此思想组织代码。

---

## 6. Test Item Mapping

以下将测试表映射为建议 sample 需求。

### 6.1 TP1/TP11 SWD 功能测试

测试目标：

- 验证板子可通过 `JLink/STLink/DAPLink` 使用 SWD 正常烧录

代码需求：

- **不需要专门 sample**
- 任意可稳定启动的 sample 都可作为 SWD 烧录验证载体
- 推荐使用 `zephyr-blink` 作为 SWD 验证基准固件

开发要求：

- `zephyr-blink` 必须保持最小依赖、编译快、启动快
- 上电后 1 秒内必须可见 LED 闪烁或串口打印，作为烧录成功标志

### 6.2 GPIO 全引脚映射功能测试

建议 sample：`zephyr-gpio-matrix`

测试目标：

- 验证各独立 IO 均可单独配置为输出并翻转
- 验证软件中的 XIAO 引脚抽象与实际板级映射一致

覆盖范围：

- 至少覆盖 XIAO Header 暴露的可测 GPIO
- 对不适合强推挽输出的管脚，需要在文档中标明限制

代码需求：

- 定义一个统一的 GPIO 测试表
- 每个引脚可按固定节拍轮流翻转
- 串口打印当前正在测试的逻辑引脚名、MCU 引脚名、翻转状态
- 支持单引脚测试模式和全引脚轮询模式

建议串口输出：

```text
[GPIO] pin=D0 mcu=PA0 state=HIGH
[GPIO] pin=D0 mcu=PA0 state=LOW
```

后续聚合兼容要求：

- 抽象出 `gpio_test_run_single(pin_id)` 和 `gpio_test_run_all()`

### 6.3 I2C 功能测试

建议 sample：`zephyr-i2c-dps318`

测试目标：

- 使用 `PB6/PB7`
- 连接 DPS318 或兼容 I2C 设备
- 完成设备探测、寄存器读写、传感器数据打印
- 验证可配置不同 I2C 速率并尝试最高稳定通信速率

代码需求：

- 启动时检测 I2C bus 是否 ready
- 支持扫地址或直接探测 DPS318 地址
- 成功后周期性读取温度/压力原始值或工程值
- 支持在编译期或运行期切换 I2C 速率
- 串口打印当前速率和读取结果

最低输出要求：

```text
[I2C] bus=I2C1 ready=1 speed=400000
[I2C] dps318 detected addr=0x77
[I2C] temp=xx.x pressure=xxxx.x
```

后续聚合兼容要求：

- 抽象出 `i2c_probe()`, `i2c_set_speed()`, `dps318_read_sample()`

### 6.4 UART 功能测试

建议 sample：`zephyr-uart-max-rate`

测试目标：

- 验证邮票孔 UART：`PA9/PA10`
- 评估最高稳定通信速率

代码需求：

- 提供 UART echo 或 loopback 模式
- 支持一组标准波特率切换：
  - `115200`
  - `230400`
  - `460800`
  - `921600`
  - 更高波特率按实际硬件能力扩展
- 串口打印当前波特率、收发计数、错误计数

注意：

- 这里要区分“调试控制串口”和“被测 UART 通道”
- 如果最终 AT 控制 sample 使用 USB CDC 作为主控制口，那么 `PA9/PA10` 更适合作为被测口

后续聚合兼容要求：

- 抽象出 `uart_test_set_baud()`, `uart_test_start_echo()`, `uart_test_get_stats()`

### 6.5 USB 串口功能测试

建议 sample：`zephyr-usb-cdc-acm`

测试目标：

- 验证 `PA11/PA12` 对应的 USB FS CDC ACM 串口
- 电脑侧枚举正常、收发正常

代码需求：

- 上电后自动枚举 CDC ACM
- 支持接收一行字符串并回显
- 支持打印设备 ready 状态
- 支持后续作为 AT 控制 sample 的主控制接口

关键要求：

- 最终综合 sample 默认优先用 USB CDC ACM 作为 AT 命令入口
- 这样可以避免和邮票孔 UART 被测功能互相干扰

### 6.6 ADC 功能测试

建议 sample：`zephyr-adc-multi-channel`

测试目标：

- 以 `PA0` 对应 A0 为主，同时覆盖 `A0/A1/A2/A3`
- 配合可调电源输入多个电压点，输出 ADC 采样结果

代码需求：

- 支持 4 路通道独立采样
- 打印原始 ADC 值和换算电压值
- 支持单次采样和周期采样模式
- 保留校准和参考电压配置点

建议输出：

```text
[ADC] ch=A0 raw=1234 mv=2103
[ADC] ch=A1 raw=1930 mv=3298
```

后续聚合兼容要求：

- 抽象出 `adc_sample_once(channel)` 和 `adc_sample_all()`

### 6.7 PWM 功能测试

建议 sample：`zephyr-pwm-output`

测试目标：

- 使用 `PA3`
- 输出固定频率、50% 占空比 PWM
- 供示波器测量验证

代码需求：

- 默认输出 1 kHz, 50% duty
- 支持后续通过 AT 修改频率和占空比
- 启动后打印当前配置

建议输出：

```text
[PWM] pin=PA3 freq=1000 duty=50.0
```

后续聚合兼容要求：

- 抽象出 `pwm_start(freq, duty)` 和 `pwm_stop()`

### 6.8 User LED 功能测试

建议 sample：复用 `zephyr-blink`

测试目标：

- 验证 `D8` 对应的用户灯可正常闪烁

说明：

- 当前板级 DTS 中用户灯映射为 `PB12`
- 测试表中的 `D8` 描述应在后续和硬件定义再次核对
- 在软件文档中应同时写出“测试项标称位号”和“当前 DTS 实际控制 GPIO”

代码需求：

- LED 以固定周期闪烁
- 串口打印闪烁计数

### 6.9 电池电压读取测试

建议 sample：`zephyr-battery-voltage`

测试目标：

- 接入电池后，读取电池电压并打印
- 与万用表读数做对比

前提：

- 必须先确认电池电压采样链路的原理图映射
- 包括：
  - 分压网络
  - ADC 通道
  - 使能脚是否存在

代码需求：

- 初始化电池检测 ADC 通道
- 如有电源检测使能脚，先拉起使能
- 打印原始 ADC 值、分压换算后的电池电压
- 保留校准系数宏定义

风险：

- 该项依赖原理图进一步确认，开发优先级略低于通用 ADC

### 6.10 Flash 读写测试

建议 sample：`zephyr-flash-rw`

测试目标：

- 读写板载外部 Flash `U7`

前提：

- 必须先确认 `U7` 是：
  - 外部 QSPI/NOR Flash
  - 还是内部 Flash 逻辑分区测试

从测试描述看，更像是板载外部存储器测试。

代码需求：

- 初始化目标 flash device
- 擦除指定测试区
- 写入固定 pattern
- 回读并校验
- 打印每一步状态和校验结果

建议输出：

```text
[FLASH] erase ok
[FLASH] write ok len=256
[FLASH] verify ok
```

风险：

- 该项强依赖原理图和具体器件型号确认
- 如果外部 flash 当前 Zephyr board DTS 尚未补齐，需要单列前置任务

### 6.11 IMU 功能测试

建议 sample：`zephyr-imu-basic`

测试目标：

- 读取 U11 六轴数据
- 验证中断可正常获取

代码需求：

- 初始化 IMU 设备
- 周期读取 accel/gyro 数据
- 配置 data-ready 或 motion interrupt
- 打印数据和中断计数

建议输出：

```text
[IMU] ax=... ay=... az=...
[IMU] gx=... gy=... gz=...
[IMU] irq_count=12
```

风险：

- 依赖 IMU 型号、总线连接方式、INT 引脚映射确认

### 6.12 IMU-PID 环 PWM 控制温补加热功能测试

建议 sample：`zephyr-imu-heat-pwm`

测试目标：

- 读取 IMU 数据
- 根据温度或姿态相关量进行 PID 运算
- 输出 PWM 驱动 `Q1`
- 通过 UART 或 CAN 打印数据

说明：

- 这是本轮最复杂 sample
- 其本质是“功能联动 sample”，不是基础外设 bring-up sample

拆分建议：

先完成以下前置能力后再开发：

1. `zephyr-imu-basic`
2. `zephyr-pwm-output`
3. 一个轻量 PID 模块

代码需求：

- 读取 IMU 温度或相关测量值
- PID 输出限制在安全范围
- PWM 输出实时可调
- 周期打印输入值、目标值、PID 输出值

建议：

- 第一版先做开环/假闭环框架
- 第二版再做真实 PID 参数整定

### 6.13 CAN 读取功能测试

建议 sample：`zephyr-can-fd-read`

测试目标：

- FDCAN 高速模式读取
- 适配两种硬件位配置场景：
  - `U3 (0Ω)`
  - `U3 (33Ω)`
- USB 转 CAN 工具侧可看到读取数据或本机能打印收到的数据

代码需求：

- 初始化 `FDCAN1`
- 支持 classic CAN 和 CAN FD 参数配置
- 打印当前 nominal/data bitrate
- 接收帧后通过 USB CDC 日志打印
- 统计接收帧数、错误帧数、总线状态

建议输出：

```text
[CAN] mode=fd nominal=500000 data=2000000
[CAN] id=0x123 dlc=16 data=...
```

关键要求：

- 最终综合 sample 中，CAN 测试日志默认走 USB CDC，而不是 CAN 自己对应 UART

---

## 7. Final Integrated Sample Requirements

建议 sample：`zephyr-testhub-at`

### 7.1 Role Definition

该 sample 不是简单把所有逻辑拷贝到一个 `main.c`。

它的角色应定义为：

- 一个测试调度器
- 一个 AT 命令入口
- 一个结果统一输出器
- 一个状态切换器

### 7.2 Main Control Interface

推荐主控制接口：

- **首选：USB CDC ACM**
- **备选：USART1**

原因：

- USB CDC 更适合作为上位机测试口
- `PA9/PA10` 邮票孔 UART 可保留给 UART 被测项本身

### 7.3 Mode Switching Model

综合 sample 采用“单次只激活一种主测试模式”的策略。

不建议默认多模块同时运行，原因：

- 更难定位问题
- 日志互相干扰
- 资源冲突概率高

建议模式：

- `IDLE`
- `LED`
- `GPIO`
- `I2C`
- `UART`
- `USB`
- `ADC`
- `PWM`
- `BAT`
- `FLASH`
- `IMU`
- `HEAT`
- `CAN`

### 7.4 AT Command Scope

建议第一版支持以下 AT 命令子集：

```text
AT
AT+HELP
AT+MODE?
AT+MODE=<name>
AT+STOP
AT+GPIO=ALL
AT+GPIO=<pin>
AT+I2C=SCAN
AT+I2C=RATE,<hz>
AT+UART=BAUD,<rate>
AT+ADC=ALL
AT+ADC=<channel>
AT+PWM=<freq>,<duty>
AT+BAT?
AT+FLASH=TEST
AT+IMU=READ
AT+IMU=STREAM
AT+CAN=START,<nominal>,<data>
AT+CAN=STOP
```

建议响应格式：

```text
OK
ERROR:<code>
MODE:ADC
ADC:A0,2103mV
```

### 7.5 Integrated Sample Internal Rules

- 任一时刻仅允许一个持续运行类测试处于 active
- 切换模式时必须执行上一个模块的 `deinit/stop`
- 所有输出统一加模块前缀
- 所有错误码统一定义
- 所有长时间运行测试都应支持 `AT+STOP`

---

## 8. Common Module Requirements

为避免后续重复开发，建议另一个 agent 在开发时同步规划公共模块。

### 8.1 Logging

统一日志格式：

```text
[MODULE] message
```

要求：

- 支持 info/error
- AT 响应与普通日志区分开

### 8.2 Error Code

统一错误码示例：

- `ERR_NOT_READY`
- `ERR_INVALID_PARAM`
- `ERR_TIMEOUT`
- `ERR_IO`
- `ERR_UNSUPPORTED`

### 8.3 Sample Lifecycle Interface

建议每个功能模块遵循统一接口风格：

- `init`
- `start`
- `poll`
- `stop`
- `deinit`

这样后续聚合 sample 切模式时最稳定。

---

## 9. Recommended Development Order

建议按如下顺序推进，减少阻塞：

1. `zephyr-blink`
2. `zephyr-usb-cdc-acm`
3. `zephyr-gpio-matrix`
4. `zephyr-adc-multi-channel`
5. `zephyr-pwm-output`
6. `zephyr-uart-max-rate`
7. `zephyr-i2c-dps318`
8. `zephyr-can-fd-read`
9. `zephyr-battery-voltage`
10. `zephyr-imu-basic`
11. `zephyr-flash-rw`
12. `zephyr-imu-heat-pwm`
13. `zephyr-testhub-at`

排序原则：

- 先做基础 bring-up
- 再做总线和传感器
- 最后做联动和聚合

---

## 10. Dependencies and Open Questions

以下项目在正式编码前应进一步确认，否则容易返工。

### 10.1 Must Confirm from Schematic

- 电池电压采样通道、分压比、使能脚
- 外部 Flash `U7` 的器件型号和总线类型
- IMU `U11` 的器件型号、总线类型、INT 引脚
- 加热控制 `Q1` 的 PWM 驱动引脚和有效电平
- CAN 收发器 `U3` 的精确连接、0Ω/33Ω 场景差异
- User LED 对应丝印位号和测试表中的 `D8` 是否一致

### 10.2 Needs Product-Level Decision

- 最终 AT 主控口是否固定使用 USB CDC
- UART 被测口是否完全独立于控制口
- I2C、UART、CAN 的“最高速率”是否要做自动扫速，还是仅提供人工配置点
- 最终综合 sample 是否要求测试结果可被脚本自动解析

---

## 11. Prompt Guidance for the Next Codex Agent

后续给实际编码 agent 的提示词，建议强约束以下几点：

1. 先做单功能 sample，不要先做综合 sample
2. 每完成一个 sample，都保证可独立编译
3. 公共逻辑优先抽象，避免在每个 `main.c` 复制粘贴
4. USB CDC 作为综合 sample 的首选控制口
5. UART 邮票孔优先保留给 UART 测试项
6. 所有持续运行类测试都必须可 stop
7. 所有 sample 日志格式统一
8. 对原理图未确认的功能，先补充需求澄清或做硬件映射确认，不要盲写代码

可直接使用的 prompt 草案如下：

```text
你现在负责为 XIAO STM32C5 的 Zephyr 样例实现 test plan 体系。请严格按以下原则开发：

1. 先实现单功能 sample，再实现最终 AT 聚合 sample。
2. 所有 sample 放在 examples/seeed-xiao-stm32c5/ 下。
3. 每个 sample 只聚焦一个主功能，要求可独立编译、可独立运行、日志明确。
4. 把 GPIO/I2C/UART/ADC/PWM/BAT/FLASH/IMU/CAN 的共用逻辑抽成公共模块，避免重复代码。
5. 最终综合 sample 名称固定为 zephyr-testhub-at，默认使用 USB CDC ACM 作为 AT 主控制口。
6. UART 邮票孔 PA9/PA10 作为被测 UART 通道优先，不要和综合 sample 控制口耦合。
7. 综合 sample 中任一时刻只允许运行一个主测试模式；切换模式前必须 stop 上一个模式。
8. 对测试表中依赖硬件细节但当前未确认的项，例如 BAT/FLASH/IMU/HEAT/CAN 细节，先依据文档检查现有 DTS、原理图结论和器件连接，再决定具体实现。
9. 所有输出统一格式，AT 响应与日志分离，错误码统一。
10. 在开始编码前，先阅读 xiao-stm32c5-test-plan-sample-requirements.md，并把开发拆分为多个小步提交。
```

---

## 12. Conclusion

本轮需求整理的核心结论如下：

- 路线正确：**单功能 sample 先行，AT 综合 sample 后置**
- 控制口建议明确：**综合 sample 用 USB CDC，UART 邮票孔用于 UART 测试**
- 代码组织要提前为聚合做准备：**公共模块化，而不是 demo 堆砌**
- BAT、FLASH、IMU、HEAT、CAN 的部分细节仍需继续结合原理图和器件信息确认

本文档后续应持续更新，作为 XIAO STM32C5 test plan 样例开发的主需求说明书。
