#!/usr/bin/env python3
"""Persistent serial monitor for JaszczurHAL VS Code entry."""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path

from vscode.runtime.exit_codes import EXIT_MONITOR, EXIT_UNSUPPORTED
from vscode.runtime.platform_api import (
    PlatformOperationUnsupported,
    get_platform_adapter,
)

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # kept importable so hosts without pyserial still load the core
    serial = None
    list_ports = None


class MonitorDependencyMissing(RuntimeError):
    """Raised when the host lacks pyserial required by the monitor."""


def require_pyserial() -> None:
    if serial is None or list_ports is None:
        raise MonitorDependencyMissing(
            "pyserial is not installed. "
            "Install it with: pip install pyserial --break-system-packages"
        )


CYAN = "\033[0;36m"
GREEN = "\033[0;32m"
YELLOW = "\033[1;33m"
RED = "\033[0;31m"
DIM = "\033[2m"
BOLD = "\033[1m"
NC = "\033[0m"

PICO_USB_IDS = {"2e8a:000a", "2e8a:f00a", "2e8a:000f", "2e8a:f00f", "2e8a:0003", "2e8a:1020", "2e8a:103a"}
DEBUG_PROBE_IDS = {"2e8a:000c", "2e8a:0004"}
RELEASE_FOR_UPLOAD = False


def usb_id_str(vid: int | None, pid: int | None) -> str | None:
    if vid is None or pid is None:
        return None
    return f"{vid:04x}:{pid:04x}"


def is_serial_candidate(device: str) -> bool:
    return get_platform_adapter().is_serial_candidate(device)


def serial_port_exists(port: str) -> bool:
    return get_platform_adapter().serial_port_exists(port)


def list_serial_ports():
    ports = [port for port in list_ports.comports() if is_serial_candidate(port.device)]
    ports.sort(key=lambda port: port.device)
    return ports


def classify_port(port_info) -> str:
    uid = usb_id_str(port_info.vid, port_info.pid)
    if uid in DEBUG_PROBE_IDS:
        return "probe"
    if uid in PICO_USB_IDS or (uid is not None and uid.startswith("2e8a:")):
        return "pico"
    return "other"


def port_matches_mode(port_info, mode: str) -> bool:
    if mode == "any":
        return True
    return classify_port(port_info) == mode


def format_port_info(port_info) -> str:
    uid = usb_id_str(port_info.vid, port_info.pid) or "?:????"
    desc = port_info.description or "no-description"
    return f"{port_info.device}[{classify_port(port_info)}|{uid}|{desc}]"


def normalize_identity_text(value: str) -> str:
    return "".join(char.lower() for char in value if char.isalnum())


def port_matches_identity(port_info, identity_tokens: list[str]) -> bool:
    fields = (
        getattr(port_info, "description", ""),
        getattr(port_info, "manufacturer", ""),
        getattr(port_info, "product", ""),
        getattr(port_info, "interface", ""),
        getattr(port_info, "hwid", ""),
    )
    identity = normalize_identity_text(" ".join(value or "" for value in fields))
    return bool(
        identity
        and any(token in identity for token in identity_tokens if token)
    )


def matching_identity_ports(mode: str, identity_tokens: list[str]):
    return [
        port
        for port in list_serial_ports()
        if port_matches_mode(port, mode)
        and port_matches_identity(port, identity_tokens)
    ]


def load_settings(project_dir: Path | None) -> dict:
    if project_dir is None:
        return {}
    settings_path = project_dir / ".vscode" / "settings.json"
    if not settings_path.is_file():
        return {}
    try:
        return json.loads(settings_path.read_text(encoding="utf-8"))
    except Exception:
        return {}


def get_preferred_port(cli_port: str, project_dir: Path | None) -> str:
    if cli_port:
        return cli_port
    settings = load_settings(project_dir)
    for port in (
        settings.get("jaszczurhal.uploadPort", ""),
        settings.get("persistentSerialMonitor.port", ""),
        settings.get("serial.port", ""),
    ):
        if isinstance(port, str) and port.strip():
            return port.strip()
    return ""


def upload_marker(project_dir: Path | None) -> Path | None:
    if project_dir is None:
        return None
    return project_dir / ".vscode" / ".jh-upload-in-progress"


def wait_for_upload_marker(project_dir: Path | None) -> None:
    marker = upload_marker(project_dir)
    if marker is None:
        return
    while marker.exists():
        time.sleep(0.5)


