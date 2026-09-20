# UDS 诊断子集

当前诊断链路使用虚拟 CAN，物理地址为 `0x7E0` 请求、`0x7E8` 应答。底层通过 DAPLink COM9 承载 CAN 帧，UDS 数据本身使用 ISO-TP 格式。切换到真实 CAN 时可保留诊断服务层。

## 已实现

- ISO-TP Single Frame 请求和应答。
- ISO-TP First Frame / Flow Control / Consecutive Frame 多帧应答。
- 1 s Flow Control 等待超时。
- `0x19 0x02 reportDTCByStatusMask`，一次返回所有匹配 DTC。
- `0x14 FFFFFF ClearDiagnosticInformation`。
- `0x10 0x01/0x03` 默认会话和扩展会话，正响应携带 P2=50 ms、P2*=5000 ms。
- `0x11 0x01` Hard Reset：仅扩展会话且非 DRIVE 状态允许，先发送正响应再延迟复位。
- `0x3E 0x00` Tester Present，支持 Bit7 `suppressPosRspMsgIndicationBit`。
- 扩展会话 S3 超时为 5 s，超时自动返回默认会话。
- `0x22 F190` VIN：`KAOYA-F407-ECU001`。
- `0x22 F195` 软件版本：`VCU-0.5.0`。
- `0x22 F100` 项目自定义运行数据：VCU 状态、故障位、电压、DTC 数量、故障注入掩码和当前会话。
- `0x22 F101` 项目自定义 ECU 运行时间，单位为 ms，用于自动验证软件复位。
- `0x22 F102` 当前向量表地址；Bootloader 阶段实测必须为 `0x08020000`。
- `0x31 0x01 FF00` 项目自定义异步例程：先返回 NRC `0x78 ResponsePending`，约 250 ms 后发送最终正响应。
- DRIVE 状态禁止清除，返回 NRC `0x22 conditionsNotCorrect`。
- 不支持的服务、子功能、长度和 DTC group 分别返回 NRC。

## 自动回归

先保留 DAPLink，拔掉会干扰 SWD 的 USB-TTL，然后执行：

```powershell
python .\tools\uds_tester.py --port COM9
python .\tools\uds_phase5_tester.py --port COM9
```

测试顺序为：

1. 周期发送 `0x100`，进入 DRIVE。
2. 用 `0x19 0x02` 读取已保存 DTC，自动处理多帧应答。
3. DRIVE 中请求 `0x14`，验证 NRC `0x22`。
4. 通过 ignition/gear=0 进入 STANDBY，再次清除并验证 `0x54`。
5. 再读一次 DTC，应为空；继续等待 5.5 s，让 NvM 将清除结果追加到 Flash。

Phase 5 测试器还会依次验证扩展会话、P2/P2*、VIN 多帧传输、Tester Present、
S3 超时、非法请求 NRC、异步例程的 `0x78` 与最终响应。最后读取 F101，执行
`0x11 0x01`，等待 ECU 启动并再次读取 F101；运行时间回落且 F100 会话恢复为
默认值才判定复位通过。

复位或重新烧录应用后，可用下列只读命令验证清除结果仍然保持：

```powershell
python .\tools\uds_tester.py --port COM9 --read-only --expect-empty
```

## 边界

当前不是完整量产 UDS 栈。Block Size 和 STmin 目前按测试器发送的 `0` 处理；
`0x78 responsePending` 只在项目自定义例程 `0x31 FF00` 上实现。尚未实现
SecurityAccess、安全刷写服务及完整 ISO 14229 状态矩阵。
