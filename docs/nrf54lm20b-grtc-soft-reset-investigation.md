# nRF54LM20B 软复位后系统时钟瘫痪 —— 调查与修复记录

分支：`fix/nrf54lm20b-grtc-soft-reset`（基于 origin/dev @ 3b34334）
日期：2026-10-08 ~ 2026-10-12
状态：**调查结案留档，修复未闭环**（E2E 未绿，详见"验证状态"）

## 1. 问题

`pio run -t upload -e seeed-xiao-nrf54lm20b` 完成后（RESETCMD 发出 `mcu-manager
serial reset`），板子全黑：LED 不闪、USB CDC 不枚举，需手动按 RESET 或断电才能
恢复。pin 复位 / 冷启动一切正常。即：**任何软复位（NVIC_SystemReset）之后，
app 镜像无法正常启动**。

## 2. 调查方法

- 分层 LED 探针固件（`examples/seeed-xiao-nrf54lm20b/grtc-probe/`，未提交）：
  PRE_KERNEL_1 标记 / busy_wait 线程 / k_msleep 主线程，把"死"分成三层
- JLink（`nrfutil device halt / cpu-register-read / read|write --direct`）
  在挂死态直接读 CPU 寄存器、驱动 RAM 变量（符号地址来自 ELF）、GRTC 寄存器
- 上游比对：framework-zephyr-nrf54lm20 = 上游 Zephyr 4.4.0 原版（已逐字节
  核对），与 zephyr main / NCS main / nrfx master diff

GRTC 关键地址（S 别名，基址 0x500E2000）：
INTEN0-3 @ +0x300 起（每组 0x10：INTEN/SET/CLR/PEND）、EVENTS_COMPARE[n]
@ +0x100+4n、CC[n] @ +0x520+0x10n（CCL/CCH/CCADD/CCEN）、SYSCOUNTER @ +0x720。
**注意 JLink 直读与 CPU 读数存在双视角差异**（SYSCOUNTER 高位尤其明显，
JLink 读到标志位/未同步值），后期数据解读受此干扰。

## 3. 根因层（按发现顺序，每层有独立取证）

软复位保留 GRTC 域（zephyr#89147：uptime 不归零），由此衍生多层"基撕裂"：

1. **tickless init 不布防首次比较**（上游已修，main-only commit 1afedec05eaf）：
   4.4.0 tickless 下等内核调 `sys_clock_set_timeout()`，soon-list 空则永不调，
   announce 永不启动，`k_sleep` 全卡死。
2. **`-EALREADY` 通道分配失败**：软复位后 SYSCOUNTER 残留运行，
   `nrfx_grtc_syscounter_start()` 返回 -EALREADY，驱动 init 直接 abort，
   INTEN 永不开（取证：挂死态 INTEN 全 0、channel_data 未赋值）。
3. **`sys_clock_disable()` 顺序**（上游 aad2c65d69e）：uninit 应在 LFCLK
   release 之后（防御性）。
4. **`cc_value` 软件基撕裂**：上游 #91432（已在 4.4.0）只初始化了
   `last_count`，静态 `cc_value` 仍从 0 起，而 tickless fast path 的
   `cc_value += cyc` 假设它以启动 counter 为基。取证：布防偏差恰为
   2^32-32 cycle，CC 落远未来。
5. **handler 的 cc_val 基撕裂**：CCADD 相对布防时 nrfx 从内部表（0 基）报
   cc_val，与驱动绝对基 last_count 做 `counter_sub()` 下溢（取证：
   expired_cc=0xD2xx 小值 vs cc_value=0x10_0000xxxx）。
6. **（推测）H 域时序**：sys_clock_driver_init 跑在 EARLY 级，LFCLK 喂的
   SYSCOUNTER 高 32 位域软复位后晚期才恢复，init 时读到 0、运行期读到累计
   值。**此层无直接证据，0006 为推测性修复**。

