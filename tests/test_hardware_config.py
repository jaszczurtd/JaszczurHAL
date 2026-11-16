#!/usr/bin/env python3
"""Native hardware CLI, ABI, generated headers and HAL module combinations.
Specification and scalar cases run in the C test jh_hardware.
"""

from __future__ import annotations

import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Sequence
import unittest

from repo_root import hal_include_dirs, repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
sys.path.insert(0, str(ROOT / "scripts"))

import device_tree_project as hardware  # noqa: E402
from device_tree_project import HardwareError  # noqa: E402
from project_config import jh_executable  # noqa: E402

FIXTURES = ROOT / "tests" / "fixtures" / "device_tree"
WORK = ROOT / ".build" / "tests" / "hardware-config"
TOOL = jh_executable()
HARDWARE_HEADER = "jh_hardware.h"
RESOLVED_JSON = "jh_hardware_resolved.json"
CONFIG_JSON = "jh_hardware_config.json"
BOARD_HEADER = "jh_board_config.h"
CMAKE_RECORD = "jh_hardware_config.cmake"
PACKAGE_RECORD = "jh_hardware_package.cmake"
RESOLVE_OUTPUTS = (HARDWARE_HEADER, RESOLVED_JSON)
FINALIZE_OUTPUTS = (BOARD_HEADER, CMAKE_RECORD, PACKAGE_RECORD, "jh_link_contract.h",
                    "jh_link_contract_definition.c", "jh_link_contract_reference.c", CONFIG_JSON, "generation.d")
CYW43_DEFINITIONS = ["HAL_ENABLE_WIFI", "HAL_NETWORK_BACKEND_CYW43", "HAL_CYW43_BUS_STM32_GSPI",
                     "HAL_CYW43_STACK_LWIP"]
RP_CYW43_DEFINITIONS = ["HAL_ENABLE_WIFI", "HAL_NETWORK_BACKEND_CYW43", "HAL_CYW43_BUS_PICO_PIO",
                        "HAL_CYW43_STACK_LWIP"]


def resolve(path: Path, assembly: str | None = None, target: str | None = None) -> dict:
    return hardware.resolved(path.parent, target, assembly)["document"]


def workdir(name: str) -> Path:
    path = WORK / name
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True)
    return path


def copy_fixture(name: str, relative: str) -> Path:
    """A copy of one device tree directory, keeping ../../bindings reachable."""
    work = workdir(name)
    shutil.copytree(FIXTURES / "bindings", work / "bindings")
    target = work / "a" / "b"
    shutil.copytree(FIXTURES / relative, target)
    return target


def mutate(name: str, relative: str, change) -> Path:
    directory = copy_fixture(name, relative)
    path = directory / "device_tree.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    change(data)
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")
    return path


def run(command: str, *arguments: str) -> subprocess.CompletedProcess:
    return subprocess.run([str(TOOL), "hardware", command, "--root", str(ROOT), *arguments], capture_output=True, text=True)


def generate(name: str, config_dir: Path, assembly: str, *defines: str,
             variant: str | None = None, compiler: Sequence[str] = ()) -> tuple[Path, subprocess.CompletedProcess]:
    """resolve then finalize into one output directory; ``compiler`` is the
    compiler and its flags whose ABI finalize measures."""
    out = workdir(name)
    resolved = run("resolve", "--config-dir", str(config_dir), "--assembly", assembly,
                   "--output-dir", str(out))
    if resolved.returncode:
        return out, resolved
    arguments = ["finalize", "--hardware", str(out / RESOLVED_JSON),
                 "--hardware-header", str(out / HARDWARE_HEADER),
                 "--config-dir", str(config_dir), "--output-dir", str(out)]
    for definition in defines:
        arguments += ["--define", definition]
    if variant:
        arguments += ["--variant", variant]
    if compiler:
        arguments += ["--compiler", compiler[0], *(f"--compiler-flag={flag}" for flag in compiler[1:])]
    return out, run(*arguments)


def macros(path: Path) -> dict[str, str]:
    """Object-like #define values outside the C++ branches."""
    values, skip = {}, False
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("#ifdef __cplusplus"):
            skip = True
        elif line.startswith(("#else", "#endif")):
            skip = False
        match = re.match(r"#define (\w+)(?: (.*?))?(?: /\*.*\*/)?$", line)
        if match and not skip:
            values[match.group(1)] = match.group(2) or ""
    return values


def value(name: str, *headers: dict[str, str]) -> str:
    """A macro followed through the other macros it names."""
    merged = {key: text for header in headers for key, text in header.items()}
    text = merged[name]
    while text in merged:
        text = merged[text]
    return text




