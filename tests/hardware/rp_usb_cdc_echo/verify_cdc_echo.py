#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
import re
import termios
import threading
import time

import serial
from serial.tools import list_ports

CHATTER_ENTER_MAGIC = b"JH:DTRSTUCK\n"
CHATTER_EXIT_MAGIC = b"JH:ECHO\n"


def deterministic_payload(length: int, salt: int) -> bytes:
    return bytes(((index * 73 + salt * 29) & 0xFF) for index in range(length))


def set_hupcl(port: serial.Serial, enabled: bool) -> None:
    """With HUPCL cleared, closing the port leaves DTR asserted - the exact
    Linux adapter behavior that used to watchdog-reset CDC debug firmware."""
    attrs = termios.tcgetattr(port.fileno())
    if enabled:
        attrs[2] |= termios.HUPCL
    else:
        attrs[2] &= ~termios.HUPCL
    termios.tcsetattr(port.fileno(), termios.TCSANOW, attrs)


def read_chatter_fields(port: serial.Serial, collect_s: float) -> dict:
    """Collect JHDTR lines for collect_s seconds and return the newest one.

    After a reopen the device FIFO first replays stale lines queued before the
    close, so the newest uptime is the only trustworthy sample.
    """
    deadline = time.monotonic() + collect_s
    buffer = bytearray()
    newest = None
    while time.monotonic() < deadline:
        chunk = port.read(256)
        if not chunk:
            continue
        buffer.extend(chunk)
        lines = buffer.split(b"\n")
        buffer = lines.pop()
        for line in lines:
            text = line.strip().decode("ascii", "replace")
            if not text.startswith("JHDTR "):
                continue
            fields = dict(
                pair.split("=", 1) for pair in text.split()[1:] if "=" in pair
            )
            if {"uptime_ms", "wdt_reboot", "seq"} <= fields.keys():
                parsed = {key: int(value) for key, value in fields.items()}
                if newest is None or parsed["uptime_ms"] >= newest["uptime_ms"]:
                    newest = parsed
    if newest is None:
        raise TimeoutError(
            "no JHDTR chatter line received - the device likely reset"
        )
    return newest


def dtr_stuck_phase(port_path: str, stuck_seconds: float) -> dict:
    with open_port(port_path) as port:
        drain(port)
        port.write(CHATTER_ENTER_MAGIC)
        port.flush()
        before = read_chatter_fields(port, 3.0)
        if before["wdt_reboot"] != 0:
            raise RuntimeError("device reports a watchdog reboot before the phase")
        set_hupcl(port, False)
    # Port closed, DTR still asserted, nobody reads: the device must keep
    # feeding its 4 s watchdog while its CDC FIFO stays full.
    time.sleep(stuck_seconds)
    with open_port(port_path) as port:
        set_hupcl(port, True)
        after = read_chatter_fields(port, 4.0)
        if after["wdt_reboot"] != 0:
            raise RuntimeError("watchdog reset during the DTR-stuck window")
        expected_ms = before["uptime_ms"] + int((stuck_seconds - 2.0) * 1000.0)
        if after["uptime_ms"] < expected_ms:
            raise RuntimeError(
                f"uptime went from {before['uptime_ms']} to "
                f"{after['uptime_ms']} ms - the device reset"
            )
        port.write(CHATTER_EXIT_MAGIC)
        port.flush()
    # Back in echo mode the transport must be fully usable again.
    with open_port(port_path) as port:
        payload = deterministic_payload(4096, 4)
        elapsed = exchange(port, payload, 20.0, 0.0)
    return {
        "name": "dtr_stuck_uptime",
        "stuckSeconds": stuck_seconds,
        "uptimeBeforeMs": before["uptime_ms"],
        "uptimeAfterMs": after["uptime_ms"],
        "echoAfterSeconds": round(elapsed, 3),
    }


def read_exact(port: serial.Serial, length: int, timeout_s: float) -> bytes:
    deadline = time.monotonic() + timeout_s
    received = bytearray()
    while len(received) < length:
        chunk = port.read(length - len(received))
        if chunk:
            received.extend(chunk)
            continue
        if time.monotonic() >= deadline:
            raise TimeoutError(
                f"received {len(received)} of {length} expected bytes"
            )
    return bytes(received)


def drain(port: serial.Serial) -> None:
    port.reset_input_buffer()
    port.reset_output_buffer()


