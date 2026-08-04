"""Windows Defender Firewall backend for the OTA callback helper."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import ipaddress
import json
import subprocess
import sys
from typing import Callable, Protocol, Sequence

from ota_firewall_common import NetworkScope, SetupError, is_rfc1918


RULE_NAME_PREFIX = "JaszczurHAL-OTA-Callback-TCP"
RULE_DISPLAY_PREFIX = "JaszczurHAL OTA callback TCP"


class Runner(Protocol):
    def which(self, command: str) -> str | None: ...

    def is_elevated(self) -> bool: ...

    def run(
        self,
        command: Sequence[str],
        *,
        sudo: bool = False,
        sudo_non_interactive: bool = False,
        check: bool = False,
    ) -> subprocess.CompletedProcess[str]: ...


@dataclass(frozen=True)
class NetworkCandidate:
    interface: str
    address: ipaddress.IPv4Address
    prefix_length: int
    category: str
    adapter_status: str
    address_state: str
    has_default_route: bool

    @property
    def scope(self) -> NetworkScope:
        interface = ipaddress.IPv4Interface(f"{self.address}/{self.prefix_length}")
        return NetworkScope(self.interface, interface.network)


@dataclass(frozen=True)
class WindowsFirewallBackend:
    configured: bool
    rule_name: str


NETWORK_QUERY = r"""
$ErrorActionPreference = 'Stop'
# JH:network-scope
$defaultIndices = @(
    Get-NetRoute -AddressFamily IPv4 -DestinationPrefix '0.0.0.0/0' |
        Where-Object { $_.NextHop -ne '0.0.0.0' } |
        Select-Object -ExpandProperty InterfaceIndex -Unique
)
$rows = foreach ($profile in Get-NetConnectionProfile) {
    $adapter = Get-NetAdapter -InterfaceIndex $profile.InterfaceIndex
    foreach ($address in Get-NetIPAddress -InterfaceIndex $profile.InterfaceIndex -AddressFamily IPv4) {
        [pscustomobject]@{
            InterfaceAlias = [string]$profile.InterfaceAlias
            IPv4Address = [string]$address.IPAddress
            PrefixLength = [int]$address.PrefixLength
            NetworkCategory = [string]$profile.NetworkCategory
            AdapterStatus = [string]$adapter.Status
            AddressState = [string]$address.AddressState
            HasDefaultRoute = [bool]($defaultIndices -contains $profile.InterfaceIndex)
        }
    }
}
@($rows) | ConvertTo-Json -Compress
""".strip()


def powershell_command(script: str) -> list[str]:
    return [
        "powershell.exe",
        "-NoProfile",
        "-NonInteractive",
        "-ExecutionPolicy",
        "Bypass",
        "-Command",
        script,
    ]


def powershell_quote(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def parse_network_candidates(text: str) -> list[NetworkCandidate]:
    if not text.strip():
        return []
    try:
        decoded = json.loads(text)
    except json.JSONDecodeError as exc:
        raise SetupError("Windows network discovery returned invalid JSON") from exc
    rows = decoded if isinstance(decoded, list) else [decoded]
    candidates: list[NetworkCandidate] = []
    for row in rows:
        if not isinstance(row, dict):
            continue
        try:
            candidate = NetworkCandidate(
                interface=str(row["InterfaceAlias"]),
                address=ipaddress.IPv4Address(str(row["IPv4Address"])),
                prefix_length=int(row["PrefixLength"]),
                category=str(row["NetworkCategory"]),
                adapter_status=str(row["AdapterStatus"]),
                address_state=str(row["AddressState"]),
                has_default_route=bool(row["HasDefaultRoute"]),
            )
        except (KeyError, TypeError, ValueError):
            continue
        if (
            candidate.category.casefold() == "private"
            and candidate.adapter_status.casefold() == "up"
            and candidate.address_state.casefold() == "preferred"
            and is_rfc1918(candidate.scope.network)
        ):
            candidates.append(candidate)
    return candidates


def detect_network_scope(
    runner: Runner,
    *,
    interface_override: str = "",
    network_override: str = "",
) -> NetworkScope:
    if not runner.which("powershell.exe"):
        raise SetupError("Windows PowerShell is required for network discovery")
    result = runner.run(powershell_command(NETWORK_QUERY))
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise SetupError(f"Windows network discovery failed: {detail}")
    candidates = parse_network_candidates(result.stdout)
    if interface_override:
        candidates = [
            candidate
            for candidate in candidates
            if candidate.interface.casefold() == interface_override.casefold()
        ]

    requested_network: ipaddress.IPv4Network | None = None
    if network_override:
        try:
            requested_network = ipaddress.IPv4Network(network_override, strict=False)
        except ValueError as exc:
            raise SetupError(f"invalid IPv4 network: {network_override}") from exc
        if not is_rfc1918(requested_network):
            raise SetupError(
                "OTA firewall network must be RFC1918 private IPv4: "
                f"{requested_network}"
            )
        candidates = [
            candidate
            for candidate in candidates
            if candidate.address in requested_network
        ]

    default_candidates = [candidate for candidate in candidates if candidate.has_default_route]
    if not interface_override and not network_override and default_candidates:
        candidates = default_candidates

    scopes: list[NetworkScope] = []
    for candidate in candidates:
        scope = NetworkScope(
            candidate.interface,
            requested_network or candidate.scope.network,
        )
        if scope not in scopes:
            scopes.append(scope)
    if len(scopes) != 1:
        if not scopes:
            raise SetupError(
                "no active Private RFC1918 network was detected; set the Windows "
                "network profile to Private or pass --interface and --network"
            )
        choices = ", ".join(
            f"{scope.network} on {scope.interface}" for scope in scopes
        )
        raise SetupError(
            f"multiple Private OTA LAN candidates were detected ({choices}); "
            "pass --interface and --network"
        )
    return scopes[0]


def rule_name(port: int) -> str:
    return f"{RULE_NAME_PREFIX}-{port}"


def rule_display_name(port: int) -> str:
    return f"{RULE_DISPLAY_PREFIX}/{port}"


def inspect_rule_script(scope: NetworkScope, port: int) -> str:
    name = powershell_quote(rule_name(port))
    interface = powershell_quote(scope.interface)
    network = powershell_quote(str(scope.network))
    network_with_mask = powershell_quote(
        f"{scope.network.network_address}/{scope.network.netmask}"
    )
    network_range = powershell_quote(
        f"{scope.network.network_address}-{scope.network.broadcast_address}"
    )
    return rf"""
