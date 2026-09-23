# RP GPIO interrupt context hardware test

`tests/hardware/rp_gpio_irq_context` checks context-aware GPIO interrupts on a
physical Pico or Pico 2. One handler serves several pins and has to tell the
instances apart from the context it was registered with, which is exactly what
the shared `IO_IRQ_BANK0` dispatch has to get right.

The probe needs no wiring and never drives a pad: edges come from switching a
pin's internal pull between down and up, so it is safe on a board whose
external circuitry is unknown. It uses GP2, GP3, GP4 and GP5.

What the firmware verifies on silicon:

- two pins sharing one handler reach their own context and report their own pin
  number;
- a context-free callback on a third pin keeps working next to them and does
  not disturb their counters;
- both handler kinds replace each other on one pin, in both directions;
- detaching stops every handler and clears the recorded owner core.

Build and perform the first BOOTSEL upload:

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/rp_gpio_irq_context \
  --target rp2040 --board pico
vscode/entry/jh-vscode upload-uf2 \
  --project tests/hardware/rp_gpio_irq_context \
  --target rp2040 --board pico
```

For Pico W and Pico 2 W use `--board picow` and `--board pico2w`. Use
`rp2350-arm` or `rp2350-riscv` with board `pico2` for Pico 2.

Run the verifier:

```sh
python3 -m pip install pyserial
python3 tests/hardware/rp_gpio_irq_context/verify_gpio_irq_context.py \
  --port /dev/serial/by-id/<device>
```

The firmware answers a single `T` command with one result line. `checks` counts
the assertions it ran, `failed` is a bitmask of the ones that did not hold, and
the per-probe `hits/pin` pairs are there to tell a dispatch bug apart from a pin
that simply never saw an edge. Leave every GP2..GP5 pin unloaded; anything
holding one of them defeats the internal pull and the probe reports zero hits
for that pin.
