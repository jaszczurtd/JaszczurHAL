<a id="jaszczurhal-feature-overview"></a>

# JaszczurHAL capabilities

*Also available in [Polish](../pl/features.md).*

JaszczurHAL provides a common C API for embedded applications across multiple platforms. This overview describes the available features and their main limitations. For signatures, configuration, and usage requirements, see the [API reference](JaszczurHAL_API.md).

For creating and building VS Code projects, see [FwProjectWorkflow.md](FwProjectWorkflow.md). For OTA setup and operation on RP and ESP32-S3, see [OTAWorkflow.md](OTAWorkflow.md).

Use the public JaszczurHAL API in application code and reusable drivers to preserve portability. The implementation for the selected platform handles Pico SDK, ESP-IDF, and hardware registers. Direct Pico SDK or ESP-IDF calls are still possible when you need platform-specific features, but they bypass the HAL and tie that part of the code to one platform.

<a id="portable-api-platforms-and-build-capabilities"></a>

## Platforms, configuration, and builds

| Area | What it offers | Source |
|---|---|---|
| Common C API | Applications and portable drivers use the same public C `hal_*` API. The selected platform determines the implementation; board capabilities and return statuses let applications account for hardware differences. | [public HAL](../../src/hal/), [API reference](JaszczurHAL_API.md) |
| Board profiles | Profiles for RP2040, RP2350, STM32G474, ESP32, ESP32-S3, and the host are generated from the board registry. Supported configurations let applications query board information at runtime. | [board registry](../../boards/README.md), [hal_board.h](../../src/hal/system/hal_board.h) |
| RP2040 / RP2350 | Support for RP2040, RP2350 ARM, and RP2350 Hazard3 RISC-V, with explicit chip and instruction-set architecture (ISA) selection and optional FreeRTOS. The implementation and native build use the official Pico SDK, while application code uses the JaszczurHAL API. | [RP backend](../../src/hal/impl/rp2040/), [native build](../../rp_native_lib/) |
| STM32G474 | Support for STM32G474 in bare-metal or FreeRTOS configurations. The implementation includes startup code, a linker script, coordinated flash access, native peripherals, and optional networking through CYW43 over gSPI. | [STM32G474 backend](../../src/hal/impl/stm32g474/) |
| ESP32 family | A common JaszczurHAL API built on ESP-IDF. ESP32-S3 implements core modules, peripherals, network services, base BLE through NimBLE, and OTA. For ESP32 and ESP32-S3, project tools generate board and memory configuration and integrate builds, upload, monitoring, and IntelliSense. | [ESP32 implementation](../../src/hal/impl/esp32/), [ESP32-S3 compile fixture](../../tests/fixtures/esp32s3_phase3/) |
| Host testing (mock) | A deterministic implementation of the public JaszczurHAL API for host unit tests and portable development without target hardware. | [mock backend](../../src/hal/impl/.mock/) |
| Optional modules | The `HAL_ENABLE_*` flags select which features and dependencies are included at build time. | [hal_config.h](../../src/hal/core/hal_config.h) |
| Compiler portability | A common header provides consistent access to GNU, Clang, and MSVC extensions: atomic operations and memory ordering, noreturn, forced inline, trap/unreachable, structure packing, and leading-zero count. | [hal_compiler.h](../../src/hal/core/hal_compiler.h) |
| Application entry points | A common `app_start()` / `app_task0()` / optional `app_task1()` model across supported platforms. HAL supplies `main()`, and ESP-IDF supplies `app_main()`. RP also supports opt-in startup on core 1. | [hal_app.h](../../src/hal/core/hal_app.h) |
| FreeRTOS integration | FreeRTOS-aware mutexes, delays, runtime diagnostics, and application-task startup managed by HAL. RP2040 and RP2350 use native SMP ports, and STM32G474 uses the Cortex-M4F port of a pinned FreeRTOS-Kernel release. ESP32-S3 uses the scheduler supplied by the pinned ESP-IDF version. | [portable app entry](../../src/hal/core/hal_app.h), [module flags](../api/en/02_module_flags.md) |
| Stack protection | Independent options enable synchronous stack-boundary guards through Pico SDK, MPU, or ESP-IDF and frame canaries through GCC/Clang `-fstack-protector-strong`, where supported. FreeRTOS configurations can also check task-stack boundaries. | [hal_system.h](../../src/hal/system/hal_system.h), [module flags](../api/en/02_module_flags.md) |
| VS Code projects | Create new projects or migrate existing ones to a common VS Code/CMake workflow. The generator and checked-in examples support platform and board selection, with a separate CMake cache for each platform. | [FwProjectWorkflow.md](FwProjectWorkflow.md) |
| Static libraries | CMake configurations and scripts build libraries for RP with the official Pico SDK and for STM32G474. The RP build also checks ELF/BIN/UF2 generation and application-entry symbols. | [RP build](../../rp_native_lib/), [STM32 build](../../stm32_lib/) |
| Project validation suite | Run unit tests, Clang ASan/UBSan/libFuzzer checks, Valgrind, static analysis, and builds for RP, STM32, and examples. The suite also includes a multi-feature ESP32-S3 configuration checked by compilation only, not on hardware. | [runalltests.sh](../../runalltests.sh) |

