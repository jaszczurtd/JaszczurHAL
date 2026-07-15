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

Modules that are not enabled cost zero code, zero RAM, and pull in no
third-party libraries via arduino-cli.

### Available flags

Application entry flags are separate from optional HAL modules:

| Flag | Effect |
|---|---|
| `HAL_ENABLE_APP_TASK1` | Dispatches optional `app_task1()` from the HAL-provided entry path. On RP2040 this emits Arduino `loop1()`, which starts the core-1 path. On STM32G474 FreeRTOS entry builds this creates the second application task. Leave undefined for single-loop/single-task apps. |

FreeRTOS integration is also an explicit opt-in, but it is not a HAL module:

| Flag | Effect |
|---|---|
| `HAL_ENABLE_FREERTOS` | Enables native FreeRTOS availability checks for the selected target. RP2040 must be built in arduino-pico FreeRTOS mode so `__FREERTOS` is defined; arduino-pico owns scheduler startup and optional `app_task1()` dispatch remains `loop1()` gated by `HAL_ENABLE_APP_TASK1`. STM32G474 builds use a pinned `FreeRTOS-Kernel` checkout from `freertos_core_version.conf` plus the target `FreeRTOSConfig.h`, explicit Cortex-M4F kernel source list, `heap_4.c`, and FreeRTOS-owned SVC/PendSV/SysTick vectors. With `HAL_PROVIDE_APP_ENTRY`, STM32 calls `app_start()`, creates the `app_task0()` task and optional `app_task1()` task, then starts the scheduler. On both supported targets, `hal_mutex_*`, `hal_delay_ms()`, and `hal_idle()` select FreeRTOS-aware paths. This flag does not add a public `hal_rtos_*` API and does not by itself make every HAL module task-safe. |

