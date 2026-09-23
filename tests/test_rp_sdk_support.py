#!/usr/bin/env python3
"""Behaviour of cmake/jh_rp_sdk_support.cmake and cmake/jh_version_pins.cmake.

Each case configures a small CMake project that includes the real module with a
fake JaszczurHAL root, fake picotool and fake Pico SDK tree, then checks what
the configure reports or where it stops. The last cases run the pinned SDK and
picotool, when present, on the flash layout that the OTA build produces.
"""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import textwrap
import unittest

from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
sys.path.insert(0, str(ROOT / "scripts"))
import component_manager  # noqa: E402
import rp_ota_artifacts  # noqa: E402

CMAKE = shutil.which("cmake") or "cmake"
SUPPORT_MODULE = ROOT / "cmake" / "jh_rp_sdk_support.cmake"
FLASH_MEMORY = Path("src/rp2_common/pico_standard_link/script_include/memory_flash.incl")
MANAGED_PICOTOOL = ROOT / ".build" / "tools" / "picotool" / "picotool"

PROBE = textwrap.dedent(
    """\
    cmake_minimum_required(VERSION 3.20)
    project(jh_rp_sdk_support_probe NONE)
    include("${JH_SUPPORT_MODULE}")
    # Stand-in for the SDK function: records override paths on the target.
    function(pico_add_linker_script_override_path TARGET PATH)
        set_property(TARGET "${TARGET}" APPEND PROPERTY JH_PROBE_OVERRIDES "${PATH}")
    endfunction()
    if(JH_PROBE_READ_PIN)
        jh_read_version_pin("${JH_PROBE_CONF}" "${JH_PROBE_KEY}" _jh_value
            ${JH_PROBE_DEFAULT_ARGS})
        message(STATUS "PIN=[${_jh_value}]")
    endif()
    if(JH_PROBE_CHECK_SDK)
        jh_rp_check_pico_sdk_version()
        message(STATUS "SDK=ok")
    endif()
    if(JH_PROBE_IMPORT_PICOTOOL)
        jh_rp_import_picotool()
        if(TARGET picotool)
            get_target_property(_jh_location picotool IMPORTED_LOCATION)
            message(STATUS "PICOTOOL=${_jh_location}")
        else()
            message(STATUS "PICOTOOL=none")
        endif()
    endif()
    if(JH_PROBE_STALE)
        jh_rp_remove_stale_flash_region()
    endif()
    if(JH_PROBE_REGION)
        add_custom_target(firmware)
        jh_rp_set_flash_region(firmware 16384 1032192)
        get_target_property(_jh_paths firmware JH_PROBE_OVERRIDES)
        message(STATUS "OVERRIDE=${_jh_paths}")
    endif()
    """
)


def write_fake_picotool(path: Path, version: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        f"#!/bin/sh\necho 'picotool v{version} (Linux, GNU-13.3.0, Release)'\n",
        encoding="utf-8",
    )
    path.chmod(0o755)
    return path


def write_relocated_elf(path: Path, machine: int) -> Path:
    """Write a minimal ELF laid out like the RP OTA application.

    The code starts in the slot after the boot image, so nothing lies at the
    flash base, and the core 1 stack occupies RP2350 scratch RAM at
    0x20080000, past the end of RP2040 SRAM.
    """
    code, scratch = 0x10004000, 0x20080000
    segments = (
        # type, offset, vaddr, paddr, filesz, memsz, flags, align
        (1, 0x100, code, code, 0x100, 0x100, 5, 0x100),
        (1, 0, scratch, scratch, 0, 0x800, 6, 0x100),
    )
    image = bytearray(b"\x7fELF" + bytes((1, 1, 1, 0)) + bytes(8))
    image += struct.pack(
        "<HHIIIIIHHHHHH", 2, machine, 1, code | 1, 52, 0, 0, 52, 32, len(segments), 40, 0, 0
    )
    for segment in segments:
        image += struct.pack("<8I", *segment)
    image += bytes(0x100 - len(image)) + bytes(range(256))
    path.write_bytes(image)
    return path


