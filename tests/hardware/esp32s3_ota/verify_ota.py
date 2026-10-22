#!/usr/bin/env python3
"""Host side of the ESP32-S3 OTA hardware test.

The board runs image A from a serial flash. The verifier uploads raw ESP-IDF
application images over the OTA protocol with deliberate faults (wrong
password, replayed or foreign authentication, wrong MD5, an image that is not
an application, an oversized announcement, an interrupted transfer, an upload
during a trial), then moves the board to image B, proves rollback of an
unconfirmed trial, confirms B and finally returns to a confirmed image A.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import sys
import time

import serial

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT))
from vscode.runtime import jh_vscode as runtime  # noqa: E402

PREFIX = "JH_ESP32_OTA "
OTA_PORT = 8266
HOSTNAME = "jh-ota-esp32s3"
CHUNK = 1024
MODE_STABLE, MODE_TRIAL = 0, 2


class Board:
    """Debug serial port of the board, reopened across its restarts."""

    def __init__(self, path: str) -> None:
        self.path = path
        self.port: serial.Serial | None = None
        self.buffer = b""

    def _open(self) -> serial.Serial:
        if self.port is None:
            port = serial.Serial(port=None, baudrate=115200, timeout=0.1,
                                 write_timeout=2.0, exclusive=True)
            port.dtr = False
            port.rts = False
            port.port = self.path
            port.open()
            self.port = port
        return self.port

    def _drop(self) -> None:
        if self.port is not None:
            try:
                self.port.close()
            except serial.SerialException:
                pass
        self.port = None
        self.buffer = b""

    def request(self, command: bytes, key: str, timeout_s: float) -> dict[str, str]:
        """Send @p command until a line with @p key answers it."""
        deadline = time.monotonic() + timeout_s
        last_send = 0.0
        while time.monotonic() < deadline:
            try:
                port = self._open()
                if time.monotonic() - last_send > 1.0:
                    port.write(command)
                    last_send = time.monotonic()
                self.buffer += port.read(256)
            except (serial.SerialException, OSError):
                self._drop()
                time.sleep(0.5)
                continue
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                line = raw.decode("utf-8", errors="replace").strip()
                position = line.find(PREFIX)
                if position < 0:
                    continue
                fields = dict(token.partition("=")[::2]
                              for token in line[position + len(PREFIX):].split())
                if key in fields:
                    return fields
        raise TimeoutError(f"no {key} answer to {command!r}")

    def status(self, timeout_s: float = 60.0, **expected: str) -> dict[str, str]:
        """Status once the board runs, has joined and serves OTA as expected."""
        deadline = time.monotonic() + timeout_s
        status: dict[str, str] = {}
        while time.monotonic() < deadline:
            status = self.request(b"S", "image", max(1.0, deadline - time.monotonic()))
            if status.get("wifi") == "1" and status.get("ota") == "1" and all(
                    status.get(key) == value for key, value in expected.items()):
                return status
            time.sleep(1.0)
        raise TimeoutError(f"board status {status} never matched {expected}")

    def confirm(self) -> str:
        return self.request(b"C", "confirm", 10.0)["confirm"]

    def restart(self) -> None:
        self.request(b"R", "restarting", 10.0)
        self._drop()
        time.sleep(2.0)

    def close(self) -> None:
        self._drop()


def application_image(build_dir: Path) -> bytes:
    """The application image of a build, checked against its manifest."""
    manifest = json.loads((build_dir / "jh_esp_idf_artifacts.json").read_text())
    name = manifest["artifacts"]["applicationBinary"]
    record = next(item for item in manifest["flashImages"] if item["path"] == name)
    image = (build_dir / name).read_bytes()
    if len(image) != record["size"] or hashlib.sha256(image).hexdigest() != record["sha256"]:
        raise RuntimeError(f"{build_dir / name} differs from its manifest")
    if "HAL_ENABLE_OTA" not in manifest["integration"]["resolvedFeatures"]:
        raise RuntimeError(f"{build_dir} was not built with HAL_ENABLE_OTA")
    return image


class Session:
    """One OTA exchange with optional faults; returns how the device ended it."""

    def __init__(self, ip: str, password: str) -> None:
        self.ip = ip
        self.password = password
        self.last_auth: bytes | None = None
        self.last_nonce = ""
        self.invite_wait_s = 0.0

    def run(self, image: bytes, *, md5: str | None = None, size: int | None = None,
            password: str | None = None, replay_auth: bytes | None = None,
            foreign_auth: bool = False, stop_after: int | None = None,
            command: int = 0) -> str:
        size = len(image) if size is None else size
        md5 = hashlib.md5(image).hexdigest() if md5 is None else md5
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind(("", OTA_PORT))
            server.listen(1)
            server.settimeout(8.0)
            udp.bind(("", 0))
            udp.connect((self.ip, OTA_PORT))
            challenge = self._invite(udp, f"{command} {OTA_PORT} {size} {md5}\n".encode())
            udp.settimeout(3.0)
            match = re.fullmatch(r"AUTH2 ([0-9a-f]{32})", challenge)
            if match is None:
                return f"invitation:{challenge}"
            nonce = match.group(1)
            fresh = nonce != self.last_nonce
            self.last_nonce = nonce
            client_nonce = os.urandom(16).hex()
            tag = runtime.ota_auth2_tag(password or self.password, command, OTA_PORT,
                                        size, md5, nonce, client_nonce)
            auth = replay_auth or f"201 {client_nonce} {tag}\n".encode()
            self.last_auth = auth
            if foreign_auth:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as other:
                    other.settimeout(3.0)
                    other.connect((self.ip, OTA_PORT))
                    other.send(auth)
                    try:
                        return f"foreign-answered:{other.recv(256)!r}"
                    except socket.timeout:
                        return "foreign-ignored"
            udp.send(auth)
            try:
                answer = runtime.ota_control_line(udp.recv(256))
            except socket.timeout:
                return "auth-timeout"
            if answer != "OK":
                return f"auth:{answer}" + ("" if fresh else ":stale-nonce")
            try:
                connection, _ = server.accept()
            except socket.timeout:
                return "no-connect"
            with connection:
                return self._transfer(connection, image, stop_after)

    def _invite(self, udp: socket.socket, invitation: bytes) -> str:
        """Send the invitation until the device answers; a device still busy
        with an earlier transfer ignores it. Records how long that took."""
        start = time.monotonic()
        udp.settimeout(1.0)
        while time.monotonic() - start < 15.0:
            udp.send(invitation)
            try:
                answer = runtime.ota_control_line(udp.recv(256))
            except socket.timeout:
                continue
            self.invite_wait_s = time.monotonic() - start
            return answer
        raise TimeoutError("the device never answered the OTA invitation")

    @staticmethod
    def _half_close(connection: socket.socket) -> str:
        """End the upload with a FIN and time how long the device takes to
        give up the transfer and close its side."""
        start = time.monotonic()
        connection.shutdown(socket.SHUT_WR)
        connection.settimeout(0.2)
        while time.monotonic() - start < 10.0:
            try:
                if not connection.recv(64):
                    break
            except socket.timeout:
                continue
            except OSError:
                break
        return f"interrupted:{time.monotonic() - start:.2f}"

    @staticmethod
    def _transfer(connection: socket.socket, image: bytes, stop_after: int | None) -> str:
        connection.settimeout(8.0)
        reader = connection.makefile("rb")
        sent = 0
        try:
            while sent < len(image):
                if stop_after is not None and sent >= stop_after:
                    return Session._half_close(connection)
                chunk = image[sent:sent + CHUNK]
                connection.sendall(chunk)
                acknowledged = 0
                while acknowledged < len(chunk):
                    line = reader.readline(32)
                    if not line:
                        return f"closed-at:{sent}"
                    acknowledged += runtime.ota_acknowledged_bytes(
                        line, len(chunk) - acknowledged)
                sent += len(chunk)
            final = reader.read(3)
        except (OSError, RuntimeError) as error:
            return f"transfer-error:{error}"
        return "accepted" if final == b"OK" else f"final:{final!r}"


def ota_password(header: Path) -> str:
    match = re.search(r'#define\s+JH_OTA_TEST_PASSWORD\s+"([^"]+)"',
                      header.read_text(encoding="utf-8"))
    if match is None:
        raise RuntimeError(f"{header} defines no JH_OTA_TEST_PASSWORD")
    return match.group(1)


def run(args: argparse.Namespace) -> dict[str, object]:
    image_a = application_image(args.image_a)
    image_b = application_image(args.image_b)
    password = ota_password(HERE / "ota_test_config.h")
    board = Board(args.port)
    results: dict[str, object] = {}

    def check(name: str, passed: bool, detail: object) -> None:
        results[name] = {"pass": passed, "detail": detail}
        print(json.dumps({name: results[name]}), flush=True)

    try:
        status = board.status(image="A")
        ip = status["ip"]
        check("start_image_a", status["mode"] == str(MODE_STABLE), status)
        devices = [device for device in runtime.discover_ota_devices(
            OTA_PORT, ip, timeout_s=2.0) if device["hostname"] == HOSTNAME]
        check("discovery", len(devices) == 1 and devices[0]["bootMode"] == MODE_STABLE
              and devices[0]["slotSize"] >= len(image_b), devices)
        slot_size = devices[0]["slotSize"] if devices else 0
        session = Session(ip, password)

        outcome = session.run(image_b, password=f"wrong-{password}")
        check("wrong_password", outcome.startswith("auth:"), outcome)
        # A completed authentication cannot be replayed against a new nonce.
        # The host ending the upload stream ends the transfer at once, not
        # after the receive timeout.
        outcome = session.run(image_b, stop_after=0)
        captured = session.last_auth
        check("abort_closes_at_once", outcome.startswith("interrupted:") and
              float(outcome.split(":")[1]) < 2.0, outcome)
        outcome = session.run(image_b, replay_auth=captured)
        check("replayed_auth", outcome.startswith("auth:"), outcome)
        outcome = session.run(image_b, foreign_auth=True)
        check("foreign_source_auth", outcome == "foreign-ignored", outcome)
        time.sleep(6.0)  # the pending invitation expires
        # The pre-AUTH2 answer format is refused even with valid digests.
        outcome = session.run(image_b, replay_auth=f"200 {os.urandom(16).hex()} "
                              f"{hashlib.md5(b'jh').hexdigest()}\n".encode())
        check("auth_downgrade", outcome.startswith("auth:"), outcome)
        # A filesystem image (command 100) is not supported on ESP32-S3.
        outcome = session.run(image_b, command=100)
        check("filesystem_command", outcome != "accepted", outcome)
        outcome = session.run(image_b, md5="0" * 32)
        check("md5_mismatch", outcome.startswith("final:") or
              outcome.startswith("closed-at"), outcome)
        junk = os.urandom(64 * 1024)
        outcome = session.run(junk)
        check("not_an_application", outcome != "accepted", outcome)
        outcome = session.run(image_b, size=slot_size + 1,
                              md5=hashlib.md5(image_b).hexdigest())
        check("oversized", outcome != "accepted" and not outcome.startswith("auth:OK"),
              outcome)
        outcome = session.run(image_b, stop_after=len(image_b) // 2)
        check("interrupted", outcome.startswith("interrupted:") and
              float(outcome.split(":")[1]) < 2.0, outcome)
        status = board.status(image="A")
        check("faults_keep_image_a", status["mode"] == str(MODE_STABLE), status)

        # Trial of B, refused upload during the trial, rollback without confirm.
        outcome = session.run(image_b)
        check("upload_b", outcome == "accepted", outcome)
        status = board.status(image="B")
        check("trial_b", status["mode"] == str(MODE_TRIAL), status)
        outcome = session.run(image_a)
        check("trial_refuses_upload", outcome != "accepted", outcome)
        board.restart()
        status = board.status(image="A")
        check("rollback_to_a", status["mode"] == str(MODE_STABLE), status)

        # Confirmed B survives a restart.
        outcome = session.run(image_b)
        check("upload_b_again", outcome == "accepted", outcome)
        board.status(image="B")
        confirm = board.confirm()
        status = board.status(image="B")
        check("confirm_b", confirm == "HAL_OK" and status["mode"] == str(MODE_STABLE),
              {"confirm": confirm, "status": status})
        board.restart()
        status = board.status(image="B")
        check("confirmed_b_persists", status["mode"] == str(MODE_STABLE), status)

        # Back to a confirmed image A.
        outcome = session.run(image_a)
        board.status(image="A")
        confirm = board.confirm()
        status = board.status(image="A")
        check("return_to_a", outcome == "accepted" and confirm == "HAL_OK" and
              status["mode"] == str(MODE_STABLE), {"outcome": outcome, "status": status})
    finally:
        board.close()
    failed = sorted(name for name, result in results.items() if not result["pass"])
    if failed:
        raise RuntimeError(f"OTA test failed: {failed}")
    return {"passed": len(results), "failed": failed}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", required=True)
    parser.add_argument("--image-a", type=Path,
                        default=HERE / ".build/esp-idf/image-a")
    parser.add_argument("--image-b", type=Path,
                        default=HERE / ".build/esp-idf/image-b")
    args = parser.parse_args()
    print(json.dumps(run(args), sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
