# 软件 VCU 闭环回归

本测试不需要电机、编码器、CAN 收发器或 USB-CAN。PC 通过 UART1 充当虚拟整车节点，
STM32 上的电机/编码器模拟仍提供闭环反馈。

## 本阶段闭环

```text
PC virtual_vcu.py
  -> UART1 外层协议 / 0x100 VCU_COMMAND / CRC8 + Alive
  -> CommTask -> CommandBroker -> ControlTask -> PID/电机模型
  -> 模拟编码器轮速 -> 0x180 VCU_MOTION_STATUS
  -> UART1 外层协议 -> PC 解码与校验
```

`CAN_BUS_INTERNAL_LOOPBACK` 默认为 `1`，因此原有 bxCAN 外设内部回环任务也会继续运行。

## 1. 编译和烧录

在 PowerShell 中进入项目根目录：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
```

然后用 Keil + DAPLink 烧录新的 `KaoYa_Project.axf`。UART1 使用 `115200, 8-N-1`。

## 2. 准备 PC 仿真器

```powershell
python -m pip install -r .\tools\requirements-sim.txt
python .\tools\virtual_vcu.py --list-ports
```

关闭正在占用 UART1 的串口助手。不要选 UART2 日志口，要选与 STM32 UART1
TX/RX 连接的 USB-TTL 虚拟串口。

若开发板上没有已连到 UART1 的 USB 串口，使用 3.3 V USB-TTL：

| USB-TTL | STM32F407ZGY6 |
|---|---|
| RXD | PA9 / USART1_TX |
| TXD | PA10 / USART1_RX |
| GND | GND |

不要将 USB-TTL 的 5 V 接到 MCU IO，也不必接 VCC；开发板另行正常供电。

## 3. 运行正常指令 + 失联注入

以 `COM9` 为例：

```powershell
python .\tools\virtual_vcu.py --port COM9 --vx 300 --wz 0 --duration 8 --drop-after 5
```

前 5 秒每 20 ms 发送 `0x100`；随后停止指令，但继续监听 `0x180`。预期：

- 正常时 `state=2(DRIVE)` 且 `fault=0x00`，左右轮速逐步跟随目标。
- 停发后约 100 ms，`state=3(SAFE_STOP)` 且 `fault=0x01`，统一安全出口立即强制 PWM 为 0，轮速逐步回落。
- `bad_status=0`，说明 STM32 发出的 CRC8 都正确。

## 4. CRC 故障注入

```powershell
python .\tools\virtual_vcu.py --port COM9 --duration 8 --drop-after 0 --bad-crc-every 10
```

UART2 日志应周期出现 `VCAN CMD reject reason=2`。因为错误报文不会刷新命令时间，
连续 100 ms 都无有效报文时仍会进入安全停车。

## 验收标准

- 固件全量编译 `0 Error, 0 Warning`。
- PC 自测 `python .\tools\virtual_vcu.py --self-test` 显示 `SELF-TEST PASS`。
- LCD 顶部 `VCU` 会从 `STBY` 变为 `DRIVE`，停发指令后变为 `SAFE`。
- PC 连续收到 `0x180`，Alive Counter 按 0..15 回绕，CRC8 无错。
- `0x100` 停发后 100 ms 内进入 `state=3/fault=0x01`。
