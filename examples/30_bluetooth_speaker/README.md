<a id="30---bluetooth-speaker"></a>

# 30 - Bluetooth audio playback

This example turns a Pico W or Pico 2 W into a Bluetooth Classic A2DP
receiver named `JaszczurHAL Speaker`. It accepts SBC audio at 44.1 or 48 kHz
in mono, stereo, or joint-stereo mode, decodes it to signed mono PCM, and
plays it through PWM on GP6. A timer and DMA control sample delivery.

The base version supports A2DP. The `avrcp` variant adds absolute volume
control, while `ble-a2dp` compiles BLE and Classic/A2DP support together
for the shared CYW43 controller. This example is available only for RP
boards.

## Wiring

**Do not connect a passive speaker directly to the Pico.** GP6 provides a
signal for an amplifier; it cannot power a loudspeaker. Each PCM sample
maps to one of 256 PWM duty-cycle levels. The PWM carrier is 44.1 or 48 kHz,
matching the negotiated sample rate.

A minimal connection to a powered amplifier is:

```text
GP6 ---- 1 kOhm ----+---- powered amplifier high-impedance input
                    |
                   10 nF
                    |
GND ----------------+---- amplifier GND
```

The 1 kΩ/10 nF low-pass filter has a cutoff near 15.9 kHz. For better audio
quality, use a properly designed second-order reconstruction filter.
AC-couple the output if the amplifier cannot tolerate the DC bias from
the PWM midpoint. Connect the grounds and choose amplifier power and
supply ratings appropriate for the loudspeaker.

## Build

Run from the repository root:

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

## Pair and play audio

With no stored device, the program opens one 60-second pairing window.
Only during this window is it discoverable and willing to automatically
accept a pending Just Works/PIN request. After receiving the first valid
SBC frame, it stores the shared link key with the A2DP profile identifier.
AVRCP uses the same key rather than storing another one. A known phone can
subsequently reconnect while the receiver remains non-discoverable.

On Android, open the new-device pairing screen during this window. Select
`JaszczurHAL Speaker`, accept the Just Works request, and start playback.
Automatic approval is an example policy; choose an appropriate consent
mechanism for the access requirements of a product.

The device uses Class of Device `0x240414`: Audio and Rendering service
classes, the Audio/Video major class, and the Loudspeaker minor class. The
Rendering bit provides Android-compatible classification as an A2DP receiver.

## Serial commands

| Command | Behavior |
|---|---|
| `INFO` | Print stream state and diagnostic counters. |
| `PAIR` | Open another time-limited pairing window. |
| `RESET` | Remove the stored bond without immediately reopening pairing. |
| `WATCHDOG` | Stop servicing the watchdog deliberately, causing a reset after four seconds. |

Use `PAIR` after removing the speaker from a phone's device list. This
window allows replacement even if the Pico still holds the previous bond.
It closes after the first valid SBC frame from the replacement connection.
After `RESET`, pairing stays closed until `PAIR` or a restart with empty
storage. `WATCHDOG` is for a separate reconnection test after a real
watchdog reset; the next boot prints the retained reset reason.

## Buffering and diagnostics

Playback starts after roughly 171-186 ms of audio has been buffered. The
output then refills toward roughly 213-232 ms. These values depend on the
sample rate; buffering absorbs variations in source and radio delivery.
The configuration reserves 4 KiB for the core-0 stack. Earlier measurements
of SBC decoding and flash-backed bond storage found insufficient margin
with the default 2 KiB.

`INFO` reports the stream format, packet loss, dropped or corrupt frames,
maximum queue and BTstack pool usage, and stack use. It also reports clock
correction, DMA use and underruns, output-adapter drops, and CPU time spent
in `poll`. Diagnostics do not print Bluetooth addresses, link keys, or
audio contents.

## Hardware test coverage

The `rp2040:picow` test used an Android POCO M8 phone and a filtered, amplified GP6 pin
output.