@unittest.skipIf(sys.platform == "win32", "fake tools are POSIX shell scripts")
class RpSdkSupportTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="jh-rp-sdk-support-")
        self.work = Path(self.temporary.name)
        self.jh = self.work / "jh"
        third_party = self.jh / "third_party"
        third_party.mkdir(parents=True)
        (third_party / "pico_sdk_version.conf").write_text(
            "# pinned SDK\nPICO_SDK_VERSION=2.3.0\n", encoding="utf-8"
        )
        (third_party / "picotool_version.conf").write_text(
            "PICOTOOL_VERSION='2.3.0'\n", encoding="utf-8"
        )
        self.source = self.work / "probe"
        self.source.mkdir()
        (self.source / "CMakeLists.txt").write_text(PROBE, encoding="utf-8")
        self.runs = 0

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def configure(self, **definitions: str) -> subprocess.CompletedProcess[str]:
        self.runs += 1
        build = self.work / f"build-{self.runs}"
        command = [
            CMAKE,
            "-S",
            str(self.source),
            "-B",
            str(build),
            f"-DJH_SUPPORT_MODULE={SUPPORT_MODULE}",
            f"-DJH_ROOT={self.jh}",
        ]
        command += [f"-D{key}={value}" for key, value in definitions.items()]
        return subprocess.run(
            command,
            capture_output=True,
            text=True,
            check=False,
            env={**os.environ, "CMAKE_BUILD_PARALLEL_LEVEL": "1"},
        )

    def assert_fails(self, result: subprocess.CompletedProcess[str], text: str) -> None:
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(text, " ".join((result.stdout + result.stderr).split()))

    def assert_reports(self, result: subprocess.CompletedProcess[str], text: str) -> None:
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(text, result.stdout)

    def test_pin_reader_matches_component_manager(self) -> None:
        conf = self.work / "sample_version.conf"
        conf.write_text(
            "# comment line\n"
            "SAMPLE_REF=0123abcd\n"
            'SAMPLE_SUBMODULES="lib/tinyusb lib/mbedtls"\n'
            "SAMPLE_QUOTED='v1.2.3'\n"
            "SAMPLE_REPEATED=first\n"
            "SAMPLE_REPEATED=second\n",
            encoding="utf-8",
        )
        expected = component_manager.parse_config(conf)
        for key in ("SAMPLE_REF", "SAMPLE_SUBMODULES", "SAMPLE_QUOTED", "SAMPLE_REPEATED"):
            with self.subTest(key=key):
                result = self.configure(
                    JH_PROBE_READ_PIN="ON", JH_PROBE_CONF=str(conf), JH_PROBE_KEY=key
                )
                self.assert_reports(result, f"PIN=[{expected[key]}]")

    def test_pin_reader_default_and_missing_key(self) -> None:
        conf = self.work / "sample_version.conf"
        conf.write_text("OTHER_KEY=1\n", encoding="utf-8")
        with_default = self.configure(
            JH_PROBE_READ_PIN="ON",
            JH_PROBE_CONF=str(conf),
            JH_PROBE_KEY="SAMPLE_DIR",
            JH_PROBE_DEFAULT_ARGS="DEFAULT;third_party/sample",
        )
        self.assert_reports(with_default, "PIN=[third_party/sample]")
        missing_file = self.configure(
            JH_PROBE_READ_PIN="ON",
            JH_PROBE_CONF=str(self.work / "absent.conf"),
            JH_PROBE_KEY="SAMPLE_DIR",
            JH_PROBE_DEFAULT_ARGS="DEFAULT;fallback",
        )
        self.assert_reports(missing_file, "PIN=[fallback]")
        required = self.configure(
            JH_PROBE_READ_PIN="ON", JH_PROBE_CONF=str(conf), JH_PROBE_KEY="SAMPLE_DIR"
        )
        self.assert_fails(required, "does not assign SAMPLE_DIR")

    def test_sdk_checkout_must_match_the_pin(self) -> None:
        self.assert_reports(
            self.configure(JH_PROBE_CHECK_SDK="ON", PICO_SDK_VERSION_STRING="2.3.0"),
            "SDK=ok",
        )
        self.assert_fails(
            self.configure(JH_PROBE_CHECK_SDK="ON", PICO_SDK_VERSION_STRING="2.2.0"),
            "does not match the pinned 2.3.0",
        )

    def test_picotool_must_report_the_pinned_version(self) -> None:
        current = write_fake_picotool(self.work / "tools/current/picotool", "2.3.0")
        stale = write_fake_picotool(self.work / "tools/stale/picotool", "2.2.0")
        self.assert_reports(
            self.configure(JH_PROBE_IMPORT_PICOTOOL="ON", JH_PICOTOOL_EXECUTABLE=str(current)),
            f"PICOTOOL={current}",
        )
        self.assert_fails(
            self.configure(JH_PROBE_IMPORT_PICOTOOL="ON", JH_PICOTOOL_EXECUTABLE=str(stale)),
            "but 2.3.0 is pinned",
        )
        self.assert_fails(
            self.configure(
                JH_PROBE_IMPORT_PICOTOOL="ON",
                JH_PICOTOOL_EXECUTABLE=str(self.work / "tools/missing/picotool"),
            ),
            "JH_PICOTOOL_EXECUTABLE does not exist",
        )

    def test_managed_picotool_is_the_fallback(self) -> None:
        self.assert_reports(self.configure(JH_PROBE_IMPORT_PICOTOOL="ON"), "PICOTOOL=none")
        managed = write_fake_picotool(self.jh / ".build/tools/picotool/picotool", "2.3.0")
        self.assert_reports(self.configure(JH_PROBE_IMPORT_PICOTOOL="ON"), f"PICOTOOL={managed}")
        write_fake_picotool(managed, "2.2.0")
        self.assert_fails(self.configure(JH_PROBE_IMPORT_PICOTOOL="ON"), "but 2.3.0 is pinned")

    def fake_sdk(self, memory_flash: str) -> Path:
        sdk = self.work / "sdk"
        (sdk / FLASH_MEMORY).parent.mkdir(parents=True, exist_ok=True)
        (sdk / FLASH_MEMORY).write_text(memory_flash, encoding="utf-8")
        return sdk

    def test_flash_region_override_moves_the_region(self) -> None:
        sdk = self.fake_sdk('MEMORY\n{\n    INCLUDE "pico_flash_region.ld"\n}\n')
        result = self.configure(JH_PROBE_REGION="ON", PICO_SDK_PATH=str(sdk))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        region_dir = self.work / f"build-{self.runs}/jh_flash_region/firmware"
        self.assertIn(f"OVERRIDE={region_dir}", result.stdout)
        self.assertEqual(
            (region_dir / "pico_flash_region.ld").read_text(encoding="utf-8"),
            "FLASH(rx) : ORIGIN = 0x10000000 + 16384, LENGTH = 1032192\n",
        )

    def test_region_left_by_an_older_sdk_is_removed(self) -> None:
        # GNU ld reads INCLUDE files from its working directory (the top build
        # directory) first, so this copy from SDK 2.2 would win over -L paths.
        sdk = self.fake_sdk('MEMORY\n{\n    INCLUDE "pico_flash_region.ld"\n}\n')
        build = self.work / f"build-{self.runs + 1}"
        build.mkdir()
        stale = build / "pico_flash_region.ld"
        stale.write_text("FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 2097152\n", encoding="utf-8")
        result = self.configure(JH_PROBE_STALE="ON", JH_PROBE_REGION="ON", PICO_SDK_PATH=str(sdk))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(stale.exists())
        self.assertIn("left by an older Pico SDK", result.stdout)
        self.assertTrue((build / "jh_flash_region/firmware/pico_flash_region.ld").is_file())

    def test_flash_region_override_refuses_an_sdk_without_the_include(self) -> None:
        sdk = self.fake_sdk("MEMORY\n{\n    FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 4M\n}\n")
        self.assert_fails(
            self.configure(JH_PROBE_REGION="ON", PICO_SDK_PATH=str(sdk)),
            "no longer takes the FLASH region from pico_flash_region.ld",
        )

    @unittest.skipUnless(
        (ROOT / "third_party/pico-sdk" / FLASH_MEMORY).is_file(),
        "pinned Pico SDK checkout is not present",
    )
    def test_pinned_sdk_accepts_the_flash_region_override(self) -> None:
        result = self.configure(
            JH_PROBE_REGION="ON", PICO_SDK_PATH=str(ROOT / "third_party/pico-sdk")
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(MANAGED_PICOTOOL.is_file(), "managed picotool is not built")
    def test_pinned_picotool_converts_a_relocated_rp2350_image(self) -> None:
        # picotool 2.3.0 took an image without code at the flash base for an
        # RP2040 RAM binary and rejected the RP2350 scratch RAM segment. The
        # arguments follow pico_add_uf2_output() in the pinned SDK: the family
        # and platform derived from PICO_PLATFORM, then the RP2350-E10 block.
        for machine, pico_platform in ((40, "rp2350-arm-s"), (243, "rp2350-riscv")):
            with self.subTest(pico_platform=pico_platform):
                elf = write_relocated_elf(self.work / f"{pico_platform}.elf", machine)
                uf2 = self.work / f"{pico_platform}.uf2"
                result = subprocess.run(
                    [
                        str(MANAGED_PICOTOOL), "uf2", "convert", "--quiet",
                        str(elf), str(uf2), "--family", pico_platform,
                        "--platform", "rp2350", "--abs-block",
                    ],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                rp_ota_artifacts.check_range(uf2, 0x10004000, 0x10004100)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
