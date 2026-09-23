# Sprzętowy test ESP32-S3 - faza 2

`tests/hardware/esp32s3_phase2` sprawdza HAL peryferiów fazy 2 w profilu
`waveshare-esp32-s3-zero`. Wymaga jedynie natywnego kabla USB płytki; żaden
zewnętrzny czujnik, zworka ani urządzenie SPI/I2C nie jest wymagane.

Firmware sprawdza:

- czas systemowy, architekturę, UID, stertę, temperaturę układu, watchdog,
  zapis i odtworzenie informacji o błędzie zachowanej po restarcie oraz
  działanie włączonej ochrony stosu FreeRTOS;
- muteksy FreeRTOS, sekcje krytyczne oraz przypisanie `app_task0` i `app_task1`
  do rdzeni 0 i 1;
- wejście GPIO z podciągnięciem (pull-up), wyjście/odczyt zwrotny oraz
  przekonfigurowane przerwanie GPIO przypisane do tego samego rdzenia;
- przerwania GPIO z kontekstem: jeden handler obsługujący dwa piny trafia we
  własny kontekst i raportuje własny pin, callback bez kontekstu podmienia go na
  jednym pinie i z powrotem, a odłączenie wycisza oba;
- odczyty 12-bitowego ADC rozstawione przez wewnętrzne podciągnięcie/
  podwieszenie (pull-down/pull-up) GPIO;
- sprzętowy UART1 TX/RX przez jeden pin pętli zwrotnej macierzy GPIO;
- czyszczenie magistrali mastera I2C, inicjalizację oraz kompletne
  skanowanie adresów (zero wykrytych urządzeń jest prawidłowe dla
  nieokablowanej płytki);
- transakcje mastera SPI2, blokujące DMA oraz synchroniczną ścieżkę zapasową
  dla asynchronicznego DMA, bez zakładania odebranych danych od nieobecnego
  slave'a;
- kontrolowane wstrzymywanie i wznawianie GPTimer z dedykowanej puli,
  powtarzane callbacki ISR oraz poprawne zwalnianie zasobów;
- dwukierunkowy ruch debugowania przez natywny VFS USB Serial/JTAG konsoli
  startowej.

Skompiluj projekt i wygeneruj przenośny manifest artefaktów:

```bash
python3 scripts/build_esp_idf.py build \
  --project tests/hardware/esp32s3_phase2 \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --name jh_esp32_phase2_hardware \
  --clean

python3 scripts/build_esp_idf.py artifacts \
  --project tests/hardware/esp32s3_phase2 \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --name jh_esp32_phase2_hardware
```

Użyj stabilnego aliasu `/dev/serial/by-id/...` płytki na Linuksie (lub jej
portu COM na Windows) zarówno dla flashowania, jak i weryfikacji:

```bash
python3 scripts/build_esp_idf.py flash \
  --project tests/hardware/esp32s3_phase2 \
  --target esp32s3 \
  --board waveshare-esp32-s3-zero \
  --name jh_esp32_phase2_hardware \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00

python3 tests/hardware/esp32s3_phase2/verify_phase2.py \
  --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_SERIAL-if00
```

Weryfikator wysyła `PING` i akceptuje wyłącznie kompletny raport
`status=PASS`. Brakujący callback, zły rdzeń, zablokowana ścieżka RX,
wynik ADC poza zakresem lub błąd peryferium nie mogą więc zostać omyłkowo
zgłoszone jako pomyślny wynik podstawowego testu.

Firmware testowy celowo nie wywołuje `hal_enter_bootloader()`: udane przejście
do trybu pobierania resetuje MCU i wymaga osobnego testu ponownego połączenia
oraz odzyskiwania. Projekt `tests/fixtures/esp32s3_phase3` sprawdza obecność symbolu podczas
kompilacji i linkowania, a sam reset wymaga osobnego testu sprzętowego.

## Pokrycie sprzętowe

Fixture przechodzi na Waveshare ESP32-S3-Zero, razem z dedykowaną pulą timerów,
sprawdzeniami ochrony stosu i przerwaniami GPIO z kontekstem.

Poza tym fixture'em testów sprzętowych wciąż wymagają: I2C w roli układu
podrzędnego, PWM i PWM_FREQ, RMT/RGB, PCNT, wejście w tryb pobierania,
celowe wymuszanie błędów stosu i innych usterek oraz odtworzenie informacji
o błędzie zachowanej po restarcie.
