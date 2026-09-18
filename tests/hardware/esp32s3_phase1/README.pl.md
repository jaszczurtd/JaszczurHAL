# Sprzętowy test ESP32-S3 - faza 1

`tests/hardware/esp32s3_phase1` sprawdza cały podstawowy proces wyboru układu
docelowego i płytki, kompilowania, wgrywania oraz monitorowania Waveshare
ESP32-S3-Zero SKU 25081. Celowo nie obejmuje API HAL dla GPIO, portu
szeregowego, magistral, sieci ani pamięci masowej; przewidziano je w późniejszych
fazach.

Firmware podaje dokładną, wygenerowaną tożsamość układu docelowego i płytki, a
następnie sprawdza wykryty model chipu, liczbę rdzeni, fizyczny rozmiar
flash, inicjalizację PSRAM oraz fizyczny rozmiar PSRAM z danymi zapisanymi w
rejestrze płytek. Skrypt `verify_phase1.py` wyznacza oczekiwane wartości na
podstawie tych samych deskryptorów układu i płytki, po czym czeka na cykliczny raport na natywnym
porcie USB Serial/JTAG.

Użyj stabilnej ścieżki `/dev/serial/by-id/`, gdy jest dostępna. Skompiluj i
zwaliduj artefakty za pomocą produkcyjnego skryptu ESP-IDF:

```bash
python3 scripts/build_esp_idf.py build \
  --project tests/hardware/esp32s3_phase1 \
  --target esp32s3 --board waveshare-esp32-s3-zero \
  --output .build/hardware/esp32s3_phase1 --clean
python3 scripts/build_esp_idf.py artifacts \
  --project tests/hardware/esp32s3_phase1 \
  --target esp32s3 --board waveshare-esp32-s3-zero \
  --output .build/hardware/esp32s3_phase1
```

Ten sam projekt jest zarejestrowany jako projekt ESP-IDF `jh-vscode`. Ustaw `PORT`
na stabilny alias podłączonej płytki, następnie skompiluj projekt, odśwież
IntelliSense, wgraj firmware i uruchom monitorowanie przez publiczny proces:

```bash
PORT="/dev/serial/by-id/<Espressif-USB-Serial-JTAG-device>"
vscode/entry/jh-vscode config-dump \
  --project tests/hardware/esp32s3_phase1
vscode/entry/jh-vscode build \
  --project tests/hardware/esp32s3_phase1
vscode/entry/jh-vscode refresh-intellisense \
  --project tests/hardware/esp32s3_phase1
vscode/entry/jh-vscode upload \
  --project tests/hardware/esp32s3_phase1 --port "$PORT"
vscode/entry/jh-vscode monitor \
  --project tests/hardware/esp32s3_phase1 --port "$PORT" \
  --lock-policy replace-own
```

Wybrane urządzenie musi mieć VID/PID USB Serial/JTAG zgodne z profilem płytki:
`303a:1001`. Aby przetestować przekazanie wgrywania, pozostaw monitor
uruchomiony i wywołaj tę samą komendę `upload` z drugiego terminala. Wgranie
musi zatrzymać monitor wyłącznie tego projektu, wgrać wszystkie trzy obrazy z
manifestu, zresetować płytkę i umożliwić monitorowi ponowne połączenie.

Zatrzymaj monitor przed uruchomieniem samodzielnego weryfikatora, ponieważ
obie komendy wymagają wyłącznego dostępu do portu szeregowego:

```bash
python3 tests/hardware/esp32s3_phase1/verify_phase1.py \
  --port "$PORT"
```

Udany przebieg wypisuje jeden obiekt JSON z `"phase": "task0"`, sekwencją co
najmniej jeden oraz `"status": "PASS"`.
