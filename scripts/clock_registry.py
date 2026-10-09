#!/usr/bin/env python3
"""Check the closed clock-tree registry against its backends and the boards.

config/hardware/clocks/<target>.json lists the trees a target supports. The
check confirms that every target has its registry, that each tree's output
frequencies follow from its parameters and its sources from the target's
clockInputs, that every board can run the default tree of each of its
targets, and that the trees match what actually sets the clocks: the
STM32G474 startup headers, the pinned Pico SDK defaults and, given an ESP-IDF
build's sdkconfig, the ESP-IDF configuration. A tree without backend code
fails. Nothing in the HAL build may override the Pico SDK clock defaults.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys
from typing import Any

import hardware_schema

REPO_ROOT = Path(__file__).resolve().parents[1]
CLOCKS = REPO_ROOT / "config" / "hardware" / "clocks"
PICO_SDK = REPO_ROOT / "third_party" / "pico-sdk" / "src"
STM32_PORT = REPO_ROOT / "src" / "hal" / "impl" / "stm32g474" / "port"
HSI16_HZ = 16000000

BACKENDS = {
    "pico-sdk": {"rp2040", "rp2350-arm", "rp2350-riscv"},
    "stm32g474": {"stm32g474"},
    "esp-idf": {"esp32", "esp32s3"},
    "mock": {"mock"},
}
PARAMETERS = {
    "pico-sdk": {"pllSysVcoHz", "pllSysPostdiv1", "pllSysPostdiv2",
                 "pllUsbVcoHz", "pllUsbPostdiv1", "pllUsbPostdiv2"},
    "stm32g474": {"pllSource", "pllM", "pllN", "pllR", "fdcanSource"},
    "esp-idf": {"cpuFreqMhz", "xtalFreqMhz"},
    "mock": set(),
}
# The HAL selects no clock here: RP and ESP builds keep the SDK configuration
# (check_no_hal_override, check_esp_sdkconfig) and the mock has none, so only
# the default tree runs. The STM32G474 trees are checked in check_stm32g474.
DEFAULT_ONLY_BACKENDS = {"pico-sdk", "esp-idf", "mock"}
# Pico SDK settings the HAL must leave at their defaults.
SDK_CLOCK_MACRO = (r"(?:SYS_CLK_(?:HZ|KHZ|MHZ)|XOSC_(?:HZ|KHZ|MHZ)|PLL_(?:SYS|USB|COMMON)_\w+|"
                   r"USB_CLK_(?:HZ|KHZ|MHZ)|PICO_USE_FASTEST_SUPPORTED_CLOCK)")
SDK_CLOCK_DEFINITION = re.compile(
    rf"#\s*define\s+{SDK_CLOCK_MACRO}\b|-D\s*{SDK_CLOCK_MACRO}\b|"
    rf"compile_definitions\s*\([^)]*\b{SDK_CLOCK_MACRO}\b")


class ClockError(ValueError):
    """A registry, board or backend fact that does not match."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ClockError(message)


def load_registry(directory: Path = CLOCKS) -> dict[str, dict[str, Any]]:
    schemas = hardware_schema.SchemaSet()
    registry: dict[str, dict[str, Any]] = {}
    for path in sorted(directory.glob("*.json")):
        try:
            data = hardware_schema.load_json(path)
        except hardware_schema.InputError as error:
            raise ClockError(str(error)) from error
        first = schemas.first_error("clock_tree.schema.json", data)
        require(first is None, f"[JH-HW-SCHEMA] {path}:{first.pointer if first else ''}: "
                               f"{first.message if first else ''}")
        require(path.stem == data["target"], f"{path}: file name must be {data['target']}.json")
        registry[data["target"]] = data
    return registry


