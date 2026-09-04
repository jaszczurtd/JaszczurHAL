#!/usr/bin/env python3
"""Unit checks for the portable serial hardware-test adapter."""

from __future__ import annotations

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
