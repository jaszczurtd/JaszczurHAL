#!/usr/bin/env python3
"""Validate pinned third-party component management."""

from __future__ import annotations

from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile
import zipfile


ROOT = Path(sys.argv[1]).resolve()
THIRD_PARTY = ROOT / "third_party"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


configs = (
    "bearssl_version.conf",
    "cjson_version.conf",
    "fatfs_version.conf",
    "lodepng_version.conf",
    "jpeg_version.conf",
    "lwip_version.conf",
    "littlefs_version.conf",
    "freertos_core_version.conf",
    "pico_sdk_version.conf",
    "picotool_version.conf",
    "riscv_toolchain_version.conf",
    "unity_version.conf",
)
for name in configs:
    require((THIRD_PARTY / name).is_file(), f"missing tracked component pin: {name}")
    require(not (ROOT / name).exists(), f"component pin remains in repository root: {name}")

updater = (THIRD_PARTY / "update_components.sh").read_text(encoding="utf-8")
for helper in (
    "ensure_bearssl.sh",
    "ensure_cjson.sh",
    "ensure_fatfs.sh",
    "ensure_lodepng.sh",
    "ensure_jpeg.sh",
    "ensure_lwip.sh",
    "ensure_littlefs.sh",
    "ensure_freertos_kernel.sh",
    "ensure_pico_sdk.sh",
    "ensure_picotool.sh",
    "ensure_riscv_toolchain.sh",
    "ensure_unity.sh",
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
    ROOT / "src/hal/impl/shared/frameworks/cjson/LICENSE",
    ROOT / "src/hal/impl/shared/frameworks/lodepng/LICENSE",
    ROOT / "src/hal/impl/shared/frameworks/jpeg/LICENSE",
    ROOT / "src/hal/impl/shared/frameworks/filesystem/ff16/LICENSE.txt",
    ROOT / "src/hal/impl/shared/frameworks/filesystem/ff16/00history.txt",
    ROOT / "src/hal/impl/shared/frameworks/filesystem/ff16/00readme.txt",
):
    require(not path.exists(), f"tracked vendored dependency remains: {path}")

bearssl_cmake = (ROOT / "cmake/jh_bearssl.cmake").read_text(encoding="utf-8")
lwip_cmake = (ROOT / "cmake/jh_cyw43_driver.cmake").read_text(encoding="utf-8")
littlefs_cmake = (ROOT / "cmake/jh_littlefs.cmake").read_text(encoding="utf-8")
managed_frameworks_cmake = (
    ROOT / "cmake/jh_managed_frameworks.cmake"
).read_text(encoding="utf-8")
require("third_party/BearSSL" in bearssl_cmake, "BearSSL build bypasses third_party")
require(
    "https://github.com/jaszczurtd/bearssl-esp8266.git"
    in (THIRD_PARTY / "bearssl_version.conf").read_text(encoding="utf-8"),
    "BearSSL pin does not use the project fork",
)
require("third_party/lwip" in lwip_cmake, "lwIP build bypasses third_party")
require(
    "third_party/littlefs" in littlefs_cmake,
    "littlefs build bypasses third_party",
)
for component in ("cJSON", "lodepng", "TJpg_Decoder", "FatFs/source", "Unity/src"):
    require(
        f'/{component}"' in managed_frameworks_cmake,
        f"{component} build bypasses third_party",
    )

require(
    "https://github.com/jaszczurtd/lodepng.git"
    in (THIRD_PARTY / "lodepng_version.conf").read_text(encoding="utf-8"),
    "LodePNG pin does not use the project fork",
)
require(
    "https://github.com/jaszczurtd/TJpg_Decoder.git"
    in (THIRD_PARTY / "jpeg_version.conf").read_text(encoding="utf-8"),
    "JPEG pin does not use the project TJpg_Decoder fork",
)
unity_pin = (THIRD_PARTY / "unity_version.conf").read_text(encoding="utf-8")
require(
    "https://github.com/jaszczurtd/Unity.git" in unity_pin,
    "Unity pin does not use the project fork",
)
require(
    "UNITY_REF=f9879bf7d82108c3eefd5fc378983317898616f3" in unity_pin,
    "Unity commit is not pinned",
)
fatfs_pin = (THIRD_PARTY / "fatfs_version.conf").read_text(encoding="utf-8")
require(
    "FATFS_REPO=https://github.com/jaszczurtd/ff16.git" in fatfs_pin,
    "FatFs pin does not use the project ff16 repository",
)
require(
    "FATFS_REF=5a2def719940c2fbe3f6592a220ec4e3f2fb9e6b" in fatfs_pin,
    "FatFs commit is not pinned",
)
require(
    "FATFS_URL=" not in fatfs_pin and "FATFS_SHA256=" not in fatfs_pin,
    "FatFs pin still uses the unreliable archive download",
)
require(
    not tuple(THIRD_PARTY.glob("*.patch")),
    "managed components use tracked patch files",
)
for helper_name in (
    "ensure_bearssl.sh",
    "ensure_cjson.sh",
    "ensure_lodepng.sh",
    "ensure_jpeg.sh",
    "ensure_fatfs.sh",
    "ensure_unity.sh",
):
    helper_text = (ROOT / "scripts" / helper_name).read_text(encoding="utf-8")
    require(
        "jh_dep_ensure_origin" in helper_text,
        f"{helper_name} does not enforce the pinned repository origin",
    )
    if helper_name in {"ensure_bearssl.sh", "ensure_fatfs.sh"}:
        continue
    require(
        "jh_dep_ensure_clean" in helper_text,
        f"{helper_name} does not enforce a clean checkout",
    )
