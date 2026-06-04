# STM32G474 ADC reader - hardware verification

Verifies the **real** `hal_adc` backend on a **Nucleo-G474RE** by periodically
reading two ADC1 inputs and printing their raw codes (and a rough millivolt
estimate). Use it to confirm the bare-metal ADC1 polled conversion works on
silicon before relying on it.

- **ADC1**, single-ended, 12-bit, polled (one regular conversion per read)
- Inputs: **PA0 = ADC1_IN1** (Arduino **A0**), **PA1 = ADC1_IN2** (Arduino **A1**)
- ADC clock = HCLK/1 = 16 MHz (HSI bring-up clock); sample time 247.5 cycles
- Console: USART2 / ST-Link Virtual COM Port @ **115200 8N1**

> Status: the ADC register sequence (regulator -> calibration -> enable -> polled
> conversion) compiles and is written to RM0440, but has **not** been validated
> on silicon in this repo. This example is exactly that validation step - run it
> on your Nucleo-G474RE.

## Hardware wiring

```
Nucleo-G474RE
  PA0 (A0) ──── analog source 0..3V3  (e.g. potentiometer wiper)
  PA1 (A1) ──── analog source 0..3V3
  3V3 ───────── potentiometer top rail
  GND ───────── potentiometer bottom rail
```

Keep inputs within **0 .. VREF+ (3.3 V)**. A 10k potentiometer between 3V3 and
GND with its wiper on PA0/PA1 is the easiest test source. Leaving a pin
floating reads an arbitrary value - that is expected, not a bug.

## Build & flash (Linux Mint / Debian-like)

```bash
sudo apt update
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi stlink-tools tio
sudo usermod -aG dialout "$USER"      # then log out/in for serial access

cd examples/g474_adc_read
./build.sh                            # -> build/g474_adc_read.{elf,bin,hex}

st-info --probe                       # confirm the ST-Link sees the G474
st-flash --reset write build/g474_adc_read.bin 0x08000000
tio /dev/ttyACM0 -b 115200
```

(OpenOCD alternative: `openocd -f board/st_nucleo_g4.cfg -c "program build/g474_adc_read.elf verify reset exit"`.)

## Expected output

With a potentiometer on PA0 swept around mid-travel and PA1 tied to 3V3:

```
=== JaszczurHAL G474 ADC reader ===
ADC1: A0=PA0 (IN1), A1=PA1 (IN2), 12-bit, VREF+=3V3
  PA0 raw=2047  (~1650 mV)
  PA1 raw=4095  (~3300 mV)

  PA0 raw=3210  (~2587 mV)
  PA1 raw=4095  (~3300 mV)
```

A 0 V input should read near `0`, a 3V3 input near `4095`, and a potentiometer
should sweep smoothly between them. That confirms the regulator/calibration/
enable/convert sequence works end-to-end.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Value pinned at `0` on a driven pin | Pin not ADC1-reachable (check the map below); source not actually connected |
| Value pinned at `4095` | Input at/above VREF+, or pin shorted to 3V3 |
| Noisy / jumpy readings | Floating input, high source impedance, or no common ground between source and board |
| Console silent, board powered | Wrong port/baud, or terminal opened before reset - press the black RESET (B2) |
| `st-info --probe` finds nothing | Use the CN1 (ST-LINK) USB port; check `dmesg \| tail` for `ttyACM0` |

## Notes

- ADC1 single-ended pin -> channel map (JaszczurHAL pin id = `port*16 + pin`),
  per RM0440: PA0..PA3 -> IN1..IN4, PB0 -> IN15, PB1 -> IN12, PB11 -> IN14,
  PB12 -> IN11, PB14 -> IN5, PC0..PC3 -> IN6..IN9. Pins outside this set return 0.
- This is ADC1 only. ADC2..ADC5 are not wired on this backend yet (so e.g.
  PA4/PA5, which are ADC2 inputs, are not readable here - they are used by the
  `hal_dac` backend).
- The millivolt figure assumes VREF+ = 3.3 V and 12-bit full scale (4095). For
  an accurate result use the device VREFINT calibration; this example keeps the
  simple linear estimate for bring-up.