class CommandLine(unittest.TestCase):
    def test_gpio_input_modes_compile_in_c_and_cpp(self) -> None:
        config = workdir("gpio-input-modes")
        nodes = {name: {"compatible": "jaszczurhal,gpio-input"}
                 for name in ("floating", "up", "down", "absent")}
        nodes["up"]["properties"] = {"pull": "down"}
        assembly_nodes = {
            "floating": {"present": True, "pins": {"in": 2}},
            "up": {"present": True, "pins": {"in": 3}, "properties": {"pull": "up"}},
            "down": {"present": True, "pins": {"in": 4}, "properties": {"pull": "down"}},
            "absent": {"present": False},
        }
        (config / "device_tree.json").write_text(json.dumps({
            "schemaVersion": 1, "nodes": nodes,
            "assemblies": {"pico": {"target": "rp2040", "board": "pico",
                                      "clock": {"tree": "default"},
                                      "statusLed": {"node": "board.statusLed"},
                                      "nodes": assembly_nodes}},
        }), encoding="utf-8")
        output = config / "output"
        result = run("resolve", "--config-dir", str(config), "--output-dir", str(output))
        self.assertEqual(0, result.returncode, result.stderr)
        header = macros(output / "jh_hardware.h")
        self.assertNotIn("JH_HW_NODE_ABSENT_INPUT_MODE", header)
        self.assertEqual("0", header["JH_HW_NODE_UP_CONFIG_AVAILABLE"])
        for compiler, standard, suffix, assertion in (
                ("cc", "c11", "c", "_Static_assert"),
                ("c++", "c++17", "cpp", "static_assert")):
            with self.subTest(language=standard):
                source = config / f"probe.{suffix}"
                source.write_text(
                    '#include "jh_hardware.h"\n#include "hal/gpio/hal_gpio.h"\n'
                    + "\n".join(f'{assertion}(JH_HW_NODE_{node}_INPUT_MODE == {mode}, "pull");'
                                for node, mode in (("FLOATING", "HAL_GPIO_INPUT"),
                                                   ("UP", "HAL_GPIO_INPUT_PULLUP"),
                                                   ("DOWN", "HAL_GPIO_INPUT_PULLDOWN")))
                    + '\nconst hal_gpio_mode_t mode = JH_HW_NODE_DOWN_INPUT_MODE;\n', encoding="utf-8")
                result = subprocess.run([compiler, f"-std={standard}", "-Wall", "-Wextra", "-Werror",
                                         "-DHAL_TARGET_MOCK=1", *(f"-I{path}" for path in hal_include_dirs(ROOT)), f"-I{output}",
                                         "-fsyntax-only", str(source)], capture_output=True, text=True)
                self.assertEqual(0, result.returncode, result.stderr)

    def test_list_prints_the_assemblies(self) -> None:
        result = run("list", "--config-dir", str(FIXTURES / "valid" / "selection"))
        self.assertEqual(0, result.returncode, result.stderr)
        listing = json.loads(result.stdout)
        self.assertEqual(["desk", "nucleo", "vehicle"], [item["name"] for item in listing["assemblies"]])
        self.assertEqual({"name": "nucleo", "target": "stm32g474", "board": "nucleo-g474re"},
                         listing["assemblies"][1])

    def test_a_failed_resolve_leaves_no_output(self) -> None:
        config = FIXTURES / "valid" / "selection"
        out, generated = generate("failed-resolve", config, "desk")
        self.assertEqual(0, generated.returncode, generated.stderr)
        result = run("resolve", "--config-dir", str(config), "--target", "rp2040", "--output-dir", str(out))
        self.assertNotEqual(0, result.returncode)
        self.assertIn("[JH-HW-ASSEMBLY]", result.stderr)
        # Nothing the earlier model produced survives either.
        self.assertEqual([], [name for name in (*RESOLVE_OUTPUTS, *FINALIZE_OUTPUTS)
                              if (out / name).exists()])

    def test_outputs_stay_in_a_managed_build_directory(self) -> None:
        result = run("resolve", "--config-dir", str(FIXTURES / "valid" / "status"),
                     "--output-dir", str(ROOT / "tests" / "not-a-build-directory"))
        self.assertNotEqual(0, result.returncode)
        self.assertFalse((ROOT / "tests" / "not-a-build-directory").exists())

    def test_finalize_refuses_a_stale_or_foreign_model(self) -> None:
        config = copy_fixture("stale", "valid/status")
        out, result = generate("stale-out", config, "pico")
        self.assertEqual(0, result.returncode, result.stderr)
        finalize = ["finalize", "--hardware", str(out / RESOLVED_JSON), "--hardware-header",
                    str(out / HARDWARE_HEADER), "--config-dir", str(config), "--output-dir", str(out)]
        header = out / HARDWARE_HEADER
        original = header.read_text(encoding="utf-8")
        header.write_text(original.replace('_SHA256 "', '_SHA256 "0', 1).replace('0"', '"', 1),
                          encoding="utf-8")
        self.assertIn("not the one this model produces", run(*finalize).stderr)
        header.write_text(original, encoding="utf-8")
        tree = config / "device_tree.json"
        tree.write_text(tree.read_text(encoding="utf-8").replace('"out": 14', '"out": 15'), encoding="utf-8")
        result = run(*finalize)
        self.assertIn("device_tree.json changed after resolve", result.stderr)
        self.assertFalse((out / "jh_board_config.h").exists())

    def test_hardware_macros_cannot_be_defined_again(self) -> None:
        for definition in ("HAL_LED_BUILTIN=3", "JH_HW_NODE_LED_PIN_OUT=15U"):
            with self.subTest(definition=definition):
                _, result = generate("owned", FIXTURES / "valid" / "status", "pico", definition)
                self.assertNotEqual(0, result.returncode)
                self.assertIn("[JH-HW-FEATURE]", result.stderr)
                self.assertIn("argument --define", result.stderr)

    def test_project_files_cannot_redefine_generated_macros(self) -> None:
        for text, line in (("#undef JH_HW_NODE_LED_PIN_OUT\n#define JH_HW_NODE_LED_PIN_OUT 15U\n", 2),
                           ("#if 0\n#define HAL_LED_BUILTIN 15u\n#endif\n", 3)):
            with self.subTest(text=text):
                config = copy_fixture("project-owned", "valid/status")
                (config / "hal_project_config.h").write_text("#pragma once\n" + text, encoding="utf-8")
                _, result = generate("project-owned-out", config, "pico")
                self.assertIn("[JH-HW-FEATURE]", result.stderr)
                self.assertIn(f"hal_project_config.h:{line}:", result.stderr)

    def test_an_edited_header_is_refused(self) -> None:
        config = FIXTURES / "valid" / "status"
        out, result = generate("edited-header", config, "pico")
        self.assertEqual(0, result.returncode, result.stderr)
        header = out / HARDWARE_HEADER
        header.write_text(header.read_text(encoding="utf-8").replace(
            "#define JH_HW_NODE_LED_PIN_OUT 14U", "#define JH_HW_NODE_LED_PIN_OUT 15U"), encoding="utf-8")
        result = run("finalize", "--hardware", str(out / RESOLVED_JSON), "--hardware-header",
                     str(header), "--config-dir", str(config), "--output-dir", str(out))
        self.assertIn("[JH-HW-GENERATION]", result.stderr)
        self.assertIn("not the one this model produces", result.stderr)

    def test_program_configuration_reads_the_early_header(self) -> None:
        for fixture, enabled in (("valid/providers", True), ("valid/selection", False)):
            with self.subTest(fixture=fixture):
                config = copy_fixture(f"early-{enabled}", fixture)
                (config / "hal_project_config.h").write_text(
                    "#pragma once\n#if JH_HW_HAS_CYW43\n#define HAL_ENABLE_WIFI\n"
                    "#define HAL_NETWORK_BACKEND_CYW43\n#define HAL_CYW43_BUS_STM32_GSPI\n"
                    "#define HAL_CYW43_STACK_LWIP\n#endif\n", encoding="utf-8")
                out, result = generate(f"early-out-{enabled}", config, "nucleo")
                self.assertEqual(0, result.returncode, result.stderr)
                final = json.loads((out / CONFIG_JSON).read_text(encoding="utf-8"))
                self.assertEqual(enabled, "HAL_ENABLE_WIFI" in final["software"]["resolvedFeatures"])

    def test_unknown_variant_fails(self) -> None:
        _, result = generate("variant", FIXTURES / "valid" / "project", "pico-rp2040", variant="MISSING")
        self.assertIn("[JH-HW-FEATURE]", result.stderr)

    def test_dependencies_name_every_input(self) -> None:
        model = resolve(FIXTURES / "valid" / "project" / "device_tree.json", "nucleo")
        names = {item["path"] for item in model["dependencies"]}
        for expected in ("device_tree.json", "../../bindings/power-stage.json",
                         "hal:boards/profiles/nucleo-g474re.json", "hal:boards/targets/stm32g474.json",
                         "hal:config/hardware/clocks/stm32g474.json",
                         "hal:config/hardware/bindings/jaszczurhal-gpio-output.json",
                         "hal:tools/jh/hardware/jh_hardware.c", "hal:src/hal/security/jh_sha256.c",
                         "hal:scripts/jh_tool_inputs.cmake"):
            self.assertIn(expected, names)


