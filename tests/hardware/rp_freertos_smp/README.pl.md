# Sprzętowy test FreeRTOS SMP na RP

`tests/hardware/rp_freertos_smp` sprawdza natywny kernel FreeRTOS w wersji
wskazanej przez repozytorium na fizycznym Pico lub Pico 2. Obejmuje start
schedulera, przypisanie zadań
aplikacji na obu rdzeniach, działanie muteksu HAL między rdzeniami,
raportowanie sterty FreeRTOS oraz natywny ruch USB CDC z opóźnionymi
odczytami hosta.

Zbuduj i wgraj przez standardowy proces VS Code dla targetów natywnych:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_freertos_smp \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_freertos_smp \
  --target rp2040 --board pico \
  --port /dev/serial/by-id/<device>
```

Uruchom weryfikator hosta:

```sh
python3 tests/hardware/rp_freertos_smp/verify_freertos_smp.py \
  --port /dev/serial/by-id/<device>
```

Użyj `rp2350-arm` lub `rp2350-riscv` z płytką `pico2` dla Pico 2. Gdy
urządzenie nie ma jeszcze działającego firmware CDC, użyj `upload-uf2`, gdy
jest ono w BOOTSEL.