def find_port(
    mode: str,
    preferred_port: str = "",
    identity_tokens: list[str] | None = None,
    follow_identity: bool = False,
) -> tuple[str | None, str]:
    if preferred_port:
        if serial_port_exists(preferred_port):
            return preferred_port, f"preferred:{preferred_port}"
        if follow_identity and identity_tokens:
            matches = matching_identity_ports(mode, identity_tokens)
            if len(matches) == 1:
                match = matches[0]
                return match.device, f"verified-identity:{format_port_info(match)}"
            if len(matches) > 1:
                return None, "identity-ambiguous"
        return None, f"preferred-missing:{preferred_port}"
    for port in list_serial_ports():
        if port_matches_mode(port, mode):
            return port.device, format_port_info(port)
    return None, "not-found"


def wait_for_device(
    mode: str,
    preferred_port: str = "",
    identity_tokens: list[str] | None = None,
    follow_identity: bool = False,
) -> str:
    spinner = ["|", "/", "-", "\\"]
    i = 0
    while True:
        port, reason = find_port(
            mode,
            preferred_port,
            identity_tokens,
            follow_identity,
        )
        if port:
            print(f"\r{' ' * 140}\r", end="", flush=True)
            time.sleep(0.3)
            if serial_port_exists(port):
                print(f"{GREEN}Found port: {port} [{reason}]{NC}")
                return port

        ports = list_serial_ports()
        if ports:
            suffix = " (" + ", ".join(format_port_info(port) for port in ports) + ")"
        else:
            fallback = get_platform_adapter().serial_fallback_candidates()
            suffix = f" ({', '.join(fallback)})" if fallback else ""
        print(f"\r{YELLOW}Waiting for device [{mode}]... {DIM}{spinner[i % len(spinner)]}{suffix}{NC}   ", end="", flush=True)
        i += 1
        time.sleep(0.5)


def clear_hupcl(fd: int) -> None:
    get_platform_adapter().configure_serial_fd(fd)


def process_cmdline(pid: int) -> str:
    return get_platform_adapter().process_cmdline(pid)


def port_owner_pids(port: str) -> list[int]:
    return get_platform_adapter().port_owner_pids(port)


def format_port_owners(port: str) -> list[str]:
    lines = []
    for pid in port_owner_pids(port):
        if pid != os.getpid():
            lines.append(f"  PID {pid}: {process_cmdline(pid) or '?'}")
    return lines


def owns_monitor(pid: int, project_dir: Path | None) -> bool:
    cmdline = process_cmdline(pid)
    if "serial_persistent.py" not in cmdline and "serial-persistent.py" not in cmdline:
        return False
    return project_dir is None or str(project_dir) in cmdline


def stop_pids(pids: list[int], label: str) -> None:
    for pid in pids:
        try:
            get_platform_adapter().terminate_process(pid)
            print(f"{YELLOW}Stopped {label} PID {pid}{NC}")
        except ProcessLookupError:
            pass
        except PermissionError:
            print(f"{YELLOW}Cannot stop PID {pid}: permission denied{NC}")


def apply_lock_policy(port: str, policy: str, project_dir: Path | None) -> bool:
    owners = [pid for pid in port_owner_pids(port) if pid != os.getpid()]
    if not owners:
        return True
    if policy == "wait":
        print(f"{YELLOW}Port {port} is busy; waiting. Owners:{NC}")
        print("\n".join(format_port_owners(port)))
        return False
    if policy == "replace-own":
        targets = [pid for pid in owners if owns_monitor(pid, project_dir)]
        if not targets:
            print(f"{YELLOW}Port {port} is busy and no own monitor can be replaced. Owners:{NC}")
            print("\n".join(format_port_owners(port)))
            return False
        stop_pids(targets, "own monitor")
    elif policy == "replace-any":
        stop_pids(owners, "port owner")
    else:
        return False

    deadline = time.time() + 2.0
    while time.time() < deadline:
        if not [pid for pid in port_owner_pids(port) if pid != os.getpid()]:
            return True
        time.sleep(0.1)
    return False


def is_lock_error(exc: Exception) -> bool:
    text = str(exc).lower()
    return "could not exclusively lock port" in text or "resource temporarily unavailable" in text or "errno 11" in text


def open_serial(port: str, baud: int) -> serial.Serial:
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = 0.5
    ser.write_timeout = 0.5
    ser.xonxoff = False
    ser.dsrdtr = False
    ser.rtscts = False
    try:
        ser.exclusive = True
    except Exception:
        pass
    ser.open()
    clear_hupcl(ser.fd)
    try:
        ser.setRTS(False)
        ser.setDTR(True)
    except Exception:
        pass
    time.sleep(0.2)
    try:
        ser.reset_input_buffer()
    except Exception:
        pass
    return ser


def request_release_for_upload(_signum, _frame) -> None:
    global RELEASE_FOR_UPLOAD
    RELEASE_FOR_UPLOAD = True


