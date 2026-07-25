#!/usr/bin/env python3
"""Exercise native RP OTA trial, confirmation, and automatic rollback."""

from __future__ import annotations

import argparse
import ast
import importlib.util
import json
import os
from pathlib import Path
import re
import sys
import termios
import time

import serial


ROOT = Path(__file__).resolve().parents[3]
RUNTIME_PATH = ROOT / "vscode" / "linux" / "runtime" / "jh_vscode.py"
ARTIFACT_SCRIPT_PATH = ROOT / "scripts" / "rp_ota_artifacts.py"
HAL_OK = 1
HAL_ENOENT = -6
BOOT_STABLE = 0
BOOT_TRIAL = 2
LOCAL_SECRETS = Path(__file__).with_name("ota_test_secrets.h")
BOARD_PROFILES = {
    ("rp2040", "picow"): "pico-w",
    ("rp2040", "pico-rm2"): "pico-pim730",
    ("rp2350-arm", "pico2w"): "pico-2-w",
}

STATUS_PATTERN = re.compile(rb"^JHOTA-HW1 (.+)\n$")
CONFIRM_PATTERN = re.compile(rb"^JHOTA-CONFIRM status=(-?\d+)\n$")


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def open_port(path: str) -> serial.Serial:
    port = serial.Serial(
        path, baudrate=115200, timeout=0.1, write_timeout=5.0, exclusive=True
    )
    port.dtr = True
    time.sleep(0.1)
    return port


def read_line(port: serial.Serial, timeout_s: float) -> bytes:
    deadline = time.monotonic() + timeout_s
    received = bytearray()
    while not received.endswith(b"\n"):
        chunk = port.read(1)
        if chunk:
            received.extend(chunk)
        elif time.monotonic() >= deadline:
            raise TimeoutError(f"incomplete response: {received!r}")
    return bytes(received)


def command(path: str, value: bytes, timeout_s: float) -> bytes:
    with open_port(path) as port:
        port.reset_input_buffer()
        port.write(value)
        port.flush()
        return read_line(port, timeout_s)


def parse_status(line: bytes) -> dict[str, int | str]:
    match = STATUS_PATTERN.fullmatch(line)
    if match is None:
        raise RuntimeError(f"invalid status response: {line!r}")
    result: dict[str, int | str] = {}
    for field in match.group(1).decode("ascii").split():
        key, value = field.split("=", 1)
        if key in {
            "target",
            "board",
            "runtime",
            "ipv4",
            "program_version",
            "staging_version",
        }:
            result[key] = value
        else:
            result[key] = int(value)
    return result


def query_status(path: str, timeout_s: float) -> dict[str, int | str]:
    return parse_status(command(path, b"S", timeout_s))


def wait_for_status(path: str, timeout_s: float, predicate) -> dict[str, int | str]:
    deadline = time.monotonic() + timeout_s
    last_error: Exception | None = None
    last_status: dict[str, int | str] | None = None
    while time.monotonic() < deadline:
        try:
            last_status = query_status(path, min(3.0, timeout_s))
            if predicate(last_status):
                return last_status
        except (
            OSError,
            RuntimeError,
            TimeoutError,
            serial.SerialException,
            termios.error,
        ) as exc:
            last_error = exc
        time.sleep(0.25)
    raise TimeoutError(
        f"device did not reach expected state; last={last_status}, error={last_error}"
    )


def confirm(path: str, timeout_s: float) -> None:
    line = command(path, b"C", timeout_s)
    match = CONFIRM_PATTERN.fullmatch(line)
    if match is None or int(match.group(1)) != HAL_OK:
        raise RuntimeError(f"trial confirmation failed: {line!r}")


def reboot(path: str) -> None:
    try:
        with open_port(path) as port:
            port.write(b"R")
            port.flush()
    except (OSError, serial.SerialException, termios.error):
        pass


def wait_for_device(
    runtime,
    target: str,
    hostname: str,
    port: int,
    broadcast: str,
    timeout_s: float,
):
    deadline = time.monotonic() + timeout_s
    last_matches = []
    while time.monotonic() < deadline:
        devices = runtime.discover_ota_devices(
            port, broadcast, timeout_s=min(1.0, timeout_s)
        )
        last_matches = [
            device
            for device in devices
            if device["target"] == target and device["hostname"] == hostname
        ]
        if len(last_matches) == 1:
            return last_matches[0]
        time.sleep(0.25)
    raise RuntimeError(
        f"expected one {target}/{hostname} OTA device, found {last_matches}"
    )


