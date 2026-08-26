# XIAO nRF54LM20B 自定义唤醒词与关键词识别（Axon NPU）

这是一个本地适配 sample。它复用 XIAO 板载 PDM 麦克风、nPM1300 麦克风供电、USB CDC 日志和 Axon NPU。目前已接入下列两份自定义模型；两者都由 Axon NPU 加速。

- WW：`Hello_Seeed_95647_wake_word.zip`，solution 95647，标签 `hello seeed`；
- KWS：`seeed_key word_95649_kws.zip`，solution 95649，类别顺序为 `OTHER, SILENCE, no, ok, opus, stop, yes`。

模型分别是：

- 唤醒词模型（WW）：持续监听，检测成功后进入关键词窗口；
- 关键词模型（KWS）：在窗口中识别命令词，超时后回到唤醒词监听。

## 训练前固定的音频接口

固件使用板载 MSM261DGT006 PDM 麦克风，当前音频配置由 `src/dmic.h` 定义。训练数据、Edge AI Lab 项目和最终固件必须一致：

- 单声道、左声道；
- 16 kHz PCM；
- 16-bit 有符号采样；
- 模型的输入窗口大小必须等于 `DMIC_SAMPLES_IN_BLOCK`；
- 音频预处理（例如 mel 特征）由导出的 Nordic Edge AI Lab 模型描述，固件不能自行改变其配置。

不要在训练完成后随意修改 `src/dmic.h` 的采样率、块大小或通道配置。若模型导出的窗口与固件不一致，`ww_init()` 或 `kws_init()` 的断言会阻止设备运行。

## 训练与导出（Nordic Edge AI Lab）

当前 Nordic 官方的 Wake Word 与 Keyword Spotting 都是 **No data required**
流程：输入英文短语/命令文本，平台自动生成训练数据、训练并为 nRF54LM20B
的 Axon NPU 生成模型。不要为这两个任务创建 Neuton/CPU 模型。

1. 登录 [Nordic Edge AI Lab](https://ai.lab.nordicsemi.com)，在 **My Solutions** 选择 **Add New Solution**。
2. 创建 WW：**Model type = Axon**，**Task type = Wake Word Detection**；输入 1–3 个英文单词（4–30 个英文字符），试听发音后点击 **Start**。官方预计约 1 小时。
3. 创建 KWS：**Model type = Axon**，**Task type = Keyword Spotting**；添加命令词后点击 **Start**。建议单词命令、约 1 秒内说完、避免读音相近的词；官方预计约 2–3 小时。
4. 两个项目训练完成后，在 **Results** 中下载各自的模型 archive。分别使用浏览器麦克风或录音进行 Live Test；WW 优先运行 **Auto Tune**，KWS 对每个命令调节 **threshold** 和 **predictions in a row**。
5. 记录每个模型包的输入窗口、输出类别及索引顺序、检测阈值和 Axon buffer 要求。真实板端/目标环境录音只用于独立验收与阈值校准，不能省略。

官方链接：

- [Wake Word Detection](https://docs.nordicsemi.com/r/bundle/edge-ai-lab/page/wake_word.html)
- [Keyword Spotting](https://docs.nordicsemi.com/r/bundle/edge-ai-lab/page/keyword_spotting.html)
- [Edge AI Add-on（Axon 板端集成）](https://docs.nordicsemi.com/bundle/addon-edge-ai_latest/page/index.html)

## 安装导出的模型

将导出物**整体**复制到下列目录，且不要将 WW 和 KWS 文件混在同一个目录：

```text
src/ww/nrf_edgeai_generated/
src/kws/nrf_edgeai_generated/
```

只训练完成一侧时，可以只替换一侧目录；另一侧仍会使用 sample 23 的参考模型。

WW 包需要包含：

```text
nrf_edgeai_generated/nrf_edgeai_user_model.h
```

并提供它调用的模型实例函数。CMake 会将 WW 和 KWS wrapper 分别绑定到各自的模型头文件，避免两个包同名 `nrf_edgeai_user_model.h` 的冲突。对于当前已安装的模型，wrapper 调用：

```c
nrf_edgeai_user_model_95647();
nrf_edgeai_user_model_95649();
```

若后续 Lab 导出的 solution ID 改变，只修改对应的 `src/ww/wakeword.c` 或 `src/kws/kws.c`，不要修改生成模型文件。

## 必须修改的三个位置

1. `zephyr/prj.conf`：将 `CONFIG_NRF_AXON_INTERLAYER_BUFFER_SIZE` 与 `CONFIG_NRF_AXON_PSUM_BUFFER_SIZE` 改成**两个模型所需值中的较大值**。
2. `src/kws/kws.c`：更新 `enum keyword_class` 和 `keyword_detection_ctxs[]`，使顺序、个数和名称与 KWS 模型输出严格一致。`silence`、`unknown` 的索引也必须正确。
3. `src/main.c`：将启动日志中的示例短语/关键词改成你训练的内容；按现场测试结果调整 Kconfig 中 WW 历史阈值和 KWS EMA/置信度阈值。

唤醒词 wrapper 目前把“模型最高概率类别”视为检测候选。若你的唤醒词模型包含多个输出类别，必须在 `src/ww/wakeword.c` 中明确只接受目标唤醒词所在类别，不能只依赖最高概率。

## 构建与烧录

在本目录执行：

```powershell
pio run -e seeed-xiao-nrf54lm20b
pio run -t upload -e seeed-xiao-nrf54lm20b
pio device monitor -b 115200
```

默认运行方式为“唤醒词触发 KWS”。测试模式可在 `zephyr/prj.conf` 中切换：

```ini
CONFIG_APP_MODE_WW_GATED_KWS=y  # 默认
# CONFIG_APP_MODE_WW_ONLY=y
# CONFIG_APP_MODE_KWS_ONLY=y
```

## 首次验收清单

- 两个模型都确认为 Axon，而不是 Neuton/CPU 包。
- 设备启动、DMIC 初始化、Axon 初始化均无错误。
- 训练时的采样率、窗口长度、通道和预处理与固件相同。
- KWS 输出类别数与 `KEYWORDS_COUNT` 断言一致。
- 在未参与训练的录音和真实环境中分别统计误唤醒、漏唤醒、命令误识别和端到端延迟。

模型版本、数据集划分、buffer 值、类别映射和现场测试结果请记录到同目录的 `MODEL_CARD.md`。
