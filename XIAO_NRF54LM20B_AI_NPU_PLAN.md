# XIAO nRF54LM20B AI / NPU 预验证方案

> 本文件为**规划文档**（planning），聚焦整体结构、实现思路与参考资料，不深入到逐行代码。
> 正式 20B 板到位前，使用「换芯 20A 板」（20A 主板 + nRF54LM20B 芯片，下称 **20b 测试板**）先行验证 Axon NPU 与 Edge AI 流程。
> 20b 测试板的 PCB 电路与 XIAO nRF54LM20A **完全一致**，仅芯片从 nRF54LM20A 换成了 nRF54LM20B（多出 Axon NPU）。

---

## 1. 背景与目标

| 项 | 说明 |
|----|------|
| SoC | nRF54LM20B（含 **Axon NPU** @ `0x56000`，compatible `"nordic,axon"`） |
| 测试硬件 | XIAO nRF54LM20A 主板 + 芯片换为 nRF54LM20B（电路同 20A，板定义用 `seeed-xiao-nrf54lm20a`，overlay 补 NPU） |
| 正式硬件 | XIAO nRF54LM20B（板定义用 `seeed-xiao-nrf54lm20b`，后续到位） |
| 目标 | ① 验证 Axon NPU 可用；② 跑通 Edge AI Add-on 三类样例（分类/回归/异常检测）；③ 全流程纳入 PlatformIO 构建 |

nRF54LM20B 与 20A 的关键差异（影响本方案）：
- **NPU**：20B 独有 Axon NPU，20A 没有。
- **LED**：红/蓝互换（`gpio1.22/23/24`）。
- **PMIC I2C**：`P1.18/P1.17`（20A 为 `P1.15/P1.16`）。

---

## 2. 当前代码现状

### 2.1 板级定义（`zephyr/boards/arm/`）
- `xiao_nrf54lm20a/` — 20A 板定义（**测试板直接复用**，因电路相同）。
- `xiao_nrf54lm20b/` — 20B 正式板定义，**当前复用 20A 的 SoC include**：
  - `xiao_nrf54lm20b_nrf54lm20a_cpuapp.dts` → `#include "nrf54lm20a_cpuapp_common.dtsi"`
  - `nrf54lm20a_cpuapp_common.dtsi` → `#include <nordic/nrf54lm20a_enga_cpuapp.dtsi>`
  - 即：两套 SoC include 都**没有 Axon NPU 节点**，因为 20A 的 SoC DTSI 不含 NPU。
- NPU 硬件在 nRF54LM20B 芯片上实际存在（`0x56000`），但当前 device tree 未描述它，需用 overlay 补节点。

### 2.2 PlatformIO 集成（`builder/frameworks/zephyr.py`）
- 把本仓库 `zephyr/boards/arm/*` 符号链接进 `framework-zephyr/boards/arm/`。
- 通过 `_preinstall_west_deps()` 预装 `hal_nordic` 等 west 依赖。
- 构建时临时把 `$PIOPLATFORM` 切到 `nordicnrf52`，再调 Zephyr 的 `platformio-build.py`。

### 2.3 样例结构约定（`examples/<name>/`）
```
examples/<name>/
├── src/main.c
└── zephyr/
    ├── CMakeLists.txt          # set(BOARD_ROOT ...) + find_package(Zephyr)
    ├── prj.conf                # Kconfig
    └── boards/
        ├── xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay
        └── xiao_nrf54lm20b_nrf54lm20a_cpuapp.overlay
```

### 2.4 PlatformIO 板 JSON（`boards/`）
- `seeed-xiao-nrf54lm20a.json` — 20A 板，variant = `xiao_nrf54lm20a/nrf54lm20a/cpuapp`。
- `seeed-xiao-nrf54lm20b.json` — 20B 正式板，variant = `xiao_nrf54lm20b/nrf54lm20a/cpuapp`，上传协议：`nrfutil-mcumgr`。
- **新增** `seeed-xiao-nrf54lm20a-npu.json` — 20b 测试板，variant = `xiao_nrf54lm20a/nrf54lm20a/cpuapp_npu`（见任务一）。

