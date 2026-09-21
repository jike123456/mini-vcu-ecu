# Host C coverage (generated)

2026-09-21T03:12:28.281249+00:00

Production sources are copied byte-for-byte for Windows path compatibility.
Only the five listed modules are in the denominator; test/stub code is excluded.
Host execution does not measure ARM hardware, ISR concurrency or MC/DC.

| Module | Lines hit/total | Line % | Branch outcomes hit/total | Branch % |
|---|---:|---:|---:|---:|
| vehicle_can.c | 56/56 | 100.00 | 32/32 | 100.00 |
| vehicle_state.c | 66/66 | 100.00 | 29/30 | 96.67 |
| dtc_manager.c | 116/116 | 100.00 | 52/56 | 92.86 |
| uds_server.c | 311/311 | 100.00 | 156/162 | 96.30 |
| pid.c | 30/30 | 100.00 | 18/18 | 100.00 |
| TOTAL | 579/579 | 100.00 | 287/298 | 96.31 |

```text
CAN CRC / counter / malformed frames: PASS
State transitions / fatal priority / recovery: PASS
DTC 3-fail / 10-pass thresholds / freeze / restore / clear: PASS
PID limits / anti-windup / derivative / rounding: PASS
UDS NRC / ISO-TP / sessions / routine / reset ordering: PASS
HOST CORE REGRESSION PASS: 857 checks (including critical-section balance)
```

Gate: lines >= 90.0% and branches >= 80.0%: PASS
