# Sprzętowy test Bluetooth Classic HID Host innej klasy

`tests/hardware/bluetooth_classic_hid_device` jest prywatnym, wyłącznie
testowym urządzeniem HID opartym na BTstack. Pico W ogłasza standardową mysz
Classic HID z deskryptorem Generic Desktop i wysyła naprzemienne raporty
względnego ruchu. Nie jest to publiczne API urządzenia HID. `device_tree.json`
opisuje sprzęt obu montaży: `picow` (Pico W, domyślny) i `pico2w` (Pico 2 W).
Żaden z nich nie wymaga zewnętrznych połączeń. Zbuduj fixture dla Pico W
i publiczny przykład `HID_HOST` dla hosta Pico 2 W:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_classic_hid_device --assembly picow
vscode/entry/jh-vscode build \
  --project examples/29_bluetooth_gamepad \
  --assembly pico2w --variant HID_HOST
```

Każdy obraz wgraj wyłącznie do przeznaczonej dla niego płytki. `INFO` na
fixture musi pokazać `controller=1`. Na hoście użyj `SCAN`, zatwierdź oczekujące
żądanie Just Works przez `AUTHORIZE`, a następnie użyj `INFO`. Akceptacja wymaga
`JHC85-HID-PASS`, `descriptor=1`, `input=1` i `saved=1`; fixture musi pokazać
`hid=1` oraz niezerowy licznik raportów. Żadna konsola nie wypisuje adresów
Bluetooth ani link keys.

Obie strony przechowują link keys wyłącznie w RAM, więc ten test nie
sprawdza parowania zachowanego po restarcie.