class Identity(unittest.TestCase):
    def test_location_and_formatting_do_not_change_the_hash(self) -> None:
        here = resolve(FIXTURES / "valid" / "project" / "device_tree.json", "nucleo")
        moved = copy_fixture("moved", "valid/project") / "device_tree.json"
        data = json.loads(moved.read_text(encoding="utf-8"))
        moved.write_text(json.dumps(data, separators=(",", ":"), sort_keys=True), encoding="utf-8")
        self.assertEqual(here["hardwareSha256"],
                         resolve(moved, "nucleo")["hardwareSha256"])


    def test_a_pin_or_an_unused_node_changes_the_hash(self) -> None:
        base = resolve(FIXTURES / "valid" / "project" / "device_tree.json", "nucleo")["hardwareSha256"]
        pin = mutate("pin", "valid/project", lambda d: d["assemblies"]["nucleo"]["nodes"]["stage"]["pins"]
                     .update(driveA="PC8"))
        self.assertNotEqual(base, resolve(pin, "nucleo")["hardwareSha256"])

    def test_integers_hash_at_their_exact_value(self) -> None:
        models = []
        for index, token in enumerate(("9007199254740992.0", "9007199254740993.0")):
            config = copy_fixture(f"uint64-{index}", "valid/presence")
            (config / "big.json").write_text(json.dumps({
                "schemaVersion": 1, "compatible": "test,big", "description": "Large value.",
                "domain": "custom", "properties": {"big": {
                    "type": "integer", "cType": "uint64", "unit": "1", "description": "Value.",
                    "default": 1}}}).replace('"default": 1', f'"default": {token}'), encoding="utf-8")
            tree = config / "device_tree.json"
            data = json.loads(tree.read_text(encoding="utf-8"))
            data.update(bindings=["big.json"], nodes={"n": {"compatible": "test,big"}})
            data["assemblies"]["pico"]["nodes"] = {"n": {"present": True}}
            tree.write_text(json.dumps(data), encoding="utf-8")
            models.append(resolve(tree, "pico"))
        values = [model["hardware"]["nodes"]["n"]["properties"]["big"]["value"] for model in models]
        self.assertEqual([2**53, 2**53 + 1], values)
        self.assertTrue(all(type(item) is int for item in values))
        self.assertNotEqual(*(model["hardwareSha256"] for model in models))
        self.assertIn('"value": 9007199254740993', json.dumps(models[1]))



    def test_resolved_json_keeps_the_literal_digits(self) -> None:
        out = workdir("literal-digits")
        result = run("resolve", "--config-dir", str(FIXTURES / "valid" / "project"),
                     "--assembly", "nucleo", "--output-dir", str(out))
        self.assertEqual(0, result.returncode, result.stderr)
        text = (out / RESOLVED_JSON).read_text(encoding="utf-8")
        self.assertIn('"value": 2.50000000e-01', text)
        self.assertEqual(0.25, json.loads(text)["hardware"]["nodes"]["stage"]["properties"]["shuntOhms"]["value"])


