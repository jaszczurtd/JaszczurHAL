# JaszczurHAL feature overview

This document is a high-level inventory of what JaszczurHAL offers. It is meant
as a compact feature map, not an API reference. For function signatures,
configuration details and module contracts, see [JaszczurHAL_API.md](JaszczurHAL_API.md).

## Platform and build capabilities

| Area | What it offers | Source |
|---|---|---|
| RP2040 / RP2350 backend | Arduino-Pico based backend for Raspberry Pi Pico-class boards, including GPIO, buses, storage, connectivity and many shared drivers. | [rp2040 backend](../src/hal/impl/rp2040/) |
| STM32G474 backend | Bare-metal STM32G474 backend with startup/runtime glue, linker support, flash services and native peripheral implementations. | [STM32G474 backend](../src/hal/impl/stm32g474/) |
| Mock backend | Deterministic host backend for unit tests and simulation-oriented development without hardware. | [mock backend](../src/hal/impl/.mock/) |
| Compile-time opt-in modules | Optional features are selected with `HAL_ENABLE_*` flags and pull in only their dependencies. | [hal_config.h](../src/hal/hal_config.h) |
| Portable app entry | Common `app_start()` / `app_task0()` / optional `app_task1()` model across supported targets and examples. | [hal_app.h](../src/hal/hal_app.h) |
| FreeRTOS integration | Native FreeRTOS-aware mutex, delay and app-task support for supported target builds. | [STM32 FreeRTOS glue](../src/hal/impl/stm32g474/freertos/) |
| Static library builds | Dedicated CMake/helper flows for RP2040 and STM32G474 library builds. | [rp2040_lib](../rp2040_lib/), [stm32_lib](../stm32_lib/) |
| Validation gate | Full local gate for unit tests, Valgrind, static analysis, target builds and examples. | [runalltests.sh](../runalltests.sh) |

## Core HAL

| Area | What it offers | Source |
|---|---|---|
| GPIO | Portable digital I/O, pull modes and interrupt-oriented usage. | [hal_gpio.h](../src/hal/hal_gpio.h) |
| ADC | Portable analog input abstraction. | [hal_adc.h](../src/hal/hal_adc.h) |
| DAC | True DAC support where available, with unsupported-target reporting elsewhere. | [hal_dac.h](../src/hal/hal_dac.h) |
| PWM | Portable PWM output plus frequency-controlled PWM helpers. | [hal_pwm.h](../src/hal/hal_pwm.h), [hal_pwm_freq.h](../src/hal/hal_pwm_freq.h) |
| Pulse counting | Edge/pulse counting for signal measurement and simple counter applications. | [hal_pcnt.h](../src/hal/hal_pcnt.h) |
| Timers and system time | Basic timers, extended timer helpers, idle/delay and system services. | [hal_timer.h](../src/hal/hal_timer.h), [hal_system.h](../src/hal/hal_system.h) |
| Synchronization | Mutexes and critical sections with target-specific implementations. | [hal_sync.h](../src/hal/hal_sync.h) |
| Soft timers | Lightweight cooperative software timers. | [hal_soft_timer.h](../src/hal/hal_soft_timer.h) |
| Utility primitives | Bit helpers, math helpers, PID control, watchdog support and common utility APIs. | [tools.h](../src/tools.h), [utils](../src/utils/) |

## Communication and connectivity

