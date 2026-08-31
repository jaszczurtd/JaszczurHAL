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
    def test_verify_distinguishes_missing_and_non_git_checkouts(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-component-state-") as text:
            temporary = Path(text)
            missing = temporary / "missing"
            repository = (temporary / "upstream").resolve().as_uri()
            with self.assertRaisesRegex(
                manager.ComponentError, r"found missing checkout\."
            ):
                manager.sync_git_checkout(
                    repository, "a" * 40, missing, verify_only=True
                )

            non_git = temporary / "non-git"
            non_git.mkdir()
            with self.assertRaisesRegex(
                manager.ComponentError, r"found non-git directory\."
            ):
                manager.sync_git_checkout(
                    repository, "a" * 40, non_git, verify_only=True
                )

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

    def test_pmd_archive_is_version_checked_and_resolved(self) -> None:
        self.assertIn("pmd", manager.SOURCE_COMPONENT_ORDER)
        with tempfile.TemporaryDirectory(prefix="jh-pmd-component-") as text:
            root = Path(text)
            third_party = root / "third_party"
            third_party.mkdir()
            (third_party / "pmd_version.conf").write_text(
                "PMD_VERSION=7.26.0\n"
                "PMD_URL=https://example.invalid/pmd.zip\n"
                f"PMD_SHA256={'a' * 64}\n"
                "PMD_DIR=third_party/pmd\n",
                encoding="utf-8",
            )
            executable_name = "pmd.bat" if sys.platform == "win32" else "pmd"

            def sync_archive(
                _url: str,
                _digest: str,
                destination: Path,
                **_kwargs: object,
            ) -> bool:
                binary = destination / "pmd-bin-7.26.0/bin" / executable_name
                binary.parent.mkdir(parents=True)
                binary.write_text("launcher\n", encoding="utf-8")
                return True

            version = subprocess.CompletedProcess(
                [executable_name, "--version"], 0, "PMD 7.26.0\n", ""
            )
            with (
                mock.patch.object(manager, "sync_archive", side_effect=sync_archive),
                mock.patch.object(manager, "_run", return_value=version),
            ):
                executable = manager.ensure_pmd(root, verify_only=False)

            self.assertEqual(executable_name, executable.name)
            if sys.platform != "win32":
                self.assertTrue(executable.stat().st_mode & 0o111)

    @unittest.skipIf(sys.platform == "win32", "POSIX launcher mode test")
    def test_pmd_verify_only_does_not_change_launcher_mode(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-pmd-verify-") as text:
            root = Path(text)
            third_party = root / "third_party"
            third_party.mkdir()
            (third_party / "pmd_version.conf").write_text(
                "PMD_VERSION=7.26.0\n"
                "PMD_URL=https://example.invalid/pmd.zip\n"
                f"PMD_SHA256={'a' * 64}\n"
                "PMD_DIR=third_party/pmd\n",
                encoding="utf-8",
            )
            executable = third_party / "pmd/pmd-bin-7.26.0/bin/pmd"
            executable.parent.mkdir(parents=True)
            executable.write_text("launcher\n", encoding="utf-8")
            executable.chmod(0o644)
            version = subprocess.CompletedProcess(
                [str(executable), "--version"], 0, "PMD 7.26.0\n", ""
            )
            with (
                mock.patch.object(manager, "sync_archive", return_value=False),
                mock.patch.object(manager, "_run", return_value=version),
            ):
                manager.ensure_pmd(root, verify_only=True)

            self.assertEqual(0, executable.stat().st_mode & 0o111)

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
    def test_esp_idf_pin_is_exact_and_opt_in(self) -> None:
        config = manager.parse_config(ROOT / "third_party/esp_idf_version.conf")
        self.assertEqual(
            config,
            {
                "ESP_IDF_REPO": "https://github.com/espressif/esp-idf.git",
                "ESP_IDF_REF":
                    "7101770dc6db2667b3c477cc31365dd1acd6db4e",
                "ESP_IDF_VERSION": "6.0.2",
                "ESP_IDF_DIR": "third_party/esp-idf",
                "ESP_IDF_TARGETS": "esp32,esp32s3",
            },
        )
        spec = manager.GIT_COMPONENTS["esp-idf"]
        self.assertTrue(spec.recursive_submodules)
        self.assertIs(spec.version_validator, manager._version_esp_idf)
        self.assertNotIn("esp-idf", manager.SOURCE_COMPONENT_ORDER)

        arguments = manager.build_parser().parse_args(
            ["component", "esp-idf", "--repo-root", str(ROOT)]
        )
        with mock.patch.dict(manager.os.environ, {}, clear=True):
            self.assertFalse(manager._component_enabled("esp-idf", arguments))
        with mock.patch.dict(
            manager.os.environ, {"JH_ENABLE_ESP_IDF": "1"}, clear=True
        ):
            self.assertTrue(manager._component_enabled("esp-idf", arguments))

        with self.assertRaisesRegex(
            manager.ComponentError, "complete recursive submodule set"
        ):
            manager.ensure_git_component(
                "esp-idf", ROOT, verify_only=True, submodule_override=""
            )

    def test_esp_idf_tools_install_and_verify_official_environment(self) -> None:
        directory = ROOT / "third_party/esp-idf"
        completed = subprocess.CompletedProcess([], 0, "", "")
        with (
            mock.patch.object(manager, "_run", return_value=completed) as run,
            mock.patch.object(manager, "_synchronize_esp_idf_python_tools"),
        ):
            manager.ensure_esp_idf_tools(
                ROOT, directory, verify_only=False
            )

        commands = [call.args[0] for call in run.call_args_list]
        installer = (
            "cmd.exe", "/d", "/s", "/c", str(directory / "install.bat"),
            "esp32,esp32s3",
        ) if sys.platform == "win32" else (
            "bash", str(directory / "install.sh"), "esp32,esp32s3",
        )
        self.assertEqual(installer, commands[0])
        self.assertEqual("check", commands[1][-1])
        self.assertEqual("check-python-dependencies", commands[2][-1])

        with (
            mock.patch.object(manager, "_run", return_value=completed) as run,
            mock.patch.object(manager, "_synchronize_esp_idf_python_tools"),
        ):
            manager.ensure_esp_idf_tools(
                ROOT, directory, verify_only=True
            )
        self.assertEqual(
            ["check", "check-python-dependencies"],
            [call.args[0][-1] for call in run.call_args_list],
        )

    def test_esp_idf_python_tools_are_pinned_by_reviewed_snapshot(self) -> None:
        pins = dict(manager._esp_idf_python_tool_pins(ROOT))
        self.assertEqual(pins["esp-coredump"], "1.16.0")
        self.assertEqual(pins["esptool"], "5.3.1")
        self.assertEqual(len(pins), 11)

    def test_esp_idf_python_tool_drift_is_rejected_in_verify_mode(self) -> None:
        pins = (("esp-coredump", "1.16.0"), ("esptool", "5.3.1"))
        with (
            mock.patch.object(
                manager, "_esp_idf_python_tool_pins", return_value=pins
            ),
            mock.patch.object(
                manager,
                "_esp_idf_python_environment",
                return_value=(Path("python"), Path("constraints.txt")),
            ),
            mock.patch.object(
                manager,
                "_query_esp_idf_python_tools",
                return_value={"esp-coredump": "1.17.0", "esptool": "5.3.1"},
            ),
        ):
            with self.assertRaisesRegex(
                manager.ComponentError,
                r"esp-coredump: expected 1\.16\.0, found 1\.17\.0",
            ):
                manager._synchronize_esp_idf_python_tools(
                    ROOT, {"ESP_IDF_VERSION": "6.0.2"}, verify_only=True
                )

    def test_esp_idf_python_tool_drift_is_repaired_after_install(self) -> None:
        pins = (("esp-coredump", "1.16.0"), ("esptool", "5.3.1"))
        with tempfile.TemporaryDirectory(prefix="jh-idf-python-pins-") as text:
            temporary = Path(text)
            constraints = temporary / "espidf.constraints.v6.0.txt"
            constraints.write_text("esp-coredump~=1.14\n", encoding="utf-8")
            versions = (
                {"esp-coredump": "1.17.0", "esptool": "5.3.1"},
                {"esp-coredump": "1.16.0", "esptool": "5.3.1"},
            )
            with (
                mock.patch.object(
                    manager, "_esp_idf_python_tool_pins", return_value=pins
                ),
                mock.patch.object(
                    manager,
                    "_esp_idf_python_environment",
                    return_value=(temporary / "python", constraints),
                ),
                mock.patch.object(
                    manager,
                    "_query_esp_idf_python_tools",
                    side_effect=versions,
                ),
                mock.patch.object(
                    manager,
                    "_run",
                    return_value=subprocess.CompletedProcess([], 0, "", ""),
                ) as run,
            ):
                manager._synchronize_esp_idf_python_tools(
                    ROOT, {"ESP_IDF_VERSION": "6.0.2"}, verify_only=False
                )

        command = run.call_args.args[0]
        self.assertIn("--constraint", command)
        self.assertIn("esp-coredump==1.16.0", command)
        self.assertIn("esptool==5.3.1", command)

    def test_recursive_submodule_sync_is_verified(self) -> None:
        directory = Path("esp-idf")
        responses = iter(("-deadbeef component", "", " deadbeef component"))
        with mock.patch.object(
            manager,
            "_git_output",
            side_effect=lambda *_args, **_kwargs: next(responses),
        ) as git_output:
            manager._sync_submodules(
                directory, (), verify_only=False, recursive=True
            )
        self.assertIn(
            mock.call(
                directory, "submodule", "update", "--init", "--recursive",
                "--depth", "1",
            ),
            git_output.mock_calls,
        )

    def test_sx126x_pin_and_license_are_exact(self) -> None:
        config = manager.parse_config(
            ROOT / "third_party/sx126x_driver_version.conf"
        )
        self.assertEqual(
            config,
            {
                "SX126X_DRIVER_REPO":
                    "https://github.com/Lora-net/sx126x_driver.git",
                "SX126X_DRIVER_REF":
                    "a10c5dfdf89788c6ac805e9fe98889de44175aa2",
                "SX126X_DRIVER_VERSION": "v2.5.0",
                "SX126X_DRIVER_DIR": "third_party/sx126x_driver",
            },
        )
        spec = manager.GIT_COMPONENTS["sx126x"]
        self.assertTrue(spec.clean)
        self.assertEqual(spec.prefix, "SX126X_DRIVER")
        self.assertIn("LICENSE.txt", spec.required_paths)
        self.assertIn("src/sx126x_hal.h", spec.required_paths)
        self.assertIn("src/sx126x_driver_version.c", spec.required_paths)
        self.assertIn("src/sx126x_status.h", spec.required_paths)
        self.assertIn("sx126x", manager.SOURCE_COMPONENT_ORDER)
        license_text = (ROOT / "third_party/LICENSE.SX126X").read_text(
            encoding="utf-8"
        )
        self.assertIn("The Clear BSD License", license_text)
        self.assertIn("NO EXPRESS OR IMPLIED LICENSES", license_text)

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
        self.assertIn("config\\tooling\\artifacts.json", bootstrap)
        self.assertIn("$ComponentVersionStamp", bootstrap)
        self.assertNotIn("'.jaszczurhal-component-version'", bootstrap)
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