$ErrorActionPreference = 'Stop'
# JH:inspect-rule
$rule = @(
    Get-NetFirewallRule -PolicyStore ActiveStore -ErrorAction Stop |
        Where-Object {{ [string]$_.Name -eq {name} }}
)
if ($rule.Count -eq 0) {{ [Console]::Out.Write('false'); exit 0 }}
$portFilter = $rule | Get-NetFirewallPortFilter
$addressFilter = $rule | Get-NetFirewallAddressFilter
$interfaceFilter = $rule | Get-NetFirewallInterfaceFilter
$remoteAddresses = @($addressFilter.RemoteAddress | ForEach-Object {{ [string]$_ }})
$networkMatches = (
    $remoteAddresses -contains {network} -or
    $remoteAddresses -contains {network_with_mask} -or
    $remoteAddresses -contains {network_range}
)
$ok = (
    [string]$rule.Enabled -eq 'True' -and
    [string]$rule.Direction -eq 'Inbound' -and
    [string]$rule.Action -eq 'Allow' -and
    [string]$rule.Profile -eq 'Private' -and
    [string]$portFilter.Protocol -eq 'TCP' -and
    @($portFilter.LocalPort) -contains '{port}' -and
    $networkMatches -and
    @($interfaceFilter.InterfaceAlias) -contains {interface}
)
[Console]::Out.Write(([string]$ok).ToLowerInvariant())
""".strip()


def detect_firewall_backend(
    runner: Runner, scope: NetworkScope, port: int
) -> WindowsFirewallBackend:
    result = runner.run(powershell_command(inspect_rule_script(scope, port)))
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise SetupError(f"Windows Defender Firewall inspection failed: {detail}")
    answer = result.stdout.strip().casefold()
    if answer not in {"true", "false"}:
        raise SetupError("Windows Defender Firewall inspection returned invalid output")
    return WindowsFirewallBackend(answer == "true", rule_name(port))


def ensure_callback_port_available(runner: Runner, port: int) -> None:
    if not runner.which("netstat.exe") and not runner.which("netstat"):
        return
    result = runner.run(["netstat.exe", "-ano", "-p", "tcp"])
    suffix = f":{port}"
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 4 and fields[0].casefold() == "tcp":
            if fields[1].endswith(suffix) and fields[3].casefold() == "listening":
                raise SetupError(
                    f"TCP/{port} is already used by a listening process; "
                    "stop it before exposing the OTA callback port"
                )


def apply_rule_script(scope: NetworkScope, port: int) -> str:
    name = powershell_quote(rule_name(port))
    display_name = powershell_quote(rule_display_name(port))
    interface = powershell_quote(scope.interface)
    network = powershell_quote(str(scope.network))
    return rf"""
