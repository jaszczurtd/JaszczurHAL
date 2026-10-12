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
  --assembly pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_freertos_smp \
  --assembly pico \
  --port /dev/serial/by-id/<device>
```

Uruchom weryfikator hosta:

```sh
python3 tests/hardware/rp_freertos_smp/verify_freertos_smp.py \
  --port /dev/serial/by-id/<device>
```

Plik `device_tree.json` opisuje sprzęt każdego montażu: `pico` dla Pico oraz
`pico2-arm` i `pico2-riscv` dla Pico 2. Test potrzebuje tylko samej płytki.
Dla Pico 2 użyj `--assembly pico2-arm` albo `--assembly pico2-riscv`. Gdy
urządzenie nie ma jeszcze działającego firmware CDC, użyj `upload-uf2`, gdy
jest ono w BOOTSEL.
