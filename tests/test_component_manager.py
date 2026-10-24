#!/usr/bin/env python3
"""Exercise the cross-platform pinned component manager."""

from __future__ import annotations

import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import py_compile
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock
import zipfile


from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
MODULE_PATH = ROOT / "scripts/component_manager.py"
SPEC = importlib.util.spec_from_file_location("jh_component_manager", MODULE_PATH)
assert SPEC and SPEC.loader
manager = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = manager
SPEC.loader.exec_module(manager)


# Fixture roots ship their own jhbase layout; a developer's JH_JHBASE_DIR must
# not redirect them. The real-pin test restores the override explicitly.
_JHBASE_OVERRIDE = os.environ.pop("JH_JHBASE_DIR", None)


def write_supplied_pins(root: Path, **pins: str) -> Path:
    """Give ROOT a jhbase component layout that ships PINS; return its third_party."""
    third_party = root / "third_party"
    third_party.mkdir(exist_ok=True)
    (third_party / "jhbase_version.conf").write_text(
        "JHBASE_REPO=https://example.invalid/jhbase.git\n"
        f"JHBASE_REF={'0' * 40}\n"
        "JHBASE_VERSION=0.1.0\n"
        "JHBASE_DIR=third_party/jhbase\n",
        encoding="utf-8",
    )
    supplied = third_party / "jhbase/third_party"
    supplied.mkdir(parents=True, exist_ok=True)
    for keyword, text in pins.items():
        (supplied / keyword.replace("_conf", ".conf")).write_text(text, encoding="utf-8")
    return third_party


def git(directory: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", *arguments], cwd=directory, check=True,
        capture_output=True, text=True,
    )
    return result.stdout.strip()


def create_repository(path: Path, *, attributes: str = "*.txt text eol=crlf\n") -> str:
    path.mkdir()
    git(path, "init", "-q")
    git(path, "config", "user.name", "JaszczurHAL Test")
    git(path, "config", "user.email", "test@jaszczurhal.invalid")
    (path / ".gitattributes").write_text(attributes, encoding="utf-8")
    (path / "payload.txt").write_text("one\n", encoding="utf-8")
    (path / "source ż.txt").write_text("quoted path\n", encoding="utf-8")
    git(path, "add", ".")
    git(path, "commit", "-q", "-m", "fixture")
    return git(path, "rev-parse", "HEAD")


