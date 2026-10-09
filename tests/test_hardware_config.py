#!/usr/bin/env python3
"""generate_hardware_config.py against the device tree specification: every
case of cases.json, the command line, identity, both headers and the module
combinations of the HAL types on registry boards."""

from __future__ import annotations

import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import unittest

from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
sys.path.insert(0, str(ROOT / "scripts"))

import clock_registry  # noqa: E402
import generate_hardware_config as generator  # noqa: E402
from hardware_model import HardwareError  # noqa: E402
import hardware_output  # noqa: E402
import hardware_renderers as renderers  # noqa: E402
import hardware_resolver as resolver  # noqa: E402
import hardware_schema  # noqa: E402

FIXTURES = ROOT / "tests" / "fixtures" / "device_tree"
CASES = json.loads((FIXTURES / "cases.json").read_text(encoding="utf-8"))
WORK = ROOT / ".build" / "tests" / "hardware-config"
SCRIPT = ROOT / "scripts" / "generate_hardware_config.py"
REGISTRY = resolver.load_registry()
CYW43_DEFINITIONS = ["HAL_ENABLE_WIFI", "HAL_NETWORK_BACKEND_CYW43", "HAL_CYW43_BUS_STM32_GSPI",
                     "HAL_CYW43_STACK_LWIP"]


def resolve(path: Path, assembly: str | None = None, target: str | None = None,
            registry: resolver.Registry = REGISTRY) -> dict:
    return resolver.Resolver(path, registry).resolve(target, assembly)


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


def run(*arguments: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(SCRIPT), *arguments], capture_output=True, text=True)


def generate(name: str, config_dir: Path, assembly: str, *defines: str,
             variant: str | None = None) -> tuple[Path, subprocess.CompletedProcess]:
    """resolve then finalize into one output directory."""
    out = workdir(name)
    resolved = run("resolve", "--config-dir", str(config_dir), "--assembly", assembly,
                   "--output-dir", str(out))
    if resolved.returncode:
        return out, resolved
    arguments = ["finalize", "--hardware", str(out / generator.RESOLVED_JSON),
                 "--hardware-header", str(out / generator.HARDWARE_HEADER),
                 "--config-dir", str(config_dir), "--output-dir", str(out)]
    for definition in defines:
        arguments += ["--define", definition]
    if variant:
        arguments += ["--variant", variant]
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


