# CAN 通信矩阵 V0.1

## 总线参数

- Classical CAN 2.0A，11-bit Standard ID
- 目标波特率：500 kbit/s
- STM32F407 APB1：42 MHz
- 初始位时序：Prescaler 6，BS1 11 TQ，BS2 2 TQ，SJW 1 TQ
- 多字节信号使用 Intel/Little-Endian

## 应用报文

| ID | 名称 | 方向 | 周期 | 超时 | DLC |
|---:|---|---|---:|---:|---:|
| 0x100 | VCU_COMMAND | RX | 20 ms | 100 ms | 8 |
| 0x101 | VCU_TEST_FAULT_INJECTION | RX | 按需 | - | 8 |
| 0x180 | VCU_MOTION_STATUS | TX | 20 ms | - | 8 |
| 0x181 | VCU_POWER_STATUS | TX | 100 ms | - | 8 |
| 0x182 | VCU_IMU_STATUS | TX | 100 ms | - | 8 |
| 0x700 | VCU_HEARTBEAT | TX | 1000 ms | - | 8 |

### 0x100 VCU_COMMAND

| Byte | 信号 | 类型/比例 | 说明 |
|---:|---|---|---|
| 0..1 | TargetVx | int16, 1 mm/s | 目标纵向速度 |
| 2..3 | TargetWz | int16, 1 mrad/s | 目标横摆角速度 |
| 4 | Ignition | uint8 | 0=OFF, 1=ON |
| 5 | Gear | uint8 | 0=N, 1=D, 2=R |
| 6 | AliveCounter | uint8, 低4位 | 每帧加一，0..15 回绕 |
| 7 | CRC8 | uint8 | SAE J1850，对 Byte0..6 计算 |

### 0x180 VCU_MOTION_STATUS

| Byte | 信号 | 类型/比例 |
|---:|---|---|
| 0..1 | LeftWheelSpeed | int16, 1 mm/s |
| 2..3 | RightWheelSpeed | int16, 1 mm/s |
| 4 | VehicleState | uint8 |
| 5 | MotionFaultFlags | uint8 |
| 6 | AliveCounter | uint8, 低4位 |
| 7 | CRC8 | uint8 |

`VehicleState` 枚举：

| 值 | 状态 | 含义 |
|---:|---|---|
| 0 | INIT | 初始化中 |
| 1 | STANDBY | 已就绪，点火关闭或 N 挡 |
| 2 | DRIVE | 有效驱动指令，允许电机输出 |
| 3 | SAFE_STOP | 命令超时，统一安全出口强制零 PWM |
| 4 | FAULT | 严重故障，禁止驱动 |

`MotionFaultFlags`：Bit0 为命令超时，Bit1 为存在已确认且活动的 DTC，
Bit2 为存在已确认的历史 DTC，Bit3 为安全关联故障活动，Bit7 为严重故障。

### 0x101 VCU_TEST_FAULT_INJECTION

该报文只用于低压台架和软件回归，不是量产控制接口。Byte0 为故障掩码：
Bit0 欠压、Bit1 编码器不可信、Bit2 IMU 通信、Bit3 任务监控、Bit4 CAN 控制器。
Byte1 必须是 `0xA5`，Byte2..5 必须为 0，Byte6 低四位为 Alive Counter，Byte7 为 CRC8。

可由 CANoe、CANalyzer、BusMaster 或 `cantools` 导入的 DBC 位于
[`mini_vcu.dbc`](mini_vcu.dbc)。当前 DBC 仅收录已实现的 `0x100` 和 `0x180`，避免把规划中的报文误标为已交付。

### 0x181 VCU_POWER_STATUS

| Byte | 信号 | 类型/比例 |
|---:|---|---|
| 0..1 | BatteryVoltage | uint16, 1 mV |
| 2..3 | ImuTemperature | int16, 0.01 degC |
| 4..5 | PowerFaultFlags | uint16 |
| 6 | AliveCounter | uint8, 低4位 |
| 7 | CRC8 | uint8 |

### 0x182 VCU_IMU_STATUS

| Byte | 信号 | 类型/比例 |
|---:|---|---|
| 0..1 | YawRate | int16, 0.01 deg/s |
| 2..3 | AccelX | int16, 1 mg |
| 4..5 | AccelY | int16, 1 mg |
| 6 | AliveCounter | uint8, 低4位 |
| 7 | CRC8 | uint8 |

### 0x700 VCU_HEARTBEAT

包含节点状态、软件主/次版本、复位原因和 Alive Counter。具体字节布局在导入固件后确定。

## 诊断寻址

| ID | 用途 |
|---:|---|
| 0x7DF | 功能寻址请求 |
| 0x7E0 | VCU 物理寻址请求 |
| 0x7E8 | VCU 物理响应 |

## CRC8 参数

- CRC-8/SAE-J1850
- Poly `0x1D`，Init `0xFF`，XorOut `0xFF`
- RefIn/RefOut 均为 false
- 检查值：ASCII `123456789` 的结果为 `0x4B`

## UART 虚拟 CAN 封装

无 USB-CAN 时，PC 通过 UART1 代理另一个 CAN 节点。串口外层仍使用原有
`AA 55 ... CRC16 66 BB` 协议，其 payload 固定为 11 字节：

| 偏移 | 字段 | 说明 |
|---:|---|---|
| 0..1 | CAN ID | uint16 小端 |
| 2 | DLC | 0..8 |
| 3..10 | CAN Data | 固定占 8 字节 |

- PC -> MCU 外层消息 ID：`0x02`
- MCU -> PC 外层消息 ID：`0x82`
- 外层 CRC16 负责串口传输完整性，CAN Data 内 CRC8/Alive Counter 负责车载应用层 E2E 检查。

## 待固化项

- Data ID 规则
- 信号无效值
- 初始值和发送偏移
- Counter/CRC 故障去抖策略
- 扩展帧和诊断并发策略