class GitManagerTests(unittest.TestCase):
    @unittest.skipIf(sys.platform == "win32", "fixture requires POSIX symlinks")
    def test_pinned_symlink_matches_blob_also_with_windows_link_separators(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            repository = root / "origin"
            repository.mkdir()
            git(repository, "init", "-q")
            git(repository, "config", "user.name", "JaszczurHAL Test")
            git(repository, "config", "user.email", "test@jaszczurhal.invalid")
            (repository / "docs").mkdir()
            (repository / "CODE_OF_CONDUCT.rst").write_text("conduct\n", encoding="utf-8")
            (repository / "docs" / "conduct.rst").symlink_to("../CODE_OF_CONDUCT.rst")
            git(repository, "add", ".")
            git(repository, "commit", "-q", "-m", "fixture")
            head = git(repository, "rev-parse", "HEAD")
            checkout = root / "checkout"
            git(root, "clone", "-q", str(repository), str(checkout))
            problem = manager._git_source_problem(checkout, str(repository), head)
            self.assertEqual("", problem)

            # Git for Windows stores the link target with backslashes; the blob
            # keeps '/'. The verifier must compare the Git form.
            real_readlink = os.readlink

            def windows_readlink(path, *arguments, **options):
                return real_readlink(path, *arguments, **options).replace("/", "\\")

            with mock.patch.object(manager, "_WINDOWS", True), \
                    mock.patch.object(manager.os, "readlink", windows_readlink):
                self.assertEqual("", manager._git_source_problem(checkout, str(repository), head))
            with mock.patch.object(manager, "_WINDOWS", False), \
                    mock.patch.object(manager.os, "readlink", windows_readlink):
                self.assertIn(
                    "source bytes differ",
                    manager._git_source_problem(checkout, str(repository), head),
                )

    def test_text_auto_respects_core_eol_without_relaxing_binary_or_lf_checks(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-component-eol-") as text:
            root = Path(text)
            config = root / "gitconfig"
            config.write_text("[core]\n\teol = crlf\n\tautocrlf = false\n", encoding="utf-8")
            with mock.patch.dict(os.environ, {"GIT_CONFIG_GLOBAL": str(config), "GIT_CONFIG_NOSYSTEM": "1"}):
                upstream = root / "upstream"
                create_repository(upstream, attributes="* text=auto\nlf.txt text eol=lf\nbinary.bin -text\n")
                for name in (".editorconfig", "lf.txt", "binary.bin"):
                    (upstream / name).write_bytes(b"first\nsecond\n")
                git(upstream, "add", ".")
                git(upstream, "commit", "-q", "-m", "text attributes")
                reference = git(upstream, "rev-parse", "HEAD")
                repository = upstream.resolve().as_uri()
                checkout = root / "checkout"
                manager.sync_git_checkout(repository, reference, checkout, clean=True)
                self.assertEqual((checkout / ".editorconfig").read_bytes(), b"first\r\nsecond\r\n")
                self.assertEqual(git(checkout, "config", "core.autocrlf"), "false")
                manager.sync_git_checkout(repository, reference, checkout, verify_only=True)
                for name in ("lf.txt", "binary.bin"):
                    with self.subTest(name=name):
                        self.assertEqual((checkout / name).read_bytes(), b"first\nsecond\n")
                        (checkout / name).write_bytes(b"first\r\nsecond\r\n")
                        with self.assertRaisesRegex(manager.ComponentError, "source bytes"):
                            manager.sync_git_checkout(repository, reference, checkout, verify_only=True)
                        (checkout / name).write_bytes(b"first\nsecond\n")

    def test_managed_sdk_recovers_legacy_bytecode_without_cleaning_source_edits(self) -> None:
        sys.path.insert(0, str(ROOT / "scripts"))
        import build_esp_idf

        with tempfile.TemporaryDirectory(prefix="jh-component-sdk-cache-") as text:
            root = Path(text)
            upstream = root / "upstream"
            create_repository(upstream)
            (upstream / ".gitignore").write_text("__pycache__/\n", encoding="utf-8")
            (upstream / "module.py").write_text("VALUE = 1\n", encoding="utf-8")
            git(upstream, "add", ".")
            git(upstream, "commit", "-q", "-m", "SDK Python fixture")
            repository = upstream.resolve().as_uri()
            git(upstream, "-c", "protocol.file.allow=always", "submodule", "add", "-q", repository, "component")
            git(upstream, "commit", "-q", "-am", "SDK submodule fixture")
            reference = git(upstream, "rev-parse", "HEAD")
            (root / "third_party").mkdir()
            (root / "third_party/esp_idf_version.conf").write_text(
                f"ESP_IDF_REPO={repository}\nESP_IDF_REF={reference}\n"
                "ESP_IDF_VERSION=6.0.2\nESP_IDF_DIR=third_party/esp-idf\n", encoding="utf-8")
            spec = manager.GitComponent("esp-idf", "SDK fixture", "esp_idf_version.conf", "ESP_IDF", ("module.py",), recursive_submodules=True)
            with mock.patch.dict(manager.GIT_COMPONENTS, {"esp-idf": spec}), \
                    mock.patch.dict(os.environ, {"GIT_ALLOW_PROTOCOL": "file"}), \
                    mock.patch.object(build_esp_idf, "component_manager", manager), \
                    mock.patch.object(manager, "ensure_esp_idf_tools"):
                checkout = manager.ensure_git_component("esp-idf", root, verify_only=False)
                cache = checkout / "__pycache__/module.cpython-312.pyc"
                cache.parent.mkdir()
                py_compile.compile(str(checkout / "module.py"), cfile=str(cache), doraise=True)
                child_cache = checkout / "component/__pycache__/module.cpython-313.opt-1.pyc"
                child_cache.parent.mkdir()
                py_compile.compile(str(checkout / "component/module.py"), cfile=str(child_cache), doraise=True)
                expected = cache.read_bytes()
                for external in ("", str(checkout)):
                    with self.subTest(external=external):
                        with self.assertRaisesRegex(manager.ComponentError, "local changes"):
                            manager.ensure_git_component("esp-idf", root, verify_only=True, directory_override=external)
                        self.assertEqual(cache.read_bytes(), expected)
                with self.assertRaisesRegex(manager.ComponentError, "local changes"):
                    manager.ensure_git_component("esp-idf", root, verify_only=False, directory_override=str(checkout))
                self.assertEqual(cache.read_bytes(), expected)
                self.assertEqual(build_esp_idf.prepare_sdk(root, ""), checkout)
                self.assertFalse(cache.exists())
                self.assertFalse(child_cache.exists())
                manager.ensure_git_component("esp-idf", root, verify_only=True)
                # A cache entry may be removed; local sources and unrelated
                # files must survive, and still prevent a build.
                cache.parent.mkdir(exist_ok=True)
                cache.write_bytes(expected)
                (cache.parent / "keep.txt").write_text("local data", encoding="utf-8")
                orphan = cache.parent / "missing.cpython-312.pyc"
                orphan.write_bytes(expected)
                git(checkout, "update-index", "--assume-unchanged", "module.py")
                (checkout / "module.py").write_text("VALUE = 2\n", encoding="utf-8")
                with self.assertRaises(manager.ComponentError):
                    build_esp_idf.prepare_sdk(root, "")
                self.assertEqual((checkout / "module.py").read_text(), "VALUE = 2\n")
                self.assertEqual((cache.parent / "keep.txt").read_text(), "local data")
                self.assertEqual(orphan.read_bytes(), expected)

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
            first_ref = create_repository(upstream)
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

            # Verification wants the pinned sources whatever the clean flag:
            # an edited or an added file fails it, while an update without
            # clean leaves the local edit alone.
            (checkout / "payload.txt").write_text("edited\n", encoding="utf-8")
            with self.assertRaises(manager.ComponentError):
                manager.sync_git_checkout(repository, first_ref, checkout, verify_only=True)
            self.assertFalse(manager.sync_git_checkout(repository, first_ref, checkout))
            self.assertEqual((checkout / "payload.txt").read_text(), "edited\n")
            git(checkout, "checkout", "--", "payload.txt")
            (checkout / "extra.c").write_text("#error added\n", encoding="utf-8")
            with self.assertRaises(manager.ComponentError):
                manager.sync_git_checkout(repository, first_ref, checkout, verify_only=True)
            (checkout / "extra.c").unlink()
            self.assertFalse(
                manager.sync_git_checkout(repository, first_ref, checkout, verify_only=True)
            )

            for flag in ("assume-unchanged", "skip-worktree"):
                with self.subTest(flag=flag):
                    try:
                        marker = temporary / "clean-filter-ran"
                        script = temporary / "clean-filter.py"
                        script.write_text(
                            "import pathlib, sys\n"
                            f"pathlib.Path({str(marker)!r}).write_text('ran')\n"
                            "sys.stdin.read()\nsys.stdout.write('quoted path\\n')\n",
                            encoding="utf-8",
                        )
                        (checkout / ".git/info/attributes").write_text("source* filter=mask\n", encoding="utf-8")
                        git(checkout, "config", "filter.mask.clean", shlex.join([Path(sys.executable).as_posix(), script.as_posix()]))
                        git(checkout, "update-index", f"--{flag}", "source ż.txt")
                        (checkout / "source ż.txt").write_text("hidden edit\n", encoding="utf-8")
                        self.assertEqual(git(checkout, "status", "--porcelain"), "")
                        marker.unlink(missing_ok=True)
                        index = (checkout / ".git/index").read_bytes()
                        with self.assertRaisesRegex(manager.ComponentError, "source bytes"):
                            manager.sync_git_checkout(repository, first_ref, checkout, verify_only=True)
                        self.assertEqual((checkout / ".git/index").read_bytes(), index)
                        self.assertFalse(marker.exists(), "verification executed a Git clean filter")
                        self.assertEqual((checkout / "source ż.txt").read_text(), "hidden edit\n")
                        self.assertTrue(manager.sync_git_checkout(repository, first_ref, checkout, clean=True))
                        self.assertEqual((checkout / "source ż.txt").read_text(), "quoted path\n")
                    finally:
                        (checkout / ".git/info/attributes").unlink(missing_ok=True)
                        git(checkout, "update-index", "--no-assume-unchanged", "source ż.txt")
                        git(checkout, "update-index", "--no-skip-worktree", "source ż.txt")
                        git(checkout, "checkout", "--", "source ż.txt")

            (checkout / ".git/info/exclude").write_text("ignored.c\n", encoding="utf-8")
            (checkout / "ignored.c").write_text("#error unexpected source\n", encoding="utf-8")
            self.assertEqual(git(checkout, "status", "--porcelain"), "")
            with self.assertRaises(manager.ComponentError):
                manager.sync_git_checkout(repository, first_ref, checkout, verify_only=True)
            self.assertTrue(manager.sync_git_checkout(repository, first_ref, checkout, clean=True))
            self.assertFalse((checkout / "ignored.c").exists())

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


    def test_external_checkout_origin_and_hidden_edits_are_read_only(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-component-external-") as text:
            temporary = Path(text)
            upstream = temporary / "upstream"
            checkout = temporary / "checkout with spaces"
            first_ref = create_repository(upstream)
            repository = upstream.resolve().as_uri()
            alternate = (temporary / "alternate").resolve().as_uri()
            manager.sync_git_checkout(repository, first_ref, checkout, clean=True)
            external_root = temporary / "HAL with spaces"
            (external_root / "third_party").mkdir(parents=True)
            (external_root / "third_party/jhbase_version.conf").write_text(
                f"JHBASE_REPO={repository}\nJHBASE_REF={first_ref}\n"
                "JHBASE_VERSION=0.1.0\nJHBASE_DIR=third_party/jhbase\n", encoding="utf-8",
            )
            spec = manager.GitComponent("jhbase", "fixture", "jhbase_version.conf", "JHBASE", ("payload.txt",), clean=True)
            with mock.patch.dict(manager.GIT_COMPONENTS, {"jhbase": spec}):
                for verify in (False, True):
                    with self.subTest(external_verify=verify):
                        manager.ensure_git_component("jhbase", external_root, verify_only=verify, directory_override=str(checkout))
                        try:
                            git(checkout, "update-index", "--assume-unchanged", "payload.txt")
                            (checkout / "payload.txt").write_text("external edit\n", encoding="utf-8")
                            index = (checkout / ".git/index").read_bytes()
                            with self.assertRaisesRegex(manager.ComponentError, "source bytes"):
                                manager.ensure_git_component("jhbase", external_root, verify_only=verify, directory_override=str(checkout))
                            self.assertEqual((checkout / ".git/index").read_bytes(), index)
                            self.assertEqual((checkout / "payload.txt").read_text(), "external edit\n")
                        finally:
                            git(checkout, "update-index", "--no-assume-unchanged", "payload.txt")
                            git(checkout, "checkout", "--", "payload.txt")
                        try:
                            git(checkout, "remote", "set-url", "origin", alternate)
                            with self.assertRaisesRegex(manager.ComponentError, "origin"):
                                manager.ensure_git_component("jhbase", external_root, verify_only=verify, directory_override=str(checkout))
                            self.assertEqual(git(checkout, "remote", "get-url", "origin"), alternate)
                        finally:
                            git(checkout, "remote", "set-url", "origin", repository)



class ArchiveManagerTests(unittest.TestCase):
    def test_ccache_uses_the_supplied_archive_and_rejects_another_version(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-ccache-") as text:
            root = Path(text)
            suffix = manager._platform_asset()[2]
            write_supplied_pins(root, ccache_version_conf=(
                "CCACHE_RELEASE_BASE=https://example.invalid/ccache\n"
                "CCACHE_VERSION=4.14.2\n"
                f"CCACHE_ASSET_{suffix}=tool.zip\n"
                f"CCACHE_SHA256_{suffix}={'a' * 64}\n"
            ))
            binary = root / "third_party/ccache" / ("ccache.exe" if sys.platform == "win32" else "ccache")
            binary.parent.mkdir()
            binary.write_bytes(b"fixture")
            version = subprocess.CompletedProcess([], 0, "ccache version 4.14.2\n", "")
            with mock.patch.object(manager, "sync_archive", return_value=False) as sync, \
                 mock.patch.object(manager, "_run", return_value=version):
                self.assertEqual(binary, manager.ensure_ccache(root, verify_only=True))
                sync.assert_called_once_with(
                    "https://example.invalid/ccache/v4.14.2/tool.zip", "a" * 64, binary.parent,
                    verify_only=True, version_stamp="ccache|4.14.2|tool.zip",
                )
                version.stdout = "ccache version 0.0\n"
                with self.assertRaisesRegex(manager.ComponentError, "version mismatch"):
                    manager.ensure_ccache(root, verify_only=True)

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
            write_supplied_pins(
                root,
                pmd_version_conf=(
                    "PMD_VERSION=7.26.0\n"
                    "PMD_URL=https://example.invalid/pmd.zip\n"
                    f"PMD_SHA256={'a' * 64}\n"
                ),
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
            third_party = write_supplied_pins(
                root,
                pmd_version_conf=(
                    "PMD_VERSION=7.26.0\n"
                    "PMD_URL=https://example.invalid/pmd.zip\n"
                    f"PMD_SHA256={'a' * 64}\n"
                ),
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
            "cmd.exe", "/d", "/s", "/c", "install.bat",
            "esp32,esp32s3",
        ) if sys.platform == "win32" else (
            "bash", str(directory / "install.sh"), "esp32,esp32s3",
        )
        self.assertEqual(installer, commands[0])
        # Without Python on the host PATH, install.bat failed with 9009.
        self.assertEqual(
            str(Path(sys.executable).parent),
            run.call_args_list[0].kwargs["env"]["PATH"].split(os.pathsep)[0],
        )
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
        for verify in (False, True):
            with self.subTest(verify_bytecode=verify), tempfile.TemporaryDirectory(prefix="jh-idf-bytecode-") as text:
                root = Path(text)
                (root / "third_party").mkdir()
                (root / "third_party/esp_idf_version.conf").write_text(
                    "ESP_IDF_VERSION=6.0.2\nESP_IDF_TARGETS=esp32,esp32s3\n", encoding="utf-8",
                )
                sdk = root / "SDK with spaces"
                (sdk / "tools").mkdir(parents=True)
                (sdk / "tools/idf_tools.py").write_text("import sdk_payload\n", encoding="utf-8")
                (sdk / "tools/sdk_payload.py").write_text("value = 1\n", encoding="utf-8")
                (sdk / "install.sh").write_text("exit 0\n", encoding="utf-8")
                (sdk / "install.bat").write_text("@exit /b 0\n", encoding="utf-8")
                before = sorted(path.relative_to(sdk) for path in sdk.rglob("*"))
                with mock.patch.dict(os.environ), mock.patch.object(manager, "_synchronize_esp_idf_python_tools"):
                    os.environ.pop("PYTHONPYCACHEPREFIX", None)
                    os.environ.pop("PYTHONDONTWRITEBYTECODE", None)
                    manager.ensure_esp_idf_tools(root, sdk, verify_only=verify)
                self.assertEqual(sorted(path.relative_to(sdk) for path in sdk.rglob("*")), before)
                self.assertEqual((root / ".build/python-cache").exists(), not verify)

    def test_esp_idf_python_tools_are_pinned_by_reviewed_snapshot(self) -> None:
        pins = dict(manager._esp_idf_python_tool_pins(ROOT))
        self.assertEqual(pins["esp-coredump"], "1.17.2")
        self.assertEqual(pins["esptool"], "5.4.0")
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
        with tempfile.TemporaryDirectory(prefix="jh-component-submodules-") as text:
            temporary = Path(text)
            leaf, middle, upstream, checkout = (temporary / name for name in ("leaf", "middle", "upstream", "checkout with spaces"))
            for path in (leaf, middle, upstream):
                create_repository(path)
            for parent, child, name in ((middle, leaf, "leaf ż"), (upstream, middle, "middle")):
                git(parent, "-c", "protocol.file.allow=always", "submodule", "add", "--", child.resolve().as_uri(), f"deps/{name}")
                git(parent, "config", "-f", ".gitmodules", f"submodule.deps/{name}.url", f"../{child.name}")
                git(parent, "commit", "-q", "-am", "submodule fixture")
            reference = git(upstream, "rev-parse", "HEAD")
            repository = upstream.resolve().as_uri()
            with mock.patch.dict(os.environ, {"GIT_ALLOW_PROTOCOL": "file"}):
                manager.sync_git_checkout(repository, reference, checkout, recursive_submodules=True)
            nested = checkout / "deps/middle/deps/leaf ż"
            for recursive in (False, True):
                with self.subTest(recursive=recursive):
                    options = dict(recursive_submodules=recursive, submodules=() if recursive else ("deps/middle",))
                    manager.sync_git_checkout(repository, reference, checkout, verify_only=True, **options)
                    try:
                        git(nested, "update-index", "--assume-unchanged", "payload.txt")
                        (nested / "payload.txt").write_text("nested hidden edit\n", encoding="utf-8")
                        self.assertEqual(git(checkout, "status", "--porcelain"), "")
                        with self.assertRaisesRegex(manager.ComponentError, "source bytes"):
                            manager.sync_git_checkout(repository, reference, checkout, verify_only=True, **options)
                        self.assertEqual((nested / "payload.txt").read_text(), "nested hidden edit\n")
                    finally:
                        git(nested, "update-index", "--no-assume-unchanged", "payload.txt")
                        git(nested, "checkout", "--", "payload.txt")
                    origin = git(nested, "remote", "get-url", "origin")
                    try:
                        git(nested, "remote", "set-url", "origin", "https://example.invalid/wrong.git")
                        with self.assertRaisesRegex(manager.ComponentError, "origin"):
                            manager.sync_git_checkout(repository, reference, checkout, verify_only=True, **options)
                    finally:
                        git(nested, "remote", "set-url", "origin", origin)
            git(checkout, "submodule", "deinit", "--force", "--", "deps/middle")
            manager.sync_git_checkout(repository, reference, checkout, verify_only=True)
            (checkout / "deps/middle/.hidden.c").write_text("unexpected source\n", encoding="utf-8")
            self.assertEqual(git(checkout, "status", "--porcelain"), "")
            with self.assertRaises(manager.ComponentError):
                manager.sync_git_checkout(repository, reference, checkout, verify_only=True)
            self.assertTrue(manager.sync_git_checkout(repository, reference, checkout, clean=True))
            self.assertEqual(list((checkout / "deps/middle").iterdir()), [])

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
        for script in (
            "build_rp_pico_lib.sh",
            "build_stm32_lib.sh",
            "build_esp32_lib.sh",
        ):
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
        # The tool pins come from the jhbase checkout; HAL adds only its directories.
        override = {"JH_JHBASE_DIR": _JHBASE_OVERRIDE} if _JHBASE_OVERRIDE else {}
        with mock.patch.dict(os.environ, override):
            riscv, riscv_path = manager.supplied_pin(ROOT, "riscv-toolchain")
            self.assertEqual("riscv_toolchain_version.conf", riscv_path.name)
            self.assertEqual("third_party/riscv-toolchain", riscv["RISCV_TOOLCHAIN_DIR"])
            for name, pin in manager.JHBASE_PINS.items():
                shipped = manager.parse_config(manager.supplied_pin(ROOT, name)[1])
                self.assertNotIn(pin.dir_key, shipped, f"{name}: jhbase pins carry no directory")
                self.assertFalse(
                    (ROOT / "third_party" / pin.config).exists(), f"{name}: duplicate pin in HAL"
                )
        for suffix in ("X86_64_LIN", "AARCH64_LIN", "X64_WIN"):
            self.assertRegex(
                riscv[f"RISCV_TOOLCHAIN_SHA256_{suffix}"], r"^[0-9a-f]{64}$"
            )
        # Shape only: a bump in jhbase must not require editing this test.
        self.assertRegex(riscv["RISCV_TOOLCHAIN_TAG"], r"^v\d+\.\d+\.\d+-\d+$")
        self.assertRegex(riscv["RISCV_TOOLCHAIN_GCC_MAJOR"], r"^\d+$")
        self.assertRegex(riscv["RISCV_TOOLCHAIN_ASSET_STEM"], r"^riscv-toolchain-\d+$")
        # HAL policy, not a pin value: the pinned Pico SDK auto-detects this triple.
        self.assertEqual("riscv32-unknown-elf", riscv["RISCV_TOOLCHAIN_TRIPLE"])
        # The pre-commit hook runs the generators on an index snapshot that
        # carries the pins but no managed checkouts; JH_DEPENDENCY_ROOT names
        # the checkout holding them, the pin stays the snapshot's.
        with tempfile.TemporaryDirectory(prefix="jh-snapshot-") as text:
            snapshot = Path(text)
            (snapshot / "third_party").mkdir()
            shutil.copy2(ROOT / "third_party/jhbase_version.conf", snapshot / "third_party")
            with mock.patch.dict(os.environ, {**override, "JH_DEPENDENCY_ROOT": str(ROOT)}):
                self.assertEqual(manager.jhbase_root(ROOT), manager.jhbase_root(snapshot))
                self.assertEqual(riscv, manager.supplied_pin(snapshot, "riscv-toolchain")[0])
            if not _JHBASE_OVERRIDE:
                with mock.patch.dict(os.environ, override):
                    os.environ.pop("JH_DEPENDENCY_ROOT", None)
                    with self.assertRaises(manager.ComponentError):
                        manager.supplied_pin(snapshot, "riscv-toolchain")

    def test_jhbase_xtensa_pin_matches_the_esp_idf_tools(self) -> None:
        # jhbase builds its ESP32-S3 device archive with its own Xtensa pin and
        # links it into ESP-IDF firmware, so both must name one toolchain.
        # HAL installs that compiler through ESP-IDF, not from this pin.
        tools_json = ROOT / "third_party/esp-idf/tools/tools.json"
        if not tools_json.is_file():
            self.skipTest("pinned ESP-IDF checkout is not present")
        override = {"JH_JHBASE_DIR": _JHBASE_OVERRIDE} if _JHBASE_OVERRIDE else {}
        with mock.patch.dict(os.environ, override):
            pin = manager.parse_config(
                manager.jhbase_root(ROOT) / "third_party/xtensa_toolchain_version.conf"
            )
        tool = next(
            item for item in json.loads(tools_json.read_text(encoding="utf-8"))["tools"]
            if item["name"] == "xtensa-esp-elf"
        )
        version = next(item for item in tool["versions"] if item.get("status") == "recommended")
        self.assertEqual(version["name"], pin["XTENSA_TOOLCHAIN_VERSION"])
        self.assertEqual(
            re.match(r"esp-(\d+)\.", version["name"]).group(1), pin["XTENSA_TOOLCHAIN_GCC_MAJOR"]
        )
        for platform, host, key in (
            ("linux-amd64", "x86_64-linux-gnu", "X86_64_LIN"),
            ("linux-arm64", "aarch64-linux-gnu", "AARCH64_LIN"),
        ):
            with self.subTest(platform=platform):
                asset = f"{pin['XTENSA_TOOLCHAIN_ASSET_STEM']}-{host}.tar.xz"
                self.assertEqual(
                    f"{pin['XTENSA_TOOLCHAIN_RELEASE_BASE']}/{version['name']}/{asset}",
                    version[platform]["url"],
                )
                self.assertEqual(version[platform]["sha256"], pin[f"XTENSA_TOOLCHAIN_SHA256_{key}"])

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

    def test_picotool_version_match_compares_the_reported_release(self) -> None:
        self.assertTrue(
            manager._picotool_reports(
                "picotool v2.3.0 (Linux, GNU-13.3.0, Release)\n", "2.3.0"
            )
        )
        self.assertTrue(
            manager._picotool_reports("picotool v2.2.0 (Windows)\n", "2.2.0-a4")
        )
        for output in (
            "picotool v2.2.0 (without USB support)\n",
            "picotool v12.3.0\n",
            "usage: picotool [options]\n",
        ):
            with self.subTest(output=output):
                self.assertFalse(manager._picotool_reports(output, "2.3.0"))

    def test_windows_picotool_is_the_pinned_picotool_release(self) -> None:
        # The native RP configure checks every host's picotool against
        # PICOTOOL_VERSION, so the managed Windows archive must be that release.
        picotool = manager.parse_config(ROOT / "third_party/picotool_version.conf")
        windows = manager.parse_config(ROOT / "third_party/windows_tools_version.conf")
        self.assertTrue(
            manager._picotool_reports(
                f"picotool v{picotool['PICOTOOL_VERSION']} (Windows)",
                windows["WINDOWS_PICOTOOL_VERSION"],
            ),
            (picotool["PICOTOOL_VERSION"], windows["WINDOWS_PICOTOOL_VERSION"]),
        )
        self.assertIn(
            windows["WINDOWS_PICOTOOL_VERSION"], windows["WINDOWS_PICOTOOL_URL"]
        )

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

    @staticmethod
    def _cppcheck_root(root: Path) -> Path:
        third_party = write_supplied_pins(
            root,
            cppcheck_version_conf=(
                "CPPCHECK_REPO=https://example.invalid/cppcheck.git\n"
                "CPPCHECK_REF=0123456789abcdef\n"
                "CPPCHECK_VERSION=2.13.0\n"
            ),
        )
        (third_party / "cppcheck").mkdir()
        return root / ".build/tools/cppcheck"

    @staticmethod
    def _write_cppcheck(build: Path, version: str) -> None:
        binary = build / "bin/cppcheck"
        binary.parent.mkdir(parents=True, exist_ok=True)
        binary.write_text(f"#!/bin/sh\necho 'Cppcheck {version}'\n", encoding="utf-8")
        binary.chmod(0o755)
        for name in ("cfg/std.cfg", "addons/misra.py", "addons/cppcheckdata.py"):
            (binary.parent / name).parent.mkdir(parents=True, exist_ok=True)
            (binary.parent / name).write_text("", encoding="utf-8")

    @unittest.skipIf(sys.platform == "win32", "fixture uses a POSIX shell executable")
    def test_cppcheck_builds_once_and_rebuilds_a_stale_build(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-cppcheck-build-") as text:
            root = Path(text)
            build = self._cppcheck_root(root)
            real_run = manager._run
            cmake_calls = []

            def fake_run(command, **kwargs):
                if command[0] == "cmake":
                    cmake_calls.append(tuple(command))
                    if "--build" in command:
                        self._write_cppcheck(build, "2.13.0")
                    return subprocess.CompletedProcess(command, 0, "", "")
                return real_run(command, **kwargs)

            with mock.patch.object(manager, "sync_git_checkout", return_value=False), \
                    mock.patch.object(manager, "_run", side_effect=fake_run):
                binary = manager.ensure_cppcheck_linux(root, verify_only=False)
                self.assertEqual(binary, build / "bin/cppcheck")
                self.assertEqual(len(cmake_calls), 2)
                self.assertIn(f"-DFILESDIR={build / 'bin'}", cmake_calls[0])

                manager.ensure_cppcheck_linux(root, verify_only=False)
                self.assertEqual(len(cmake_calls), 2)
                self.assertEqual(manager.ensure_cppcheck_linux(root, verify_only=True), binary)

                (build / manager.VERSION_STAMP).write_text("cppcheck|old\n", encoding="utf-8")
                with self.assertRaisesRegex(manager.ComponentError, "different pin"):
                    manager.ensure_cppcheck_linux(root, verify_only=True)
                manager.ensure_cppcheck_linux(root, verify_only=False)
                self.assertEqual(len(cmake_calls), 4)
                self.assertEqual(manager.resolve_cppcheck(root), binary)

    @unittest.skipIf(sys.platform == "win32", "fixture uses a POSIX shell executable")
    def test_cppcheck_resolution_rejects_other_builds_and_names_the_repair(self) -> None:
        with tempfile.TemporaryDirectory(prefix="jh-cppcheck-resolve-") as text:
            root = Path(text)
            build = self._cppcheck_root(root)
            config = manager._cppcheck_config(root)
            with self.assertRaisesRegex(manager.ComponentError, "no executable"):
                manager.resolve_cppcheck(root)

            self._write_cppcheck(build, "2.10")
            (build / manager.VERSION_STAMP).write_text(
                manager._cppcheck_stamp(config, build) + "\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(
                manager.ComponentError, r"reports Cppcheck 2\.10.*ensure_cppcheck\.sh"
            ):
                manager.resolve_cppcheck(root)

            self._write_cppcheck(build, "2.13.0")
            (build / "bin/addons/misra.py").unlink()
            with self.assertRaisesRegex(manager.ComponentError, "addons/misra.py"):
                manager.resolve_cppcheck(root)

            self._write_cppcheck(build, "2.13.0")
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                self.assertEqual(
                    manager.main(["tool-path", "cppcheck", "--repo-root", str(root)]), 0
                )
            self.assertEqual(stdout.getvalue().strip(), str(build / "bin/cppcheck"))

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
        # runmefirst.sh and Linux CI install through one script.
        unix_tools = (ROOT / "scripts/install_host_tools.sh").read_text(encoding="utf-8")
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
        self.assertIn('"${SCRIPT_DIR}/scripts/install_host_tools.sh"', unix_bootstrap)
        self.assertIn("build-essential cmake ninja-build", unix_tools)
        self.assertIn("COMMANDS=(\n    cmake ninja", unix_tools)
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
        # The host network backend runs over Winsock only in this job.
        self.assertIn("test_bsd_socket_headers_c test_hal_host_network", workflow)
        self.assertIn("|test_hal_host_network)$'", workflow)

    def test_source_component_command_excludes_host_tool_archives(self) -> None:
        parser = manager.build_parser()
        arguments = parser.parse_args(
            ["source-components", "--verify-only", "--repo-root", str(ROOT)]
        )
        self.assertEqual(arguments.command, "source-components")
        self.assertTrue(arguments.verify_only)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
