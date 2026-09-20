# Phase 7 静态分析报告

## 1. 结论

2026-09-11 对 Mini VCU 自研 C 代码完成一次可重复的 MISRA 风格静态筛查。
最终结果如下：

```text
STATIC ANALYSIS: files=34 warnings=0 errors=0 policy=0
STATIC ANALYSIS COMPLETE
```

修正后又使用项目实际目标编译器完成构建：Bootloader 构建通过，Application 为
`0 Error(s), 0 Warning(s)`；18 项 Python 自动化测试全部通过。

本结果表示当前规则集没有遗留告警，不代表通过 MISRA C 合规认证，也不替代代码
评审、动态测试和实物台架验证。

## 2. 工具与检查范围

- 静态分析器：Arm GNU Toolchain `arm-none-eabi-gcc 12.2.1`。
- 目标参数：Cortex-M4、Thumb、硬浮点，STM32F407 宏和 Application 向量偏移。
- 分析入口：`tools/run_static_analysis.ps1`。
- 范围：18 个 App、13 个自研 Bsp、3 个 Bootloader 源文件，共 34 个文件。
- 规则：`-fanalyzer`、`-Wall/-Wextra`、整数与符号转换、格式串、阴影变量、
  指针对齐/const、枚举完整性、VLA、重复条件、空指针和函数原型等严格告警。
- 策略扫描：禁止直接调用 `goto`、动态内存分配、`strcpy/strcat/sprintf/gets`。
- 失败门槛：任一 warning、error 或策略命中都会使脚本返回失败。

CubeMX 生成代码、STM32 HAL/CMSIS、FreeRTOS、LCD 历史底层实现和 ST 的 IMU
寄存器驱动作为第三方或生成依赖，不进入 34 文件告警统计。它们仍由 Keil 全量构建
覆盖；在调用点发现的 LCD 接口缺陷也已同步修改到底层声明和实现。

## 3. 发现与修正

检查分两轮扩展：首轮严格检查产生 12 条诊断，修正后再开启 const、对齐、枚举完整性
等规则，发现 37 条诊断。两轮诊断均已清零，主要问题如下：

| 类别 | 风险 | 修正 |
|---|---|---|
| LCD 坐标运算 | 无符号 `y - 2` 可能下溢，窄化可能截断坐标 | 使用 32 位中间量、边界饱和和显式转换 |
| LCD 字符串接口 | 字面量被传给可写 `char *`；横坐标被保存为 8 位 | 接口改为 `const char *`，横坐标改为 16 位 |
| ADC DMA 缓冲区 | 16 位数组强转 32 位指针，丢失 volatile 且可能未按 4 字节对齐 | 使用带 32 位对齐成员的 union，DMA 仍按 half-word 写入 |
| CAN 统计快照 | 通过 `void *` 强转移除 volatile | 在临界区使用结构体赋值完成清零和快照 |
| 枚举处理 | FreeRTOS、VCU 和日志等级分支不完整 | 显式处理 `eInvalid`、`VCU_STATE_INIT`、`LOG_LVL_KAOYA` |
| 接口可见性 | 仅文件内使用的函数暴露，部分长度参数被缩窄 | 改为 `static`，长度统一为 `uint16_t` |
| 配置确定性 | 编码器教学宏依赖外部包含顺序 | 在接口头文件提供安全默认值 0 |
| 编译器可移植性 | Bootloader 跳转汇编只支持 ARMCC | 按 ARMCC/GCC 分支提供等价实现 |

## 4. 回归证据

```text
Bootloader build passed.
KaoYa_Application.axf - 0 Error(s), 0 Warning(s).
18 passed in 0.63s
```

本次生成的 Application 包信息：

- 地址：`0x08020000..0x080326BF`
- 大小：75,456 bytes
- CRC32：`0xA4C4485C`
- 版本：`0.6.0.0`
- 初始 SP：`0x2000B5C0`
- Reset Vector：`0x0802025D`

## 5. 未关闭项

- 新包尚未在本阶段自动烧录；开发板上仍是上一轮通过实板回归的镜像。
- 尚未生成语句、分支或 MC/DC 覆盖率，`ADV-QA-002` 因此保持 `PARTIAL`。
- 尚未测量 10 ms ControlTask 的真实抖动，`ACC-TIMING-001` 保持 `OPEN`。
- 第三方/生成代码没有进行本项目规则集的逐文件零告警承诺。
- 静态分析不能覆盖 DMA 并发时序、CAN 电气层或实体电机闭环，仍需后续实物回归。

## 6. 复现命令

```powershell
.\tools\run_static_analysis.ps1
.\tools\build_bootloader.ps1
.\tools\build_firmware.ps1
D:\ProgramData\anaconda3\python.exe -m pytest -q
```
