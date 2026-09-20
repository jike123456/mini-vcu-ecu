# Phase 6 Bootloader 跳转实板测试报告

测试日期：2026-09-11  
目标板：STM32F407ZGY6  
下载接口：DAPLink CMSIS-DAP/SWD  
诊断链路：DAPLink COM9，115200 bit/s

## 构建结果

- Bootloader：链接地址 `0x08000000`，构建通过。
- Application：链接地址 `0x08020000`，0 Error、0 Warning。
- Application 大小：75168 字节。
- Application CRC32/IEEE：`0x1EA6D1D2`。
- Reset Handler：`0x0802025D`，带 Thumb 位且位于镜像范围内。
- 主机侧镜像与协议测试：13/13 通过。

## 工厂烧录

执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_factory.ps1
```

结果：

```text
Erase Done.
Programming Done.
Verify OK.
Application running ...
Factory SWD flash passed; application debug AXF restored.
```

工厂映像包含 Bootloader、Application 和 Manifest，不包含 Sector 11 DTC NvM。
DAPLink 大容量存储拖拽方式曾在高地址写校验时报错，因此正式流程使用 Keil
CMSIS-DAP/SWD 下载算法。

## Bootloader 跳转证据

UDS 自动回归结果：

```text
0x22 F195 SOFTWARE VERSION: PASS
0x22 F102 VTOR 0x08020000: PASS
0x31 ROUTINE 0xFF00: NRC 0x78 -> FINAL (0.265s): PASS
0x22 F101 UPTIME BEFORE RESET: 34936 ms
0x11 POSITIVE RESPONSE BEFORE RESET: PASS
0x11 HARD RESET/UPTIME 34936->75 ms: PASS
UDS PHASE5 REGRESSION PASS
```

F102 证明当前中断向量表来自重定位 Application。`0x11` 之后运行时间归零且诊断
重新上线，证明 CPU 先回到 `0x08000000` Bootloader，完成 Manifest、向量表和应用
CRC32 校验，再次设置 MSP/VTOR 并跳转至 Application。

## 回归

- 五类故障注入、安全状态切换、DTC 去抖/恢复、受限清除和 NvM settle：通过。
- Boot 镜像与原 CAN/UART 协议单元测试：13/13 通过。

## 结论

Phase 6 的 Flash 分区、独立构建、应用 CRC32、Manifest 提交模型和向量表跳转已
完成实板验证。下一步实现 Bootloader 内的诊断下载服务 `0x27/0x31/0x34/0x36/0x37`。
