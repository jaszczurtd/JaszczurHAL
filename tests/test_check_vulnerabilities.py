#!/usr/bin/env python3
"""Behaviour of the CVE scan when its data source fails or finds CVEs."""

from __future__ import annotations

import datetime
import json
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
from scanner_fixtures import install_cve  # noqa: E402

ROOT = repo_root(sys.argv, __file__)

# Fake scanner: an empty-directory run is a database refresh, an --sbom run is
# the scan, whose --vex-file is kept as vex.json. Exit codes come from one line
# each in refresh.codes / scan.code; --version reports cve.version and fails
# without it.
FAKE_SCANNER = """#!/usr/bin/env bash
if [[ "$1" == "--version" ]]; then
    [[ -f "${FAKE_DIR}/cve.version" ]] || exit 2
    echo "$(cat "${FAKE_DIR}/cve.version")"
    exit 0
fi
echo "$*" >> "${FAKE_DIR}/calls.log"
if [[ " $* " == *" --sbom "* ]]; then
    args=("$@")
    for ((i = 0; i + 1 < ${#args[@]}; i++)); do
        if [[ "${args[i]}" == "--vex-file" ]]; then
            cp "${args[i + 1]}" "${FAKE_DIR}/vex.json"
        fi
    done
    exit "$(cat "${FAKE_DIR}/scan.code")"
fi
code="$(head -n 1 "${FAKE_DIR}/refresh.codes")"
tail -n +2 "${FAKE_DIR}/refresh.codes" > "${FAKE_DIR}/refresh.next"
mv "${FAKE_DIR}/refresh.next" "${FAKE_DIR}/refresh.codes"
exit "${code:-0}"
"""

# Fake osv-scanner: one exit code per run, taken from osv.codes; --version
# reports osv.version and fails without it. A -L input is kept as
# osv_input.json.
FAKE_OSV = """#!/usr/bin/env bash
if [[ "$1" == "--version" ]]; then
    [[ -f "${FAKE_DIR}/osv.version" ]] || exit 2
    echo "osv-scanner version: $(cat "${FAKE_DIR}/osv.version")"
    exit 0
fi
echo "osv $*" >> "${FAKE_DIR}/calls.log"
pwd >> "${FAKE_DIR}/osv_cwd.log"
for argument in "$@"; do
    if [[ "${argument}" == osv-scanner:* ]]; then
        cp "${argument#osv-scanner:}" "${FAKE_DIR}/osv_input.json"
    fi
done
[[ -f "${FAKE_DIR}/osv.output" ]] && cat "${FAKE_DIR}/osv.output"
code="$(head -n 1 "${FAKE_DIR}/osv.codes")"
tail -n +2 "${FAKE_DIR}/osv.codes" > "${FAKE_DIR}/osv.next"
mv "${FAKE_DIR}/osv.next" "${FAKE_DIR}/osv.codes"
exit "${code:-0}"
"""


def pinned(conf: str, key: str) -> str:
    for line in (ROOT / "third_party" / conf).read_text(encoding="utf-8").splitlines():
        if line.startswith(f"{key}="):
            return line.split("=", 1)[1]
    raise AssertionError(f"third_party/{conf} has no {key}")


def pinned_osv_version() -> str:
    return pinned("osv_scanner_version.conf", "OSV_SCANNER_VERSION")


def pinned_cve_version() -> str:
    return pinned("cve_bin_tool_version.conf", "CVE_BIN_TOOL_VERSION")


RELEASE = "c859b25da02955fef659d658b8f324b5cde87be3"
# An SBOM with one component pinned after its release and one at a release.
SBOM_WITH_RELEASE = {"components": [
    {"name": "cJSON", "purl": f"pkg:github/DaveGamble/cJSON@{'6' * 40}",
     "pedigree": {"ancestors": [
         {"name": "cJSON", "purl": f"pkg:github/DaveGamble/cJSON@{RELEASE}"}]}},
    {"name": "littlefs", "purl": f"pkg:github/littlefs-project/littlefs@{'7' * 40}"},
]}


