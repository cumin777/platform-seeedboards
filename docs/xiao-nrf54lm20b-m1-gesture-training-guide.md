# XIAO nRF54LM20B：自训练手势分类模型（M1）

目标：使用 XIAO nRF54LM20B 板载 LSM6DS3TR-C IMU 采集手势数据，训练自己的分类模型，并部署到 `edgeai-gesture-recognition-local` sample 的 Axon NPU。

手势识别是时序分类：一小段 IMU 数据对应一个类别，例如 `idle`、`swipe_left` 或 `shake`。

## 1. 使用程序获取训练用数据

使用 sample：

```text
examples/seeed-xiao-nrf54lm20b/edgeai-gesture-data-collection
```

### 1.1 固定采集格式

数据采集 sample 每约 10 ms 输出一行数据，前 6 个通道顺序固定为：

```text
accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z
```

采集 sample 的第 7 列是通过 USB CDC `label <name>` 设置的标签；训练上传时将它作为
标签列，前 6 列仍保持模型输入顺序和单位不变。

| 参数 | 当前配置 |
|---|---|
| 输入 | 3 轴加速度 + 3 轴陀螺仪 |
| 应用采样率 | 100 Hz（10 ms 一次） |
| IMU ODR | 104 Hz |
| 加速度量程 | ±4 g |
| 陀螺仪量程 | ±1000 dps |
| CSV 数值 | 加速度 `m/s² × 1000`；陀螺仪 `rad/s × 1000` |

训练数据必须使用这 6 列的相同顺序和单位。不要训练时使用 g/dps、部署时使用 SI 单位，也不要调整轴顺序。

### 1.2 打开采集模式

编辑 sample 的 `zephyr/prj.conf`，临时加入：

```ini
CONFIG_DATA_COLLECTION_MODE=y
CONFIG_BLE_MODE_NONE=y
```

此时程序不运行推理，而是通过 USB CDC 输出 CSV 行。编译、烧录并打开串口：

```powershell
cd D:\workspace\platform-seeedboards\examples\seeed-xiao-nrf54lm20b\edgeai-gesture-data-collection
pio run
pio run -t upload
pio device monitor -b 115200
```

正常数据类似：

```text
9812,-35,104,2,-1,0
9807,-42,98,3,0,-2
```

### 1.3 采集方法与数据集组织

建议先定义少量、明确的类别：

```text
idle
swipe_left
swipe_right
shake
rotate_left
rotate_right
```

需要注意：数据采集 sample 不会自动识别手势类别；类别由采集者通过 USB CDC 的 `label <name>` 命令指定。

第一版建议采用“每次只采集一个类别、采集后按类别保存”的方式：

1. 先确定本轮类别，例如 `swipe_left`。
2. 只做该类别的动作，完成若干次录制。
3. 将本轮日志保存到 `swipe_left` 目录，或在转换脚本中写入 `label=swipe_left`。
4. 再切换到下一个类别。

这不要求模型或 IMU 自动识别类别，而是由采集者在开始录制前指定标签。数据采集 sample
通过 USB CDC 接收 `label swipe_left`、`start`、`stop` 命令，只在 `start` 到 `stop`
之间输出数据，并在每行末尾附加当前标签。这样可以避免手工复制日志时把类别弄错。

每次录制只采集一个手势：静止 1～2 秒 → 做一次动作 → 静止 1～2 秒 → 停止保存。每个类别至少采集 100 个独立动作样本，并覆盖不同操作者、左右手、握持方向、速度和力度。`idle` 必须采集；如果实际环境会有其他动作，增加 `unknown` 类别。

文件建议按类别保存：

```text
gesture_dataset/
├── idle/user01_001.csv
├── swipe_left/user01_001.csv
├── swipe_right/user01_001.csv
├── shake/user01_001.csv
├── rotate_left/user01_001.csv
└── rotate_right/user01_001.csv
```

串口日志有启动信息时，只保留 7 列数据行。以下脚本假设当前文件中的所有数据都是
`swipe_left`，将旧版 6 列录制转换为带标签的 CSV：

```python
import re
from pathlib import Path

src = Path("raw_swipe_left.txt")
dst = Path("swipe_left.csv")
row = re.compile(r"^\s*-?\d+(?:,-?\d+){5}\s*$")

with src.open(encoding="utf-8", errors="ignore") as fin, \
     dst.open("w", encoding="utf-8", newline="") as fout:
    fout.write("accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,label\n")
    for line in fin:
        line = line.strip()
        if row.match(line):
            fout.write(line + ",swipe_left\n")
```

