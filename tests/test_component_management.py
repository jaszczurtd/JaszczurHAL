#!/usr/bin/env python3
"""Validate pinned third-party component management."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(sys.argv[1]).resolve()
THIRD_PARTY = ROOT / "third_party"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


configs = (
    "bearssl_version.conf",
    "lwip_version.conf",
    "littlefs_version.conf",
    "freertos_core_version.conf",
    "pico_sdk_version.conf",
    "picotool_version.conf",
    "riscv_toolchain_version.conf",
)
for name in configs:
    require((THIRD_PARTY / name).is_file(), f"missing tracked component pin: {name}")
    require(not (ROOT / name).exists(), f"component pin remains in repository root: {name}")

updater = (THIRD_PARTY / "update_components.sh").read_text(encoding="utf-8")
for helper in (
    "ensure_bearssl.sh",
    "ensure_lwip.sh",
    "ensure_littlefs.sh",
    "ensure_freertos_kernel.sh",
    "ensure_pico_sdk.sh",
    "ensure_picotool.sh",
    "ensure_riscv_toolchain.sh",
):
    require(helper in updater, f"central updater does not invoke {helper}")

runmefirst = (ROOT / "runmefirst.sh").read_text(encoding="utf-8")
require(
    "third_party/update_components.sh" in runmefirst,
    "runmefirst does not use the central component updater",
)
require(
    "/scripts/ensure_" not in runmefirst,
    "runmefirst bypasses the central component updater",
)
require(
    "scripts/configure_ota_firewall.py" in runmefirst,
    "runmefirst does not prepare the OTA callback firewall",
)

for path in (
    ROOT / "src/hal/impl/shared/frameworks/BearSSL/vendor",
    ROOT / "src/hal/impl/shared/frameworks/lwip/vendor",
    ROOT / "src/hal/impl/shared/frameworks/littlefs",
):
    require(not path.exists(), f"tracked vendored dependency remains: {path}")

bearssl_cmake = (ROOT / "cmake/jh_bearssl.cmake").read_text(encoding="utf-8")
lwip_cmake = (ROOT / "cmake/jh_cyw43_driver.cmake").read_text(encoding="utf-8")
littlefs_cmake = (ROOT / "cmake/jh_littlefs.cmake").read_text(encoding="utf-8")
require("third_party/BearSSL" in bearssl_cmake, "BearSSL build bypasses third_party")
require("third_party/lwip" in lwip_cmake, "lwIP build bypasses third_party")
require(
    "third_party/littlefs" in littlefs_cmake,
    "littlefs build bypasses third_party",
)

for component in ("BearSSL", "lwip", "littlefs"):
    ignored = subprocess.run(
        ["git", "check-ignore", "-q", f"third_party/{component}/sentinel"],
        cwd=ROOT,
        check=False,
    )
    require(ignored.returncode == 0, f"third_party/{component} is not ignored")
for name in configs:
    ignored = subprocess.run(
        ["git", "check-ignore", "-q", f"third_party/{name}"],
        cwd=ROOT,
        check=False,
    )
    require(ignored.returncode != 0, f"tracked pin is ignored: {name}")


def git(*args: str, cwd: Path) -> str:
    result = subprocess.run(
        ["git", *args],
        cwd=cwd,
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


with tempfile.TemporaryDirectory(prefix="jh-component-sync-") as temporary:
    temp = Path(temporary)
    upstream = temp / "upstream"
    checkout = temp / "checkout"
    upstream.mkdir()
    git("init", "-q", cwd=upstream)
    git("config", "user.name", "JaszczurHAL Test", cwd=upstream)
    git("config", "user.email", "test@jaszczurhal.invalid", cwd=upstream)

    payload = upstream / "payload.txt"
    payload.write_text("version-one\n", encoding="utf-8")
    git("add", "payload.txt", cwd=upstream)
    git("commit", "-q", "-m", "version one", cwd=upstream)
    first_ref = git("rev-parse", "HEAD", cwd=upstream)

    payload.write_text("version-two\n", encoding="utf-8")
    git("commit", "-q", "-am", "version two", cwd=upstream)
    second_ref = git("rev-parse", "HEAD", cwd=upstream)

    sync_command = (
        'source "$1"; '
        'jh_dep_sync_pinned "$2" "$3" "$4" "$5"; '
        'git -C "$4" rev-parse HEAD'
    )
    helper = ROOT / "scripts/lib/pinned_repo.sh"
    repo_url = upstream.resolve().as_uri()

    first = subprocess.run(
        [
            "bash",
            "-c",
            sync_command,
            "bash",
            str(helper),
            repo_url,
            first_ref,
            str(checkout),
            "0",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    require(first.stdout.strip() == first_ref, "initial pinned checkout failed")

    replaced = subprocess.run(
        [
            "bash",
            "-c",
            sync_command,
            "bash",
            str(helper),
            repo_url,
            second_ref,
            str(checkout),
            "0",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    require(replaced.stdout.strip() == second_ref, "mismatched checkout was not replaced")
    require(
        (checkout / "payload.txt").read_text(encoding="utf-8") == "version-two\n",
        "replacement did not install the pinned component content",
    )

    rejected = subprocess.run(
        [
            "bash",
            "-c",
            sync_command,
            "bash",
            str(helper),
            repo_url,
            first_ref,
            str(checkout),
            "1",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    require(rejected.returncode != 0, "verify-only accepted a mismatched component")
    require(
        git("rev-parse", "HEAD", cwd=checkout) == second_ref,
        "verify-only modified a mismatched component",
    )