fatfs_helper = (ROOT / "scripts/ensure_fatfs.sh").read_text(encoding="utf-8")
require(
    "jh_dep_sync_pinned" in fatfs_helper,
    "ensure_fatfs.sh does not synchronize the pinned checkout",
)
require(
    "jh_dep_verify_ref" in fatfs_helper,
    "ensure_fatfs.sh does not verify the pinned commit",
)
require(
    "jh_dep_sync_archive" not in fatfs_helper,
    "ensure_fatfs.sh still uses archive synchronization",
)

for component in (
    "BearSSL",
    "cJSON",
    "lodepng",
    "TJpg_Decoder",
    "FatFs",
    "Unity",
    "lwip",
    "littlefs",
):
    ignored = subprocess.run(
        ["git", "check-ignore", "-q", f"third_party/{component}/sentinel"],
        cwd=ROOT,
        check=False,
    )
    require(ignored.returncode == 0, f"third_party/{component} is not ignored")

unity_wrapper = (ROOT / "src/utils/unity.h").read_text(encoding="utf-8")
unity_source_wrapper = (ROOT / "src/utils/unity.c").read_text(encoding="utf-8")
unity_internals_wrapper = (
    ROOT / "src/utils/unity_internals.h"
).read_text(encoding="utf-8")
require(
    '"../../third_party/Unity/src/unity.h"' in unity_wrapper,
    "Unity header bypasses third_party",
)
require("UNITY_VERSION_MAJOR" not in unity_wrapper, "vendored Unity header remains")
require(
    '"../../third_party/Unity/src/unity.c"' in unity_source_wrapper,
    "Unity source bypasses third_party",
)
require(
    '"../../third_party/Unity/src/unity_internals.h"' in unity_internals_wrapper,
    "Unity internals header bypasses third_party",
)
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

    alternate_url = (temp / "alternate").resolve().as_uri()
    git("remote", "set-url", "origin", alternate_url, cwd=checkout)
    origin_command = (
        'source "$1"; '
        'jh_dep_ensure_origin "$2" "$3" "$4"; '
        'git -C "$2" remote get-url origin'
    )
    rejected_origin = subprocess.run(
        [
            "bash",
            "-c",
            origin_command,
            "bash",
            str(helper),
            str(checkout),
            repo_url,
            "1",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    require(rejected_origin.returncode != 0, "verify-only accepted a wrong origin")
    require(
        git("remote", "get-url", "origin", cwd=checkout) == alternate_url,
        "verify-only modified a mismatched origin",
    )
    corrected_origin = subprocess.run(
        [
            "bash",
            "-c",
            origin_command,
            "bash",
            str(helper),
            str(checkout),
            repo_url,
            "0",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    require(corrected_origin.stdout.strip() == repo_url, "wrong origin was not corrected")

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


with tempfile.TemporaryDirectory(prefix="jh-archive-sync-") as temporary:
    temp = Path(temporary)
    archive = temp / "component.zip"
    install = temp / "install"
    with zipfile.ZipFile(archive, "w") as package:
        package.writestr("source/payload.txt", "archive-version\n")
        package.writestr("LICENSE.txt", "test license\n")
    archive_sha256 = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive_command = (
        'source "$1"; '
        'jh_dep_sync_archive "$2" "$3" "$4" "$5"'
    )

    subprocess.run(
        [
            "bash",
            "-c",
            archive_command,
            "bash",
            str(helper),
            archive.resolve().as_uri(),
            archive_sha256,
            str(install),
            "0",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    require(
        (install / "source/payload.txt").read_text(encoding="utf-8")
        == "archive-version\n",
        "initial pinned archive installation failed",
    )

    (install / "source/payload.txt").write_text("modified\n", encoding="utf-8")
    rejected_archive = subprocess.run(
        [
            "bash",
            "-c",
            archive_command,
            "bash",
            str(helper),
            archive.resolve().as_uri(),
            archive_sha256,
            str(install),
            "1",
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        rejected_archive.returncode != 0,
        "verify-only accepted modified archive content",
    )
    require(
        (install / "source/payload.txt").read_text(encoding="utf-8")
        == "modified\n",
        "verify-only changed modified archive content",
    )

    subprocess.run(
        [
            "bash",
            "-c",
            archive_command,
            "bash",
            str(helper),
            archive.resolve().as_uri(),
            archive_sha256,
            str(install),
            "0",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    require(
        (install / "source/payload.txt").read_text(encoding="utf-8")
        == "archive-version\n",
        "modified archive installation was not restored",
    )
