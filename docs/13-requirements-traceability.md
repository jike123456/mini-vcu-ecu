# 需求追踪矩阵

本文档把项目章程中的需求连到设计、代码、自动化测试和实板证据。机器可读的
明细位于 `docs/requirements-traceability.json`，并由 `tools/check_traceability.py` 校验。

## 状态口径

- `VERIFIED`：实现存在，且具有自动化或实板验证证据。
- `PARTIAL`：已实现一部分，但与原始需求仍有可明确说明的差距。
- `OPEN`：还没有可用验证证明完成。

## 总览

| ID | 需求摘要 | 状态 | 主实现 | 验证/证据 | 未关闭差距 |
|---|---|---|---|---|---|
| REQ-CAN-001 | CAN1 500 kbit/s 及硬件过滤 | PARTIAL | `can_bus.c` | Loopback 实板 | accept-all；真实双节点/Bus-off 未验证 |
| REQ-COM-001 | 20 ms 通信与 100 ms 超时安全输出 | VERIFIED | `control_task.c` | 软件 VCU 失联回归 | — |
| REQ-STATE-001 | VCU 运行/降级/故障状态机 | PARTIAL | `vehicle_state.c` | 故障注入实板 | READY/DERATE 未独立建模 |
| REQ-CTRL-001 | 双轮 PID 和正反转反馈 | PARTIAL | `pid.c`, `capture.c` | 软件电机模型 | 实体电机/编码器未验证 |
| REQ-COM-002 | CRC8/Alive/新鲜度校验 | VERIFIED | `vehicle_can.c` | 单元+失联回归 | — |
| REQ-DTC-001 | DTC、Freeze Frame、清除和 NvM | VERIFIED | `dtc_manager.c`, `nvm_dtc.c` | Phase 4/5 实板回归 | — |
| REQ-UDS-001 | ISO-TP 与应用 UDS 子集 | VERIFIED | `uds_server.c` | Phase 5 实板回归 | — |
| REQ-TEST-001 | Python 自动化和故障注入 | VERIFIED | `tools/*.py` | 单元+实板脚本 | — |
| ADV-BOOT-001 | Boot/Application/NvM 分区与独立构建 | VERIFIED | `boot_layout.h` | Factory HEX 边界测试 | — |
| ADV-BOOT-002 | UDS 诊断刷写服务 | VERIFIED | `boot_diag.c` | 75284 B 实板刷写 | — |
| ADV-BOOT-003 | CRC32/Manifest/向量表安全跳转 | VERIFIED | `boot_image.c` | 损坏镜像+中断恢复 | — |
| ADV-QA-001 | 可重复构建与零告警 | VERIFIED | `build_*.ps1` | Keil 0 Error/0 Warning | — |
| ADV-QA-002 | 静态分析与覆盖率报告 | PARTIAL | 静态检查与主机覆盖率脚本 | 34 文件零告警；5 模块行 100%/分支 96.31% | Bootloader/驱动等目标代码覆盖率未生成 |
| ACC-TIMING-001 | 10 ms 控制周期抖动测量 | OPEN | 周期已配置 | — | 无实测时序数据 |

## 结论

当前共 14 项：9 项 `VERIFIED`、4 项 `PARTIAL`、1 项 `OPEN`。Phase 7 的后续顺序由
该矩阵决定：`ADV-QA-002` 已建立五模块主机覆盖率基线，接着做 `ACC-TIMING-001` 运行时时序报告；
真实 CAN 和实体电机所需硬件到位后，再关闭三个 `PARTIAL` 需求。