另排查并排除：INTEN 中断组错位（DT IRQ=228=GRTC_2 与驱动 S-APP 宏组 2 配套）、
mcuboot/loader 侧问题（板上 loader 已是 no-STOP 修复版）、上游 soc init 顺序
（NCS 4078a1b18ef 已在 4.4.0）。

## 4. 补丁清单（zephyr/patches/xiao_nrf54lm20b/ + fixes.yml）

| # | id | 来源 | 内容 | 可信度 |
|---|----|------|------|--------|
| 0001 | grtc-arm-first-compare | 上游 1afedec 背移植 + 本地绝对化 | init 无条件布防首比较，且用**绝对**写入（`set_abs(last_count+CYC_PER_TICK)`，覆盖 CC 残留） | 高 |
| 0002 | grtc-warm-reset-syscounter-ealready | 本地自研 | start() 返回 -EALREADY 时改为 channel_alloc 继续 | 高 |
| 0003 | grtc-release-lfclk-before-uninit | 上游 aad2c65d 背移植（适配 4.4.0） | sys_clock_disable 顺序对调 | 防御性 |
| 0004 | grtc-seed-cc-value | 本地自研 | init 时 `cc_value = last_count` 种子对齐 | 中高 |
| 0005 | grtc-hw-cc-in-handler | 本地自研 | timeout handler 改读硬件 CC 寄存器（绝对基） | 中 |
| 0006 | grtc-reanchor-timebase | 本地自研（推测性） | grtc_post_init（LFCLK 稳定后）重锚 last_count/cc_value 并重布防 | 低 |

工程教训：fixes.yml 补丁链的 already-applied 检测对"后续补丁修改同一区域"不
鲁棒——0006v1 曾因此破坏幂等，已把绝对布防并入 0001（同区域单一补丁）。

## 5. 验证状态（诚实记录）

- 补丁应用机制：6 补丁 fresh 应用 + 幂等重跑均 ✓；产物（signed.bin）反汇编
  确认补丁代码编入固件 ✓；冷启动（pin 复位）正常 ✓
- **软复位链 E2E：始终未绿**。每加一层修复后复测仍全黑；第 4 层后 JLink 读数
  开始出现 CPU/JLink 双视角矛盾，解读可信度下降
- 已验证的旁证：JLink 手动写 INTEN0SET + CCADD 可让 tick handler 执行一次
  （expired_cc/last_count 更新），说明硬件中断路径本身健康

## 6. 本机环境（本分支提交后已全部还原）

- framework-zephyr-nrf54lm20 包的 nrf_grtc_timer.c 曾打 6 补丁（原版备份
  /tmp/grtc/pkg_backup.c）→ **已还原原版**
- 两个平台安装副本（~/.platformio/platforms/SeeedStudio 主副本与
  @src-91a680ea 快照——构建实际命中后者）的 fixes.yml/patches 曾同步 →
  **已还原**（各自 git checkout + 删除 xiao_nrf54lm20b 补丁目录）
- 板子最后固件为 6 补丁版 blink，处于挂死态，按 RESET 即恢复

## 7. 遗留与建议方向

1. **转 Nordic DevZone**：携带本文取证数据提问——"nRF54L + Zephyr 4.4 GRTC
   软复位后系统时钟瘫痪"，重点问 SYSCOUNTER 高 32 位域软复位后的同步时序，
   以及 CPU/JLink 双视角的正确读法。上游 main 至今带病（0004/0005 无对应
   修复），值得报给上游。
2. **loader 侧 A/B**：恢复产线 loader 的 `STOP_AT_UNINIT=y`（loader 复位前
   停 SYSCOUNTER → app 冷等效启动）。第一轮调查曾错误排除该路径（用了不经
   loader 的 `nrfutil device reset` 做对照）。原厂 hex 与烧录脚本在
   factory_flash 相关目录。
3. 0001-0005 是有独立取证的真实缺陷修复，后续任何方案都建议保留；0006 待证。
