# JaszczurHAL documentation

*Also available in [Polish](table_of_contents.pl.md).*

## Project documentation

- [Project overview](../README.md)
- [JaszczurHAL capabilities](en/features.md)
- [JaszczurHAL - API Reference](en/JaszczurHAL_API.md)
- [Target and board profiles](en/boards_profiles_howto.md)
- [Working with firmware projects](en/FwProjectWorkflow.md)
- [Building JaszczurHAL](en/lib_compilation.md)
- [Setting up a native Windows environment](en/windows_setup.md)
- [Updating firmware over OTA](en/OTAWorkflow.md)
- [Dependency and toolchain security](en/security_supply_chain.md)
- [Module flags quick reference](HAL_FLAGS.txt)

## API chapters

- [00 - Repository setup, build, and validation scripts](api/en/00_scripts.md)
- [01 - Operation results and error handling (`hal_status_t`)](api/en/01_status_api.md)
- [02 - Module flags and configuration](api/en/02_module_flags.md)
- [03 - Building, automated tests, and hardware validation](api/en/03_build_tests.md)
- [04 - Concurrency, drivers, and logging](api/en/04_multicore_drivers_migration.md)
- [05 - GPIO, ADC and PWM](api/en/05_gpio_adc_pwm.md)
- [06 - Timers, system, bits, math](api/en/06_timers_system.md)
- [07 - Cryptography - `hal_crypto`](api/en/07_crypto.md)
- [08 - Sync, USB, serial output, framing and auth](api/en/08_sync_serial.md)
- [09 - Communication buses](api/en/09_buses.md)
- [10 - CAN and displays](api/en/10_can_display.md)
- [11 - Sensors](api/en/11_sensors.md)
- [12 - Cellular modem](api/en/12_modem.md)
- [13 - LEDs, controls, I/O devices, and RFID/NFC readers](api/en/13_output_devices.md)
- [14 - Storage](api/en/14_storage.md)
- [15 - Network connectivity](api/en/15_connectivity.md)
- [16 - Timers, controllers, and utility functions](api/en/16_utilities.md)
- [17 - JSON - parsing, creation, and modification](api/en/17_cJSON.md)
- [18 - PNG - image encoding and decoding](api/en/18_LodePNG.md)
- [19 - JPEG - image decoding](api/en/19_JPEG.md)
- [20 - Bluetooth - BLE, audio, and HID devices](api/en/20_bluetooth.md)
- [21 - LoRa - radio configuration and packet transfers](api/en/21_lora.md)
- [22 - LoRa - messages, acknowledgements, and retries](api/en/22_lora_link.md)
- [23 - Transport-independent application commands](api/en/23_commands.md)
- [24 - Hardware period capture](api/en/24_pulse_capture.md)
- [25 - Hardware-paced ADC scan](api/en/25_adc_scan.md)
