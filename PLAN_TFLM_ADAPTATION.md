# 方案 A: TFLite Micro / LiteRT 适配实施计划

## 目标

在当前已适配 Neuton (Edge AI) 的分支上, 增加 TFLite Micro (LiteRT for Microcontrollers)
运行时支持, 并创建一个可编译运行的 hello_world 样例。

## 架构概述

```
当前分支 (xiao_nrf54lm20b_plan_b)
├── sdk-edge-ai (Neuton)     ← 已完成
├── ncs-compat               ← 已完成
└── tflite-micro             ← 本计划新增
    ├── 源码: zephyrproject-rtos/tflite-micro (zephyr-v4.1.0 分支)
    ├── 构建文件: Zephyr 树内 modules/tflite-micro/ (Kconfig + CMakeLists.txt)
    └── 优化: CMSIS-NN (已存在于 _pio/modules/lib/cmsis-nn/)
```

TFLM 采用 "reverse module" 模式 (与 cmsis-nn 相同):
- 源代码在外部 west 模块 (`_pio/modules/lib/tflite-micro/`)
- 构建文件在 Zephyr 树内 (`modules/tflite-micro/Kconfig` + `CMakeLists.txt`)
- `${ZEPHYR_CURRENT_MODULE_DIR}` 指向外部模块根目录

## 实施步骤

### 步骤 1: 拉取 tflite-micro 源码

```bash
cd ~/.platformio/packages/framework-zephyr@3.40201.251021/_pio/modules/lib/
git clone --branch zephyr-v4.1.0 https://github.com/zephyrproject-rtos/tflite-micro.git
cd tflite-micro
git reset --hard 8d404de73acf7687831e16d88e86e4f73cfddf8e
```

**验证**: `_pio/modules/lib/tflite-micro/zephyr/module.yml` 存在且内容为:
```yaml
name: tflite-micro
build:
  cmake-ext: True
  kconfig-ext: True
```

### 步骤 2: 在 west.yml 中注册 tflite-micro

在 framework-zephyr 的 `west.yml` 中, `ncs-compat` 条目后追加:

```yaml
  - name: tflite-micro
    path: modules/lib/tflite-micro
    revision: 8d404de73acf7687831e16d88e86e4f73cfddf8e
    url: https://github.com/zephyrproject-rtos/tflite-micro
```

**验证**: `platformio-build.py` 的 `is_project_required()` 对 tflite-micro 返回 True
(不以 `hal_`/`tool`/`nrf_hw_` 开头), 它会被加入 `ZEPHYR_MODULES`。

### 步骤 3: 更新 state.json

删除 `_pio/state.json` 或在其中添加 tflite-micro 条目, 使 `install-deps.py` 不跳过它:

```bash
rm ~/.platformio/packages/framework-zephyr@3.40201.251021/_pio/state.json
```

下次构建时 `install-deps.py` 会重新生成 state.json, 包含 tflite-micro。

### 步骤 4: 创建 tflm-hello-world 样例

从 Zephyr 内置 sample (`samples/modules/tflite-micro/hello_world/`) 复制源码,
适配为 PIO 项目:

```
examples/tflm-hello-world/
├── platformio.ini
├── zephyr/
│   ├── CMakeLists.txt
│   ├── prj.conf
│   └── boards/
│       └── xiao_nrf54lm20a_nrf54lm20a_cpuapp_npu.overlay
└── src/
    ├── main.c              # C 入口 (调用 setup()/loop())
    ├── main_functions.cpp  # setup() + loop() 实现
    ├── main_functions.h
    ├── model.cpp           # sine wave 模型 + tensor arena
    ├── model.h
    ├── output_handler.cpp  # 输出处理 (LOG_INF)
    ├── output_handler.hpp
    ├── constants.h         # kInferencesPerCycle / kXrange
    ├── constants.c
    └── assert.cpp          # TF Micro assert → Zephyr __ASSERT
```

**platformio.ini**:
```ini
[env:seeed-xiao-nrf54lm20a-npu]
platform = file:///home/seeed/workspace/nrf54lm20a_pio/platform-seeedboards
framework = zephyr
board = seeed-xiao-nrf54lm20a-npu
monitor_speed = 115200
```

**zephyr/CMakeLists.txt** (改编自 Zephyr 内置 sample):
```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tflm_hello_world)

set(NO_THREADSAFE_STATICS $<TARGET_PROPERTY:compiler-cpp,no_threadsafe_statics>)
zephyr_compile_options($<$<COMPILE_LANGUAGE:CXX>:${NO_THREADSAFE_STATICS}>)

file(GLOB app_sources src/*.c* src/*.cpp)
target_sources(app PRIVATE ${app_sources})
```

**zephyr/prj.conf**:
```kconfig
CONFIG_CPP=y
CONFIG_STD_CPP17=y
CONFIG_TENSORFLOW_LITE_MICRO=y
CONFIG_TENSORFLOW_LITE_MICRO_CMSIS_NN_KERNELS=y
CONFIG_MAIN_STACK_SIZE=4096
CONFIG_REQUIRES_FLOAT_PRINTF=y
CONFIG_NEWLIB_LIBC=y
CONFIG_FPU=y
CONFIG_NCS_SAMPLES_DEFAULTS=y
```

> `CONFIG_MAIN_STACK_SIZE=4096`: TFLM interpreter 初始化需要较大栈。
> `CONFIG_NEWLIB_LIBC=y`: GLIBCXX_LIBCPP 依赖 NEWLIB_LIBC 或 PICOLIBC。

