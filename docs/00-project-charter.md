# 项目章程

## 1. 项目名称

Mini VCU ECU：基于 STM32F407 的双轮电驱控制与车载诊断节点。

## 2. 可用资源

- STM32F407ZGT6，168 MHz
- FreeRTOS/CMSIS-RTOS2
- 双电机、TIM3 PWM 与方向 GPIO
- TIM2 双路编码器捕获
- TIM10/TIM11 编码器脉冲模拟
- LSM6DSV16X IMU
- ADC3 + DMA 电压采集
- 800 x 480 LCD
- USART1 通信、USART2 异步日志
- IWDG、任务栈和堆监控
- CAN1 候选引脚：PA11 RX、PA12 TX

## 3. 项目范围

### 必须完成

1. `[REQ-CAN-001]` CAN1 500 kbit/s 基础通信和硬件过滤。
2. `[REQ-COM-001]` 20 ms 控制命令、状态上报和 100 ms 命令超时保护。
3. `[REQ-STATE-001]` INIT、STANDBY、READY、DRIVE、DERATE、FAULT 状态机。
4. `[REQ-CTRL-001]` 双轮闭环控制以及正反转速度反馈。
5. `[REQ-COM-002]` 报文 Alive Counter、CRC 和新鲜度判断。
6. `[REQ-DTC-001]` 可查询、可清除、可掉电保存的 DTC。
7. `[REQ-UDS-001]` ISO-TP 和基础 UDS 服务。
8. `[REQ-TEST-001]` Python 自动化测试与故障注入。

### 进阶范围

- `[ADV-BOOT-001]` CAN Bootloader
- `[ADV-BOOT-002]` UDS 固件下载服务
- `[ADV-BOOT-003]` 应用 CRC32 和安全跳转
- `[ADV-QA-001]` 编译告警清零
- `[ADV-QA-002]` 静态检查和测试覆盖率报告

### 暂不包含

- 真正的 AUTOSAR BSW/RTE/MCAL 产品栈
- ASIL 或 ISO 26262 合规认证
- CAN FD、LIN、车载以太网
- 真实高压电驱和道路测试

## 4. 初始验收指标

- `[ACC-TIMING-001]` 控制周期：10 ms，测量并记录实际抖动。
- CAN 命令周期：20 ms；连续 100 ms 无有效命令时进入安全输出。
- CRC 或 Alive Counter 异常报文不得更新控制目标。
- CAN Bus-off、编码器丢失、IMU 失败和欠压均能形成确定的状态转移和 DTC。
- 严重故障发生后，PWM 必须由统一安全出口置零。
- 复位后历史 DTC 可读取，清除操作有明确条件。
- 自动化测试可以重复生成通过/失败结果，不依赖人工观察日志。

上述指标是项目目标，必须经过实测后才能写入简历结果。