class SpecificationCases(unittest.TestCase):
    """Every expectation of cases.json."""

    def test_resolver_cases(self) -> None:
        for case in CASES["cases"]:
            if case["schema"] != "device_tree.schema.json" or case.get("phase") == "finalize":
                continue
            path = FIXTURES / case["file"]
            if case["expected"] == "accept":
                for assembly in case["resolveAssemblies"]:
                    with self.subTest(file=case["file"], assembly=assembly):
                        resolve(path, assembly)
                continue
            with self.subTest(file=case["file"]):
                with self.assertRaises(HardwareError) as raised:
                    resolve(path, case.get("input", {}).get("assembly"))
                error = raised.exception
                self.assertEqual((case["error"], case["at"]), (error.code, error.pointer), error)
                self.assertEqual(Path(error.source).resolve(), path.resolve())
                if "related" in case:
                    self.assertEqual(tuple(case["related"]), error.related)

    def test_clock_source_cases(self) -> None:
        stm32_inputs = REGISTRY.targets["stm32g474"]["clockInputs"]
        for case in CASES["cases"]:
            if "clockTree" not in case:
                continue
            with self.subTest(file=case["file"]):
                trees = hardware_schema.load_json(FIXTURES / case["clockRegistry"])["trees"]
                errors = clock_registry.source_errors(hardware_schema.load_json(FIXTURES / case["file"]),
                                                      trees[case["clockTree"]], stm32_inputs)
                self.assertEqual(case["at"], errors[0][0])

    def test_selection_cases(self) -> None:
        for case in CASES["selectionCases"]:
            with self.subTest(file=case["file"], input=case["input"]):
                path = FIXTURES / case["file"]
                data = resolver.load_device_tree(path)
                arguments = (path, data, case["input"].get("target"), case["input"].get("assembly"))
                if "error" in case:
                    with self.assertRaises(HardwareError) as raised:
                        resolver.select_assembly(*arguments)
                    self.assertEqual(case["error"], raised.exception.code)
                    self.assertTrue(raised.exception.argument)
                else:
                    self.assertEqual(case["assembly"], resolver.select_assembly(*arguments))

    def test_resolution_cases(self) -> None:
        for case in CASES["resolutionCases"]:
            with self.subTest(file=case["file"]):
                hardware = resolve(FIXTURES / case["file"], case["assembly"])["hardware"]
                nodes = hardware["nodes"]

                def signal(key: str) -> dict:
                    path, _, name = key.rpartition(".")
                    return nodes[path]["signals"][name]

                for key, expected in case.get("properties", {}).items():
                    path, _, name = key.rpartition(".")
                    self.assertEqual(expected, nodes[path]["properties"][name]["value"], key)
                for key, owner in case.get("owners", {}).items():
                    self.assertEqual(owner, signal(key)["owner"], key)
                for key in case.get("unconnected", []):
                    self.assertIsNone(signal(key)["endpoint"], key)
                for key, expected in case.get("transport", {}).items():
                    self.assertEqual(expected, nodes[key]["transport"], key)
                for key in case.get("noDrivingInitializer", []):
                    self.assertIsNone(renderers.config_init(key, nodes[key], hardware))
                    self.assertTrue(all(item["owner"] != f"{key}.{name}"
                                        for name, item in nodes[key]["signals"].items()))
                if "uart" in case:
                    bus = next(item for item in hardware["buses"].values() if item["kind"] == "uart")
                    self.assertEqual(case["uart"]["baudRate"], bus["baudRate"])
                    self.assertEqual(case["uart"]["frameConfig"], renderers.uart_frame_token(bus["frame"]))
                if "statusLed" in case:
                    for key, expected in case["statusLed"].items():
                        self.assertEqual(expected, hardware["statusLed"][key])
                for capability, path in case.get("providers", {}).items():
                    self.assertEqual(path, hardware["providers"][capability])
                if "releasedReservations" in case:
                    self.assertEqual(case["releasedReservations"],
                                     sorted(k for k, v in hardware["reservations"].items() if v["released"]))
                if "boardMacros" in case:
                    out, result = generate(f"resolution-{case['file'].split('/')[1]}",
                                           (FIXTURES / case["file"]).parent, case["assembly"])
                    self.assertEqual(0, result.returncode, result.stderr)
                    headers = macros(out / "jh_hardware.h"), macros(out / "jh_board_config.h")
                    for name, expected in case["boardMacros"].items():
                        self.assertEqual(str(expected), value(name, *headers), name)

    def test_feature_cases(self) -> None:
        for index, case in enumerate(CASES["featureCases"]):
            with self.subTest(file=case["file"], definitions=case["definitions"]):
                config = (FIXTURES / case["file"]).parent
                defines = [f"{name}={number}" for name, number in case["definitions"].items()]
                out, result = generate(f"feature-{index}", config, case["assembly"], *defines)
                if "error" in case:
                    self.assertNotEqual(0, result.returncode)
                    self.assertIn(f"[{case['error']}]", result.stderr)
                    self.assertIn(case["definition"], result.stderr)
                    self.assertIn(f"argument {case['argument']}", result.stderr)
                    self.assertFalse((out / "jh_board_config.h").exists())
                    model = resolve(FIXTURES / case["file"], case["assembly"])["hardware"]
                    for name, expected in case["boardMacros"].items():
                        capability = name.removeprefix("HAL_BOARD_HAS_").lower().replace("_", "-")
                        self.assertEqual(bool(expected), model["capabilities"][capability])
                    continue
                self.assertEqual(0, result.returncode, result.stderr)
                headers = macros(out / "jh_hardware.h"), macros(out / "jh_board_config.h")
                for name, expected in case.get("boardMacros", {}).items():
                    self.assertEqual(str(expected), value(name, *headers), name)
                final = json.loads((out / generator.CONFIG_JSON).read_text(encoding="utf-8"))
                for feature in case.get("doesNotEnable", []):
                    self.assertNotIn(feature, final["software"]["resolvedFeatures"])

    def test_finalize_phase_cases(self) -> None:
        for case in CASES["cases"]:
            if case.get("phase") != "finalize":
                continue
            with self.subTest(file=case["file"]):
                work = workdir(f"phase-{Path(case['file']).stem}")
                shutil.copytree(FIXTURES / "bindings", work / "bindings")
                config = work / "invalid"
                config.mkdir()
                shutil.copy(FIXTURES / case["file"], config / "device_tree.json")
                defines = [f"{name}={number}" for name, number in case["definitions"].items()]
                _, result = generate(f"phase-out-{Path(case['file']).stem}", config,
                                     case["input"]["assembly"], *defines)
                self.assertNotEqual(0, result.returncode)
                self.assertIn(f"[{case['error']}]", result.stderr)
                self.assertIn(f":{case['at']}:", result.stderr)

    def test_header_cases(self) -> None:
        for index, case in enumerate(CASES["headerCases"]):
            with self.subTest(file=case["file"]):
                config = copy_fixture(f"header-{index}", str(Path(case["deviceTree"]).parent))
                shutil.copy(FIXTURES / case["file"], config / "hal_project_config.h")
                assembly = resolver.list_assemblies(resolver.load_device_tree(config / "device_tree.json"))[0]
                _, result = generate(f"header-out-{index}", config, assembly["name"])
                self.assertNotEqual(0, result.returncode)
                self.assertIn(f"[{case['error']}]", result.stderr)
                self.assertIn(f"hal_project_config.h:{case['line']}:", result.stderr)

    def test_renderer_cases(self) -> None:
        for case in CASES["rendererCases"]:
            with self.subTest(case=case):
                if "frame" in case:
                    problem = renderers.uart_frame_problem(case["renderer"].split("-", 1)[1], case["frame"])
                    self.assertEqual(case["at"], problem[0])
                    continue
                problem = renderers.transport_problem_for(case["renderer"], case["transport"])
                if "error" in case:
                    self.assertEqual(case["at"], f"/transport/{problem[0]}")
                    continue
                self.assertIsNone(problem)
                config_type, clock_field = renderers.RENDERER_TYPES[case["renderer"]]
                self.assertEqual(case["configType"], config_type)
                self.assertEqual(case.get("clockField"), clock_field)
                self.assertEqual(not case.get("hasSpiClockField", True), clock_field is None)