训练、验证、测试必须按录制批次或操作者分开。不要把同一段连续录制切成重叠窗口后随机分到三个集合，否则评估准确率会虚高。

## 2. 使用训练数据训练模型

官方入口：

- [Nordic Edge AI Lab](https://ai.lab.nordicsemi.com)
- [Edge AI Lab 文档](https://docs.nordicsemi.com/bundle/edge-ai-lab)
- [手势数据准备说明](https://docs.nordicsemi.com/r/bundle/edge-ai-lab/page/get_started.html/preparing-data-for-gesture-recognition?contentId=mI0oi_sFCmGnPTyqdX8TRQ)
- [编译到 Axon NPU](https://docs.nordicsemi.com/r/bundle/edge-ai-lab/page/compile_model.html/compile-for-axon-npu)

训练步骤：

1. 在 Edge AI Lab 创建 Solution，选择 `Classification` 和传感器时序数据。
2. 上传已标注 CSV，按当前页面要求指定 6 个输入列、标签列（常见名称为 `label`、`class` 或 `target`）和采样率。
3. 设置窗口长度。建议从约 1 秒开始，即约 100 个时间点；窗口和步长以最终导出的模型为准。
4. 训练后查看验证集混淆矩阵，重点检查左右滑动、静止与其他动作是否容易混淆。
5. 用未参与训练的独立测试录制评估效果。
6. 在导出页面选择 nRF54LM20B / Axon NPU 目标（若当前 Solution 提供该选项），下载完整模型包。

模型能否部署到本 sample 的判断标准是导出包中有 Axon 生成物，例如：

```text
nrf_edgeai_user_model.c
nrf_edgeai_user_model.h
nrf_edgeai_user_types.h
nrf_edgeai_user_model_axon.h
prj_example.conf
```

如果当前 Lab 手势流程只导出 `Neuton`，它是 CPU 模型，不是 Axon NPU 模型，不能直接放到本 sample。坚持使用 Axon 时，需要训练全 INT8 LiteRT/TensorFlow Lite 模型，再按 Axon 编译文档生成 Axon C/H 文件。

## 3. 向程序导入自定义模型

复制 `edgeai-gesture-recognition-local` 成自己的项目，例如 `edgeai-gesture-my-model`。将下载的 Axon 模型包整体放入：

```text
src/nrf_edgeai_generated/Axon/
├── nrf_edgeai_user_model.c
├── nrf_edgeai_user_model.h
├── nrf_edgeai_user_types.h
├── nrf_edgeai_user_model_axon.h
└── prj_example.conf
```

不要只替换一个头文件。然后在 `zephyr/CMakeLists.txt` 中把模型目录由外部官方目录改为项目内目录，并编译自己的 `nrf_edgeai_user_model.c`：

```cmake
set(CUSTOM_MODEL_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/nrf_edgeai_generated/Axon")

target_include_directories(app PRIVATE ${CUSTOM_MODEL_DIR})
target_sources(app PRIVATE
  "${CUSTOM_MODEL_DIR}/nrf_edgeai_user_model.c")
```

在 `zephyr/prj.conf` 中保留 Axon 配置，并把模型包 `prj_example.conf` 中的 buffer 值复制进来：

```ini
CONFIG_NRF_EDGEAI=y
CONFIG_NRF_AXON=y
CONFIG_NEWLIB_LIBC=y
CONFIG_FPU=y
CONFIG_NRF_AXON_INTERLAYER_BUFFER_SIZE=<模型要求的值>
CONFIG_NRF_AXON_PSUM_BUFFER_SIZE=<模型要求的值>
```

最后按新模型修改 `inference_postprocessing.h/.c` 的类别编号、名称和阈值；以生成模型中的 `MODEL_OUTPUTS_NUM`、`INPUT_UNIQ_FEATURES_NUM`、`INPUT_WINDOW_SIZE` 和 `INPUT_WINDOW_SHIFT` 为准。关闭采集模式后重新编译烧录：

```powershell
pio run
pio run -t upload
pio device monitor -b 115200
```

确认构建日志链接的是项目内的 `nrf_edgeai_user_model.c`，并用未参与训练的数据完成板端手势测试。

