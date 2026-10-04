#!/usr/bin/env python3
"""Drive the STM32 UART fixture over the ST-LINK virtual COM port.

The fixture answers line commands on hal_uart PORT_2 (USART2) at 3 Mbaud 8N1
after a three-second boot window in which the debug console prints its boot
line at 115200. The verifier checks echo integrity, both send paths and their
rates, overrun recovery, frame formats, rate changes, line error counters, the
console hand-over in both directions, USART1 through its single-wire
self-test, receive interrupts held off by a mask, and, unless resets are
skipped, the software reset and the stack guard with the port open. A
FreeRTOS build also runs a blocking writer and a reader in two tasks.
"""

import argparse
import json
import os
import random
import re
import sys
import threading
import time

import serial

APP_BAUD = 3_000_000
CONSOLE_BAUD = 115_200
RX_RING = 512  # HAL_UART_RX_BUFFER_SIZE of the fixture

STAT = re.compile(
    rb"STAT baud=(\d+) fmt=(\w+) serial=([0-9A-F]+) len=(\d+) uid=([0-9A-F]+) "
    rb"ore=(\d+) fe=(\d+) pe=(\d+) ovf=(\d+) txfree=(\d+) rtos=(\d) "
    rb"clock=(\d+)"
)
LOOP = re.compile(
    rb"OK loop1 baud=(\d+) fmt=(\w+) sent=(\d+) got=(\d+) bad=(\d+) "
    rb"ovf=(\d+) ore=(\d+) fe=(\d+) pe=(\d+) ms=(\d+)"
)
DUPLEX = re.compile(
    rb"OK duplex sent=(\d+) got=(\d+) sum=([0-9a-f]{8}) probes=(\d+) "
    rb"refused=(\d+) probe_max_us=(\d+) ovf=(\d+)"
)
BOOT = re.compile(rb"JHUARTBOOT reset=(\w+) serial=([0-9A-F]+) len=(\d+) uid=([0-9A-F]+)")
FORMATS = {
    "8N1": (8, serial.PARITY_NONE, 1),
    "8N2": (8, serial.PARITY_NONE, 2),
    "8E1": (8, serial.PARITY_EVEN, 1),
    "8O1": (8, serial.PARITY_ODD, 1),
    "8E2": (8, serial.PARITY_EVEN, 2),
    "7E1": (7, serial.PARITY_EVEN, 1),
    "7O1": (7, serial.PARITY_ODD, 1),
    "7N1": (7, serial.PARITY_NONE, 1),
    "6E1": (6, serial.PARITY_EVEN, 1),
    "6O1": (6, serial.PARITY_ODD, 1),
}


def pattern(count):
    return bytes(((i * 31) + (i >> 8)) & 0xFF for i in range(count))


def stream_sum(data):
    total = 0
    for byte in data:
        total = (total * 31 + byte) & 0xFFFFFFFF
    return total