| Flag | Header | Impl | 3rd-party deps pulled in |
|---|---|---|---|
| `HAL_ENABLE_WIFI` | `hal_wifi.h` | `hal_wifi.cpp` | WiFi (arduino-pico) |
| `HAL_ENABLE_TIME` | `hal_time.h` | `hal_time.cpp` | WiFi NTP helpers (propagates WIFI) |
| `HAL_ENABLE_MQTT` | `hal_mqtt.h` | `hal_mqtt.cpp` | PubSubClient (propagates WIFI) |
| `HAL_ENABLE_UDP`  | `hal_udp.h`  | `hal_udp.cpp`  | WiFiUDP (propagates WIFI) |
| `HAL_ENABLE_TCP` | `hal_tcp.h` | `hal_tcp.cpp` | WiFiClient/WiFiServer TCP transport (propagates WIFI) |
| `HAL_ENABLE_HTTP_SERVER` | `hal_http_server.h` | `impl/shared/compat/http_server/hal_http_server.cpp` | Small poll-driven HTTP/1.1 server over HAL TCP (propagates TCP + WIFI) |
| `HAL_ENABLE_HTTP_FILES` | `hal_http_files.h` | `impl/shared/compat/http_files/hal_http_files.cpp` | Callback-backed file serving, ETag and upload helpers over HAL HTTP routes (propagates HTTP_SERVER + TCP + WIFI) |
| `HAL_ENABLE_WEBSOCKET` | `hal_websocket.h` | `impl/shared/compat/websocket/hal_websocket.cpp` | Small poll-driven WebSocket server over HAL TCP (propagates TCP + WIFI) |
| `HAL_ENABLE_NET_CONSOLE` | `hal_net_console.h` | `impl/shared/compat/net_console/hal_net_console.cpp` | Password-protected serial/debug mirror and command stream over HAL TCP (propagates TCP + WIFI) |
| `HAL_ENABLE_NET_COMMANDS` | `hal_net_commands.h` | `impl/shared/compat/net_commands/hal_net_commands.cpp` | Shared JSON/text command dispatcher for HTTP and WebSocket control channels (propagates HTTP_SERVER + WEBSOCKET + CJSON + TCP + WIFI) |
| `HAL_ENABLE_BSD_SOCKETS` | `sys/socket.h`, `netinet/in.h`, `arpa/inet.h`, `netdb.h`, `fcntl.h`, `sys/select.h`, `unistd.h` | `impl/shared/compat/bsd_sockets/hal_bsd_sockets.cpp` | BSD/POSIX compatibility over HAL UDP/TCP, including IPv4 `getaddrinfo()` (propagates UDP + TCP + WIFI) |
| `HAL_ENABLE_OTA`  | `hal_ota.h`  | `hal_ota.cpp`  | ArduinoOTA (propagates WIFI) |
| `HAL_ENABLE_WIREGUARD` | `hal_wireguard.h` | `hal_wireguard.cpp` | bundled WireGuard (propagates WIFI) |
| `HAL_ENABLE_EEPROM` | `hal_eeprom.h` | `hal_eeprom.cpp` | Target flash EEPROM emulation; AT24C256 over HAL I2C when selected |
| `HAL_ENABLE_KV` | `hal_kv.h` | `hal_kv.cpp` | *(propagates EEPROM)* |
| `HAL_ENABLE_LITTLEFS` | `hal_littlefs.h` | `hal_littlefs.cpp` | LittleFS lifecycle helpers; STM32G474 uses `HAL_STM32_FLASH_LITTLEFS_SIZE` |
| `HAL_ENABLE_SDLOGGER` | `hal_sdlogger.h` | `impl/shared/frameworks/filesystem/sdlogger/hal_sdlogger.cpp` | SD logger over shared FatFs (propagates FAT + EEPROM + SPI) |
| `HAL_ENABLE_UART` | `hal_uart.h` | `hal_uart.cpp` | Hardware UART |
| `HAL_ENABLE_SWSERIAL` | `hal_swserial.h` | target `hal_swserial.cpp` | Native Pico SDK PIO/DMA software UART on RP2040; shared HAL GPIO backend on other targets |
| `HAL_ENABLE_I2C` | `hal_i2c.h` | `hal_i2c.cpp` | I2C master/controller bus |
| `HAL_ENABLE_I2C_SLAVE` | `hal_i2c_slave.h` | `hal_i2c_slave.cpp` | I2C slave/target register-map mode |
| `HAL_ENABLE_SPI` | `hal_spi.h` | `hal_spi.cpp` | SPI master/controller |
| `HAL_ENABLE_CAN` | `hal_can.h` | `hal_can.cpp` + `hal_can_util.cpp` | Generic CAN API facade; requires at least one backend |
| `HAL_ENABLE_MCP2515` | `hal_can.h` + `impl/shared/drivers/mcp2515/mcp2515_driver.h` | target `hal_can.cpp` facade + `impl/shared/drivers/mcp2515/hal_can_mcp2515.cpp` + `impl/shared/drivers/mcp2515/hal_can_mcp2515_config.cpp` + `impl/shared/drivers/mcp2515/mcp2515_driver.cpp` | Shared Arduino-free MCP2515 CAN backend (propagates CAN + SPI) |
| `HAL_ENABLE_MCP251XFD` | `hal_can.h` + `impl/shared/drivers/mcp251xfd/mcp251xfd_driver.h` | target `hal_can.cpp` facade + `impl/shared/drivers/mcp251xfd/hal_can_mcp251xfd.cpp` + `impl/shared/drivers/mcp251xfd/hal_can_mcp251xfd_config.cpp` + `impl/shared/drivers/mcp251xfd/mcp251xfd_driver.cpp` | Shared MCP2517FD/MCP2518FD CAN FD backend (propagates CAN + SPI) |
| `HAL_ENABLE_STM32G474_FDCAN` | `hal_can.h` | `impl/stm32g474/hal_can.cpp` + `impl/stm32g474/hal_can_stm32g474_fdcan.cpp` + `impl/stm32g474/hal_can_stm32g474_fdcan_config.cpp` | Native STM32G474 FDCAN1 CAN FD backend (propagates CAN; compile-time rejected outside STM32G474) |
| `HAL_ENABLE_RTC` | `hal_rtc.h` | `hal_rtc.cpp` | *(needs PCF8563 or DS3231 backend)* |
| `HAL_ENABLE_PCF8563` | `hal_rtc.h` | `hal_rtc.cpp` | PCF8563 backend (propagates RTC + I2C) |
| `HAL_ENABLE_DS3231` | `hal_rtc.h` | `hal_rtc.cpp` | DS3231 backend (propagates RTC + I2C) |
| `HAL_ENABLE_THERMOCOUPLE` | `hal_thermocouple.h` | `hal_thermocouple.cpp` | *(needs MCP9600 or MAX6675 backend)* |
| `HAL_ENABLE_MCP9600` | `hal_thermocouple.h` + `impl/shared/drivers/mcp9600/mcp9600_driver.h` | `hal_thermocouple.cpp` + `impl/shared/drivers/mcp9600/mcp9600_driver.cpp` | shared Arduino-free MCP9600/MCP9601 driver (propagates THERMOCOUPLE + I2C) |
| `HAL_ENABLE_MAX6675` | `hal_thermocouple.h` + `impl/shared/drivers/max6675/max6675_driver.h` | `hal_thermocouple.cpp` + `impl/shared/drivers/max6675/max6675_driver.cpp` | shared Arduino-free MAX6675 bit-bang driver (propagates THERMOCOUPLE) |
| `HAL_ENABLE_DS18B20` | `hal_ds18b20.h` + `impl/shared/drivers/onewire/onewire_driver.h` | `impl/shared/drivers/ds18b20/hal_ds18b20.cpp` + `impl/shared/drivers/onewire/onewire_driver.cpp` | shared Arduino-free DS18B20 backend over 1-Wire (propagates ONEWIRE) |
| `HAL_ENABLE_DHT` | `hal_dht.h` | `impl/shared/drivers/dht/hal_dht.cpp` | shared DHT11/DHT22 temperature/humidity driver over HAL GPIO |
| `HAL_ENABLE_BH1750` | `hal_bh1750.h` | `impl/shared/drivers/bh1750/hal_bh1750.cpp` | shared HAL I2C BH1750 ambient-light sensor driver (propagates I2C) |
| `HAL_ENABLE_ADP5360` | `hal_adp5360.h` | `impl/shared/drivers/adp5360/hal_adp5360.cpp` | shared HAL I2C ADP5360 PMIC driver: MFD init/reset/shipment, charger, fuel-gauge and buck/buck-boost regulator control (propagates I2C) |
| `HAL_ENABLE_MCP3221` | `hal_mcp3221.h` | `impl/shared/drivers/simple_io/hal_simple_io_drivers.cpp` | MCP3221 12-bit ADC over HAL I2C (propagates I2C) |
| `HAL_ENABLE_TSC2007` | `hal_tsc2007.h` | `impl/shared/drivers/tsc2007/tsc2007.cpp` | shared HAL I2C TSC2007 resistive touch controller driver (propagates I2C) |
| `HAL_ENABLE_STMPE610` | `hal_stmpe610.h` | `impl/shared/drivers/stmpe610/stmpe610.cpp` | shared HAL I2C/SPI STMPE610 resistive touch controller driver (propagates I2C + SPI) |
| `HAL_ENABLE_IRSMALL_DECODER` | `hal_irsmall_decoder.h` | `impl/shared/frameworks/irsmall_decoder/irsmall_decoder.cpp` | shared HAL GPIO interrupt infrared receiver decoder |
| `HAL_ENABLE_ONEWIRE` | `hal_onewire.h` + `impl/shared/drivers/onewire/onewire_driver.h` | `impl/shared/drivers/onewire/hal_onewire.cpp` + `impl/shared/drivers/onewire/onewire_driver.cpp` | shared Arduino-free 1-Wire bit-bang driver (propagates CRC) |
| `HAL_ENABLE_EXTERNAL_ADC` | `hal_external_adc.h` + `impl/shared/drivers/ads1x15/ads1x15_driver.h` | `impl/shared/drivers/ads1x15/hal_external_adc_ads1x15.cpp` + `impl/shared/drivers/ads1x15/ads1x15_driver.cpp` | shared Arduino-free ADS1X15/ADS1115 driver (propagates I2C) |
| `HAL_ENABLE_GPS` | `hal_gps.h` | `hal_gps.cpp` + `impl/shared/frameworks/gps/gps_nmea_parser.cpp` | portable NMEA engine (RP2040 + STM32G474); needs a transport: SWSERIAL or UART |
| `HAL_ENABLE_DIGIPOT` | `hal_digipot.h` + `impl/shared/drivers/digipot/hal_digipot_ops.h` | `hal_digipot.cpp` + `impl/shared/drivers/digipot/*.cpp` | facade/pool/dispatch; needs MCP401X or MAX5395 backend |
| `HAL_ENABLE_MCP401X` | `hal_digipot.h` + `impl/shared/drivers/digipot/hal_digipot_ops.h` | `hal_digipot.cpp` + `impl/shared/drivers/digipot/digipot_mcp401x.cpp` | MCP4017/4018/4019 shared HAL I2C driver (propagates DIGIPOT + I2C) |
| `HAL_ENABLE_MAX5395` | `hal_digipot.h` + `impl/shared/drivers/digipot/hal_digipot_ops.h` | `hal_digipot.cpp` + `impl/shared/drivers/digipot/digipot_max5395.cpp` | MAX5395 shared HAL I2C driver (propagates DIGIPOT + I2C) |
| `HAL_ENABLE_PGA2311` | `hal_pga2311.h` + `impl/shared/drivers/pga2311/pga2311_driver.h` | `hal_pga2311.cpp` + `impl/shared/drivers/pga2311/pga2311_driver.cpp` | PGA2311 shared HAL SPI/GPIO stereo volume driver (propagates SPI) |
| `HAL_ENABLE_MCP23017` | `hal_mcp23017.h` | `impl/shared/drivers/simple_io/hal_simple_io_drivers.cpp` | MCP23017 GPIO expander over HAL I2C (propagates I2C) |
| `HAL_ENABLE_PCA9654E` | `hal_pca9654e.h` | `impl/shared/drivers/simple_io/hal_simple_io_drivers.cpp` | PCA9654E output expander over HAL I2C (propagates I2C) |
| `HAL_ENABLE_PCF8574` | `hal_pcf8574.h` | `impl/shared/drivers/simple_io/hal_simple_io_drivers.cpp` | PCF8574 quasi-bidirectional GPIO expander over HAL I2C (propagates I2C) |
| `HAL_ENABLE_HC595` | `hal_hc595.h` | `impl/shared/drivers/simple_io/hal_simple_io_drivers.cpp` | 74HC595 shift-register output expander over HAL SPI/GPIO (propagates SPI) |
| `HAL_ENABLE_MCP4725` | `hal_mcp4725.h` | `impl/shared/drivers/simple_io/hal_simple_io_drivers.cpp` | MCP4725 12-bit DAC over HAL I2C (propagates I2C) |
| `HAL_ENABLE_MFRC522` | `hal_mfrc522.h` + `impl/shared/drivers/mfrc522/mfrc522.h` | `impl/shared/drivers/mfrc522/mfrc522*.cpp` | MFRC522 RFID reader driver over HAL SPI/I2C (propagates SPI) |
| `HAL_ENABLE_PN532` | `hal_pn532.h` + `impl/shared/drivers/pn532/pn532.h` | `impl/shared/drivers/pn532/pn532*.cpp` | PN532 NFC/RFID reader driver over HAL SPI/I2C/UART (propagates SPI) |
| `HAL_ENABLE_DACLESS` | `hal_dacless.h` + `impl/shared/drivers/dacless/dacless.h` | `impl/shared/drivers/dacless/dacless.cpp` | Shared DACless PWM-audio engine with block/sample callbacks and ADC sampling (propagates DMA_PWM_AUDIO + PWM_FREQ) |
| `HAL_ENABLE_DMA_PWM_AUDIO` | `hal_dma_pwm_audio.h` | `hal_dma_pwm_audio.cpp` | Timer-paced PWM-audio DMA helper used by DACless |
| `HAL_ENABLE_PWM_FREQ` | `hal_pwm_freq.h` | `hal_pwm_freq.cpp` | RP2040 hardware/pwm or STM32G474 TIM PWM |
| `HAL_ENABLE_RGB_LED` | `hal_rgb_led.h` + `impl/shared/drivers/neopixel/jh_neopixel.h` | `hal_rgb_led.cpp` + `impl/shared/drivers/neopixel/jh_neopixel.cpp` | shared NeoPixel core + target transport (RP2040 PIO / STM32 cycle-timed GPIO) |
| `HAL_ENABLE_HD44780` | `hal_hd44780.h` + `impl/shared/drivers/hd44780/hd44780.h` | `impl/shared/drivers/hd44780/hd44780.cpp` | HD44780-compatible parallel character LCD over HAL GPIO/system timing |
| `HAL_ENABLE_DISPLAY` | `hal_display.h` | `impl/shared/drivers/display/hal_display.cpp` | *(needs a TFT, OLED, LCD or EPD backend)* |
| `HAL_ENABLE_TFT` | `hal_display.h` | `impl/shared/drivers/display/hal_display.cpp` | *(needs at least one TFT driver below; propagates DISPLAY + SPI)* |
| `HAL_ENABLE_ILI9341` | `hal_display.h` + `impl/shared/drivers/display/ili9341_driver.h` | `impl/shared/drivers/display/hal_display.cpp` + `impl/shared/drivers/display/ili9341_driver.cpp` | shared HAL SPI/GPIO ILI9341 core + GFX engine (propagates TFT + DISPLAY + SPI) |
| `HAL_ENABLE_ST7789` | `hal_display.h` + `impl/shared/drivers/display/st77xx_driver.h` | `impl/shared/drivers/display/hal_display.cpp` + `impl/shared/drivers/display/st77xx_driver.cpp` | shared HAL SPI/GPIO ST77xx core + GFX engine (propagates TFT + DISPLAY + SPI) |
| `HAL_ENABLE_ST7735` | `hal_display.h` + `impl/shared/drivers/display/st77xx_driver.h` | `impl/shared/drivers/display/hal_display.cpp` + `impl/shared/drivers/display/st77xx_driver.cpp` | shared HAL SPI/GPIO ST77xx core + GFX engine (propagates TFT + DISPLAY + SPI) |
| `HAL_ENABLE_ST7796S` | `hal_display.h` + `impl/shared/drivers/display/st77xx_driver.h` | `impl/shared/drivers/display/hal_display.cpp` + `impl/shared/drivers/display/st77xx_driver.cpp` | shared HAL SPI/GPIO ST77xx core + GFX engine (propagates TFT + DISPLAY + SPI) |
| `HAL_ENABLE_GC9A01` | `hal_display.h` + `impl/shared/drivers/display/st77xx_driver.h` | `impl/shared/drivers/display/hal_display.cpp` + `impl/shared/drivers/display/st77xx_driver.cpp` | shared HAL SPI/GPIO GC9A01 round-TFT core + GFX engine (propagates TFT + DISPLAY + SPI) |
| `HAL_ENABLE_SSD1306` | `hal_display.h` + `impl/shared/drivers/display/ssd1306_driver.h` | `impl/shared/drivers/display/hal_display.cpp` + `impl/shared/drivers/display/ssd1306_driver.cpp` | shared HAL SSD1306-family OLED core (`SSD1306`/`SSD1309`/`SSD1315`/`SH1106`/`CH1115`) + GFX engine; I2C is auto-enabled, SPI transport is available when `HAL_ENABLE_SPI` is also enabled (propagates DISPLAY + I2C) |
| `HAL_ENABLE_SSD1331` | `hal_display.h` + `impl/shared/drivers/display/rgb_oled_driver.h` | `impl/shared/drivers/display/hal_display.cpp` + `impl/shared/drivers/display/rgb_oled_driver.cpp` | SSD1331 RGB565 OLED facade/backend over HAL SPI/GPIO (propagates DISPLAY + SPI) |
| `HAL_ENABLE_SSD135X` | `hal_display.h` + `impl/shared/drivers/display/rgb_oled_driver.h` | `impl/shared/drivers/display/hal_display.cpp` + `impl/shared/drivers/display/rgb_oled_driver.cpp` | SSD1351/SSD1357 RGB565 OLED facade/backend over HAL SPI/GPIO (propagates DISPLAY + SPI) |
| `HAL_ENABLE_ST7567` | `hal_display.h` + `impl/shared/drivers/display/st7567_driver.h` | `impl/shared/drivers/display/hal_display.cpp` + `impl/shared/drivers/display/st7567_driver.cpp` | ST7567 raw monochrome facade/backend over HAL I2C or SPI/GPIO (propagates DISPLAY + I2C; SPI transport also needs SPI) |
| `HAL_ENABLE_SSD16XX` | `hal_display.h` + `impl/shared/drivers/display/ssd16xx_driver.h` | display facade + shared EPD transport + SSD16xx driver | SSD1608/SSD1673/SSD1675A/SSD1680/SSD1681 raw MONO10 EPD backend (propagates DISPLAY + SPI) |
| `HAL_ENABLE_UC81XX` | `hal_display.h` + `impl/shared/drivers/display/uc81xx_driver.h` | display facade + shared EPD transport + UC81xx driver | UC8175/UC8176/UC8151D/UC8179 raw MONO10 EPD backend (propagates DISPLAY + SPI) |
| `HAL_ENABLE_CRYPTO` | `hal_crypto.h` + `hal_sc_auth.h` | `hal_crypto.cpp` + `hal_sc_auth.cpp` | Base64, MD5, SHA-256, HMAC-SHA256, ChaCha20-Poly1305 |
| `HAL_ENABLE_CRC` | `hal_crc.h` | `hal_crc.cpp` | generic CRC-8/16/32 checksums for integrity (auto-enabled by ONEWIRE/DS18B20) |
| `HAL_ENABLE_CELLULAR_MODEM` | `hal_modem_at.h` | `hal_modem_at.cpp` | *(facade - needs a modem-family backend such as `HAL_ENABLE_A7670`)* |
| `HAL_ENABLE_A7670` | `hal_simcom_a76xx.h` | `hal_simcom_a76xx.cpp` | SimCom A76xx-family driver (propagates CELLULAR_MODEM + UART) |
| `HAL_ENABLE_CJSON` | `hal/impl/shared/frameworks/cjson/cJSON.h`, `hal/impl/shared/frameworks/cjson/cJSON_Utils.h` (`tools.h` from C++) | `hal/impl/shared/frameworks/cjson/cJSON.c`, `hal/impl/shared/frameworks/cjson/cJSON_Utils.c` | bundled cJSON |
| `HAL_ENABLE_PNG` | `hal/impl/shared/frameworks/lodepng/lodepng.h` (`tools.h` from C++) | `hal/impl/shared/frameworks/lodepng/lodepng.cpp` | bundled LodePNG memory-based PNG encoder/decoder |
| `HAL_ENABLE_PNG_AS_BASE64` | `utils/tools_api.h` helpers + `hal/impl/shared/frameworks/lodepng/lodepng.h` + `hal_crypto.h` | `utils/tools.cpp` + `hal/impl/shared/frameworks/lodepng/lodepng.cpp` + `hal_crypto.cpp` | Base64-encoded PNG decode helpers (propagates CRYPTO + PNG) |
| `HAL_ENABLE_JPEG` | `hal/impl/shared/frameworks/jpeg/JPEGDecoder.h`, `hal/impl/shared/frameworks/jpeg/picojpeg.h` (`tools.h` from C++) | `hal/impl/shared/frameworks/jpeg/JPEGDecoder.cpp`, `hal/impl/shared/frameworks/jpeg/picojpeg.c` | bundled baseline JPEG decoder |
| `HAL_ENABLE_JPEG_AS_BASE64` | `utils/tools_api.h` helpers + `hal/impl/shared/frameworks/jpeg/JPEGDecoder.h` + `hal_crypto.h` | `utils/tools.cpp` + `hal/impl/shared/frameworks/jpeg/JPEGDecoder.cpp` + `hal/impl/shared/frameworks/jpeg/picojpeg.c` + `hal_crypto.cpp` | Base64-encoded JPEG decode helpers (propagates CRYPTO + JPEG) |
| `HAL_ENABLE_UNITY` | utility headers/sources | `utils/unity.*` | bundled Unity framework |

