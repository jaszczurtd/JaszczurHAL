# JaszczurHAL feature overview

This document is a high-level inventory of what JaszczurHAL offers. It is meant
as a compact feature map, not an API reference. For function signatures,
configuration details and module contracts, see [JaszczurHAL_API.md](JaszczurHAL_API.md).
For the target-selectable VS Code firmware project model, see
[FwProjectWorkflow.md](FwProjectWorkflow.md). For native RP OTA provisioning
and operation, see [OTAWorkflow.md](OTAWorkflow.md).

## Platform and build capabilities

| Area | What it offers | Source |
|---|---|---|
| RP2040 / RP2350 backend | Official Pico SDK backend for RP2040, RP2350 ARM, and RP2350 Hazard3 RISC-V, with exact chip and ISA selection. | [RP backend](../src/hal/impl/rp2040/), [native build](../rp_native_lib/) |
| Board profiles and runtime capabilities | Generated Pico, Pico W, Pico 2, Pico 2 W, Pico+PIM730, Waveshare RP2040-LoRa-LF, Pico+Core1262-HF, NUCLEO-G474RE, Nucleo+Core1262-HF, and host profiles, with runtime state for USB, CYW43, and external radio hardware. | [board registry](../boards/README.md), [hal_board.h](../src/hal/system/hal_board.h) |
| STM32G474 backend | Bare-metal and FreeRTOS STM32G474 backend with startup/runtime glue, linker support, coordinated flash services, native peripherals, and optional CYW43-over-gSPI networking. | [STM32G474 backend](../src/hal/impl/stm32g474/) |
| Mock backend | Deterministic host backend for unit tests and simulation-oriented development without hardware. | [mock backend](../src/hal/impl/.mock/) |
| Compile-time opt-in modules | Optional features are selected with `HAL_ENABLE_*` flags and pull in only their dependencies. | [hal_config.h](../src/hal/core/hal_config.h) |
| Compiler portability layer | One header resolves the compiler extensions the HAL depends on - noreturn, forced inline, trap/unreachable, structure packing and leading-zero count - across GNU, Clang and MSVC. | [hal_compiler.h](../src/hal/core/hal_compiler.h) |
| Portable app entry | Common `app_start()` / `app_task0()` / optional `app_task1()` model across supported targets and examples, including HAL-owned `main()` and opt-in RP core-1 startup. | [hal_app.h](../src/hal/core/hal_app.h) |
| FreeRTOS integration | Pinned upstream kernel with native RP2040/RP2350 SMP ports, STM32G474 Cortex-M4F port, FreeRTOS-aware mutex/delay/runtime reporting and HAL-owned application-task startup. | [RP FreeRTOS glue](../src/hal/impl/rp2040/freertos/), [STM32 FreeRTOS glue](../src/hal/impl/stm32g474/freertos/) |
| Dispatcher-backed firmware projects | Shared VS Code/CMake workflow for generated projects, migrated downstream modules and checked-in examples, with target/board selection and per-target CMake cache isolation. | [FwProjectWorkflow.md](FwProjectWorkflow.md) |
| Static library builds | Dedicated CMake/helper flows for official Pico SDK RP and STM32G474 builds; the RP flow also verifies ELF/BIN/UF2 generation and application-entry symbols. | [RP build](../rp_native_lib/), [STM32 build](../stm32_lib/) |
| Validation gate | Full local gate for unit tests, Valgrind, static analysis, target builds and examples. | [runalltests.sh](../runalltests.sh) |

## Core HAL

| Area | What it offers | Source |
|---|---|---|
| GPIO | Portable digital I/O, pull modes and interrupts, including explicit IRQ core ownership and diagnostics on multicore targets. | [hal_gpio.h](../src/hal/gpio/hal_gpio.h) |
| ADC | Portable analog input abstraction. | [hal_adc.h](../src/hal/analog/hal_adc.h) |
| DAC | True DAC output with additional diagnostics on STM32G474 and the host mock; targets without DAC hardware report `HAL_EUNSUPPORTED`. | [hal_dac.h](../src/hal/analog/hal_dac.h) |
| PWM | Portable PWM output plus frequency-controlled PWM helpers. | [hal_pwm.h](../src/hal/gpio/hal_pwm.h), [hal_pwm_freq.h](../src/hal/gpio/hal_pwm_freq.h) |
| Pulse counting | Edge/pulse counting for signal measurement and simple counter applications. | [hal_pcnt.h](../src/hal/analog/hal_pcnt.h) |
| Timers and system time | Basic timers, extended timer helpers, idle/delay, watchdog, unique device ID and crash/fault diagnostics with target fault handlers. | [hal_timer.h](../src/hal/timers/hal_timer.h), [hal_system.h](../src/hal/system/hal_system.h) |
| Synchronization | Mutexes and critical sections with target-specific implementations. | [hal_sync.h](../src/hal/system/hal_sync.h) |
| Soft timers | Lightweight cooperative software timers. | [hal_soft_timer.h](../src/hal/timers/hal_soft_timer.h) |
| Utility primitives | Bit helpers, math helpers, PID control, watchdog support and common utility APIs. | [tools.h](../src/tools.h), [utils](../src/utils/) |