**src/model.cpp**: 需要内嵌 sine wave 模型数据。
从 Zephyr sample 的 `model.cpp` 中复制 `g_model[]` 数组和 `kModelPath`。

**src/output_handler.cpp**: 用 Zephyr LOG 替代 TF_LOG:
```cpp
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(output_handler, LOG_LEVEL_INF);
void HandleOutput(tflite::ErrorReporter* error_reporter, float x, float y) {
    LOG_INF("x_value: %f, y_value: %f\n", x * 2.f, y);
}
```

**src/assert.cpp**: 重定向 MicroPrintf 断言到 Zephyr:
```cpp
void __assert_func(const char *file, int line, const char *fn, const char *expr) {
    __ASSERT(false, "TFM assert: %s:%d %s %s", file, line, fn, expr);
}
```

### 步骤 5: 首次构建与调试

预期可能遇到的问题及解决方案:

| 问题 | 原因 | 解决 |
|------|------|------|
| C++ 源文件不被编译 | PIO auto-glob 的 glob 模式 | 确认 `file(GLOB src/*.c*)` 能匹配 `.cpp` |
| libstdc++ 链接错误 | GLIBCXX_LIBCPP 未正确选择 | 检查 `CONFIG_REQUIRES_FULL_LIBCPP` 是否生效 |
| `state.json` 阻止新模块 | install-deps.py 跳过已存在 state | 删除 state.json 或 `--secondary-installation` |
| CMSIS-NN 符号冲突 | 多个 CMSIS 版本 | 确保 `modules/hal/cmsis_6/` 不与 TFLM 冲突 |
| tensor arena 不足 | hello_world 需要 2KB | 已在 model.cpp 中设置 `kTensorArenaSize = 2000` |
| `no_threadsafe_statics` 属性缺失 | compiler-cpp target 属性 | Zephyr 4.2.1 已支持, 无需额外配置 |

构建命令:
```bash
cd examples/tflm-hello-world
rm -rf .pio/build/seeed-xiao-nrf54lm20a-npu
pio run -v 2>&1 | tee /tmp/tflm_build.log
```

### 步骤 6: 验证

1. **符号检查**:
   ```bash
   arm-none-eabi-nm firmware.elf | grep -w "main"         # → T main (强符号)
   arm-none-eabi-nm firmware.elf | grep "tflite\|TfLite\|MicroInterpreter"  # TFLM 符号存在
   ```

2. **Flash 占用**: 预期 ~100-150KB (TFLM 运行时 + CMSIS-NN + hello_world 模型)

3. **串口输出**: 预期每秒输出推理结果:
   ```
   x_value: -1.000000, y_value: -0.950000
   x_value: -0.980000, y_value: -0.900000
   ...
   ```

### 步骤 7: 输出固件和文档

```
ai_auto_b/
├── tflm-hello-world.hex
├── tflm-hello-world.bin
├── tflm-hello-world_expected_behavior.md
└── (保留之前的 Neuton 固件)
```

## 依赖分析

| 组件 | 当前状态 | 需要的工作 |
|------|----------|-----------|
| tflite-micro 源码 | 不存在 | 步骤 1: git clone |
| west.yml 条目 | 不存在 | 步骤 2: 添加条目 |
| state.json | 存在(旧) | 步骤 3: 删除以刷新 |
| CMSIS-NN | 已存在 | 无需工作 |
| CMSIS-DSP | 已存在 | 无需工作 |
| C++ 编译器 (g++) | 已存在 | 无需工作 |
| GLIBCXX_LIBCPP | Kconfig 已支持 | prj.conf 中设置 NEWLIB_LIBC=y |
| Zephyr 树内 modules/tflite-micro/ | 已有 Kconfig+CMakeLists.txt | 无需工作 |

## 风险与对策

### 风险 1: PIO Zephyr 构建器的 C++ 处理

**描述**: `platformio-build.py` 的 `build_library()` 和 `compile_source_files()`
从未在本项目中处理过 `.cc/.cpp` 文件。

**评估**: SCons 原生支持 `.cpp` 扩展名, 会自动使用 g++。CMake code model 会正确标记
C++ 源文件的语言类型。`platformio-build.py` 的 `compile_source_files()` 从 code model
读取编译选项, 应能正确传递 C++ 编译标志。

**回退**: 如果 `compile_source_files()` 无法编译 `.cpp`, 可修改它或使用
`ZEPHYR_APP_BUILD_CONTROL` 让 CMake/ninja 直接构建 app 库。

### 风险 2: Flash 占用

TFLM 运行时 (~100-150KB) 远大于 Neuton (~5KB), 但 nRF54LM20A 有 2MB Flash, 完全够用。

### 风险 3: TFLM 与 sdk-edge-ai 共存

两者是独立的 ML 运行时, 不共享符号, 可以在同一分支上共存。
但同一个样例项目只能使用其中一个 (不能在 prj.conf 中同时启用两者)。

## 不涉及的范围

- 不修改 sdk-edge-ai 或 ncs-compat 模块
- 不修改已有的 Edge AI 样例 (classification/regression/anomaly)
- 不涉及 Axon NPU 加速 (TFLM 在 CPU 上运行)
- 不涉及模型训练或转换 (使用 hello_world 内置 sine 模型)
