# Sprzętowy test USB CDC na RP

`tests/hardware/rp_usb_cdc_echo` sprawdza natywną obsługę TinyUSB przez HAL na RP
na fizycznym Pico lub Pico 2, w tym targety RP2350 ARM i RISC-V. Firmware
odbija dowolne bajty CDC i przełącza diodę LED płytki po każdym w pełni
odbitym bloku odbioru USB. Dodatkowo uruchamia sprzętowy watchdog 4 s karmiony
z pętli aplikacji, więc każda ścieżka transportu blokująca pętlę kończy się
widocznym resetem.

Skompiluj i wykonaj pierwsze wgranie BOOTSEL:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_usb_cdc_echo \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload-uf2 \
  --project tests/hardware/rp_usb_cdc_echo \
  --target rp2040 --board pico
```

Dla Pico W i Pico 2 W użyj odpowiednio `--board picow` i `--board pico2w`.
Gdy inna płytka jest już w trybie BOOTSEL, niezależna od targetu akcja
`upload` najpierw zapamiętuje listę istniejących dysków, następnie wyzwala
reset przez 1200 bps i zapisuje plik wyłącznie na nowo wykrytym dysku.

Zweryfikuj integralność danych, opóźnione odczyty hosta, przepustowość,
zamknięcie/ponowne otwarcie oraz okno uptime z zawieszonym DTR:

```sh
python3 -m pip install pyserial
python3 tests/hardware/rp_usb_cdc_echo/verify_cdc_echo.py \
  --port /dev/serial/by-id/<device>
```

Końcowa faza `dtr_stuck_uptime` odtwarza linuksowy terminal, który czyści
`HUPCL` i zamyka port: DTR zostaje w górze, a nikt nie odbiera danych. Skrypt
najpierw przełącza firmware w tryb chatter (`JH:DTRSTUCK\n` na wejściu CDC),
w którym co 20 ms wychodzi linia debug
`JHDTR uptime_ms=... wdt_reboot=... seq=...`, a LED przełącza się co 25
linii. Potem zamyka port z wyczyszczonym `HUPCL`, czeka 65 s
(`--dtr-stuck-seconds`, `0` pomija fazę), otwiera port ponownie i wymaga, by
najnowszy raportowany uptime obejmował całe okno przy `wdt_reboot=0` - dowód,
że zablokowane zapisy debug nie mogą zagłodzić watchdoga. `JH:ECHO\n`
przełącza firmware z powrotem w tryb echo, a końcowa wymiana echo potwierdza,
że transport wrócił do normalnej pracy.

Po pierwszym wgraniu, neutralna względem targetu akcja `upload` musi wejść
do BOOTSEL przez dotknięcie DTR 1200 bps i wrócić z tą samą tożsamością CDC:

```sh
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_usb_cdc_echo \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
```

Użyj jawnego, stabilnego portu by-id, gdy podłączonych jest wiele
kompatybilnych płytek. Skrypt celowo nie wybiera samodzielnie między dwoma
zweryfikowanymi portami.

## Wstrzymanie i wznowienie podczas pracy na Linuksie

Zamknij każdy proces trzymający port CDC. Ustaw `USB_DEVICE_SYSFS` na węzeł
urządzenia USB, a nie węzeł jego interfejsu (na przykład
`/sys/bus/usb/devices/3-4.1.4`):

```sh
printf '0\n' |
  sudo tee "$USB_DEVICE_SYSFS/power/autosuspend_delay_ms" >/dev/null
printf 'auto\n' |
  sudo tee "$USB_DEVICE_SYSFS/power/control" >/dev/null
sleep 3
cat "$USB_DEVICE_SYSFS/power/runtime_status"

printf 'on\n' |
  sudo tee "$USB_DEVICE_SYSFS/power/control" >/dev/null
sleep 1
cat "$USB_DEVICE_SYSFS/power/runtime_status"
```

Oczekiwane stany to `suspended`, a następnie `active`. Uruchom ponownie
`verify_cdc_echo.py` po wznowieniu, a następnie przywróć oryginalne wartości
`autosuspend_delay_ms` i `control`.
