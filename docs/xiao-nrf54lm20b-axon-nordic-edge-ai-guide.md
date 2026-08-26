# XIAO nRF54LM20B：Axon NPU + Nordic Edge AI Lab 自定义模型指南

> 范围：本指南只覆盖 **XIAO nRF54LM20B** 上、由 **Nordic Edge AI Lab** 训练并在 **Axon NPU** 上执行的自定义模型。首期目标是完成分类、回归、异常检测、手势识别、唤醒词/关键词识别五类模型的训练和部署。
>
> 不在本期范围：通用 TensorFlow Lite → Axon Compiler 和 Edge Impulse 模型。两者的占位及后续接入要求见本文末尾。

## 1. 目标与边界

### 1.1 首期交付目标

| 编号 | 模型类型 | 典型输入 | 训练平台 | 目标执行位置 | XIAO 起始示例 |
|---|---|---|---|---|---|
| M1 | 分类（Classification） | 传感器时序或提取后的特征 | Nordic Edge AI Lab | Axon NPU | `edgeai-classification` |
| M2 | 回归（Regression） | 多个连续或离散数值特征 | Nordic Edge AI Lab | Axon NPU | Nordic `samples/nrf_edgeai/regression`，需新增 XIAO 包装 sample |
| M3 | 异常检测（Anomaly detection） | 正常工况的传感器时序/特征 | Nordic Edge AI Lab | **待确认 Axon 导出能力** | Nordic `samples/nrf_edgeai/anomaly`，当前仅含 Neuton/CPU 示例 |
| M4 | 手势识别（Gesture recognition） | 加速度计、陀螺仪时序 | Edge AI Lab（官方教程为 Neuton）或自有 LiteRT → Axon | **当前 Lab 手势教程不是 Axon** | `edgeai-gesture-recognition-local`（预训练 Axon 模型） |
| M5 | 唤醒词/关键词识别（Wake word / KWS） | 16 kHz 单声道音频 | Nordic Edge AI Lab | Axon NPU | `edgeai-wake-words-local` |

除 M3、M4 的兼容性验证外，所有模型都必须在 Nordic Edge AI Lab 中选择面向 **nRF54LM20B / Axon NPU** 的目标。不要把 M4 官方 Neuton 手势包误认为 Axon 包；即使 API 类似，也不满足本项目的 NPU 加速目标。

**M3 的当前技术闸门：** 本地 Edge AI Add-on v2.1.0 的 `samples/nrf_edgeai/anomaly` 只包含 Neuton 生成模型，且没有 `CONFIG_NRF_EDGEAI_ANOMALY_MODEL_AXON` 配置项。因此在 Edge AI Lab 实际成功导出一份 Axon 异常检测模型包并完成 XIAO 编译前，不能把 M3 视为已确认可用的“Nordic Edge AI Lab → Axon”路径。若 Lab 不能导出 Axon 包，M3 应留到后续的 **TensorFlow Lite → Axon Compiler** 阶段实现，而不能降级为 CPU 模型。

### 1.2 Axon 模型设计红线

Axon 不是通用深度学习运行时。模型包最终必须为 8-bit 量化、且可由 Axon 后端生成。对于由 Nordic Edge AI Lab 生成的模型，平台会完成模型生成；设计输入数据和网络时仍应遵守以下硬件约束：

- 一个模型最多一个外部输入、20 个输出；每个节点最多两个输入。
- 适合卷积型时序/CNN 网络；ReLU、ReLU6、LeakyReLU、Conv1D/2D、池化、Mean、Dense 等是优先组合。
- 复杂 RNN、LSTM、GRU、Transformer、Attention 或任意动态算子不是本期目标。
- Dense 的输入向量和输出神经元均应控制在 2048 以内。
- 每次训练后以生成模型要求的 `INTERLAYER_BUFFER_SIZE`、`PSUM_BUFFER_SIZE` 为准；不可沿用其他 sample 的数值。

参考：`D:/workspace/ncs/edge_add_on/edge-ai/doc/axon_compiler/supported_operators.rst`。

## 2. 已确认的本地基础

| 项目 | 本地状态 |
|---|---|
| PlatformIO | `6.1.19` |
| Platform 仓库 | `D:/workspace/platform-seeedboards` |
| Edge AI Add-on | `D:/workspace/ncs/edge_add_on/edge-ai`，`v2.1.0-4-g7ff0d0d` |
| Axon 编译器二进制 | 已存在：`tools/axon/compiler/bin/Windows/nrf-axon-nn-compiler-lib-amd64.dll` |
| XIAO Axon 示例 | 22、23、24、25、26 均已在仓库内 |
| Nordic Edge AI Lab 自定义模型部署 | 已具备运行时和构建集成能力 |