### Opt-out flag

| Flag | Effect |
|---|---|
| `HAL_DISABLE_ASSERTS` | Compiles every `HAL_ASSERT()` to a no-op. Asserts are ON by default. Mirrors the standard `NDEBUG` convention. |

### Dependency propagation (hal\_config.h)

Enabling a leaf module automatically enables every module it requires:

```
HAL_ENABLE_KV          -> HAL_ENABLE_EEPROM
HAL_ENABLE_SDLOGGER    -> HAL_ENABLE_FAT + HAL_ENABLE_EEPROM + HAL_ENABLE_SPI
HAL_ENABLE_TIME        -> HAL_ENABLE_WIFI
HAL_ENABLE_MQTT        -> HAL_ENABLE_WIFI
HAL_ENABLE_UDP         -> HAL_ENABLE_WIFI
HAL_ENABLE_TCP         -> HAL_ENABLE_WIFI
HAL_ENABLE_HTTP_SERVER -> HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_HTTP_FILES  -> HAL_ENABLE_HTTP_SERVER -> HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_WEBSOCKET   -> HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_NET_CONSOLE -> HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_NET_COMMANDS -> HAL_ENABLE_HTTP_SERVER + HAL_ENABLE_WEBSOCKET +
                           HAL_ENABLE_CJSON + HAL_ENABLE_TCP + HAL_ENABLE_WIFI
HAL_ENABLE_BSD_SOCKETS -> HAL_ENABLE_UDP + HAL_ENABLE_TCP -> HAL_ENABLE_WIFI
HAL_ENABLE_OTA         -> HAL_ENABLE_WIFI
HAL_ENABLE_WIREGUARD   -> HAL_ENABLE_WIFI
HAL_ENABLE_EXTERNAL_ADC-> HAL_ENABLE_I2C
HAL_ENABLE_BH1750      -> HAL_ENABLE_I2C
HAL_ENABLE_ADP5360     -> HAL_ENABLE_I2C
HAL_ENABLE_MCP3221     -> HAL_ENABLE_I2C
HAL_ENABLE_TSC2007     -> HAL_ENABLE_I2C
HAL_ENABLE_STMPE610    -> HAL_ENABLE_I2C + HAL_ENABLE_SPI
HAL_ENABLE_PCF8563     -> HAL_ENABLE_RTC + HAL_ENABLE_I2C
HAL_ENABLE_DS3231      -> HAL_ENABLE_RTC + HAL_ENABLE_I2C
HAL_ENABLE_MCP9600     -> HAL_ENABLE_THERMOCOUPLE + HAL_ENABLE_I2C
HAL_ENABLE_MAX6675     -> HAL_ENABLE_THERMOCOUPLE
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
HAL_ENABLE_GPS         -> HAL_ENABLE_UART (only when UART and SWSERIAL are both absent)
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
HAL_ENABLE_PNG_AS_BASE64 -> HAL_ENABLE_CRYPTO + HAL_ENABLE_PNG
HAL_ENABLE_JPEG_AS_BASE64 -> HAL_ENABLE_CRYPTO + HAL_ENABLE_JPEG
```

