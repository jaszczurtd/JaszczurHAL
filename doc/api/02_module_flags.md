# Module flags and configuration

> **Part of [JaszczurHAL API Reference](../JaszczurHAL_API.md)**

## Selective module inclusion (`HAL_ENABLE_*`)

JaszczurHAL uses an **opt-in** model: by default *no* optional module is
compiled. To use a module, define its `HAL_ENABLE_<MODULE>` flag (in
`hal_project_config.h` or via `-D`). Enabling a flag pulls in:

* the **API declarations** in the corresponding header (otherwise the file
  compiles to an empty translation unit and calls to its functions raise a
  clear compile-time error);
* the **implementation** .cpp (and the bundled third-party drivers it
  depends on - all `#include`s are gated);
* the entry in the **umbrella header** `hal/hal.h`.

Modules that are disabled cost zero code and RAM and do not pull their
third-party dependencies into the target build.

Feature flags use presence semantics. Supported project definitions are
`#define HAL_ENABLE_X` and `#define HAL_ENABLE_X 1`. Do not use
`#define HAL_ENABLE_X 0`: the production preprocessor still evaluates
`#ifdef HAL_ENABLE_X`, so the symbol remains enabled. Supported board
generation, CMake helpers, static-library scripts, and `jh-vscode` reject `=0`
and other explicit values with `[JH-CFG-VALUE]`. Arbitrary direct compiler
invocations retain preprocessor presence semantics. The feature-registry lint
also reports unknown or derived symbols. In definition-list inputs, every
`HAL_ENABLE_*` entry must be a standalone simple token separated with
semicolons. Whitespace does not separate multiple feature definitions, and
CMake generator expressions are rejected.

The declarative registry under `config/features/` is the production source for
the feature graph. `hal_config.h` includes its generated C header, while CMake,
the board/link contract and `jh-vscode` consume the generated resolver and its
`requestedFeatures` / `resolvedFeatures` result.

### Available flags

This section is the maintained public catalog of `HAL_ENABLE_*` flags.
`hal_config.h` remains the public configuration facade and retains contextual
rules outside registry v1. `doc/HAL_FLAGS.txt` provides a concise text summary.

Application entry flags are separate from optional HAL modules:

| Flag | Effect |
|---|---|
| `HAL_ENABLE_APP_TASK1` | Dispatches optional `app_task1()` from the HAL-provided entry path. Bare RP launches core 1; RP FreeRTOS creates a task pinned to core 1. STM32G474 FreeRTOS creates the second application task. ESP32-S3 creates it on core 1 by default and accepts a valid `HAL_FREERTOS_TASK1_CORE` or `-1` for no affinity. Leave undefined for single-loop/single-task apps. |

FreeRTOS integration is also an explicit opt-in, but it is not a HAL module:

| Flag | Effect |
|---|---|
| `HAL_ENABLE_FREERTOS` | Enables FreeRTOS for the selected target. RP builds use the pinned kernel and SMP ports for RP2040, RP2350 ARM and RP2350 RISC-V; HAL starts the scheduler and pins application tasks to their corresponding cores. STM32G474 uses the pinned Cortex-M4F port. `hal_mutex_*`, `hal_delay_ms()`, `hal_idle()` and runtime reporting select FreeRTOS-aware paths. This flag does not add a public `hal_rtos_*` API and does not by itself make every HAL module task-safe. |

Stack protection uses two independent opt-ins:

| Flag | Effect |
|---|---|
| `HAL_ENABLE_STACK_GUARD` | Enables synchronous hardware protection for native RP2040/RP2350 system stacks, the STM32G474 main stack, and ESP32-S3 task-stack end watchpoints supplied by ESP-IDF. FreeRTOS builds additionally enable kernel task-stack overflow checking. The target-independent `hal_stack_guard_init_ex()` API reports whether protection is active; periodic polling is unnecessary. |
| `HAL_ENABLE_STACK_PROTECTOR` | Enables GCC/Clang `-fstack-protector-strong` instrumentation for HAL and application sources in supported RP and STM32G474 firmware recipes. A canary mismatch uses the target retained stack-overflow reset path. It is independent from `HAL_ENABLE_STACK_GUARD`. |

