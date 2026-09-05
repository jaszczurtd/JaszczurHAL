# 30 - Bluetooth speaker

This RP-only example turns a Pico W or Pico 2 W into a Bluetooth Classic A2DP
Sink named `JaszczurHAL Speaker`. It accepts SBC at 44.1 or 48 kHz in mono,
stereo, or joint-stereo mode, downmixes to signed mono PCM, and feeds a
timer-paced DMA PWM output. The base build is A2DP-only; the `avrcp` variant
adds absolute volume, and `ble-a2dp` proves BLE and Classic/A2DP coexist on the
shared CYW43 controller. The output adapter prebuffers roughly 171-186 ms and
refills toward roughly 213-232 ms, depending on the negotiated sample rate, to
absorb source and RF jitter. The example reserves 4 KiB for the active core-0
stack after measured SBC and flash-backed bonding paths exhausted the safe
margin of the 2 KiB default.

The inquiry identity uses Class of Device `0x240414`: Audio and Rendering
service classes, Audio/Video major class, and Loudspeaker minor class. The
Rendering bit is required for Android-compatible A2DP Sink classification.

## Wiring

The PWM output is **GP6**. Each signed PCM sample is converted to one of 256
duty-cycle levels, so the PWM carrier follows the negotiated sample rate:
44.1 or 48 kHz. Do not connect a passive speaker directly to the Pico. A
minimal signal path is:

```text
GP6 ---- 1 kOhm ----+---- powered amplifier high-impedance input
                    |
                   10 nF
                    |
GND ----------------+---- amplifier GND
```

This first-order low-pass has a cutoff near 15.9 kHz. Use a properly designed
second-order reconstruction filter when audio quality matters. AC-couple the
filter output if the amplifier input does not tolerate the PWM midpoint DC
bias. Power and size the amplifier for the attached loudspeaker; the Pico pin
is only a logic-level source.

## Build

```bash
./scripts/examples_dispatcher.py build --target rp2040 \
  --example 30_bluetooth_speaker
./scripts/examples_dispatcher.py build --target rp2350-arm \
  --example 30_bluetooth_speaker

vscode/entry/jh-vscode build --project examples/30_bluetooth_speaker \
  --target rp2040 --board picow --variant avrcp
vscode/entry/jh-vscode build --project examples/30_bluetooth_speaker \
  --target rp2350-arm --board pico2w --variant ble-a2dp
```

On an empty bond store the firmware opens one 60-second discoverable pairing
window and automatically accepts a pending Just Works/PIN request only during
that window. Once the first valid SBC frame arrives, the Classic manager stores
the shared link key with the A2DP profile identifier. AVRCP never stores a
second key. Known phones can reconnect while the device remains
non-discoverable.

The serial commands are `INFO`, `PAIR`, `RESET`, and `WATCHDOG`. `PAIR` opens
another bounded window. `RESET` removes the persisted bond and keeps pairing
closed until an explicit `PAIR` command or a restart with empty storage.
Use `PAIR` after removing the speaker on a phone: the bounded replacement
window remains open even while Pico retains the old bond, and closes after the
replacement produces its first valid SBC frame.
`WATCHDOG` deliberately stops servicing the four-second watchdog, allowing an
actual watchdog-reset reconnect test; the next boot reports the latched reset
reason. `INFO` reports stream format, packet loss, dropped/corrupt frames,
bounded-queue and BTstack-pool high-water marks, stack use, clock correction,
DMA use and underruns, adapter drops, and poll-context CPU timing. Diagnostics
never print a Bluetooth address, link key, or audio contents.

For hardware acceptance, verify pairing, audio start, pause/resume/stop,
absolute volume with the `avrcp` image, at least 30 minutes of playback,
reconnect after device and phone restarts, watchdog-free cold boot, and bond
removal through `RESET`. Use `WATCHDOG` for the separate real-watchdog reboot
and reconnect check.

An XY-BT-Mini-class module cannot act as the test source: it is another A2DP
Sink. Use a phone, computer, or a dedicated A2DP Source instead.
