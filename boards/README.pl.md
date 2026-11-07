# Deskryptory płytek

Ten katalog jest miarodajnym źródłem definicji targetów i fizycznych płytek
JaszczurHAL. Deskryptory sprawdza `jh board validate`
([README jh](../tools/jh/README.pl.md#rejestr-płytek)).
`jh board config` zapisuje ze sprawdzonego rejestru konfigurację CMake i C/C++
jednej kompilacji w katalogu `.build`, a `jh generate boards` utrzymuje
przechowywany w repozytorium rejestr oraz zastępczą konfigurację źródłową,
używaną wtedy, gdy profil nie został wygenerowany podczas buildu.

- `capabilities.json` przypisuje stabilne publiczne bity reprezentujące możliwości.
- `targets/` opisuje targety buildu dla poszczególnych MCU i ISA.
- `profiles/` opisuje fizyczne płytki.
- `board.schema.json` pomaga wyłącznie edytorowi. Za walidację odpowiada
  `jh`.

Aplikacje wybierają stabilne identyfikatory `target` i `board`. Nie mogą
analizować tych plików w czasie działania.

Po zmianie targetów, profili, możliwości lub ról urządzeń odśwież artefakty
przechowywane w repozytorium:

```bash
python3 scripts/sync_generated.py --write
```

CI sprawdza wszystkie wygenerowane artefakty przechowywane w repozytorium
poleceniem `python3 scripts/sync_generated.py --check`. Nie edytuj ręcznie
`src/hal/generated/jh_board_registry.h` ani
`src/hal/generated/jh_board_fallback_config.h`.
