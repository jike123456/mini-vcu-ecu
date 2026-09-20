# Mini VCU ECU

基于 STM32F407 + FreeRTOS 的非 AUTOSAR 微型电驱控制器学习项目。

基于 Kaoya Robot 既有下位机框架扩展。原 STM32/FreeRTOS 学习工程独立存放于
[stm32-freertos-robot](https://github.com/jike123456/stm32-freertos-robot)；本仓库侧重
控制状态机、通信保护、诊断、固件升级及测试验证，不主张全部底层代码为原创。

项目目标不是复刻量产 ECU，而是在现有双电机、编码器、IMU、电池 ADC、LCD 和串口工程基础上，形成一套可以演示、测试并在汽车嵌入式面试中深入讲解的 ECU 原型。

## 目标能力

- Classical CAN 通信、过滤、周期报文和超时监控
- 双轮速度闭环与整车运行状态机
- Alive Counter、CRC、故障降级和安全停车
- DTC、Freeze Frame 与掉电存储
- ISO-TP 与 UDS 诊断子集
- CAN Bootloader 与应用完整性校验
- Python 自动化测试和故障注入

## 仓库结构

```text
docs/       需求、架构、通信矩阵和阶段计划
firmware/   STM32F407 固件工程
tools/      PC 端 CAN、UDS、刷写及测试工具
tests/      单元、集成与台架测试
```

## 当前状态

当前控制闭环使用软件电机模型，控制与诊断主路径通过 UART 虚拟 CAN 联调。
CAN1 已完成内部回环实板验证；真实 CAN 双节点模式目前仅有应答测试入口，
尚不代表完成真实 CAN 控制与 UDS 业务链路验收。

当前 Phase 6 主路径已完成：除软件电机闭环、VCU 状态机、DTC/NvM 和应用 UDS
外，已实现独立 Bootloader、Flash 分区、CRC32/Manifest、向量表安全跳转，以及
`0x27/0x31/0x34/0x36/0x37` 诊断下载。75284 B 应用已通过 DAPLink COM9 完成
首次全量诊断升级，板端 CRC 校验、自动复位和升级后应用回归均通过。另外已
完成 4096 B 处断电、错误块序号、错误 CRC 及再次完整恢复刷写，Phase 6 已闭环。

详细计划见 [docs/01-roadmap.md](docs/01-roadmap.md)。

Phase 5 实板证据见 [docs/09-phase5-test-report.md](docs/09-phase5-test-report.md)。

Phase 6 Flash 分区与镜像格式见 [docs/10-bootloader-design.md](docs/10-bootloader-design.md)。

Bootloader 跳转实板证据见 [docs/11-phase6-boot-jump-test-report.md](docs/11-phase6-boot-jump-test-report.md)。

诊断刷写实板证据见 [docs/12-phase6-diagnostic-flash-test-report.md](docs/12-phase6-diagnostic-flash-test-report.md)。

需求、代码和测试证据的追踪关系见 [docs/13-requirements-traceability.md](docs/13-requirements-traceability.md)。

自研代码静态分析范围、缺陷修复和残余边界见 [docs/14-static-analysis-report.md](docs/14-static-analysis-report.md)。

五个固件 C 模块的主机覆盖率（行 100%、分支 96.31%）见 [docs/15-host-coverage-report.md](docs/15-host-coverage-report.md)。

当前无需电机和 CAN 收发器的上板步骤见 [docs/04-simulation-regression.md](docs/04-simulation-regression.md)。

真实CAN双节点接线、PC应答程序和验收标准见 [docs/05-physical-can-regression.md](docs/05-physical-can-regression.md)。

无 USB-CAN 的软件整车节点闭环见 [docs/06-software-vcu-regression.md](docs/06-software-vcu-regression.md)。

## 边界声明

本项目采用类 AUTOSAR 的分层思想，但没有使用商业 AUTOSAR 协议栈或配置工具，因此不得描述为“实现 AUTOSAR”。所有测试均在低压台架环境完成，不连接真实车辆执行机构。

ISO-TP/UDS 为功能子集，不是完整标准栈。CRC32 为完整性校验，不是密码学验签；
断电后恢复刷写指重新全量下载，不是断点续传、双分区回滚或 OTA。

## 从源码开始

- 应用工程：`firmware/KaoYa_Project/MDK-ARM/KaoYa_Project.uvprojx`。
- 原工具链配置：Keil ARM Compiler 5.06 update 1，`Keil.STM32F4xx_DFP.2.17.1`。
- Bootloader 构建入口：`tools/build_bootloader.ps1`；应用构建入口：`tools/build_firmware.ps1`。
- 构建脚本的工具路径为原验证环境默认值，请通过 `-ArmccBin`、`-KeilUv4`、`-PythonExecutable` 参数匹配本机安装。
- 应用采用重定位分区，不能把应用镜像当成普通从 `0x08000000` 启动的固件。首次下载前先阅读 [分区与镜像设计](docs/10-bootloader-design.md) 和 `tools/flash_factory.ps1`。

不连接开发板也可以运行主机 Python 测试：

```powershell
python -m pip install pytest -r tools/requirements-sim.txt -r tools/requirements-can.txt
python -m pytest -q
```

Windows 主机 C 测试和覆盖率需要匹配的 GCC/gcov：

```powershell
python tools/run_host_coverage.py --gcc C:\path\to\mingw64\bin\gcc.exe
```

随附覆盖率与板端报告记录了各自测试范围和日期；主机测试不能代替硬件时序、电气和整车验证。
仓库仅提供源码、工程配置和文档，不包含编译固件、个人调试配置或外部教学 PDF。

## 来源与许可

保留 Kaoya、ST、Arm、FreeRTOS 等原有版权和许可文件。不为整库添加统一开源许可证，
详见 [第三方来源与许可说明](THIRD_PARTY_NOTICES.md)。