---

## 3. 任务一：为 20A 板定义添加 NPU 节点（20b 测试板）

### 目的
20b 测试板 = XIAO nRF54LM20A 电路板 + nRF54LM20B 芯片。电路与 20A 完全一致（LED、PMIC、外设引脚等），因此**复用 20A 的设备树**即可正确描述所有外设。唯一差异是 nRF54LM20B 芯片多出 Axon NPU 硬件，需在板级定义中补上 NPU 节点。

### 方案选择：独立板型变体（方案 B）
将 NPU 节点固化进一个**新的板型变体**（board target），而非散落在各样例 overlay 里。理由：
- 与最终正式 20B 板结构**同构**（都是「板级 DTS 自带 NPU」），迁移时只需改 `board =`。
- 样例干净，无需每个样例重复 NPU overlay。
- 回滚简单：删除新增的板型文件即可，原 20A 板定义零影响。

### 两套板级定义
| 板 | PlatformIO board | Zephyr variant | 设备树来源 | NPU 来源 |
|----|-----------------|----------------|-----------|---------|
| 20b 测试板 | `seeed-xiao-nrf54lm20a-npu`（新增） | `xiao_nrf54lm20a/nrf54lm20a/cpuapp_npu`（新增板型） | 20A 设备树（电路匹配） | **板级 DTS 固化 NPU 节点** |
| 正式 20B 板 | `seeed-xiao-nrf54lm20b`（现有） | `xiao_nrf54lm20b/nrf54lm20a/cpuapp`（现有） | 20B 设备树（LED 互换、PMIC 引脚不同） | 后续切 `nrf54lm20b.dtsi` 或板级补 |

### 实现方式
在现有 `xiao_nrf54lm20a/` 板目录下新增一个板型变体（仿照已有的 `cpuapp` / `cpuflpr` 两个板型）：

```
zephyr/boards/arm/xiao_nrf54lm20a/
├── axon-npu.dtsi                                   # 新增：NPU 节点定义
├── xiao_nrf54lm20a_nrf54lm20a_cpuapp_npu.dts       # 新增：板型 DTS（= cpuapp + #include axon-npu.dtsi）
├── xiao_nrf54lm20a_nrf54lm20a_cpuapp_npu.yaml      # 新增：板型 YAML（从 cpuapp.yaml 复制改）
├── xiao_nrf54lm20a_nrf54lm20a_cpuapp_npu_defconfig # 新增：defconfig（从 cpuapp_defconfig 复制）
└── （其余现有文件不动）
```

**`axon-npu.dtsi`**（NPU 节点，字段待 SoC 手册/NCS 确认）：
```dts
/ {
    axon: axon@56000 {
        compatible = "nordic,axon";
        reg = <0x56000 SIZE_TO_BE_CONFIRMED>;
        interrupts = <INTERRUPT_TO_BE_CONFIRMED>;
        status = "okay";
    };
};
```

**`xiao_nrf54lm20a_nrf54lm20a_cpuapp_npu.dts`**（板型入口，内容≈现有 cpuapp.dts + NPU）：
```dts
/dts-v1/;
#include "nrf54lm20a_cpuapp_common.dtsi"
#include "seeed_xiao_connector.dtsi"
#include "axon-npu.dtsi"    /* 唯一新增 */

/ {
    compatible = "seeed,xiao_nrf54lm20a_nrf54lm20a-cpuapp-npu";
    model = "Seeed XIAO nRF54LM20A (20B chip) NPU test board";
    chosen {
        zephyr,code-partition = &slot0_partition;
        zephyr,sram = &cpuapp_sram;
        zephyr,boot-mode = &boot_mode_retention;
    };
};
```

