<a id="10---storage"></a>

# 10 - Storing data in flash and on an SD card

This example writes and reads a key-value (KV) store, mounts a LittleFS
partition, and logs to an SD card through SDLogger. Errors are handled separately, but SDLogger needs initialized EEPROM and
SPI. LittleFS does not depend on successful KV initialization.

The KV example erases the spare bank at startup with `hal_kv_prepare_ex()`,
increments a persistent boot counter, writes a binary device-name record,
commits the changes, and verifies the name read back.
After mounting LittleFS, the application checks for `/hal_marker.txt`
and removes it if present, without creating new files. SDLogger writes
a log and a one-time boot report.

## Data layout

On RP and STM32, the application passes `0u` as the size to
`hal_eeprom_init()`, leaving size selection to HAL configuration. KV starts
at `KV_BASE_ADDR=0u`; its size is `HAL_RP_FLASH_EEPROM_SIZE` or
`HAL_STM32_FLASH_EEPROM_SIZE`, respectively. The fallback branch sets both
EEPROM and KV size to 8192 bytes.

The LittleFS partition layout and SDLogger persistent data depend on HAL
configuration and implementation. Check both before reserving additional
memory regions for application data.

## SD card

The card uses SPI bus 0. `device_tree.json` describes the wiring of every
assembly, and the program takes the bus, its pins and the card's chip select
from the generated header. The table below repeats that wiring.

| SD card signal | Pico / Pico 2 | NUCLEO-G474RE |
|---|---|---|
| MISO | GPIO16 | PA6, CN10 pin 13 / D12 |
| MOSI | GPIO19 | PA7, CN10 pin 15 / D11 |
| SCK | GPIO18 | PB3, CN10 pin 31 / D3 |
| CS | GPIO17 | PB6, CN10 pin 17 / D10 |

On NUCLEO-G474RE the SPI clock uses PB3 rather than D13 (PA5), because PA5
drives the LD2 LED.

After an SDLogger operation fails, the application closes the current log
through the public API and retries initialization every five seconds.
A failed boot-report write is handled separately: its file is closed before
the next attempt.

## LittleFS formatting

Formatting is disabled by default so a mount error cannot erase existing
data. Set `EXAMPLE_STORAGE_ALLOW_LITTLEFS_FORMAT=1` in
`hal_project_config.h`, directly or through a variant, only when erasing the
reserved LittleFS partition is acceptable.
