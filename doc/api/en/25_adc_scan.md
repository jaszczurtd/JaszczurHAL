# Hardware-paced ADC scan

Enable `HAL_ENABLE_ADC_SCAN` and include `<hal/analog/hal_adc_scan.h>` to
sample a fixed set of pins continuously at a hardware-timed period. The
converter runs the pins one after another, DMA writes the results into the
two halves of a buffer you own, and the application only ever reads finished
blocks. Every sample has a known position in time, and the CPU never waits
for a conversion.

```c
static uint16_t blocks[2u * 400u * 3u] __attribute__((aligned(4)));

hal_adc_scan_config_t config = {0};
config.pins[0] = 26u; /* GPIO26 on RP, PA0 (0) on STM32G474, an ADC1 pad on ESP32-S3 */
config.pins[1] = 28u;
config.pins[2] = HAL_ADC_SCAN_PIN_TEMPERATURE;
config.pin_count = 3u;
config.conversion_period_ns = 4000u; /* each pin every 12 us */
config.buffer = blocks;
config.block_frames = 400u;          /* one block every 4.8 ms */
hal_status_t status = hal_adc_scan_start(&config);

/* In the application loop. */
hal_adc_scan_block_t block;
if (status == HAL_OK && hal_adc_scan_take(&block) == HAL_OK) {
    const uint8_t shunt = hal_adc_scan_pin_position(26u);
    for (uint32_t k = 0u; k < block.frames; ++k) {
        const uint16_t raw = block.samples[(k * block.pin_count) + shunt];
        /* k * hal_adc_scan_frame_period_ns() is the sample's offset. */
        (void)raw;
    }
}
```

The `scan` variant of [13_adc](../../../examples/13_adc/) prints block
statistics on RP and STM32G474 targets.

## Blocks, positions and the marker

A frame holds one sample of every pin. Frames are laid out one after another
in a block, and the position of a pin inside a frame comes from
`hal_adc_scan_pin_position()`: the RP converter delivers its inputs in
ascending order whatever the configured order, STM32G474 and ESP32-S3 keep
the configured order. `hal_adc_scan_frame_period_ns()` is the time between two
samples of one pin as the converter actually runs; backends round the
requested conversion period up to what they can do.

`hal_adc_scan_take()` hands out the newest completed block once and returns
`HAL_EAGAIN` until the next one completes. The block stays valid until the
following block completes, so process or copy it within one block period; a
block a slow consumer never asked for is not queued, it only shows as a gap
in `sequence`. `hal_adc_scan_latest()` returns the newest sample of a pin,
from the block in progress when it already holds a full frame.

The optional `marker` hook runs at block completion and its result travels
with the block. On RP and STM32G474 that is inside the DMA interrupt on the
core that started the scan: a few loads at most, no HAL calls, no locks. Use
it to pair a block with something like a PWM counter. On ESP32-S3 the hook and
`completed_us` are taken by the task that collects the block.

## Ownership

Start and stop belong to one owner core; take and latest are serialized and
must not be called from an ISR. While a scan runs it owns the converter. On
RP and STM32G474 the DACless audio path and the scan exclude each other with
`HAL_EBUSY`, and `hal_adc_read()` of a scanned pin returns the newest scanned
sample instead of converting; a pin outside the scan reads 0 for the duration,
as it does under DACless. On ESP32-S3 the scan keeps ADC1 in continuous mode
and `hal_adc_read()` of a scanned pin is served the same way.

## Backend limits

| Target | Period | Pins | Notes |
| --- | --- | --- | --- |
| RP2040 / RP2350 | 2 us and up, whole 48 MHz cycles | up to 5 (GPIO26..29 and temperature) | two DMA channels, `DMA_IRQ_0` exclusively |
| STM32G474 | sample-time steps from about 0.35 us; 5.8 us with the temperature sensor | up to 8 | ADC1 regular sequence, DMA1 channel 3 circular |
| ESP32-S3 | 12 us and up (83.3 kS/s) | ADC1 pads only, no temperature | ESP-IDF continuous driver |

`hal_adc_scan_start()` reports `HAL_EUNSUPPORTED` for a period or pin the
backend cannot serve, `HAL_EBUSY` when the converter or the interrupt is
already taken, and `HAL_ENOMEM` when no DMA channel is free. Stop is
idempotent and releases everything, also after a failed start.

The RP2040 backend is validated on hardware; RP2350, STM32G474 and ESP32-S3
hardware validation is still pending.
