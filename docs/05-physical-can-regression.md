# CAN 双节点实物回归

本阶段使用 STM32F407 作为节点 A，USB-CAN 适配器和 PC 脚本作为节点 B。总线为 Classical CAN 2.0A、11-bit 标准帧、500 kbit/s。

## 1. 硬件连接

必须经过 CAN 收发器连接，禁止把 MCU 的 PA11/PA12 直接接到 CANH/CANL。

| STM32 开发板 CAN 接口 | USB-CAN |
|---|---|
| CANH | CANH |
| CANL | CANL |
| GND | GND |

开发板使用板载 CAN 收发器时，将 PA11/PA12 的 USB/CAN 选择跳帽切到 CAN 侧。总线两端各保留一个 120 ohm 终端电阻；全部断电后测量 CANH 与 CANL，两个终端并联时应约为 60 ohm。

## 2. 固件协议

- 节点 A 每秒发送标准帧 `0x5A0`、DLC 8。
- 节点 B 收到后复制全部 8 字节，以标准帧 `0x5A1` 返回。
- 节点 A 在 500 ms 内收到并校验 ID、DLC 和数据后计为一次 PASS。
- LCD 显示 `PASS` 才表示物理双向通信通过；只看到任务 `hb OK` 不代表总线正常。

## 3. PC 节点

安装依赖：

```powershell
python -m pip install -r tools/requirements-can.txt
```

CANable/slcan 示例：

```powershell
python tools/can_peer.py --interface slcan --channel COM10
```

PCAN 示例：

```powershell
python tools/can_peer.py --interface pcan --channel PCAN_USBBUS1
```

将命令中的接口和通道替换为实际适配器参数。脚本收到 `0x5A0` 后会立即发送 `0x5A1`，并打印双方计数和载荷。

## 4. 验收与故障定位

- PC持续看到 `RX 0x5A0`，STM32 LCD持续显示 `PASS`：双向链路通过。
- PC完全收不到：检查500 kbit/s、跳帽、CANH/CANL、共地和终端电阻。
- PC能收到但LCD显示 `NO RX`：检查PC脚本是否发出 `0x5A1` 标准帧且DLC为8。
- CANH/CANL反接、缺少共地或没有第二节点ACK时，STM32可能进入错误被动或Bus-off。

回归结束后，可把 `CAN_BUS_INTERNAL_LOOPBACK` 临时改回 `1U`，验证问题位于物理层而不是MCU内部控制器。
