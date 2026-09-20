# 软件架构

本项目采用非 AUTOSAR 的分层实现，借鉴应用与硬件基础软件分离的思想。

```text
Application
  VehicleState     TorqueControl     SafetyManager
                         |
RteLike                  SignalStore / CommandBroker
                         |
Services
  CanCom     E2E     DtcManager     NvM     IsoTp     Uds     WdgManager
                         |
EcuAbstraction
  WheelSpeed     BatterySensor     ImuSensor     MotorActuator
                         |
Platform
  STM32 HAL: CAN / ADC / TIM / GPIO / DMA / Flash / IWDG
```

## 关键约束

1. ISR 只搬运数据和更新时间戳，不解析业务协议。
2. 控制任务不直接调用 CAN、Flash 或 LCD 驱动。
3. UART 和 CAN 命令都必须经过 CommandBroker。
4. 所有电机停机操作汇入 SafetyManager 的统一安全出口。
5. 故障检测、故障记录和故障反应分离。
6. 调度器启动后原则上不再进行动态内存分配。
7. 周期任务使用 `vTaskDelayUntil()` 或硬件时间基准，必须测量实际抖动。

## 建议任务

| 任务 | 周期/触发 | 优先级意图 |
|---|---|---|
| ControlTask | 10 ms | 最高业务优先级 |
| CanRxTask | 队列触发 | 高 |
| CanTxTask | 5 ms 调度槽 | 高 |
| SensorTask | 10 ms/100 ms 分频 | 中高 |
| DiagnosticTask | 队列触发 | 中 |
| NvMTask | 请求触发 | 低 |
| MonitorTask | 100 ms | 低 |
| LcdTask | 200 ms | 最低 |

看门狗不得通过“任务正在 Blocked”判断异常。周期任务报告完成心跳，事件任务使用最后一次成功处理时间和允许静默状态。

