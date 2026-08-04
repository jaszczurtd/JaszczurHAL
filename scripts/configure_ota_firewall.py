#!/usr/bin/env python3
"""Configure a persistent, LAN-scoped host firewall rule for OTA callbacks."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import ipaddress
import os
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Callable, Sequence


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from ota_firewall_common import NetworkScope, SetupError, is_rfc1918


DEFAULT_PORT = 8266
RULE_COMMENT = "JaszczurHAL OTA callback"


@dataclass(frozen=True)
class FirewallBackend:
    kind: str
    command: str = ""
    chain: str = ""
    zone: str = ""
    configured: bool = False
    needs_persistence_package: bool = False


class CommandRunner:
    """Small subprocess wrapper kept injectable for host-side tests."""

    def which(self, command: str) -> str | None:
        return shutil.which(command)

    def is_elevated(self) -> bool:
        if sys.platform == "win32":
            import ctypes

            return bool(ctypes.windll.shell32.IsUserAnAdmin())
        return os.geteuid() == 0

    def run(
        self,
        command: Sequence[str],
        *,
        sudo: bool = False,
        sudo_non_interactive: bool = False,
        check: bool = False,
    ) -> subprocess.CompletedProcess[str]:
        argv = list(command)
        if sudo and os.geteuid() != 0:
            sudo_argv = ["sudo"]
            if sudo_non_interactive:
                sudo_argv.append("-n")
            argv = [*sudo_argv, *argv]
        environment = dict(os.environ)
        environment["LC_ALL"] = "C"
        return subprocess.run(
            argv,
            check=check,
            capture_output=True,
            text=True,
            env=environment,
        )

def interface_from_route(line: str) -> str:
    fields = line.split()
    try:
        return fields[fields.index("dev") + 1]
    except (ValueError, IndexError):
        return ""


def parse_route_networks(text: str, interface: str = "") -> list[NetworkScope]:
    scopes: list[NetworkScope] = []
    for line in text.splitlines():
        fields = line.split()
        if not fields or fields[0] == "default":
            continue
        route_interface = interface_from_route(line)
        if not route_interface or (interface and route_interface != interface):
            continue
        try:
            network = ipaddress.IPv4Network(fields[0], strict=False)
        except ValueError:
            continue
        if not is_rfc1918(network):
            continue
        scope = NetworkScope(route_interface, network)
        if scope not in scopes:
            scopes.append(scope)
    return scopes


def parse_address_networks(text: str, interface: str) -> list[NetworkScope]:
    scopes: list[NetworkScope] = []
    for line in text.splitlines():
        fields = line.split()
        if "inet" not in fields:
            continue
        try:
            address = ipaddress.IPv4Interface(fields[fields.index("inet") + 1])
        except (ValueError, IndexError):
            continue
        if not is_rfc1918(address.network):
            continue
        scope = NetworkScope(interface, address.network)
        if scope not in scopes:
            scopes.append(scope)
    return scopes


def detect_network_scope(
    runner: CommandRunner,
    *,
    interface_override: str = "",
    network_override: str = "",
) -> NetworkScope:
    if not runner.which("ip"):
        raise SetupError("iproute2 is required to determine the OTA LAN scope")

    interface = interface_override
    if not interface:
        default_routes = runner.run(["ip", "-4", "route", "show", "default"])
        interfaces = [
            candidate
            for line in default_routes.stdout.splitlines()
            if (candidate := interface_from_route(line))
        ]
        interface = interfaces[0] if interfaces else ""

    if network_override:
        try:
            network = ipaddress.IPv4Network(network_override, strict=False)
        except ValueError as exc:
            raise SetupError(f"invalid IPv4 network: {network_override}") from exc
        if not is_rfc1918(network):
            raise SetupError(
                f"OTA firewall network must be RFC1918 private IPv4: {network}"
            )
        if not interface:
            raise SetupError("--interface is required when no default route is available")
        return NetworkScope(interface, network)

    route_command = ["ip", "-4", "route", "show", "scope", "link"]
    route_result = runner.run(route_command)
    scopes = parse_route_networks(route_result.stdout, interface)
    if not scopes and interface:
        address_result = runner.run(
            ["ip", "-4", "-o", "addr", "show", "dev", interface, "scope", "global"]
        )
        scopes = parse_address_networks(address_result.stdout, interface)
    if not scopes and not interface:
        scopes = parse_route_networks(route_result.stdout)
    if len(scopes) != 1:
        if not scopes:
            raise SetupError(
                "no RFC1918 LAN was detected; pass --interface and --network"
            )
        choices = ", ".join(f"{item.network} on {item.interface}" for item in scopes)
        raise SetupError(
            f"multiple OTA LAN candidates were detected ({choices}); "
            "pass --interface and --network"
        )
    return scopes[0]


def sudo_is_ready(runner: CommandRunner) -> bool:
    if runner.is_elevated():
        return True
    result = runner.run(["true"], sudo=True, sudo_non_interactive=True)
    return result.returncode == 0


def ensure_callback_port_available(runner: CommandRunner, port: int) -> None:
    if not runner.which("ss"):
        return
    listeners = runner.run(["ss", "-H", "-ltn", f"sport = :{port}"])
    if listeners.returncode == 0 and listeners.stdout.strip():
        raise SetupError(
            f"TCP/{port} is already used by a listening process; "
            "stop it before exposing the OTA callback port"
        )


def iptables_rule_arguments(
    action: str,
    chain: str,
    scope: NetworkScope,
    port: int,
) -> list[str]:
    arguments = [
        "-t",
        "filter",
        action,
        chain,
    ]
    if action == "-I":
        arguments.append("1")
    arguments.extend(
        [
            "-i",
            scope.interface,
            "-s",
            str(scope.network),
            "-p",
            "tcp",
            "--dport",
            str(port),
            "-m",
            "conntrack",
            "--ctstate",
            "NEW",
            "-m",
            "comment",
            "--comment",
            RULE_COMMENT,
            "-j",
            "ACCEPT",
        ]
    )
    return arguments


def ufw_rule(scope: NetworkScope, port: int) -> list[str]:
    return [
        "ufw",
        "allow",
        "in",
        "on",
        scope.interface,
        "proto",
        "tcp",
        "from",
        str(scope.network),
        "to",
        "any",
        "port",
        str(port),
        "comment",
        RULE_COMMENT,
    ]


def firewalld_rule(scope: NetworkScope, port: int) -> str:
    return (
        f'rule family="ipv4" source address="{scope.network}" '
        f'port port="{port}" protocol="tcp" accept'
    )


def iptables_persistent_rule_present(
    runner: CommandRunner,
    command: str,
    chain: str,
    scope: NetworkScope,
    port: int,
    *,
    sudo_non_interactive: bool,
) -> bool:
    live = runner.run(
        [
            command,
            *iptables_rule_arguments("-C", chain, scope, port),
        ],
        sudo=True,
        sudo_non_interactive=sudo_non_interactive,
    )
    if live.returncode != 0:
        return False
    saved = runner.run(
        ["cat", "/etc/iptables/rules.v4"],
        sudo=True,
        sudo_non_interactive=sudo_non_interactive,
    )
    required = (
        f"-A {chain}",
        f"-i {scope.interface}",
        f"-s {scope.network}",
        f"--dport {port}",
        RULE_COMMENT,
        "-j ACCEPT",
    )
    return saved.returncode == 0 and any(
        all(item in line for item in required) for line in saved.stdout.splitlines()
    )


def netfilter_persistence_enabled(
    runner: CommandRunner, *, sudo_non_interactive: bool
) -> bool:
    if not runner.which("systemctl"):
        return True
    enabled = runner.run(
        ["systemctl", "is-enabled", "netfilter-persistent.service"],
        sudo=True,
        sudo_non_interactive=sudo_non_interactive,
    )
    return enabled.returncode == 0


def iptables_input_is_permissive(
    runner: CommandRunner,
    command: str,
    *,
    sudo_non_interactive: bool,
) -> bool:
    rules = runner.run(
        [command, "-t", "filter", "-S", "INPUT"],
        sudo=True,
        sudo_non_interactive=sudo_non_interactive,
    )
    lines = [line.strip() for line in rules.stdout.splitlines() if line.strip()]
    if rules.returncode != 0 or lines != ["-P INPUT ACCEPT"]:
        return False
    if runner.which("nft"):
        native_rules = runner.run(
            ["nft", "list", "ruleset"],
            sudo=True,
            sudo_non_interactive=sudo_non_interactive,
        )
        if native_rules.returncode != 0:
            return False
        tokens = [
            token.rstrip(";,")
            for token in native_rules.stdout.replace("{", " ").replace("}", " ").split()
        ]
        if any(token in {"drop", "reject"} for token in tokens):
            return False
    return True


def detect_firewall_backend(
    runner: CommandRunner,
    scope: NetworkScope,
    port: int,
    *,
    sudo_non_interactive: bool,
) -> FirewallBackend:
    if runner.which("ufw"):
        status = runner.run(
            ["ufw", "status"],
            sudo=True,
            sudo_non_interactive=sudo_non_interactive,
        )
        if status.returncode == 0 and "Status: active" in status.stdout:
            added = runner.run(
                ["ufw", "show", "added"],
                sudo=True,
                sudo_non_interactive=sudo_non_interactive,
            )
            required = (
                f"on {scope.interface}",
                f"from {scope.network}",
                f"port {port}",
                "proto tcp",
            )
            configured = added.returncode == 0 and any(
                all(item in line for item in required)
                for line in added.stdout.splitlines()
            )
            return FirewallBackend("ufw", command="ufw", configured=configured)

    if runner.which("firewall-cmd"):
        state = runner.run(
            ["firewall-cmd", "--state"],
            sudo=True,
            sudo_non_interactive=sudo_non_interactive,
        )
        if state.returncode == 0 and state.stdout.strip() == "running":
            zone_result = runner.run(
                ["firewall-cmd", "--get-zone-of-interface", scope.interface],
                sudo=True,
                sudo_non_interactive=sudo_non_interactive,
            )
            zone = zone_result.stdout.strip()
            if zone_result.returncode != 0 or not zone or zone == "no zone":
                default_zone = runner.run(
                    ["firewall-cmd", "--get-default-zone"],
                    sudo=True,
                    sudo_non_interactive=sudo_non_interactive,
                    check=True,
                )
                zone = default_zone.stdout.strip()
            rich_rule = firewalld_rule(scope, port)
            runtime = runner.run(
                [
                    "firewall-cmd",
                    "--zone",
                    zone,
                    "--query-rich-rule",
                    rich_rule,
                ],
                sudo=True,
                sudo_non_interactive=sudo_non_interactive,
            )
            permanent = runner.run(
                [
                    "firewall-cmd",
                    "--permanent",
                    "--zone",
                    zone,
                    "--query-rich-rule",
                    rich_rule,
                ],
                sudo=True,
                sudo_non_interactive=sudo_non_interactive,
            )
            return FirewallBackend(
                "firewalld",
                command="firewall-cmd",
                zone=zone,
                configured=runtime.returncode == 0 and permanent.returncode == 0,
            )

    command = ""
    for candidate in ("iptables", "iptables-nft"):
        if runner.which(candidate):
            command = candidate
            break
    if not command:
        if runner.which("nft"):
            native_rules = runner.run(
                ["nft", "list", "ruleset"],
                sudo=True,
                sudo_non_interactive=sudo_non_interactive,
            )
            if native_rules.returncode == 0 and native_rules.stdout.strip():
                raise SetupError(
                    "an unmanaged native nftables ruleset is active; "
                    "configure it with its owning system policy"
                )
        return FirewallBackend("iptables", needs_persistence_package=True)

    chain = "INPUT"
    input_chain = runner.run(
        [command, "-t", "filter", "-n", "-L", chain],
        sudo=True,
        sudo_non_interactive=sudo_non_interactive,
    )
    if input_chain.returncode != 0:
        raise SetupError(f"cannot inspect the iptables chain {chain}")
    if iptables_input_is_permissive(
        runner,
        command,
        sudo_non_interactive=sudo_non_interactive,
    ):
        return FirewallBackend("iptables-permissive", command=command, configured=True)

    persistence_available = bool(runner.which("netfilter-persistent"))
    configured = (
        persistence_available
        and netfilter_persistence_enabled(
            runner, sudo_non_interactive=sudo_non_interactive
        )
        and iptables_persistent_rule_present(
            runner,
            command,
            chain,
            scope,
            port,
            sudo_non_interactive=sudo_non_interactive,
        )
    )
    return FirewallBackend(
        "iptables",
        command=command,
        chain=chain,
        configured=configured,
        needs_persistence_package=not persistence_available,
    )


def install_iptables_persistence(runner: CommandRunner) -> None:
    if not runner.which("apt-get"):
        raise SetupError(
            "iptables persistence is unavailable and apt-get was not found"
        )
    runner.run(["apt-get", "update"], sudo=True, check=True)
    runner.run(
        [
            "env",
            "DEBIAN_FRONTEND=noninteractive",
            "apt-get",
            "install",
            "-y",
            "iptables-persistent",
        ],
        sudo=True,
        check=True,
    )


def apply_ufw(
    runner: CommandRunner, backend: FirewallBackend, scope: NetworkScope, port: int
) -> None:
    del backend
    runner.run(ufw_rule(scope, port), sudo=True, check=True)


def apply_firewalld(
    runner: CommandRunner, backend: FirewallBackend, scope: NetworkScope, port: int
) -> None:
    rich_rule = firewalld_rule(scope, port)
    permanent_query = [
        backend.command,
        "--permanent",
        "--zone",
        backend.zone,
        "--query-rich-rule",
        rich_rule,
    ]
    runtime_query = [
        backend.command,
        "--zone",
        backend.zone,
        "--query-rich-rule",
        rich_rule,
    ]
    permanent_command = [
        backend.command,
        "--permanent",
        "--zone",
        backend.zone,
        "--add-rich-rule",
        rich_rule,
    ]
    runtime_command = [
        backend.command,
        "--zone",
        backend.zone,
        "--add-rich-rule",
        rich_rule,
    ]
    permanent_added = False
    if runner.run(permanent_query, sudo=True).returncode != 0:
        runner.run(permanent_command, sudo=True, check=True)
        permanent_added = True
    try:
        if runner.run(runtime_query, sudo=True).returncode != 0:
            runner.run(runtime_command, sudo=True, check=True)
    except subprocess.CalledProcessError:
        if permanent_added:
            runner.run(
                [
                    backend.command,
                    "--permanent",
                    "--zone",
                    backend.zone,
                    "--remove-rich-rule",
                    rich_rule,
                ],
                sudo=True,
            )
        raise


def apply_iptables(
    runner: CommandRunner, backend: FirewallBackend, scope: NetworkScope, port: int
) -> None:
    if backend.needs_persistence_package:
        install_iptables_persistence(runner)
        backend = detect_firewall_backend(
            runner, scope, port, sudo_non_interactive=False
        )
        if backend.kind != "iptables":
            raise SetupError("the iptables backend changed during package installation")

    if not backend.command or not runner.which("netfilter-persistent"):
        raise SetupError("iptables and netfilter-persistent are required")
    save_command = (
        "iptables-nft-save"
        if backend.command == "iptables-nft" and runner.which("iptables-nft-save")
        else "iptables-save"
    )
    if not runner.which(save_command):
        raise SetupError(f"{save_command} is required for IPv4 rule persistence")

    if runner.which("systemctl"):
        runner.run(
            ["systemctl", "enable", "netfilter-persistent.service"],
            sudo=True,
            check=True,
        )

    check_arguments = [
        backend.command,
        *iptables_rule_arguments("-C", backend.chain, scope, port),
    ]
    add_arguments = [
        backend.command,
        *iptables_rule_arguments("-I", backend.chain, scope, port),
    ]
    delete_arguments = [
        backend.command,
        *iptables_rule_arguments("-D", backend.chain, scope, port),
    ]
    live = runner.run(check_arguments, sudo=True).returncode == 0
    if not live:
        runner.run(add_arguments, sudo=True, check=True)
    try:
        runner.run(
            [save_command, "-f", "/etc/iptables/rules.v4"],
            sudo=True,
            check=True,
        )
    except subprocess.CalledProcessError:
        if not live:
            runner.run(delete_arguments, sudo=True)
        raise


def apply_firewall_backend(
    runner: CommandRunner, backend: FirewallBackend, scope: NetworkScope, port: int
) -> None:
    if backend.kind == "ufw":
        apply_ufw(runner, backend, scope, port)
    elif backend.kind == "firewalld":
        apply_firewalld(runner, backend, scope, port)
    elif backend.kind == "iptables":
        apply_iptables(runner, backend, scope, port)
    else:
        raise SetupError(f"unsupported firewall backend: {backend.kind}")


def ask_for_consent(
    scope: NetworkScope,
    port: int,
    backend: FirewallBackend | None,
    input_function: Callable[[str], str] = input,
) -> bool:
    print()
    print("JaszczurHAL OTA needs a device-to-host TCP callback.")
    print(f"  interface: {scope.interface}")
    print(f"  source:    {scope.network}")
    print(f"  port:      TCP/{port}")
    print("  lifetime:  persistent across reboot")
    if backend is None:
        print("  backend:   inspected with sudo only after consent")
        print("  package:   iptables-persistent may be installed if required")
    if backend and backend.needs_persistence_package:
        print("  package:   iptables-persistent (installed only after consent)")
    if backend and backend.kind == "iptables":
        print("  storage:   iptables-save writes the active IPv4 ruleset")
        print("  boot:      netfilter-persistent.service is enabled")
    try:
        response = input_function(
            "Allow this LAN-scoped OTA callback rule? [y/N] "
        ).strip()
    except EOFError:
        return False
    return response.lower() in {"y", "yes"}


def configure_linux_firewall(
    args: argparse.Namespace,
    *,
    runner: CommandRunner | None = None,
    input_function: Callable[[str], str] = input,
) -> int:
    runner = runner or CommandRunner()
    scope = detect_network_scope(
        runner,
        interface_override=args.interface,
        network_override=args.network,
    )

    backend: FirewallBackend | None = None
    ready = sudo_is_ready(runner)
    if ready:
        backend = detect_firewall_backend(
            runner, scope, args.port, sudo_non_interactive=True
        )
        if backend.configured:
            print(
                f"OTA firewall already allows TCP/{args.port} from "
                f"{scope.network} on {scope.interface} ({backend.kind})."
            )
            return 0
        if args.check:
            print(
                f"OTA firewall rule is missing for TCP/{args.port} from "
                f"{scope.network} on {scope.interface}.",
                file=sys.stderr,
            )
            return 1
    elif args.check:
        print(
            "Cannot inspect the OTA firewall without cached sudo credentials.",
            file=sys.stderr,
        )
        return 2

    ensure_callback_port_available(runner, args.port)
    if args.dry_run:
        print("Linux firewall OTA callback plan:")
        print(f"  interface: {scope.interface}")
        print(f"  source:    {scope.network}")
        print(f"  port:      TCP/{args.port}")
        print("  lifetime:  persistent across reboot")
        if backend is not None:
            print(f"  backend:   {backend.kind}")
        else:
            print("  backend:   selected after elevation")
        print("Dry run: no firewall changes were made.")
        return 0
    if not args.yes and not ask_for_consent(
        scope, args.port, backend, input_function=input_function
    ):
        print(
            "OTA firewall setup skipped. Re-run this helper before using OTA."
        )
        return 0

    if not ready:
        print("Elevated access is now required to inspect and update the firewall.")
        runner.run(["true"], sudo=True, check=True)
        backend = detect_firewall_backend(
            runner, scope, args.port, sudo_non_interactive=False
        )
        if backend.configured:
            print(
                f"OTA firewall already allows TCP/{args.port} from "
                f"{scope.network} on {scope.interface} ({backend.kind})."
            )
            return 0

    if backend is None:
        raise SetupError("no supported firewall backend was detected")
    apply_firewall_backend(runner, backend, scope, args.port)
    verified = detect_firewall_backend(
        runner, scope, args.port, sudo_non_interactive=False
    )
    if not verified.configured:
        raise SetupError("the OTA firewall rule could not be verified as persistent")
    print(
        f"Configured persistent OTA callback access: TCP/{args.port} from "
        f"{scope.network} on {scope.interface} ({verified.kind})."
    )
    return 0


def configure_firewall(
    args: argparse.Namespace,
    *,
    runner: CommandRunner | None = None,
    input_function: Callable[[str], str] = input,
    platform_name: str | None = None,
) -> int:
    runner = runner or CommandRunner()
    selected_platform = platform_name or sys.platform
    if selected_platform == "win32":
        from ota_firewall_windows import configure_firewall as configure_windows

        return configure_windows(
            args,
            runner=runner,
            input_function=input_function,
        )
    return configure_linux_firewall(
        args,
        runner=runner,
        input_function=input_function,
    )


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Configure a persistent LAN-scoped firewall rule for the "
            "JaszczurHAL OTA TCP callback."
        )
    )
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--interface", default="")
    parser.add_argument("--network", default="")
    parser.add_argument(
        "--yes",
        action="store_true",
        help="Apply the detected rule without the confirmation prompt.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Check for the persistent rule without changing the firewall.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the scoped rule plan without changing the firewall.",
    )
    args = parser.parse_args(argv)
    if not 1 <= args.port <= 65535:
        parser.error("--port must be in range 1..65535")
    if args.yes and (not args.interface or not args.network):
        parser.error("--yes requires explicit --interface and --network")
    if args.check and args.dry_run:
        parser.error("--check and --dry-run are mutually exclusive")
    return args


def main(argv: Sequence[str]) -> int:
    try:
        return configure_firewall(parse_args(argv))
    except (SetupError, subprocess.CalledProcessError) as exc:
        print(f"error: OTA firewall setup failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