class CommandLine(unittest.TestCase):
    def test_list_prints_the_assemblies(self) -> None:
        result = run("list", "--config-dir", str(FIXTURES / "valid" / "selection"))
        self.assertEqual(0, result.returncode, result.stderr)
        listing = json.loads(result.stdout)
        self.assertEqual(["desk", "nucleo", "vehicle"], [item["name"] for item in listing["assemblies"]])
        self.assertEqual({"name": "nucleo", "target": "stm32g474", "board": "nucleo-g474re"},
                         listing["assemblies"][1])

    def test_a_failed_resolve_leaves_no_output(self) -> None:
        out = workdir("failed-resolve")
        config = FIXTURES / "valid" / "selection"
        self.assertEqual(0, run("resolve", "--config-dir", str(config), "--assembly", "desk",
                                "--output-dir", str(out)).returncode)
        result = run("resolve", "--config-dir", str(config), "--target", "rp2040", "--output-dir", str(out))
        self.assertNotEqual(0, result.returncode)
        self.assertIn("[JH-HW-ASSEMBLY]", result.stderr)
        self.assertFalse(any((out / name).exists() for name in generator.RESOLVE_OUTPUTS))

    def test_outputs_stay_in_a_managed_build_directory(self) -> None:
        result = run("resolve", "--config-dir", str(FIXTURES / "valid" / "status"),
                     "--output-dir", str(ROOT / "tests" / "not-a-build-directory"))
        self.assertNotEqual(0, result.returncode)
        self.assertFalse((ROOT / "tests" / "not-a-build-directory").exists())

    def test_finalize_refuses_a_stale_or_foreign_model(self) -> None:
        config = copy_fixture("stale", "valid/status")
        out, result = generate("stale-out", config, "pico")
        self.assertEqual(0, result.returncode, result.stderr)
        finalize = ["finalize", "--hardware", str(out / generator.RESOLVED_JSON), "--hardware-header",
                    str(out / generator.HARDWARE_HEADER), "--config-dir", str(config), "--output-dir", str(out)]
        header = out / generator.HARDWARE_HEADER
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
        header = out / generator.HARDWARE_HEADER
        header.write_text(header.read_text(encoding="utf-8").replace(
            "#define JH_HW_NODE_LED_PIN_OUT 14U", "#define JH_HW_NODE_LED_PIN_OUT 15U"), encoding="utf-8")
        result = run("finalize", "--hardware", str(out / generator.RESOLVED_JSON), "--hardware-header",
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
                final = json.loads((out / generator.CONFIG_JSON).read_text(encoding="utf-8"))
                self.assertEqual(enabled, "HAL_ENABLE_WIFI" in final["software"]["resolvedFeatures"])

    def test_unknown_variant_fails(self) -> None:
        _, result = generate("variant", FIXTURES / "valid" / "project", "pico-rp2040", variant="MISSING")
        self.assertIn("[JH-HW-FEATURE]", result.stderr)

    def test_dependencies_name_every_input(self) -> None:
        model = generator.resolve(FIXTURES / "valid" / "project", None, "nucleo")[0]
        names = {item["path"] for item in model["dependencies"]}
        for expected in ("device_tree.json", "../../bindings/power-stage.json",
                         "hal:boards/profiles/nucleo-g474re.json", "hal:boards/targets/stm32g474.json",
                         "hal:config/hardware/clocks/stm32g474.json",
                         "hal:config/hardware/bindings/jaszczurhal-gpio-output.json",
                         "hal:scripts/hardware_resolver.py"):
            self.assertIn(expected, names)


class Identity(unittest.TestCase):
    def test_location_and_formatting_do_not_change_the_hash(self) -> None:
        here = resolve(FIXTURES / "valid" / "project" / "device_tree.json", "nucleo")
        moved = copy_fixture("moved", "valid/project") / "device_tree.json"
        data = json.loads(moved.read_text(encoding="utf-8"))
        moved.write_text(json.dumps(data, separators=(",", ":"), sort_keys=True), encoding="utf-8")
        self.assertEqual(hardware_output.hardware_digest(here),
                         hardware_output.hardware_digest(resolve(moved, "nucleo")))

    def test_a_pin_or_an_unused_node_changes_the_hash(self) -> None:
        base = hardware_output.hardware_digest(resolve(FIXTURES / "valid" / "project" / "device_tree.json",
                                                       "nucleo"))
        pin = mutate("pin", "valid/project", lambda d: d["assemblies"]["nucleo"]["nodes"]["stage"]["pins"]
                     .update(driveA="PC8"))
        self.assertNotEqual(base, hardware_output.hardware_digest(resolve(pin, "nucleo")))

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
        self.assertNotEqual(*(hardware_output.hardware_digest(model) for model in models))
        self.assertIn('"value": 9007199254740993', hardware_output.dump_json(models[1]))

    def test_text_never_turns_into_a_number(self) -> None:
        data = {"text": "\u0000raw:1", "nested": [{"x": "\u0000raw:2.5e+00"}], "n": 7}
        self.assertEqual(data, json.loads(hardware_output.dump_json(data)))

    def test_floats_are_hashed_as_bit_patterns(self) -> None:
        typed = {"value": hardware_schema.JsonNumber("0.250"), "cType": "float", "unit": "ohm"}
        self.assertEqual(hardware_output.digest({"p": typed}),
                         hardware_output.digest({"p": {**typed, "value": 0.25}}))
        self.assertNotEqual(hardware_output.digest({"p": typed}),
                            hardware_output.digest({"p": {**typed, "cType": "double"}}))

    def test_resolved_json_keeps_the_literal_digits(self) -> None:
        text = hardware_output.dump_json(resolve(FIXTURES / "valid" / "project" / "device_tree.json",
                                                 "nucleo"))
        self.assertIn('"value": 2.50000000e-01', text)
        self.assertEqual(0.25, json.loads(text)["hardware"]["nodes"]["stage"]["properties"]["shuntOhms"]["value"])


class Headers(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.out, result = generate("headers", FIXTURES / "hal" / "nucleo-canhat", "canhat", *CYW43_DEFINITIONS)
        assert result.returncode == 0, result.stderr
        cls.early = macros(cls.out / "jh_hardware.h")
        cls.late = macros(cls.out / "jh_board_config.h")

    def test_each_macro_has_one_owner(self) -> None:
        self.assertTrue(all(name.startswith("JH_HW_") for name in self.early))
        self.assertFalse(any(name.startswith("JH_HW_") for name in self.late))
        self.assertNotIn("#include", (self.out / "jh_hardware.h").read_text(encoding="utf-8"))

    def test_late_values_refer_to_the_early_ones(self) -> None:
        self.assertEqual("JH_HW_NODE_BOARD_STATUS_LED_PIN_OUT", self.late["HAL_BOARD_STATUS_LED_PIN"])
        self.assertEqual("JH_HW_HAS_CYW43", self.late["HAL_BOARD_HAS_CYW43"])
        self.assertEqual("1", value("HAL_BOARD_HAS_CYW43", self.early, self.late))
        self.assertIn("HAL_STM32G474_CLOCK_HSE_160MHZ", self.late)
        self.assertIn("JH_HW_NODE_HAT_CAN2_PIN_TX", self.late["HAL_BOARD_CAN_CHANNELS(X)"]
                      if "HAL_BOARD_CAN_CHANNELS(X)" in self.late else
                      (self.out / "jh_board_config.h").read_text(encoding="utf-8"))
        cmake = (self.out / generator.CMAKE_RECORD).read_text(encoding="utf-8")
        self.assertIn('set(JH_BOARD_COMPILE_DEFINITIONS "")', cmake)
        self.assertIn("cyw43-stm32-gspi", cmake)
        self.assertIn("stm32g474-hse-24mhz", cmake)

    def test_absent_and_disconnected_nodes(self) -> None:
        out, result = generate("presence", FIXTURES / "valid" / "presence", "pico")
        self.assertEqual(0, result.returncode, result.stderr)
        early = macros(out / "jh_hardware.h")
        self.assertEqual("0", early["JH_HW_NODE_ABSENT_LED_PRESENT"])
        self.assertFalse(any(name.startswith("JH_HW_NODE_ABSENT_LED_PIN") for name in early))
        self.assertEqual("0", early["JH_HW_NODE_UNCONNECTED_LED_PIN_OUT_CONNECTED"])
        self.assertNotIn("JH_HW_NODE_UNCONNECTED_LED_PIN_OUT", early)

    def test_device_pins_export_provider_and_index(self) -> None:
        early = hardware_output.render_header(
            {**resolve(FIXTURES / "valid" / "expander" / "device_tree.json", "pico"), "hardwareSha256": "0" * 64},
            sorted(REGISTRY.capabilities))
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
        boards = workdir("boards-without-hse") / "boards"
        shutil.copytree(ROOT / "boards", boards)
        profile = boards / "profiles" / "nucleo-g474re.json"
        data = json.loads(profile.read_text(encoding="utf-8"))
        del data["clockSources"]["hse"]
        del data["gpio"]["reservations"]["hse-crystal"]
        profile.write_text(json.dumps(data, indent=2), encoding="utf-8")
        with self.assertRaises(HardwareError) as raised:
            resolve(self.canhat, "canhat", registry=resolver.load_registry(boards))
        self.assertEqual(("JH-HW-CLOCK", "/assemblies/canhat/clock/tree"),
                         (raised.exception.code, raised.exception.pointer))
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
        def on_pim730_board(data: dict) -> None:
            data["assemblies"]["display"]["board"] = "nucleo-g474re-pim730"
            data["assemblies"]["display"]["nodes"]["wifi"]["pins"] = {
                "cs": "PA0", "clock": "PA1", "wlOn": "PA4", "data": "PB10"}
            del data["assemblies"]["display"]["providers"]
        path = self.hal_mutate("two-radios", "nucleo-display", on_pim730_board)
        hardware = resolve(path, "display")["hardware"]
        self.assertIsNone(hardware["providers"].get("cyw43"))
        self.assertFalse(hardware["capabilities"]["cyw43"])
        for choice in ("board", "wifi.radio"):
            with self.subTest(choice=choice):
                data = json.loads(path.read_text(encoding="utf-8"))
                data["assemblies"]["display"]["providers"] = {"cyw43": choice}
                path.write_text(json.dumps(data), encoding="utf-8")
                hardware = resolve(path, "display")["hardware"]
                self.assertEqual(choice, hardware["providers"]["cyw43"])
                self.assertTrue(hardware["capabilities"]["cyw43"])
                out, result = generate(f"two-radios-{choice}", path.parent, "display", *CYW43_DEFINITIONS)
                self.assertEqual(0, result.returncode, result.stderr)
                late = macros(out / "jh_board_config.h")
                expected = "30u" if choice == "board" else "JH_HW_NODE_WIFI_RADIO_PIN_WL_ON"
                self.assertEqual(expected, late["HAL_CYW43_PIN_WL_ON"])
        data = json.loads(path.read_text(encoding="utf-8"))
        del data["assemblies"]["display"]["providers"]
        path.write_text(json.dumps(data), encoding="utf-8")
        out, result = generate("two-radios-none", path.parent, "display")
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
                hardware = resolve(path, assembly)["hardware"]
                self.assertIsNone(renderers.config_init(node, hardware["nodes"][node], hardware))
        hardware = resolve(self.display, "display")["hardware"]
        self.assertIsNotNone(renderers.config_init("display", hardware["nodes"]["display"], hardware))

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