def exchange(
    port: serial.Serial, payload: bytes, timeout_s: float, pause_before_read_s: float
) -> float:
    drain(port)
    started = time.monotonic()
    writer_result = {"written": 0, "error": None}

    def write_payload() -> None:
        try:
            writer_result["written"] = port.write(payload)
            port.flush()
        except Exception as exc:  # Propagate the writer failure on the main thread.
            writer_result["error"] = exc

    writer = threading.Thread(target=write_payload, name="cdc-probe-writer")
    writer.start()
    if pause_before_read_s > 0.0:
        time.sleep(pause_before_read_s)
    echoed = read_exact(port, len(payload), timeout_s)
    writer.join(timeout_s)
    if writer.is_alive():
        raise TimeoutError("CDC writer did not finish")
    if writer_result["error"] is not None:
        raise writer_result["error"]
    if writer_result["written"] != len(payload):
        raise RuntimeError(
            f"wrote {writer_result['written']} of {len(payload)} bytes"
        )
    elapsed = time.monotonic() - started
    if echoed != payload:
        mismatch = next(
            (
                index
                for index, (expected, actual) in enumerate(zip(payload, echoed))
                if expected != actual
            ),
            min(len(payload), len(echoed)),
        )
        raise RuntimeError(f"echo mismatch at byte {mismatch}")
    return elapsed


def open_port(path: str) -> serial.Serial:
    port = serial.Serial(
        path,
        baudrate=115200,
        timeout=0.1,
        write_timeout=10.0,
        exclusive=True,
    )
    port.dtr = True
    time.sleep(0.1)
    return port


def read_identity(port: serial.Serial, timeout_s: float) -> dict:
    """Ask for the identity line; the echo of the command comes first."""
    drain(port)
    port.write(b"JH:IDENTITY\n")
    port.flush()
    deadline = time.monotonic() + timeout_s
    buffer = bytearray()
    while time.monotonic() < deadline:
        buffer.extend(port.read(256))
        match = re.search(
            rb"JHID serial=(\w+) len=(\d+) uid=(\w+) reset=(\w+) wdt=(\d)\n",
            buffer)
        if match:
            return {
                "serial": match.group(1).decode(),
                "len": int(match.group(2)),
                "uid": match.group(3).decode(),
                "reset": match.group(4).decode(),
                "wdt": int(match.group(5)),
            }
    raise TimeoutError(f"no identity line, got {bytes(buffer[-120:])!r}")


def usb_serial_number(path: str) -> str:
    device = os.path.realpath(path)
    for info in list_ports.comports():
        if os.path.realpath(info.device) == device:
            return info.serial_number or ""
    return ""


def identity_reset_phase(port_path: str) -> dict:
    """The serial number must be the USB serial number (the flash unique id,
    eight bytes, the same as the UID); hal_system_reset() must come back as a
    software reset."""
    usb_serial = usb_serial_number(port_path)
    with open_port(port_path) as port:
        before = read_identity(port, 5.0)
        if (before["serial"] != usb_serial or before["len"] != 8 or
                before["uid"] != before["serial"]):
            raise RuntimeError(f"identity {before} vs USB serial {usb_serial}")
        port.write(b"JH:RESET\n")
        port.flush()
        time.sleep(0.5)
    deadline = time.monotonic() + 20.0
    after = None
    while after is None and time.monotonic() < deadline:
        time.sleep(0.5)
        try:
            with open_port(port_path) as port:
                after = read_identity(port, 3.0)
        except (serial.SerialException, OSError, TimeoutError):
            continue
    if after is None or after["reset"] != "SOFT" or after["serial"] != usb_serial:
        raise RuntimeError(f"after reset: {after}")
    return {"name": "identity_reset", "usbSerial": usb_serial,
            "before": before, "after": after}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--bytes", type=int, default=262144)
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--backpressure-pause", type=float, default=0.75)
    parser.add_argument(
        "--dtr-stuck-seconds",
        type=float,
        default=65.0,
        help="DTR-stuck uptime window in seconds (0 skips the phase)",
    )
    parser.add_argument(
        "--identity",
        action="store_true",
        help="also check the serial number and hal_system_reset() (resets the board)",
    )
    args = parser.parse_args()

    phases = []
    with open_port(args.port) as port:
        payload = deterministic_payload(4096, 1)
        elapsed = exchange(port, payload, args.timeout, 0.0)
        phases.append(("initial", payload, elapsed))

        payload = deterministic_payload(args.bytes, 2)
        elapsed = exchange(
            port, payload, args.timeout, args.backpressure_pause
        )
        phases.append(("throughput_backpressure", payload, elapsed))

    time.sleep(0.25)

    with open_port(args.port) as port:
        payload = deterministic_payload(16384, 3)
        elapsed = exchange(port, payload, args.timeout, 0.0)
        phases.append(("reconnect", payload, elapsed))

    phase_reports = [
        {
            "name": name,
            "bytes": len(payload),
            "elapsedSeconds": round(elapsed, 3),
            "bytesPerSecond": round(len(payload) / elapsed),
            "sha256": hashlib.sha256(payload).hexdigest(),
        }
        for name, payload, elapsed in phases
    ]

    if args.identity:
        time.sleep(0.25)
        phase_reports.append(identity_reset_phase(args.port))

    if args.dtr_stuck_seconds > 0.0:
        time.sleep(0.25)
        phase_reports.append(
            dtr_stuck_phase(args.port, args.dtr_stuck_seconds)
        )

    result = {
        "port": args.port,
        "phases": phase_reports,
        "status": "pass",
    }
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
