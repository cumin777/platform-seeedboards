# 模型记录卡

| 字段 | 唤醒词模型（WW） | 关键词模型（KWS） |
|---|---|---|
| 模型名称 / 版本 | Hello Seeed / solution 95647 | seeed key word / solution 95649 |
| Edge AI Lab 项目 / 导出日期 | `Hello_Seeed_95647_wake_word.zip` / 2026-08-25 | `seeed_key word_95649_kws.zip` / 2026-08-25 |
| 目标 | nRF54LM20B Axon NPU | nRF54LM20B Axon NPU |
| 输入规格 | 16 kHz、单声道、16-bit；160 samples（10 ms） | 16 kHz、单声道、16-bit；160 samples（10 ms） |
| 标签及输出索引 | 0: `hello seeed` | 0: `OTHER`；1: `SILENCE`；2: `no`；3: `ok`；4: `opus`；5: `stop`；6: `yes` |
| 训练/验证/测试数据划分 | 待填写 | 待填写 |
| Axon interlayer buffer | 6048 bytes | 6656 bytes |
| Axon psum buffer | 0 bytes | 0 bytes |
| 板端阈值 | 待填写 | 待填写 |
| 现场测试结果 | 待填写 | 待填写 |
