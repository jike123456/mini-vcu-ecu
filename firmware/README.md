# Firmware

此目录存放 STM32F407 固件，实际工程位于 `KaoYa_Project/`。

Phase 1 已从原 KaoYa_Project 中导入实际参与 Keil 编译的源文件和 CubeMX 配置，没有纳入旧 `.o`、`.axf`、`.map`、旧目标以及重复备用驱动。

计划保留以下逻辑并逐步重构：

- FreeRTOS 任务框架
- UART DMA/IDLE 通信
- 双轮 PID 和电机驱动
- 编码器、IMU、电池与 LCD
- 异步日志和 IWDG

## 编译

在仓库根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
```

默认使用 `D:\Keil_v5\UV4\UV4.exe`。如 Keil 安装在其他位置，可传入 `-KeilUv4`。

当前基线（ARMCC 5.06 update 7）：0 Error、0 Warning。

## Phase 1 修正

- PID 每个控制周期都更新，避免误差进入阈值后保留旧 PWM
- 事件驱动任务不再被周期心跳误判；重复启用监控不会伪造心跳
- Monitor 心跳周期与超时阈值匹配
- UART1 DMA 异常后释放上报 busy 状态
- IMU 角速度从 mdps 正确换算为协议的 0.01 deg/s
- 单通道编码器依据最近一次非零电机指令估计正反方向
- LCD 将正常阻塞任务显示为 WAIT，而不是 STALL

注意：单通道编码器不能从脉冲本身判定真实方向。当前方向提示适合项目现有硬件，但不等价于正交 AB 相反馈。
