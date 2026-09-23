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


from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
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
        # Rule state per protocol ("tcp", "udp") for each firewall manager.
        self.live_rules: set[str] = set()
        self.saved_rules: set[str] = set()
        self.ufw_rules: set[str] = set()
        self.firewalld_runtime: set[str] = set()
        self.firewalld_permanent: set[str] = set()
        self.tcp_listener = True
        self.udp_bound = False
        self.saved_text: str | None = None
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
            listener = "LISTEN 0 1 0.0.0.0:8266 0.0.0.0:*\n"
            return result(command, stdout=listener if self.tcp_listener else "")
        if command == ["ss", "-H", "-lun", "sport = :8266"]:
            bound = "UNCONN 0 0 0.0.0.0:8266 0.0.0.0:*\n"
            return result(command, stdout=bound if self.udp_bound else "")
        if command[:2] == ["ufw", "status"]:
            return result(command, stdout="Status: active\n")
        if command[:3] == ["ufw", "show", "added"]:
            added = "".join(
                f"ufw allow in on enp7s0 proto {protocol} from 192.168.2.0/24 "
                f"to any port 8266 comment '{FIREWALL.RULE_COMMENTS[protocol]}'\n"
                for protocol in sorted(self.ufw_rules)
            )
            return result(command, stdout=added)
        if command[:2] == ["ufw", "allow"]:
            self.ufw_rules.add(command[command.index("proto") + 1])
            return result(command)
        if command[:2] == ["firewall-cmd", "--state"]:
            return result(command, stdout="running\n")
        if command[:2] == ["firewall-cmd", "--get-zone-of-interface"]:
            return result(command, stdout="home\n")
        if "--query-rich-rule" in command:
            rules = (
                self.firewalld_permanent
                if "--permanent" in command
                else self.firewalld_runtime
            )
            configured = rich_rule_protocol(command[-1]) in rules
            return result(command, returncode=0 if configured else 1)
        if "--add-rich-rule" in command:
            rules = (
                self.firewalld_permanent
                if "--permanent" in command
                else self.firewalld_runtime
            )
            rules.add(rich_rule_protocol(command[-1]))
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
            protocol = command[command.index("-p") + 1]
            return result(command, returncode=0 if protocol in self.live_rules else 1)
        if (
            command
            and command[0] in {"iptables", "iptables-nft"}
            and "-I" in command
        ):
            self.live_rules.add(command[command.index("-p") + 1])
            return result(command)
        if (
            command
            and command[0] in {"iptables", "iptables-nft"}
            and "-D" in command
        ):
            self.live_rules.discard(command[command.index("-p") + 1])
            return result(command)
        if command == ["cat", "/etc/iptables/rules.v4"]:
            if self.saved_text is not None:
                return result(command, stdout=self.saved_text)
            saved = "".join(
                f"-A INPUT -i enp7s0 -s 192.168.2.0/24 -p {protocol} "
                f"--dport 8266 -m comment "
                f'--comment "{FIREWALL.RULE_COMMENTS[protocol]}" -j ACCEPT\n'
                for protocol in sorted(self.saved_rules)
            )
            return result(command, stdout=saved)
        if command in (
            ["iptables-save", "-f", "/etc/iptables/rules.v4"],
            ["iptables-nft-save", "-f", "/etc/iptables/rules.v4"],
        ):
            self.saved_rules = set(self.live_rules)
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


def rich_rule_protocol(rich_rule: str) -> str:
    return rich_rule.split('protocol="', 1)[1].split('"', 1)[0]


