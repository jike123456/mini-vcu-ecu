# CAN Bootloader 设计

## Flash 分区

STM32F407ZG 提供 1 MiB 内部 Flash。分区按物理扇区边界划分：

| 区域 | 地址 | 扇区 | 大小 | 用途 |
|---|---|---:|---:|---|
| Bootloader | `0x08000000..0x0801FFFF` | 0..4 | 128 KiB | 启动判断、镜像校验、诊断下载 |
| Application 数据 | `0x08020000..0x080DFFDF` | 5..10 | 约 768 KiB | VCU 应用与向量表 |
| Image Manifest | `0x080DFFE0..0x080DFFFF` | 10 | 32 B | 长度、版本、CRC32、提交标记 |
| DTC NvM | `0x080E0000..0x080FFFFF` | 11 | 128 KiB | 保持现有 DTC/Freeze Frame 日志 |

应用从 Sector 5 开始，可在升级时擦除 Sector 5..10 而不影响 Bootloader 和 DTC。

## 镜像清单

清单固定为 8 个小端 `uint32_t`，共 32 字节：

1. Magic：`KAYA`。
2. 清单格式版本。
3. Application 起始地址。
4. 镜像字节数。
5. 应用 CRC32/IEEE。
6. 软件版本，四个 8-bit 分量。
7. Flags，当前保留为 0。
8. 前 28 字节的 CRC32/IEEE。

Bootloader 只有在全部 TransferData 完成、镜像 CRC 正确后才把清单写到固定地址。
断电发生在此前时，清单不存在或校验失败，下一次启动留在 Bootloader，不跳转到
半写入的应用。

## 启动校验顺序

1. 校验清单 Magic、格式、地址、长度和清单 CRC。
2. 校验应用首字为合法 SRAM 栈顶地址。
3. 校验 Reset Handler 带 Thumb 位且落在镜像范围内。
4. 对 Application 有效字节计算 CRC32 并与清单比较。
5. 全部通过才关闭中断和外设、设置 MSP/VTOR，并跳到 Application Reset Handler。

## 主机离线校验

应用必须先链接到 `0x08020000` 并导出原始 `.bin`，再执行：

```powershell
python .\tools\boot_image.py pack --input app.bin --output app.kya --version 0.6.0
python .\tools\boot_image.py verify --input app.kya
```

`.kya` 文件由 32 字节清单和原始应用镜像组成。工具会拒绝错误链接地址、非法
栈顶、非 Thumb Reset Handler、越界镜像、CRC 损坏和清单损坏。

## 已实现的诊断刷写顺序

1. 应用在 STANDBY 接受 `0x10 02` 编程会话，再以 `0x11 01` 复位。
2. 复位前向保留 SRAM 地址写入一次性标记；Bootloader 启动后读取并立即清零。
3. Bootloader 通过 USART1 上的虚拟 CAN 接收 `0x7E0`，以 `0x7E8` 应答。
4. `0x27 01/02` 完成演示级 Seed/Key 解锁。
5. `0x31 01 FF01` 先发送 NRC `0x78`，再擦除 Application 的 Sector 5..10。
6. `0x34` 固定校验起始地址和长度；`0x36` 采用 ISO-TP 多帧、每块最多 256 B。
7. `0x37` 比对整镜像 CRC32，仅在成功后写入 Manifest，随后复位启动应用。

Seed/Key 算法仅用于学习和台架展示，不可宣称达到量产 ECU 安全等级。待完成的
异常测试包括错误块序号、错误 CRC 和传输中断后重新下载。

重定位实板验收读取 UDS DID F102；只有返回 `0x08020000` 才能证明 Application
通过 Bootloader 跳转后使用了自己的中断向量表。

工厂首次烧录镜像可以由下列命令生成。输出只包含 Bootloader、Application 和
Manifest 三段稀疏地址，工具会额外确认没有任何记录进入 Sector 11：

```powershell
python .\tools\boot_image.py factory `
  --bootloader .\firmware\Bootloader\MDK-ARM\Build\KaoYa_Bootloader.bin `
  --application .\firmware\KaoYa_Project\MDK-ARM\KaoYa_Application\KaoYa_Application.kya `
  --output .\firmware\factory\KaoYa_Factory.hex
```
