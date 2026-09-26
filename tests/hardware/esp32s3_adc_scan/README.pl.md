# Test skanu ADC na ESP32-S3

`tests/hardware/esp32s3_adc_scan` sprawdza ciągły skan ADC na Waveshare
ESP32-S3-Zero. Skan pracuje na GPIO3 i GPIO4 z konwersją co 12 µs (400 ramek,
9,6 ms na blok). Sonda sprawdza, że bloki przychodzą po kolei z rosnącym czasem
zakończenia, że najnowsza próbka podąża za GPIO3 między wewnętrznym pull-down i
pull-up zarówno przez `hal_adc_scan_latest()`, jak i przez `hal_adc_read()` bez
pobierania bloków w międzyczasie, że pin spoza skanu czyta 0, gdy skan jest
właścicielem ADC1, że cztery zapisy sektora flash (ostatni sektor partycji NVS,
tutaj nieużywanej) kończą się powodzeniem w czasie skanu (utracone bloki
liczone są z sekwencji), oraz że stop oddaje przetwornik
odczytom jednorazowym, a drugi start działa. Dioda WS2812 na GPIO21 świeci
niebiesko w czasie sprawdzeń, zielono przy PASS i czerwono przy FAIL. Nic nie
trzeba podłączać; oceniana jest geometria pierścienia.

Zbuduj, zmaterializuj artefakty i wgraj przez natywny port USB Serial/JTAG, jak
dla sondy fazy 2:

```bash
python3 scripts/build_esp_idf.py build \
  --project tests/hardware/esp32s3_adc_scan \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --name jh_esp32_adc_scan_hardware \
  --clean

python3 scripts/build_esp_idf.py artifacts \
  --project tests/hardware/esp32s3_adc_scan \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --name jh_esp32_adc_scan_hardware

python3 scripts/build_esp_idf.py flash \
  --project tests/hardware/esp32s3_adc_scan \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --name jh_esp32_adc_scan_hardware \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00

python3 tests/hardware/esp32s3_adc_scan/verify_esp32s3_adc_scan.py \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00
```

Weryfikator przyjmuje tylko raport `status=PASS` ze wszystkimi flagami
sprawdzeń, okresem ramki 24000 ns, co najmniej 20 blokami w oknie 300 ms,
pull-down poniżej 600 i pull-up powyżej 3400 na obu ścieżkach odczytu, pinem
spoza skanu równym 0 i czterema zapisami flash.
