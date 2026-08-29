# Deskryptory boardów

Ten katalog jest źródłem prawdy dla definicji targetów i fizycznych boardów
JaszczurHAL. `scripts/generate_board_config.py` sprawdza deskryptory, emituje
konfigurację CMake oraz C/C++ czasu builda poniżej `.build`, a także utrzymuje
śledzony rejestr i source fallback używany bez profilu wygenerowanego podczas
builda.

- `capabilities.json` przypisuje stabilne publiczne bity capabilities.
- `targets/` opisuje targety builda MCU/ISA.
- `profiles/` opisuje fizyczne boardy.
- `board.schema.json` pomaga wyłącznie edytorowi. Za walidację odpowiada generator.

Aplikacje wybierają stabilne identyfikatory `target` i `board`. Nie powinny
analizować tych plików w runtime.

Po zmianie targetów, profili, capabilities lub ról urządzeń odśwież śledzone
artefakty:

```bash
python3 scripts/sync_generated.py --write
```

CI sprawdza wszystkie śledzone, generowane artefakty poleceniem
`python3 scripts/sync_generated.py --check`. Nie edytuj ręcznie
`src/hal/generated/jh_board_registry.h` ani
`src/hal/generated/jh_board_fallback_config.h`.
