#!/usr/bin/env python3
"""Unit checks for the portable serial hardware-test adapter."""

from __future__ import annotations

import importlib.util
from pathlib import Path
from types import SimpleNamespace
import sys
import unittest
from unittest import mock


ROOT = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(ROOT))

from vscode.runtime import serial_io


class FakeSerialException(Exception):
    pass


# Verifiers that read request/response lines through the shared reader.
LINE_VERIFIERS = (
    "rp_flash_transaction/verify_flash_transaction.py",
    "rp_freertos_smp/verify_freertos_smp.py",
    "rp_kv_power_loss/verify_kv_power_loss.py",
    "rp_ota/verify_ota.py",
    "rp_storage/verify_storage.py",
    "rp_usb_multicore/verify_usb_multicore.py",
)


class ScriptedPort:
    """Serial port whose every read(1) costs `step` seconds of fake time."""

    def __init__(self, data: bytes, step: float, repeat: bool = False) -> None:
        self.data = data
        self.step = step
        self.repeat = repeat
        self.position = 0
        self.reads = 0
        self.now = 0.0

    def clock(self) -> float:
        return self.now

    def read(self, size: int) -> bytes:
        assert size == 1
        if self.reads >= 10000:
            raise AssertionError("reader kept reading past its deadline")
        self.reads += 1
        self.now += self.step
        if self.position >= len(self.data):
            if not self.repeat:
                return b""
            self.position = 0
        byte = self.data[self.position : self.position + 1]
        self.position += 1
        return byte


def load_verifier(relative: str):
    path = ROOT / "tests" / "hardware" / relative
    spec = importlib.util.spec_from_file_location(path.stem, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ReadLineTests(unittest.TestCase):
    def test_complete_line_is_returned(self) -> None:
        port = ScriptedPort(b"JHKV3 ok\n", step=0.01)
        line = serial_io.read_line(port, 1.0, clock=port.clock)
        self.assertEqual(b"JHKV3 ok\n", line)

    def test_deadline_holds_while_bytes_keep_arriving(self) -> None:
        # A device streaming without a newline must not extend the wait.
        port = ScriptedPort(b"x", step=0.05, repeat=True)
        with self.assertRaises(TimeoutError):
            serial_io.read_line(port, 0.2, clock=port.clock)
        self.assertLess(port.now, 0.3)

    def test_deadline_holds_when_the_port_is_silent(self) -> None:
        port = ScriptedPort(b"", step=0.1)
        with self.assertRaises(TimeoutError):
            serial_io.read_line(port, 0.5, clock=port.clock)
        self.assertLess(port.now, 0.7)

    def test_line_without_newline_is_bounded(self) -> None:
        port = ScriptedPort(b"y", step=0.0, repeat=True)
        with self.assertRaisesRegex(ValueError, "exceeds 64 bytes"):
            serial_io.read_line(port, 1.0, limit=64, clock=port.clock)
        self.assertEqual(64, port.reads)

    def test_hardware_verifiers_use_the_bounded_reader(self) -> None:
        for relative in LINE_VERIFIERS:
            with self.subTest(verifier=relative):
                module = load_verifier(relative)
                port = ScriptedPort(b"x", step=0.05, repeat=True)
                with mock.patch.object(serial_io.time, "monotonic", port.clock):
                    with self.assertRaises(TimeoutError):
                        module.read_line(port, 0.2)
                self.assertLess(port.now, 0.3)


class SerialIoTests(unittest.TestCase):
    def test_open_passes_portable_timeouts_and_exclusive_ownership(self) -> None:
        factory = mock.Mock(return_value=object())
        fake_module = SimpleNamespace(
            Serial=factory,
            SerialException=FakeSerialException,
        )
        with mock.patch.object(serial_io, "_serial", fake_module):
            opened = serial_io.open_serial_port("COM17")
        self.assertIs(opened, factory.return_value)
        factory.assert_called_once_with(
            "COM17",
            baudrate=115200,
            timeout=0.1,
            write_timeout=5.0,
            exclusive=True,
        )

    def test_error_tuple_is_host_independent(self) -> None:
        fake_module = SimpleNamespace(SerialException=FakeSerialException)
        with mock.patch.object(serial_io, "_serial", fake_module):
            errors = serial_io.serial_error_types()
        self.assertIn(OSError, errors)
        self.assertIn(FakeSerialException, errors)
        if serial_io._termios is not None:
            self.assertIn(serial_io._termios.error, errors)

    def test_missing_pyserial_has_clear_error(self) -> None:
        with mock.patch.object(serial_io, "_serial", None):
            with self.assertRaisesRegex(RuntimeError, "pyserial"):
                serial_io.open_serial_port("COM17")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
