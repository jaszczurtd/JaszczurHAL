# 10 - Storage

This example exercises three independent storage paths:

- a flash-backed EEPROM and KV store,
- a LittleFS flash partition,
- SDLogger over SPI, including a one-shot boot report.

The KV path writes and reads back the device-name blob. After mounting
LittleFS, the example checks for `/hal_marker.txt` and removes it when present,
which exercises the path-query and removal facades without creating arbitrary
files.

Failure of one path does not stop the others. The EEPROM is 1024 bytes at
runtime. SDLogger owns bytes 0-7 and the KV store uses bytes 64-575, so their
records cannot overlap. Native builds reserve separate physical flash regions:
the default 4 KiB EEPROM region and a 64 KiB LittleFS region.

The SD card uses SPI0. RP targets use MISO/MOSI/SCK/CS GPIO 16/19/18/17;
STM32G474 uses PA6/PA7/PA5/PA4. Failed SDLogger operations close the current log
through the public API and retry initialization every five seconds. A failed
boot-report write is cleaned up and retried independently.

LittleFS formatting is disabled by default so a mount error cannot erase an
existing partition. Set `EXAMPLE_STORAGE_ALLOW_LITTLEFS_FORMAT=1` in
`hal_project_config.h` (or as a compiler definition) only when erasing the
reserved LittleFS partition is explicitly acceptable.
