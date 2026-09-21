# Mini VCU ECU

基于 **STM32F407 + FreeRTOS** 的微型电驱控制器原型，围绕速度控制、通信保护、故障管理、诊断与固件升级构建可复现的软件链路。

当前主验证环境为 **STM32F407ZGY6 实板 + 软件电机模型 + UART 虚拟 CAN**：PC 下发速度命令，固件完成状态判断与双轮 PID 控制，再回传轮速和故障信息。CAN1 已通过 **500 kbit/s 内部回环**验证；真实 CAN 双节点控制与 UDS 业务链路尚未验收。

[验证结果](#验证结果) · [构建与测试](#构建与测试) · [需求追踪](docs/13-requirements-traceability.md) · [来源与许可](#来源与许可)

## 架构与主要扩展

在既有 STM32/FreeRTOS 下位机工程上，扩展以下控制、诊断和验证能力：

| 模块 | 实现内容 | 源码入口 |
|---|---|---|
| 控制与状态机 | 接入已有电机模型与双轮 PID，扩展 `INIT / STANDBY / DRIVE / SAFE_STOP / FAULT` 状态与驱动条件 | [control_task.c](firmware/KaoYa_Project/App/Src/control_task.c)、[vehicle_state.c](firmware/KaoYa_Project/App/Src/vehicle_state.c) |
| 命令与通信保护 | 命令仲裁、CRC8、Alive Counter、无效帧拒收及命令超时停车 | [command_broker.c](firmware/KaoYa_Project/App/Src/command_broker.c)、[vehicle_can.c](firmware/KaoYa_Project/Bsp/Src/vehicle_can.c) |
| 故障与持久化 | 统一安全出口、DTC 去抖与恢复、Freeze Frame、受限清除和 Flash 存储 | [safety_manager.c](firmware/KaoYa_Project/App/Src/safety_manager.c)、[dtc_manager.c](firmware/KaoYa_Project/App/Src/dtc_manager.c)、[nvm_dtc.c](firmware/KaoYa_Project/App/Src/nvm_dtc.c) |
| 应用诊断 | ISO-TP/UDS 子集：会话、DID、DTC、多帧响应、S3 超时、异步例程与 ECU Reset | [uds_server.c](firmware/KaoYa_Project/App/Src/uds_server.c) |
| 固件升级 | 独立 Bootloader、Flash 分区、CRC32/Manifest、向量表跳转；`0x27 / 0x31 / 0x34 / 0x36 / 0x37` 下载服务 | [Bootloader](firmware/Bootloader)、[boot_layout.h](firmware/Shared/boot_layout.h) |
| 验证工具 | Python 虚拟整车节点、故障注入、诊断刷写器、真实 C 模块主机测试与覆盖率统计 | [tools](tools)、[tests](tests) |

当前控制回归链路：

```text
PC virtual_vcu.py
  → UART1 / 虚拟 CAN：0x100 命令帧，CRC8 + Alive
  → CommTask → CommandBroker → ControlTask → PID / 软件电机模型
  → 0x180 状态帧：轮速、状态、故障 → PC 校验

故障处理：检测 → SafetyManager / VehicleState → 停机或故障状态
故障记录：DtcManager → Freeze Frame / NvM → UDS 查询
诊断升级：PC 刷写器 → UART 虚拟 CAN → Bootloader → 校验并启动应用
```

通信矩阵见 [DBC](docs/mini_vcu.dbc) 与 [报文说明](docs/03-can-matrix.md)；诊断设计见 [UDS 文档](docs/08-uds-diagnostics.md)，升级设计见 [Flash 分区与镜像格式](docs/10-bootloader-design.md)。

## 验证结果

| 验证层次 | 已有结果 | 证据与范围 |
|---|---|---|
| 主机 Python 测试 | **20 项通过** | 协议、镜像、刷写协议与覆盖率解析器测试；[主机测试报告](docs/15-host-coverage-report.md) |
| 主机 C 测试 | **857 次断言通过**；行覆盖率 **100%（579/579）**、分支覆盖率 **96.31%（287/298）** | 仅统计 `vehicle_can.c`、`vehicle_state.c`、`dtc_manager.c`、`uds_server.c`、`pid.c`；[覆盖率与未覆盖分支](docs/15-host-coverage-report.md) |
| 实板诊断与故障注入 | VIN 多帧、会话超时、否定响应、异步例程、复位、五类故障注入及 DTC 恢复通过 | 通过 UART 虚拟 CAN；[诊断与故障报告](docs/09-phase5-test-report.md) |
| 实板 Bootloader | 重定位应用跳转通过；**75,284 B 应用完成诊断刷写，用时 24.8 s**，升级后诊断回归通过 | [跳转报告](docs/11-phase6-boot-jump-test-report.md)、[刷写报告](docs/12-phase6-diagnostic-flash-test-report.md) |
| 升级异常恢复 | 错误密钥、块序号、CRC 被拒绝；写入 4,096 B 后断电，重上电留在 Bootloader，再次全量刷写恢复 | [中断与恢复证据](docs/12-phase6-diagnostic-flash-test-report.md) |
| CAN 外设 | CAN1 500 kbit/s 内部回环通过，覆盖发送邮箱、接收 FIFO、中断、队列和载荷校验 | [回环链路与判据](docs/04-simulation-regression.md)；不包含物理收发器与第二节点 |
| 静态分析 | 34 个项目侧 C 文件在报告所列规则下 **0 告警、0 错误、0 策略命中** | [静态分析报告](docs/14-static-analysis-report.md)；不等同于 MISRA 合规认证 |

主机 C 测试直接编译固件源文件，通过替身隔离 RTOS 与硬件依赖。覆盖率分母不包含 Bootloader、Flash/NvM 写入、驱动、RTOS 任务及中断并发。板端指标来自随附历史报告，其中刷写数据对应 2026-09-11 的测试镜像；后续源码修正与主机测试不自动构成新一轮实板验收。

## 构建与测试

### 无需开发板的测试

在仓库根目录安装依赖并运行 Python 测试：

```powershell
python -m pip install pytest -r tools/requirements-sim.txt -r tools/requirements-can.txt
python -m pytest -q
```

Windows 主机 C 覆盖率测试需要配套的 GCC/gcov；原验证环境使用 MinGW GCC/gcov 8.1.0。将路径替换为本机安装位置：

```powershell
python tools/run_host_coverage.py --gcc C:\path\to\mingw64\bin\gcc.exe
```

结果生成至 `docs/coverage/`，包含汇总、源码摘要与逐行执行次数。编译、断言或覆盖率门槛失败时，工具返回非零退出码。

### 固件构建与首次下载

- 应用工程：[KaoYa_Project.uvprojx](firmware/KaoYa_Project/MDK-ARM/KaoYa_Project.uvprojx)。
- 原工具链：**Keil ARM Compiler 5.06 update 1**，设备包 **Keil.STM32F4xx_DFP.2.17.1**。
- Bootloader 构建：[build_bootloader.ps1](tools/build_bootloader.ps1)，通过 `-ArmccBin` 指定编译器目录。
- 应用构建：[build_firmware.ps1](tools/build_firmware.ps1)，通过 `-KeilUv4`、`-PythonExecutable` 指定本机工具；同时生成应用镜像与 `.kya` 包。

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_bootloader.ps1 -ArmccBin C:\path\to\ARMCC\bin
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1 -KeilUv4 C:\path\to\UV4.exe -PythonExecutable C:\path\to\python.exe
```

Bootloader 位于 `0x08000000`，Application 位于 `0x08020000`。首次下载先阅读 [分区与镜像设计](docs/10-bootloader-design.md)，使用 [flash_factory.ps1](tools/flash_factory.ps1) 经 SWD 写入 Bootloader、Application 与匹配的 Manifest。重定位应用不能按普通零偏移固件单独烧录。

板端回归入口包括 [virtual_vcu.py](tools/virtual_vcu.py)、[fault_injection_tester.py](tools/fault_injection_tester.py)、[uds_phase5_tester.py](tools/uds_phase5_tester.py) 和 [bootloader_tester.py](tools/bootloader_tester.py)。测试报告使用 DAPLink COM9、USART1 115200 8N1，运行时需匹配实际串口；具体参数见 [工具说明](tools/README.md)。

## 仓库结构

```text
firmware/KaoYa_Project/  应用工程：App / Bsp / Core / Drivers / FreeRTOS
firmware/Bootloader/     独立 Bootloader 与链接配置
firmware/Shared/         Flash 布局及镜像元数据定义
tools/                  构建、镜像打包、诊断、刷写与测试工具
tests/                  Python 测试、C 主机测试及硬件替身
docs/                   需求、设计、通信矩阵、验证报告与覆盖率
```

## 实现边界与待完成项

- 项目为非 AUTOSAR 实现；分层组织不代表实现了 AUTOSAR，也没有量产 ECU 或功能安全认证结论。
- 当前速度闭环反馈来自软件电机模型。真实电机闭环、CAN 双节点控制和真实 CAN 上的 UDS 业务仍待实物验证；已有 [双节点应答测试入口](docs/05-physical-can-regression.md)。
- ISO-TP/UDS 仅实现项目所需子集，未声明完整标准合规；完整流控行为仍有未覆盖范围。
- CRC32 用于完整性校验，不能代替密码学验签。断电恢复通过**重新全量刷写**完成，未实现断点续传、双分区回滚或 OTA。
- 10 ms 控制任务的实际抖动、执行耗时与栈/堆余量仍需板端测量；主机测试不能替代硬件时序、电气和并发验证。

后续工作与验收项见 [路线图](docs/01-roadmap.md) 和 [需求—代码—测试追踪表](docs/13-requirements-traceability.md)。仓库提供源码、工程配置和文档，不包含编译固件、个人调试配置或外部教学 PDF。

## 来源与许可

本项目以 **Kaoya Robot 的 STM32F407/FreeRTOS 下位机框架**为基础扩展；原框架整理版见 [stm32-freertos-robot](https://github.com/jike123456/stm32-freertos-robot)。保留 Kaoya、ST、Arm、FreeRTOS 等原有版权与许可声明，具体来源和使用条款见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。本仓库不为整库添加统一开源许可证。
