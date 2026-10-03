"""Fake serial port for host tests of bounded serial readers."""

from __future__ import annotations


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
