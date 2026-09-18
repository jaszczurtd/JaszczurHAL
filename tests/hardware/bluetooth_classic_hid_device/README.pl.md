# Sprzętowy test Bluetooth Classic HID Host innej klasy

`tests/hardware/bluetooth_classic_hid_device` jest prywatnym, wyłącznie
testowym urządzeniem HID opartym na BTstack. Pico W ogłasza standardową mysz
Classic HID z deskryptorem Generic Desktop i wysyła naprzemienne raporty
względnego ruchu. Nie jest to publiczne API urządzenia HID. Zbuduj fixture
RP2040 i publiczny przykład `hid-host` dla hosta RP2350 ARM:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_classic_hid_device \
  --target rp2040 --board picow
vscode/entry/jh-vscode build \
  --project examples/29_bluetooth_gamepad \
  --target rp2350-arm --board pico2w --variant hid-host
```

Każdy obraz wgraj wyłącznie do przeznaczonej dla niego płytki. `INFO` na
fixture musi pokazać `controller=1`. Na hoście użyj `SCAN`, zatwierdź oczekujące
żądanie Just Works przez `AUTHORIZE`, a następnie użyj `INFO`. Akceptacja wymaga
`JHC85-HID-PASS`, `descriptor=1`, `input=1` i `saved=1`; fixture musi pokazać
`hid=1` oraz niezerowy licznik raportów. Żadna konsola nie wypisuje adresów
Bluetooth ani link keys.

Obie strony przechowują link keys wyłącznie w RAM, więc ten test nie
sprawdza parowania zachowanego po restarcie.
