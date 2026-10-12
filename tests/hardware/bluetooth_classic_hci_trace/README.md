# Bluetooth Classic raw HCI inquiry diagnostics

`tests/hardware/bluetooth_classic_hci_trace` records BTstack HCI commands and
events before the public manager parses them. The same fixture builds for Pico
W RP2040 and Pico 2 W RP2350 ARM. It also reports HCI transport counters and
the measured CYW43 gSPI clock. Bluetooth addresses are masked, unknown command
payloads and ACL bodies are hidden, and Extended Inquiry Result is retained
only through its RSSI byte; its EIR data body is redacted.

`device_tree.json` describes the hardware of both assemblies: `picow`
(Pico W, the default) and `pico2w` (Pico 2 W). Neither needs external wiring.

```sh
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_classic_hci_trace --assembly picow
vscode/entry/jh-vscode build \
  --project tests/hardware/bluetooth_classic_hci_trace --assembly pico2w
```

Use `SCAN` for one ten-second scan or `SCAN30` for three consecutive inquiry
cycles, then issue `INFO` and `DUMP`. `STOP` exercises cancellation and `RESET`
discards buffered records. This fixture uses private diagnostic interfaces and
is not part of the public HAL API.

The backend requests Extended Inquiry Result mode, so a successful scan shows
the EIR name and RSSI.

When one board finds a device and another does not, compare their traces. If
controller initialization and the Inquiry command match and neither board
reports dropped records or drain-budget hits, the difference lies in the radio
path (antenna, placement, a weak responder) rather than in the result parser,
the scan deadline, or the HCI transport.