PlatformIO 在 `zephyr/prj.conf` 发现 `CONFIG_NRF_EDGEAI=y` 或 `CONFIG_NRF_AXON=y` 时，会自动注册 `sdk-edge-ai` Zephyr 模块。因此，自定义 sample 不需要手工克隆该模块。

注意：当前默认 Python 环境不含 TensorFlow、NumPy、TFLite、scikit-learn 等包。它不会影响 Nordic Edge AI Lab 的云端训练和生成源码的部署；仅当后续进入“自建 TensorFlow Lite → Axon Compiler”阶段时，才需要单独建立 Python 3.11 环境。

## 3. 总体工作流

```text
定义任务/标签
    ↓
采集、清洗、标注真实数据
    ↓
Nordic Edge AI Lab：导入数据 → 选择 Axon 目标 → 训练、评估
    ↓
下载 Axon 模型包（生成的 C/H 源码和配置）
    ↓
替换 XIAO sample 的 nrf_edgeai_generated 目录
    ↓
按模型更新输入采集、特征顺序、窗口和结果处理
    ↓
写入模型给出的 Axon 缓冲区配置
    ↓
pio run → pio run -t upload → USB CDC 实机验证
```

训练集、验证集和最终实机测试集必须相互独立。特别是传感器数据，不要把同一次连续录制简单随机切片后同时放入训练和测试集，否则准确率会明显虚高。

## 4. Nordic Edge AI Lab 训练通用流程

### 4.1 准备数据

1. 明确一次推理的输入：采样率、通道、窗口长度、步长和单位。
2. 采集覆盖真实场景的数据：不同用户、姿态、安装位置、噪声、环境温度、设备个体和边界条件。
3. 清洗无效记录，并划分训练/验证/测试集。
4. 导入到 Nordic Edge AI Lab。
5. 分类任务的 `target` 列使用从 `0` 起连续编号的整数；类别名称另行维护为标签表。
6. 在创建模型时明确选择 Axon NPU 目标硬件，然后完成训练、评估和模型包导出。

模型的输入字段顺序、采样率、窗口大小、缩放/特征提取参数是接口契约。固件采集端必须与训练时完全一致；只替换模型而不调整固件输入流程，通常会导致输出无意义。

### 4.2 导出物

下载的 Nordic Edge AI Lab Axon 模型包通常包含如下生成文件（实际名称可能随模型类型变化）：

```text
nrf_edgeai_user_model.c
nrf_edgeai_user_model.h
nrf_edgeai_user_types.h
nrf_edgeai_user_model_axon.h
prj_example.conf            # 若模型包提供，应作为 buffer 配置的唯一依据
```

在任何 sample 中替换模型时，应整体替换模型包生成的目录内容，不能只替换 `nrf_edgeai_user_model_axon.h`。

### 4.3 固件的最小 Axon 配置

所有自定义项目的 `zephyr/prj.conf` 至少应具有：

```ini
CONFIG_NRF_EDGEAI=y
CONFIG_NRF_AXON=y
CONFIG_NEWLIB_LIBC=y
CONFIG_FPU=y
CONFIG_LOG=y

# 下面两个值必须取自本次导出的模型包，示例数字不可直接复用。
CONFIG_NRF_AXON_INTERLAYER_BUFFER_SIZE=<model_required_value>
CONFIG_NRF_AXON_PSUM_BUFFER_SIZE=<model_required_value>
```

若工程将浮点结果打印到控制台，再增加：

```ini
CONFIG_NEWLIB_LIBC_FLOAT_PRINTF=y
```

Axon 节点也必须在该 sample 的板级 overlay 中启用：

```dts
&axon {
    status = "okay";
};
```

现有 `22` 至 `26` 示例均可作为 overlay 参考。

## 5. 五类模型的实施路线

### M1：分类

**起点：** `examples/seeed-xiao-nrf54lm20b/edgeai-classification`

该 sample 当前是“包裹状态”分类：一段 50 个加速度模长样本，输出 7 个类别。它证明了 XIAO、`CONFIG_NRF_EDGEAI` 和 Axon NPU 的基本链路，但其输入窗口、类别和数据采集逻辑不能直接当作自定义分类任务的实现。