Facade modules (`HAL_ENABLE_RTC`, `HAL_ENABLE_THERMOCOUPLE`,
`HAL_ENABLE_DISPLAY`, `HAL_ENABLE_TFT`, `HAL_ENABLE_CELLULAR_MODEM`)
emit a compile-time `#error` if enabled without any backend.

You only need to enable the **leaf** module you actually use; everything
upstream is pulled in for you.

### Passing flags - recommended: `hal_project_config.h`

Create `hal_project_config.h` in your sketch directory and enable the
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
#define HAL_ENABLE_PWM_FREQ
```

`hal_config.h` detects it via `__has_include("hal_project_config.h")`.

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

- RP2040: use arduino-pico's FreeRTOS mode. The HAL validates that
  `__FREERTOS` is present and emits a clear compile-time error if a normal
  non-FreeRTOS Arduino-pico build defines `HAL_ENABLE_FREERTOS`. For the
  static library, use `./scripts/build_rp2040_lib.sh --freertos`; for
  examples, build `examples/29_freertos_smoke`, whose dispatcher manifest sets
  the RP2040 FreeRTOS FQBN option. The
  `29_freertos_smoke` example verifies that `<FreeRTOS.h>` and `<task.h>` are
  available to application code and exercises portable `app_task0()` /
  `app_task1()` dispatch, native `xTaskCreate()` worker tasks, a
  mutex-protected shared table, `hal_mutex_*`, `hal_delay_ms()`, and
  `hal_idle()` from FreeRTOS task context. arduino-pico remains responsible for
  scheduler ownership; HAL represents the secondary path as `loop1()` only when
  `HAL_ENABLE_APP_TASK1` is defined.
- STM32G474: use the pinned `third_party/FreeRTOS-Kernel` dependency from
  `freertos_core_version.conf`, or pass
  `-DJH_FREERTOS_KERNEL_DIR=/path/to/FreeRTOS-Kernel`. STM32 CMake builds run
  `scripts/ensure_freertos_kernel.sh` before adding FreeRTOS source paths,
  compile the explicit Cortex-M4F kernel source list, include the target
  `FreeRTOSConfig.h`, use `heap_4.c`, and let the FreeRTOS port own
  SVC/PendSV/SysTick. In FreeRTOS mode, STM32 `hal_mutex_*` uses FreeRTOS
  mutexes, `hal_delay_ms()` uses `vTaskDelay()` from legal task context, and
  `hal_idle()` yields to the scheduler from legal task context. When
  `HAL_PROVIDE_APP_ENTRY` is also defined, HAL calls `app_start()`, creates an
  `app_task0()` FreeRTOS task, creates `app_task1()` only when
  `HAL_ENABLE_APP_TASK1` is defined, and then calls `vTaskStartScheduler()`.
- Host/mock: `HAL_ENABLE_FREERTOS` is not supported by the normal mock backend.
  CI uses the optional `JH_ENABLE_FREERTOS_POSIX_TESTS` host build to compile the
  FreeRTOS kernel GCC/Posix port, run a real scheduler as pthreads, and exercise
  the STM32G474 host-stub `HAL_ENABLE_FREERTOS` paths in `ctest`.

HAL-provided STM32 FreeRTOS entry task defaults:

| Macro | Default | Unit / meaning |
|---|---|---|
| `HAL_FREERTOS_TASK0_STACK` | `512` | FreeRTOS stack words for `app_task0()` |
| `HAL_FREERTOS_TASK1_STACK` | `512` | FreeRTOS stack words for `app_task1()` |
| `HAL_FREERTOS_TASK0_PRIORITY` | `tskIDLE_PRIORITY + 1` | FreeRTOS priority for `app_task0()` |
| `HAL_FREERTOS_TASK1_PRIORITY` | `tskIDLE_PRIORITY + 1` | FreeRTOS priority for `app_task1()` |

Platform stack-size overrides:

| Macro | Default | Unit / meaning |
|---|---|---|
| `HAL_STM32_MAIN_STACK_SIZE` | `0x800` | Bytes reserved as STM32 `_Min_Stack_Size` (linker reserve between heap and top-of-RAM stack) |
| `HAL_RP2040_STACK_SIZE` | `0x800` | Bytes mapped to `PICO_STACK_SIZE` (core0 stack reservation in Arduino-pico/Pico SDK linker flow) |
| `HAL_RP2040_CORE1_STACK_SIZE` | `HAL_RP2040_STACK_SIZE` / `0x800` | Bytes mapped to `PICO_CORE1_STACK_SIZE` (core1 stack reservation) |

Thread-safety note: RP2040 and STM32G474 FreeRTOS modes upgrade core
mutex/delay/idle primitives, while hard `hal_critical_section_*` remains a full
interrupt mask for timing-sensitive code. The implementation includes atomic
create-once fallbacks for singleton/per-bus mutexes and hardens the RP2040
I2C-slave callback path. Timer callback context, Arduino-origin wrapper
internals, and remaining per-module exceptions are tracked in the
[future work backlog](../future_ideas.md).

The supported VS Code project flow (`create-vscode-example.py` plus
`jh-vscode`) adds the project include path automatically through the shared
dispatcher. The raw `arduino-cli` commands below are for manual or legacy
Arduino builds. In that mode, Arduino CLI does not add the sketch directory to
the include path for library source files, so the build command must add it
explicitly:

```bash
arduino-cli compile \
  --fqbn rp2040:rp2040:rpipico \
  --build-property "compiler.cpp.extra_flags=-I '/path/to/sketch'" \
  --build-property "compiler.c.extra_flags=-I '/path/to/sketch'" \
  --build-path .build \
  --warnings all .
