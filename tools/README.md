# PC Tools

计划使用 Python 构建：

- CAN 报文收发与 DBC 解码
- UDS 诊断测试器
- CAN Bootloader 刷写器
- 故障注入与 pytest 自动化回归
- 测试结果和时序统计报告

首选依赖为 `python-can`、`cantools` 和 `pytest`，具体版本将在 Phase 2 锁定。

## 已有工具

- `build_firmware.ps1`：调用 Keil UV4 全量重编译固件，并以 0 Error、0 Warning 作为成功条件。
- `run_static_analysis.ps1`：使用 ARM GNU GCC `-fanalyzer` 和严格告警扫描 34 个自研源文件；任何告警、错误或禁用函数命中均返回失败。
- `run_host_coverage.py`：MinGW GCC/gcov 编译执行五个真实固件 C 模块；生成 `docs/coverage/latest.md/json` 和逐行 `.gcov`，以总体行 90%、分支 80% 为门槛；不需要连接开发板。
- `flash_firmware.ps1`：供工厂烧录脚本调用的底层 Keil CMSIS-DAP 下载入口；禁止单独下载没有匹配 Manifest 的重定位 Application。
- `build_bootloader.ps1`：使用 ARMCC 独立构建地址为 `0x08000000` 的 Bootloader。
- `boot_image.py`：校验重定位向量表，生成带双 CRC32 的 `.kya` 包和稀疏工厂 HEX。
- `flash_factory.ps1`：构建 Bootloader/Application/Manifest 组合 AXF，经 Keil CMSIS-DAP 首次烧录，并自动恢复 Application 调试 AXF。
- `bootloader_tester.py`：通过 DAPLink COM 虚拟 CAN 执行编程会话、Seed/Key、擦除、下载、CRC/Manifest 提交；`--probe` 只验证入口和解锁，不擦 Flash。
- `can_peer.py`：作为USB-CAN节点B，收到 `0x5A0` 后以 `0x5A1` 回传相同载荷。
- `requirements-can.txt`：PC端CAN工具的Python依赖。
- `virtual_vcu.py`：通过 UART1 模拟另一个整车 CAN 节点，注入 `0x100` 并校验 `0x180`。
- `requirements-sim.txt`：软件 VCU 仿真器依赖。

安全探测（会停在 Bootloader，重新上电可返回应用）：

```powershell
D:\ProgramData\anaconda3\python.exe .\tools\bootloader_tester.py --port COM9 --probe
```

完整诊断升级：

```powershell
D:\ProgramData\anaconda3\python.exe .\tools\bootloader_tester.py --port COM9 `
  --package .\firmware\KaoYa_Project\MDK-ARM\KaoYa_Application\KaoYa_Application.kya
```

两段式断电恢复回归（第一条结束后要给开发板断电重上电）：

```powershell
D:\ProgramData\anaconda3\python.exe .\tools\bootloader_tester.py --port COM9 `
  --interrupt-test .\firmware\KaoYa_Project\MDK-ARM\KaoYa_Application\KaoYa_Application.kya
D:\ProgramData\anaconda3\python.exe .\tools\bootloader_tester.py --port COM9 `
  --recover .\firmware\KaoYa_Project\MDK-ARM\KaoYa_Application\KaoYa_Application.kya
```