| Area | What it offers | Source |
|---|---|---|
| UART | Hardware serial communication abstraction. | [hal_uart.h](../src/hal/hal_uart.h) |
| Serial/debug console | TX-serialized console output, RP2040 TinyUSB CDC transport, streamed task-context debug formatting, ISR-deferred logs and per-source error rate limiting. | [hal_serial.h](../src/hal/hal_serial.h), [serial API](api/08_sync_serial.md) |
| Software serial | Bit-banged serial transport for targets/modules that need it. | [hal_swserial.h](../src/hal/hal_swserial.h) |
| I2C master | Portable I2C controller API used by sensors, RTCs and storage devices. | [hal_i2c.h](../src/hal/hal_i2c.h) |
| I2C slave | Target-mode/register-map style I2C support. | [hal_i2c_slave.h](../src/hal/hal_i2c_slave.h) |
| SPI | Portable SPI master/controller API used by displays, CAN, SD and audio drivers. | [hal_spi.h](../src/hal/hal_spi.h) |
| CAN facade | Backend-selectable CAN surface for classic CAN and CAN FD-capable backends. | [hal_can.h](../src/hal/hal_can.h) |
| MCP2515 CAN | Shared SPI CAN backend. | [mcp2515 driver](../src/hal/impl/shared/drivers/mcp2515/) |
| MCP2517FD/MCP2518FD CAN FD | Shared SPI CAN FD backend. | [mcp251xfd driver](../src/hal/impl/shared/drivers/mcp251xfd/) |
| STM32G474 native FDCAN | Native STM32G474 FDCAN backend. | [STM32 FDCAN backend](../src/hal/impl/stm32g474/hal_can_stm32g474_fdcan.cpp) |
| WiFi | WiFi-capable RP2040/Pico W style connectivity surface. | [hal_wifi.h](../src/hal/hal_wifi.h) |
| UDP | Handle-based multi-socket UDP transport plus legacy single-socket compatibility wrapper for WiFi builds. | [hal_udp.h](../src/hal/hal_udp.h) |
| TCP sockets | Handle-based TCP client sockets and listener/server handles with connect, bind/listen/accept, send/recv, shutdown and mock/RP2040 backends. | [hal_tcp.h](../src/hal/hal_tcp.h) |
| BSD sockets adapter | Minimal IPv4 `sys/socket.h` / `netinet/in.h` / `arpa/inet.h` / `netdb.h` compatibility layer over HAL UDP/TCP handles, including `getaddrinfo()`, `setsockopt()`, `O_NONBLOCK`, `MSG_DONTWAIT` and `select()` readiness. | [socket.h](../src/sys/socket.h), [netdb.h](../src/netdb.h) |
| MQTT | PubSubClient-based MQTT connectivity wrapper. | [hal_mqtt.h](../src/hal/hal_mqtt.h) |
| OTA | ArduinoOTA-oriented update integration. | [hal_ota.h](../src/hal/hal_ota.h) |
| NTP / time-of-day sync | Network time helpers for connected builds. | [hal_time.h](../src/hal/hal_time.h) |
| WireGuard | Bundled WireGuard integration for secure connected firmware scenarios. | [hal_wireguard.h](../src/hal/hal_wireguard.h), [WireGuard port](../src/hal/impl/rp2040/frameworks/arduino-wireguard-pico-w/) |
| Cellular modem | Generic AT-command modem engine plus SimCom A76xx family support. | [hal_modem_at.h](../src/hal/hal_modem_at.h), [hal_simcom_a76xx.h](../src/hal/hal_simcom_a76xx.h) |

## Storage, files and logging

| Area | What it offers | Source |
|---|---|---|
| EEPROM abstraction | Target flash or external EEPROM-style persistent storage facade. | [hal_eeprom.h](../src/hal/hal_eeprom.h) |
| Key-value storage | Small persistent key-value layer on top of EEPROM-style storage. | [hal_kv.h](../src/hal/hal_kv.h) |
| LittleFS | Lightweight filesystem lifecycle/helpers; STM32G474 can use internal flash partitioning. | [hal_littlefs.h](../src/hal/hal_littlefs.h) |
| FatFs / SD over SPI | Shared FatFs core and SD-over-SPI disk I/O. | [filesystem framework](../src/hal/impl/shared/frameworks/filesystem/) |
| SD logger | SD-card logging and crash-report logging support. | [hal_sdlogger.h](../src/hal/hal_sdlogger.h), [sdlogger](../src/hal/impl/shared/frameworks/filesystem/sdlogger/) |

## Sensors, input devices and timekeeping

| Area | What it offers | Source |
|---|---|---|
| RTC facade | Common real-time-clock surface with multiple chip backends. | [hal_rtc.h](../src/hal/hal_rtc.h) |
| PCF8563 RTC | Shared I2C PCF8563 backend. | [pcf8563 driver](../src/hal/impl/shared/drivers/pcf8563/) |
| DS3231 RTC | Shared I2C DS3231 backend. | [ds3231 driver](../src/hal/impl/shared/drivers/ds3231/) |
| GPS / NMEA | Portable NMEA parser and GPS abstraction for UART/software-serial receivers. | [hal_gps.h](../src/hal/hal_gps.h), [GPS framework](../src/hal/impl/shared/frameworks/gps/) |
| Thermocouple facade | Common temperature surface for thermocouple backends. | [hal_thermocouple.h](../src/hal/hal_thermocouple.h) |
| MCP9600/MCP9601 | Shared I2C thermocouple amplifier driver. | [mcp9600 driver](../src/hal/impl/shared/drivers/mcp9600/) |
| MAX6675 | Shared GPIO bit-banged thermocouple converter driver. | [max6675 driver](../src/hal/impl/shared/drivers/max6675/) |
| DS18B20 | Shared 1-Wire digital temperature sensor support. | [hal_ds18b20.h](../src/hal/hal_ds18b20.h), [ds18b20 driver](../src/hal/impl/shared/drivers/ds18b20/) |
| DHT11/DHT22 | Shared GPIO temperature and humidity sensor driver. | [hal_dht.h](../src/hal/hal_dht.h), [dht driver](../src/hal/impl/shared/drivers/dht/) |
| 1-Wire bus | Generic shared 1-Wire bus wrapper/driver. | [hal_onewire.h](../src/hal/hal_onewire.h), [onewire driver](../src/hal/impl/shared/drivers/onewire/) |
| BH1750 | Shared I2C ambient-light sensor driver. | [hal_bh1750.h](../src/hal/hal_bh1750.h), [bh1750 driver](../src/hal/impl/shared/drivers/bh1750/) |
| ADS1X15 / ADS1115 | Shared external ADC driver over I2C. | [hal_external_adc.h](../src/hal/hal_external_adc.h), [ads1x15 driver](../src/hal/impl/shared/drivers/ads1x15/) |
| TSC2007 touch | Shared I2C resistive touch controller driver. | [hal_tsc2007.h](../src/hal/hal_tsc2007.h), [tsc2007 driver](../src/hal/impl/shared/drivers/tsc2007/) |
| STMPE610 touch | Shared I2C/SPI resistive touch controller driver. | [hal_stmpe610.h](../src/hal/hal_stmpe610.h), [stmpe610 driver](../src/hal/impl/shared/drivers/stmpe610/) |
| Infrared receiver decoding | Shared GPIO/timing based IR receiver decoder. | [hal_irsmall_decoder.h](../src/hal/hal_irsmall_decoder.h), [IR framework](../src/hal/impl/shared/frameworks/irsmall_decoder/) |

