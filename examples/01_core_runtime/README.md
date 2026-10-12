<a id="01---core-runtime"></a>

# 01 - Core system functions

This example blinks the board LED, reports system diagnostics, uses software
timers, and runs a PID controller against a simulated process. It needs only
the board LED and a debug console, with no additional hardware.


The `CAPTURE` variant measures a periodic signal using hardware timestamps.
It enables `HAL_ENABLE_PULSE_CAPTURE` and needs an external digital source
with a common ground. Drain service runs every 1 ms. `device_tree.json`
describes the wiring of every assembly, and the variant takes its input pin
from the generated header. The table below repeats that wiring.

| Signal | Pico / Pico 2 | NUCLEO-G474RE | ESP32-S3-Zero |
|---|---|---|---|
| Capture input, internal pull-up | GPIO0 | PA0, CN7 pin 28 / A0 | GPIO0 |

On the ESP32-S3-Zero GPIO0 is also the BOOT button, so the source must not
hold it low during reset.
