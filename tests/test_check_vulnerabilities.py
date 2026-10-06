#!/usr/bin/env python3
"""Behaviour of the CVE scan when its data source fails or finds CVEs."""

from __future__ import annotations

import datetime
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import tomllib
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

# Fake osv-scanner: one exit code per run, taken from osv.codes.
FAKE_OSV = """#!/usr/bin/env bash
echo "osv $*" >> "${FAKE_DIR}/calls.log"
[[ -f "${FAKE_DIR}/osv.output" ]] && cat "${FAKE_DIR}/osv.output"
code="$(head -n 1 "${FAKE_DIR}/osv.codes")"
tail -n +2 "${FAKE_DIR}/osv.codes" > "${FAKE_DIR}/osv.next"
mv "${FAKE_DIR}/osv.next" "${FAKE_DIR}/osv.codes"
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
        shutil.copy(ROOT / "security" / "osv-scanner.toml", self.work / "repo" / "security")
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
                 cached_db: bool,
                 osv_codes: list[int] | None = None) -> subprocess.CompletedProcess:
        if osv_codes is not None:
            executable(self.fake / "osv-scanner", FAKE_OSV)
            (self.fake / "osv.codes").write_text(
                "".join(f"{code}\n" for code in osv_codes), encoding="utf-8")
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


    def osv_runs(self) -> int:
        return sum(call.startswith("osv ") for call in self.calls())

    def osv_calls(self) -> list[str]:
        return [call for call in self.calls() if call.startswith("osv ")]

    def test_osv_scans_own_files_then_component_commits(self) -> None:
        result = self.run_scan([0], 0, cached_db=True, osv_codes=[0, 0])
        self.assertEqual(0, result.returncode, result.stderr)
        own, components = self.osv_calls()
        config = self.work / "repo" / "security" / "osv-scanner.toml"
        for call in (own, components):
            self.assertIn(f"--config {config}", call)
        # Own files honour .gitignore and skip the file-hash heuristic.
        self.assertIn("--experimental-disable-plugins filesystem/vendored", own)
        self.assertNotIn("--no-ignore", own)
        # Components by commit only, build trees out.
        for flag in ("--experimental-no-default-plugins",
                     "--experimental-plugins vcs/gitrepo", "--include-git-root",
                     "--no-ignore", "--experimental-exclude .build"):
            self.assertIn(flag, components)

    def test_osv_findings_fail_without_retry(self) -> None:
        result = self.run_scan([0], 0, cached_db=True, osv_codes=[1])
        self.assertNotEqual(0, result.returncode)
        self.assertEqual(1, self.osv_runs())

    def test_osv_component_findings_fail(self) -> None:
        result = self.run_scan([0], 0, cached_db=True, osv_codes=[0, 1])
        self.assertNotEqual(0, result.returncode)
        self.assertEqual(2, self.osv_runs())

    def test_osv_unfinished_run_is_retried(self) -> None:
        result = self.run_scan([0], 0, cached_db=True, osv_codes=[127, 0, 0])
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(3, self.osv_runs())
        self.assertIn("could not finish", result.stderr)

    def test_osv_accepts_no_scanner_error(self) -> None:
        # The ESP-IDF hashing error was once accepted; no error is now.
        (self.fake / "osv.output").write_text(
            "Error during extraction: (extracting as filesystem/vendored) "
            "repo/third_party/esp-idf: failed during hashing: too many files to hash\n",
            encoding="utf-8")
        result = self.run_scan([0], 0, cached_db=True, osv_codes=[127, 127, 127])
        self.assertNotEqual(0, result.returncode)
        self.assertEqual(3, self.osv_runs())

    def test_osv_lasting_failure_fails_the_scan(self) -> None:
        result = self.run_scan([0], 0, cached_db=True, osv_codes=[0, 127, 127, 127])
        self.assertNotEqual(0, result.returncode)
        self.assertEqual(4, self.osv_runs())


class RecordedDecisionTests(unittest.TestCase):
    """security/osv-scanner.toml only silences findings that have a decision
    row in security/vulnerability_log.md, and every such row is listed."""

    def test_ignored_findings_match_the_log(self) -> None:
        config = tomllib.loads(
            (ROOT / "security" / "osv-scanner.toml").read_text(encoding="utf-8"))
        log_rows = [
            [cell.strip() for cell in line.strip().strip("|").split("|")]
            for line in (ROOT / "security" / "vulnerability_log.md")
            .read_text(encoding="utf-8").splitlines()
            if line.startswith("| 20")
        ]
        by_id = {}
        for date, ids, *_, status, _cvss, _reach, decision in (
            (row[0], row[1], row[2], row[3], row[4], row[5], row[6], row[7])
            for row in log_rows
        ):
            for vuln in ids.split(","):
                by_id[vuln.strip()] = (date, status, decision)
        ignored = {entry["id"]: entry for entry in config["IgnoredVulns"]}
        for vuln, entry in ignored.items():
            self.assertIn(vuln, by_id, f"{vuln} is ignored without a log row")
            date, status, _ = by_id[vuln]
            self.assertTrue(entry["reason"].startswith(f"{date}: "), vuln)
            self.assertIn(status, {"not_affected", "fixed", "mitigated"}, vuln)
            self.assertEqual(status == "mitigated", "ignoreUntil" in entry,
                             f"{vuln}: mitigated entries, and only they, expire")
        listed = {vuln for vuln, (_, _, decision) in by_id.items()
                  if "security/osv-scanner.toml" in decision}
        self.assertEqual(listed, set(ignored),
                         "log rows naming osv-scanner.toml and its entries differ")

    def test_mitigated_decisions_are_reviewed_on_time(self) -> None:
        # Scanner-independent: a commit-identified component can stop being
        # reported while its upstream issue stays open. Due on the date itself,
        # as osv-scanner reports the finding again from that day.
        config = tomllib.loads(
            (ROOT / "security" / "osv-scanner.toml").read_text(encoding="utf-8"))
        for entry in config["IgnoredVulns"]:
            if "ignoreUntil" in entry:
                self.assertGreater(
                    entry["ignoreUntil"], datetime.date.today(),
                    f"{entry['id']}: review due; update security/vulnerability_log.md")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
