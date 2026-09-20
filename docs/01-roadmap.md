# 实施路线

每个阶段都必须满足“可编译、可验证、可说明”后再进入下一阶段。

## Phase 0 - 项目基线

- [x] 建立项目章程
- [x] 建立软件架构
- [x] 建立第一版 CAN 通信矩阵
- [x] 建立仓库目录和忽略规则

退出条件：范围、边界和各阶段交付物明确。

## Phase 1 - 导入并清理原固件

- [x] 仅导入真实参与编译的源文件
- [x] 保留 CubeMX `.ioc` 和 Keil 工程
- [x] 去除构建产物和备用重复实现
- [x] 修复反向编码器反馈、PID 死区和看门狗刷新问题
- [x] 建立干净编译基线
- [ ] 在实物上回归 UART 控车、急停和传感器显示

当前结果：Keil 全量编译已达到 0 Error、0 Warning；电机/编码器已选择定时器输出到输入捕获的跳线模拟方案；原有 UART 控车链路等待实物回归后关闭本阶段。

## Phase 2 - CAN 驱动与台架通信

- [x] CAN1 PA11/PA12，500 kbit/s（寄存器驱动已实现）
- [ ] CAN 收发器和终端电阻检查
- [x] Loopback 模式上板稳定性测试
- [ ] Silent 和 Normal 模式台架测试（Normal双节点固件已实现，待USB-CAN实测）
- [x] RX FIFO0 中断到 FreeRTOS Queue
- [ ] TX 邮箱管理、过滤器、错误统计和 Bus-off 恢复

当前结果：内部 Loopback 模式每秒发送标准帧 `0x5A0`，接收中断投递队列后由 `CanTask` 校验 ID、DLC 和 8 字节载荷；完整工程编译为 0 Error、0 Warning。当前过滤器为接收全部标准帧，已具备 TX 邮箱选择和基础错误计数；精确过滤、Silent/Normal 台架测试与 Bus-off 恢复留待接入收发器后完成。

退出条件：PC 或第二节点可以稳定收发，并通过压力与错误恢复测试。

## Phase 3 - 信号层与 VCU 状态机

- [x] UART/CAN 统一 Command Broker
- [x] CAN 通信矩阵与 DBC
- [x] Alive Counter、CRC 和报文超时
- [x] VCU 状态机与统一安全输出
- [x] 周期状态报文

当前结果：软件整车节点可通过 UART1 注入 `0x100`，控制指令经 Command Broker
进入明确的 STANDBY/DRIVE/SAFE_STOP/FAULT 状态机；电机输出经 SafetyManager 唯一出口处理。
CRC8、Alive Counter 或 100 ms 新鲜度检查不通过时不更新控制目标并进入安全停车。

退出条件：丢失、篡改或延迟控制帧时，系统能在规定时间内进入安全状态。

## Phase 4 - 故障管理与 NvM

- [x] DTC 状态位和去抖
- [x] Freeze Frame（RAM）
- [x] Flash 参数区和磨损控制（Sector 11 追加日志 + 5 s 限频）
- [x] 欠压、编码器、IMU、任务和 CAN 故障联动（软件注入 + 真实 IMU/CAN 健康源）

退出条件：故障可复现、可保存、可读取、可按条件清除。

当前结果：Phase 4 已通过 DAPLink COM9 实板回归。五类故障可一键注入，
安全策略能区分报警、SAFE_STOP 与 FAULT，9 个 DTC 均可通过 UDS 多帧读取、
去抖恢复、条件清除并追加保存到 Flash Sector 11。

## Phase 5 - ISO-TP 与 UDS

- [x] ISO-TP 单帧请求/应答、多帧应答、流控和超时
- [x] 0x10、0x11、0x14、0x19、0x22、0x3E
- [x] 会话状态、P2/P2* 和 NRC（含异步例程的 0x78 调度）
- [x] Python 诊断测试器（DTC、DID、会话、Tester Present、S3 超时、ECU Reset）

退出条件：自动化测试覆盖正常响应、否定响应和超时路径。

当前结果：Phase 5 已通过 DAPLink COM9 实板回归。诊断测试覆盖单帧和多帧、
默认/扩展会话、S3 超时、DID、DTC 读取清除、否定响应、异步例程
`0x31 FF00` 的 `0x78 ResponsePending` 及最终响应，以及 `0x11` 硬复位后
运行时间归零和会话恢复默认。

## Phase 6 - CAN Bootloader

- [x] Bootloader/Application Flash 分区与独立构建
- [x] 应用 CRC32 和向量表跳转
- [x] 0x27、0x31、0x34、0x36、0x37
- [x] 下载中断后的可恢复刷写

退出条件：正常、错误和中断刷写场景都有可重复测试结果。

当前结果：已固定 Bootloader `0x08000000..0x0801FFFF`、Application
`0x08020000..0x080DFFFF`、DTC NvM `0x080E0000..0x080FFFFF`。Bootloader 和重定位
Application 均可独立构建，组合工厂镜像通过 13 项离线测试。Bootloader 已实现双
CRC32、版本/长度/向量合法性检查和 MSP/VTOR 安全跳转。组合镜像已通过 DAPLink
SWD 实板烧录，DID F102 返回 `0x08020000`；`0x11` 复位后运行时间由 34936 ms
回落至 75 ms，证明 Bootloader 能在复位后重新校验并跳转 Application。

诊断刷写主路径也已通过 DAPLink COM9 实板测试：应用在 STANDBY 下经 `0x10 02`
和 `0x11 01` 进入 Bootloader，`0x27` 解锁后由 `0x31 FF01` 擦除 Sector 5..10，
`0x34/0x36` 以 256 B 分块写入 75284 B 应用，`0x37` 校验 CRC32
`0x8DB3C74B` 并最后提交 Manifest。升级耗时 24.8 s，自动复位后的完整 Phase 5
诊断回归再次通过。恢复测试又在写入 4096 B 后主动断电，重新上电后
Bootloader 拒绝跳转残缺应用；错误 BSC 返回 `7F 36 73`，错误 CRC 返回
`7F 37 72`，最后完整恢复镜像并再次通过 Phase 5 回归。Phase 6 退出条件已满足。

## Phase 7 - 工程化交付

- [x] 需求到测试的追踪矩阵
- [x] MISRA 风格检查和静态分析
- [ ] 单元、集成和台架自动化测试
- [x] 五个固件 C 模块的主机单元/集成测试与覆盖率基线
- [ ] CPU、栈、堆、总线负载和时序报告
- [ ] 演示视频、架构图和简历描述

当前结果：已使用 ARM GNU GCC 12.2.1 对 34 个自研 App/Bsp/Bootloader 源文件执行
`-fanalyzer`、严格类型/枚举/格式告警和禁用函数扫描，最终为 0 Warning、0 Error、
0 Policy Hit；随后 ARMCC/Keil 全量构建保持 0 Error、0 Warning，18 项主机自动化
测试全部通过。2026-09-12 又完成 CAN/状态机/DTC/UDS/PID 五个真实 C 模块的
主机覆盖率：857 次断言检查通过，行 579/579（100%）、分支 287/298（96.31%）；
Python 测试现为 20 项通过。Bootloader/其他目标代码覆盖率及运行时时序报告仍待完成。
