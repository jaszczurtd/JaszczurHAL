# SX1262 raw LoRa hardware gate

This gate uses the buildable
[`27_lora_point_to_point`](../../../examples/27_lora_point_to_point/) firmware
and two radios in the same band. It validates initialization, bidirectional
over-the-air packets, sequence continuity, DIO1-driven asynchronous callbacks,
IRQ/cancel diagnostics, RSSI/SNR metadata, sleep/wake and radio destroy/create
reinitialization.

On profiles with a GPIO status LED, a solid LED indicates transmit activity
and a 120 ms pulse confirms a received packet.

Do not pair an LF device with an HF device. Confirm the labels on both radios
and antennas, connect the correct antenna before power-up, use 3.3 V I/O and
comply with local spectrum, power and duty-cycle rules.

## LF pair: two RP2040-LoRa-LF boards

Build and upload the initiator to the first board, then build and upload the
responder to the second board:

```bash
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf
vscode/entry/jh-vscode upload \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf \
  --port /dev/serial/by-id/<lf-initiator>

vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf --variant responder
vscode/entry/jh-vscode upload \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board rp2040-lora-lf --variant responder \
  --port /dev/serial/by-id/<lf-responder>
```

The firmware deliberately uses 434.0 MHz for this fixture; this is a test
configuration, not a universal regulatory preset.

## HF pair: external Core1262-HF on RP2040 and STM32G474

Use the external wiring table in the example README. Build the RP2040/Pico as
initiator and the NUCLEO-G474RE as responder (or reverse both roles):

```bash
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target rp2040 --board pico
vscode/entry/jh-vscode build \
  --project examples/27_lora_point_to_point \
  --target stm32g474 --board nucleo-g474re --variant responder
```

Both Core1262-HF devices use the same electrical helper and EU868 technical
preset while the host pin mapping stays in the example.

## Verify

Run long enough to observe the automatic lifecycle probes at sequence 10 and
20:

```bash
python3 tests/hardware/lora_sx1262/verify_pair.py \
  --initiator-port /dev/serial/by-id/<initiator> \
  --responder-port /dev/serial/by-id/<responder> \
  --duration 75
```

A pass requires at least five matched ping/pong sequences, packet metadata,
the asynchronous event-loop marker on both radios, non-zero IRQ/callback/cancel
counters, `HAL_OK` sleep/wake and `HAL_OK` reinitialization. Then swap which
physical device receives the `responder` build and repeat.

Repeat the gate with `HAL_LORA_EXAMPLE_SF` and
`HAL_LORA_EXAMPLE_TX_POWER_DBM` supplied as project extra definitions for at
least two legal combinations for the test location. The LF desk fixture uses SF7/6 dBm
and SF9/10 dBm; do not assume that SF12/14 dBm is permitted. Both ends of a run
must use the same values. Record module/antenna labels, exact wiring, firmware
revision, distance, packet counts, loss, RSSI/SNR range and the verifier JSON
in the private hardware report.
