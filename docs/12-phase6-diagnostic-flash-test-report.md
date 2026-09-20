# Phase 6 诊断刷写实板测试报告

## 测试对象

- MCU：STM32F407ZGY6
- 调试/通信：DAPLink CMSIS-DAP + COM9，USART1 115200 8N1
- 诊断承载：UART 外层协议中的虚拟 Classical CAN，Request `0x7E0`、Response `0x7E8`
- 应用包：75284 B，版本 `0.6.0.0`，CRC32/IEEE `0x8DB3C74B`
- 测试日期：2026-09-11

## 工厂更新

新 Bootloader 首次通过 `tools/flash_factory.ps1` 写入。下载日志结果：

```text
Erase Done.
Programming Done.
Verify OK.
Application running ...
Factory SWD flash passed; application debug AXF restored.
```

这是最后一次依赖 SWD 的 Bootloader 更新；后续 Application 可走诊断链路更新。

## 无擦写安全探测

执行 `bootloader_tester.py --port COM9 --probe`，结果：

```text
Vehicle STANDBY: PASS
Application programming session: PASS  50 02 00 32 01 F4
Programming reset accepted: PASS  51 01
Bootloader programming session: PASS  50 02 00 32 01 F4
Security seed: PASS  67 01 6F 5C 10 69
Security unlock: PASS  67 02
BOOTLOADER PROBE PASS (Flash was not erased)
```

## 完整下载

探测后在 Bootloader 内执行完整 `.kya` 下载：

| 步骤 | 请求/行为 | 实板结果 |
|---|---|---|
| 编程会话 | `10 02` | `50 02 ...`，PASS |
| 安全访问 | `27 01` / `27 02` | Seed/Key 解锁，PASS |
| 擦除 | `31 01 FF01` | 先 `7F 31 78`，后 `71 01 FF 01 00`，PASS |
| 请求下载 | `34 00 44 + address + length` | 最大块长 258 B，PASS |
| 数据传输 | `36 + BSC + 256 B` | 75284 B 全部确认，BSC 回卷正常，PASS |
| 退出传输 | `37 + CRC32 + version` | `77`，CRC 与 Manifest 提交 PASS |
| 自动复位 | Bootloader 再校验并跳转 | PASS |

总传输时间为 24.8 s。Manifest 在数据写完且整镜像 CRC 正确后才写入，因此中途
断电不会把半成品标为有效应用。

## 升级后回归

升级自动复位后执行完整 Phase 5 测试：VIN、软件版本、运行数据、S3 超时、NRC、
异步例程和 ECU Reset 全部通过。关键证据：

```text
0x22 F195 SOFTWARE VERSION: PASS
0x22 F102 VTOR 0x08020000: PASS
0x31 ROUTINE 0xFF00: NRC 0x78 -> FINAL: PASS
0x11 HARD RESET/UPTIME 26597->75 ms: PASS
UDS PHASE5 REGRESSION PASS
```

## 中断和错误恢复回归

在完整镜像写入 4096 B 后关闭串口并给 ECU 断电。此时 Application 已被擦除且
Manifest 尚未写入。重新上电后 Bootloader 没有跳入残缺应用，并可继续响应
`0x10/0x27`。详细结果：

```text
Wrong security key rejected: PASS  7F 27 35
Wrong block sequence rejected: PASS  7F 36 73
INTERRUPTION POINT REACHED: 4096 bytes written, Manifest absent
Interrupted image stayed in Bootloader: PASS  7F 34 24
Wrong image CRC rejected: PASS  7F 37 72
PROGRAMMING PASS: 75284 bytes in 24.8s
INTERRUPTION/CRC RECOVERY PASS
```

完整恢复刷写后再次执行 Phase 5 回归，`F102=0x08020000`，硬复位后运行时间从
22216 ms 回落到 76 ms，全部通过。

## 结论

正常刷写、错误密钥、错误块序号、错误 CRC、下载中断、重新上电留在 Bootloader
和完整恢复刷写均已有可重复实板结果。Phase 6 退出条件满足。
