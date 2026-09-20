# DTC 故障管理 V0.1

## 已实现 DTC

| 内部 ID | 3 字节 DTC | 监控条件 | 当前故障反应 |
|---|---:|---|---|
| COMMAND_TIMEOUT | `C00100` | 最后有效指令要求 DRIVE，且 `0x100` 超过 100 ms 未刷新 | SAFE_STOP，禁止 PWM |
| COMMAND_CRC | `C00200` | CAN 应用层 CRC8 错误 | 拒绝报文，超时后 SAFE_STOP |
| COMMAND_COUNTER | `C00300` | Alive Counter 不连续 | 拒绝报文，重新同步计数器 |
| COMMAND_VALUE | `C00400` | Ignition/Gear/保留位非法 | 拒绝报文 |
| BATTERY_UNDERVOLTAGE | `C00500` | 欠压检测/软件注入 | SAFE_STOP，禁止 PWM |
| ENCODER_PLAUSIBILITY | `C00600` | 编码器不可信/软件注入 | SAFE_STOP，禁止 PWM |
| IMU_COMMUNICATION | `C00700` | IMU 初始化或运行通信失败 | 报警，底盘可继续运行 |
| TASK_MONITOR | `C00800` | 任务监控故障/软件注入 | FAULT，禁止 PWM |
| CAN_CONTROLLER | `C00900` | CAN 未初始化或连续 3 次自检失败 | SAFE_STOP，禁止 PWM |

DTC 编码为学习项目内部定义，不声称是任何整车厂的量产故障码。
最后有效指令为 ignition/gear=0 时，节点进入 STANDBY；此后报文停止视为正常下线，不重新生成 `C00100`。

## 去抖与状态

- 连续 3 次失败：置 `pendingDTC + confirmedDTC`。
- 连续 10 次通过：清除当前 `testFailed` 和 `pendingDTC`。
- `confirmedDTC` 保留到执行明确清除，供后续 UDS `0x19/0x14` 复用。
- `0x180 MotionFaultFlags.Bit1` 表示当前至少有一个已确认且仍活动的 DTC。
- `0x180 MotionFaultFlags.Bit2` 表示存在已确认的当前或历史 DTC，恢复正常或重启后仍保留。

状态位采用 ISO 14229 的位定义：Bit0 `testFailed`、Bit1
`testFailedThisOperationCycle`、Bit2 `pendingDTC`、Bit3 `confirmedDTC`、Bit5
`testFailedSinceLastClear`。

## Freeze Frame

DTC 首次进入 confirmed 时在 RAM 中保存：

- 时间戳
- VCU 状态
- 目标纵向速度和横摆角速度
- 左右轮实际速度

Freeze Frame 由 NvM 服务持久化到 STM32 Flash。

## NvM 存储策略

- 保留 STM32F407ZG 1 MB Flash 的 Sector 11：`0x080E0000..0x080FFFFF`。
- 链接器中的应用区截止到 `0x080DFFFF`，防止代码增长覆盖 NvM。
- NvM 格式 V2 每个快照 192 字节，容纳 9 个 DTC，使用递增 Sequence、CRC32/IEEE 和最后写入的 Commit Word。
- 只追加写入，不原地改写；掉电时未提交的记录会被忽略。
- 最快 5 秒写一次，并且只允许在非 DRIVE 状态写入。
- Sector 可容纳 682 个快照。V0.2 在写满时拒绝继续写，不在运行中冒险擦除整个大扇区。

上电日志会显示：

```text
[INFO][NVM] DTC init restored=1 seq=... confirmed=... slots=... crc_err=0
```

## 软件故障注入

一键验证欠压、编码器、IMU、任务和 CAN 五类故障的状态联动、UDS 多帧读取、恢复去抖和条件清除：

```powershell
python .\tools\fault_injection_tester.py --port COM9
```

通过判据为最后输出 `FAULT INJECTION REGRESSION PASS`。

### 命令链路 CRC 故障

先正常驱动 2 秒，随后连续注入 1 秒错误 CRC，然后恢复正常报文：

```powershell
python .\tools\virtual_vcu.py --port COM9 --duration 6 --drop-after 0 `
  --bad-crc-from 2 --bad-crc-duration 1 --expect-dtc-cycle
```

预期过程：

```text
DRIVE fault=0x00
SAFE_STOP fault=0x07
DRIVE fault=0x06
DRIVE fault=0x04
DTC-CYCLE PASS
```

`0x07` 由 Bit0 命令超时、Bit1 活动 DTC 和 Bit2 历史 DTC 组成。恢复报文后先返回 DRIVE，
再经过 10 次通过去抖后清除 Bit1；Bit2 保留到 UDS `0x14 FFFFFF` 在非 DRIVE 状态下明确清除。
