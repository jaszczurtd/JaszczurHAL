# Test współbieżnej obsługi USB na RP

`tests/hardware/rp_usb_multicore` uruchamia jednego producenta CDC na każdym
rdzeniu RP. Obaj producenci zapisują 4096 niezależnie numerowanych rekordów z
sumą kontrolną przez `hal_usb`, podczas gdy host weryfikuje granice rekordów,
integralność, kompletność, przypisanie producenta do rdzenia i końcowy status
HAL. Uszkodzona linia wskazuje przeplatanie bajtów między równoległymi
zapisami.

Skompiluj i wgraj wariant bare-metal RP2040:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_usb_multicore \
  --assembly pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_usb_multicore \
  --assembly pico \
  --port /dev/serial/by-id/<device>
python3 tests/hardware/rp_usb_multicore/verify_usb_multicore.py \
  --port /dev/serial/by-id/<device> \
  --target rp2040 --board pico --runtime baremetal
```

Plik `device_tree.json` opisuje sprzęt każdego montażu: `pico` dla Pico oraz
`pico2-arm` i `pico2-riscv` dla Pico 2. Test potrzebuje tylko samej płytki.
Dla Pico 2 zbuduj firmware z `--assembly pico2-arm` albo
`--assembly pico2-riscv`, a weryfikatorowi przekaż `--target rp2350-arm` albo
`--target rp2350-riscv` razem z `--board pico2`. Dodaj `--variant FREERTOS`
do komend kompilacji i wgrywania oraz użyj `--runtime freertos` dla przebiegu
FreeRTOS SMP.

Domyślne `--records 4096` weryfikatora musi zgadzać się z
`JH_USB_MULTICORE_RECORDS` w kompilacji firmware.
