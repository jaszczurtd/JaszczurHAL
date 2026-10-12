# Test zapisu danych w pamięci urządzenia na RP

`tests/hardware/rp_storage` sprawdza natywne EEPROM i LittleFS na fizycznym
sprzęcie RP2040/RP2350. Zapisuje trwale licznik uruchomień w EEPROM, formatuje
i ponownie montuje partycję LittleFS, wywołuje reset przez watchdog, a następnie
potwierdza trwałość EEPROM i możliwość zamontowania LittleFS bez ponownego
formatowania.

Zbuduj i wgraj zgodnie ze zwykłym procesem dla targetów natywnych:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_storage \
  --assembly pico
vscode/entry/jh-vscode upload \
  --project tests/hardware/rp_storage \
  --assembly pico \
  --port /dev/serial/by-id/<device>
```

Uruchom weryfikator:

```sh
python3 tests/hardware/rp_storage/verify_storage.py \
  --port /dev/serial/by-id/<device>
```

Plik `device_tree.json` opisuje sprzęt każdego montażu: `pico` dla Pico oraz
`pico2-arm` i `pico2-riscv` dla Pico 2. Test potrzebuje tylko samej płytki.
Dla Pico 2 użyj `--assembly pico2-arm` albo `--assembly pico2-riscv`.