**PlatformIO 板 JSON**（`boards/seeed-xiao-nrf54lm20a-npu.json`）：
- 从 `seeed-xiao-nrf54lm20a.json` 复制，改 `variant` 为 `xiao_nrf54lm20a/nrf54lm20a/cpuapp_npu`，`name` 标注为 NPU 测试板。

> **注意**：NPU 节点的 `reg`、`interrupts`、`clocks` 等字段最终以 NCS 上游 `nordic/nrf54lm20b.dtsi` 为准。若 NCS 已提供该文件，`axon-npu.dtsi` 可直接 `#include` 对应节点，无需手写字段。

### 提交策略
- **独立 commit**，message 例：`feat(boards): add xiao_nrf54lm20a cpuapp_npu variant with Axon NPU for 20b test board`。
- 回滚：`git revert <hash>`，原 `seeed-xiao-nrf54lm20a` / `cpuapp` 板型与非 AI 样例完全不受影响。

---

## 4. 任务二：PlatformIO 集成 Edge AI Add-on SDK

### SDK 信息
- 仓库：`https://github.com/nrfconnect/sdk-edge-ai`（最新 v2.0.0）
- 形态：**Zephyr module**（west manifest 方式集成），含 `samples/ lib/ drivers/ include/ tools/axon/compiler/ tests/axon/ applications/ axon_simulator/ zephyr/`。
- 依赖：NCS v3.3.0-preview2（含 `nrf54lm20b.dtsi` SoC 定义 + Axon NPU 支持）。
- 两类技术栈：
  - **Neuton**（<5KB tiny 模型，纯 CPU，任何 Nordic SoC 可用）。
  - **Axon NPU**（硬件加速，仅 20B；TFLite/LiteRT 模型经 Axon compiler 编译）。

### 核心原则：双 SDK 并存，不破坏现有板

当前 `framework-zephyr`（`~3.40201.251021`）是 20A / 15 等板的稳定依赖，**绝对不动**。20B 需要 NCS 3.3.0，通过**新增一个独立 PIO 包**实现并存。

| PIO 包 | 版本 | NCS 对应 | 服务板 |
|--------|------|---------|--------|
| `framework-zephyr`（现有） | `~3.40201.251021`（不动） | 当前稳定版 | 20A、15 等所有现有 nRF 板 |
| `framework-zephyr-ncs330`（**新增**） | NCS 3.3.0 对应版本 | v3.3.0-preview2 | 仅 20B 测试板 + 正式 20B 板 |

> **按需拉取**：两个包在 `platform.json` 中均为 `"optional": true`。PlatformIO 只在板子被选中时才下载对应包——构建 20A/15 的用户永远不会下载 NCS 3.3.0 包。
> **安全共存**：两个包在 `~/.platformio/packages/` 下各自独立目录，互不污染。

### 实现方式

**① `platform.json` 新增包声明**
```json
"framework-zephyr-ncs330": {
  "type": "framework",
  "optional": true,
  "version": "<NCS 3.3.0 对应的 PIO 包版本或 git URL>"
}
```

**② `platform_cfg/nrf_cfg.py` 按板切换 framework 包**
本仓库**已有此模式**（`nrf_cfg.py:30` 对 mbed 板做 `self.frameworks["arduino"]["package"] = "framework-arduino-mbed"`），直接套用：
```python
# 在 configure_nrf_default_packages() 的 board 判断分支内
if "nrf54lm20b" in board or "-npu" in board:
    self.frameworks["zephyr"]["package"] = "framework-zephyr-ncs330"
```
- 20A / 15 板：仍走 `framework-zephyr`，路径完全不变。
- 20B 测试板 / 正式板：走 `framework-zephyr-ncs330`。