def state_identity(status: dict[str, int | str]) -> dict[str, int | str]:
    return {
        key: status[key]
        for key in (
            "state",
            "mode",
            "attempts",
            "max",
            "program_generation",
            "staging_generation",
            "program_version",
            "staging_version",
        )
    }


def gspi_clock(status: dict[str, int | str]) -> dict[str, int | str]:
    return {
        "clkSysHz": status["clk_sys"],
        "targetHz": status["gspi_target"],
        "actualHz": status["gspi_actual"],
        "dividerInt": status["gspi_div_int"],
        "dividerFrac8": status["gspi_div_frac8"],
        "program": status["gspi_program"],
    }


def ota_password(env_name: str) -> str:
    value = os.environ.get(env_name)
    if value:
        return value
    if LOCAL_SECRETS.is_file():
        match = re.search(
            r'^#define\s+JH_OTA_TEST_PASSWORD\s+(".+")\s*$',
            LOCAL_SECRETS.read_text(encoding="utf-8"),
            re.MULTILINE,
        )
        if match is not None:
            parsed = ast.literal_eval(match.group(1))
            if isinstance(parsed, str) and parsed:
                return parsed
    raise RuntimeError(
        f"OTA password is absent from {env_name} and {LOCAL_SECRETS}"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--target", choices=("rp2040", "rp2350-arm"), required=True)
    parser.add_argument(
        "--board", choices=("picow", "pico2w", "pico-rm2"), required=True
    )
    parser.add_argument(
        "--runtime", choices=("baremetal", "freertos"), required=True
    )
    parser.add_argument(
        "--artifact-dir", type=Path, default=ROOT / ".build" / "hardware" / "rp_ota"
    )
    parser.add_argument("--password-env", default="JH_OTA_TEST_PASSWORD")
    parser.add_argument("--ota-port", type=int, default=8266)
    parser.add_argument("--broadcast", default="255.255.255.255")
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument(
        "--status-only",
        action="store_true",
        help="validate fixture identity and gSPI telemetry without performing OTA",
    )
    parser.add_argument("--expected-gspi-hz", type=int, default=31_250_000)
    parser.add_argument(
        "--expected-gspi-program", type=int, choices=(0, 1), default=0
    )
    args = parser.parse_args()
    try:
        expected_board_profile = BOARD_PROFILES[(args.target, args.board)]
    except KeyError:
        parser.error(
            f"board {args.board!r} is incompatible with target {args.target!r}"
        )

    expected_hostname = (
        "jh-ota-rp2040" if args.target == "rp2040" else "jh-ota-rp2350-arm"
    )
    ready = wait_for_status(
        args.port,
        args.timeout,
        lambda status: status["config"] == 1
        and status["wifi"] == 1
        and status["ota"] == 1,
    )
    if (
        ready["target"] != args.target
        or ready["board"] != expected_board_profile
        or ready["runtime"] != args.runtime
    ):
        raise RuntimeError(f"unexpected fixture identity: {ready}")
    if (
        ready["gspi_status"] != HAL_OK
        or ready["gspi_target"] != args.expected_gspi_hz
        or ready["gspi_actual"] > ready["gspi_target"]
        or ready["gspi_actual"] < ready["gspi_target"] * 99 // 100
        or ready["gspi_program"] != args.expected_gspi_program
    ):
        raise RuntimeError(f"unexpected automatic gSPI clock: {ready}")
    if args.status_only:
        print(
            json.dumps(
                {
                    "port": args.port,
                    "target": args.target,
                    "board": args.board,
                    "boardProfile": expected_board_profile,
                    "runtime": args.runtime,
                    "ipv4": ready["ipv4"],
                    "gspiClock": gspi_clock(ready),
                    "status": "pass",
                },
                indent=2,
            )
        )
        return 0

    password = ota_password(args.password_env)
    runtime = load_module("jh_vscode_runtime", RUNTIME_PATH)
    artifacts = load_module("rp_ota_artifacts", ARTIFACT_SCRIPT_PATH)
    binary = args.artifact_dir / "firmware.bin"
    template = args.artifact_dir / "firmware.ota"
    if not binary.is_file() or not template.is_file():
        raise FileNotFoundError(
            f"missing OTA build artifacts in {args.artifact_dir}"
        )

    template_bytes = template.read_bytes()
    program_offset = int.from_bytes(template_bytes[16:20], "little")
    if ready["state"] == HAL_OK and ready["mode"] == BOOT_TRIAL:
        confirm(args.port, args.timeout)
        ready = wait_for_status(
            args.port,
            args.timeout,
            lambda status: status["state"] == HAL_OK
            and status["mode"] == BOOT_STABLE,
        )
    elif ready["state"] not in (HAL_OK, HAL_ENOENT):
        raise RuntimeError(f"fixture starts from an invalid OTA state: {ready}")

    generation_a = max(
        1001,
        int(ready["program_generation"]) + 1,
        int(ready["staging_generation"]) + 1,
    )
    generation_b = generation_a + 1
    image_a = args.artifact_dir / "hardware-A.ota"
    image_b = args.artifact_dir / "hardware-B.ota"
    signed_a = args.artifact_dir / "hardware-A.signed.ota"
    signed_b = args.artifact_dir / "hardware-B.signed.ota"
    artifacts.package_ota(
        binary, image_a, args.target, program_offset, generation_a, "hw-A"
    )
    artifacts.package_ota(
        binary, image_b, args.target, program_offset, generation_b, "hw-B"
    )
    container_a = runtime.sign_ota_container(image_a, password, signed_a)
    container_b = runtime.sign_ota_container(image_b, password, signed_b)

    device = wait_for_device(
        runtime,
        args.target,
        expected_hostname,
        args.ota_port,
        args.broadcast,
        args.timeout,
    )
    before_wrong_password = state_identity(ready)
    wrong_password_rejected = False
    try:
        runtime.upload_ota_container(device, container_a, f"wrong-{password}")
    except (OSError, RuntimeError, TimeoutError):
        wrong_password_rejected = True
    if not wrong_password_rejected:
        raise RuntimeError("device accepted an OTA invitation with a wrong password")
    after_wrong_password = query_status(args.port, args.timeout)
    if state_identity(after_wrong_password) != before_wrong_password:
        raise RuntimeError(
            "wrong-password attempt changed OTA state: "
            f"before={before_wrong_password}, after={after_wrong_password}"
        )

    device = wait_for_device(
        runtime,
        args.target,
        expected_hostname,
        args.ota_port,
        args.broadcast,
        args.timeout,
    )
    runtime.upload_ota_container(device, container_a, password)
    trial_a = wait_for_status(
        args.port,
        args.timeout,
        lambda status: status["state"] == HAL_OK
        and status["mode"] == BOOT_TRIAL
        and status["program_generation"] == generation_a
        and status["program_version"] == "hw-A",
    )
    confirm(args.port, args.timeout)
    stable_a = wait_for_status(
        args.port,
        args.timeout,
        lambda status: status["mode"] == BOOT_STABLE
        and status["program_generation"] == generation_a
        and status["program_version"] == "hw-A",
    )

    device = wait_for_device(
        runtime,
        args.target,
        expected_hostname,
        args.ota_port,
        args.broadcast,
        args.timeout,
    )
    runtime.upload_ota_container(device, container_b, password)
    trial_b = wait_for_status(
        args.port,
        args.timeout,
        lambda status: status["state"] == HAL_OK
        and status["mode"] == BOOT_TRIAL
        and status["program_generation"] == generation_b
        and status["program_version"] == "hw-B",
    )

    rollback_boots = []
    for _ in range(int(trial_b["max"]) + 2):
        reboot(args.port)
        status = wait_for_status(
            args.port,
            args.timeout,
            lambda value: value["state"] == HAL_OK,
        )
        rollback_boots.append(state_identity(status))
        if status["mode"] == BOOT_STABLE:
            break
    rolled_back = rollback_boots[-1]
    if (
        rolled_back["mode"] != BOOT_STABLE
        or rolled_back["program_generation"] != generation_a
        or rolled_back["program_version"] != "hw-A"
        or rolled_back["staging_generation"] != generation_b
        or rolled_back["staging_version"] != "hw-B"
    ):
        raise RuntimeError(f"automatic rollback failed: {rolled_back}")

    recovered_device = wait_for_device(
        runtime,
        args.target,
        expected_hostname,
        args.ota_port,
        args.broadcast,
        args.timeout,
    )
    print(
        json.dumps(
            {
                "port": args.port,
                "target": args.target,
                "board": args.board,
                "boardProfile": expected_board_profile,
                "runtime": args.runtime,
                "gspiClock": gspi_clock(ready),
                "device": recovered_device,
                "wrongPassword": {
                    "rejected": wrong_password_rejected,
                    "stateUnchanged": True,
                },
                "trialA": state_identity(trial_a),
                "stableA": state_identity(stable_a),
                "trialB": state_identity(trial_b),
                "rollbackBoots": rollback_boots,
                "status": "pass",
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