| Flag | Header | Impl | 3rd-party deps pulled in |
|---|---|---|---|
| `HAL_ENABLE_BLE` | `hal_ble.h` | `hal_ble.cpp` + `hal/bluetooth/*` | Experimental BLE Peripheral over the pinned BTstack and CYW43 controller; supported on RP2040 Pico W, STM32G474+PIM730, and mock. BTstack carries a non-commercial-use restriction; see the [Bluetooth API](20_bluetooth.md#license-and-distribution-boundary). |
| `HAL_ENABLE_BLE_STREAM` | `hal_ble_stream.h` | `hal_ble_stream.cpp` + `hal/bluetooth/*` | Experimental bounded framed byte stream over BLE (propagates BLE + CRYPTO) |
| `HAL_ENABLE_LORA` | `hal_lora_radio.h` | `hal_lora_radio.cpp` | Provider-neutral raw LoRa lifecycle, modem presets, blocking TX, polling RX, diagnostics, power state and time-on-air; requires exactly one provider |
| `HAL_ENABLE_LORA_LINK` | `hal_lora_link.h` | `hal_lora_link.cpp` + `jh_lora_link_frame.cpp` | Reliable private messages with addressing, sequences, ACK/retry, duplicate suppression and fragmentation (propagates LORA + CRC); optional AEAD requires CRYPTO; see the [LoRa link API](22_lora_link.md) |
| `HAL_ENABLE_SX126X` | `hal_lora_radio.h` | `hal_lora_radio.cpp` + `hal/radio/sx126x/*` + pinned Semtech driver | SX1262 plus experimental, software-only SX1261 provider over HAL SPI/GPIO (propagates LORA + SPI); see the [LoRa radio API](21_lora.md) |
| `HAL_ENABLE_SX127X` | `hal_lora_radio.h` | `hal_lora_radio.cpp` + `hal/radio/sx127x/*` | Experimental, software-only SX1276/SX1278 provider over HAL SPI/GPIO (propagates LORA + SPI and conflicts with SX126X); see the [LoRa radio API](21_lora.md) |
| `HAL_ENABLE_WIFI` | `hal_wifi.h` | `hal_wifi.cpp` | CYW43/lwIP or native ESP-IDF WiFi/`esp_netif`/lwIP backend selected by target/board configuration |
| `HAL_ENABLE_TIME` | optional declarations in `hal_time.h` | shared runtime clock + target libc bridge | Runtime setter/status and WiFi NTP helpers (propagates UDP + WIFI); pure calendar/range helpers stay unconditional |
| `HAL_ENABLE_MQTT` | `hal_mqtt.h` | `hal_mqtt.cpp` | PubSubClient over HAL TCP or BearSSL TLS (propagates TCP + WIFI) |
| `HAL_ENABLE_UDP`  | `hal_udp.h`  | `hal_udp.cpp`  | Handle-based UDP transport (propagates WIFI) |
| `HAL_ENABLE_TCP` | `hal_tcp.h` | `hal_tcp.cpp` | TCP client/listener transport (propagates WIFI) |
| `HAL_ENABLE_HTTP_SERVER` | `hal_http_server.h` | `hal/network/http/hal_http_server.cpp` | Small poll-driven plaintext HTTP/1.1 server over HAL TCP (propagates TCP + WIFI); no HTTPS-server API |
| `HAL_ENABLE_HTTP_FILES` | `hal_http_files.h` | `hal/network/http/hal_http_files.cpp` | Callback-backed file serving, ETag and upload helpers over HAL HTTP routes (propagates HTTP_SERVER + TCP + WIFI) |
| `HAL_ENABLE_WEBSOCKET` | `hal_websocket.h` | `hal/network/websocket/hal_websocket.cpp` | Small poll-driven plaintext WebSocket server over HAL TCP (propagates TCP + WIFI); no WSS or WebSocket-client API |
| `HAL_ENABLE_NET_CONSOLE` | `hal_net_console.h` | `hal/network/net_console/hal_net_console.cpp` | Password-protected serial/debug mirror and command stream over HAL TCP (propagates TCP + WIFI) |
| `HAL_ENABLE_NET_COMMANDS` | `hal_net_commands.h` | `hal/network/net_commands/hal_net_commands.cpp` | Shared JSON/text command dispatcher for HTTP and WebSocket control channels (propagates HTTP_SERVER + WEBSOCKET + CJSON + TCP + WIFI) |
| `HAL_ENABLE_NOTIFY` | `hal_notify.h` | `hal/network/notify/hal_notify.cpp` | Backend-dispatched notification facade with generation-checked channel handles |
| `HAL_ENABLE_NOTIFY_TELEGRAM` | `hal_notify.h` | `hal/network/notify/hal_notify_telegram.cpp` | Telegram Bot API backend over `hal_http_client`; public Telegram delivery uses HTTPS, while custom HTTP hosts may be used for local/proxy deployments (propagates NOTIFY + HTTP_CLIENT + TLS + CJSON + TCP + WIFI) |
| `HAL_ENABLE_BSD_SOCKETS` | `sys/socket.h`, `netinet/in.h`, `arpa/inet.h`, `netdb.h`, `fcntl.h`, `sys/select.h`, `unistd.h` | `hal/network/adapters/bsd/hal_bsd_sockets.cpp` | Public BSD/POSIX adapter over HAL UDP/TCP, including `getaddrinfo()` (propagates UDP + TCP + WIFI); remains usable with or without TLS |
| `HAL_ENABLE_TLS` | `hal_tls.h` | `hal/network/tls/hal_tls.cpp` + `hal/network/tls/BearSSL/*` | BearSSL TLS client over native HAL TCP (propagates TCP + WIFI); does not force BSD sockets, while the optional BearSSL BSD transport keeps TLS-over-BSD available |
| `HAL_ENABLE_HTTP_CLIENT` | `hal_http_client.h` | `hal/network/http/hal_http_client.cpp` | Bounded one-shot HTTP/1.1 client over HAL TCP with an HTTPS transport when TLS is selected (propagates TCP + WIFI) |
| `HAL_ENABLE_OTA`  | `hal_ota.h`  | target `hal_ota.cpp` + shared OTA protocol | Native UDP/TCP update service with discovery, optional password challenge, trial boot, confirmation and rollback. RP uses a signed JaszczurHAL container/swap engine; ESP32-S3 stages a raw ESP application image through ESP-IDF OTA partitions (propagates WIFI + UDP + TCP + CRYPTO + CRC). |
| `HAL_ENABLE_WIREGUARD` | `hal_wireguard.h` | `hal/network/wireguard/hal_wireguard.cpp` + target lwIP extension port | Bundled WireGuard over a capability-advertised host lwIP stack, including ESP32-S3 (propagates UDP + WIFI) |
| `HAL_ENABLE_EEPROM` | `hal_eeprom.h` | `hal_eeprom.cpp` | Target flash EEPROM emulation; AT24C256 over HAL I2C when selected |
| `HAL_ENABLE_KV` | `hal_kv.h` | `hal_kv.cpp` | *(propagates EEPROM)* |
| `HAL_ENABLE_LITTLEFS` | `hal_littlefs.h` | `hal_littlefs.cpp` | LittleFS lifecycle helpers; native RP uses `HAL_RP_FLASH_LITTLEFS_SIZE`, STM32G474 uses `HAL_STM32_FLASH_LITTLEFS_SIZE` |
| `HAL_ENABLE_SDLOGGER` | `hal_sdlogger.h` | `hal/storage/filesystem/sdlogger/hal_sdlogger.cpp` | SD logger over shared FatFs (propagates FAT + EEPROM + SPI) |
| `HAL_ENABLE_UART` | `hal_uart.h` | `hal_uart.cpp` | Hardware UART |
| `HAL_ENABLE_SWSERIAL` | `hal_swserial.h` | target `hal_swserial.cpp` | Native Pico SDK PIO/DMA software UART on RP2040; shared HAL GPIO backend on other targets |
| `HAL_ENABLE_I2C` | `hal_i2c.h` | `hal_i2c.cpp` | I2C master/controller bus |
| `HAL_ENABLE_I2C_SLAVE` | `hal_i2c_slave.h` | `hal_i2c_slave.cpp` | I2C slave/target register-map mode |
| `HAL_ENABLE_SPI` | `hal_spi.h` | `hal_spi.cpp` | SPI master/controller |
| `HAL_ENABLE_CAN` | `hal_can.h` | `hal_can.cpp` + `hal_can_util.cpp` | Generic CAN API facade; requires at least one backend |
| `HAL_ENABLE_MCP2515` | `hal_can.h` + `hal/can/mcp2515/mcp2515_driver.h` | target `hal_can.cpp` facade + `hal/can/mcp2515/hal_can_mcp2515.cpp` + `hal/can/mcp2515/hal_can_mcp2515_config.cpp` + `hal/can/mcp2515/mcp2515_driver.cpp` | Shared HAL-only MCP2515 CAN backend (propagates CAN + SPI) |
| `HAL_ENABLE_MCP251XFD` | `hal_can.h` + `hal/can/mcp251xfd/mcp251xfd_driver.h` | target `hal_can.cpp` facade + `hal/can/mcp251xfd/hal_can_mcp251xfd.cpp` + `hal/can/mcp251xfd/hal_can_mcp251xfd_config.cpp` + `hal/can/mcp251xfd/mcp251xfd_driver.cpp` | Shared MCP2517FD/MCP2518FD CAN FD backend (propagates CAN + SPI) |
| `HAL_ENABLE_STM32G474_FDCAN` | `hal_can.h` | `impl/stm32g474/hal_can.cpp` + `impl/stm32g474/hal_can_stm32g474_fdcan.cpp` + `impl/stm32g474/hal_can_stm32g474_fdcan_config.cpp` | Native STM32G474 FDCAN1 CAN FD backend (propagates CAN; compile-time rejected outside STM32G474) |
| `HAL_ENABLE_RTC` | `hal_rtc.h` | `hal_rtc.cpp` | *(needs PCF8563, DS3231, or internal backend)* |
| `HAL_ENABLE_PCF8563` | `hal_rtc.h` | `hal_rtc.cpp` | PCF8563 backend (propagates RTC + I2C) |
| `HAL_ENABLE_DS3231` | `hal_rtc.h` | `hal_rtc.cpp` | DS3231 backend (propagates RTC + I2C) |
| `HAL_ENABLE_INTERNAL_RTC` | `hal_rtc.h` | target RTC provider | Target-native RTC backend for STM32G474 and RP2040/RP2350 (propagates RTC; no I2C) |
| `HAL_ENABLE_POWER_MANAGEMENT` | `hal_power.h` | target `hal_power.cpp` | Capability-driven Sleep/deep-sleep/power-down API (propagates INTERNAL_RTC + RTC); see [Timers and system](06_timers_system.md#halpower-low-power-transitions-optional-halenablepowermanagement) |
| `HAL_ENABLE_THERMOCOUPLE` | `hal_thermocouple.h` | `hal_thermocouple.cpp` | *(needs MCP9600 or MAX6675 backend)* |
| `HAL_ENABLE_MCP9600` | `hal_thermocouple.h` + `hal/temperature/mcp9600/mcp9600_driver.h` | `hal_thermocouple.cpp` + `hal/temperature/mcp9600/mcp9600_driver.cpp` | shared HAL-only MCP9600/MCP9601 driver (propagates THERMOCOUPLE + I2C) |
| `HAL_ENABLE_MAX6675` | `hal_thermocouple.h` + `hal/temperature/max6675/max6675_driver.h` | `hal_thermocouple.cpp` + `hal/temperature/max6675/max6675_driver.cpp` | shared HAL-only MAX6675 bit-bang driver (propagates THERMOCOUPLE) |
| `HAL_ENABLE_DS18B20` | `hal_ds18b20.h` + `hal/onewire/onewire_driver.h` | `hal/temperature/ds18b20/hal_ds18b20.cpp` + `hal/onewire/onewire_driver.cpp` | shared HAL-only DS18B20 backend over 1-Wire (propagates ONEWIRE) |
| `HAL_ENABLE_DHT` | `hal_dht.h` | `hal/temperature/dht/hal_dht.cpp` | shared DHT11/DHT22 temperature/humidity driver over HAL GPIO |
| `HAL_ENABLE_BH1750` | `hal_bh1750.h` | `hal/sensors/bh1750/hal_bh1750.cpp` | shared HAL I2C BH1750 ambient-light sensor driver (propagates I2C) |
| `HAL_ENABLE_ADP5360` | `hal_adp5360.h` | `hal/power/adp5360/hal_adp5360.cpp` | shared HAL I2C ADP5360 PMIC driver: MFD init/reset/shipment, charger, fuel-gauge and buck/buck-boost regulator control (propagates I2C) |
| `HAL_ENABLE_MCP3221` | `hal_mcp3221.h` | `hal/gpio/simple_io/hal_simple_io_drivers.cpp` | MCP3221 12-bit ADC over HAL I2C (propagates I2C) |
| `HAL_ENABLE_TSC2007` | `hal_tsc2007.h` | `hal/input/tsc2007/tsc2007.cpp` | shared HAL I2C TSC2007 resistive touch controller driver (propagates I2C) |
| `HAL_ENABLE_STMPE610` | `hal_stmpe610.h` | `hal/input/stmpe610/stmpe610.cpp` | shared HAL I2C/SPI STMPE610 resistive touch controller driver (propagates I2C + SPI) |
| `HAL_ENABLE_IRSMALL_DECODER` | `hal_irsmall_decoder.h` | `hal/input/irsmall_decoder/irsmall_decoder.cpp` | shared HAL GPIO interrupt infrared receiver decoder |
| `HAL_ENABLE_ONEWIRE` | `hal_onewire.h` + `hal/onewire/onewire_driver.h` | `hal/onewire/hal_onewire.cpp` + `hal/onewire/onewire_driver.cpp` | shared HAL-only 1-Wire bit-bang driver (propagates CRC) |
| `HAL_ENABLE_EXTERNAL_ADC` | `hal_external_adc.h` + `hal/analog/ads1x15/ads1x15_driver.h` | `hal/analog/ads1x15/hal_external_adc_ads1x15.cpp` + `hal/analog/ads1x15/ads1x15_driver.cpp` | shared HAL-only ADS1X15/ADS1115 driver (propagates I2C) |
| `HAL_ENABLE_GPS` | `hal_gps.h` | `hal_gps.cpp` + `hal/gps/` | portable facade and NMEA engine (RP2040 + STM32G474); needs a transport: SWSERIAL or UART |
| `HAL_ENABLE_DIGIPOT` | `hal_digipot.h` + `hal/analog/digipot/hal_digipot_ops.h` | `hal_digipot.cpp` + `hal/analog/digipot/*.cpp` | facade/pool/dispatch; needs MCP401X or MAX5395 backend |
| `HAL_ENABLE_MCP401X` | `hal_digipot.h` + `hal/analog/digipot/hal_digipot_ops.h` | `hal_digipot.cpp` + `hal/analog/digipot/digipot_mcp401x.cpp` | MCP4017/4018/4019 shared HAL I2C driver (propagates DIGIPOT + I2C) |
| `HAL_ENABLE_MAX5395` | `hal_digipot.h` + `hal/analog/digipot/hal_digipot_ops.h` | `hal_digipot.cpp` + `hal/analog/digipot/digipot_max5395.cpp` | MAX5395 shared HAL I2C driver (propagates DIGIPOT + I2C) |
| `HAL_ENABLE_PGA2311` | `hal_pga2311.h` + `hal/audio/pga2311/pga2311_driver.h` | `hal_pga2311.cpp` + `hal/audio/pga2311/pga2311_driver.cpp` | PGA2311 shared HAL SPI/GPIO stereo volume driver (propagates SPI) |
| `HAL_ENABLE_MCP23017` | `hal_mcp23017.h` | `hal/gpio/simple_io/hal_simple_io_drivers.cpp` | MCP23017 GPIO expander over HAL I2C (propagates I2C) |
| `HAL_ENABLE_PCA9654E` | `hal_pca9654e.h` | `hal/gpio/simple_io/hal_simple_io_drivers.cpp` | PCA9654E output expander over HAL I2C (propagates I2C) |
| `HAL_ENABLE_PCF8574` | `hal_pcf8574.h` | `hal/gpio/simple_io/hal_simple_io_drivers.cpp` | PCF8574 quasi-bidirectional GPIO expander over HAL I2C (propagates I2C) |
| `HAL_ENABLE_HC595` | `hal_hc595.h` | `hal/gpio/simple_io/hal_simple_io_drivers.cpp` | 74HC595 shift-register output expander over HAL SPI/GPIO (propagates SPI) |
| `HAL_ENABLE_MCP4725` | `hal_mcp4725.h` | `hal/gpio/simple_io/hal_simple_io_drivers.cpp` | MCP4725 12-bit DAC over HAL I2C (propagates I2C) |
| `HAL_ENABLE_MFRC522` | `hal_mfrc522.h` + `hal/nfc/mfrc522/mfrc522.h` | `hal/nfc/mfrc522/mfrc522*.cpp` | MFRC522 RFID reader driver over HAL SPI/I2C (propagates SPI) |
| `HAL_ENABLE_PN532` | `hal_pn532.h` + `hal/nfc/pn532/pn532.h` | `hal/nfc/pn532/pn532*.cpp` | PN532 NFC/RFID reader driver over HAL SPI/I2C/UART (propagates SPI) |
| `HAL_ENABLE_DACLESS` | `hal_dacless.h` + `hal/audio/dacless/dacless.h` | `hal/audio/dacless/dacless.cpp` | Shared DACless PWM-audio engine with block/sample callbacks and ADC sampling (propagates DMA_PWM_AUDIO + PWM_FREQ) |
| `HAL_ENABLE_DMA_PWM_AUDIO` | `hal_dma_pwm_audio.h` | `hal_dma_pwm_audio.cpp` | Timer-paced PWM-audio DMA helper used by DACless |
| `HAL_ENABLE_PWM_FREQ` | `hal_pwm_freq.h` | `hal_pwm_freq.cpp` | RP2040 hardware/pwm, STM32G474 TIM PWM, or ESP32-S3 LEDC |
| `HAL_ENABLE_DAC` | `hal_dac.h` | target `hal_dac.cpp` | True-DAC capability facade; STM32G474 provides hardware output, while RP2040 reports the capability as unsupported |
| `HAL_ENABLE_PCNT` | `hal_pcnt.h` | target `hal_pcnt.cpp` | Target pulse-counter facade for RP2040, STM32G474, ESP32-S3 PCNT, and mock targets |
| `HAL_ENABLE_RGB_LED` | `hal_rgb_led.h` + `hal/gpio/neopixel/jh_neopixel.h` | `hal_rgb_led.cpp` + `hal/gpio/neopixel/jh_neopixel.cpp` | Shared NeoPixel core + target transport (RP2040 PIO / STM32 cycle-timed GPIO / ESP32-S3 RMT) |
| `HAL_ENABLE_HD44780` | `hal_hd44780.h` + `hal/display/hd44780/hd44780.h` | `hal/display/hd44780/hd44780.cpp` | HD44780-compatible parallel character LCD over HAL GPIO/system timing |
| `HAL_ENABLE_DISPLAY` | `hal_display.h` | `hal/display/drivers/hal_display.cpp` | *(needs a TFT, OLED, LCD or EPD backend)* |
| `HAL_ENABLE_TFT` | `hal_display.h` | `hal/display/drivers/hal_display.cpp` | *(needs at least one TFT driver below; propagates DISPLAY + SPI)* |
| `HAL_ENABLE_ILI9341` | `hal_display.h` + `hal/display/drivers/ili9341_driver.h` | `hal/display/drivers/hal_display.cpp` + `hal/display/drivers/ili9341_driver.cpp` | shared HAL SPI/GPIO ILI9341 core + GFX engine (propagates TFT + DISPLAY + SPI) |
| `HAL_ENABLE_ST7789` | `hal_display.h` + `hal/display/drivers/st77xx_driver.h` | `hal/display/drivers/hal_display.cpp` + `hal/display/drivers/st77xx_driver.cpp` | shared HAL SPI/GPIO ST77xx core + GFX engine (propagates TFT + DISPLAY + SPI) |
| `HAL_ENABLE_ST7735` | `hal_display.h` + `hal/display/drivers/st77xx_driver.h` | `hal/display/drivers/hal_display.cpp` + `hal/display/drivers/st77xx_driver.cpp` | shared HAL SPI/GPIO ST77xx core + GFX engine (propagates TFT + DISPLAY + SPI) |
| `HAL_ENABLE_ST7796S` | `hal_display.h` + `hal/display/drivers/st77xx_driver.h` | `hal/display/drivers/hal_display.cpp` + `hal/display/drivers/st77xx_driver.cpp` | shared HAL SPI/GPIO ST77xx core + GFX engine (propagates TFT + DISPLAY + SPI) |
| `HAL_ENABLE_GC9A01` | `hal_display.h` + `hal/display/drivers/st77xx_driver.h` | `hal/display/drivers/hal_display.cpp` + `hal/display/drivers/st77xx_driver.cpp` | shared HAL SPI/GPIO GC9A01 round-TFT core + GFX engine (propagates TFT + DISPLAY + SPI) |
| `HAL_ENABLE_SSD1306` | `hal_display.h` + `hal/display/drivers/ssd1306_driver.h` | `hal/display/drivers/hal_display.cpp` + `hal/display/drivers/ssd1306_driver.cpp` | shared HAL SSD1306-family OLED core (`SSD1306`/`SSD1309`/`SSD1315`/`SH1106`/`CH1115`) + GFX engine; I2C is auto-enabled, SPI transport is available when `HAL_ENABLE_SPI` is also enabled (propagates DISPLAY + I2C) |
| `HAL_ENABLE_SSD1331` | `hal_display.h` + `hal/display/drivers/rgb_oled_driver.h` | `hal/display/drivers/hal_display.cpp` + `hal/display/drivers/rgb_oled_driver.cpp` | SSD1331 RGB565 OLED facade/backend over HAL SPI/GPIO (propagates DISPLAY + SPI) |
| `HAL_ENABLE_SSD135X` | `hal_display.h` + `hal/display/drivers/rgb_oled_driver.h` | `hal/display/drivers/hal_display.cpp` + `hal/display/drivers/rgb_oled_driver.cpp` | SSD1351/SSD1357 RGB565 OLED facade/backend over HAL SPI/GPIO (propagates DISPLAY + SPI) |
| `HAL_ENABLE_ST7567` | `hal_display.h` + `hal/display/drivers/st7567_driver.h` | `hal/display/drivers/hal_display.cpp` + `hal/display/drivers/st7567_driver.cpp` | ST7567 raw monochrome facade/backend over HAL I2C or SPI/GPIO (propagates DISPLAY + I2C; SPI transport also needs SPI) |
| `HAL_ENABLE_SSD16XX` | `hal_display.h` + `hal/display/drivers/ssd16xx_driver.h` | display facade + shared EPD transport + SSD16xx driver | SSD1608/SSD1673/SSD1675A/SSD1680/SSD1681 raw MONO10 EPD backend (propagates DISPLAY + SPI) |
| `HAL_ENABLE_UC81XX` | `hal_display.h` + `hal/display/drivers/uc81xx_driver.h` | display facade + shared EPD transport + UC81xx driver | UC8175/UC8176/UC8151D/UC8179 raw MONO10 EPD backend (propagates DISPLAY + SPI) |
| `HAL_ENABLE_CRYPTO` | `hal_crypto.h` + `hal_sc_auth.h` | `hal_crypto.cpp` + `hal_sc_auth.cpp` | Base64, MD5, SHA-256, HMAC-SHA256, ChaCha20-Poly1305 |
| `HAL_ENABLE_CRC` | `hal_crc.h` | `hal_crc.cpp` | generic CRC-8/16/32 checksums for integrity (auto-enabled by ONEWIRE/DS18B20) |
| `HAL_ENABLE_CELLULAR_MODEM` | `hal_modem_at.h` | `hal_modem_at.cpp` | *(facade - needs a modem-family backend such as `HAL_ENABLE_A7670`)* |
| `HAL_ENABLE_A7670` | `hal_simcom_a76xx.h` | `hal_simcom_a76xx.cpp` | SimCom A76xx-family driver (propagates CELLULAR_MODEM + UART) |
| `HAL_ENABLE_CJSON` | `hal/codecs/cjson/cJSON.h`, `hal/codecs/cjson/cJSON_Utils.h` (`tools.h` from C++) | `hal/codecs/cjson/cJSON.c`, `hal/codecs/cjson/cJSON_Utils.c` | managed cJSON checkout with tracked wrappers |
| `HAL_ENABLE_PNG` | `hal/codecs/lodepng/lodepng.h` (`tools.h` from C++) | `hal/codecs/lodepng/lodepng.cpp` | managed LodePNG checkout with a tracked embedded-profile wrapper |
| `HAL_ENABLE_PNG_AS_BASE64` | `utils/tools_api.h` helpers + `hal/codecs/lodepng/lodepng.h` + `hal_crypto.h` | `utils/tools.cpp` + `hal/codecs/lodepng/lodepng.cpp` + `hal_crypto.cpp` | Base64-encoded PNG decode helpers (propagates CRYPTO + PNG) |
| `HAL_ENABLE_JPEG` | `hal/codecs/jpeg/tjpgd.h` (`tools.h` from C++) | `hal/codecs/jpeg/tjpgd.c` + `utils/tools.cpp` | managed TJpgDec core with memory input and RGB565 output |
| `HAL_ENABLE_JPEG_AS_BASE64` | `utils/tools_api.h` helpers + `hal/codecs/jpeg/tjpgd.h` + `hal_crypto.h` | `utils/tools.cpp` + `hal/codecs/jpeg/tjpgd.c` + `hal_crypto.cpp` | Base64-encoded JPEG decode helpers (propagates CRYPTO + JPEG) |
| `HAL_ENABLE_UNITY` | utility headers/sources | `utils/unity.*` | managed Unity framework |

### Opt-out flag

| Flag | Effect |
|---|---|
| `HAL_DISABLE_ASSERTS` | Compiles every `HAL_ASSERT()` to a no-op. Asserts are ON by default. Mirrors the standard `NDEBUG` convention. |

### Generated feature resolution

The registry resolver keeps direct requests and their closure separate:

* `requestedFeatures` contains normalized, direct requests collected from the
  effective project and build inputs;
* `resolvedFeatures` adds the transitive registry `implies` closure. Production
  source/dependency selection and the board/link feature hash use this set.

The compiler receives the requested feature definitions. The generated
`src/hal/generated/jh_hal_features.h` header materializes the same registry
closure for C and C++ preprocessing. The following summary shows the public
feature implications; internal edges to the derived
`HAL_ENABLE_NETWORK_CORE` symbol are omitted:

```
HAL_ENABLE_KV          -> HAL_ENABLE_EEPROM
HAL_ENABLE_SDLOGGER    -> HAL_ENABLE_FAT + HAL_ENABLE_EEPROM + HAL_ENABLE_SPI
HAL_ENABLE_BLE_STREAM  -> HAL_ENABLE_BLE + HAL_ENABLE_CRYPTO
HAL_ENABLE_TIME        -> HAL_ENABLE_UDP + HAL_ENABLE_WIFI
HAL_ENABLE_MQTT        -> HAL_ENABLE_TCP + HAL_ENABLE_WIFI
HAL_ENABLE_UDP         -> HAL_ENABLE_WIFI
HAL_ENABLE_TCP         -> HAL_ENABLE_WIFI
HAL_ENABLE_HTTP_SERVER -> HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_HTTP_FILES  -> HAL_ENABLE_HTTP_SERVER -> HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_WEBSOCKET   -> HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_NET_CONSOLE -> HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_NET_COMMANDS -> HAL_ENABLE_HTTP_SERVER + HAL_ENABLE_WEBSOCKET +
                           HAL_ENABLE_CJSON + HAL_ENABLE_TCP + HAL_ENABLE_WIFI
HAL_ENABLE_NOTIFY_TELEGRAM -> HAL_ENABLE_NOTIFY + HAL_ENABLE_HTTP_CLIENT +
                              HAL_ENABLE_TLS + HAL_ENABLE_CJSON +
                              HAL_ENABLE_TCP + HAL_ENABLE_WIFI
HAL_ENABLE_BSD_SOCKETS -> HAL_ENABLE_UDP + HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_TLS         -> HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_HTTP_CLIENT -> HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_OTA         -> HAL_ENABLE_WIFI + HAL_ENABLE_UDP + HAL_ENABLE_TCP + HAL_ENABLE_CRYPTO + HAL_ENABLE_CRC
HAL_ENABLE_WIREGUARD   -> HAL_ENABLE_UDP + HAL_ENABLE_WIFI
HAL_ENABLE_EXTERNAL_ADC-> HAL_ENABLE_I2C
HAL_ENABLE_BH1750      -> HAL_ENABLE_I2C
HAL_ENABLE_ADP5360     -> HAL_ENABLE_I2C
HAL_ENABLE_MCP3221     -> HAL_ENABLE_I2C
HAL_ENABLE_TSC2007     -> HAL_ENABLE_I2C
HAL_ENABLE_STMPE610    -> HAL_ENABLE_I2C + HAL_ENABLE_SPI
HAL_ENABLE_PCF8563     -> HAL_ENABLE_RTC + HAL_ENABLE_I2C
HAL_ENABLE_DS3231      -> HAL_ENABLE_RTC + HAL_ENABLE_I2C
HAL_ENABLE_INTERNAL_RTC-> HAL_ENABLE_RTC
HAL_ENABLE_POWER_MANAGEMENT -> HAL_ENABLE_INTERNAL_RTC -> HAL_ENABLE_RTC
HAL_ENABLE_MCP9600     -> HAL_ENABLE_THERMOCOUPLE + HAL_ENABLE_I2C
HAL_ENABLE_MAX6675     -> HAL_ENABLE_THERMOCOUPLE
HAL_ENABLE_MCP401X     -> HAL_ENABLE_DIGIPOT + HAL_ENABLE_I2C
HAL_ENABLE_MAX5395     -> HAL_ENABLE_DIGIPOT + HAL_ENABLE_I2C
HAL_ENABLE_PGA2311     -> HAL_ENABLE_SPI
HAL_ENABLE_MCP23017    -> HAL_ENABLE_I2C
HAL_ENABLE_PCA9654E    -> HAL_ENABLE_I2C
HAL_ENABLE_PCF8574     -> HAL_ENABLE_I2C
HAL_ENABLE_HC595       -> HAL_ENABLE_SPI
HAL_ENABLE_MCP4725     -> HAL_ENABLE_I2C
HAL_ENABLE_MFRC522     -> HAL_ENABLE_SPI
HAL_ENABLE_PN532       -> HAL_ENABLE_SPI
HAL_ENABLE_DS18B20     -> HAL_ENABLE_ONEWIRE
HAL_ENABLE_ONEWIRE     -> HAL_ENABLE_CRC
HAL_ENABLE_A7670       -> HAL_ENABLE_CELLULAR_MODEM + HAL_ENABLE_UART
HAL_ENABLE_MCP2515     -> HAL_ENABLE_CAN + HAL_ENABLE_SPI
HAL_ENABLE_MCP251XFD   -> HAL_ENABLE_CAN + HAL_ENABLE_SPI
HAL_ENABLE_STM32G474_FDCAN -> HAL_ENABLE_CAN (STM32G474 only)
HAL_ENABLE_{ILI9341,ST7789,ST7735,ST7796S,GC9A01} -> HAL_ENABLE_TFT -> HAL_ENABLE_DISPLAY + HAL_ENABLE_SPI
HAL_ENABLE_SSD1306     -> HAL_ENABLE_DISPLAY + HAL_ENABLE_I2C
                           (SPI OLED transport additionally needs HAL_ENABLE_SPI)
HAL_ENABLE_{SSD1331,SSD135X} -> HAL_ENABLE_DISPLAY + HAL_ENABLE_SPI
HAL_ENABLE_ST7567      -> HAL_ENABLE_DISPLAY + HAL_ENABLE_I2C
HAL_ENABLE_{SSD16XX,UC81XX} -> HAL_ENABLE_DISPLAY + HAL_ENABLE_SPI
HAL_ENABLE_DACLESS     -> HAL_ENABLE_DMA_PWM_AUDIO + HAL_ENABLE_PWM_FREQ
HAL_ENABLE_PNG_AS_BASE64 -> HAL_ENABLE_CRYPTO + HAL_ENABLE_PNG
HAL_ENABLE_JPEG_AS_BASE64 -> HAL_ENABLE_CRYPTO + HAL_ENABLE_JPEG
```

You only need to enable the **leaf** module you actually use; everything
upstream is pulled in for you.

### Rules retained outside feature registry v1

`hal_config.h` remains the public configuration facade for contextual rules
that registry v1 cannot express:

| Category | Retained behavior |
|---|---|
| Conditional feature propagation | `HAL_ENABLE_EEPROM` with `HAL_EEPROM_TYPE == EEPROM_TYPE_AT24C256` adds `HAL_ENABLE_I2C`. `HAL_ENABLE_GPS` adds `HAL_ENABLE_UART` only when neither UART nor SWSERIAL was selected. |
| Provider and choice rules | Network configuration selects exactly one backend and checks its required capabilities. Facades validate providers for RTC, cellular modem, thermocouple, CAN, digital potentiometer, GPS transport, display and TFT. |
| Target and board rules | BLE controller/target/board support, CYW43 bus/stack/profile/pin and target/board constraints, FreeRTOS target/toolchain/header constraints, and the STM32G474-only FDCAN rule remain contextual. |
| Defaults, tunables, layout and ranges | Target-dependent EEPROM defaults, storage/OTA region layout, CYW43 pin/clock/country defaults, pool sizes, backlog and TLS limits, plus the remaining tunable defaults and range checks stay in the facade. |

These retained sections contain all 46 production compile-time `#error`
checks.

Registry `resolvedFeatures` and its feature hash describe the registry v1
closure. They do not append the two contextual propagation results above. A
GPS-only request can therefore finish preprocessing with `HAL_ENABLE_UART`,
and AT24 EEPROM can finish with `HAL_ENABLE_I2C`, even though those additions
are absent from `resolvedFeatures` and the feature hash.

With `HAL_CONFIG_VERBOSE`, the generated header checks the complete inventory
of all 101 registered `HAL_ENABLE_*` and `HAL_DISABLE_*` symbols. The report is
emitted after the retained conditional propagation, so its `#pragma message`
output describes the final preprocessor state, including contextual I2C or
UART additions.

### Passing flags - recommended: `hal_project_config.h`

Create `hal_project_config.h` in your firmware project directory and enable the
modules you use:

```c
#pragma once
#define HAL_ENABLE_WIFI
#define HAL_ENABLE_KV            // -> propagates EEPROM
#define HAL_ENABLE_GPS           // -> auto-enables UART when no transport is selected
#define HAL_ENABLE_MCP9600       // -> propagates THERMOCOUPLE + I2C
#define HAL_ENABLE_BH1750        // -> propagates I2C
#define HAL_ENABLE_TSC2007       // -> propagates I2C
#define HAL_ENABLE_STMPE610      // -> propagates I2C + SPI
#define HAL_ENABLE_IRSMALL_DECODER
#define HAL_ENABLE_HD44780 // HD44780-compatible character LCD over GPIO
#define HAL_ENABLE_DACLESS // DACless PWM-audio engine -> DMA_PWM_AUDIO + PWM_FREQ
#define HAL_ENABLE_UART
#define HAL_ENABLE_PCF8563       // -> propagates RTC + I2C
#define HAL_ENABLE_INTERNAL_RTC  // -> RTC; native on STM32G474 and RP family
#define HAL_ENABLE_POWER_MANAGEMENT // -> INTERNAL_RTC + RTC
#define HAL_ENABLE_PWM_FREQ
```

The target-selection path detects it via
`__has_include("hal_project_config.h")` before target auto-detection. Keep the
header macro-only and avoid includes or conditions based on derived
`HAL_TARGET_IS_*` and `HAL_BOARD_IS_*` macros, which are resolved afterward.
Feature definitions used for source selection must be unconditional
`#define HAL_ENABLE_X` or `#define HAL_ENABLE_X 1`; the only supported
conditional form is a same-symbol `#ifndef HAL_ENABLE_X` guard. Do not put
feature definitions under any other `#if`/`#ifdef`, including raw or derived
target/board branches, because the early collector reads the file textually.

### FreeRTOS availability flag

`HAL_ENABLE_FREERTOS` is a target/runtime integration flag rather than an
optional module flag. It is intended for projects that want to include native
FreeRTOS headers directly:

```c
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
```

Target rules:

Direct dispatcher/CMake paths for both native RP and STM32G474 invoke
`scripts/component_manager.py component freertos --enable` automatically when
FreeRTOS is selected. Explicit static-library helper modes invoke
`scripts/ensure_freertos_kernel.sh` first: `--freertos` for RP and STM32G474,
and an explicit `-D HAL_ENABLE_FREERTOS` for STM32G474. The wrapper delegates
to the same manager. If the feature comes only from `hal_project_config.h` (or
from RP `-D`), the CMake fallback still prepares the kernel. An external
`JH_FREERTOS_KERNEL_DIR` is verified and never replaced.

- Native RP2040/RP2350: use `./scripts/build_rp_native_lib.sh --freertos` or
  select `examples/18_freertos_suite` through the normal VS Code target.
  CMake selects the pinned SMP port for RP2040, RP2350 ARM_NTZ or RP2350
  RISC-V, links `heap_4`, creates `app_task0()` pinned to core 0 and optional
  `app_task1()` pinned to core 1, then starts the scheduler. Native USB is
  serviced by a dedicated task pinned to core 0. The hardware fixture under
  `tests/hardware/rp_freertos_smp` verifies both affinities, cross-core mutexes,
  scheduler/heap state and CDC backpressure.
- STM32G474: use the pinned `third_party/FreeRTOS-Kernel` dependency from
  `third_party/freertos_core_version.conf`, or pass
  `-DJH_FREERTOS_KERNEL_DIR=/path/to/FreeRTOS-Kernel`. STM32 CMake builds
  compile the explicit Cortex-M4F kernel source list, include the target
  `FreeRTOSConfig.h`, use `heap_4.c`, and let the FreeRTOS port own
  SVC/PendSV/SysTick. In FreeRTOS mode, STM32 `hal_mutex_*` uses FreeRTOS
  mutexes, `hal_delay_ms()` uses `vTaskDelay()` from legal task context, and
  `hal_idle()` yields to the scheduler from legal task context. When
  `HAL_PROVIDE_APP_ENTRY` is also defined, HAL calls `app_start()`, creates an
  `app_task0()` FreeRTOS task, creates `app_task1()` only when
  `HAL_ENABLE_APP_TASK1` is defined, and then calls `vTaskStartScheduler()`.
- ESP32-S3: uses the FreeRTOS scheduler already started by the pinned ESP-IDF.
  `app_main()` calls `app_start()`, creates `app_task0()` on core 0 by default,
  optionally creates `app_task1()` on core 1, and returns to ESP-IDF. The target
  requires `HAL_ENABLE_FREERTOS`. `HAL_FREERTOS_TASK0_CORE` and
  `HAL_FREERTOS_TASK1_CORE` accept either target core or `-1` for no affinity.
- Host/mock: `HAL_ENABLE_FREERTOS` is not supported by the normal mock backend.
  CI uses the optional `JH_ENABLE_FREERTOS_POSIX_TESTS` host build to compile the
  FreeRTOS kernel GCC/Posix port, run a real scheduler as pthreads, and exercise
  the STM32G474 host-stub `HAL_ENABLE_FREERTOS` paths in `ctest`.

HAL-provided native FreeRTOS entry task defaults:

| Macro | Default | Unit / meaning |
|---|---|---|
| `HAL_FREERTOS_CORE_COUNT` | `2` | Native RP scheduler core count; allowed values are `1` and `2`. A single-core build must not enable `HAL_ENABLE_APP_TASK1`. |
| `HAL_FREERTOS_TASK0_STACK` | `512` | FreeRTOS stack words for `app_task0()` |
| `HAL_FREERTOS_TASK1_STACK` | `512` | FreeRTOS stack words for `app_task1()` |
| `HAL_FREERTOS_TASK0_PRIORITY` | `tskIDLE_PRIORITY + 1` | FreeRTOS priority for `app_task0()` |
| `HAL_FREERTOS_TASK1_PRIORITY` | `tskIDLE_PRIORITY + 1` | FreeRTOS priority for `app_task1()` |
| `HAL_FREERTOS_HEAP_SIZE` | `164 * 1024` | RP `heap_4` pool size in bytes |
| `HAL_USB_FREERTOS_TASK_STACK` | `512` | RP core-0 USB worker stack in FreeRTOS words |
| `HAL_USB_FREERTOS_TASK_PRIORITY` | `tskIDLE_PRIORITY + 2` | RP core-0 USB worker priority |

ESP32-S3 overrides both application stack defaults to `3072` ESP-IDF
stack-depth bytes and adds core defaults `HAL_FREERTOS_TASK0_CORE=0` and
`HAL_FREERTOS_TASK1_CORE=1`. Application task priorities retain the shared
`tskIDLE_PRIORITY + 1` defaults.

STM32G474 uses a 24 KiB `heap_4` pool from its target-local
`FreeRTOSConfig.h`. Application task stacks use the same 512-word defaults;
the linker `_Min_Stack_Size` remains the boot/exception stack reservation.

Platform stack-size overrides:

| Macro | Default | Unit / meaning |
|---|---|---|
| `HAL_STM32_MAIN_STACK_SIZE` | `0x800` | Bytes reserved as STM32 `_Min_Stack_Size` (linker reserve between heap and top-of-RAM stack) |
| `HAL_RP_CORE0_STACK_SIZE` | `0x800` | Bytes mapped to `PICO_STACK_SIZE` for any native RP target |
| `HAL_RP_CORE1_STACK_SIZE` | `HAL_RP_CORE0_STACK_SIZE` / `0x800` | Bytes mapped to `PICO_CORE1_STACK_SIZE` for any native RP target |

Thread-safety note: RP2040, STM32G474, and ESP32-S3 FreeRTOS modes provide
mutex/delay/idle primitives, while `hal_critical_section_*` uses the target's
hard interrupt-critical mechanism for timing-sensitive code. ESP32-S3 also
serializes both cores with a shared `portMUX_TYPE`. The implementation includes
atomic create-once fallbacks for singleton/per-bus mutexes and hardens the
RP2040 I2C-slave callback path. Timer callback context and remaining per-module
exceptions require dedicated module-level audits before stronger thread-safety
guarantees are documented.

The VS Code project flow adds the project include path automatically through
the shared dispatcher. Generated projects should use the `Project: Build` and
`Project: Select board` tasks emitted by `jh-vscode`.

### Alternative: `-D` flags on the command line

```bash
./scripts/build_rp_native_lib.sh \
  --target rp2040 \
  --board picow \
  -D HAL_ENABLE_WIFI \
  -D HAL_ENABLE_EEPROM \
  -D HAL_ENABLE_GPS \
  -D HAL_ENABLE_I2C
```

### Core modules (no disable flag)

| Module | Purpose |
|---|---|
| `hal_gpio` | GPIO read / write / interrupts |
| `hal_adc` | On-chip ADC |
| `hal_pwm` | Basic analogWrite PWM |
| `hal_timer` | Low-level one-shot alarms plus managed timers (periodic/one-shot create/start/stop/pause/resume/query) |
| `hal_system` | millis / delay / watchdog / idle + type-independent `hal_constrain` / `hal_map` + `COUNTOF(arr)` |
| `hal_bits` | bit helpers (`is_set`, `set_bit`, `bitSet`, volatile register ops) |
| `hal_sync` | Mutexes, critical sections |
| `hal_serial` | Debug serial output |
| `hal_spi` | SPI bus init |
| `hal_math` | type-independent `hal_constrain` / `hal_map` macros |

`hal_crypto` is opt-in via `HAL_ENABLE_CRYPTO` (it is not part of the always-on core set).

### Dependency ownership

Optional third-party integrations used by HAL modules are selected by CMake.
Target-specific RP helpers live under
`src/hal/impl/rp2040/drivers/rp2040/`. Portable headers, facades, device drivers,
and reusable engines are co-located by topic under `src/hal/<domain>/`.
`src/hal/impl/` is reserved for `.mock`, `rp2040`, and `stm32g474` backends.

Actual compiled dependencies are controlled by the module set:

- enabled modules (`HAL_ENABLE_*`) pull in their third-party backends
- modules left disabled (the default) compile out both declarations and
  implementation details

\* `HAL_ENABLE_TIME` enables the shared runtime clock, status, NTP, and
local-time APIs. With `HAL_ENABLE_RTC`, it can restore from RTC and persist
validated NTP results. The pure
`hal_time_from_components(...)`, `hal_time_is_daylight_saving_time(...)`,
`hal_time_adjust_cet_cest(...)`, `hal_time_is_in_range(...)`, and
`hal_time_extract_minutes(...)` helpers remain available unconditionally with
no network dependency.

---


---

*Next: [Multicore safety, drivers, migration guide](03_build_tests.md)*
