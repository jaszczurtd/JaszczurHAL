# 01 - Core runtime

Ten przenośny przykład łączy małe demonstracje runtime, które wcześniej
znajdowały się w pięciu osobnych projektach firmware.

| Poprzedni przykład | Zakres w tym projekcie |
|---|---|
| `01_blink` | LED boardu jest przełączany przez callback soft timera. |
| `02_debug_helper` | Raport startowy podaje wybrany target, backend, MCU, CPU, RTOS, zegary, RAM i flash. |
| `03_soft_timer_table` | Jedna tabela steruje LED-em i okresowymi aktualizacjami PID. |
| `17_pid_controller` | Symulowany proces jest doprowadzany do wartości zadanej oraz raportuje stabilność i oscylacje. |
| `19_timer_ext` | Powtarzalny handle timera zlicza niezależne takty 250 ms oraz raportuje stan i pozostały czas. |

Aplikacja używa wyłącznie podstawowych funkcji JaszczurHAL, dlatego nie wymaga
flag `HAL_ENABLE_*` ani sprzętu poza LED-em boardu i konsolą debug.
