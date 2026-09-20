# Phase 5 实板测试报告

测试日期：2026-09-10  
目标板：STM32F407ZGY6  
调试与通信：DAPLink，虚拟串口 COM9，115200 bit/s  
前置条件：USB-TTL 已拔除，避免干扰 SWD；VCU 保持低压台架运行。

## 构建与烧录

- Keil 全量重建：0 Error、0 Warning。
- Flash：Erase Done、Programming Done、Verify OK、Application running。

## UDS Phase 5 回归

执行命令：

```powershell
python .\tools\uds_phase5_tester.py --port COM9
```

实测结果：

```text
0x10 EXTENDED SESSION/P2/P2*: PASS
0x3E TESTER PRESENT: PASS
0x22 F190 VIN MULTIFRAME: PASS
0x22 F195 SOFTWARE VERSION: PASS
0x22 F100 RUNTIME DATA: PASS
NRC 0x31/0x12: PASS
0x3E SUPPRESS RESPONSE/S3 REFRESH: PASS
S3 5s TIMEOUT -> DEFAULT SESSION: PASS
0x11 RESET IN DEFAULT -> NRC 0x7E: PASS
0x31 ROUTINE 0xFF00: NRC 0x78 -> FINAL (0.266s): PASS
0x11 HARD RESET/UPTIME 23696->61 ms: PASS
UDS PHASE5 REGRESSION PASS
```

其中复位测试不是只检查响应字节：测试器在复位前后读取 DID F101。运行时间由
23696 ms 回落到 61 ms，同时 DID F100 中的诊断会话恢复为默认会话，因此可以
确认 MCU 确实完成了软件复位。

## 安全与故障回归

```text
BASELINE DRIVE: PASS
INJECT ALL -> FAULT: PASS
INJECTED DTC: C00500/0x2F, C00600/0x2F, C00700/0x2F,
              C00800/0x2F, C00900/0x2F
REMOVE INJECTION -> DRIVE: PASS
DTC DEBOUNCE/HEAL: PASS
CLEAR while DRIVE: NRC 0x22 PASS
CLEAR in STANDBY/NVM settle: PASS
FAULT INJECTION REGRESSION PASS
```

## 主机协议单元测试

CRC 参考向量、DBC 命令帧、状态帧 CRC 拒绝和 UART 外层 CRC 拒绝共 4 项，
全部通过。

## 结论

Phase 5 退出条件满足：自动化测试已覆盖正常响应、否定响应、多帧、会话超时、
ResponsePending、最终响应和 ECU Reset，且 Phase 4 的安全故障链路无回归。