def check_tree(target: str, name: str, tree: dict[str, Any]) -> None:
    """Output frequencies must follow from the backend parameters."""
    where = f"{target}/{name}"
    backend = tree["backend"]
    require(target in BACKENDS.get(backend, set()), f"{where}: backend {backend} does not drive {target}")
    require(name == "default" or backend not in DEFAULT_ONLY_BACKENDS,
            f"{where}: the HAL does not set the {backend} clock, so only its default tree runs")
    params, freq, sources = tree["parameters"], tree["frequenciesHz"], tree["requiredSources"]
    missing = PARAMETERS[backend] - set(params)
    require(not missing, f"{where}: missing parameters {sorted(missing)}")
    if backend == "pico-sdk":
        xosc = sources.get("xosc", {}).get("frequencyHz")
        require(xosc is not None, f"{where}: needs the xosc crystal")
        sys_hz = params["pllSysVcoHz"] // (params["pllSysPostdiv1"] * params["pllSysPostdiv2"])
        usb_hz = params["pllUsbVcoHz"] // (params["pllUsbPostdiv1"] * params["pllUsbPostdiv2"])
        expected = {"ref": xosc, "sys": sys_hz, "peri": sys_hz, "usb": usb_hz, "adc": usb_hz}
    elif backend == "stm32g474":
        source = HSI16_HZ if params["pllSource"] == "hsi16" else sources.get("hse", {}).get("frequencyHz")
        require(source is not None, f"{where}: the hse source is not required")
        vco = source // params["pllM"] * params["pllN"]
        sysclk = vco // params["pllR"]
        fdcan = vco // params["pllQ"] if params["fdcanSource"] == "pll-q" else sysclk
        expected = {"sysclk": sysclk, "hclk": sysclk, "pclk1": sysclk, "pclk2": sysclk,
                    "fdcan": fdcan, "i2c": HSI16_HZ}
    elif backend == "esp-idf":
        require(sources.get("xtal", {}).get("frequencyHz") == params["xtalFreqMhz"] * 1000000,
                f"{where}: the xtal source does not match xtalFreqMhz")
        expected = {"cpu": params["cpuFreqMhz"] * 1000000}
    else:
        require(not sources and not params, f"{where}: the mock has no clock")
        expected = {}
    require(freq == expected, f"{where}: frequencies {freq} do not follow from the parameters {expected}")


def check_registry(registry: dict[str, dict[str, Any]], targets: dict[str, dict[str, Any]]) -> None:
    """Each tree against its backend and the clock inputs of its target."""
    require(set(registry) == set(targets),
            f"clock registries {sorted(registry)} differ from targets {sorted(targets)}")
    for target, data in sorted(registry.items()):
        inputs = targets[target].get("clockInputs", {})
        for name, tree in sorted(data["trees"].items()):
            check_tree(target, name, tree)
            for key, source in sorted(tree["requiredSources"].items()):
                require(source["kind"] in inputs.get(key, {}),
                        f"{target}/{name}: {target} has no {key} input for the {source['kind']}")


def source_problems(inputs: dict[str, Any], key: str, source: dict[str, Any]) -> list[str]:
    """A board source against the target clock input it drives: the input
    takes that kind and the source lists exactly the GPIOs the kind occupies."""
    required = inputs.get(key, {}).get(source["kind"])
    if required is None:
        return [f"the target has no {key} input for the {source['kind']}"]
    declared = [endpoint["id"] for endpoint in source["pins"]]
    if len(declared) != len(required) or set(declared) != set(required):
        return [f"the {source['kind']} on {key} occupies {required}, the board lists {declared}"]
    return []


def board_sources_satisfy(board: dict[str, Any], tree: dict[str, Any],
                          inputs: dict[str, Any]) -> list[str]:
    """Why the board cannot run the tree on a target with these clock inputs."""
    available = board.get("clockSources", {})
    problems = []
    for key, required in sorted(tree["requiredSources"].items()):
        source = available.get(key)
        if source is None:
            problems.append(f"no {key} source")
        elif (source["kind"], source["frequencyHz"]) != (required["kind"], required["frequencyHz"]):
            problems.append(f"{key} is a {source['kind']} at {source['frequencyHz']} Hz, "
                            f"the tree needs a {required['kind']} at {required['frequencyHz']} Hz")
        else:
            problems.extend(source_problems(inputs, key, source))
    return problems


def check_boards(registry: dict[str, dict[str, Any]], boards: dict[str, dict[str, Any]],
                 targets: dict[str, dict[str, Any]]) -> None:
    for board_id, board in sorted(boards.items()):
        for target in board["compatibleTargets"]:
            problems = board_sources_satisfy(board, registry[target]["trees"]["default"],
                                             targets[target].get("clockInputs", {}))
            require(not problems, f"{board_id}: default {target} tree: {'; '.join(problems)}")


