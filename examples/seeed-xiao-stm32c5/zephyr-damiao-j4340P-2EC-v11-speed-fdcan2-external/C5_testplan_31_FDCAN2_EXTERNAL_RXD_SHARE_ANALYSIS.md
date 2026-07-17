# FDCAN2 外置收发器 RXD 不能直接共用：测试结论与原理

## 结论

XIAO 的 FDCAN2（PB5/PB13）已连接板载 CAN 收发器。若再把外置收发器的
`RXD` 直接接到 PB5，就会让两个收发器的 `RXD` 输出并联。这不是受支持的
硬件连接方式；板载收发器即使进入 standby，也不能保证其 `RXD` 为高阻。


## 已验证的测试结果

| 结果 | 结论 |
| --- | --- |
| PB5 始终为低或不稳定 | 板载收发器 RXD 或板级 PB5 被拉低；FDCAN2 不能与外置收发器直接共用 RXD。 |
| 拔掉外置 RXD 后 PB5 变高 | PB5 的低电平来自外置收发器或其 CANH/CANL 总线支路；应检查外置模块供电、EN/STB、CANH/L 短路或接反。 |

第二项尤为关键：拔线后 PB5 恢复高电平，说明 MCU 的 PB5 和 FDCAN2 pinmux
本身没有把输入固定为低；低电平是通过外置 RXD 支路传入的。

## 软件和总线证据

- FDCAN1 使用外置收发器和同一 CAN 总线可以正常通信。
- FDCAN2 已从软件设备树移除板载 `phys` 绑定，PB14 也保持为 standby 高电平，
  但仍持续 Bus-Off。
- FDCAN2 的 M_CAN 状态为 `LEC=5 (bit0-error)`：控制器发送 recessive（1）时，
  PB5/RXD 却检测到 dominant（0）。
- 外置收发器 RXD 接到 PB5 时，PB5 为低或不稳定；拔掉该 RXD 后，PB5 恢复为高。

这些事实排除了电机协议和 FDCAN 软件配置问题，故障位于 FDCAN2 的 RXD 电气链路。

## 为什么不能并联 RXD

CANH/CANL 是总线，可并联；`RXD` 是收发器到 MCU 的单端数字**输出**，不可并联。

```text
板载收发器 RXD ─┐
                 ├── PB5 / FDCAN2_RX
外置收发器 RXD ─┘
```

CAN 控制器使用 recessive=高电平、dominant=低电平的 RXD 信号监测总线状态。
正常空闲总线时，RXD 必须为高。外置收发器接入后 PB5 变低或不稳定，说明
FDCAN2 看到的是 dominant 或不确定电平；这正好对应实测的
`LEC=5 (bit0-error)`：控制器发送 recessive（1）时却读到 dominant（0），最终
累计发送错误并进入 Bus-Off。

仅修改固件或拉高 PB14，不能消除两个 RXD 输出的物理并联关系。