**③ `builder/frameworks/zephyr.py` 适配多包**
当前 `get_package_dir("framework-zephyr")` 硬编码。改为按 env 实际选中的 framework 包取目录：
```python
fw_pkg = env.GetProjectOption("framework")  # 或从 board 推断
framework_dir = env.PioPlatform().get_package_dir(
    self.frameworks["zephyr"]["package"]  # 已被 nrf_cfg.py 切换
)
```
板定义符号链接、west 预装逻辑都基于这个 `framework_dir`，自动落到正确的包目录。

**④ sdk-edge-ai 作为 NCS 3.3.0 包内的 west module**
- 新包的 `west.yml` 追加 `sdk-edge-ai` project。
- `_preinstall_west_deps()` 放宽白名单允许 `sdk-edge-ai`，但**仅对新包目录执行**（通过判断 `framework_dir` 包名）。
- sdk-edge-ai 自带 `zephyr/module.yml`，自动注入 CMake/Kconfig。

**⑤ Axon compiler 工具链**
`tools/axon/compiler/` 为 PC 端工具；确认是 pip 包还是独立二进制，按需在 `platform.json` 声明为 optional 工具包。

### 影响文件
| 文件 | 改动 |
|------|------|
| `platform.json` | 新增 `framework-zephyr-ncs330` 包声明 |
| `platform_cfg/nrf_cfg.py` | 20B 板按板切 `frameworks["zephyr"]["package"]` |
| `builder/frameworks/zephyr.py` | `get_package_dir` 改为读实际选中的包；west 预装对新包执行 |
| 新包的 `west.yml` | 追加 sdk-edge-ai project |
| `boards/*.json` | 无需改（包切换由 builder 按板名决定） |

### 验证点
- 构建 20A 例程：`framework-zephyr/` 被使用，`framework-zephyr-ncs330/` 不被下载。
- 构建 20B 例程：`framework-zephyr-ncs330/` 被下载，`_pio/modules/` 含 `sdk-edge-ai`。
- 两个包目录并存于 `packages/`，无冲突。

---

## 5. 任务三：典型 AI 样例移植

### 上游样例分布（sdk-edge-ai）
- 路径：`samples/nrf_edgeai/`
- 三大类：
  1. **Classification（分类）** — 例如关键字唤醒 / 图像分类。
  2. **Regression（回归）** — 例如传感器数值预测。
  3. **Anomaly Detection（异常检测）** — 例如振动/电流异常。
- 另有 `applications/`（完整应用）、`tests/axon/`（NPU 单元测试）、`axon_simulator/`（PC 仿真，无需硬件）。

### 选型建议（每类挑 1 个最小可用样例）
| 类型 | 上游样例候选 | NPU 参与度 | 备注 |
|------|-------------|-----------|------|
| 分类 | `classification` 最小变体 | 高（Axon） | 验证 NPU 推理通路 |
| 回归 | `regression` 最小变体 | 中 | 验证连续值输出 |
| 异常检测 | `anomaly_detection` | 可 CPU/NPU 双跑 | 对比加速比 |
| NPU 自检 | `tests/axon/` 基础用例 | 纯 NPU | 最小冒烟测试 |

> 具体样例名以上游仓库 `samples/nrf_edgeai/` 实际目录为准；移植时优先选**不依赖外部大数据集**、**日志输出即可验证**的变体。

### 移植要点
- **CMakeLists.txt**：`target_link_libraries(app PRIVATE edge_ai ...)`，并参考本仓库 BOARD_ROOT 写法。
- **prj.conf**：打开 `CONFIG_NRF_EDGE_AI_*`、`CONFIG_AXON_*` 相关 Kconfig（以 module 暴露的符号为准）。
- **board overlay**：测试板 NPU 已固化在板型 DTS 中，样例 overlay **无需补 NPU**；正式板同理（NPU 由板定义或 SoC include 提供）。样例 overlay 仅处理样例特有需求（如 IMU 配置等）。
- **模型文件**：经 Axon compiler 编译后的 `.bin`/数组，放 `src/` 或 `data/`。
- **测试板 vs 正式板差异**：仅 `platformio.ini` 的 `board =` 不同，源码与 overlay 共用。
  - 测试板：`board = seeed-xiao-nrf54lm20a-npu`
  - 正式板：`board = seeed-xiao-nrf54lm20b`