实施步骤：

1. 明确类别表、采样频率、通道和窗口（例如 3 轴加速度的 128 点窗口）。
2. 在 Edge AI Lab 中训练 Axon 分类模型并下载模型包。
3. 新建项目时复制 `24`；建议不要直接改官方外部模型目录，而是将自己的生成文件放在项目内：

   ```text
   src/nrf_edgeai_generated/Axon/
   ```

4. 将 `zephyr/CMakeLists.txt` 的模型源改为项目内的 `nrf_edgeai_user_model.c`，并增加该目录的 include path。
5. 修改 `main.c`：按训练时顺序喂入数据，等待窗口完整后执行推理，按模型实际类别数读取输出。
6. 从导出配置复制 `CONFIG_NRF_AXON_*_BUFFER_SIZE`，构建并验证混淆矩阵和现场误报率。

验收：每个类别都有独立测试录制；串口能输出“预测类别 + 置信度”；模型未见过的环境下的误报率可接受。

### M2：回归

**起点：** `D:/workspace/ncs/edge_add_on/edge-ai/samples/nrf_edgeai/regression`

回归模型输出连续数值，例如空气质量、浓度、估计距离、温度补偿值或设备健康指标。当前 XIAO 示例目录没有对应包装工程，因此本项目应在 M1 验证稳定后新增一个本地 `edge-ai-regression-local` sample。

实施步骤：

1. 定义每一行输入特征和目标连续值的标定方法；单位、校准方式和缺失值策略必须固定。
2. 上传带 `target` 连续数值列的数据到 Edge AI Lab，选择 Axon 回归模型。
3. 从 Nordic regression sample 复制 `main.c` 的 API 调用模式和 CMake 的 Axon 模型选择方式。
4. 将生成的 Axon 模型文件置入项目内 `src/nrf_edgeai_generated/Axon/`。
5. 用如下 Kconfig 选择 Axon 回归模型（若采用 Nordic sample 的模型选择结构）：

   ```ini
   CONFIG_NRF_EDGEAI=y
   CONFIG_NRF_EDGEAI_REGRESSION_MODEL_AXON=y
   ```

6. 以 `MAE`、`RMSE` 和真实工况误差为验收指标；不要只看训练损失。

验收：输出值的单位、范围和更新频率正确；对保留测试集和实测校准点分别记录误差。

### M3：异常检测（Axon 可行性验证门）

**起点：** `D:/workspace/ncs/edge_add_on/edge-ai/samples/nrf_edgeai/anomaly`

异常检测通常只使用“正常工况”训练，输出偏离正常模式的异常分数，而不是异常类别。阈值不是固定常数，必须根据实测正常/异常数据确定。

**先决条件：** 当前本地官方 anomaly sample 内的 `src/nrf_edgeai_generated/nrf_edgeai_user_model.c` 是 Neuton 模型，不可直接用于本项目的 Axon 目标。开始本任务前，先在 Nordic Edge AI Lab 验证其导出物中是否存在 Axon 模型实现和对应的 `nrf_edgeai_user_model_axon.h`/buffer 配置。该检查通过，才新建 XIAO Axon sample；不通过则停止在数据集准备和评估阶段，等待后续 TensorFlow Lite 路线。

实施步骤：

1. 收集尽可能多样的正常工况；保留独立的故障/异常录制仅用于评估和设阈值。
2. 在 Edge AI Lab 中尝试创建针对 Axon 的异常检测模型，并检查导出物是否为 Axon 模型包。
3. **仅在第 2 步通过后**新增 `edge-ai-anomaly-local` XIAO sample，并将模型生成文件置于 `src/nrf_edgeai_generated/`。
4. 参考 Nordic anomaly sample，读取 anomaly score；不要把最高分类概率当作异常分数。
5. 在设备实测数据上绘制正常和异常 score 分布，选取阈值并记录误报/漏报。
6. 配置模型包要求的 Axon buffer，构建、烧录和长期运行测试。

验收：设备打印 score 和阈值判断；正常条件下误报率、模拟异常时的检出率均有实测记录；构建日志确认链接的是 Axon 模型而非 Neuton。阈值应作为 Kconfig 参数或配置项，不应硬编码在模型生成文件中。

### M4：手势识别（当前官方训练路径为 Neuton）

**起点：** `examples/seeed-xiao-nrf54lm20b/edgeai-gesture-recognition-local`