## Communication and connectivity

| Area | What it offers | Source |
|---|---|---|
| UART | Hardware serial communication abstraction; RP2040 RX IRQ affinity follows the core that starts the UART. | [hal_uart.h](../src/hal/serial/hal_uart.h) |
| USB device / CDC | Status-first USB lifecycle and CDC API with native RP TinyUSB ownership, descriptors, background pumping, bounded backpressure, 1200-bps BOOTSEL reset and a host mock. | [hal_usb.h](../src/hal/usb/hal_usb.h) |
| Serial/debug console | One TX-serialized shared core with streamed task-context formatting, ISR-deferred logs, per-source error rate limiting, net-console mirroring and link-time RP USB CDC, STM32 USART2/stdout or mock capture/RX ports. | [hal_serial.h](../src/hal/serial/hal_serial.h), [serial API](api/08_sync_serial.md) |
| Software serial | Target-optimized software UART: native Pico SDK PIO/DMA on RP2040 and a shared HAL GPIO backend on other targets. | [hal_swserial.h](../src/hal/serial/hal_swserial.h) |
| I2C master | Portable i2c controller API with two-bus support, atomic helpers, bus recovery and a bounded 7-bit scanner accepting a watchdog/progress callback. | [hal_i2c.h](../src/hal/i2c/hal_i2c.h) |
| I2C slave | Target-mode/register-map style I2C support. | [hal_i2c_slave.h](../src/hal/i2c/hal_i2c_slave.h) |
| SPI | Portable SPI master/controller API plus target-neutral per-device bus/CS/settings descriptors, including status-returning transfer APIs and blocking/asynchronous DMA-capable write paths where supported. | [hal_spi.h](../src/hal/spi/hal_spi.h), [hal_spi_device.h](../src/hal/spi/hal_spi_device.h) |
| Raw LoRa radio | Provider-neutral lifecycle for validated SX1262 hardware plus experimental, software-only SX1261, SX1276 and SX1278 integrations. Includes asynchronous TX/RX/CAD, current RSSI, capabilities, callbacks, cancellation, packet metadata, diagnostics, power states and time-on-air; SX126x also exposes explicit band-aware calibration. | [LoRa radio API](api/21_lora.md), [hal_lora_radio.h](../src/hal/radio/hal_lora_radio.h) |
| Reliable LoRa link | Private addressed messages over one raw LoRa handle with 32-bit sequences, ACK and bounded retransmission, duplicate suppression, transparent fragmentation, complete-message CRC and optional ChaCha20-Poly1305 protection. | [LoRa link API](api/22_lora_link.md), [hal_lora_link.h](../src/hal/radio/hal_lora_link.h) |
| Network status API | Additive `hal_status_t` operations for WiFi/DNS, TCP/UDP, MQTT and WireGuard with legacy wrappers preserved and exact absent/inactive/failed board-hardware status mapping. | [connectivity API](api/15_connectivity.md) |
| CAN facade | Backend-selectable CAN surface for classic CAN and CAN FD-capable backends. | [hal_can.h](../src/hal/can/hal_can.h) |
| MCP2515 CAN | Shared SPI CAN backend. | [mcp2515 driver](../src/hal/can/mcp2515/) |
| MCP2517FD/MCP2518FD CAN FD | Shared SPI CAN FD backend. | [mcp251xfd driver](../src/hal/can/mcp251xfd/) |
| STM32G474 native FDCAN | Native STM32G474 FDCAN backend. | [STM32 FDCAN backend](../src/hal/impl/stm32g474/hal_can_stm32g474_fdcan.cpp) |
| MFRC522 RFID | Shared RFID reader driver over HAL SPI/I2C. | [hal_mfrc522.h](../src/hal/nfc/hal_mfrc522.h), [mfrc522 driver](../src/hal/nfc/mfrc522/) |
| PN532 NFC/RFID | Shared NFC/RFID reader driver over HAL SPI/I2C/UART. | [hal_pn532.h](../src/hal/nfc/hal_pn532.h), [pn532 driver](../src/hal/nfc/pn532/) |
| WiFi | CYW43/lwIP connectivity on Pico W, Pico 2 W, Pico+PIM730, and configured STM32G474+PIM730 hardware. | [hal_wifi.h](../src/hal/network/hal_wifi.h) |
| BLE Peripheral and Observer (experimental) | One Peripheral connection, copied legacy advertising, passive scanning with bounded copied reports and AD parsing, static GAP/GATT services, ATT MTU reporting, and a host mock on Pico W and STM32G474+PIM730. | [Bluetooth API](api/20_bluetooth.md) |
| UDP | Handle-based multi-socket UDP transport plus legacy single-socket compatibility wrapper for WiFi builds. | [hal_udp.h](../src/hal/network/hal_udp.h) |
| TCP sockets | Handle-based TCP client sockets and listener/server handles with connect, bind/listen/accept, send/recv, shutdown and mock/RP-family backends. | [hal_tcp.h](../src/hal/network/hal_tcp.h) |
| JH BLE Stream v1 (experimental) | Bounded byte stream over one static GATT service, versioned framing with capability negotiation, mutual HMAC-SHA256 proofs, directional ChaCha20-Poly1305 keys, replay and rate-limit protection, and bounded RX/TX queues. | [Bluetooth API](api/20_bluetooth.md) |
| HTTP server | Small poll-driven HTTP/1.1 server over HAL TCP with exact/prefix routes, request headers, buffered responses, automatic `Content-Length` and mock-testable request handling. | [hal_http_server.h](../src/hal/network/http/hal_http_server.h) |
| HTTP files | Callback-backed static file serving, ETag/`If-None-Match`, raw PUT and multipart upload helpers over HAL HTTP routes. | [hal_http_files.h](../src/hal/network/http/hal_http_files.h) |
| WebSocket server | Small poll-driven WebSocket server over HAL TCP with HTTP Upgrade handshake, callbacks, send helpers and broadcast. | [hal_websocket.h](../src/hal/network/websocket/hal_websocket.h) |
| Net console | Password-protected TCP console that mirrors `hal_serial`/debug output to authenticated clients while preserving local UART/USB logs, plus bidirectional command input. | [hal_net_console.h](../src/hal/network/net_console/hal_net_console.h) |
| Network commands | Shared JSON/text command dispatcher for HTTP and WebSocket control channels, backed by cJSON and `hal_status_t` responses. | [hal_net_commands.h](../src/hal/network/net_commands/hal_net_commands.h) |
| BSD sockets adapter | Minimal IPv4 `sys/socket.h` / `netinet/in.h` / `arpa/inet.h` / `netdb.h` compatibility layer over HAL UDP/TCP handles, including `getaddrinfo()`, `setsockopt()`, `O_NONBLOCK`, `MSG_DONTWAIT` and `select()` readiness. | [socket.h](../src/sys/socket.h), [netdb.h](../src/netdb.h) |
| TLS | Provider-neutral TLS client API backed by BearSSL and native HAL TCP, with trust anchors, time/entropy callbacks, cancellation, bounded polling and an optional BSD-socket transport bridge. | [hal_tls.h](../src/hal/network/tls/hal_tls.h), [BearSSL transport](../src/hal/network/tls/BearSSL/) |
| HTTP/HTTPS client | Bounded one-shot HTTP/1.1 requests over HAL TCP or verified BearSSL TLS, with caller-owned headers/body and explicit response metadata. | [hal_http_client.h](../src/hal/network/http/hal_http_client.h), [connectivity API](api/15_connectivity.md#halhttpclient-httphttps-client-opt-in-halenablehttpclient) |
| MQTT | PubSubClient-based MQTT connectivity wrapper. | [hal_mqtt.h](../src/hal/network/mqtt/hal_mqtt.h) |
| OTA | Authenticated RP firmware updates over HAL UDP/TCP with versioned images, resumable program/staging swap, boot confirmation, rollback, discovery, and VS Code upload. | [hal_ota.h](../src/hal/network/ota/hal_ota.h), [OTA workflow](OTAWorkflow.md) |
| Calendar / NTP / time-of-day | Always-available Gregorian conversion, CET/CEST adjustment, range and minute helpers, plus a shared thread-safe NTP state machine with bounded primary/secondary fallback for connected builds. | [hal_time.h](../src/hal/time/hal_time.h) |
| WireGuard | Shared host-lwIP WireGuard integration with split/full tunnel routing on capability-advertised backends. | [hal_wireguard.h](../src/hal/network/wireguard/hal_wireguard.h), [WireGuard engine](../src/hal/network/wireguard/core/) |
| Cellular modem | Generic AT-command modem engine plus SimCom A76xx family support. | [hal_modem_at.h](../src/hal/modem/hal_modem_at.h), [hal_simcom_a76xx.h](../src/hal/modem/hal_simcom_a76xx.h) |

## Storage, files and logging

| Area | What it offers | Source |
|---|---|---|
| Flash transaction coordinator | Single internal coordinator for all native flash mutations: serializes callers, makes the other core safe, pauses TinyUSB, rejects XIP-resident callbacks and active DMA, applies bounded timeouts and restores runtime state. EEPROM/KV, LittleFS and OTA staging route through it on native RP; STM32G474 uses its coordinated flash services. | [rp flash drivers](../src/hal/impl/rp2040/drivers/flash/), [storage API](api/14_storage.md) |
| EEPROM abstraction | One provider-dispatched persistent-storage facade with shared locking/range behavior, a portable AT24C256 driver, target flash providers, a host-memory provider, and status-returning (`hal_status_t`) APIs. | [hal_eeprom.h](../src/hal/storage/hal_eeprom.h) |
| Key-value storage | Small persistent key-value layer on top of EEPROM-style storage, including status-returning (`hal_status_t`) get/set/commit APIs. | [hal_kv.h](../src/hal/storage/hal_kv.h) |
| LittleFS | Lightweight filesystem lifecycle/helpers, including status-returning (`hal_status_t`) mount/format/path APIs; native RP and STM32G474 use linker-reserved internal flash partitions. | [hal_littlefs.h](../src/hal/storage/hal_littlefs.h) |
| FatFs / SD over SPI | Exact-commit FatFs R0.16 checkout and shared SD-over-SPI disk I/O. | [filesystem framework](../src/hal/storage/filesystem/) |
| SD logger | SD-card logging and crash-report logging support. | [hal_sdlogger.h](../src/hal/storage/hal_sdlogger.h), [sdlogger](../src/hal/storage/filesystem/sdlogger/) |

## Sensors, input devices and timekeeping

| Area | What it offers | Source |
|---|---|---|
| RTC facade | One target-independent real-time-clock facade with provider-dispatched chip/mock backends, status-returning (`hal_status_t`) datetime/epoch/alarm/timer APIs, shared lifecycle/locking, and Gregorian validation. | [hal_rtc.h](../src/hal/rtc/hal_rtc.h), [hal_rtc.cpp](../src/hal/rtc/hal_rtc.cpp), [providers](../src/hal/rtc/), [calendar core](../src/hal/time/) |
| PCF8563 RTC | Shared I2C PCF8563 backend. | [pcf8563 driver](../src/hal/rtc/pcf8563/) |
| DS3231 RTC | Shared I2C DS3231 backend. | [ds3231 driver](../src/hal/rtc/ds3231/) |
| GPS / NMEA | One target-independent GPS facade with compile-time HAL UART/SoftwareSerial selection, a shared mutex-protected NMEA engine and deterministic mock injection. RP UART retains IRQ/core-affinity guidance. | [hal_gps.h](../src/hal/gps/hal_gps.h), [hal_gps.cpp](../src/hal/gps/hal_gps.cpp), [GPS framework](../src/hal/gps/) |
| Thermocouple facade | One target-independent, provider-dispatched facade owns lifecycle, locking, validation, chip capabilities and deterministic mock injection for MCP9600/MCP9601 and MAX6675. | [hal_thermocouple.h](../src/hal/temperature/hal_thermocouple.h), [facade/providers](../src/hal/temperature/) |
| MCP9600/MCP9601 | Shared I2C thermocouple amplifier driver. | [mcp9600 driver](../src/hal/temperature/mcp9600/) |
| MAX6675 | Shared GPIO bit-banged thermocouple converter driver. | [max6675 driver](../src/hal/temperature/max6675/) |
| DS18B20 | Shared 1-Wire digital temperature sensor support. | [hal_ds18b20.h](../src/hal/temperature/hal_ds18b20.h), [ds18b20 driver](../src/hal/temperature/ds18b20/) |
| DHT11/DHT22 | Shared GPIO temperature and humidity sensor driver. | [hal_dht.h](../src/hal/temperature/hal_dht.h), [dht driver](../src/hal/temperature/dht/) |
| 1-Wire bus | Generic shared 1-Wire bus wrapper/driver. | [hal_onewire.h](../src/hal/onewire/hal_onewire.h), [onewire driver](../src/hal/onewire/) |
| BH1750 | Shared I2C ambient-light sensor driver. | [hal_bh1750.h](../src/hal/sensors/hal_bh1750.h), [bh1750 driver](../src/hal/sensors/bh1750/) |
| ADP5360 PMIC | Shared I2C PMIC driver with charger control, fuel-gauge readings, shipment/reset helpers and buck/buckboost regulator configuration. | [hal_adp5360.h](../src/hal/power/hal_adp5360.h), [adp5360 driver](../src/hal/power/adp5360/) |
| MCP3221 | Shared I2C 12-bit ADC driver. | [hal_mcp3221.h](../src/hal/analog/hal_mcp3221.h), [simple I/O drivers](../src/hal/gpio/simple_io/) |
| ADS1X15 / ADS1115 | Shared external ADC driver over I2C. | [hal_external_adc.h](../src/hal/analog/hal_external_adc.h), [ads1x15 driver](../src/hal/analog/ads1x15/) |
| TSC2007 touch | Shared I2C resistive touch controller driver. | [hal_tsc2007.h](../src/hal/input/hal_tsc2007.h), [tsc2007 driver](../src/hal/input/tsc2007/) |
| STMPE610 touch | Shared I2C/SPI resistive touch controller driver. | [hal_stmpe610.h](../src/hal/input/hal_stmpe610.h), [stmpe610 driver](../src/hal/input/stmpe610/) |
| Infrared receiver decoding | Shared GPIO/timing based IR receiver decoder. | [hal_irsmall_decoder.h](../src/hal/input/hal_irsmall_decoder.h), [IR framework](../src/hal/input/irsmall_decoder/) |

## Displays, indicators and output devices

| Area | What it offers | Source |
|---|---|---|
| Generic display facade | Common drawing/display surface for TFT, RGB OLED and monochrome backends, including runtime capabilities, status-returning raw area writes, RGB565 streaming/DMA paths and drawing/text APIs where advertised. | [hal_display.h](../src/hal/display/hal_display.h) |
| GFX engine and fonts | Shared graphics primitives and bundled bitmap fonts. | [display drivers](../src/hal/display/drivers/) |
| ILI9341 TFT | SPI TFT display backend. | [ili9341 driver](../src/hal/display/drivers/ili9341_driver.h) |
| ST7735/ST7789/ST7796S/GC9A01 TFT | Shared ST77xx-family SPI TFT backend, including GC9A01 round TFT init/rotation support. | [st77xx driver](../src/hal/display/drivers/st77xx_driver.h) |
| SSD1306-family OLED | OLED backend for `SSD1306`, `SSD1309`, `SSD1315`, `SH1106` and `CH1115` over HAL I2C/SPI. | [ssd1306 driver](../src/hal/display/drivers/ssd1306_driver.h) |
| SSD1331/SSD135x RGB OLED | Public `hal_display` RGB565 backend over HAL SPI/GPIO with raw writes, streaming and GFX primitives; ported from Zephyr display-driver behavior. | [hal_display.h](../src/hal/display/hal_display.h) |
| ST7567 LCD | Public `hal_display` raw MONO01/MONO10 backend over HAL I2C or SPI/GPIO with page-layout capabilities; ported from Zephyr display-driver behavior. | [hal_display.h](../src/hal/display/hal_display.h) |
| SSD16xx / UC81xx EPD | Shared SPI/GPIO monochrome e-paper drivers for SSD1608/SSD1673/SSD1675A/SSD1680/SSD1681 and UC8175/UC8176/UC8151D/UC8179, with BUSY timeouts, full/partial LUT profiles, deferred frame refresh and raw MONO10 facade capabilities. | [hal_display.h](../src/hal/display/hal_display.h) |
| HD44780 LCD | Parallel character LCD support over HAL GPIO/timing. | [hal_hd44780.h](../src/hal/display/hal_hd44780.h), [hd44780 driver](../src/hal/display/hd44780/) |
| RGB / NeoPixel status LED | Shared NeoPixel-style RGB LED support with target-specific transport. | [hal_rgb_led.h](../src/hal/gpio/hal_rgb_led.h), [neopixel driver](../src/hal/gpio/neopixel/) |
| Digital potentiometer facade | Common API for I2C digital potentiometers. | [hal_digipot.h](../src/hal/analog/hal_digipot.h) |
| MCP4017/4018/4019 | Shared I2C digital potentiometer backend. | [digipot drivers](../src/hal/analog/digipot/) |
| MAX5395 | Shared I2C digital potentiometer backend. | [digipot drivers](../src/hal/analog/digipot/) |
| PGA2311 audio volume | Shared SPI/GPIO stereo volume controller driver. | [hal_pga2311.h](../src/hal/audio/hal_pga2311.h), [pga2311 driver](../src/hal/audio/pga2311/) |
| MCP23017 / PCA9654E / PCF8574 / 74HC595 / MCP4725 | Shared simple I/O expander and DAC drivers over HAL I2C/SPI/GPIO. | [simple I/O drivers](../src/hal/gpio/simple_io/) |
| DACless PWM audio | Shared full-duplex PWM-audio engine with DMA and polling paths and block/sample callbacks. The RP backend adds free-running round-robin ADC capture over DMA, so per-sample callbacks read microphone/analog inputs at the audio rate in the same clock domain as playback. | [hal_dacless.h](../src/hal/audio/hal_dacless.h), [hal_dma_pwm_audio.h](../src/hal/audio/hal_dma_pwm_audio.h), [dacless driver](../src/hal/audio/dacless/) |

## Crypto, media and bundled libraries

| Area | What it offers | Source |
|---|---|---|
| Crypto helpers | Base64, MD5, SHA-256, HMAC-SHA256 and ChaCha20/Poly1305-oriented helpers. | [hal_crypto.h](../src/hal/security/hal_crypto.h), [wireguard crypto](../src/hal/network/wireguard/core/crypto/) |
| CRC checksums | Generic CRC-8/16/32 for data integrity: CRC-8/MAXIM, Maxim 1-Wire CRC-16, CRC-16/CCITT-FALSE and CRC-32/ISO-HDLC. | [hal_crc.h](../src/hal/security/hal_crc.h) |
| Session authentication helpers | Optional serial-session authentication support. | [hal_sc_auth.h](../src/hal/security/hal_sc_auth.h) |
| Serial framing/session tools | Reusable serial frame/session vocabulary helpers for embedded protocols. | [hal_serial_frame.h](../src/hal/serial/hal_serial_frame.h), [hal_serial_session.h](../src/hal/serial/hal_serial_session.h) |
| cJSON | Managed cJSON/cJSON_Utils for constrained JSON work. | [cJSON API](api/17_cJSON.md) |
| PNG | Managed memory-oriented LodePNG support plus optional Base64 helpers. | [LodePNG API](api/18_LodePNG.md) |
| JPEG | Managed TJpgDec baseline JPEG decoding to RGB565 plus optional Base64 helpers. | [JPEG API](api/19_JPEG.md) |
| Unity | Managed Unity 2.5.4 framework for host and target-side tests. | [Unity pin](../third_party/unity_version.conf) |

## Examples and documentation

| Area | What it offers | Source |
|---|---|---|
| Portable examples | Buildable example applications covering core, sensors, displays, connectivity, storage and media modules. | [examples](../examples/) |
| API reference | Detailed module contracts, signatures and backend notes. | [doc/api](api/) |
| Firmware project workflow | Manifest, target/board selection, source discovery, upload/debug-build behavior and generated files for dispatcher-backed projects. | [FwProjectWorkflow.md](FwProjectWorkflow.md) |
| Native RP OTA workflow | Firmware integration, build and first flash, VS Code upload, firewall, confirmation, rollback and recovery. | [OTAWorkflow.md](OTAWorkflow.md) |
| Local datasheets | Local reference PDFs and notes for supported hardware. | [datasheets](datasheets/) |
