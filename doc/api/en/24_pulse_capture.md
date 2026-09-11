# Hardware period capture

Enable `HAL_ENABLE_PULSE_CAPTURE` and include `<hal/analog/hal_pulse_capture.h>`
to measure a periodic digital input. `hal_pulse_capture_read()` returns the
elapsed hardware ticks for 32 complete periods. The first capture establishes
the starting point; it is not counted as a period. Input phase and CPU service
latency do not enter the frequency calculation.

```c
const hal_pulse_capture_config_t config = {0, true, 10000};
hal_status_t status = hal_pulse_capture_init(&config);
/* In the application loop, drain until HAL_EAGAIN. */
hal_pulse_capture_sample_t sample;
while (status == HAL_OK && hal_pulse_capture_read(&sample) == HAL_OK) {
    uint32_t hz = (uint32_t)(((uint64_t)sample.periods * sample.clock_hz +
                              sample.ticks / 2) / sample.ticks);
    /* Use hz and sample.measured_us; handle read errors in real applications. */
    (void)hz;
}
```

The `capture` variant of [01_core_runtime](../../../examples/01_core_runtime/)
shows status handling and periodic service. It uses GPIO0 (PA0 on STM32).

## Lifetime and validity

There is one active input. The caller exclusively owns its pin. Init/deinit
must run on the same owning core without concurrent API calls; runtime reads
are serialized with a mutex and are not allowed in an ISR. Repeated init
returns `HAL_EBUSY`; deinit is idempotent, including after failed init. Retry
deinit if releasing resources fails. Configuration is copied.

Drain the stream at least every 5 ms. Real backends reject a service gap of
10 ms with `HAL_EOVERFLOW`, avoiding ambiguous timer or buffer wrap. Input
frequency must not exceed 100 kHz, and both levels must last at least 1 us.
Choose `timeout_us` (1000..100000 us) longer than a 32-period interval plus the
service delay. `clock_hz` is the actual timebase; its oscillator tolerance
still limits absolute accuracy.

`measured_us` is a conservative completion time in the `hal_micros()` domain,
not the time the application reads the sample. Use unsigned subtraction for
age across wrap. `sequence` starts at 1; deinit/init starts a new sequence.
`HAL_EAGAIN` means no complete interval, `HAL_ETIMEOUT` means stale data or a
signal gap. Timeout discards the incomplete interval. `HAL_EOVERFLOW` and
hardware faults remain latched until deinit/init. Errors leave the output
unchanged. An application must invalidate its own accumulated window after
any capture error.

## Backends

| Platform | Measurement and resources | Limits |
| --- | --- | --- |
| RP2040 / RP2350 ARM / RP2350 RISC-V | One dynamically claimed PIO SM and seven instructions; two DMA channels; 1024-word ring. PIO timestamps every edge; the CPU reads every 32nd timestamp. | GPIO0..29; timebase system clock / 8, at least 2 MHz. Keep the system clock fixed while active. RX FIFO loss and DMA/buffer errors invalidate capture. |
| STM32G474 | TIM5_CH1 on PA0/AF2, every eighth selected edge, DMA2 channel 8 / DMAMUX channel 15, 256-word ring. Four intervals form a sample. | Reserves the entire TIM5 and DMA channel. Busy timer/DMA or an alternate-function pin returns `HAL_EBUSY`. Capture overrun and DMA transfer errors are reported. |
| ESP32-S3 | MCPWM timer and two capture channels: input prescaler 32 and a software-only reference channel for sample age. A short ISR puts hardware timestamps in a 128-word RAM ring. | Requires `CONFIG_MCPWM_ISR_CACHE_SAFE=y`; the HAL ESP-IDF build enables it. ISR and queue reside in internal RAM. |
| Mock | Injected 16 MHz timestamps through `hal_mock_pulse_capture_edge()`; same public interval assembly and timeout handling. | Deterministic host tests; no peripheral/IRQ latency simulation. |

On ESP32-S3, the input is inverted before prescaling when falling edges are
selected. Capture then uses the positive edge of the divided signal. Initial
phase is unspecified; consecutive captures span 32 original periods. The ISR
must service each event before the next capture: less than 320 us at 100 kHz,
about 914 us at 35 kHz. The peripheral has a single latch, so a delayed ISR can
lose an event without a distinct overrun indication. RAM queue overflow is
reported, but it does not detect every such loss. Reserve sufficient interrupt
latency margin. RP2350, STM32 and ESP32-S3 hardware validation is still pending.

The RP timing program runs through both input levels; matched-edge intervals
have an eight-clock quantization. DMA continues while application code runs.
Sample time is mapped to the system timer when the state machine starts, with
a small conservative offset; it is not intended for cross-device phase locking.