class BuildRecords(unittest.TestCase):
    """What finalize gives a build and an installed package."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.out, result = generate("records", FIXTURES / "hal" / "nucleo-canhat", "canhat", "HAL_ENABLE_WIFI")
        assert result.returncode == 0, result.stderr
        cls.config = json.loads((cls.out / CONFIG_JSON).read_text(encoding="utf-8"))

    def record(self, name: str) -> dict[str, str]:
        text = (self.out / name).read_text(encoding="utf-8")
        return {match.group(1): match.group(2) or match.group(3)
                for match in re.finditer(r'set\((\w+) (?:"([^"]*)"|\[=*\[(.*?)\]=*\])\)', text)}

    def test_a_disabled_feature_value_is_refused(self) -> None:
        _, result = generate("records-zero", FIXTURES / "hal" / "nucleo-canhat", "canhat", "HAL_ENABLE_WIFI=0")
        self.assertEqual(2, result.returncode)
        self.assertIn("JH-HW-FEATURE", result.stderr)
        self.assertIn("JH-CFG-VALUE", result.stderr)

    def test_the_board_header_belongs_to_one_hardware_generation(self) -> None:
        early = macros(self.out / HARDWARE_HEADER)
        text = (self.out / BOARD_HEADER).read_text(encoding="utf-8")
        identity = "0x" + self.config["hardwareSha256"][:16] + "ULL"
        self.assertEqual(identity, early["JH_HW_HARDWARE_ID"])
        self.assertIn(f"#if JH_HW_HARDWARE_ID != {identity}\n#error", text)
        self.assertIn(f"#define HAL_BOARD_HARDWARE_ID {identity}\n", text)

    def test_identity_names_the_finalized_configuration(self) -> None:
        result = run("identity", "--config-dir", str(FIXTURES / "hal" / "nucleo-canhat"),
                     "--assembly", "canhat", "--define", "HAL_ENABLE_WIFI")
        self.assertEqual(0, result.returncode, result.stderr)
        identity = json.loads(result.stdout)
        record = self.record(CMAKE_RECORD)
        self.assertEqual(identity["configSha256"], record["JH_CONFIG_SHA256"])
        self.assertEqual(identity["selection"], self.config["selection"])
        self.assertTrue(record["JH_BOARD_CONTRACT_SYMBOL"].endswith(identity["configSha256"][:12]))
        other = json.loads(run("identity", "--config-dir", str(FIXTURES / "hal" / "nucleo-canhat"),
                               "--assembly", "canhat").stdout)
        self.assertNotEqual(identity["configSha256"], other["configSha256"])
        self.assertEqual(identity["hardwareSha256"], other["hardwareSha256"])

    def test_the_abi_is_part_of_the_configuration(self) -> None:
        self.assertEqual({"isa": "armv7e-m", "fpu": 1, "recipe": "stm32g474"}, self.config["software"]["abi"])
        self.assertEqual("isa=armv7e-m;fpu=1;recipe=stm32g474", self.record(CMAKE_RECORD)["JH_HARDWARE_ABI"])

    def test_the_package_record_follows_its_installation(self) -> None:
        text = (self.out / PACKAGE_RECORD).read_text(encoding="utf-8")
        self.assertNotIn(str(ROOT), text)
        self.assertNotIn("JH_BOARD_INPUT_GLOBS", text)
        prefix = workdir("records-package") / "prefix with space"
        share = prefix / "share" / "JaszczurHAL" / "generated"
        share.mkdir(parents=True)
        shutil.copy2(self.out / PACKAGE_RECORD, share / CMAKE_RECORD)
        script = prefix.parent / "print.cmake"
        script.write_text(f'include("{(share / CMAKE_RECORD).as_posix()}")\n'
                          'foreach(name JH_HARDWARE_HEADER JH_HAL_PACKAGE_LIBRARY JH_CONFIG_SHA256 JH_ASSEMBLY)\n'
                          '  message("${name}=${${name}}")\n'
                          'endforeach()\n', encoding="utf-8")
        result = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True)
        self.assertEqual(0, result.returncode, result.stderr)
        printed = dict(line.split("=", 1) for line in result.stderr.splitlines() if "=" in line)
        self.assertEqual((prefix / "include" / "generated" / "jh_hardware.h").as_posix(),
                         printed["JH_HARDWARE_HEADER"])
        self.assertEqual((prefix / "lib" / "libJaszczurHAL.a").as_posix(), printed["JH_HAL_PACKAGE_LIBRARY"])
        self.assertEqual(self.config["configSha256"], printed["JH_CONFIG_SHA256"])
        self.assertEqual("canhat", printed["JH_ASSEMBLY"])

    def test_a_variant_joins_the_configuration(self) -> None:
        config = workdir("records-variant") / "config"
        config.mkdir()
        shutil.copy2(FIXTURES / "hal" / "nucleo-canhat" / "device_tree.json", config)
        (config / "hal_project_config.h").write_text(
            '#define JH_PROJECT_VARIANTS(X) X(SERIAL, "Adds the UART", HAL_ENABLE_UART)\n',
            encoding="utf-8")
        out, result = generate("records-variant-out", config, "canhat", variant="SERIAL")
        self.assertEqual(0, result.returncode, result.stderr)
        final = json.loads((out / CONFIG_JSON).read_text(encoding="utf-8"))
        self.assertEqual("SERIAL", final["software"]["variant"])
        self.assertIn("HAL_ENABLE_UART", final["software"]["resolvedFeatures"])
        self.assertIn("set(JH_PROJECT_VARIANT [=[SERIAL]=])",
                      (out / CMAKE_RECORD).read_text(encoding="utf-8"))
        variant = json.loads(run("identity", "--config-dir", str(config), "--assembly", "canhat",
                                 "--variant", "SERIAL").stdout)
        base = json.loads(run("identity", "--config-dir", str(config), "--assembly", "canhat").stdout)
        self.assertEqual(final["configSha256"], variant["configSha256"])
        self.assertNotEqual(base["configSha256"], variant["configSha256"])
        self.assertEqual(base["hardwareSha256"], variant["hardwareSha256"])

    def test_compiler_flags_need_the_compiler(self) -> None:
        result = run("identity", "--config-dir", str(FIXTURES / "hal" / "nucleo-canhat"),
                     "--assembly", "canhat", "--compiler-flag=-mfloat-abi=soft")
        self.assertEqual(2, result.returncode)
        self.assertIn("--compiler-flag needs --compiler", result.stderr)


@unittest.skipUnless(shutil.which("cc") and shutil.which("c++"), "the layout checks need a host C and C++ compiler")
class StructLayout(unittest.TestCase):
    """Packing and enum size change no predefined macro; the measured layout
    tells such builds apart and the board header refuses a unit with another."""

    FLAGS = {"natural": (), "packed": ("-fpack-struct=1",), "short enums": ("-fshort-enums",)}

    @classmethod
    def setUpClass(cls) -> None:
        cls.out, cls.config = {}, {}
        for name, flags in cls.FLAGS.items():
            out, result = generate(f"layout {name}", FIXTURES / "hal" / "nucleo-canhat", "canhat",
                                   compiler=("cc", *flags))
            assert result.returncode == 0, result.stderr
            cls.out[name] = out
            cls.config[name] = json.loads((out / CONFIG_JSON).read_text(encoding="utf-8"))

    def test_the_layout_joins_the_configuration(self) -> None:
        abi = {name: config["software"]["abi"] for name, config in self.config.items()}
        # The premise: the compiler macros alone do not see these flags.
        self.assertEqual(abi["natural"]["compiler"], abi["packed"]["compiler"])
        self.assertEqual(abi["natural"]["compiler"], abi["short enums"]["compiler"])
        self.assertEqual(4, abi["natural"]["layout"]["member_int"])
        self.assertEqual(1, abi["packed"]["layout"]["member_int"])
        self.assertEqual(4, abi["natural"]["layout"]["enum"])
        self.assertEqual(1, abi["short enums"]["layout"]["enum"])
        self.assertEqual(3, len({config["configSha256"] for config in self.config.values()}))
        record = (self.out["packed"] / CMAKE_RECORD).read_text(encoding="utf-8")
        self.assertIn("layout.member_int=1", record)

    def compile(self, name: str, language: str, *flags: str, prelude: str = "") -> subprocess.CompletedProcess:
        """A unit that includes the board header of the natural build."""
        source = workdir(f"layout unit {name}") / ("unit.cpp" if language == "c++" else "unit.c")
        source.write_text(f'{prelude}#include "jh_hardware.h"\n#include "jh_board_config.h"\n'
                          "int main(void) { return 0; }\n", encoding="utf-8")
        compiler, standard = ("c++", "-std=c++17") if language == "c++" else ("cc", "-std=c11")
        return subprocess.run([compiler, standard, "-Wall", "-Wextra", "-Wpedantic", "-Werror", *flags,
                               f"-I{self.out['natural']}", *[f"-I{path}" for path in hal_include_dirs(ROOT)],
                               "-fsyntax-only", str(source)],
                              capture_output=True, text=True)

    def test_a_unit_with_the_same_layout_compiles(self) -> None:
        for language in ("c", "c++"):
            with self.subTest(language=language):
                result = self.compile(f"same {language}", language)
                self.assertEqual(0, result.returncode, result.stderr)

    def test_a_unit_with_another_layout_stops(self) -> None:
        cases = {"packed": ("-fpack-struct=1",), "short enums": ("-fshort-enums",), "pragma": ()}
        for language in ("c", "c++"):
            for name, flags in cases.items():
                with self.subTest(language=language, case=name):
                    # A #pragma pack around the include changes the layout without a flag.
                    prelude = "#pragma pack(push, 1)\n" if name == "pragma" else ""
                    result = self.compile(f"{name} {language}", language, *flags, prelude=prelude)
                    self.assertNotEqual(0, result.returncode)
                    self.assertIn("another struct layout than the configuration", result.stderr)

    def test_the_measurement_writes_nothing_where_it_runs(self) -> None:
        # Flags such as -fstack-usage write files next to the compile.
        out = workdir("layout cwd")
        here = out / "cwd"
        here.mkdir()
        result = subprocess.run([str(TOOL), "hardware", "identity", "--root", str(ROOT), "--config-dir",
                                 str(FIXTURES / "hal" / "nucleo-canhat"), "--assembly", "canhat",
                                 "--compiler", "cc", "--compiler-flag=-fstack-usage",
                                 "--compiler-flag=-save-temps"], capture_output=True, text=True, cwd=here)
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual([], list(here.iterdir()))

    def test_a_failed_write_is_a_diagnostic(self) -> None:
        blocker = workdir("records-blocked") / "file"
        blocker.write_text("not a directory\n", encoding="utf-8")
        result = run("resolve", "--config-dir", str(FIXTURES / "hal" / "nucleo-canhat"), "--assembly",
                     "canhat", "--output-dir", str(blocker / "out"))
        self.assertEqual(2, result.returncode)
        self.assertIn("[JH-HW-GENERATION]", result.stderr)
        self.assertNotIn("Traceback", result.stderr)


class Headers(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.out, result = generate("headers", FIXTURES / "hal" / "nucleo-canhat", "canhat", "HAL_ENABLE_WIFI")
        assert result.returncode == 0, result.stderr
        cls.early = macros(cls.out / "jh_hardware.h")
        cls.late = macros(cls.out / "jh_board_config.h")

    def test_each_macro_has_one_owner(self) -> None:
        self.assertTrue(all(name.startswith("JH_HW_") for name in self.early))
        self.assertFalse(any(name.startswith("JH_HW_") for name in self.late))
        self.assertNotIn("#include", (self.out / "jh_hardware.h").read_text(encoding="utf-8"))

    def test_canhat_example_signals_on_the_hat_ready_led(self) -> None:
        # CAN-HAT projects signal on the RDY LED of the HAT (PC1), chosen as
        # the status LED of their assembly.
        out, result = generate("canhat-example", ROOT / "examples" / "21_stm32g474_fdcan_native",
                               "nucleo-canhat")
        self.assertEqual(0, result.returncode, result.stderr)
        early, late = macros(out / "jh_hardware.h"), macros(out / "jh_board_config.h")
        self.assertEqual("JH_HW_NODE_HAT_LED_READY_PIN_OUT", late["HAL_BOARD_STATUS_LED_PIN"])
        self.assertEqual("33U", value("HAL_LED_BUILTIN", early, late))

    def test_late_values_refer_to_the_early_ones(self) -> None:
        self.assertEqual("JH_HW_NODE_BOARD_STATUS_LED_PIN_OUT", self.late["HAL_BOARD_STATUS_LED_PIN"])
        self.assertEqual("JH_HW_HAS_CYW43", self.late["HAL_BOARD_HAS_CYW43"])
        self.assertEqual("1", value("HAL_BOARD_HAS_CYW43", self.early, self.late))
        self.assertIn("HAL_STM32G474_CLOCK_HSE_160MHZ", self.late)
        self.assertIn("JH_HW_NODE_HAT_CAN2_PIN_TX", self.late["HAL_BOARD_CAN_CHANNELS(X)"]
                      if "HAL_BOARD_CAN_CHANNELS(X)" in self.late else
                      (self.out / "jh_board_config.h").read_text(encoding="utf-8"))
        cmake = (self.out / CMAKE_RECORD).read_text(encoding="utf-8")
        compiled = re.search(r'set\(JH_BOARD_COMPILE_DEFINITIONS "([^"]*)"\)', cmake).group(1).split(";")
        # The module radio brings its component's transport to the compiler;
        # the macros the header owns, such as its pins and the clock, stay off -D.
        self.assertEqual({"HAL_NETWORK_BACKEND_CYW43", "HAL_CYW43_BUS_STM32_GSPI", "HAL_CYW43_STACK_LWIP",
                          "HAL_CYW43_MAX_TRANSACTION_BYTES=2048u"}, set(compiled))
        self.assertEqual("JH_HW_NODE_WIFI_RADIO_PIN_WL_ON", self.late["HAL_CYW43_PIN_WL_ON"])
        self.assertEqual("1", self.late["HAL_CYW43_BUS_STM32_GSPI"])
        # The PIM730 adds the STM32 transport, lwIP and the Bluetooth host to
        # the components of the board and its HSE.
        components = re.search(r'set\(JH_BOARD_COMPONENTS "([^"]*)"\)', cmake).group(1).split(";")
        self.assertEqual(["btstack-host", "cyw43-lwip", "cyw43-stm32-gspi", "stm32g474",
                          "stm32g474-hse-24mhz"], components)

    def test_absent_and_disconnected_nodes(self) -> None:
        out, result = generate("presence", FIXTURES / "valid" / "presence", "pico")
        self.assertEqual(0, result.returncode, result.stderr)
        early = macros(out / "jh_hardware.h")
        self.assertEqual("0", early["JH_HW_NODE_ABSENT_LED_PRESENT"])
        self.assertFalse(any(name.startswith("JH_HW_NODE_ABSENT_LED_PIN") for name in early))
        self.assertEqual("0", early["JH_HW_NODE_UNCONNECTED_LED_PIN_OUT_CONNECTED"])
        self.assertNotIn("JH_HW_NODE_UNCONNECTED_LED_PIN_OUT", early)

    def test_device_pins_export_provider_and_index(self) -> None:
        early = hardware.resolved(FIXTURES / "valid" / "expander", assembly="pico")["header"]
        self.assertIn('#define JH_HW_NODE_LED_PIN_OUT_PROVIDER "io0"', early)
        self.assertIn("#define JH_HW_NODE_LED_PIN_OUT_INDEX 0U", early)
        self.assertNotIn("#define JH_HW_NODE_LED_PIN_OUT ", early)

    def test_status_led_choices_reach_the_late_header(self) -> None:
        out, result = generate("esp32s3", FIXTURES / "valid" / "backends", "esp32s3")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn("HAL_BOARD_STATUS_LED_PIXEL_ORDER_GRB", macros(out / "jh_board_config.h"))
        out, result = generate("host", FIXTURES / "valid" / "backends", "host")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertNotIn("HAL_LED_BUILTIN", macros(out / "jh_board_config.h"))

    def test_status_led_order_is_one_value_in_both_headers(self) -> None:
        # The binding default is GRB; an assembly choosing RGB must reach the
        # node property that hal_rgb_led_init_board_status() reads, not only
        # the late header.
        config = copy_fixture("status-led-rgb", "valid/backends")
        tree = json.loads((config / "device_tree.json").read_text(encoding="utf-8"))
        tree["assemblies"]["esp32s3"]["statusLed"]["pixelOrder"] = "RGB"
        (config / "device_tree.json").write_text(json.dumps(tree), encoding="utf-8")
        out, result = generate("status-led-rgb-out", config, "esp32s3")
        self.assertEqual(0, result.returncode, result.stderr)
        early = macros(out / "jh_hardware.h")
        late = macros(out / "jh_board_config.h")
        self.assertEqual('"RGB"', early["JH_HW_NODE_BOARD_STATUS_LED_PROP_PIXEL_ORDER"])
        self.assertIn("HAL_BOARD_STATUS_LED_PIXEL_ORDER_RGB", late)
        self.assertNotIn("HAL_BOARD_STATUS_LED_PIXEL_ORDER_GRB", late)

    def test_released_soft_reservation_leaves_the_mask(self) -> None:
        out, result = generate("released", FIXTURES / "valid" / "released-pin", "pico")
        self.assertEqual(0, result.returncode, result.stderr)
        mask = int(macros(out / "jh_board_config.h")["HAL_BOARD_GPIO_SOFT_RESERVED_MASK"][9:-1], 16)
        self.assertFalse(mask & (1 << 25))


class ModuleCombinations(unittest.TestCase):
    """Combinations of HAL types and registry boards from the design notes."""

    canhat = FIXTURES / "hal" / "nucleo-canhat" / "device_tree.json"
    display = FIXTURES / "hal" / "nucleo-display" / "device_tree.json"

    def rejected(self, path: Path, assembly: str) -> HardwareError:
        with self.assertRaises(HardwareError) as raised:
            resolve(path, assembly)
        return raised.exception

    def hal_mutate(self, name: str, fixture: str, change) -> Path:
        work = workdir(name)
        data = json.loads((FIXTURES / "hal" / fixture / "device_tree.json").read_text(encoding="utf-8"))
        change(data)
        (work / "device_tree.json").write_text(json.dumps(data, indent=2), encoding="utf-8")
        return work / "device_tree.json"

    def test_pim730_collides_with_the_display_bus_until_rewired(self) -> None:
        resolve(self.display, "display")

        def onto_spi2(data: dict) -> None:
            data["assemblies"]["display"]["buses"]["lcd"].update(
                index=1, pins={"sck": "PB13", "mosi": "PB15", "miso": "PB14"})
        error = self.rejected(self.hal_mutate("display-spi2", "nucleo-display", onto_spi2), "display")
        self.assertEqual(("JH-HW-PIN-CONFLICT", "/assemblies/display/nodes/wifi/pins/clock"),
                         (error.code, error.pointer))
        self.assertEqual(("/assemblies/display/buses/lcd/pins/sck",), error.related)

    def test_pim730_collides_with_the_can_hat_until_rewired(self) -> None:
        resolve(self.canhat, "canhat")

        def legacy_pins(data: dict) -> None:
            data["assemblies"]["canhat"]["nodes"]["wifi"]["pins"] = {
                "cs": "PB12", "clock": "PB13", "wlOn": "PB14", "data": "PB15"}
        error = self.rejected(self.hal_mutate("canhat-legacy", "nucleo-canhat", legacy_pins), "canhat")
        self.assertEqual(("JH-HW-PIN-CONFLICT", "/assemblies/canhat/nodes/wifi/pins/clock"),
                         (error.code, error.pointer))
        self.assertEqual(("/assemblies/canhat/nodes/hat.can2/pins/tx",), error.related)

    def test_a_connected_unused_relay_still_owns_its_pin(self) -> None:
        def on_relay(data: dict) -> None:
            data["assemblies"]["canhat"]["nodes"]["wifi"]["pins"]["wlOn"] = "PC3"
        error = self.rejected(self.hal_mutate("canhat-relay", "nucleo-canhat", on_relay), "canhat")
        self.assertEqual(("JH-HW-PIN-CONFLICT", ("/assemblies/canhat/nodes/hat.relay1/pins/out",)),
                         (error.code, error.related))

    def test_two_channels_on_one_fdcan(self) -> None:
        def twice(data: dict) -> None:
            data["assemblies"]["canhat"]["nodes"]["hat.can2"]["controller"]["index"] = 0
        error = self.rejected(self.hal_mutate("canhat-fdcan", "nucleo-canhat", twice), "canhat")
        self.assertEqual(("JH-HW-CONTROLLER", "/assemblies/canhat/nodes/hat.can2/controller"),
                         (error.code, error.pointer))

    def test_fdcan_reads_pa0_as_its_default_pin(self) -> None:
        def on_pa0(data: dict) -> None:
            nodes = data["assemblies"]["canhat"]["nodes"]
            nodes["wifi"]["pins"]["wlOn"] = "PC4"
            nodes["hat.can1"]["pins"]["rx"] = "PA0"
        error = self.rejected(self.hal_mutate("canhat-pa0", "nucleo-canhat", on_pa0), "canhat")
        self.assertEqual(("JH-HW-PIN-RANGE", "/assemblies/canhat/nodes/hat.can1/pins/rx"),
                         (error.code, error.pointer))

    def test_two_leds_share_the_ld2_signal(self) -> None:
        nodes = resolve(self.canhat, "canhat")["hardware"]["nodes"]
        self.assertEqual("board.statusLed.out", nodes["hat.ledCan2Tx"]["signals"]["out"]["owner"])
        self.assertEqual("board.statusLed.out", nodes["board.statusLed"]["signals"]["out"]["owner"])

    def test_pins_above_63_follow_the_reservations(self) -> None:
        for pin, code in (("PG10", "JH-HW-RESERVATION"), ("PF1", "JH-HW-RESERVATION"),
                          ("PF2", "JH-HW-PIN-RANGE")):
            with self.subTest(pin=pin):
                path = self.hal_mutate(f"canhat-{pin}", "nucleo-canhat", lambda data: data["assemblies"]
                                       ["canhat"]["nodes"]["wifi"]["pins"].update(clock=pin))
                error = self.rejected(path, "canhat")
                self.assertEqual((code, "/assemblies/canhat/nodes/wifi/pins/clock"), (error.code, error.pointer))

    def test_a_clock_tree_needs_its_board_source(self) -> None:
        other = self.hal_mutate("pico-hse", "pico-lora",
                                lambda d: d["assemblies"]["pico"]["clock"].update(tree="hse-160mhz"))
        self.assertEqual("JH-HW-CLOCK", self.rejected(other, "pico").code)

    def test_mcp2515_takes_its_fixed_spi_rate_only(self) -> None:
        path = self.hal_mutate("mcp2515-rate", "pico-lora", lambda d: d["assemblies"]["pico"]["buses"]
                               ["can"].update(frequencyHz=8000000))
        error = self.rejected(path, "pico")
        self.assertEqual(("JH-HW-TRANSPORT", "/assemblies/pico/buses/can/frequencyHz"),
                         (error.code, error.pointer))

    def test_radio_clock_stays_below_its_module_limit(self) -> None:
        path = self.hal_mutate("lora-rate", "pico-lora", lambda d: d["assemblies"]["pico"]["buses"]
                               ["radio"].update(frequencyHz=20000000))
        error = self.rejected(path, "pico")
        self.assertEqual(("JH-HW-TRANSPORT", "/assemblies/pico/buses/radio/frequencyHz"),
                         (error.code, error.pointer))

    def test_selection_follows_the_isa(self) -> None:
        path = FIXTURES / "valid" / "project" / "device_tree.json"
        self.assertEqual("pico2-riscv", resolve(path, target="rp2350-riscv")["selection"]["assembly"])
        self.assertEqual("rp2350-arm", resolve(path, "pico2-arm")["selection"]["target"])

    def test_a_project_reads_only_its_own_bindings(self) -> None:
        config = copy_fixture("isolation", "valid/status")
        (config.parent / "sibling.json").write_text(
            (FIXTURES / "bindings" / "gpio-input.json").read_text(encoding="utf-8")
            .replace("test,gpio-input", "test,sibling"), encoding="utf-8")
        tree = config / "device_tree.json"
        data = json.loads(tree.read_text(encoding="utf-8"))
        data["nodes"]["led"]["compatible"] = "test,sibling"
        tree.write_text(json.dumps(data), encoding="utf-8")
        with self.assertRaises(HardwareError) as raised:
            resolve(tree, "pico")
        self.assertEqual(("JH-HW-BINDING", "/nodes/led/compatible"),
                         (raised.exception.code, raised.exception.pointer))

    def test_a_board_radio_and_a_module_radio_need_a_choice(self) -> None:
        # The Pico W carries a CYW43; a PIM730 module on free pins adds another.
        def module_on_pico_w(data: dict) -> None:
            data["assemblies"] = {"picow": data["assemblies"]["picow"]}
            data["assemblies"]["picow"]["nodes"]["wifi"] = {
                "present": True, "pins": {"wlOn": 2, "cs": 3, "data": 4, "clock": 5}}
        path = self.hal_mutate("two-radios", "library", module_on_pico_w)
        hardware = resolve(path, "picow")["hardware"]
        self.assertIsNone(hardware["providers"].get("cyw43"))
        self.assertFalse(hardware["capabilities"]["cyw43"])
        for choice in ("board", "wifi.radio"):
            with self.subTest(choice=choice):
                data = json.loads(path.read_text(encoding="utf-8"))
                data["assemblies"]["picow"]["providers"] = {"cyw43": choice}
                path.write_text(json.dumps(data), encoding="utf-8")
                hardware = resolve(path, "picow")["hardware"]
                self.assertEqual(choice, hardware["providers"]["cyw43"])
                self.assertTrue(hardware["capabilities"]["cyw43"])
                out, result = generate(f"two-radios-{choice}", path.parent, "picow", *RP_CYW43_DEFINITIONS)
                self.assertEqual(0, result.returncode, result.stderr)
                late = macros(out / "jh_board_config.h")
                # The board radio keeps the pins of its board; the module's
                # come from its node.
                expected = None if choice == "board" else "JH_HW_NODE_WIFI_RADIO_PIN_WL_ON"
                self.assertEqual(expected, late.get("HAL_CYW43_PIN_WL_ON"))
        data = json.loads(path.read_text(encoding="utf-8"))
        del data["assemblies"]["picow"]["providers"]
        path.write_text(json.dumps(data), encoding="utf-8")
        out, result = generate("two-radios-none", path.parent, "picow")
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertNotIn("HAL_NETWORK_BACKEND_CYW43", macros(out / "jh_board_config.h"))

    def test_a_net_keeps_the_signal_domains(self) -> None:
        def dc_on_expander(data: dict) -> None:
            data["nodes"]["lcd"] = {"compatible": "ilitek,ili9341"}
            data["assemblies"]["lora-lf"]["nodes"]["lcd"] = {"present": False}
            assembly = data["assemblies"]["pico"]
            assembly["nets"] = {"dc": {"endpoint": {"domain": "device-pin", "node": "io", "index": 5},
                                       "owner": "lcd.dc"}}
            assembly["nodes"]["can"] = {"present": False}
            assembly["nodes"]["lcd"] = {"present": True, "bus": "can", "pins": {"cs": 5, "dc": {"net": "dc"}}}
        error = self.rejected(self.hal_mutate("dc-on-expander", "pico-lora", dc_on_expander), "pico")
        self.assertEqual(("JH-HW-PIN-RANGE", "/assemblies/pico/nodes/lcd/pins/dc"), (error.code, error.pointer))

    def test_shared_outputs_need_one_polarity(self) -> None:
        path = mutate("opposite-leds", "valid/shared",
                      lambda d: d["nodes"]["secondLed"].update(properties={"activeLow": True}))
        error = self.rejected(path, "pico")
        self.assertEqual(("JH-HW-NET-OWNER", "/assemblies/pico/nodes/secondLed/pins/out"),
                         (error.code, error.pointer))
        self.assertEqual(("/assemblies/pico/nodes/firstLed/pins/out",), error.related)

    def test_a_missing_bus_line_leaves_no_initializer(self) -> None:
        for fixture, assembly, bus, line, node in (("nucleo-display", "display", "lcd", "mosi", "display"),
                                                   ("pico-lora", "pico", "can", "miso", "can")):
            with self.subTest(node=node):
                path = self.hal_mutate(f"no-{line}", fixture, lambda d: d["assemblies"][assembly]["buses"]
                                       [bus]["pins"].update({line: None}))
                header = hardware.resolved(path.parent, assembly=assembly)["header"]
                self.assertNotIn(f"#define JH_HW_NODE_{node.upper()}_CONFIG_INIT", header)
        header = hardware.resolved(self.display.parent, assembly="display")["header"]
        self.assertIn("#define JH_HW_NODE_DISPLAY_CONFIG_INIT", header)

    def test_one_phase_reports_its_first_pointer(self) -> None:
        def two_errors(data: dict) -> None:
            assembly = data["assemblies"]["pico"]
            assembly["buses"]["i2c"]["index"] = 7
            assembly["nodes"]["fast"]["pins"]["cs"] = 99
        error = self.rejected(mutate("bus-and-pin", "valid/transports", two_errors), "pico")
        self.assertEqual(("JH-HW-CONTROLLER", "/assemblies/pico/buses/i2c/index"), (error.code, error.pointer))

        def condition_and_property(data: dict) -> None:
            nodes = data["assemblies"]["pico"]["nodes"]
            nodes["disabledSensor"]["pins"]["irq"] = 18
            nodes["enabledSensor"]["properties"] = {"voltage": -1e40}
        error = self.rejected(mutate("condition-and-property", "valid/children", condition_and_property), "pico")
        self.assertEqual(("JH-HW-PROPERTY", "/assemblies/pico/nodes/disabledSensor/pins/irq"),
                         (error.code, error.pointer))

    def test_wifi_needs_the_selected_module_complete(self) -> None:
        def incomplete(data: dict) -> None:
            data["assemblies"]["display"]["nodes"]["wifi"]["pins"]["cs"] = None
        config = self.hal_mutate("wifi-incomplete", "nucleo-display", incomplete).parent
        _, result = generate("wifi-incomplete-out", config, "display", *CYW43_DEFINITIONS)
        self.assertIn("[JH-HW-FEATURE]", result.stderr)
        self.assertIn(":/assemblies/display/providers/cyw43:", result.stderr)
        _, result = generate("wifi-complete-out", FIXTURES / "hal" / "nucleo-display", "display",
                             *CYW43_DEFINITIONS, "HAL_ENABLE_BLE")
        self.assertEqual(0, result.returncode, result.stderr)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