def monitor(port: str, baud: int, lock_policy: str, project_dir: Path | None) -> str:
    global RELEASE_FOR_UPLOAD
    RELEASE_FOR_UPLOAD = False
    while True:
        if not apply_lock_policy(port, lock_policy, project_dir):
            time.sleep(1.0)
            continue
        try:
            ser = open_serial(port, baud)
            break
        except serial.SerialException as exc:
            if is_lock_error(exc):
                print(f"{YELLOW}Cannot lock {port}: {exc}{NC}")
                owners = format_port_owners(port)
                if owners:
                    print(f"{YELLOW}Port owners:{NC}")
                    print("\n".join(owners))
                time.sleep(1.0)
                continue
            print(f"{RED}Cannot open {port}: {exc}{NC}")
            return "error"

    print(f"{GREEN}Connected to {port} @ {baud}{NC}")
    print(f"{DIM}{'-' * 80}{NC}")
    released_for_upload = False
    try:
        while True:
            if RELEASE_FOR_UPLOAD:
                released_for_upload = True
                break
            try:
                raw = ser.readline()
            except (serial.SerialException, OSError):
                break
            if RELEASE_FOR_UPLOAD:
                released_for_upload = True
                break
            if raw:
                text = raw.replace(b"\r\n", b"\n").replace(b"\r", b"\n").decode("utf-8", errors="replace").rstrip("\n")
                if text:
                    print(text)
    except KeyboardInterrupt:
        try:
            ser.close()
        except Exception:
            pass
        return "quit"
    try:
        ser.close()
    except Exception:
        pass
    if released_for_upload or RELEASE_FOR_UPLOAD:
        return "released-for-upload"
    return "disconnected"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Persistent serial monitor.")
    parser.add_argument("port", nargs="?", default="", help="Explicit serial port.")
    parser.add_argument("--project", default="", help="Firmware module directory.")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="Baud rate.")
    parser.add_argument("-m", "--mode", choices=["pico", "probe", "any"], default="pico")
    parser.add_argument("--lock-policy", choices=["wait", "replace-own", "replace-any"], default="wait")
    parser.add_argument(
        "--follow-identity",
        action="store_true",
        help="Follow one serial device matching the supplied project identity.",
    )
    parser.add_argument(
        "--identity-token",
        action="append",
        default=[],
        help="Normalized USB identity token accepted by --follow-identity.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    project_dir = Path(args.project).resolve() if args.project else None
    preferred = get_preferred_port(args.port, project_dir)
    identity_tokens = [
        normalize_identity_text(token) for token in args.identity_token if token
    ]
    follow_identity = args.follow_identity and bool(identity_tokens)

    print()
    print(f"{BOLD}{CYAN}Persistent Serial Monitor{NC}")
    print(f"  Baud:        {GREEN}{args.baud}{NC}")
    print(f"  Mode:        {GREEN}{args.mode}{NC}")
    print(f"  Port:        {GREEN}{preferred if preferred else 'auto'}{NC}")
    if follow_identity:
        print(f"  Follow USB:  {GREEN}verified project identity{NC}")
    print(f"  Lock policy: {GREEN}{args.lock_policy}{NC}")
    if project_dir:
        print(f"  Project:     {GREEN}{project_dir}{NC}")
    print(f"  {YELLOW}Ctrl+C{NC} to stop")
    print()

    while True:
        port, _ = find_port(
            args.mode,
            preferred,
            identity_tokens,
            follow_identity,
        )
        if not port:
            port = wait_for_device(
                args.mode,
                preferred,
                identity_tokens,
                follow_identity,
            )
        result = monitor(port, args.baud, args.lock_policy, project_dir)
        if result == "quit":
            print(f"\n{CYAN}Done.{NC}")
            return 0
        if result == "disconnected":
            print(f"\n{DIM}{'-' * 80}{NC}")
            print(f"{YELLOW}Device disconnected: {port}{NC}\n")
            time.sleep(0.5)
        elif result == "released-for-upload":
            print(f"\n{DIM}{'-' * 80}{NC}")
            print(f"{YELLOW}Serial monitor released {port} for upload; waiting to reconnect.{NC}\n")
            wait_for_upload_marker(project_dir)
            time.sleep(0.5)
        elif result == "error":
            time.sleep(2.0)


def run() -> int:
    try:
        require_pyserial()
        get_platform_adapter().install_monitor_signal_handlers(
            lambda _s, _f: (print(f"\n{CYAN}Done.{NC}"), sys.exit(0)),
            request_release_for_upload,
        )
        return main()
    except MonitorDependencyMissing as exc:
        print(f"error: {exc}", file=sys.stderr)
        return EXIT_MONITOR
    except PlatformOperationUnsupported as exc:
        print(f"error: {exc}", file=sys.stderr)
        return EXIT_UNSUPPORTED


if __name__ == "__main__":
    raise SystemExit(run())