## Displays, indicators and output devices

| Area | What it offers | Source |
|---|---|---|
| Generic display facade | Common drawing/display surface for OLED and TFT backends. | [hal_display.h](../src/hal/hal_display.h) |
| GFX engine and fonts | Shared graphics primitives and bundled bitmap fonts. | [display drivers](../src/hal/impl/shared/drivers/display/) |
| ILI9341 TFT | SPI TFT display backend. | [ili9341 driver](../src/hal/impl/shared/drivers/display/ili9341_driver.h) |
| ST7735/ST7789/ST7796S TFT | Shared ST77xx-family SPI TFT backend. | [st77xx driver](../src/hal/impl/shared/drivers/display/st77xx_driver.h) |
| SSD1306 OLED | I2C OLED display backend. | [ssd1306 driver](../src/hal/impl/shared/drivers/display/ssd1306_driver.h) |
| HD44780 LCD | Parallel character LCD support over HAL GPIO/timing. | [hal_hd44780.h](../src/hal/hal_hd44780.h), [hd44780 driver](../src/hal/impl/shared/drivers/hd44780/) |
| RGB / NeoPixel status LED | Shared NeoPixel-style RGB LED support with target-specific transport. | [hal_rgb_led.h](../src/hal/hal_rgb_led.h), [neopixel driver](../src/hal/impl/shared/drivers/neopixel/) |
| Digital potentiometer facade | Common API for I2C digital potentiometers. | [hal_digipot.h](../src/hal/hal_digipot.h) |
| MCP4017/4018/4019 | Shared I2C digital potentiometer backend. | [digipot drivers](../src/hal/impl/shared/drivers/digipot/) |
| MAX5395 | Shared I2C digital potentiometer backend. | [digipot drivers](../src/hal/impl/shared/drivers/digipot/) |
| PGA2311 audio volume | Shared SPI/GPIO stereo volume controller driver. | [hal_pga2311.h](../src/hal/hal_pga2311.h), [pga2311 driver](../src/hal/impl/shared/drivers/pga2311/) |
| DACless PWM audio | Shared PWM-audio engine with DMA and polling paths, block/sample callbacks and ADC sampling. | [hal_dacless.h](../src/hal/hal_dacless.h), [hal_dma_pwm_audio.h](../src/hal/hal_dma_pwm_audio.h), [dacless driver](../src/hal/impl/shared/drivers/dacless/) |

## Crypto, media and bundled libraries

| Area | What it offers | Source |
|---|---|---|
| Crypto helpers | Base64, MD5, SHA-256, HMAC-SHA256 and ChaCha20/Poly1305-oriented helpers. | [hal_crypto.h](../src/hal/hal_crypto.h), [wireguard crypto](../src/hal/impl/shared/frameworks/wireguard/crypto/) |
| Session authentication helpers | Optional serial-session authentication support. | [hal_sc_auth.h](../src/hal/hal_sc_auth.h) |
| Serial framing/session tools | Reusable serial frame/session vocabulary helpers for embedded protocols. | [hal_serial_frame.h](../src/hal/hal_serial_frame.h), [hal_serial_session.h](../src/hal/hal_serial_session.h) |
| cJSON | Bundled cJSON/cJSON_Utils for constrained JSON work. | [cjson framework](../src/hal/impl/shared/frameworks/cjson/) |
| PNG | Bundled memory-oriented LodePNG support plus optional Base64 helpers. | [lodepng framework](../src/hal/impl/shared/frameworks/lodepng/) |
| JPEG | Bundled JPEGDecoder/picojpeg baseline JPEG decoding plus optional Base64 helpers. | [jpeg framework](../src/hal/impl/shared/frameworks/jpeg/) |
| Unity | Bundled Unity framework for host-side tests. | [unity sources](../src/utils/unity.h) |

## Examples and documentation

| Area | What it offers | Source |
|---|---|---|
| Portable examples | Buildable example applications covering core, sensors, displays, connectivity, storage and media modules. | [examples](../examples/) |
| API reference | Detailed module contracts, signatures and backend notes. | [doc/api](api/) |
| Porting/status notes | Target-specific progress notes and architecture roadmap. | [STM32G474 progress](STM32G474_porting_progress.md), [future ideas](future_ideas.md) |
| Local datasheets | Local reference PDFs and notes for supported hardware. | [datasheets](datasheets/) |