---

## 6. 任务四：目录结构规划

### 6.1 样例目录（`examples/`）
```
examples/
├── xiao_nrf54lm20b/                 # 正式 20B 板（board = seeed-xiao-nrf54lm20b）
│   ├── edgeai-classification/
│   ├── edgeai-regression/
│   ├── edgeai-anomaly/
│   └── axon-smoke-test/
└── xiao_nrf54lm20b_test/            # 20b 测试板（board = seeed-xiao-nrf54lm20a-npu，先行验证）
    ├── edgeai-classification/
    ├── edgeai-regression/
    ├── edgeai-anomaly/
    └── axon-smoke-test/
```
- 两套目录**源码共用**（可通过相对路径 include 或 symlink `src/`），仅 `platformio.ini` 的 `board =` 不同：
  - 测试板：`board = seeed-xiao-nrf54lm20a-npu`（NPU 已固化在板型 DTS，无需 overlay）。
  - 正式板：`board = seeed-xiao-nrf54lm20b`。

### 6.2 板级定义目录（`zephyr/boards/arm/`）
- `xiao_nrf54lm20a/` 内新增板型变体 `cpuapp_npu`：
  - `axon-npu.dtsi` — NPU 硬件节点定义。
  - `xiao_nrf54lm20a_nrf54lm20a_cpuapp_npu.dts` — 板型 DTS（= 现有 cpuapp + include NPU）。
  - `xiao_nrf54lm20a_nrf54lm20a_cpuapp_npu.yaml` — 板型元数据。
  - `xiao_nrf54lm20a_nrf54lm20a_cpuapp_npu_defconfig` — 默认 Kconfig。
- `xiao_nrf54lm20b/` 维持现有定义；正式板到位后按需切 `nrf54lm20b.dtsi` SoC include。
- `boards/` 新增 `seeed-xiao-nrf54lm20a-npu.json`（从 `seeed-xiao-nrf54lm20a.json` 复制，改 variant 和 name）。

### 6.3 现有命名一致性
- 现有样例均为 `zephyr-<feature>` 扁平命名（如 `zephyr-imu`、`zephyr-ble`）。
- 本方案提议 `xiao_nrf54lm20b[_test]/` **子目录**形式，原因是：AI 样例与之前外设样例定位不同（专属于 20B/NPU），且需成对维护测试板/正式板两套，子目录更清晰。
- 若希望保持扁平风格，可改为：`edgeai-classification-20b-test`、`edgeai-classification-20b` 等命名，二选一在实施前定稿。

---

## 7. 风险与回滚策略

| 风险 | 缓解 |
|------|------|
| NPU 节点字段与实际 SoC 不符 | 优先用 NCS 上游 `nrf54lm20b.dtsi`；手写时以手册为准，先冒烟测试 |
| NCS 3.3.0 与现有 framework-zephyr 版本冲突 | **双包并存架构**：新增 `framework-zephyr-ncs330`，现有包不动；20A/15 走旧包，20B 走新包 |
| **工具链版本差异**（NCS 3.3.0 可能要求更新版 GCC） | 核对 `toolchain-gccarmnoneeabi` 版本兼容性：若两包需不同 GCC 版本，用 `optionalVersions` 或为新包单独声明工具链版本；**这是双包方案唯一需重点验证项** |
| PlatformIO west 预装白名单放开后影响其他 nRF 板 | 白名单仅对新包 `framework-zephyr-ncs330` 生效；旧包预装逻辑不变 |
| 20b 测试板用 20A 设备树但芯片是 20B，外设地址/中断可能有微小差异 | 20A/20B 同属 nRF54L 系列，外设地址空间一致；NPU 是唯一新增硬件块，板型 DTS 仅补 NPU |
| Axon compiler 工具链跨平台问题 | 先在 Linux 构建主机验证；WSL 下注意路径 |
| 新包体积大、下载慢 | `optional: true` 保证仅 20B 用户才下载；可提供国内镜像 URL |

