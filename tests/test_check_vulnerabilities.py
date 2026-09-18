#!/usr/bin/env python3
"""Behaviour of the CVE scan when its data source fails or finds CVEs."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest

from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)

# Fake scanner: an empty-directory run is a database refresh, an --sbom run is
# the scan. Exit codes come from one line each in refresh.codes / scan.code.
FAKE_SCANNER = """#!/usr/bin/env bash
echo "$*" >> "${FAKE_DIR}/calls.log"
if [[ " $* " == *" --sbom "* ]]; then
    exit "$(cat "${FAKE_DIR}/scan.code")"
fi
code="$(head -n 1 "${FAKE_DIR}/refresh.codes")"
tail -n +2 "${FAKE_DIR}/refresh.codes" > "${FAKE_DIR}/refresh.next"
mv "${FAKE_DIR}/refresh.next" "${FAKE_DIR}/refresh.codes"
exit "${code:-0}"
"""


def executable(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


class CveScanTests(unittest.TestCase):
    def setUp(self) -> None:
        self.work = Path(tempfile.mkdtemp(prefix="jh-cve-"))
        scripts = self.work / "repo" / "scripts"
        scripts.mkdir(parents=True)
        (self.work / "repo" / "security").mkdir()
        shutil.copy(ROOT / "scripts" / "check_vulnerabilities.sh", scripts)
        executable(
            scripts / "generate_sbom.py",
            "#!/usr/bin/env bash\necho '{}' > \"$2\"\n",
        )
        self.fake = self.work / "fake"
        self.fake.mkdir()
        executable(self.fake / "cve-bin-tool", FAKE_SCANNER)
        self.home = self.work / "home"
        self.home.mkdir()

    def tearDown(self) -> None:
        shutil.rmtree(self.work)

    def run_scan(self, refresh_codes: list[int], scan_code: int,
                 cached_db: bool) -> subprocess.CompletedProcess:
        (self.fake / "refresh.codes").write_text(
            "".join(f"{code}\n" for code in refresh_codes), encoding="utf-8")
        (self.fake / "scan.code").write_text(f"{scan_code}\n", encoding="utf-8")
        if cached_db:
            database = self.home / ".cache" / "cve-bin-tool" / "cve.db"
            database.parent.mkdir(parents=True)
            database.write_bytes(b"cached")
        env = {
            "PATH": f"{self.fake}:/usr/bin:/bin",
            "HOME": str(self.home),
            "FAKE_DIR": str(self.fake),
            "JH_SECURITY_SCAN_SOURCE": "1",
            "JH_CVE_REFRESH_ATTEMPTS": "3",
            "JH_CVE_REFRESH_DELAY_S": "0",
        }
        return subprocess.run(
            ["bash", str(self.work / "repo" / "scripts" / "check_vulnerabilities.sh")],
            env=env, capture_output=True, text=True, timeout=60, check=False)

    def calls(self) -> list[str]:
        return (self.fake / "calls.log").read_text(encoding="utf-8").splitlines()

    def test_unreachable_source_without_cache_fails(self) -> None:
        result = self.run_scan([1, 1, 1], 0, cached_db=False)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("no cached database", result.stderr)
        self.assertFalse(any("--sbom" in call for call in self.calls()))

    def test_unreachable_source_scans_the_cached_database(self) -> None:
        result = self.run_scan([1, 1, 1], 0, cached_db=True)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn("cached database", result.stderr)
        scan = [call for call in self.calls() if "--sbom" in call]
        self.assertEqual(1, len(scan))
        self.assertIn("--update never", scan[0])

    def test_found_cves_fail_the_scan(self) -> None:
        result = self.run_scan([0], 1, cached_db=True)
        self.assertNotEqual(0, result.returncode)

    def test_transient_failure_is_retried(self) -> None:
        result = self.run_scan([1, 0], 0, cached_db=False)
        self.assertEqual(0, result.returncode, result.stderr)
        refreshes = [call for call in self.calls() if "--sbom" not in call]
        self.assertEqual(2, len(refreshes))
        self.assertNotIn("cached database", result.stderr)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
