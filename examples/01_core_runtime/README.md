<a id="01---core-runtime"></a>

# 01 - Core system functions

This example blinks the board LED, reports system diagnostics, uses software
timers, and runs a PID controller against a simulated process. It needs only
the board LED and a debug console, with no additional hardware.


The `capture` variant measures a periodic signal on GPIO0 (PA0 on STM32)
using hardware timestamps. It enables `HAL_ENABLE_PULSE_CAPTURE` and needs an
external digital source with a common ground. Drain service runs every 1 ms.