def _value(text: str, pattern: str, where: str) -> str:
    found = re.findall(pattern, text)
    require(len(found) == 1, f"{where}: expected one match of {pattern!r}, found {found}")
    return found[0]


def check_stm32g474(registry: dict[str, dict[str, Any]], port: Path = STM32_PORT) -> None:
    """The two STM32G474 trees as the startup code programs them."""
    clock = (port / "stm32g474_clock.h").read_text(encoding="utf-8")
    tree_h = (port / "stm32g474_clock_tree.h").read_text(encoding="utf-8")
    trees = registry["stm32g474"]["trees"]
    require(set(trees) == {"default", "hse-160mhz"},
            f"stm32g474 trees {sorted(trees)}: the startup code builds default and hse-160mhz only")
    default, hse = trees["default"], trees["hse-160mhz"]
    require(int(_value(clock, r"#define JH_G474_HSI_CLOCK_HZ (\d+)u", "stm32g474_clock.h")) == HSI16_HZ,
            "HSI16 frequency changed")
    require(int(_value(clock, r"#define JH_G474_HSE_CLOCK_HZ (\d+)u", "stm32g474_clock.h"))
            == hse["requiredSources"]["hse"]["frequencyHz"], "HSE frequency differs")
    cores = [int(v) for v in re.findall(r"#define JH_G474_CORE_CLOCK_HZ (\d+)u", clock)]
    require(cores == [hse["frequenciesHz"]["sysclk"], default["frequenciesHz"]["sysclk"]],
            f"core clocks {cores} differ from the registry")
    require(int(_value(clock, r"#define JH_G474_FDCAN_CLOCK_HZ (\d+)u", "stm32g474_clock.h"))
            == hse["frequenciesHz"]["fdcan"], "FDCAN clock of the HSE tree differs")
    hp, dp = hse["parameters"], default["parameters"]
    require(f"RCC_PLLCFGR_PLLN({hp['pllN']}u)" in tree_h and f"RCC_PLLCFGR_PLLQ_DIV{hp['pllQ']}" in tree_h
            and f"RCC_PLLCFGR_PLLR_DIV{hp['pllR']}" in tree_h,
            "HSE tree PLL N/Q/R differ from stm32g474_clock_tree.h")
    require(hse["requiredSources"]["hse"]["frequencyHz"] // 4000000 == hp["pllM"],
            "HSE tree PLL M differs from HSE / 4 MHz")
    require(f"RCC_PLLCFGR_PLLM({dp['pllM']}u) | RCC_PLLCFGR_PLLN({dp['pllN']}u)" in tree_h
            and f"RCC_PLLCFGR_PLLR_DIV{dp['pllR']}" in tree_h,
            "default tree PLL M/N/R differ from stm32g474_clock_tree.h")