```

In hand-written VS Code tasks for legacy Arduino projects, use
`${workspaceFolder}` for the path. New generated projects should keep using the
`Project: Build` / `Project: Select board` tasks emitted by `jh-vscode`.

### Alternative: `-D` flags on the command line

```bash
arduino-cli compile \
  --fqbn rp2040:rp2040:rpipico \
  --build-property "build.extra_flags=\
-DHAL_ENABLE_WIFI \
-DHAL_ENABLE_EEPROM \
-DHAL_ENABLE_GPS \
-DHAL_ENABLE_THERMOCOUPLE \
-DHAL_ENABLE_UART \
-DHAL_ENABLE_SWSERIAL \
-DHAL_ENABLE_I2C \
-DHAL_ENABLE_BH1750 \
-DHAL_ENABLE_EXTERNAL_ADC \
-DHAL_ENABLE_DACLESS \
-DHAL_ENABLE_PWM_FREQ" \
  --build-path .build \
  --warnings all .
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

### Note about `library.properties:depends`

`library.properties` currently does **not** declare `depends=`.
Optional third-party integrations used by HAL modules are encapsulated under
`src/hal/impl/rp2040/frameworks/`; target-specific RP2040 helpers live under
`src/hal/impl/rp2040/drivers/rp2040/`. Portable device drivers live under
`src/hal/impl/shared/`, with hardware-oriented modules under
`shared/drivers/` and reusable engines/stacks under `shared/frameworks/`.

Actual compiled dependencies are controlled by the module set:

- enabled modules (`HAL_ENABLE_*`) pull in their third-party backends
- modules left disabled (the default) compile out both declarations and
  implementation details

\* `HAL_ENABLE_TIME` enables NTP/local-time APIs;
`hal_time_from_components(...)` remains available unconditionally as a pure
conversion helper with no network dependency.

---


---

*Next: [Multicore safety, drivers, migration guide](03_build_tests.md)*
