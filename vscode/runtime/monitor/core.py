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
    SerialPortRecord,
    get_platform_adapter,
)
from vscode.runtime.serial_identity import (
    SerialIdentityExpectation,
    match_serial_identity,
    normalize_identity_text,
)
from vscode.runtime.monitor.ownership import (
    RELEASE_REPLACE,
    RELEASE_UPLOAD,
    MonitorOwnershipConflict,
    load_monitor_ownership,
    monitor_release_action,
    register_monitor_ownership,
    request_monitor_release,
    unregister_monitor_ownership,
)

try:
    import serial
except ImportError:  # kept importable so hosts without pyserial still load the core
    serial = None


class MonitorDependencyMissing(RuntimeError):
    """Raised when the host lacks pyserial required by the monitor."""


def require_pyserial() -> None:
    if serial is None:
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
    return [
        port
        for port in get_platform_adapter().list_serial_ports()
        if is_serial_candidate(port.device)
    ]


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


def _serial_record(port_info) -> SerialPortRecord:
    if isinstance(port_info, SerialPortRecord):
        return port_info
    return SerialPortRecord.from_port_info(port_info)


def port_matches_identity(
    port_info,
    identity_tokens: list[str],
    expected: SerialIdentityExpectation | None = None,
) -> bool:
    record = _serial_record(port_info)
    if expected is not None and expected.configured():
        return match_serial_identity(record, expected).verified
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