TRIAGE = ROOT / "security" / "cve-bin-tool-triage.toml"
UNITY_COMMIT = "f" * 40
SBOM_WITH_UNITY = {"components": [
    {"name": "Unity", "purl": f"pkg:github/jaszczurtd/Unity@{UNITY_COMMIT}",
     "properties": [{"name": "jaszczurhal:commit", "value": UNITY_COMMIT}]},
]}


def decision(commit: str = UNITY_COMMIT, extra: str = "") -> str:
    """One cve-bin-tool triage entry for the Unity component above."""
    return f"""
[[Triage]]
ids = ["CVE-2008-4542"]
component = "Unity"
commit = "{commit}"
product = "cisco/unity"
status = "not_affected"
justification = "component_not_present"
reason = "2026-10-09: another product; not_affected"
{extra}
"""


def logged_decisions() -> dict[str, tuple[str, str, str]]:
    """Map each ID in security/vulnerability_log.md to (date, status, decision)."""
    by_id = {}
    for line in (ROOT / "security" / "vulnerability_log.md").read_text(
            encoding="utf-8").splitlines():
        if line.startswith("| 20"):
            cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
            for vuln in cells[1].split(","):
                by_id[vuln.strip()] = (cells[0], cells[4], cells[7])
    return by_id


def executable(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR)


