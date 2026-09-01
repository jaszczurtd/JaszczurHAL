# 01 - Podstawowe usługi runtime'u

Ten przenośny przykład łączy niewielkie demonstracje podstawowych usług
systemowych, które wcześniej znajdowały się w pięciu osobnych projektach
firmware.

| Poprzedni przykład | Zakres w tym projekcie |
|---|---|
| `01_blink` | Dioda płytki jest przełączana przez funkcję zwrotną timera programowego. |
| `02_debug_helper` | Raport startowy podaje wybrany target, implementację, MCU, CPU, RTOS, zegary, RAM i pamięć flash. |
| `03_soft_timer_table` | Jedna tabela steruje LED-em i okresowymi aktualizacjami PID. |
| `17_pid_controller` | Regulator doprowadza symulowany proces do wartości zadanej i sygnalizuje stabilność oraz oscylacje. |
| `19_timer_ext` | Powtarzalny uchwyt timera zlicza niezależne okresy 250 ms oraz podaje stan i pozostały czas. |

Aplikacja używa wyłącznie podstawowych funkcji JaszczurHAL, dlatego nie wymaga
flag `HAL_ENABLE_*` ani sprzętu poza diodą płytki i konsolą diagnostyczną.