def matching_identity_ports(
    mode: str,
    identity_tokens: list[str],
    expected: SerialIdentityExpectation | None = None,
):
    return [
        port
        for port in list_serial_ports()
        if port_matches_mode(port, mode)
        and port_matches_identity(port, identity_tokens, expected)
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


def load_local_settings(project_dir: Path | None) -> dict:
    if project_dir is None:
        return {}
    local_path = project_dir / ".vscode" / "jaszczurhal.local.json"
    if not local_path.is_file():
        return {}
    try:
        value = json.loads(local_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return value if isinstance(value, dict) else {}


def get_preferred_port(cli_port: str, project_dir: Path | None) -> str:
    if cli_port:
        return cli_port
    local = load_local_settings(project_dir)
    local_port = local.get("uploadPort")
    if isinstance(local_port, str) and local_port.strip():
        return local_port.strip()
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
    expected_identity: SerialIdentityExpectation | None = None,
) -> tuple[str | None, str]:
    if preferred_port:
        if serial_port_exists(preferred_port):
            return preferred_port, f"preferred:{preferred_port}"
        if follow_identity and (identity_tokens or expected_identity is not None):
            matches = matching_identity_ports(
                mode,
                identity_tokens,
                expected_identity,
            )
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
    expected_identity: SerialIdentityExpectation | None = None,
) -> str:
    spinner = ["|", "/", "-", "\\"]
    i = 0
    while True:
        port, reason = find_port(
            mode,
            preferred_port,
            identity_tokens,
            follow_identity,
            expected_identity,
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


def format_port_owners(port: str, project_dir: Path | None = None) -> list[str]:
    adapter = get_platform_adapter()
    ownership = load_monitor_ownership(adapter, project_dir, port)
    owners = set(adapter.port_owner_pids(port))
    if ownership is not None:
        owners.add(ownership.pid)

    lines = []
    for pid in sorted(owners):
        if pid != os.getpid():
            marker = " [JaszczurHAL monitor marker]" if (
                ownership is not None and pid == ownership.pid
            ) else ""
            lines.append(f"  PID {pid}: {adapter.process_cmdline(pid) or '?'}{marker}")
    if not lines and adapter.platform_name == "windows":
        lines.append(
            "  PID unavailable: no verified JaszczurHAL monitor marker "
            "for the busy COM port"
        )
    return lines


def apply_lock_policy(port: str, policy: str, project_dir: Path | None) -> bool:
    adapter = get_platform_adapter()
    ownership = load_monitor_ownership(adapter, project_dir, port)
    owners = {
        pid for pid in port_owner_pids(port) if pid != os.getpid()
    }
    if ownership is not None:
        owners.add(ownership.pid)
    if not owners:
        return True
    if policy == "wait":
        print(f"{YELLOW}Port {port} is busy; waiting. Owners:{NC}")
        print("\n".join(format_port_owners(port, project_dir)))
        return False
    if policy not in {"replace-own", "replace-any"}:
        return False

    foreign = sorted(
        pid
        for pid in owners
        if ownership is None or pid != ownership.pid
    )
    if ownership is None or foreign:
        print(
            f"{YELLOW}Port {port} is busy and no verified own monitor can be replaced. Owners:{NC}"
        )
        print("\n".join(format_port_owners(port, project_dir)))
        return False

    try:
        request_monitor_release(adapter, ownership, RELEASE_REPLACE)
    except (ProcessLookupError, PermissionError):
        return False

    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        current = load_monitor_ownership(adapter, project_dir, port)
        remaining = {
            pid for pid in port_owner_pids(port) if pid != os.getpid()
        }
        if current is None and not remaining:
            return True
        time.sleep(0.1)

    current = load_monitor_ownership(adapter, project_dir, port)
    if current is None or (
        current.pid != ownership.pid
        or current.process_start != ownership.process_start
        or adapter.process_start_identity(current.pid) != current.process_start
    ):
        return False
    try:
        adapter.terminate_process(current.pid)
    except (ProcessLookupError, PermissionError):
        return False
    deadline = time.monotonic() + 1.0
    while time.monotonic() < deadline:
        current = load_monitor_ownership(adapter, project_dir, port)
        remaining = {
            pid for pid in port_owner_pids(port) if pid != os.getpid()
        }
        if current is None and not remaining:
            return True
        time.sleep(0.1)
    return False


def is_lock_error(exc: Exception) -> bool:
    text = str(exc).lower()
    common_lock = (
        "could not exclusively lock port" in text
        or "resource temporarily unavailable" in text
        or "errno 11" in text
    )
    if common_lock:
        return True
    if get_platform_adapter().platform_name != "windows":
        return False
    windows_lock_tokens = (
        "access is denied",
        "permissionerror(13",
        "winerror 5",
        "errno 13",
    )
    return getattr(exc, "errno", None) in {5, 13} or any(
        token in text for token in windows_lock_tokens
    )


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
    clear_hupcl(getattr(ser, "fd", -1))
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
                owners = format_port_owners(port, project_dir)
                if owners:
                    print(f"{YELLOW}Port owners:{NC}")
                    print("\n".join(owners))
                time.sleep(1.0)
                continue
            print(f"{RED}Cannot open {port}: {exc}{NC}")
            return "error"

    print(f"{GREEN}Connected to {port} @ {baud}{NC}")
    print(f"{DIM}{'-' * 80}{NC}")
    adapter = get_platform_adapter()
    try:
        ownership = register_monitor_ownership(adapter, project_dir, port)
    except (MonitorOwnershipConflict, RuntimeError) as exc:
        try:
            ser.close()
        except Exception:
            pass
        print(f"{RED}Cannot register monitor ownership for {port}: {exc}{NC}")
        return "error"

    release_action: str | None = None
    try:
        while True:
            release_action = monitor_release_action(ownership)
            if release_action is not None or RELEASE_FOR_UPLOAD:
                release_action = release_action or RELEASE_UPLOAD
                break
            try:
                raw = ser.readline()
            except (serial.SerialException, OSError):
                break
            release_action = monitor_release_action(ownership)
            if release_action is not None or RELEASE_FOR_UPLOAD:
                release_action = release_action or RELEASE_UPLOAD
                break
            if raw:
                text = raw.replace(b"\r\n", b"\n").replace(b"\r", b"\n").decode("utf-8", errors="replace").rstrip("\n")
                if text:
                    print(text)
    except KeyboardInterrupt:
        release_action = "quit"
    finally:
        try:
            ser.close()
        except Exception:
            pass
        unregister_monitor_ownership(ownership)
    if release_action == "quit":
        return "quit"
    if release_action == RELEASE_REPLACE:
        return "released-for-replace"
    if release_action == RELEASE_UPLOAD or RELEASE_FOR_UPLOAD:
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
    parser.add_argument(
        "--identity-json",
        default="",
        help="Structured project USB identity used for exact metadata matching.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    project_dir = Path(args.project).resolve() if args.project else None
    preferred = get_preferred_port(args.port, project_dir)
    identity_tokens = [
        normalize_identity_text(token) for token in args.identity_token if token
    ]
    expected_identity = SerialIdentityExpectation()
    if args.identity_json:
        try:
            identity_value = json.loads(args.identity_json)
        except json.JSONDecodeError as exc:
            print(f"error: invalid --identity-json: {exc}", file=sys.stderr)
            return EXIT_MONITOR
        if not isinstance(identity_value, dict):
            print("error: --identity-json must contain an object", file=sys.stderr)
            return EXIT_MONITOR
        try:
            expected_identity = SerialIdentityExpectation.from_config(identity_value)
        except ValueError as exc:
            print(f"error: invalid --identity-json: {exc}", file=sys.stderr)
            return EXIT_MONITOR
    follow_identity = args.follow_identity and bool(
        identity_tokens or expected_identity.configured()
    )

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
            expected_identity,
        )
        if not port:
            port = wait_for_device(
                args.mode,
                preferred,
                identity_tokens,
                follow_identity,
                expected_identity,
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
        elif result == "released-for-replace":
            print(f"\n{CYAN}Monitor replaced by another JaszczurHAL session.{NC}")
            return 0
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