class CveScanTests(unittest.TestCase):
    def setUp(self) -> None:
        self.work = Path(tempfile.mkdtemp(prefix="jh cve-"))
        scripts = self.work / "repo" / "scripts"
        scripts.mkdir(parents=True)
        (self.work / "repo" / "security").mkdir()
        shutil.copy(ROOT / "scripts" / "check_vulnerabilities.sh", scripts)
        shutil.copy(ROOT / "scripts" / "scanner_pins.sh", scripts)
        shutil.copy(ROOT / "scripts" / "cve_bin_tool_triage.py", scripts)
        # Decisions come from each test; RecordedDecisionTests checks the
        # repository's own file.
        self.write_triage("")
        (self.work / "repo" / "third_party").mkdir()
        for conf in ("osv_scanner_version.conf", "cve_bin_tool_version.conf"):
            shutil.copy(ROOT / "third_party" / conf, self.work / "repo" / "third_party")
        shutil.copy(ROOT / "security" / "osv-scanner.toml", self.work / "repo" / "security")
        executable(
            scripts / "generate_sbom.py",
            '#!/usr/bin/env bash\n'
            'if [[ -f "${FAKE_DIR}/sbom.json" ]]; then cp "${FAKE_DIR}/sbom.json" "$2"\n'
            "else echo '{}' > \"$2\"; fi\n",
        )
        self.fake = self.work / "fake"
        self.fake.mkdir()
        self.home = self.work / "home"
        # Where scripts/install_host_tools.sh puts each scanner: osv-scanner
        # into JH_OSV_SCANNER_DIR (self.fake), cve-bin-tool into ~/.local/bin.
        self.cve = self.home / ".local" / "bin" / "cve-bin-tool"
        self.cve.parent.mkdir(parents=True)
        self.source_metadata = install_cve(
            self.cve, FAKE_SCANNER, pinned_cve_version(),
            pinned("cve_bin_tool_version.conf", "CVE_BIN_TOOL_SHA256"))

    def tearDown(self) -> None:
        shutil.rmtree(self.work)

    def run_scan(self, refresh_codes: list[int], scan_code: int,
                 cached_db: bool,
                 osv_codes: list[int] | None = None,
                 osv_version: str | None = None,
                 cve_version: str | None = None,
                 sbom: dict | None = None,
                 path_first: Path | None = None,
                 cache_home: Path | None = None) -> subprocess.CompletedProcess:
        if cve_version != "":
            (self.fake / "cve.version").write_text(
                cve_version or pinned_cve_version(), encoding="utf-8")
        if sbom is not None:
            (self.fake / "sbom.json").write_text(json.dumps(sbom), encoding="utf-8")
        if osv_codes is not None:
            executable(self.fake / "osv-scanner", FAKE_OSV)
            (self.fake / "osv.codes").write_text(
                "".join(f"{code}\n" for code in osv_codes), encoding="utf-8")
            if osv_version != "":
                (self.fake / "osv.version").write_text(
                    osv_version or pinned_osv_version(), encoding="utf-8")
        (self.fake / "refresh.codes").write_text(
            "".join(f"{code}\n" for code in refresh_codes), encoding="utf-8")
        (self.fake / "scan.code").write_text(f"{scan_code}\n", encoding="utf-8")
        if cached_db:
            database = (cache_home or self.home / ".cache") / "cve-bin-tool" / "cve.db"
            database.parent.mkdir(parents=True)
            database.write_bytes(b"cached")
        env = {
            "PATH": f"{path_first or self.fake}:{self.fake}:/usr/bin:/bin",
            "HOME": str(self.home),
            "FAKE_DIR": str(self.fake),
            "JH_OSV_SCANNER_DIR": str(self.fake),
            "JH_SECURITY_SCAN_SOURCE": "1",
            "JH_CVE_REFRESH_ATTEMPTS": "3",
            "JH_CVE_REFRESH_DELAY_S": "0",
        }
        if cache_home is not None:
            env["XDG_CACHE_HOME"] = str(cache_home)
        # Run from outside the repository, as a direct call may be.
        return subprocess.run(
            ["bash", str(self.work / "repo" / "scripts" / "check_vulnerabilities.sh")],
            env=env, cwd=self.home, capture_output=True, text=True, timeout=60,
            check=False)

    def calls(self) -> list[str]:
        log = self.fake / "calls.log"
        return log.read_text(encoding="utf-8").splitlines() if log.exists() else []

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

    def test_unreachable_source_uses_the_xdg_cache_directory(self) -> None:
        result = self.run_scan([1, 1, 1], 0, cached_db=True,
                               cache_home=self.work / "cache elsewhere")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn("cached database", result.stderr)
        self.assertEqual(1, sum("--sbom" in call for call in self.calls()))

    def test_found_cves_fail_the_scan(self) -> None:
        result = self.run_scan([0], 1, cached_db=True)
        self.assertNotEqual(0, result.returncode)

    def test_transient_failure_is_retried(self) -> None:
        result = self.run_scan([1, 0], 0, cached_db=False)
        self.assertEqual(0, result.returncode, result.stderr)
        refreshes = [call for call in self.calls() if "--sbom" not in call]
        self.assertEqual(2, len(refreshes))
        self.assertNotIn("cached database", result.stderr)

    def write_triage(self, text: str) -> None:
        (self.work / "repo" / "security" / "cve-bin-tool-triage.toml").write_text(
            text, encoding="utf-8")

    def scanned_statements(self) -> list[dict]:
        vex = json.loads((self.fake / "vex.json").read_text(encoding="utf-8"))
        return vex["statements"]

    def test_recorded_decisions_filter_the_scan(self) -> None:
        self.write_triage(decision())
        result = self.run_scan([0], 0, cached_db=True, sbom=SBOM_WITH_UNITY)
        self.assertEqual(0, result.returncode, result.stderr)
        scan = [call for call in self.calls() if "--sbom" in call]
        self.assertIn("--filter-triage", scan[0])
        # cve-bin-tool matches vendor, product and the version from the purl.
        (statement,) = self.scanned_statements()
        self.assertEqual("CVE-2008-4542", statement["vulnerability"]["name"])
        self.assertEqual([{"@id": f"pkg:generic/cisco/unity@{UNITY_COMMIT}"}],
                         statement["products"])
        self.assertEqual("not_affected", statement["status"])
        self.assertEqual("component_not_present", statement["justification"])

    def test_decision_for_another_pin_is_not_applied(self) -> None:
        self.write_triage(decision(commit="e" * 40))
        result = self.run_scan([0], 0, cached_db=True, sbom=SBOM_WITH_UNITY)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual([], self.scanned_statements())
        self.assertIn("not applied, review it", result.stderr)

    def test_decision_past_its_review_date_is_not_applied(self) -> None:
        self.write_triage(decision(extra="reviewUntil = 2000-01-01"))
        result = self.run_scan([0], 0, cached_db=True, sbom=SBOM_WITH_UNITY)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual([], self.scanned_statements())
        self.assertIn("review was due on 2000-01-01", result.stderr)

    def test_decision_for_a_missing_component_fails_before_scanning(self) -> None:
        self.write_triage(decision())
        result = self.run_scan([0], 0, cached_db=True)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("Unity: no such SBOM component", result.stderr)
        self.assertFalse(any("--sbom" in call for call in self.calls()))

    def test_malformed_decision_fails_before_scanning(self) -> None:
        self.write_triage(decision().replace(
            'justification = "component_not_present"\n', ""))
        result = self.run_scan([0], 0, cached_db=True, sbom=SBOM_WITH_UNITY)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("not_affected needs an OpenVEX justification", result.stderr)
        self.assertFalse(any("--sbom" in call for call in self.calls()))


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
        # osv-scanner matches the excluded .build relative to its working
        # directory: from anywhere else, stale trees in .build get scanned.
        cwds = (self.fake / "osv_cwd.log").read_text(encoding="utf-8").splitlines()
        self.assertEqual(str((self.work / "repo").resolve()), cwds[1])
        # No component pinned after a release: no third pass.
        self.assertEqual(2, self.osv_runs())

    def test_osv_other_release_fails_before_scanning(self) -> None:
        result = self.run_scan([0], 0, cached_db=True, osv_codes=[0, 0],
                               osv_version="2.4.0")
        self.assertNotEqual(0, result.returncode)
        self.assertIn(f"osv-scanner 2.4.0 at {self.fake / 'osv-scanner'}, "
                      f"pinned {pinned_osv_version()}", result.stderr)
        self.assertEqual(0, self.osv_runs())

    def test_osv_without_a_version_fails_before_scanning(self) -> None:
        result = self.run_scan([0], 0, cached_db=True, osv_codes=[0, 0],
                               osv_version="")
        self.assertNotEqual(0, result.returncode)
        self.assertIn("osv-scanner unknown at", result.stderr)
        self.assertEqual(0, self.osv_runs())

    def test_osv_scans_the_release_of_a_commit_pin(self) -> None:
        result = self.run_scan([0], 0, cached_db=True, osv_codes=[0, 0, 0],
                               sbom=SBOM_WITH_RELEASE)
        self.assertEqual(0, result.returncode, result.stderr)
        releases = self.osv_calls()[2]
        config = self.work / "repo" / "security" / "osv-scanner.toml"
        self.assertIn(f"--config {config}", releases)
        self.assertIn("-L osv-scanner:", releases)
        scanned = json.loads((self.fake / "osv_input.json").read_text(encoding="utf-8"))
        self.assertEqual(
            [{"package": {"name": "https://github.com/DaveGamble/cJSON",
                          "commit": RELEASE}}],
            [package for source in scanned["results"] for package in source["packages"]])

    def test_osv_release_findings_fail(self) -> None:
        result = self.run_scan([0], 0, cached_db=True, osv_codes=[0, 0, 1],
                               sbom=SBOM_WITH_RELEASE)
        self.assertNotEqual(0, result.returncode)
        self.assertEqual(3, self.osv_runs())

    def test_release_without_a_commit_fails(self) -> None:
        sbom = json.loads(json.dumps(SBOM_WITH_RELEASE))
        sbom["components"][0]["pedigree"]["ancestors"][0]["purl"] = "pkg:generic/cjson@1.7.19"
        result = self.run_scan([0], 0, cached_db=True, osv_codes=[0, 0, 0], sbom=sbom)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("cJSON: release without a GitHub commit purl", result.stderr)
        self.assertEqual(2, self.osv_runs())

    def test_cve_bin_tool_other_release_fails_before_scanning(self) -> None:
        result = self.run_scan([0], 0, cached_db=True, cve_version="3.3")
        self.assertNotEqual(0, result.returncode)
        self.assertIn(f"cve-bin-tool 3.3 at {self.cve}, "
                      f"pinned {pinned_cve_version()}", result.stderr)
        self.assertEqual([], self.calls())

    def test_cve_bin_tool_without_a_version_fails_before_scanning(self) -> None:
        result = self.run_scan([0], 0, cached_db=True, cve_version="")
        self.assertNotEqual(0, result.returncode)
        self.assertIn("cve-bin-tool unknown at", result.stderr)
        self.assertEqual([], self.calls())

    def test_same_version_wrong_source_fails_before_refreshing(self) -> None:
        (self.source_metadata / "direct_url.json").write_text(json.dumps({
            "archive_info": {"hashes": {"sha256": "0" * 64}},
        }), encoding="utf-8")
        result = self.run_scan([0], 0, cached_db=True)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("cve-bin-tool", result.stderr)
        self.assertEqual([], self.calls())

    def test_missing_source_record_fails_before_refreshing(self) -> None:
        (self.source_metadata / "direct_url.json").unlink()
        result = self.run_scan([0], 0, cached_db=True)
        self.assertNotEqual(0, result.returncode)
        self.assertIn("cve-bin-tool", result.stderr)
        self.assertEqual([], self.calls())

    def test_older_scanners_earlier_in_path_are_not_run(self) -> None:
        other = self.work / "other"
        other.mkdir()
        executable(other / "osv-scanner",
                   '#!/bin/sh\necho "osv-scanner version: 2.4.0"; exit 2\n')
        executable(other / "cve-bin-tool", "#!/bin/sh\necho 3.3; exit 2\n")
        result = self.run_scan([0], 0, cached_db=True, osv_codes=[0, 0],
                               path_first=other)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(2, self.osv_runs())
        self.assertTrue(any("--sbom" in call for call in self.calls()))

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
    """security/osv-scanner.toml and security/cve-bin-tool-triage.toml only
    silence findings that have a decision row in security/vulnerability_log.md,
    and every such row is listed."""

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

    def test_cve_bin_tool_decisions_match_the_log(self) -> None:
        logged = logged_decisions()
        triaged = set()
        for entry in tomllib.loads(TRIAGE.read_text(encoding="utf-8"))["Triage"]:
            for vuln in entry["ids"]:
                triaged.add(vuln)
                self.assertIn(vuln, logged, f"{vuln} is triaged without a log row")
                date, status, _ = logged[vuln]
                self.assertTrue(entry["reason"].startswith(f"{date}: "), vuln)
                allowed = ({"fixed"} if entry["status"] == "fixed"
                           else {"not_affected", "mitigated"})
                self.assertIn(status, allowed, vuln)
                self.assertEqual(status == "mitigated", "reviewUntil" in entry,
                                 f"{vuln}: mitigated entries, and only they, expire")
        listed = {vuln for vuln, (_, _, decision_text) in logged.items()
                  if "security/cve-bin-tool-triage.toml" in decision_text}
        self.assertEqual(listed, triaged,
                         "log rows naming cve-bin-tool-triage.toml and its entries differ")

    def test_cve_bin_tool_decisions_follow_the_pins(self) -> None:
        # Each decision names the component commit it was made for; a new pin
        # or a due review date leaves it out of the scan and fails here first.
        with tempfile.TemporaryDirectory(prefix="jh triage ") as directory:
            output = Path(directory) / "triage.json"
            result = subprocess.run(
                [sys.executable, str(ROOT / "scripts" / "cve_bin_tool_triage.py"),
                 "--output", str(output)],
                capture_output=True, text=True, timeout=60, check=False)
            self.assertEqual(0, result.returncode, result.stderr)
            self.assertEqual("", result.stderr)
            statements = json.loads(output.read_text(encoding="utf-8"))["statements"]
        entries = tomllib.loads(TRIAGE.read_text(encoding="utf-8"))["Triage"]
        self.assertEqual(sum(len(entry["ids"]) for entry in entries), len(statements))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
