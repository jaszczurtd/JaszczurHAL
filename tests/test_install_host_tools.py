#!/usr/bin/env python3
"""scripts/install_host_tools.sh installs and checks the pinned scanners."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest

from repo_root import repo_root  # noqa: E402
from scanner_fixtures import install_cve  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
OSV_CONF = "third_party/osv_scanner_version.conf"
CVE_CONF = "third_party/cve_bin_tool_version.conf"

# sudo runs apt-get as a no-op and installs only into the test's directories.
FAKE_SUDO = """#!/usr/bin/env bash
echo "$*" >> "${FAKE_DIR}/sudo.log"
case "$1" in
    apt-get) exit 0 ;;
    install)
        [[ "${@: -1}" == "${FAKE_DIR}"/* ]] || exit 1
        exec "$@"
        ;;
esac
exit 1
"""

# Serves downloads/<last URL segment>.
FAKE_CURL = """#!/usr/bin/env bash
while (($#)); do
    case "$1" in
        -o) out="$2"; shift 2 ;;
        -*) shift ;;
        *) url="$1"; shift ;;
    esac
done
echo "${url}" >> "${FAKE_DIR}/curl.log"
cp "${FAKE_DIR}/downloads/${url##*/}" "${out}"
"""

# Simulated pipx installs preserve the downloaded archive's hash in real
# distribution metadata, which the production source check reads.
FAKE_PYTHON = """import hashlib
import os
from pathlib import Path
import sys
from scanner_fixtures import install_cve

with (Path(os.environ["FAKE_DIR"]) / "python3.log").open("a") as log:
    print(" ".join(sys.argv[1:]), file=log)
if sys.argv[1:4] == ["-m", "pipx", "install"]:
    if os.environ.get("FAKE_PIPX_FAIL") == "1":
        sys.exit(1)
    archive = Path(sys.argv[-1])
    assert archive.name.endswith(".tar.gz")
    content = archive.read_bytes()
    version = content.decode()
    directory = Path(os.environ.get("PIPX_BIN_DIR", Path.home() / ".local/bin"))
    install_cve(directory / "cve-bin-tool", f"#!/bin/sh\\necho {version}\\n",
                version, hashlib.sha256(content).hexdigest())
"""


def executable(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


def pin(conf: str) -> dict[str, str]:
    keys = [
        line.split("=", 1)[0]
        for line in (ROOT / conf).read_text(encoding="utf-8").splitlines()
        if line and not line.startswith("#")
    ]
    values = subprocess.check_output(
        ["bash", "-c", 'source "$1"; shift; for name; do printf "%s\\n" "${!name}"; done',
         "bash", str(ROOT / conf), *keys], text=True).splitlines()
    return dict(zip(keys, values, strict=True))


OSV = pin(OSV_CONF)
CVE = pin(CVE_CONF)


class InstallScannerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.work = Path(tempfile.mkdtemp(prefix="jh host tools-"))
        self.repo = self.work / "repo"
        for script in ("install_host_tools.sh", "scanner_pins.sh"):
            executable(self.repo / "scripts" / script,
                       (ROOT / "scripts" / script).read_text(encoding="utf-8"))
        executable(self.repo / "scripts" / "run_sanitizer_fuzz.sh", "#!/bin/sh\n")
        executable(self.repo / "third_party" / "update_components.sh", "#!/bin/sh\n")
        for conf in (OSV_CONF, CVE_CONF):
            shutil.copy(ROOT / conf, self.repo / conf)
        self.fake = self.work / "fake"
        self.bin = self.fake / "bin"
        self.home = self.work / "home"
        (self.fake / "downloads").mkdir(parents=True)
        (self.fake / "usr-local-bin").mkdir()
        self.home.mkdir()
        executable(self.bin / "sudo", FAKE_SUDO)
        executable(self.bin / "curl", FAKE_CURL)
        executable(self.bin / "python3", f"#!{sys.executable}\n" + FAKE_PYTHON)
        # Every other required command, as present.
        script = (ROOT / "scripts" / "install_host_tools.sh").read_text(encoding="utf-8")
        commands = re.search(r"^COMMANDS=\((.*?)^\)", script, re.M | re.S).group(1)
        for tool in re.sub(r"#.*", "", commands).split():
            if tool not in ("osv-scanner", "cve-bin-tool", "python3"):
                executable(self.bin / tool, "#!/bin/sh\n")
        # Both scanners at their pinned release unless a test says otherwise.
        self.osv_installed(OSV["OSV_SCANNER_VERSION"])
        self.cve_installed(CVE["CVE_BIN_TOOL_VERSION"])

    def tearDown(self) -> None:
        shutil.rmtree(self.work)

    def osv_installed(self, version: str | None) -> None:
        path = self.fake / "usr-local-bin" / "osv-scanner"
        if version is None:
            path.unlink()
        else:
            executable(path, f'#!/bin/sh\necho "osv-scanner version: {version}"\n')

    def cve_installed(self, version: str | None,
                      source_hash: str | None = "pinned") -> None:
        path = self.home / ".local" / "bin" / "cve-bin-tool"
        if version is None:
            path.unlink()
        else:
            install_cve(path, f"#!/bin/sh\necho {version}\n", version,
                        CVE["CVE_BIN_TOOL_SHA256"] if source_hash == "pinned"
                        else source_hash)

    def offer(self, url: str, content: str, conf: str | None = None,
              key: str | None = None) -> None:
        """Serves content at url; with conf and key, pins its SHA-256 there."""
        (self.fake / "downloads" / url.rsplit("/", 1)[1]).write_text(
            content, encoding="utf-8")
        if conf:
            digest = hashlib.sha256(content.encode()).hexdigest()
            path = self.repo / conf
            path.write_text(re.sub(rf"^{key}=.*$", f"{key}={digest}",
                                   path.read_text(encoding="utf-8"), flags=re.M),
                            encoding="utf-8")

    def offer_osv(self, arch: str, version: str, pinned_sum: bool = True) -> None:
        self.offer(OSV[f"OSV_SCANNER_URL_{arch}"],
                   f'#!/bin/sh\necho "osv-scanner version: {version}"\n',
                   OSV_CONF if pinned_sum else None, f"OSV_SCANNER_SHA256_{arch}")

    def offer_cve(self, version: str, pinned_sum: bool = True) -> None:
        self.offer(CVE["CVE_BIN_TOOL_URL"], version,
                   CVE_CONF if pinned_sum else None, "CVE_BIN_TOOL_SHA256")

    def run_script(self, *args: str, machine: str = "x86_64",
                   **extra_env: str) -> subprocess.CompletedProcess:
        executable(self.bin / "uname", f"#!/bin/sh\necho {machine}\n")
        # The install locations are not on PATH: the scripts use them directly.
        env = {
            "PATH": f"{self.bin}:/usr/bin:/bin",
            "HOME": str(self.home),
            "FAKE_DIR": str(self.fake),
            "PYTHONPATH": str(ROOT / "tests"),
            "JH_OSV_SCANNER_DIR": str(self.fake / "usr-local-bin"),
            **extra_env,
        }
        return subprocess.run(
            ["bash", str(self.repo / "scripts" / "install_host_tools.sh"), *args],
            env=env, stdin=subprocess.DEVNULL, capture_output=True, text=True,
            timeout=60, check=False)

    def log(self, name: str) -> list[str]:
        path = self.fake / name
        return path.read_text(encoding="utf-8").splitlines() if path.exists() else []

    def osv_installs(self) -> list[str]:
        return [line for line in self.log("sudo.log") if line.startswith("install ")]

    def cve_installs(self) -> list[str]:
        return [line for line in self.log("python3.log") if line.startswith("-m pipx ")]

    def assert_ok(self, result: subprocess.CompletedProcess) -> None:
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("All required tools present.", result.stdout)

    def test_installs_the_pinned_osv_scanner(self) -> None:
        self.osv_installed(None)
        self.offer_osv("AMD64", OSV["OSV_SCANNER_VERSION"])
        result = self.run_script()
        self.assert_ok(result)
        self.assertEqual([OSV["OSV_SCANNER_URL_AMD64"]], self.log("curl.log"))
        self.assertEqual(1, len(self.osv_installs()))

    def test_arm64_takes_its_own_osv_scanner(self) -> None:
        self.osv_installed(None)
        self.offer_osv("ARM64", OSV["OSV_SCANNER_VERSION"])
        result = self.run_script(machine="aarch64")
        self.assert_ok(result)
        self.assertEqual([OSV["OSV_SCANNER_URL_ARM64"]], self.log("curl.log"))

    def test_osv_download_with_another_checksum_is_not_installed(self) -> None:
        self.osv_installed(None)
        self.offer_osv("AMD64", OSV["OSV_SCANNER_VERSION"], pinned_sum=False)
        result = self.run_script()
        self.assertNotEqual(0, result.returncode)
        self.assertIn("does not match its pinned SHA-256", result.stderr)
        self.assertEqual([], self.osv_installs())

    def test_failed_download_installs_nothing(self) -> None:
        self.osv_installed(None)
        result = self.run_script()
        self.assertNotEqual(0, result.returncode)
        self.assertNotIn("SHA-256", result.stderr)
        self.assertEqual([], self.osv_installs())

    def test_other_osv_release_is_replaced(self) -> None:
        self.osv_installed("2.4.0")
        self.offer_osv("AMD64", OSV["OSV_SCANNER_VERSION"])
        result = self.run_script()
        self.assert_ok(result)
        self.assertEqual(1, len(self.osv_installs()))

    def test_installs_the_pinned_cve_bin_tool(self) -> None:
        self.cve_installed(None)
        self.offer_cve(CVE["CVE_BIN_TOOL_VERSION"])
        result = self.run_script()
        self.assert_ok(result)
        self.assertEqual([CVE["CVE_BIN_TOOL_URL"]], self.log("curl.log"))
        (install,) = self.cve_installs()
        self.assertTrue(install.startswith("-m pipx install --force "), install)
        self.assertEqual(f"cve-bin-tool-{CVE['CVE_BIN_TOOL_COMMIT']}.tar.gz",
                         install.rsplit("/", 1)[1])

    def test_same_version_from_another_source_is_replaced(self) -> None:
        self.cve_installed(CVE["CVE_BIN_TOOL_VERSION"], source_hash="0" * 64)
        self.offer_cve(CVE["CVE_BIN_TOOL_VERSION"])
        self.assert_ok(self.run_script())
        self.assertEqual(1, len(self.cve_installs()))

    def test_check_refuses_a_release_without_source_metadata(self) -> None:
        self.cve_installed(CVE["CVE_BIN_TOOL_VERSION"], source_hash=None)
        result = self.run_script("--check")
        self.assertNotEqual(0, result.returncode)
        self.assertIn(f"pinned {CVE['CVE_BIN_TOOL_VERSION']}@{CVE['CVE_BIN_TOOL_COMMIT']}",
                      result.stdout)
        self.assertEqual([], self.cve_installs())

    def test_check_refuses_same_version_from_another_source(self) -> None:
        self.cve_installed(CVE["CVE_BIN_TOOL_VERSION"], source_hash="0" * 64)
        result = self.run_script("--check")
        self.assertNotEqual(0, result.returncode)
        self.assertIn("VERSION  cve-bin-tool", result.stdout)

    def test_source_check_uses_its_environment_despite_pythonpath(self) -> None:
        digest = CVE["CVE_BIN_TOOL_SHA256"]
        for installed, foreign in ((digest, "0" * 64), ("0" * 64, digest)):
            with self.subTest(installed=installed):
                self.cve_installed(CVE["CVE_BIN_TOOL_VERSION"], source_hash=installed)
                metadata = install_cve(
                    self.work / "elsewhere" / "bin" / "cve-bin-tool",
                    "#!/bin/sh\nexit 0\n", CVE["CVE_BIN_TOOL_VERSION"], foreign)
                result = self.run_script(
                    "--check", PYTHONPATH=f"{metadata.parent}:{ROOT / 'tests'}")
                self.assertEqual(installed == digest, result.returncode == 0,
                                 result.stdout + result.stderr)

    def test_check_refuses_malformed_source_metadata(self) -> None:
        metadata = install_cve(
            self.home / ".local/bin/cve-bin-tool",
            f"#!/bin/sh\necho {CVE['CVE_BIN_TOOL_VERSION']}\n",
            CVE["CVE_BIN_TOOL_VERSION"], CVE["CVE_BIN_TOOL_SHA256"])
        (metadata / "direct_url.json").write_text("{", encoding="utf-8")
        result = self.run_script("--check")
        self.assertNotEqual(0, result.returncode)
        self.assertIn("VERSION  cve-bin-tool", result.stdout)
        self.assertNotIn("Traceback", result.stderr)

    def test_legacy_archive_hash_record_is_accepted(self) -> None:
        metadata = install_cve(
            self.home / ".local/bin/cve-bin-tool",
            f"#!/bin/sh\necho {CVE['CVE_BIN_TOOL_VERSION']}\n",
            CVE["CVE_BIN_TOOL_VERSION"], CVE["CVE_BIN_TOOL_SHA256"])
        (metadata / "direct_url.json").write_text(json.dumps({
            "url": "file:///removed-download/source.tar.gz",
            "archive_info": {"hash": f"sha256={CVE['CVE_BIN_TOOL_SHA256']}"},
        }), encoding="utf-8")
        self.assert_ok(self.run_script("--check"))

    def test_cve_download_with_another_checksum_is_not_installed(self) -> None:
        self.cve_installed(None)
        self.offer_cve(CVE["CVE_BIN_TOOL_VERSION"], pinned_sum=False)
        result = self.run_script()
        self.assertNotEqual(0, result.returncode)
        self.assertIn("does not match its pinned SHA-256", result.stderr)
        self.assertEqual([], self.cve_installs())

    def test_failed_source_install_removes_its_download(self) -> None:
        self.cve_installed(None)
        self.offer_cve(CVE["CVE_BIN_TOOL_VERSION"])
        result = self.run_script(FAKE_PIPX_FAIL="1")
        self.assertNotEqual(0, result.returncode)
        (install,) = self.cve_installs()
        archive = Path(install.split("--force ", 1)[1])
        self.assertFalse(archive.parent.exists())
        self.assertFalse((self.home / ".local/bin/cve-bin-tool").exists())

    def test_other_cve_release_is_replaced(self) -> None:
        self.cve_installed("3.3")
        self.offer_cve(CVE["CVE_BIN_TOOL_VERSION"])
        result = self.run_script()
        self.assert_ok(result)
        self.assertEqual(1, len(self.cve_installs()))

    def test_older_scanners_earlier_in_path_change_nothing(self) -> None:
        # Another copy of each, found first on PATH.
        executable(self.bin / "osv-scanner",
                   '#!/bin/sh\necho "osv-scanner version: 2.4.0"\n')
        executable(self.bin / "cve-bin-tool", "#!/bin/sh\necho 3.3\n")
        self.osv_installed(None)
        self.cve_installed(None)
        self.offer_osv("AMD64", OSV["OSV_SCANNER_VERSION"])
        self.offer_cve(CVE["CVE_BIN_TOOL_VERSION"])
        self.assert_ok(self.run_script())
        self.assertEqual(1, len(self.osv_installs()))
        self.assertEqual(1, len(self.cve_installs()))
        # A second run finds both installed.
        self.assert_ok(self.run_script())
        self.assertEqual(2, len(self.log("curl.log")))

    def test_pipx_bin_dir_elsewhere_changes_nothing(self) -> None:
        self.cve_installed(None)
        self.offer_cve(CVE["CVE_BIN_TOOL_VERSION"])
        elsewhere = self.work / "pipx-bin"
        self.assert_ok(self.run_script(PIPX_BIN_DIR=str(elsewhere)))
        self.assertFalse((elsewhere / "cve-bin-tool").exists())

    def test_check_wants_the_scanner_where_it_installs_it(self) -> None:
        self.osv_installed(None)
        executable(self.bin / "osv-scanner",
                   f'#!/bin/sh\necho "osv-scanner version: {OSV["OSV_SCANNER_VERSION"]}"\n')
        result = self.run_script("--check")
        self.assertNotEqual(0, result.returncode)
        self.assertIn(
            f"MISSING  osv-scanner at {self.fake / 'usr-local-bin' / 'osv-scanner'}",
            result.stdout)

    def test_pinned_releases_are_kept(self) -> None:
        result = self.run_script()
        self.assert_ok(result)
        self.assertEqual([], self.log("curl.log"))
        self.assertEqual([], self.osv_installs())
        self.assertEqual([], self.cve_installs())

    def test_check_refuses_other_releases(self) -> None:
        self.osv_installed("2.4.0")
        self.cve_installed("3.3")
        result = self.run_script("--check")
        self.assertNotEqual(0, result.returncode)
        self.assertIn(
            f"VERSION  osv-scanner 2.4.0 at {self.fake / 'usr-local-bin' / 'osv-scanner'}"
            f", pinned {OSV['OSV_SCANNER_VERSION']}", result.stdout)
        self.assertIn(
            f"VERSION  cve-bin-tool 3.3 at {self.home / '.local' / 'bin' / 'cve-bin-tool'}"
            f", pinned {CVE['CVE_BIN_TOOL_VERSION']}", result.stdout)
        self.assertEqual([], self.log("sudo.log"))
        self.assertEqual([], self.log("curl.log"))

    def test_check_accepts_pinned_releases(self) -> None:
        result = self.run_script("--check")
        self.assert_ok(result)
        self.assertNotIn("VERSION", result.stdout)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