<a id="core-hal"></a>

## Core HAL features

| Area | What it offers | Source |
|---|---|---|
| GPIO | Read and write digital pins, configure pull modes, and handle interrupts. Multicore RP and ESP32-S3 support explicit IRQ core ownership and diagnostics. | [hal_gpio.h](../../src/hal/gpio/hal_gpio.h) |
| ADC | Read analog inputs through a common API that serializes access. ESP32-S3 oneshot ADC channels are selected from generated board pin masks. | [hal_adc.h](../../src/hal/analog/hal_adc.h) |
| DAC | Control hardware analog output on STM32G474 or test its behavior with the host mock, with additional diagnostics. Platforms without DAC hardware return `HAL_EUNSUPPORTED`. | [hal_dac.h](../../src/hal/analog/hal_dac.h) |
| PWM | Generate PWM output and control its frequency through dedicated helper functions. | [hal_pwm.h](../../src/hal/gpio/hal_pwm.h), [hal_pwm_freq.h](../../src/hal/gpio/hal_pwm_freq.h) |
| Pulse counting | Count edges or pulses for signal measurements and simple counters. | [hal_pcnt.h](../../src/hal/analog/hal_pcnt.h) |
| Hardware period capture | Hardware timestamps and 32-period frequency windows on RP, STM32G474, ESP32-S3, and mock. | [API](../api/en/24_pulse_capture.md) |
| Timers and system time | Basic and extended timers, idle handling, delays, a watchdog, and a unique device ID. Crash diagnostics use the fault handlers provided for each platform. | [hal_timer.h](../../src/hal/timers/hal_timer.h), [hal_system.h](../../src/hal/system/hal_system.h) |
| Low-power modes | CPU Sleep and STM32G474 STOP0, STOP1, and Standby, depending on board capabilities. The API supports RTC wake classification, clock restoration, callbacks, and monotonic-time compensation for RTC-timed transitions. | [hal_power.h](../../src/hal/power/hal_power.h), [power API](../api/en/06_timers_system.md#halpower-low-power-transitions-optional-halenablepowermanagement) |
| Synchronization | Mutexes and critical sections implemented for each platform. | [hal_sync.h](../../src/hal/system/hal_sync.h) |
| Software timers | Lightweight software timers serviced cooperatively by the application. | [hal_soft_timer.h](../../src/hal/timers/hal_soft_timer.h) |
| Utility functions | Array, math, text, pixel, and image operations; byte-order conversion; ADC/NTC conversion; and network and time utilities. PID and watchdog support are also available. | [HAL core](../../src/hal/core/), [HAL modules](../../src/hal/), [compatibility utils](../../src/utils/) |

## Communication and connectivity

| Area | What it offers | Source |
|---|---|---|
| UART | Hardware serial communication through a common API. On RP2040 and ESP32-S3, lifecycle operations are tied to the core that initialized the UART. | [hal_uart.h](../../src/hal/serial/hal_uart.h) |
| USB device / CDC | Start and stop USB/CDC with status-returning APIs. On RP, HAL handles TinyUSB, descriptors, background servicing, backpressure, and the 1200 bps reset into BOOTSEL. A host test implementation is also available. | [hal_usb.h](../../src/hal/usb/hal_usb.h) |
| Serial console and diagnostics | Output data and logs through RP USB CDC, ESP-IDF USB Serial/JTAG VFS, or STM32 USART2/stdout, selected at link time. The module serializes TX, formats data in task context, defers ISR logs, rate-limits errors by source, and mirrors output to the network console. Tests can use capture or sink transports. | [hal_serial.h](../../src/hal/serial/hal_serial.h), [serial API](../api/en/08_sync_serial.md) |
| Software serial | UART communication without a hardware UART controller. RP2040 uses native Pico SDK PIO/DMA; other platforms use a shared HAL GPIO implementation. | [hal_swserial.h](../../src/hal/serial/hal_swserial.h) |
| I2C controller | Two-bus support, atomic operations, and bus recovery. Optional 10-bit addressing is enabled by `HAL_ENABLE_I2C_10BIT`. The bounded 7-bit scanner accepts a callback for watchdog servicing or progress reporting. | [hal_i2c.h](../../src/hal/i2c/hal_i2c.h) |
| I2C target | Operate as an I2C target (slave) with a register map. | [hal_i2c_slave.h](../../src/hal/i2c/hal_i2c_slave.h) |
| SPI | Control an SPI bus and configure each device's bus, CS signal, and transfer settings. Operations return a status. Blocking and asynchronous writes are available, with DMA where supported. | [hal_spi.h](../../src/hal/spi/hal_spi.h), [hal_spi_device.h](../../src/hal/spi/hal_spi_device.h) |
| Commands and message format | Define and handle commands independently of the transport. The router applies source and security rules and supports binary-safe metadata and bounded replies. Versioned request, response, and event messages work with packet adapters and framed streams. | [command API](../api/en/23_commands.md), [hal_command_router.h](../../src/hal/commands/hal_command_router.h), [hal_command_wire.h](../../src/hal/commands/hal_command_wire.h) |
| LoRa radio | Asynchronous TX/RX/CAD, RSSI readings, operation cancellation, packet metadata, diagnostics, power states, and time-on-air calculation. The API exposes capabilities and callbacks; SX126x also provides band-specific calibration. SX1262 has been checked on hardware. SX1261, SX1276, and SX1278 remain experimental and hardware-unverified. | [LoRa radio API](../api/en/21_lora.md), [hal_lora_radio.h](../../src/hal/radio/hal_lora_radio.h) |
| LoRa link with acknowledgements | Exchange addressed messages using a private protocol over one LoRa radio handle. The link supports 32-bit sequence numbers, ACKs, bounded retries, duplicate suppression, automatic fragmentation, and whole-message CRC. Optional ChaCha20-Poly1305 protection is available. | [LoRa link API](../api/en/22_lora_link.md), [hal_lora_link.h](../../src/hal/radio/hal_lora_link.h) |
| LoRa commands | Send application-defined commands and return responses automatically over the LoRa link. Bounded queues own copies of their data, and handlers receive link metadata. | [command API](../api/en/23_commands.md), [hal_lora_commands.h](../../src/hal/radio/hal_lora_commands.h) |
| CAN and CAN FD | A common API for Classical CAN and CAN FD, with the implementation selected by configuration. | [hal_can.h](../../src/hal/can/hal_can.h) |
| MCP2515 CAN | Platform-independent support for the MCP2515 CAN controller over SPI. | [mcp2515 driver](../../src/hal/can/mcp2515/) |
| MCP2517FD/MCP2518FD CAN FD | Support for MCP2517FD and MCP2518FD CAN FD controllers over SPI. | [mcp251xfd driver](../../src/hal/can/mcp251xfd/) |
| STM32G474 FDCAN | Support for the built-in STM32G474 FDCAN controller. | [STM32 FDCAN backend](../../src/hal/impl/stm32g474/hal_can_stm32g474_fdcan.cpp) |
| MFRC522 RFID | Support for the MFRC522 RFID reader through HAL SPI or I2C. | [hal_mfrc522.h](../../src/hal/nfc/hal_mfrc522.h), [mfrc522 driver](../../src/hal/nfc/mfrc522/) |
| PN532 NFC/RFID | Support for the PN532 NFC/RFID reader through HAL SPI, I2C, or UART. | [hal_pn532.h](../../src/hal/nfc/hal_pn532.h), [pn532 driver](../../src/hal/nfc/pn532/) |
| WiFi | CYW43/lwIP networking on Pico W, Pico 2 W, Pico with a PIM730 module, and STM32G474 with PIM730. ESP32-S3 uses native ESP-IDF WiFi, `esp_netif`, and lwIP. | [hal_wifi.h](../../src/hal/network/hal_wifi.h) |
| BLE Peripheral and Observer | One Peripheral connection, legacy BLE advertising, and passive scanning. The module copies advertising data and scan reports, bounds the report queue, parses AD, provides static GAP/GATT services, and exposes ATT MTU. Implementations include CYW43/BTstack, base BLE through ESP32-S3 NimBLE, and a deterministic mock. | [Bluetooth API](../api/en/20_bluetooth.md) |
| Bluetooth Classic management | Device discovery (inquiry), copied results, pairing, SDP, and indexed access to stored devices, shared by optional Classic profiles. HID Host exposes raw descriptors and reports to separate adapters. Implementations use CYW43/BTstack or Bluedroid on the original ESP32. | [module flags](../api/en/02_module_flags.md), [Bluetooth API](../api/en/20_bluetooth.md) |
| Bluetooth Classic HID | Generic HID Host support without device-specific assumptions. The API exposes one copied descriptor and bounded raw Input/Output/Feature reports. | [hal_bluetooth_hid_host.h](../../src/hal/bluetooth/hal_bluetooth_hid_host.h) |
| Bluetooth gamepad | An optional Bluetooth Classic adapter returns normalized button, axis, and D-pad state. Input state is cleared on disconnect. | [hal_gamepad.h](../../src/hal/bluetooth/hal_gamepad.h) |
| Bluetooth A2DP Sink and AVRCP Target | Receive one SBC stream and output signed 16-bit PCM, with optional mono downmix. Support includes bounded buffers, shared Classic bonding, stream diagnostics, and absolute-volume control. | [Bluetooth API](../api/en/20_bluetooth.md#a2dp-sink-and-avrcp-target), [hal_bluetooth_a2dp_sink.h](../../src/hal/bluetooth/hal_bluetooth_a2dp_sink.h), [hal_bluetooth_avrcp_target.h](../../src/hal/bluetooth/hal_bluetooth_avrcp_target.h) |
| UDP | Exchange datagrams through multiple handle-based sockets. WiFi configurations also provide compatibility with the legacy single-socket API. | [hal_udp.h](../../src/hal/network/hal_udp.h) |
| TCP sockets | Handle-based client connections and listener/server sockets. The API covers connect, bind/listen/accept, send/recv, and shutdown, with mock, CYW43/lwIP, and native ESP-IDF lwIP implementations. | [hal_tcp.h](../../src/hal/network/hal_tcp.h) |
| JH BLE Stream v1 | Transfer a byte stream through one static GATT service with defined resource limits. The protocol supports versioned frames, capability negotiation, mutual HMAC-SHA256 authentication, separate directional ChaCha20-Poly1305 keys, replay protection, rate limiting, and bounded RX/TX queues. | [Bluetooth API](../api/en/20_bluetooth.md) |
| BLE Stream commands | Send commands and return responses automatically through one authenticated JH BLE Stream session owned exclusively by the adapter. The module fragments and reassembles messages to fit the MTU, dispatches requests to the command router, and exposes authenticated peer and session metadata. It closes the session if its state cannot be safely reconstructed. | [command API](../api/en/23_commands.md#authenticated-ble-stream-adapter), [hal_ble_commands.h](../../src/hal/bluetooth/hal_ble_commands.h) |
| HTTP server | An HTTP/1.1 server serviced by regular polling. It supports exact and prefix routes, request headers, automatic `Content-Length`, resumable partial TCP writes, and response and idle timeouts. Connections are unencrypted; the API does not provide an HTTPS server. | [hal_http_server.h](../../src/hal/network/http/hal_http_server.h) |
| Files over HTTP | Serve static files through callbacks, handle ETag/`If-None-Match`, and receive raw PUT or multipart uploads through HAL HTTP routes. | [hal_http_files.h](../../src/hal/network/http/hal_http_files.h) |
| WebSocket server | A WebSocket server over HAL TCP, serviced by polling. It provides the HTTP Upgrade handshake, callbacks, sending, and broadcasting. Connections are unencrypted; WSS and WebSocket-client APIs are not provided. | [hal_websocket.h](../../src/hal/network/websocket/hal_websocket.h) |
| Network console | A password-protected TCP console with bidirectional command communication. It mirrors `hal_serial` and diagnostic output to authenticated clients while preserving local UART/USB logs. | [hal_net_console.h](../../src/hal/network/net_console/hal_net_console.h) |
| Network commands | Handle text and JSON commands over HTTP and WebSocket. Adapters use cJSON and the shared command router while preserving the established network handler API. | [command API](../api/en/23_commands.md), [hal_net_commands.h](../../src/hal/network/net_commands/hal_net_commands.h) |
| BSD sockets compatibility | A minimal IPv4 layer exposing `sys/socket.h`, `netinet/in.h`, `arpa/inet.h`, and `netdb.h` through HAL UDP/TCP handles. It includes `getaddrinfo()`, `setsockopt()`, `O_NONBLOCK`, `MSG_DONTWAIT`, and `select()` readiness checks. | [socket.h](../../src/sys/socket.h), [netdb.h](../../src/netdb.h) |
| TLS client | TLS connections through BearSSL and native HAL TCP, using an implementation-independent API. Configuration includes trust anchors and callbacks that supply time and entropy. Cancellation, bounded polling, and an optional BSD sockets transport adapter are available. | [hal_tls.h](../../src/hal/network/tls/hal_tls.h), [BearSSL transport](../../src/hal/network/tls/BearSSL/) |
| HTTP/HTTPS client | Issue individual HTTP/1.1 requests with defined limits over HAL TCP or verified BearSSL TLS. The caller owns the header and body buffers, and the result includes explicit response metadata. | [hal_http_client.h](../../src/hal/network/http/hal_http_client.h), [connectivity API](../api/en/15_connectivity.md#halhttpclient-httphttps-client-opt-in-halenablehttpclient) |
| Notifications | Send notifications through a common API. The available Telegram Bot API implementation uses the HTTP/HTTPS client. | [hal_notify.h](../../src/hal/network/notify/hal_notify.h), [connectivity API](../api/en/15_connectivity.md#halnotify-notifications-opt-in-halenablenotify) |
| MQTT | MQTT connectivity through a PubSubClient-based client. | [hal_mqtt.h](../../src/hal/network/mqtt/hal_mqtt.h) |
| OTA updates | Update firmware over HAL UDP/TCP, with device discovery, trial boot, image confirmation, rollback, and VS Code upload. Optional AUTH2 password authentication rejects updates on authentication failure. RP uses a signed, versioned container and resumable image swap; ESP32-S3 uses a manifest-validated raw application image and ESP-IDF OTA partitions. | [hal_ota.h](../../src/hal/network/ota/hal_ota.h), [OTA workflow](OTAWorkflow.md) |
| Calendar, NTP, and wall-clock time | Always-available Gregorian calendar utilities and a shared, thread-safe wall clock. The clock exposes its source and status, restores time from RTC, preserves NTP state, and integrates with libc. It supports bounded fallback from a primary NTP server to one secondary server. | [hal_time.h](../../src/hal/time/hal_time.h) |
| WireGuard | WireGuard integration with host-lwIP, supporting split- and full-tunnel routing. | [hal_wireguard.h](../../src/hal/network/wireguard/hal_wireguard.h), [WireGuard engine](../../src/hal/network/wireguard/core/) |
| Cellular modem | A common AT-command module and support for the SimCom A76xx modem family. | [hal_modem_at.h](../../src/hal/modem/hal_modem_at.h), [hal_simcom_a76xx.h](../../src/hal/modem/hal_simcom_a76xx.h) |

## Storage, files and logging

| Area | What it offers | Source |
|---|---|---|
| Coordinated flash access | On RP, a shared coordinator serializes EEPROM/KV, LittleFS, and OTA staging operations. It makes the other core safe, pauses TinyUSB, rejects XIP-resident callbacks and active DMA, applies timeouts, and restores the previous state afterward. On STM32G474, one flash mutex serializes erase/program sequences for EEPROM/KV and LittleFS. | [RP flash drivers](../../src/hal/impl/rp2040/drivers/flash/), [storage API](../api/en/14_storage.md) |
| EEPROM storage | A common persistent-storage API with range checks, locking, and `hal_status_t` results. Implementations support AT24C256, platform flash, and host memory for testing. | [hal_eeprom.h](../../src/hal/storage/hal_eeprom.h) |
| Key-value storage (K/V) | Persist key-value pairs in two banks over EEPROM-like storage, with data protection for failure scenarios. The get/set/commit API returns `hal_status_t`. | [hal_kv.h](../../src/hal/storage/hal_kv.h) |
| LittleFS | Mount, unmount, and format a filesystem and query space usage through a common API. The module synchronizes access between threads, validates arguments and mount state, and supports progress reporting. RP and STM32G474 use linker-reserved internal flash; the mock lets tests specify operation results. | [LittleFS facade and provider](../../src/hal/storage/), [storage API](../api/en/14_storage.md) |
| FatFs / SD over SPI | SD card access over SPI and the FatFs R0.16 filesystem, checked out at a pinned revision. | [filesystem framework](../../src/hal/storage/filesystem/) |
| SD logging | Write application logs and crash reports to an SD card. | [hal_sdlogger.h](../../src/hal/storage/hal_sdlogger.h), [sdlogger](../../src/hal/storage/filesystem/sdlogger/) |

## Sensors, input devices and timekeeping

| Area | What it offers | Source |
|---|---|---|
| Real-time clock (RTC) | Read and set calendar or epoch time and use alarms, timers, and relative wake-up. The common API returns `hal_status_t`, validates Gregorian dates, and provides consistent lifecycle and locking rules. Hardware and mock implementations are available. | [hal_rtc.h](../../src/hal/rtc/hal_rtc.h), [hal_rtc.cpp](../../src/hal/rtc/hal_rtc.cpp), [providers](../../src/hal/rtc/), [calendar core](../../src/hal/time/) |
| PCF8563 RTC | Support for the PCF8563 clock over I2C. | [pcf8563 driver](../../src/hal/rtc/pcf8563/) |
| DS3231 RTC | Support for the DS3231 clock over I2C. | [ds3231 driver](../../src/hal/rtc/ds3231/) |
| STM32G474 internal RTC | Backup-domain calendar support with LSE/LSI selection and retained-time integrity checks. Features include Alarm A through IRQ or polling, one-shot WUT wake-up, source diagnostics, and a 1 Hz calibration output. | [STM32G474 provider](../../src/hal/impl/stm32g474/jh_stm32g474_rtc_provider.cpp) |
| RP2040/RP2350 AON RTC | Timekeeping through Pico SDK AON using the RP2040 calendar RTC or the RP2350 Powman timer. State survives a warm reset. Relative wake-up alarms and shared RTC/NTP integration are available. | [RP provider](../../src/hal/impl/rp2040/jh_rp_rtc_provider.cpp) |
| GPS / NMEA | Read position, speed, date, and time from a GPS receiver that outputs NMEA data. Hardware HAL UART or software serial is selected at build time. A mutex protects the NMEA parser, and the mock supports deterministic test data. RP UART and IRQ core-affinity rules still apply. | [hal_gps.h](../../src/hal/gps/hal_gps.h), [hal_gps.cpp](../../src/hal/gps/hal_gps.cpp), [GPS framework](../../src/hal/gps/) |
| Thermocouple support | Read temperature from MCP9600/MCP9601 and MAX6675 devices through a common API. Available operations depend on the device; MAX6675 supports type-K thermocouples only. The module provides consistent lifecycle, locking, and validation rules and a deterministic mock. | [hal_thermocouple.h](../../src/hal/temperature/hal_thermocouple.h), [facade/providers](../../src/hal/temperature/) |
| MCP9600/MCP9601 | Support for MCP9600/MCP9601 thermocouple amplifiers over I2C. | [mcp9600 driver](../../src/hal/temperature/mcp9600/) |
| MAX6675 | Support for the MAX6675 thermocouple converter through software-controlled GPIO. | [max6675 driver](../../src/hal/temperature/max6675/) |
| DS18B20 | Read temperature from the DS18B20 digital sensor over 1-Wire. | [hal_ds18b20.h](../../src/hal/temperature/hal_ds18b20.h), [ds18b20 driver](../../src/hal/temperature/ds18b20/) |
| DHT11/DHT22 | Read temperature and humidity from DHT11/DHT22 sensors through GPIO. | [hal_dht.h](../../src/hal/temperature/hal_dht.h), [dht driver](../../src/hal/temperature/dht/) |
| 1-Wire bus | A shared driver for communication with 1-Wire devices. | [hal_onewire.h](../../src/hal/onewire/hal_onewire.h), [onewire driver](../../src/hal/onewire/) |
| BH1750 | Measure ambient light with a BH1750 sensor over I2C. | [hal_bh1750.h](../../src/hal/sensors/hal_bh1750.h), [bh1750 driver](../../src/hal/sensors/bh1750/) |
| ADP5360 power management | Control charging, battery-level measurement, shipping mode, reset, and buck/buck-boost converter configuration over I2C. | [hal_adp5360.h](../../src/hal/power/hal_adp5360.h), [adp5360 driver](../../src/hal/power/adp5360/) |
| MCP3221 | Read the 12-bit MCP3221 ADC over I2C. | [hal_mcp3221.h](../../src/hal/analog/hal_mcp3221.h), [simple I/O drivers](../../src/hal/gpio/simple_io/) |
| ADS1X15 / ADS1115 | Support for external ADS1X15/ADS1115 ADCs over I2C. | [hal_external_adc.h](../../src/hal/analog/hal_external_adc.h), [ads1x15 driver](../../src/hal/analog/ads1x15/) |
| TSC2007 touch controller | Support for the TSC2007 resistive touch controller over I2C. | [hal_tsc2007.h](../../src/hal/input/hal_tsc2007.h), [tsc2007 driver](../../src/hal/input/tsc2007/) |
| STMPE610 touch controller | Support for the STMPE610 resistive touch controller over I2C or SPI. | [hal_stmpe610.h](../../src/hal/input/hal_stmpe610.h), [stmpe610 driver](../../src/hal/input/stmpe610/) |
| Infrared receiver | Decode IR receiver signals using GPIO and timing measurements. | [hal_irsmall_decoder.h](../../src/hal/input/hal_irsmall_decoder.h), [IR framework](../../src/hal/input/irsmall_decoder/) |

## Displays, indicators and output devices

| Area | What it offers | Source |
|---|---|---|
| Display support | Draw and display images through a common API for TFT, RGB OLED, and monochrome displays. Applications can query supported formats and operations. Features include status-returning area writes, streaming, RGB565 DMA, and text, depending on the implementation. | [hal_display.h](../../src/hal/display/hal_display.h) |
| GFX drawing and fonts | Shared drawing primitives and bundled bitmap fonts. | [display drivers](../../src/hal/display/drivers/) |
| ILI9341 TFT | Support for ILI9341 TFT displays over SPI. | [ili9341 driver](../../src/hal/display/drivers/ili9341_driver.h) |
| ST7735/ST7789/ST7796S/GC9A01 TFT | ST77xx-family support over SPI, including initialization and rotation for the round GC9A01 display. | [st77xx driver](../../src/hal/display/drivers/st77xx_driver.h) |
| SSD1306-family OLED | Support for `SSD1306`, `SSD1309`, `SSD1315`, `SH1106`, and `CH1115` through HAL I2C or SPI. | [ssd1306 driver](../../src/hal/display/drivers/ssd1306_driver.h) |
| SSD1331/SSD135x RGB OLED | Display RGB565 through `hal_display` and HAL SPI/GPIO, with direct pixel writes, streaming, and GFX primitives. Behavior is ported from Zephyr display drivers. | [hal_display.h](../../src/hal/display/hal_display.h) |
| ST7567 LCD | MONO01/MONO10 support through `hal_display`, using HAL I2C or SPI/GPIO and accounting for the device's page-based memory. Behavior is ported from Zephyr display drivers. | [hal_display.h](../../src/hal/display/hal_display.h) |
| SSD16xx / UC81xx e-paper | Monochrome SSD1608/SSD1673/SSD1675A/SSD1680/SSD1681 and UC8175/UC8176/UC8151D/UC8179 support over SPI/GPIO. Features include bounded BUSY waits, full and partial LUT profiles, deferred frame refresh, and direct MONO10 format. | [hal_display.h](../../src/hal/display/hal_display.h) |
| HD44780 LCD | Support for parallel character LCDs through HAL GPIO and timing functions. | [hal_hd44780.h](../../src/hal/display/hal_hd44780.h), [hd44780 driver](../../src/hal/display/hd44780/) |
| RGB / NeoPixel LED | Control a NeoPixel-style RGB status LED using the transport provided for each platform. | [hal_rgb_led.h](../../src/hal/gpio/hal_rgb_led.h), [neopixel driver](../../src/hal/gpio/neopixel/) |
| Digital potentiometers | A common API for controlling digital potentiometers over I2C. | [hal_digipot.h](../../src/hal/analog/hal_digipot.h) |
| MCP4017/4018/4019 | Control MCP4017/4018/4019 digital potentiometers over I2C. | [digipot drivers](../../src/hal/analog/digipot/) |
| MAX5395 | Control the MAX5395 digital potentiometer over I2C. | [digipot drivers](../../src/hal/analog/digipot/) |
| PGA2311 volume control | Control a stereo audio volume controller through SPI/GPIO. | [hal_pga2311.h](../../src/hal/audio/hal_pga2311.h), [pga2311 driver](../../src/hal/audio/pga2311/) |
| MCP23017 / PCA9654E / PCF8574 / 74HC595 / MCP4725 | Support for I/O expanders and a DAC through shared HAL I2C/SPI/GPIO drivers. | [simple I/O drivers](../../src/hal/gpio/simple_io/) |
| DACless PWM audio | Full-duplex PWM audio with DMA or polling and callbacks for blocks and individual samples. On RP, continuous round-robin ADC sampling through DMA lets sample callbacks read microphone or analog inputs at audio rates in the playback clock domain. | [hal_dacless.h](../../src/hal/audio/hal_dacless.h), [hal_dma_pwm_audio.h](../../src/hal/audio/hal_dma_pwm_audio.h), [dacless driver](../../src/hal/audio/dacless/) |

## Crypto, media and bundled libraries

| Area | What it offers | Source |
|---|---|---|
| Encoding and cryptography | Base64, MD5, SHA-256, and HMAC-SHA256 functions, plus ChaCha20/Poly1305 operations. | [hal_crypto.h](../../src/hal/security/hal_crypto.h), [wireguard crypto](../../src/hal/network/wireguard/core/crypto/) |
| CRC checksums | Compute CRC-8/MAXIM, Maxim 1-Wire CRC-16, CRC-16/CCITT-FALSE, and CRC-32/ISO-HDLC for data-integrity checks. | [hal_crc.h](../../src/hal/security/hal_crc.h) |
| Session authentication | Optional serial-session authentication functions. | [hal_sc_auth.h](../../src/hal/security/hal_sc_auth.h) |
| Serial frames and sessions | Framing and session support for serial protocols. An optional adapter synchronously dispatches TEXT/JSON commands to handlers and formats replies. Existing response and fallback callbacks are preserved. | [hal_serial_frame.h](../../src/hal/serial/hal_serial_frame.h), [hal_serial_session.h](../../src/hal/serial/hal_serial_session.h), [command API](../api/en/23_commands.md#framed-serial-session-adapter) |
| cJSON | Repository-managed cJSON and cJSON_Utils for JSON processing in resource-constrained environments. | [cJSON API](../api/en/17_cJSON.md) |
| PNG | A repository-managed LodePNG configuration for memory-constrained use, with optional Base64 helpers. | [LodePNG API](../api/en/18_LodePNG.md) |
| JPEG | Baseline JPEG decoding to RGB565 through repository-managed TJpgDec, with optional Base64 helpers. | [JPEG API](../api/en/19_JPEG.md) |
| Unity | Repository-managed Unity 2.5.4 for tests on the host and target hardware. | [Unity pin](../../third_party/unity_version.conf) |

## Examples and documentation

| Area | What it offers | Source |
|---|---|---|
| Portable examples | Buildable applications demonstrating core HAL features, sensors, displays, connectivity, storage, and media support. | [examples](../../examples/) |
| API reference | Function signatures, module usage requirements, and implementation-specific behavior. | [doc/api](../api/en/) |
| Firmware project workflow | Instructions for manifest configuration, platform and board selection, source discovery, CMake or ESP-IDF builds, upload, and monitoring. They also cover IntelliSense and generated files. | [FwProjectWorkflow.md](FwProjectWorkflow.md) |
| OTA workflow | Instructions for OTA integration on RP and ESP32-S3: build artifacts, initial flashing, VS Code updates, firewall configuration, image confirmation, rollback, and recovery. | [OTAWorkflow.md](OTAWorkflow.md) |