BOTH_PROTOCOLS = {"tcp", "udp"}


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
        self.assertEqual(runner.live_rules, set())
        self.assertEqual(runner.saved_rules, set())

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
        self.assertEqual(runner.saved_rules, set())

    def test_refuses_to_expose_an_existing_udp_socket(self) -> None:
        runner = FakeRunner()
        runner.available.add("ss")
        runner.tcp_listener = False
        runner.udp_bound = True
        with self.assertRaisesRegex(FIREWALL.SetupError, "UDP/8266"):
            FIREWALL.configure_firewall(
                arguments(yes=True),
                runner=runner,
                input_function=lambda _: "n",
                platform_name="linux",
            )
        self.assertEqual(runner.saved_rules, set())

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
        self.assertEqual(runner.live_rules, set())
        self.assertEqual(runner.saved_rules, set())

    def test_iptables_rule_is_scoped_and_persisted(self) -> None:
        runner = FakeRunner()
        status = FIREWALL.configure_firewall(
            arguments(yes=True),
            runner=runner,
            input_function=lambda _: "n",
            platform_name="linux",
        )
        self.assertEqual(status, 0)
        self.assertEqual(runner.live_rules, BOTH_PROTOCOLS)
        self.assertEqual(runner.saved_rules, BOTH_PROTOCOLS)
        inserted = [call[0] for call in runner.calls if "-I" in call[0]]
        self.assertEqual(
            sorted(command[command.index("-p") + 1] for command in inserted),
            ["tcp", "udp"],
        )
        for command in inserted:
            self.assertIn("INPUT", command)
            self.assertIn("enp7s0", command)
            self.assertIn("192.168.2.0/24", command)
            self.assertEqual(command[command.index("--dport") + 1], "8266")

    def test_iptables_persistence_requires_both_rules(self) -> None:
        runner = FakeRunner()
        scope = FIREWALL.detect_network_scope(runner)
        runner.live_rules = set(BOTH_PROTOCOLS)
        runner.saved_rules = {"tcp"}
        backend = FIREWALL.detect_firewall_backend(
            runner, scope, 8266, sudo_non_interactive=True
        )
        self.assertFalse(backend.configured)
        runner.saved_rules = set(BOTH_PROTOCOLS)
        backend = FIREWALL.detect_firewall_backend(
            runner, scope, 8266, sudo_non_interactive=True
        )
        self.assertTrue(backend.configured)
        # The saved protocol decides, not the rule label.
        runner.saved_text = "".join(
            f"-A INPUT -i enp7s0 -s 192.168.2.0/24 -p tcp --dport 8266 "
            f'-m comment --comment "{FIREWALL.RULE_COMMENTS[protocol]}" -j ACCEPT\n'
            for protocol in ("tcp", "udp")
        )
        backend = FIREWALL.detect_firewall_backend(
            runner, scope, 8266, sudo_non_interactive=True
        )
        self.assertFalse(backend.configured)

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
        self.assertEqual(runner.saved_rules, set())
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
        self.assertEqual(runner.saved_rules, BOTH_PROTOCOLS)
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
        self.assertEqual(runner.saved_rules, BOTH_PROTOCOLS)

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
        self.assertEqual(runner.saved_rules, set())

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
        self.assertEqual(runner.ufw_rules, BOTH_PROTOCOLS)
        self.assertFalse(any("-I" in call[0] for call in runner.calls))

    def test_ufw_callback_rule_alone_gains_the_discovery_rule(self) -> None:
        runner = FakeRunner()
        runner.available.add("ufw")
        runner.ufw_rules = {"tcp"}
        with redirect_stderr(io.StringIO()):
            missing = FIREWALL.configure_firewall(
                arguments(check=True),
                runner=runner,
                platform_name="linux",
            )
        self.assertEqual(missing, 1)
        status = FIREWALL.configure_firewall(
            arguments(yes=True),
            runner=runner,
            input_function=lambda _: "n",
            platform_name="linux",
        )
        self.assertEqual(status, 0)
        self.assertEqual(runner.ufw_rules, BOTH_PROTOCOLS)
        present = FIREWALL.configure_firewall(
            arguments(check=True),
            runner=runner,
            platform_name="linux",
        )
        self.assertEqual(present, 0)

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
        self.assertEqual(runner.firewalld_runtime, BOTH_PROTOCOLS)
        self.assertEqual(runner.firewalld_permanent, BOTH_PROTOCOLS)

    def test_firewalld_preserves_existing_permanent_rule(self) -> None:
        runner = FakeRunner()
        runner.available = {"ip", "firewall-cmd"}
        runner.firewalld_permanent = set(BOTH_PROTOCOLS)
        status = FIREWALL.configure_firewall(
            arguments(yes=True),
            runner=runner,
            input_function=lambda _: "n",
            platform_name="linux",
        )
        self.assertEqual(status, 0)
        self.assertEqual(runner.firewalld_runtime, BOTH_PROTOCOLS)
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
