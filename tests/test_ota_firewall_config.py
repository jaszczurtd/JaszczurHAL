#!/usr/bin/env python3
"""Host-side tests for persistent OTA firewall setup."""

from __future__ import annotations

import argparse
from contextlib import redirect_stderr
import importlib.util
import io
import ipaddress
import json
from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(sys.argv[1]).resolve()
MODULE_PATH = ROOT / "scripts/configure_ota_firewall.py"
SPEC = importlib.util.spec_from_file_location("configure_ota_firewall", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {MODULE_PATH}")
FIREWALL = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = FIREWALL
SPEC.loader.exec_module(FIREWALL)

import ota_firewall_windows as WINDOWS_FIREWALL


def result(
    command: list[str], returncode: int = 0, stdout: str = "", stderr: str = ""
) -> subprocess.CompletedProcess[str]:
    return subprocess.CompletedProcess(command, returncode, stdout, stderr)


class FakeRunner:
    def __init__(self) -> None:
        self.available = {
            "ip",
            "iptables-nft",
            "iptables-nft-save",
            "netfilter-persistent",
            "apt-get",
        }
        self.calls: list[tuple[tuple[str, ...], bool, bool]] = []
        self.live_rule = False
        self.saved_rule = False
        self.ufw_configured = False
        self.firewalld_runtime = False
        self.firewalld_permanent = False
        self.netfilter_service_enabled = False
        self.iptables_permissive = False
        self.elevated = True

    def which(self, command: str) -> str | None:
        return f"/fake/{command}" if command in self.available else None

    def is_elevated(self) -> bool:
        return self.elevated

    def run(
        self,
        command: list[str],
        *,
        sudo: bool = False,
        sudo_non_interactive: bool = False,
        check: bool = False,
    ) -> subprocess.CompletedProcess[str]:
        argv = list(command)
        self.calls.append((tuple(argv), sudo, sudo_non_interactive))
        response = self.dispatch(argv)
        if check and response.returncode != 0:
            raise subprocess.CalledProcessError(
                response.returncode,
                argv,
                output=response.stdout,
                stderr=response.stderr,
            )
        return response

    def dispatch(self, command: list[str]) -> subprocess.CompletedProcess[str]:
        if command == ["true"]:
            return result(command)
        if command == ["ip", "-4", "route", "show", "default"]:
            return result(command, stdout="default via 192.168.2.1 dev enp7s0\n")
        if command == ["ip", "-4", "route", "show", "scope", "link"]:
            return result(
                command,
                stdout=(
                    "192.168.2.0/24 dev enp7s0 proto kernel scope link "
                    "src 192.168.2.180\n"
                    "172.17.0.0/16 dev docker0 proto kernel scope link "
                    "src 172.17.0.1\n"
                ),
            )
        if command == ["ss", "-H", "-ltn", "sport = :8266"]:
            return result(command, stdout="LISTEN 0 1 0.0.0.0:8266 0.0.0.0:*\n")
        if command[:2] == ["ufw", "status"]:
            return result(command, stdout="Status: active\n")
        if command[:3] == ["ufw", "show", "added"]:
            added = ""
            if self.ufw_configured:
                added = (
                    "ufw allow in on enp7s0 proto tcp from 192.168.2.0/24 "
                    "to any port 8266 comment 'JaszczurHAL OTA callback'\n"
                )
            return result(command, stdout=added)
        if command[:2] == ["ufw", "allow"]:
            self.ufw_configured = True
            return result(command)
        if command[:2] == ["firewall-cmd", "--state"]:
            return result(command, stdout="running\n")
        if command[:2] == ["firewall-cmd", "--get-zone-of-interface"]:
            return result(command, stdout="home\n")
        if "--query-rich-rule" in command:
            configured = (
                self.firewalld_permanent
                if "--permanent" in command
                else self.firewalld_runtime
            )
            return result(command, returncode=0 if configured else 1)
        if "--add-rich-rule" in command:
            if "--permanent" in command:
                self.firewalld_permanent = True
            else:
                self.firewalld_runtime = True
            return result(command)
        if (
            command
            and command[0] in {"iptables", "iptables-nft"}
            and command[1:5] == ["-t", "filter", "-n", "-L"]
        ):
            return result(command)
        if (
            command
            and command[0] in {"iptables", "iptables-nft"}
            and command[1:] == ["-t", "filter", "-S", "INPUT"]
        ):
            output = "-P INPUT ACCEPT\n" if self.iptables_permissive else ""
            return result(command, stdout=output)
        if (
            command
            and command[0] in {"iptables", "iptables-nft"}
            and "-C" in command
        ):
            return result(command, returncode=0 if self.live_rule else 1)
        if (
            command
            and command[0] in {"iptables", "iptables-nft"}
            and "-I" in command
        ):
            self.live_rule = True
            return result(command)
        if (
            command
            and command[0] in {"iptables", "iptables-nft"}
            and "-D" in command
        ):
            self.live_rule = False
            return result(command)
        if command == ["cat", "/etc/iptables/rules.v4"]:
            saved = ""
            if self.saved_rule:
                saved = (
                    '-A INPUT -i enp7s0 -s 192.168.2.0/24 '
                    '-p tcp --dport 8266 -m comment '
                    '--comment "JaszczurHAL OTA callback" -j ACCEPT\n'
                )
            return result(command, stdout=saved)
        if command in (
            ["iptables-save", "-f", "/etc/iptables/rules.v4"],
            ["iptables-nft-save", "-f", "/etc/iptables/rules.v4"],
        ):
            self.saved_rule = self.live_rule
            return result(command)
        if command == [
            "systemctl",
            "is-enabled",
            "netfilter-persistent.service",
        ]:
            return result(
                command,
                returncode=0 if self.netfilter_service_enabled else 1,
            )
        if command == ["systemctl", "enable", "netfilter-persistent.service"]:
            self.netfilter_service_enabled = True
            return result(command)
        if command[:2] == ["apt-get", "update"]:
            return result(command)
        if "iptables-persistent" in command:
            self.available.update(
                {"iptables-nft", "iptables-nft-save", "netfilter-persistent"}
            )
            return result(command)
        if command == ["nft", "list", "ruleset"]:
            return result(command, stdout="table inet managed_elsewhere {}\n")
        return result(command, returncode=127, stderr="unexpected fake command")


class FakeWindowsRunner:
    def __init__(self, *, elevated: bool = False) -> None:
        self.elevated = elevated
        self.configured = False
        self.listener = False
        self.calls: list[tuple[str, ...]] = []

    def which(self, command: str) -> str | None:
        if command in {"powershell.exe", "netstat.exe", "netstat"}:
            return f"C:/Windows/System32/{command}"
        return None

    def is_elevated(self) -> bool:
        return self.elevated

    def run(
        self,
        command: list[str],
        *,
        sudo: bool = False,
        sudo_non_interactive: bool = False,
        check: bool = False,
    ) -> subprocess.CompletedProcess[str]:
        del sudo, sudo_non_interactive
        argv = list(command)
        self.calls.append(tuple(argv))
        response = self.dispatch(argv)
        if check and response.returncode != 0:
            raise subprocess.CalledProcessError(
                response.returncode,
                argv,
                output=response.stdout,
                stderr=response.stderr,
            )
        return response

    def dispatch(self, command: list[str]) -> subprocess.CompletedProcess[str]:
        if command[:1] == ["powershell.exe"]:
            script = command[-1]
            if "JH:network-scope" in script:
                return result(
                    command,
                    stdout=json.dumps(
                        [
                            {
                                "InterfaceAlias": "Wi-Fi",
                                "IPv4Address": "192.168.2.15",
                                "PrefixLength": 24,
                                "NetworkCategory": "Private",
                                "AdapterStatus": "Up",
                                "AddressState": "Preferred",
                                "HasDefaultRoute": True,
                            },
                            {
                                "InterfaceAlias": "VPN",
                                "IPv4Address": "10.8.0.7",
                                "PrefixLength": 24,
                                "NetworkCategory": "Private",
                                "AdapterStatus": "Up",
                                "AddressState": "Preferred",
                                "HasDefaultRoute": False,
                            },
                        ]
                    ),
                )
            if "JH:inspect-rule" in script:
                return result(command, stdout=str(self.configured).lower())
            if "JH:apply-rule" in script:
                self.configured = True
                return result(command)
        if command == ["netstat.exe", "-ano", "-p", "tcp"]:
            output = ""
            if self.listener:
                output = "  TCP    0.0.0.0:8266    0.0.0.0:0    LISTENING    42\n"
            return result(command, stdout=output)
        return result(command, returncode=127, stderr="unexpected fake command")


def arguments(**overrides: object) -> argparse.Namespace:
    values = {
        "port": 8266,
        "interface": "",
        "network": "",
        "yes": False,
        "check": False,
        "dry_run": False,
    }
    values.update(overrides)
    return argparse.Namespace(**values)


class OtaFirewallTests(unittest.TestCase):
    def test_detects_default_interface_private_network(self) -> None:
        scope = FIREWALL.detect_network_scope(FakeRunner())
        self.assertEqual(scope.interface, "enp7s0")
        self.assertEqual(scope.network, ipaddress.IPv4Network("192.168.2.0/24"))

    def test_rejects_public_network_override(self) -> None:
        with self.assertRaises(FIREWALL.SetupError):
            FIREWALL.detect_network_scope(
                FakeRunner(),
                interface_override="enp7s0",
                network_override="203.0.113.0/24",
            )

    def test_non_interactive_mode_requires_explicit_scope(self) -> None:
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            FIREWALL.parse_args(["--yes"])

    def test_linux_dry_run_does_not_mutate_firewall(self) -> None:
        runner = FakeRunner()
        status = FIREWALL.configure_firewall(
            arguments(dry_run=True),
            runner=runner,
            input_function=lambda _: "y",
            platform_name="linux",
        )
        self.assertEqual(status, 0)
        self.assertFalse(runner.live_rule)
        self.assertFalse(runner.saved_rule)

    def test_refuses_to_expose_an_existing_listener(self) -> None:
        runner = FakeRunner()
        runner.available.add("ss")
        with self.assertRaises(FIREWALL.SetupError):
            FIREWALL.configure_firewall(
                arguments(yes=True),
                runner=runner,
                input_function=lambda _: "n",
                platform_name="linux",
            )
        self.assertFalse(runner.saved_rule)

    def test_decline_does_not_mutate_firewall(self) -> None:
        runner = FakeRunner()
        runner.elevated = False
        original_dispatch = runner.dispatch

        def dispatch(command: list[str]) -> subprocess.CompletedProcess[str]:
            if command == ["true"]:
                return result(command, returncode=1)
            return original_dispatch(command)

        runner.dispatch = dispatch
        status = FIREWALL.configure_firewall(
            arguments(),
            runner=runner,
            input_function=lambda _: "n",
            platform_name="linux",
        )
        self.assertEqual(status, 0)
        self.assertFalse(runner.live_rule)
        self.assertFalse(runner.saved_rule)

    def test_iptables_rule_is_scoped_and_persisted(self) -> None:
        runner = FakeRunner()
        status = FIREWALL.configure_firewall(
            arguments(yes=True),
            runner=runner,
            input_function=lambda _: "n",
            platform_name="linux",
        )
        self.assertEqual(status, 0)
        self.assertTrue(runner.live_rule)
        self.assertTrue(runner.saved_rule)
        inserted = next(call[0] for call in runner.calls if "-I" in call[0])
        self.assertIn("INPUT", inserted)
        self.assertIn("enp7s0", inserted)
        self.assertIn("192.168.2.0/24", inserted)
        self.assertIn("8266", inserted)

    def test_permissive_input_needs_no_rule_or_package(self) -> None:
        runner = FakeRunner()
        runner.available = {"ip", "iptables", "iptables-save"}
        runner.iptables_permissive = True
        status = FIREWALL.configure_firewall(
            arguments(),
            runner=runner,
            input_function=lambda _: "n",
            platform_name="linux",
        )
        self.assertEqual(status, 0)
        self.assertFalse(runner.saved_rule)
        self.assertFalse(any("-I" in call[0] for call in runner.calls))
        self.assertFalse(
            any("iptables-persistent" in call[0] for call in runner.calls)
        )

    def test_missing_iptables_persistence_installs_minimum_package(self) -> None:
        runner = FakeRunner()
        runner.available = {"ip", "apt-get"}
        status = FIREWALL.configure_firewall(
            arguments(yes=True),
            runner=runner,
            input_function=lambda _: "n",
            platform_name="linux",
        )
        self.assertEqual(status, 0)
        self.assertTrue(runner.saved_rule)
        self.assertTrue(
            any("iptables-persistent" in call[0] for call in runner.calls)
        )

    def test_iptables_enables_persistent_boot_loader(self) -> None:
        runner = FakeRunner()
        runner.available.add("systemctl")
        status = FIREWALL.configure_firewall(
            arguments(yes=True),
            runner=runner,
            input_function=lambda _: "n",
            platform_name="linux",
        )
        self.assertEqual(status, 0)
        self.assertTrue(runner.netfilter_service_enabled)
        self.assertTrue(runner.saved_rule)

    def test_unmanaged_native_nftables_is_not_modified(self) -> None:
        runner = FakeRunner()
        runner.available = {"ip", "nft", "apt-get"}
        with self.assertRaises(FIREWALL.SetupError):
            FIREWALL.configure_firewall(
                arguments(yes=True),
                runner=runner,
                input_function=lambda _: "n",
                platform_name="linux",
            )
        self.assertFalse(runner.saved_rule)

    def test_active_ufw_uses_ufw_persistent_rule(self) -> None:
        runner = FakeRunner()
        runner.available.add("ufw")
        status = FIREWALL.configure_firewall(
            arguments(yes=True),
            runner=runner,
            input_function=lambda _: "n",
            platform_name="linux",
        )
        self.assertEqual(status, 0)
        self.assertTrue(runner.ufw_configured)
        self.assertFalse(any("-I" in call[0] for call in runner.calls))

    def test_active_firewalld_updates_runtime_and_permanent_state(self) -> None:
        runner = FakeRunner()
        runner.available = {"ip", "firewall-cmd"}
        status = FIREWALL.configure_firewall(
            arguments(yes=True),
            runner=runner,
            input_function=lambda _: "n",
            platform_name="linux",
        )
        self.assertEqual(status, 0)
        self.assertTrue(runner.firewalld_runtime)
        self.assertTrue(runner.firewalld_permanent)

    def test_firewalld_preserves_existing_permanent_rule(self) -> None:
        runner = FakeRunner()
        runner.available = {"ip", "firewall-cmd"}
        runner.firewalld_permanent = True
        status = FIREWALL.configure_firewall(
            arguments(yes=True),
            runner=runner,
            input_function=lambda _: "n",
            platform_name="linux",
        )
        self.assertEqual(status, 0)
        self.assertTrue(runner.firewalld_runtime)
        permanent_adds = [
            call
            for call in runner.calls
            if "--permanent" in call[0] and "--add-rich-rule" in call[0]
        ]
        self.assertEqual(permanent_adds, [])


class WindowsOtaFirewallTests(unittest.TestCase):
    def test_detects_private_default_route_and_ignores_vpn(self) -> None:
        runner = FakeWindowsRunner()
        status = FIREWALL.configure_firewall(
            arguments(check=True),
            runner=runner,
            platform_name="win32",
        )
        self.assertEqual(status, 1)
        network_query = next(
            call[-1] for call in runner.calls if call[0] == "powershell.exe"
        )
        self.assertIn("JH:network-scope", network_query)

    def test_check_is_read_only(self) -> None:
        runner = FakeWindowsRunner()
        status = FIREWALL.configure_firewall(
            arguments(
                check=True,
                interface="Wi-Fi",
                network="192.168.2.0/24",
            ),
            runner=runner,
            platform_name="win32",
        )
        self.assertEqual(status, 1)
        self.assertFalse(runner.configured)
        self.assertFalse(any("JH:apply-rule" in call[-1] for call in runner.calls))

    def test_dry_run_is_read_only(self) -> None:
        runner = FakeWindowsRunner(elevated=True)
        status = FIREWALL.configure_firewall(
            arguments(
                dry_run=True,
                interface="Wi-Fi",
                network="192.168.2.0/24",
            ),
            runner=runner,
            input_function=lambda _: "y",
            platform_name="win32",
        )
        self.assertEqual(status, 0)
        self.assertFalse(runner.configured)
        self.assertFalse(any("JH:inspect-rule" in call[-1] for call in runner.calls))
        self.assertFalse(any("JH:apply-rule" in call[-1] for call in runner.calls))

    def test_inspection_accepts_windows_network_representations(self) -> None:
        scope = FIREWALL.NetworkScope(
            "Wi-Fi",
            ipaddress.IPv4Network("192.168.2.0/24"),
        )
        script = WINDOWS_FIREWALL.inspect_rule_script(scope, 8266)
        self.assertIn("192.168.2.0/24", script)
        self.assertIn("192.168.2.0/255.255.255.0", script)
        self.assertIn("192.168.2.0-192.168.2.255", script)
        self.assertIn("-ErrorAction Stop", script)

    def test_decline_is_read_only(self) -> None:
        runner = FakeWindowsRunner(elevated=True)
        status = FIREWALL.configure_firewall(
            arguments(interface="Wi-Fi", network="192.168.2.0/24"),
            runner=runner,
            input_function=lambda _: "n",
            platform_name="win32",
        )
        self.assertEqual(status, 0)
        self.assertFalse(runner.configured)

    def test_requires_elevated_shell_after_consent(self) -> None:
        runner = FakeWindowsRunner()
        with self.assertRaisesRegex(FIREWALL.SetupError, "administrator"):
            FIREWALL.configure_firewall(
                arguments(
                    yes=True,
                    interface="Wi-Fi",
                    network="192.168.2.0/24",
                ),
                runner=runner,
                platform_name="win32",
            )
        self.assertFalse(runner.configured)

    def test_applies_idempotent_private_lan_rule(self) -> None:
        runner = FakeWindowsRunner(elevated=True)
        args = arguments(
            yes=True,
            interface="Wi-Fi",
            network="192.168.2.0/24",
        )
        self.assertEqual(
            FIREWALL.configure_firewall(
                args,
                runner=runner,
                platform_name="win32",
            ),
            0,
        )
        self.assertTrue(runner.configured)
        apply_script = next(
            call[-1] for call in runner.calls if "JH:apply-rule" in call[-1]
        )
        self.assertIn("Profile = 'Private'", apply_script)
        self.assertIn("RemoteAddress = '192.168.2.0/24'", apply_script)
        self.assertIn("InterfaceAlias = 'Wi-Fi'", apply_script)
        call_count = len(runner.calls)
        self.assertEqual(
            FIREWALL.configure_firewall(
                args,
                runner=runner,
                platform_name="win32",
            ),
            0,
        )
        self.assertEqual(len(runner.calls), call_count + 2)

    def test_refuses_existing_listener(self) -> None:
        runner = FakeWindowsRunner(elevated=True)
        runner.listener = True
        with self.assertRaisesRegex(FIREWALL.SetupError, "already used"):
            FIREWALL.configure_firewall(
                arguments(
                    yes=True,
                    interface="Wi-Fi",
                    network="192.168.2.0/24",
                ),
                runner=runner,
                platform_name="win32",
            )
        self.assertFalse(runner.configured)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
