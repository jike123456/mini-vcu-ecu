# 来源与许可说明

Mini VCU 在Robot STM32F407／FreeRTOS 下位机框架基础上扩展控制状态机、通信校验、诊断、Bootloader 和测试工具。它不是从零编写全部底层代码的项目。原框架整理版见 [STM32 FreeRTOS Robot](https://github.com/jike123456/stm32-freertos-robot)。

原始源文件中的作者、版权与许可声明均保留。本仓库不为整库添加 MIT 或其他统一再许可。

| 组成部分 | 来源或权利人 | 随附说明 |
|---|---|---|
| 原下位机框架及相关业务代码 | 多个文件标注 `Copyright (c) 2025-2035 . All rights reserved.`；未见覆盖原工程全部代码的统一许可 |
| STM32F4 HAL | STMicroelectronics | [HAL LICENSE](firmware/KaoYa_Project/Drivers/STM32F4xx_HAL_Driver/LICENSE.txt) |
| CMSIS | Arm 及相应权利人 | [CMSIS LICENSE](firmware/KaoYa_Project/Drivers/CMSIS/LICENSE.txt) |
| STM32F4 CMSIS Device | STMicroelectronics | [Device LICENSE](firmware/KaoYa_Project/Drivers/CMSIS/Device/ST/STM32F4xx/LICENSE.txt) |
| FreeRTOS | FreeRTOS 项目及相应权利人 | [FreeRTOS LICENSE](firmware/KaoYa_Project/Middlewares/Third_Party/FreeRTOS/Source/LICENSE) |

其他驱动、字体数据和文件以各自声明及来源条款为准。公开访问不意味着所有内容均可任意再分发、商用或重新许可；使用者应确认相关授权。此说明不替代权利人的许可。

外部教学 PDF、本机调试配置、编译产物及原本地 Git 历史未纳入本仓库。
