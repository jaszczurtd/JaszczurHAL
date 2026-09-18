# Sprzętowy test USB CDC na RP

`tests/hardware/rp_usb_cdc_echo` sprawdza natywną obsługę TinyUSB przez HAL na RP
na fizycznym Pico lub Pico 2, w tym targety RP2350 ARM i RISC-V. Firmware
odbija dowolne bajty CDC i przełącza diodę LED płytki po każdym w pełni
odbitym bloku odbioru USB.

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

Zweryfikuj integralność danych, opóźnione odczyty hosta, przepustowość oraz
zamknięcie/ponowne otwarcie:

```sh
python3 -m pip install pyserial
python3 tests/hardware/rp_usb_cdc_echo/verify_cdc_echo.py \
  --port /dev/serial/by-id/<device>
```

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
