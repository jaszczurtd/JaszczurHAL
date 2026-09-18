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
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_usb_multicore \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
python3 tests/hardware/rp_usb_multicore/verify_usb_multicore.py \
  --port /dev/serial/by-id/<device> \
  --target rp2040 --board pico --runtime baremetal
```

Dla Pico 2 wybierz `rp2350-arm` lub `rp2350-riscv`, użyj płytki kompilacji
`pico2` i przekaż `--board pico2` do weryfikatora. Dodaj `--variant freertos`
do komend kompilacji i wgrywania oraz użyj `--runtime freertos` dla przebiegu
FreeRTOS SMP.

Domyślne `--records 4096` weryfikatora musi zgadzać się z
`JH_USB_MULTICORE_RECORDS` w kompilacji firmware.
