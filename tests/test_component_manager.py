#!/usr/bin/env python3
"""Exercise the cross-platform pinned component manager."""

from __future__ import annotations

import hashlib
import importlib.util
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock
import zipfile


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
MODULE_PATH = ROOT / "scripts/component_manager.py"
SPEC = importlib.util.spec_from_file_location("jh_component_manager", MODULE_PATH)
assert SPEC and SPEC.loader
manager = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = manager
SPEC.loader.exec_module(manager)


def git(directory: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", *arguments], cwd=directory, check=True,
        capture_output=True, text=True,
    )
    return result.stdout.strip()


class GitManagerTests(unittest.TestCase):
    def test_exact_ref_origin_cleanliness_and_idempotency(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-component-git-") as text:
            temporary = Path(text)
            upstream = temporary / "upstream"
            checkout = temporary / "checkout"
            upstream.mkdir()
            git(upstream, "init", "-q")
            git(upstream, "config", "user.name", "JaszczurHAL Test")
            git(upstream, "config", "user.email", "test@jaszczurhal.invalid")
            (upstream / "payload.txt").write_text("one\n", encoding="utf-8")
            git(upstream, "add", "payload.txt")
            git(upstream, "commit", "-q", "-m", "one")
            first_ref = git(upstream, "rev-parse", "HEAD")
            (upstream / "payload.txt").write_text("two\n", encoding="utf-8")
            git(upstream, "commit", "-q", "-am", "two")
            second_ref = git(upstream, "rev-parse", "HEAD")
            repository = upstream.resolve().as_uri()

            changed = manager.sync_git_checkout(
                repository, first_ref, checkout,
                clean=True, required_paths=("payload.txt",),
            )
            self.assertTrue(changed)
            self.assertEqual(git(checkout, "rev-parse", "HEAD"), first_ref)
            self.assertFalse(
                manager.sync_git_checkout(
                    repository, first_ref, checkout, verify_only=True,
                    clean=True, required_paths=("payload.txt",),
                )
            )

            (checkout / "payload.txt").write_text("dirty\n", encoding="utf-8")
            with self.assertRaises(manager.ComponentError):
                manager.sync_git_checkout(
                    repository, first_ref, checkout, verify_only=True, clean=True,
                )
            self.assertTrue(
                manager.sync_git_checkout(repository, first_ref, checkout, clean=True)
            )
            self.assertEqual((checkout / "payload.txt").read_text(), "one\n")

            alternate = (temporary / "alternate").resolve().as_uri()
            git(checkout, "remote", "set-url", "origin", alternate)
            with self.assertRaises(manager.ComponentError):
                manager.sync_git_checkout(
                    repository, first_ref, checkout, verify_only=True,
                )
            self.assertTrue(manager.sync_git_checkout(repository, first_ref, checkout))
            self.assertEqual(git(checkout, "remote", "get-url", "origin"), repository)

            with self.assertRaises(manager.ComponentError):
                manager.sync_git_checkout(
                    repository, second_ref, checkout, verify_only=True,
                )
            self.assertTrue(manager.sync_git_checkout(repository, second_ref, checkout))
            self.assertEqual(git(checkout, "rev-parse", "HEAD"), second_ref)


class ArchiveManagerTests(unittest.TestCase):
    def test_windows_https_download_uses_system_schannel_and_hashes_payload(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-component-schannel-") as text:
            temporary = Path(text)
            system_root = temporary / "Windows"
            curl = system_root / "System32/curl.exe"
            curl.parent.mkdir(parents=True)
            curl.write_bytes(b"fixture")
            destination = temporary / "download.zip"
            payload = b"authenticated archive"
            observed: list[str] = []

            def run(
                command: tuple[str, ...], **_kwargs: object
            ) -> subprocess.CompletedProcess[str]:
                observed.extend(command)
                output = Path(command[command.index("--output") + 1])
                output.write_bytes(payload)
                return subprocess.CompletedProcess(command, 0, "", "")

            with (
                mock.patch.object(manager.sys, "platform", "win32"),
                mock.patch.dict(manager.os.environ, {"SystemRoot": str(system_root)}),
                mock.patch.object(manager.subprocess, "run", side_effect=run),
            ):
                digest = manager._download(
                    "https://example.invalid/tool.zip", destination
                )

            self.assertEqual(digest, hashlib.sha256(payload).hexdigest())
            self.assertEqual(observed[0], str(curl))
            self.assertIn("--fail", observed)
            self.assertIn("--location", observed)
            self.assertIn("--tlsv1.2", observed)
            self.assertIn("=https", observed)

    def _exercise_archive(self, archive: Path, payload: str) -> None:
        destination = archive.parent / f"install-{archive.suffix.replace('.', '')}"
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        url = archive.resolve().as_uri()
        self.assertTrue(
            manager.sync_archive(
                url, digest, destination, version_stamp="fixture-v1"
            )
        )
        self.assertEqual(
            (destination / "package/payload.txt").read_text(encoding="utf-8"),
            payload,
        )
        self.assertFalse(
            manager.sync_archive(
                url, digest, destination, verify_only=True,
                version_stamp="fixture-v1",
            )
        )
        (destination / "package/payload.txt").write_text("damaged\n", encoding="utf-8")
        with self.assertRaises(manager.ComponentError):
            manager.sync_archive(
                url, digest, destination, verify_only=True,
                version_stamp="fixture-v1",
            )
        self.assertTrue(
            manager.sync_archive(
                url, digest, destination, version_stamp="fixture-v1"
            )
        )
        self.assertEqual(
            (destination / "package/payload.txt").read_text(encoding="utf-8"),
            payload,
        )

    def test_zip_without_posix_mode_and_tar_gz(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-component-archive-") as text:
            temporary = Path(text)
            zip_path = temporary / "fixture.zip"
            with zipfile.ZipFile(zip_path, "w") as package:
                item = zipfile.ZipInfo("package/payload.txt")
                item.external_attr = 0
                package.writestr(item, "zip payload\n")
            self._exercise_archive(zip_path, "zip payload\n")

            nupkg_path = temporary / "fixture.nupkg"
            with zipfile.ZipFile(nupkg_path, "w") as package:
                package.writestr("package/payload.txt", "nupkg payload\n")
            self._exercise_archive(nupkg_path, "nupkg payload\n")

            source = temporary / "source"
            (source / "package").mkdir(parents=True)
            (source / "package/payload.txt").write_text(
                "tar payload\n", encoding="utf-8"
            )
            tar_path = temporary / "fixture.tar.gz"
            with tarfile.open(tar_path, "w:gz") as package:
                package.add(source / "package", arcname="package")
            self._exercise_archive(tar_path, "tar payload\n")

    @unittest.skipIf(sys.platform == "win32", "fixture requires POSIX symlinks")
    def test_safe_tar_symlink_is_manifested(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-component-link-") as text:
            temporary = Path(text)
            source = temporary / "source"
            source.mkdir()
            (source / "compiler").write_text("binary\n", encoding="utf-8")
            (source / "compiler-alias").symlink_to("compiler")
            archive = temporary / "fixture.tar.gz"
            with tarfile.open(archive, "w:gz", dereference=False) as package:
                package.add(source / "compiler", arcname="bin/compiler")
                package.add(source / "compiler-alias", arcname="bin/compiler-alias")
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            destination = temporary / "install"
            manager.sync_archive(archive.resolve().as_uri(), digest, destination)
            self.assertTrue((destination / "bin/compiler-alias").is_symlink())
            manifest = (destination / manager.MANIFEST_FILE).read_text(encoding="utf-8")
            self.assertIn("link:", manifest)
            self.assertFalse(
                manager.sync_archive(
                    archive.resolve().as_uri(), digest, destination, verify_only=True
                )
            )

    def test_checksum_and_path_traversal_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-component-invalid-") as text:
            temporary = Path(text)
            archive = temporary / "fixture.zip"
            with zipfile.ZipFile(archive, "w") as package:
                package.writestr("../escape.txt", "no\n")
            with self.assertRaises(manager.ComponentError):
                manager.sync_archive(
                    archive.resolve().as_uri(),
                    hashlib.sha256(archive.read_bytes()).hexdigest(),
                    temporary / "install",
                )
            self.assertFalse((temporary / "escape.txt").exists())
            with self.assertRaises(manager.ComponentError):
                manager.sync_archive(
                    archive.resolve().as_uri(), "0" * 64, temporary / "other"
                )

    def test_modified_version_stamp_is_detected(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-component-stamp-") as text:
            temporary = Path(text)
            archive = temporary / "fixture.zip"
            with zipfile.ZipFile(archive, "w") as package:
                package.writestr("payload.txt", "payload\n")
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            destination = temporary / "install"
            manager.sync_archive(
                archive.resolve().as_uri(), digest, destination,
                version_stamp="expected",
            )
            (destination / manager.VERSION_STAMP).write_text(
                "damaged\n", encoding="utf-8"
            )
            with self.assertRaises(manager.ComponentError):
                manager.sync_archive(
                    archive.resolve().as_uri(), digest, destination,
                    verify_only=True, version_stamp="expected",
                )

    def test_explicit_invalid_windows_member_exclusion_is_manifested(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-component-exclusion-") as text:
            temporary = Path(text)
            archive = temporary / "fixture.zip"
            invalid = "scripts/target/?invalid?.cfg"
            with zipfile.ZipFile(archive, "w") as package:
                package.writestr("bin/tool.exe", "fixture\n")
                package.writestr(invalid, "unsupported target\n")
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            destination = temporary / "install"
            manager.sync_archive(
                archive.resolve().as_uri(), digest, destination,
                excluded_members=(invalid,),
            )
            self.assertFalse((destination / invalid).exists())
            self.assertEqual(
                (destination / manager.EXCLUSIONS_FILE).read_text(encoding="utf-8"),
                invalid + "\n",
            )
            self.assertFalse(
                manager.sync_archive(
                    archive.resolve().as_uri(), digest, destination,
                    verify_only=True, excluded_members=(invalid,),
                )
            )
            with self.assertRaises(manager.ComponentError):
                manager.sync_archive(
                    archive.resolve().as_uri(), digest, temporary / "wrong",
                    excluded_members=("missing",),
                )


class TrackedContractTests(unittest.TestCase):
    def test_freertos_component_rejects_disabled_feature_spelling(self) -> None:
        parser = manager.build_parser()
        arguments = parser.parse_args(
            ["component", "freertos", "--repo-root", str(ROOT)]
        )
        with mock.patch.dict(
            manager.os.environ,
            {"EXTRA_HAL_DEFINES": "HAL_ENABLE_WIFI=0"},
            clear=True,
        ):
            with self.assertRaisesRegex(manager.ComponentError, "JH-CFG-VALUE"):
                manager._component_enabled("freertos", arguments)
        with mock.patch.dict(
            manager.os.environ,
            {"EXTRA_HAL_DEFINES": "$<1:HAL_$<1:ENABLE>_FREERTOS=0>"},
            clear=True,
        ):
            with self.assertRaisesRegex(manager.ComponentError, "JH-CFG-VALUE"):
                manager._component_enabled("freertos", arguments)
        with mock.patch.dict(
            manager.os.environ,
            {"EXTRA_HAL_DEFINES": "HAL_ENABLE_FREERTOS = 0"},
            clear=True,
        ):
            with self.assertRaisesRegex(manager.ComponentError, "JH-CFG-VALUE"):
                manager._component_enabled("freertos", arguments)
        with mock.patch.dict(
            manager.os.environ,
            {"HAL_ENABLE_FREERTOS": "0"},
            clear=True,
        ):
            with self.assertRaisesRegex(manager.ComponentError, "JH-CFG-VALUE"):
                manager._component_enabled("freertos", arguments)
        with mock.patch.dict(
            manager.os.environ,
            {"EXTRA_HAL_DEFINES": "HAL_ENABLE_FREERTOS=1"},
            clear=True,
        ):
            self.assertTrue(manager._component_enabled("freertos", arguments))

    @unittest.skipIf(sys.platform == "win32", "fixture requires POSIX scripts")
    def test_static_build_scripts_reject_disabled_feature_spelling(self) -> None:
        for script in ("build_rp_native_lib.sh", "build_stm32_lib.sh"):
            for definition in (
                "HAL_ENABLE_WIFI=0",
                "$<1:HAL_$<1:ENABLE>_WIFI=0>",
            ):
                result = subprocess.run(
                    [str(ROOT / "scripts" / script), "-D", definition],
                    check=False,
                    capture_output=True,
                    text=True,
                )
                self.assertNotEqual(result.returncode, 0, script)
                self.assertIn("[JH-CFG-VALUE]", result.stderr, script)

    def test_windows_and_riscv_pins_are_complete(self) -> None:
        windows = manager.parse_config(ROOT / "third_party/windows_tools_version.conf")
        for prefix in ("PYTHON", "CMAKE", "NINJA", "GNU_ARM", "OPENOCD", "PICOTOOL"):
            for suffix in ("VERSION", "URL", "SHA256", "EXECUTABLE"):
                self.assertTrue(windows[f"WINDOWS_{prefix}_{suffix}"])
            self.assertRegex(windows[f"WINDOWS_{prefix}_SHA256"], r"^[0-9a-f]{64}$")
        riscv = manager.parse_config(ROOT / "third_party/riscv_toolchain_version.conf")
        for suffix in ("X86_64_LIN", "AARCH64_LIN", "X64_WIN"):
            self.assertRegex(
                riscv[f"RISCV_TOOLCHAIN_SHA256_{suffix}"], r"^[0-9a-f]{64}$"
            )

    def test_cli_keeps_legacy_flags_and_custom_directory(self) -> None:
        parser = manager.build_parser()
        arguments = parser.parse_args(
            [
                "component", "pico-sdk", "--force", "--verify-only",
                "--repo-root", str(ROOT), "--sdk-dir", "custom sdk",
                "--with-submodules", "lib/tinyusb lib/mbedtls",
            ]
        )
        self.assertTrue(arguments.force)
        self.assertTrue(arguments.verify_only)
        self.assertEqual(arguments.sdk_dir, "custom sdk")

        windows_tools = parser.parse_args(
            [
                "windows-tools", "--prefer-managed",
                "--tools-root", "tools", "--build-root", "build",
            ]
        )
        self.assertTrue(windows_tools.prefer_managed)

    def test_picotool_capabilities_are_symmetric_and_dependency_aware(self) -> None:
        def fake_run(command, **_kwargs):
            if command[1] == "version":
                return subprocess.CompletedProcess(
                    command, 0, "picotool v2.2.0 (without USB support)\n", ""
                )
            if command[1] == "help":
                return subprocess.CompletedProcess(
                    command, 0, "commands: load verify reboot\n", ""
                )
            raise AssertionError(command)

        with mock.patch.object(manager, "_run", side_effect=fake_run):
            self.assertEqual(
                manager._picotool_capability_issues(
                    Path("picotool"), require_usb=True, require_signing=True
                ),
                ["USB support", "'seal' command"],
            )
            self.assertEqual(
                manager._picotool_capability_issues(
                    Path("picotool"), require_usb=False, require_signing=False
                ),
                [],
            )

    @unittest.skipIf(sys.platform == "win32", "fixture uses a POSIX shell executable")
    def test_linux_picotool_rebuilds_when_new_capabilities_are_available(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-picotool-repair-") as text:
            root = Path(text)
            third_party = root / "third_party"
            third_party.mkdir()
            (third_party / "picotool_version.conf").write_text(
                "PICOTOOL_REPO=https://example.invalid/picotool.git\n"
                "PICOTOOL_REF=0123456789abcdef\n"
                "PICOTOOL_VERSION=2.2.0\n"
                "PICOTOOL_DIR=third_party/picotool\n",
                encoding="utf-8",
            )
            (third_party / "pico_sdk_version.conf").write_text(
                "PICO_SDK_DIR=third_party/pico-sdk\n",
                encoding="utf-8",
            )
            source = third_party / "picotool"
            source.mkdir()
            (third_party / "pico-sdk/lib/mbedtls/library").mkdir(parents=True)
            build = root / ".build/tools/picotool"
            build.mkdir(parents=True)
            binary = build / "picotool"

            def write_binary(*, usb: bool, signing: bool) -> None:
                version = "picotool v2.2.0" + ("" if usb else " (without USB support)")
                commands = "load verify reboot" + (" seal" if signing else "")
                binary.write_text(
                    "#!/bin/sh\n"
                    f"if [ \"$1\" = version ]; then echo '{version}'; "
                    f"else echo '{commands}'; fi\n",
                    encoding="utf-8",
                )
                binary.chmod(0o755)

            write_binary(usb=False, signing=False)
            real_run = manager._run
            cmake_calls = []

            def fake_run(command, **kwargs):
                if command[0] == "cmake":
                    cmake_calls.append(tuple(command))
                    if "--build" in command:
                        build.mkdir(parents=True, exist_ok=True)
                        write_binary(usb=True, signing=True)
                    return subprocess.CompletedProcess(command, 0, "", "")
                return real_run(command, **kwargs)

            with mock.patch.object(manager, "sync_git_checkout", return_value=False), \
                    mock.patch.object(
                        manager, "_libusb_build_support_available", return_value=True
                    ), \
                    mock.patch.object(manager, "_run", side_effect=fake_run):
                resolved = manager.ensure_picotool_linux(root, verify_only=False)

            self.assertEqual(resolved, binary)
            self.assertEqual(len(cmake_calls), 2)
            self.assertEqual(
                manager._picotool_capability_issues(
                    binary, require_usb=True, require_signing=True
                ),
                [],
            )

    def test_openocd_without_required_scripts_falls_back_to_managed(self) -> None:
        spec = manager.ToolArchive(
            "openocd", "0.12.0", "https://example.invalid/openocd.zip",
            "0" * 64, "openocd.exe", ("--version",), lambda output: "0.12" in output,
        )
        with tempfile.TemporaryDirectory(prefix="jh-openocd-system-") as text:
            root = Path(text)
            executable = root / "bin/openocd.exe"
            executable.parent.mkdir()
            executable.write_bytes(b"")
            with mock.patch.object(manager.shutil, "which", return_value=str(executable)), \
                    mock.patch.object(manager, "_tool_output", return_value="OpenOCD 0.12"):
                self.assertIsNone(manager._system_tool(spec))

                scripts = root / "share/openocd/scripts"
                (scripts / "board").mkdir(parents=True)
                (scripts / "interface").mkdir(parents=True)
                (scripts / "target").mkdir()
                (scripts / "board/st_nucleo_g4.cfg").write_text("board\n")
                (scripts / "interface/stlink.cfg").write_text("adapter\n")
                (scripts / "target/stm32g4x.cfg").write_text("target\n")
                (scripts / "interface/cmsis-dap.cfg").write_text("adapter\n")
                (scripts / "target/rp2040.cfg").write_text("target\n")
                (scripts / "target/rp2350.cfg").write_text("target\n")
                self.assertEqual(manager._system_tool(spec), executable.resolve())

    def test_arm_cstdlib_probe_selects_rp2040_multilib(self) -> None:
        commands = []

        def fake_run(command, **_kwargs):
            commands.append(tuple(command))
            output = Path(command[command.index("-o") + 1])
            output.write_bytes(b"object")
            return subprocess.CompletedProcess(command, 0, "", "")

        with tempfile.TemporaryDirectory(prefix="jh-arm-probe-") as text, \
                mock.patch.object(manager, "_run", side_effect=fake_run):
            manager._arm_cstdlib_self_check(Path("arm-none-eabi-g++"), Path(text))
        self.assertIn("-mcpu=cortex-m0plus", commands[0])
        self.assertIn("-mthumb", commands[0])
        self.assertIn("-mfloat-abi=soft", commands[0])

    def test_windows_bootstrap_is_consent_gated_and_read_only_on_verify(self) -> None:
        bootstrap = (ROOT / "runmefirst.ps1").read_text(encoding="utf-8")
        unix_bootstrap = (ROOT / "runmefirst.sh").read_text(encoding="utf-8")
        inventory = (ROOT / "scripts/windows_host_inventory.ps1").read_text(
            encoding="utf-8"
        )
        workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        self.assertLess(
            bootstrap.index("JaszczurHAL native Windows setup plan"),
            bootstrap.index("\nPrepare-HostSettings\n"),
        )
        self.assertIn("[switch]$VerifyOnly", bootstrap)
        self.assertIn("[switch]$ConfigureHost", bootstrap)
        self.assertIn("[switch]$InstallExtensions", bootstrap)
        self.assertIn("[switch]$FirmwareOnly", bootstrap)
        self.assertIn("$inventoryArguments += '-FirmwareOnly'", bootstrap)
        self.assertIn("configure_cortex_debug.py", bootstrap)
        self.assertIn("if (-not $FirmwareOnly)", bootstrap)
        self.assertIn("$cortexDebugArguments += '--check'", bootstrap)
        self.assertIn("$cortexDebugArguments += '--yes'", bootstrap)
        self.assertIn("The script never elevates itself", bootstrap)
        self.assertIn("WSL UNC paths are not supported", bootstrap)
        self.assertNotIn("-Verb RunAs", bootstrap)
        self.assertIn("--require-hashes", bootstrap)
        self.assertIn("source-components", bootstrap)
        self.assertIn("windows-tools", bootstrap)
        self.assertIn("--prefer-managed", bootstrap)
        self.assertIn("$MIN_CMAKE       = [version]'3.20'", inventory)
        self.assertIn("CurrentBuildNumber", inventory)
        self.assertIn("[switch]$FirmwareOnly", inventory)
        self.assertIn("$editorCategory", inventory)
        self.assertIn("build-essential cmake ninja-build", unix_bootstrap)
        self.assertIn("for tool in cmake ninja", unix_bootstrap)
        self.assertGreaterEqual(
            workflow.count("-ConfigureHost -FirmwareOnly"),
            2,
        )
        self.assertIn(
            ".\\runmefirst.ps1 -VerifyOnly -Force -FirmwareOnly",
            workflow,
        )
        self.assertIn("include-hidden-files: true", workflow)
        self.assertIn("Enter-VsDevShell", workflow)
        self.assertIn("-G Ninja", workflow)
        self.assertIn("compile_commands.json", workflow)
        self.assertIn("test_host_compiler_smoke.cpp", workflow)
        self.assertIn("@('/W4', '/permissive-', '/WX')", workflow)
        self.assertIn("--target test_host_compiler_smoke", workflow)

    def test_source_component_command_excludes_host_tool_archives(self) -> None:
        parser = manager.build_parser()
        arguments = parser.parse_args(
            ["source-components", "--verify-only", "--repo-root", str(ROOT)]
        )
        self.assertEqual(arguments.command, "source-components")
        self.assertTrue(arguments.verify_only)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
