# 无电机、无 CAN 收发器回归步骤

本阶段只使用 STM32F407 开发板和 DAPLink。不要连接 TB6612FNG，也不要连接直流电机。默认 `MOTOR_CFG_USE_SIM=1`，PID 直接使用软件电机模型的轮速，因此主回归不需要编码器跳线。

## 1. 可选的定时器输出/捕获回归接线

只有需要额外验证 TIM10/TIM11 模拟脉冲与 TIM2 捕获时，才在断电后接两根跳线：

| 模拟对象 | 定时器模拟输出 | 输入捕获反馈 | 接线 |
|---|---|---|---|
| 左轮编码器 | PB8 / TIM10_CH1 | PA15 / TIM2_CH1 | PB8 -> PA15 |
| 右轮编码器 | PB9 / TIM11_CH1 | PB3 / TIM2_CH2 | PB9 -> PB3 |

CAN内部 Loopback 回归时，先将 `can_bus.h` 中的 `CAN_BUS_INTERNAL_LOOPBACK` 改为 `1U`。该模式不需要连接 PA11、PA12，也不需要终端电阻；仓库当前默认为真实物理总线模式。

## 2. 烧录

1. 用 Keil 打开 `firmware/KaoYa_Project/MDK-ARM/KaoYa_Project.uvprojx`。
2. 全量编译并烧录 `KaoYa_Project.hex`。
3. 复位开发板，打开 UART2 日志串口。

当前构建基线：`0 Error(s), 0 Warning(s)`。

## 3. CAN 内部回环判据

启动后应看到：

```text
[CAN] CAN1 500k internal loopback ready
[CAN] loopback PASS seq=0 total=1
```

之后每 10 次成功打印一次 PASS。测试链路为：

```text
CanTask -> CAN TX mailbox -> bxCAN internal loopback
        -> RX FIFO0 interrupt -> FreeRTOS queue -> CanTask byte check
```

任何 `loopback FAIL` 都算失败；日志会同时显示累计 TX、RX、错误中断和 ESR 寄存器值。

## 4. 电机/编码器模拟判据

通过 UART1 连续下发非零速度命令后，控制链路会计算左右轮 PWM，但不会驱动实体电机。软件一阶电机模型把 PWM 换算成轮速，PID 与 CAN `0x180` 都使用这一份反馈。TIM10/TIM11 仍会输出相应脉冲，可用两根跳线送回 TIM2 做独立的输入捕获回归。

观察要点：

- 目标速度改变后，左右轮反馈速度应逐渐接近目标，不应瞬间跳变。
- 停止命令后，反馈速度应按模型时间常数回落到 0。
- 拔掉 PB8 -> PA15，只应破坏左轮反馈；拔掉 PB9 -> PB3，只应破坏右轮反馈。
- 两根线都拔掉后，PWM 仍可能由 PID 增大，但捕获轮速应为 0；这可作为编码器断线故障注入的基础。

## 5. 当前边界

- 轮速方向来自电机命令符号；单通道脉冲不能像 AB 相编码器一样独立判断方向。
- CAN Loopback 验证的是 MCU 内部控制器、邮箱、FIFO、中断和队列，不验证物理收发器、线束、终端电阻或第二节点。
- PF7 电池 ADC 和 PB10/PB11 软件 I2C IMU 仍是实物输入，不属于当前模拟源。