**回滚**：任务一（NPU 节点）独立 commit，可单独 revert；任务二（SDK 集成）独立 commit；任务三每个样例独立 commit。互不耦合。

---

## 8. 参考资料

- Edge AI Add-on SDK: https://github.com/nrfconnect/sdk-edge-ai
- Nordic Edge AI 文档（NCS）: https://docs.nordicsemi.com/bundle/ncs-latest/page/nrfxlib/edge_ai.html
- Axon NPU / Edge AI 介绍: https://www.nordicsemi.com/Products/Development-software/Edge-AI
- nRF54LM20B 产品页（SoC 手册入口）: https://www.nordicsemi.com/Products/nRF54LM20B
- Zephyr module 机制: https://docs.zephyrproject.org/latest/develop/modules.html
- 本仓库板定义: `zephyr/boards/arm/xiao_nrf54lm20a/`（测试板用）、`zephyr/boards/arm/xiao_nrf54lm20b/`（正式板用）
- 本仓库构建集成: `builder/frameworks/zephyr.py`
- 本仓库样例模板: `examples/zephyr-imu/`

---

## 9. 实施顺序（建议）

1. **核对工具链兼容性** → 确认 NCS 3.3.0 所需 `toolchain-gccarmnoneeabi` 版本是否与现有 `~1.80201.0` 兼容；若不兼容，规划工具链隔离方案。
2. **任务二（框架先行）**：`platform.json` 新增 `framework-zephyr-ncs330` 包 + `nrf_cfg.py` 按板切包 + `zephyr.py` 适配多包 → 先用现有 zephyr-blink 样例在 20B 板上验证双包路由正确（旧包不被拉取、新包正常编译）→ 独立 commit。
3. **任务一**：新增 `cpuapp_npu` 板型变体（DTS+YAML+defconfig）+ `axon-npu.dtsi` + `seeed-xiao-nrf54lm20a-npu.json` → 冒烟测试（`axon-smoke-test`，board = `seeed-xiao-nrf54lm20a-npu`）→ 独立 commit。
4. **任务二续（Edge AI SDK）**：扩展新包的 west 预装逻辑，装 `sdk-edge-ai` → 跑通一个上游 AI 样例编译 → 独立 commit。
5. **任务三**：移植三类样例到 `examples/xiao_nrf54lm20b_test/`（board = `seeed-xiao-nrf54lm20a-npu`）→ 每样例独立 commit。
6. **任务四（固件交付）**：编译全部四个 AI 固件 → 整理到 `D:\workspace\xiao_nrf54lm20b\firmware\ai_auto\`（按第 11 节目录结构与命名）→ 编写 `README.md`（功能、烧录方法、预期现象）→ commit。
7. **正式 20B 板到位**：复制 `examples/xiao_nrf54lm20b_test/` → `examples/xiao_nrf54lm20b/`，`board =` 切到 `seeed-xiao-nrf54lm20b`，验证 → commit。

---

## 10. 项目管理细则

### 10.1 规范指导
- 开发全程使用 **superpower mcp** 作为规范指导，贯穿需求拆解、实现、验证、提交各环节。

### 10.2 提交与仓库
- **仓库**：`https://github.com/cumin777/platform-seeedboards`（remote 名 `dev1`）
- **分支**：`xiao_nrf54lm20b_dev`（当前开发分支，勿切其他分支）
- **提交节奏**：每完成一部分**通过验证**的修改，立即提交并推送到上述仓库分支。
  - 验证标准：对应改动能通过编译（`pio run`）；板级/硬件类改动需在硬件上冒烟测试通过。
  - 提交粒度：按本文档第 9 节的步骤，每步一个独立 commit，message 遵循现有 Conventional Commits 风格（`feat(...)` / `fix(...)`）。
  - 推送命令：`git push dev1 xiao_nrf54lm20b_dev`。
