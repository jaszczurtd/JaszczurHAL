#!/usr/bin/env python3
"""Validate pinned third-party component management."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys


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
    "btstack_version.conf",
    "sx126x_driver_version.conf",
    "freertos_core_version.conf",
    "pico_sdk_version.conf",
    "esp_idf_version.conf",
    "picotool_version.conf",
    "pmd_version.conf",
    "riscv_toolchain_version.conf",
    "unity_version.conf",
    "windows_tools_version.conf",
)
for name in configs:
    require((THIRD_PARTY / name).is_file(), f"missing tracked component pin: {name}")
    require(not (ROOT / name).exists(), f"component pin remains in repository root: {name}")

pmd_pin = (THIRD_PARTY / "pmd_version.conf").read_text(encoding="utf-8")
require("PMD_VERSION=7.26.0" in pmd_pin, "PMD CPD version is not pinned to 7.26.0")
require(
    "PMD_SHA256=9f55cb7ff0e9f9a66dd2f005eaa370e84c8a4cd971b134aa14a930c4a283ebc9"
    in pmd_pin,
    "PMD CPD archive digest is not pinned",
)

updater = (THIRD_PARTY / "update_components.sh").read_text(encoding="utf-8")
require(
    "scripts/component_manager.py" in updater and " all " in updater,
    "central updater does not invoke the cross-platform component manager",
)

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
    ROOT / "src/hal/network/tls/BearSSL/vendor",
    ROOT / "src/hal/network/lwip/vendor",
    ROOT / "src/hal/storage/littlefs/vendor",
    ROOT / "src/hal/codecs/cjson/LICENSE",
    ROOT / "src/hal/codecs/lodepng/LICENSE",
    ROOT / "src/hal/codecs/jpeg/LICENSE",
    ROOT / "src/hal/storage/filesystem/ff16/LICENSE.txt",
    ROOT / "src/hal/storage/filesystem/ff16/00history.txt",
    ROOT / "src/hal/storage/filesystem/ff16/00readme.txt",
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
managed_contract = json.loads(
    (ROOT / "config/tooling/managed_components.json").read_text(encoding="utf-8")
)
wrapper_components = {
    helper_name: component_name
    for component_name, helper_name in managed_contract["launchers"].items()
}
for helper_name, component_name in wrapper_components.items():
    helper_text = (ROOT / "scripts" / helper_name).read_text(encoding="utf-8")
    require(
        "component_manager.py" in helper_text
        and f"component {component_name}" in helper_text,
        f"{helper_name} is not a thin component-manager wrapper",
    )
require(
    all(
        "pinned_repo.sh" not in (ROOT / "scripts" / name).read_text(encoding="utf-8")
        for name in wrapper_components
    ),
    "a production component wrapper still uses the Bash-only manager",
)
require(
    not (ROOT / "scripts/lib/pinned_repo.sh").exists(),
    "unused Bash component manager was retained",
)

stm32_freertos_cmake = (
    ROOT / "stm32_lib/freertos_stm32g474.cmake"
).read_text(encoding="utf-8")
require(
    '"${Python3_EXECUTABLE}" "${_helper}"' in stm32_freertos_cmake
    and "scripts/component_manager.py" in stm32_freertos_cmake
    and "component freertos" in stm32_freertos_cmake,
    "STM32 FreeRTOS CMake still invokes a host-specific dependency helper",
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
    "BTstack",
    "sx126x_driver",
    "esp-idf",
    "pmd",
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
