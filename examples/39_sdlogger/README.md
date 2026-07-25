# 39_sdlogger

Portable SD-card logger example for native RP2040/RP2350 and STM32G474.

The example uses `HAL_ENABLE_SDLOGGER`, which pulls in the shared FatFs SD
file layer and the HAL SPI-backed SD disk I/O. It initialises target-native
flash EEPROM for log counters, starts SPI0, opens the periodic SD log, writes a
short boot crash report, and then appends one line per second.

Default wiring:

| Target | SPI bus | MISO | MOSI | SCK | CS |
|---|---:|---:|---:|---:|---:|
| RP2040 | 0 | GP16 | GP19 | GP18 | GP17 |
| STM32G474 | SPI1 | PA6 | PA7 | PA5 | PA4 |

Build one target:

```bash
../../vscode/entry/jh-vscode build --project . --target rp2040
../../vscode/entry/jh-vscode build --project . --target rp2350-arm
../../vscode/entry/jh-vscode build --project . --target rp2350-riscv
../../vscode/entry/jh-vscode build --project . --target stm32g474
```

Generated filenames stay in FatFs 8.3 form because LFN is disabled:
`logNNNNN.txt` for the periodic log and `wdNNNNNN.txt` for crash reports.