- **不批量提交**：禁止积累多个步骤再一次性提交；每步验证通过即提交。

### 10.3 持续推进原则
- **不停顿**：在本阶段开发目标全部完成之前，持续推进开发，不主动暂停或等待指令。
- **仅以下情况中断并询问**：
  - 需求存在歧义、有多种合理理解时。
  - 方案选型影响范围大、需用户拍板时（如工具链版本隔离方案等架构决策）。
  - 遇到阻塞型错误且无法自主判定修复方向时。
- 除上述情况外，按第 9 节顺序逐项实现、验证、提交，直至本阶段目标（20B 测试板三类 AI 样例全部跑通）完成。

---

## 11. 固件交付物

### 11.1 目标
完成测试板（board = `seeed-xiao-nrf54lm20a-npu`）所有 AI 样例固件的编译，整理为可直接烧录使用的固件包，并配套使用说明文档。

### 11.2 交付位置
```
D:\workspace\xiao_nrf54lm20b\firmware\ai_auto\
```

### 11.3 固件包目录结构
```
ai_auto/
├── README.md                          # 固件使用说明（见 11.5）
├── axon-smoke-test/
│   └── axon-smoke-test.bin            # NPU 冒烟测试固件
├── edgeai-classification/
│   └── edgeai-classification.bin      # 分类样例固件
├── edgeai-regression/
│   └── edgeai-regression.bin          # 回归样例固件
└── edgeai-anomaly/
    └── edgeai-anomaly.bin             # 异常检测样例固件
```
- **命名规则**：目录名 = sample 名；固件文件名 = `{sample名}.bin`。
- 每个固件同时保留 `.bin`（用于 MCUboot SMP 上传）和 `.elf`（用于调试器烧录），放在对应子目录内。
- 固件来源：`examples/xiao_nrf54lm20b_test/<sample>/.pio/build/seeed-xiao-nrf54lm20a-npu/firmware.{bin,elf}`。

### 11.4 固件与 sample 对应表
| 固件文件 | 对应 sample 目录 | 源自上游 | 功能 |
|---------|-----------------|---------|------|
| `axon-smoke-test.bin` | `examples/xiao_nrf54lm20b_test/axon-smoke-test/` | `tests/axon/` | NPU 硬件自检，验证 Axon 寄存器读写与基础推理通路 |
| `edgeai-classification.bin` | `examples/xiao_nrf54lm20b_test/edgeai-classification/` | `samples/nrf_edgeai/classification` | 分类推理（如关键字/传感器类别识别） |
| `edgeai-regression.bin` | `examples/xiao_nrf54lm20b_test/edgeai-regression/` | `samples/nrf_edgeai/regression` | 回归预测（连续数值输出） |
| `edgeai-anomaly.bin` | `examples/xiao_nrf54lm20b_test/edgeai-anomaly/` | `samples/nrf_edgeai/anomaly_detection` | 异常检测 |

### 11.5 README.md 内容要求
`ai_auto/README.md` 需包含每个固件的：
1. **固件功能**：该固件做什么、验证什么。
2. **烧录方法**：
   - 串口方式（MCUboot SMP）：`mcumgr image upload ...` / PlatformIO `pio run -t upload`。
   - 调试器方式（CMSIS-DAP / J-Link）：刷 `.elf`。
3. **预期现象**：烧录后串口日志应输出什么、LED 行为等可观测结果。
4. **串口配置**：波特率 115200，引脚为 UART20。

### 11.6 交付验收标准
- 四个固件全部编译通过（无报错）。
- `axon-smoke-test` 在测试板上实际运行，串口能看到 NPU 初始化成功 / 推理结果。
- README.md 内容完整，用户照文档可独立完成烧录与验证。
- 固件包目录结构符合 11.3 规范。
