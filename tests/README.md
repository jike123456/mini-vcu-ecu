# Tests

测试按以下层级组织：

```text
tests/unit/         与硬件无关的 C 模块测试
tests/integration/  CAN、ISO-TP、UDS 和状态机联调
tests/hil/          STM32 台架和故障注入测试
```

每条需求最终应关联至少一个正常路径测试和一个异常路径测试。
# 自动化测试

安装 CAN 工具依赖后，在项目根目录执行：

```powershell
python -m pip install -r .\tools\requirements-can.txt
python -m unittest discover -s .\tests -v
```

`test_phase3_protocol.py` 检查 CRC 标准向量、UART 外层帧、CAN 应用层 CRC 和 DBC 字节布局的一致性。

## 已实现的主机 C 回归

`host/test_core.c` 直接链接生产版 `vehicle_can.c`、`vehicle_state.c`、
`dtc_manager.c`、`uds_server.c`、`pid.c`，以 `host/stubs` 替代硬件和 RTOS 依赖。
它不由 pytest 自动启动，需单独运行以下入口：

```powershell
D:\ProgramData\anaconda3\python.exe .\tools\run_host_coverage.py
```

覆盖率与未覆盖分支见 `docs/coverage/latest.json`，范围解释见
`docs/15-host-coverage-report.md`。pytest 中的 `test_coverage_parser.py` 只检查统计
解析是否正确；不能把这两项 Python 测试算作两次固件回归。
