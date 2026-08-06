# 01 - Core runtime

This portable example combines the small runtime demonstrations that used to
live in five separate firmware projects.

| Previous example | Coverage in this project |
|---|---|
| `01_blink` | The board LED toggles from a soft-timer callback. |
| `02_debug_helper` | Startup reports the selected target, backend, MCU, CPU, RTOS, clocks, RAM, and flash. |
| `03_soft_timer_table` | One table drives the LED and periodic PID updates. |
| `17_pid_controller` | A simulated process is driven toward its setpoint and reports stability and oscillation. |
| `19_timer_ext` | A repeating timer handle counts independent 250 ms ticks and reports its state and remaining time. |

The application uses only core JaszczurHAL facilities, so it needs no
`HAL_ENABLE_*` flags or external hardware beyond the board LED and debug
console.