def check_pico_sdk(registry: dict[str, dict[str, Any]], sdk: Path = PICO_SDK) -> None:
    """The pinned Pico SDK defaults behind each RP tree."""
    clocks = (sdk / "rp2_common/hardware_clocks/include/hardware/clocks.h").read_text(encoding="utf-8")
    standard = clocks.split("PLL settings for standard 125/150 MHz system clock.", 1)
    require(len(standard) == 2, "Pico SDK standard PLL block moved")
    block = standard[1].split("#endif // SYS_CLK_KHZ == 125000", 1)[0]
    require("#define PLL_SYS_VCO_FREQ_HZ                (1500 * MHZ)" in block,
            "Pico SDK standard PLL_SYS VCO is no longer 1500 MHz")
    require(re.search(r"#if SYS_CLK_HZ == 125 \* MHZ\s+#define PLL_SYS_POSTDIV1\s+6\s+#else\s+"
                      r"#define PLL_SYS_POSTDIV1\s+5", block) is not None,
            "Pico SDK PLL_SYS post divider 1 is no longer 6 at 125 MHz and 5 otherwise")
    require(re.search(r"#define PLL_SYS_POSTDIV2\s+2\b", block) is not None,
            "Pico SDK PLL_SYS post divider 2 is no longer 2")
    for macro, value in (("PLL_USB_VCO_FREQ_HZ", "(1200 * MHZ)"), ("PLL_USB_POSTDIV1", "5"),
                         ("PLL_USB_POSTDIV2", "5")):
        require(re.search(rf"#define {macro}\s+{re.escape(value)}", clocks) is not None,
                f"Pico SDK {macro} is no longer {value}")
    for chip, targets in (("rp2040", ("rp2040",)), ("rp2350", ("rp2350-arm", "rp2350-riscv"))):
        defs = (sdk / chip / "hardware_regs/include/hardware/platform_defs.h").read_text(encoding="utf-8")
        where = f"{chip} platform_defs.h"
        xosc = int(_value(defs, r"#define XOSC_HZ _u\((\d+)\)", where))
        if chip == "rp2040":
            require(re.search(r"#define PICO_USE_FASTEST_SUPPORTED_CLOCK 0\b", defs) is not None,
                    "RP2040 no longer defaults to the standard clock")
            sys_hz = int(re.findall(r"#define SYS_CLK_HZ _u\((\d+)\)", defs)[-1])
        else:
            sys_hz = int(_value(defs, r"#define SYS_CLK_HZ _u\((\d+)\)", where))
        for target in targets:
            tree = registry[target]["trees"]["default"]
            p = tree["parameters"]
            require(tree["frequenciesHz"]["sys"] == sys_hz, f"{target}: Pico SDK default is {sys_hz} Hz")
            require(tree["requiredSources"]["xosc"]["frequencyHz"] == xosc, f"{target}: Pico SDK XOSC is {xosc} Hz")
            require((p["pllSysVcoHz"], p["pllSysPostdiv1"], p["pllSysPostdiv2"])
                    == (1500000000, 6 if sys_hz == 125000000 else 5, 2), f"{target}: PLL_SYS differs")
            require((p["pllUsbVcoHz"], p["pllUsbPostdiv1"], p["pllUsbPostdiv2"]) == (1200000000, 5, 5),
                    f"{target}: PLL_USB differs")


def check_no_hal_override(root: Path = REPO_ROOT) -> None:
    """No HAL build file or example defines a Pico SDK clock macro."""
    for base in ("cmake", "link_libraries", "src", "config", "examples"):
        for path in sorted((root / base).rglob("*")):
            if not path.is_file() or path.suffix not in {".cmake", ".txt", ".h", ".c", ".cpp"}:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            match = SDK_CLOCK_DEFINITION.search(text)
            if match:
                raise ClockError(f"{path.relative_to(root)} sets a Pico SDK clock macro: {match.group(0)}")


def check_esp_sdkconfig(registry: dict[str, dict[str, Any]], target: str, sdkconfig: Path) -> None:
    """An ESP-IDF build's resolved configuration against the default tree."""
    text = sdkconfig.read_text(encoding="utf-8")
    params = registry[target]["trees"]["default"]["parameters"]
    for option, key in (("CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ", "cpuFreqMhz"), ("CONFIG_XTAL_FREQ", "xtalFreqMhz")):
        value = int(_value(text, rf"(?m)^{option}=(\d+)$", str(sdkconfig)))
        require(value == params[key], f"{target}: {option}={value}, registry {key}={params[key]}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--esp-sdkconfig", action="append", default=[], metavar="TARGET=PATH",
                        help="also check the sdkconfig an ESP-IDF build of TARGET wrote")
    args = parser.parse_args()
    boards_root = REPO_ROOT / "boards"
    try:
        registry = load_registry()
        targets, boards = ({path.stem: json.loads(path.read_text(encoding="utf-8"))
                            for path in (boards_root / kind).glob("*.json")}
                           for kind in ("targets", "profiles"))
        check_registry(registry, targets)
        check_boards(registry, boards, targets)
        check_stm32g474(registry)
        check_pico_sdk(registry)
        check_no_hal_override()
        for item in args.esp_sdkconfig:
            target, _, path = item.partition("=")
            check_esp_sdkconfig(registry, target, Path(path))
    except (ClockError, OSError) as error:
        print(f"[ERROR] clock registry: {error}", file=sys.stderr)
        return 1
    print(f"[OK] clock registry: {len(registry)} targets, "
          f"{sum(len(d['trees']) for d in registry.values())} trees")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