$ErrorActionPreference = 'Stop'
# JH:apply-rule
$existing = Get-NetFirewallRule -Name {name} -ErrorAction SilentlyContinue
if ($null -ne $existing) {{ $existing | Remove-NetFirewallRule }}
$parameters = @{{
    Name = {name}
    DisplayName = {display_name}
    Description = 'JaszczurHAL OTA device-to-host callback'
    Enabled = 'True'
    Profile = 'Private'
    Direction = 'Inbound'
    Action = 'Allow'
    Protocol = 'TCP'
    LocalPort = {port}
    RemoteAddress = {network}
    InterfaceAlias = {interface}
}}
New-NetFirewallRule @parameters | Out-Null
""".strip()


def print_plan(scope: NetworkScope, port: int) -> None:
    print("Windows Defender Firewall OTA callback plan:")
    print(f"  interface: {scope.interface}")
    print(f"  profile:   Private")
    print(f"  source:    {scope.network}")
    print(f"  port:      TCP/{port}")
    print(f"  rule:      {rule_name(port)}")
    print("  lifetime:  persistent across reboot")


def ask_for_consent(
    scope: NetworkScope,
    port: int,
    input_function: Callable[[str], str],
) -> bool:
    print()
    print_plan(scope, port)
    print("  elevation: administrator shell required for the change")
    try:
        response = input_function(
            "Allow this Private-LAN-scoped OTA callback rule? [y/N] "
        ).strip()
    except EOFError:
        return False
    return response.casefold() in {"y", "yes"}


def configure_firewall(
    args: argparse.Namespace,
    *,
    runner: Runner,
    input_function: Callable[[str], str] = input,
) -> int:
    scope = detect_network_scope(
        runner,
        interface_override=args.interface,
        network_override=args.network,
    )
    if args.dry_run:
        ensure_callback_port_available(runner, args.port)
        print_plan(scope, args.port)
        print("Dry run: no firewall changes were made.")
        return 0

    backend = detect_firewall_backend(runner, scope, args.port)
    if backend.configured:
        print(
            f"OTA firewall already allows TCP/{args.port} from "
            f"{scope.network} on {scope.interface} "
            "(Windows Defender Firewall, Private)."
        )
        return 0
    if args.check:
        print(
            f"OTA firewall rule is missing for TCP/{args.port} from "
            f"{scope.network} on {scope.interface} "
            "(Windows Defender Firewall, Private).",
            file=sys.stderr,
        )
        return 1

    ensure_callback_port_available(runner, args.port)
    if not args.yes and not ask_for_consent(
        scope, args.port, input_function=input_function
    ):
        print("OTA firewall setup skipped. Re-run this helper before using OTA.")
        return 0
    if not runner.is_elevated():
        raise SetupError(
            "administrator access is required to update Windows Defender Firewall; "
            "re-run this command from an elevated PowerShell window"
        )

    runner.run(
        powershell_command(apply_rule_script(scope, args.port)),
        check=True,
    )
    verified = detect_firewall_backend(runner, scope, args.port)
    if not verified.configured:
        raise SetupError("the Windows Defender Firewall rule could not be verified")
    print(
        f"Configured persistent OTA callback access: TCP/{args.port} from "
        f"{scope.network} on {scope.interface} "
        "(Windows Defender Firewall, Private)."
    )
    return 0