class Fixture:
    def __init__(self, path):
        self.port = serial.Serial(path, baudrate=APP_BAUD, timeout=0.2)
        self.results = {}
        self.failures = []

    def check(self, name, ok, detail="", optional=False):
        """An optional check is reported but does not fail the run."""
        self.results[name] = {"ok": bool(ok), "detail": detail,
                              "optional": optional}
        if not ok and not optional:
            self.failures.append(f"{name}: {detail}")
        label = "PASS" if ok else ("INFO" if optional else "FAIL")
        print(f"{label} {name} {detail}".rstrip(), flush=True)
        return ok

    def settings(self, baud=APP_BAUD, fmt="8N1"):
        bits, parity, stop = FORMATS[fmt]
        self.port.baudrate = baud
        self.port.bytesize = bits
        self.port.parity = parity
        self.port.stopbits = stop
        time.sleep(0.02)
        self.port.reset_input_buffer()

    def send_line(self, text):
        self.port.write(text.encode() + b"\n")
        self.port.flush()

    def read_line(self, timeout=2.0):
        deadline = time.monotonic() + timeout
        line = bytearray()
        while time.monotonic() < deadline:
            byte = self.port.read(1)
            if not byte:
                continue
            if byte == b"\n":
                return bytes(line).rstrip(b"\r")
            line.extend(byte)
        return None

    def expect(self, prefix, timeout=2.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.read_line(max(0.05, deadline - time.monotonic()))
            if line is not None and line.startswith(prefix):
                return line
        return None

    def read_exact(self, count, timeout):
        deadline = time.monotonic() + timeout
        data = bytearray()
        while len(data) < count and time.monotonic() < deadline:
            data.extend(self.port.read(count - len(data)))
        return bytes(data)

    def status(self, timeout=1.0):
        self.send_line("S")
        line = self.expect(b"STAT ", timeout)
        if line is None:
            return None
        m = STAT.search(line)
        if m is None:
            return None
        keys = ("baud", "fmt", "serial", "len", "uid", "ore", "fe", "pe", "ovf",
                "txfree", "rtos", "clock")
        values = {}
        for key, raw in zip(keys, m.groups()):
            values[key] = raw.decode() if key in ("fmt", "serial", "uid") else int(raw)
        return values

    def wait_boot(self, timeout):
        self.settings(CONSOLE_BAUD)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            line = self.read_line(0.5)
            if line is None:
                continue
            m = BOOT.search(line)
            if m:
                return {
                    "reset": m.group(1).decode(),
                    "serial": m.group(2).decode(),
                    "len": int(m.group(3)),
                    "uid": m.group(4).decode(),
                }
        return None

    def sync(self, boot_timeout):
        """Reach command mode at 3 Mbaud, catching the boot line if seen."""
        self.settings()
        self.send_line("")
        st = self.status(0.5)
        if st is not None:
            return None, st
        boot = self.wait_boot(boot_timeout)
        deadline = time.monotonic() + 6.0
        while time.monotonic() < deadline:
            self.settings()
            self.send_line("")
            st = self.status(0.5)
            if st is not None:
                return boot, st
        return boot, None

    # ── checks ────────────────────────────────────────────────────────────
    def echo(self, name, count, mask=0xFF, optional=False):
        payload = bytes(b & mask for b in os.urandom(count))
        self.send_line(f"E{count}")
        if self.expect(b"READY echo", 1.0) is None:
            return self.check(name, False, "no READY", optional)
        # A separate reader keeps the host draining while it writes: the
        # ST-LINK drops what the host leaves unread for too long.
        echoed = bytearray()
        reading = threading.Event()
        reading.set()

        def reader():
            while reading.is_set() and len(echoed) < count:
                echoed.extend(self.port.read(min(65536, count - len(echoed))))

        thread = threading.Thread(target=reader)
        started = time.monotonic()
        thread.start()
        for offset in range(0, count, 4096):
            self.port.write(payload[offset:offset + 4096])
        thread.join(10.0 + count / 20000)
        reading.clear()
        thread.join()
        elapsed = time.monotonic() - started
        done = self.expect(b"OK echo", 3.0)
        same = bytes(echoed) == payload
        first_bad = next((i for i, (a, b) in enumerate(zip(echoed, payload)) if a != b), None)
        detail = f"n={count} got={len(echoed)} {count / elapsed / 1000:.0f} kB/s"
        if not same:
            detail += f" first_mismatch={first_bad}"
        if done is not None:
            detail += " " + done.decode().replace("OK echo ", "dev:")
        return self.check(name, same and done is not None, detail, optional)

    def send_path(self, name, op, count, baud):
        self.send_line(f"{op}{count}")
        if self.expect(b"READY tx", 1.0) is None:
            return self.check(name, False, "no READY")
        data = self.read_exact(count, 10.0 + count / 20000)
        done = self.expect(b"OK tx", 3.0)
        ms = None
        if done is not None:
            m = re.search(rb"sent=(\d+) again=(\d+) ms=(\d+)", done)
            ms = int(m.group(3)) if m else None
        line_ms = count * 10 * 1000 / baud
        # The device queues the last ring-full without waiting for it.
        fast = ms is not None and ms <= line_ms * 1.10 + 20
        ok = data == pattern(count) and fast
        detail = f"n={count} got={len(data)} dev_ms={ms} line_ms={line_ms:.0f}"
        if done is not None:
            detail += " " + done.decode().replace("OK tx ", "dev:")
        return self.check(name, ok, detail)

    def overrun(self):
        before = self.status()
        sent = 2000
        payload = bytes(random.randrange(256) for _ in range(sent))
        self.send_line("O300")
        if self.expect(b"READY hold", 1.0) is None:
            return self.check("overrun_keeps_newest_half", False, "no READY")
        self.port.write(payload)
        line = self.expect(b"DATA ", 3.0)
        if line is None:
            return self.check("overrun_keeps_newest_half", False, "no DATA")
        count = int(line.split()[1])
        data = self.read_exact(count, 2.0)
        done = self.expect(b"OK hold", 2.0)
        m = re.search(rb"ovf=(\d+)", done) if done else None
        ovf = int(m.group(1)) if m else -1
        after = self.status()
        ok = (count == RX_RING // 2 and data == payload[-count:]
              and ovf == sent - count and after is not None
              and after["ovf"] == before["ovf"] + sent - count)
        return self.check("overrun_keeps_newest_half", ok,
                          f"sent={sent} kept={count} ovf={ovf} suffix_match={data == payload[-count:]}")

    def switch(self, op, value, baud, fmt):
        """F/B command: True when switched, False when refused, None on error."""
        # Replies take longer at slow rates: two lines of up to 40 frames.
        line_time = 80 * 12 / self.port.baudrate
        self.send_line(f"{op}{value}")
        if self.expect(b"OK ", 1.0 + line_time) is None:
            return None
        refused = self.expect(b"ERR ", 0.2 + line_time)
        if refused is not None:
            return False
        self.settings(baud, fmt)
        return True

    def frame_formats(self):
        for fmt in ("8E1", "8O1", "8N2", "8E2", "7E1", "7O1"):
            switched = self.switch("F", fmt, APP_BAUD, fmt)
            if not switched:
                self.check(f"format_{fmt}", False, f"switch={switched}")
                self.settings()
                continue
            self.echo(f"format_{fmt}_echo", 8192, 0x7F if fmt[0] == "7" else 0xFF)
            if fmt == "8E1":
                self.parity_errors()
            back = self.switch("F", "8N1", APP_BAUD, "8N1")
            self.check(f"format_{fmt}_back", back is True, f"switch={back}")
        # The device carries these; whether the ST-LINK bridge does is what
        # the run finds out, so they do not fail it (the USART1 self-test
        # covers them inside the chip).
        for fmt in ("7N1", "6E1", "6O1"):
            try:
                switched = self.switch("F", fmt, APP_BAUD, fmt)
            except (serial.SerialException, ValueError) as error:
                switched = f"host refused: {error}"
            if switched is True:
                self.echo(f"vcp_format_{fmt}_echo", 4096,
                          (1 << int(fmt[0])) - 1, optional=True)
            else:
                self.check(f"vcp_format_{fmt}", False, f"switch={switched}",
                           optional=True)
            back = self.switch("F", "8N1", APP_BAUD, "8N1")
            if back is not True:
                # The fixture returns to 8N1 on its own after 5 s without a
                # command it could read.
                time.sleep(6.0)
                self.settings()
                self.send_line("")
                back = self.status() is not None
            self.check(f"format_{fmt}_back", back is True, f"switch={back}")
        for fmt in ("5N1", "6N1"):
            refused = self.switch("F", fmt, APP_BAUD, "8N1")
            st = self.status()
            self.check(f"format_{fmt}_refused", refused is False and st is not None
                       and st["fmt"] == "8N1", f"switch={refused}")

    def parity_errors(self):
        before = self.status()
        self.port.parity = serial.PARITY_ODD
        time.sleep(0.02)
        self.port.write(b"\x55" * 32)
        self.port.flush()
        time.sleep(0.05)
        self.port.parity = serial.PARITY_EVEN
        time.sleep(0.02)
        self.send_line("")
        self.port.reset_input_buffer()
        after = self.status()
        ok = before is not None and after is not None and after["pe"] > before["pe"]
        self.check("parity_errors_counted", ok,
                   f"pe {before and before['pe']} -> {after and after['pe']}")

    def framing_errors(self):
        before = self.status()
        self.port.baudrate = 1_000_000
        time.sleep(0.02)
        self.port.write(b"\x00" * 32)
        self.port.flush()
        time.sleep(0.05)
        self.settings()
        self.send_line("")
        after = self.status()
        ok = before is not None and after is not None and after["fe"] > before["fe"]
        self.check("framing_errors_counted", ok,
                   f"fe {before and before['fe']} -> {after and after['fe']}")

    def rates(self):
        switched = self.switch("B", 1_000_000, 1_000_000, "8N1")
        if self.check("rate_1M_switch", switched is True, f"switch={switched}"):
            self.echo("rate_1M_echo", 32768)
            self.send_path("rate_1M_try_write", "T", 50000, 1_000_000)
        switched = self.switch("B", 9600, 9600, "8N1")
        if self.check("rate_9600_switch", switched is True, f"switch={switched}"):
            self.echo("rate_9600_echo", 256)
        # Below SYSCLK/65535 the divider needs the USART prescaler.
        switched = self.switch("B", 1200, 1200, "8N1")
        if self.check("rate_1200_switch", switched is True, f"switch={switched}"):
            self.echo("rate_1200_echo", 64)
        refused = self.switch("B", 20_000_000, 9600, "8N1")
        self.check("rate_20M_refused", refused is False, f"switch={refused}")
        back = self.switch("B", APP_BAUD, APP_BAUD, "8N1")
        self.check("rate_3M_back", back is True, f"switch={back}")

    def leak(self):
        self.send_line("L")
        captured = bytearray()
        deadline = time.monotonic() + 1.5
        while time.monotonic() < deadline:
            captured.extend(self.port.read(256))
            if b"OK leak" in captured:
                break
        ok = b"OK leak" in captured and b"JHUARTLEAK" not in captured
        self.check("console_silent_while_owned", ok, f"captured={bytes(captured)!r}")

    def console(self):
        self.send_line("C")
        if self.expect(b"OK console", 1.0) is None:
            return self.check("console_returns", False, "no OK")
        self.settings(CONSOLE_BAUD)
        lines = []
        deadline = time.monotonic() + 1.3
        while time.monotonic() < deadline:
            line = self.read_line(0.3)
            if line and b"JHUARTCONSOLE" in line:
                lines.append(line.decode("utf-8", "replace"))
        self.check("console_returns", len(lines) >= 3, f"lines={len(lines)}")
        st = None
        deadline = time.monotonic() + 4.0
        while st is None and time.monotonic() < deadline:
            self.settings()
            self.send_line("")
            st = self.status(0.4)
        self.check("port_taken_back", st is not None and st["baud"] == APP_BAUD,
                   f"status={st}")

    def loopback(self):
        cases = [(3_000_000, "8N1", 4096), (1_000_000, "8N1", 4096),
                 (3_000_000, "8E1", 4096), (3_000_000, "8O1", 4096),
                 (3_000_000, "8E2", 4096),
                 (3_000_000, "7N1", 4096), (3_000_000, "7E1", 4096),
                 (3_000_000, "6E1", 4096), (3_000_000, "6O1", 4096),
                 (1200, "8N1", 48)]
        for baud, fmt, count in cases:
            self.send_line(f"Q{baud},{fmt},{count}")
            line = self.expect(b"OK loop1", 10.0 + count * 20 / baud)
            m = LOOP.search(line) if line else None
            name = f"usart1_loop_{baud}_{fmt}"
            if m is None:
                self.check(name, False, f"reply={line}")
                continue
            sent, got, bad, ovf, ore, fe, pe = (int(m.group(i)) for i in range(3, 10))
            self.check(name, sent == count and got == count and bad == 0 and
                       ovf == 0 and ore == 0 and fe == 0 and pe == 0,
                       line.decode().replace("OK loop1 ", ""))

    def masked_send(self, sent, mask_ms):
        """Send while the device masks interrupts; returns what came back."""
        payload = os.urandom(sent)
        self.send_line(f"M{mask_ms}")
        if self.expect(b"READY mask", 1.0) is None:
            return payload, -1, b"", -1, -1
        self.port.write(payload)
        line = self.expect(b"DATA ", 3.0)
        count = int(line.split()[1]) if line else -1
        data = self.read_exact(max(count, 0), 2.0)
        done = self.expect(b"OK mask", 2.0)
        m = re.search(rb"ovf=(\d+) ore=(\d+)", done) if done else None
        ovf, ore = (int(m.group(1)), int(m.group(2))) if m else (-1, -1)
        return payload, count, data, ovf, ore

    def mask_sweep(self, ring=RX_RING, mask_ms=40):
        """The host sends while the device keeps interrupts masked for 40 ms,
        far longer than one and a half rings take. What comes back must be
        every byte, or the newest bytes with the rest counted as lost, or
        the newest bytes with an overrun event, which marks an interrupt so
        late that whole rings may have gone by: never a silent gap. A mask
        shorter than one and a half rings must raise no event."""
        payload, count, data, ovf, ore = self.masked_send(400, 2)
        self.check("mask_short_no_event",
                   count == 400 and data == payload and ovf == 0 and ore == 0,
                   f"kept={count} ovf={ovf} ore={ore}")
        outcomes = []
        for sent in (300, 480, 700, 900, 1100, 1300, 1600, 2000, 2600, 4000):
            payload, count, data, ovf, ore = self.masked_send(sent, mask_ms)
            suffix = 0 < count <= ring and data == payload[-count:]
            if count == sent and data == payload and ovf == 0:
                outcome = "intact"
            elif suffix and ovf == sent - count:
                outcome = "counted"
            elif suffix and ore > 0:
                outcome = "flagged"
            else:
                outcome = "silent"
            outcomes.append(f"{sent}:{outcome}")
            self.check(f"mask_{sent}_bytes", outcome != "silent",
                       f"{outcome} kept={count} ovf={ovf} ore={ore} "
                       f"suffix={suffix}")
        return outcomes

    def stack_overflow(self):
        """The guard's reset path must stay off USART2 while the port is
        open; the next boot reports the overflow."""
        self.send_line("Z")
        if self.expect(b"OK overflow", 1.0) is None:
            return self.check("stack_overflow_silent", False, "no OK")
        captured = bytearray()
        deadline = time.monotonic() + 1.0
        while time.monotonic() < deadline:
            captured.extend(self.port.read(256))
        self.check("stack_overflow_silent", b"STACK" not in captured,
                   f"captured={bytes(captured[:80])!r}")
        boot = self.wait_boot(6.0)
        self.check("stack_overflow_reported",
                   boot is not None and boot["reset"] == "STACK_OVERFLOW",
                   f"boot={boot}")
        st = None
        deadline = time.monotonic() + 6.0
        while st is None and time.monotonic() < deadline:
            self.settings()
            self.send_line("")
            st = self.status(0.4)
        self.check("port_after_stack_overflow", st is not None, f"status={st}")

    def duplex(self, baud, count, optional=False):
        """FreeRTOS: a blocking writer in one task, a reader and lock probe
        in the other."""
        name = f"duplex_{baud}"
        switched = self.switch("B", baud, baud, "8N1")
        if not self.check(f"{name}_switch", switched is True, f"switch={switched}"):
            return
        payload = os.urandom(count)
        self.send_line(f"P{count}")
        if self.expect(b"READY duplex", 1.0) is None:
            self.check(name, False, "no READY", optional)
            return
        received = bytearray()

        def reader():
            deadline = time.monotonic() + 10.0 + count * 30 / baud
            while len(received) < count and time.monotonic() < deadline:
                received.extend(self.port.read(min(65536, count - len(received))))

        thread = threading.Thread(target=reader)
        thread.start()
        for offset in range(0, count, 4096):
            self.port.write(payload[offset:offset + 4096])
        thread.join()
        line = self.expect(b"OK duplex", 10.0)
        m = DUPLEX.search(line) if line else None
        if m is None:
            self.check(name, False, f"reply={line}", optional)
            return
        sent, got = int(m.group(1)), int(m.group(2))
        device_sum = int(m.group(3), 16)
        probes, refused, probe_us, ovf = (int(m.group(i)) for i in range(4, 8))
        ok = (bytes(received) == pattern(count) and sent == count and
              got == count and device_sum == stream_sum(payload) and
              refused > 0 and probe_us < 5000 and ovf == 0)
        self.check(name, ok, line.decode().replace("OK duplex ", "") +
                   f" tx_match={bytes(received) == pattern(count)}", optional)
        back = self.switch("B", APP_BAUD, APP_BAUD, "8N1")
        self.check(f"{name}_back", back is True, f"switch={back}")

    def reset(self):
        self.send_line("R")
        if self.expect(b"OK reset", 1.0) is None:
            return self.check("software_reset", False, "no OK")
        boot = self.wait_boot(5.0)
        self.check("software_reset", boot is not None and boot["reset"] == "SOFT",
                   f"boot={boot}")
        st = None
        deadline = time.monotonic() + 6.0
        while st is None and time.monotonic() < deadline:
            self.settings()
            self.send_line("")
            st = self.status(0.4)
        self.check("port_after_reset", st is not None and st["ovf"] == 0, f"status={st}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--expect-serial",
                        help="24 hex digits of the UID words, e.g. read with "
                        "openocd 'mdw 0x1fff7590 3'")
    parser.add_argument("--skip-reset", action="store_true",
                        help="leave out the software reset check")
    parser.add_argument("--boot-timeout", type=float, default=15.0)
    parser.add_argument("--json", help="write the results to this file")
    args = parser.parse_args()

    fx = Fixture(args.port)
    boot, st = fx.sync(args.boot_timeout)
    if not fx.check("command_mode", st is not None, f"boot={boot}"):
        return 1
    serial_ok = len(st["serial"]) == 24 and st["len"] == 12 and len(st["uid"]) == 16
    if args.expect_serial:
        serial_ok = serial_ok and st["serial"] == args.expect_serial.upper()
    fx.check("serial_number", serial_ok,
             f"serial={st['serial']} len={st['len']} uid={st['uid']}")
    if boot is not None:
        fx.check("boot_line_matches", boot["serial"] == st["serial"]
                 and boot["uid"] == st["uid"], f"boot={boot}")

    fx.check("build", True, f"rtos={st['rtos']} clock={st['clock']}")
    fx.echo("echo_3M", 65536)
    fx.echo("echo_3M_1MB", 1_000_000)
    fx.send_path("try_write_3M", "T", 200000, APP_BAUD)
    fx.send_path("blocking_write_3M", "W", 50000, APP_BAUD)
    fx.overrun()
    fx.framing_errors()
    fx.frame_formats()
    fx.rates()
    fx.loopback()
    fx.results["mask_outcomes"] = {"ok": True, "detail": " ".join(fx.mask_sweep())}
    if st["rtos"]:
        fx.duplex(1_000_000, 200_000)
        fx.duplex(APP_BAUD, 300_000, optional=True)
    fx.leak()
    fx.console()
    if not args.skip_reset:
        fx.reset()
        if not st["rtos"]:
            fx.stack_overflow()
    final = fx.status()
    fx.check("final_status", final is not None and final["ore"] == 0,
             f"status={final}")

    summary = {"pass": not fx.failures, "failures": fx.failures, "results": fx.results}
    if args.json:
        with open(args.json, "w", encoding="utf-8") as out:
            json.dump(summary, out, indent=2)
    print(json.dumps({"pass": summary["pass"], "failures": fx.failures}, indent=2))
    return 0 if summary["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