该 sample 已接入 XIAO 的 LSM6DSL IMU，并将一个 Nordic 提供的预训练 Axon
模型输出到 USB CDC 日志。但 Nordic 官方 Edge AI Lab 的手势教程目前要求在
Create Solution 中选择 **Neuton → Classification**，上传加速度计/陀螺仪 CSV；
它没有“Gesture Recognition”独立任务，也不能从该流程导出 Axon 手势模型。

实施步骤：

1. 固定佩戴/握持方向、采样率、加速度和陀螺仪量程；它们必须与数据采集和最终推理一致。
2. 基于官方 [Gesture Recognition 数据准备教程](https://docs.nordicsemi.com/r/bundle/edge-ai-lab/page/get_started.html/preparing-data-for-gesture-recognition?contentId=mI0oi_sFCmGnPTyqdX8TRQ) 输出原始 6 轴数据，完成自有手势数据采集、标注和窗口分割。
3. 若接受 Neuton：在 Edge AI Lab 创建 **Neuton → Classification** solution，上传 CSV，选择 `class` 为 target，完成训练并部署 Neuton 模型。
4. 若必须 Axon：先在本地训练一个全整型 int8 LiteRT/TFLite 手势分类模型，再使用官方 [Compile your own model](https://docs.nordicsemi.com/r/bundle/edge-ai-lab/page/compile_model.html) 的 **Compile for Axon NPU** 流程。只有编译成功并生成 `outputs/` 中的 Axon C/H 文件后，才能替换 sample 的模型源。
5. 用自有生成目录替换原 sample 引用的 `nrf54lm20dk/Axon` 模型源。
6. 确保 `imu_lsm6dsl.c` 的采样频率、轴顺序、量程、窗口长度和模型要求一致。
7. 修改类别字符串/动作映射和后处理阈值。

验收：不同操作者、左右手、不同速度和不同初始方向的识别结果均记录；若方向敏感，应明确写入产品使用约束或扩充训练集。

### M5：唤醒词与关键词识别

**自定义模型 sample：** `examples/seeed-xiao-nrf54lm20b/edgeai-wake-kws-custom`

该 sample 以 `edgeai-wake-words-local` 的已验证硬件流程为基础，但把
Nordic Edge AI Lab 导出的模型位置固定在项目内；模型文件会随项目 Git 历史
保存，不需要修改 Edge AI Add-on。

该 sample 为两个模型分别预留了项目内目录：

```text
src/ww/nrf_edgeai_generated/
src/kws/nrf_edgeai_generated/
```

它实现“唤醒词触发后，在限定时间内识别关键词”的两阶段逻辑。两个模型独立训练、独立导出、独立替换。

实施步骤：

1. 在 [Nordic Edge AI Lab](https://ai.lab.nordicsemi.com) 创建 WW 和 KWS 两个 solution，均选择 **Axon**。
   当前官方流程对这两种语音任务标为 **No data required**：平台根据英文唤醒短语/命令文本自动生成训练数据并训练。
2. WW 使用 1–3 个英文单词（4–30 个英文字符）；KWS 使用易发音、约 1 秒内完成且读音差异明显的命令词。
3. 在 Results 中下载两个 Axon model archive；用官方 Live Test（WW 可先 Auto Tune）调节阈值。
4. 分别整体替换 `src/ww/nrf_edgeai_generated/` 和 `src/kws/nrf_edgeai_generated/`。
   在模型目录仍只有 `.gitkeep` 时，sample 会回退到 sample 23 的参考模型，因而可先验证麦克风、日志和 Axon NPU 链路；任意一侧放入自定义模型后，该侧自动改用自定义模型。
5. 在 `wakeword.c`、`kws.c` 中更新模型实例获取函数、类别枚举、关键词名称数组及概率/历史窗口阈值。
6. 使用两个模型中较大的 interlayer/psum 要求作为全局 `CONFIG_NRF_AXON_*_BUFFER_SIZE` 配置。

官方训练文档：

- [Wake Word Detection](https://docs.nordicsemi.com/r/bundle/edge-ai-lab/page/wake_word.html)
- [Keyword Spotting](https://docs.nordicsemi.com/r/bundle/edge-ai-lab/page/keyword_spotting.html)
- [Compile for Axon NPU](https://docs.nordicsemi.com/r/bundle/edge-ai-lab/page/compile_model.html/compile-for-axon-npu)

验收：记录误唤醒率、漏唤醒率、命令准确率、端到端响应时间；测试必须包含设备实际使用时的噪声和播放源。

## 6. 推荐的本地 sample 布局

为避免覆盖官方 add-on 中的文件，自定义模型应始终放在 PlatformIO 项目内。建议最终目录如下：

```text
examples/seeed-xiao-nrf54lm20b/
├── edge-ai-classification-local/
│   ├── src/nrf_edgeai_generated/Axon/
│   ├── src/main.c
│   └── zephyr/{CMakeLists.txt,prj.conf,boards/...overlay}
├── edge-ai-regression-local/
├── edge-ai-anomaly-local/
├── edge-ai-gesture-local/
└── edge-ai-wake-kws-local/
    ├── src/ww/nrf_edgeai_generated/
    └── src/kws/nrf_edgeai_generated/
```

`edgeai-classification` 和 `edgeai-gesture-recognition-local` 当前为了复用 Nordic 官方 demo，会通过 `XIAO_EDGE_AI_DIR` 引用 add-on 内的模型文件。自定义项目不要继续采用该外部模型路径；应改成工程内模型目录，才能让模型版本跟随项目的 Git 历史。

## 7. 构建、烧录与验证

在某个 sample 根目录执行：

```powershell
pio run -e seeed-xiao-nrf54lm20b
pio run -t upload -e seeed-xiao-nrf54lm20b
pio device monitor -b 115200
```

每次替换模型后，至少检查：

- CMake 输出是否实际编译了项目内的 `nrf_edgeai_user_model.c`。
- 编译期 static assertion 是否提示 Axon interlayer/psum buffer 太小。
- 串口日志是否显示稳定推理，且无内存/堆栈异常。
- 输入频率、窗口长度、字段/轴顺序与训练记录一致。
- 模型输出类别数、标签表、阈值和应用逻辑一致。
- 用从未参与训练的数据完成桌面测试和板端实测。

## 8. 训练与部署记录模板

每个模型在仓库中维护一份 `MODEL_CARD.md`，至少记录：

```text
模型名称 / 版本：
任务类型：分类 / 回归 / 异常 / 手势 / 唤醒词 / KWS
Edge AI Lab 项目链接或项目 ID：
目标硬件：nRF54LM20B Axon NPU
固件 sample / Git commit：
输入：通道、单位、采样率、窗口、步长、预处理：
标签（如有）：
训练、验证、测试数据来源及划分规则：
训练指标与保留测试指标：
模型生成目录 SHA256 / 导出日期：
CONFIG_NRF_AXON_INTERLAYER_BUFFER_SIZE：
CONFIG_NRF_AXON_PSUM_BUFFER_SIZE：
板端测试条件、结果、阈值：
已知失败场景：
```

## 9. 后续占位：TensorFlow Lite 与 Edge Impulse

### 9.1 TensorFlow Lite → Axon Compiler（待后续实现）

计划支持：自行训练的 Keras/TensorFlow 模型，导出为 **全 INT8 `.tflite`** 后，使用本地 `tools/axon/compiler` 生成 Axon C 头文件和模型对象。

进入该阶段前需要完成：Python 3.11 虚拟环境、`requirements.txt` 依赖安装、TFLite 算子扫描、生成源文件的 CMake 集成和板端一致性测试。

### 9.2 Edge Impulse（待后续实现）

计划支持：在 Edge Impulse Studio 训练后，导出 **Nordic Axon NPU Library** ZIP，并在 XIAO sample 中以 `CONFIG_EDGE_IMPULSE_PATH` 引用该 ZIP。

该路径要求的模型名、Axon buffer、`conf_overlay.conf` 和模型 archive CMake 集成将在后续单独建立 sample 后补充。

## 10. 本期实施顺序

建议按以下顺序推进，以最小化同时引入的变量：

1. 先构建/运行现有 `edgeai-gesture-recognition-local`，确认 IMU、USB CDC、Axon NPU 链路。
2. 完成 M4 自定义手势模型；它复用了最多板级实现。
3. 完成 M1 通用分类模型，沉淀“项目内生成模型”模板。
4. 完成 M2 回归，新增本地 XIAO sample；同时执行 M3 的 Axon 模型导出验证。
5. 完成 M5 唤醒词/KWS，重点进行真实噪声条件测试。
6. 若 M3 验证通过，新增异常检测 XIAO sample；若不通过，保留其数据集和评估方案，等待 TensorFlow Lite 路线。
7. 在已确认的 Nordic Edge AI Lab Axon 模型稳定后，再开启 TensorFlow Lite 和 Edge Impulse 路线。

