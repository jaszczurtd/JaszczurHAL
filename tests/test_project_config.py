#!/usr/bin/env python3
"""Check the hal_project_config.h reader against the C preprocessor."""

from __future__ import annotations

from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from repo_root import repo_root

ROOT = repo_root(sys.argv, __file__)
COMPILER = sys.argv[2] if len(sys.argv) > 2 else "cc"
sys.path.insert(0, str(ROOT / "scripts"))
import project_config  # noqa: E402
from project_config import ProjectConfigError, read_project_config  # noqa: E402


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def write(directory: Path, text: str, name: str = "hal_project_config.h") -> Path:
    path = directory / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def compiler_macros(header: Path, predefined: list[str],
                    compiler: str = COMPILER) -> tuple[dict[str, str], bool]:
    """Macros the real preprocessor defines after the header, and whether it
    stopped on #error."""
    result = subprocess.run(
        [compiler, "-E", "-dM", "-x", "c", "-", f"-I{header.parent}"]
        + [f"-D{item}" for item in predefined],
        input='#include "hal_project_config.h"\n',
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        require("#error" in result.stderr, f"preprocessor failed: {result.stderr}")
        return {}, True
    macros: dict[str, str] = {}
    for line in result.stdout.splitlines():
        match = re.match(r"#define (\w+)(\([^)]*\))? ?(.*)$", line)
        if match:
            macros[match.group(1)] = " ".join(match.group(3).split())
    return macros, False


def normalized(value: str) -> str:
    return " ".join(value.split())


EQUIVALENCE_HEADERS = {
    "defaults_and_conditions": """
#pragma once
#ifndef HAL_RP_FLASH_EEPROM_SIZE
#define HAL_RP_FLASH_EEPROM_SIZE (8 * 1024)
#endif
#define BASE 4
#if BASE * 2 == 8 && !defined(MISSING)
#define HAL_ENABLE_KV
#elif 1
#define HAL_ENABLE_I2C
#endif
#if defined(HAL_TARGET_RP2350_ARM) || defined HAL_TARGET_RP2040
#define HAL_RP_CORE0_STACK_SIZE (BASE * 1024)
#else
#define HAL_RP_CORE0_STACK_SIZE 2048
#endif
#ifdef HAL_ENABLE_KV
#undef BASE
#define BASE 5
#endif
#if 0
#if 1 / 0
#define NEVER
#endif
#elif BASE == 5
#define HAL_ENABLE_SPI 1
#endif
#if 1
#define FIRST_TAKEN
#elif 0
#define NEVER_A
#elif 1
#define NEVER_B
#else
#define NEVER_C
#endif
#define KEPT 1
#if 0
#undef KEPT
#endif
""",
    "variant_switches": """
#pragma once
#define JH_PROJECT_VARIANTS(X) \\
  X(PROBE, "Smoke test", PROBE_ONLY=1) \\
  X(WIDE, "Wide panel", PANEL = 2, EXTRA)
#if !PROBE_ONLY
#define HAL_ENABLE_DMA_PWM_AUDIO
#endif
#if PANEL == 2
#define HAL_ENABLE_ST7796S
#else
#define HAL_ENABLE_ILI9341
#endif
#if defined(EXTRA) && PANEL > 1 ? 1 : 0
#define HAL_ENABLE_APP_TASK1
#endif
#if PANEL == 2 && !defined(HAL_TARGET_RP2350_ARM)
#error "wide panel needs RP2350"
#endif
""",
    "short_circuit_and_literals": """
#pragma once
#if 0 && (1 / 0)
#define A 1
#endif
#if 1 || (1 % 0)
#define B 1
#endif
#if 1 ? 2 : (1 / 0)
#define C 1
#endif
#if 0 ? (1 / 0) : 0
#define D 1
#endif
#if 1u && 0x10 == 16 && 'a' == 97 && 0b11 == 3
#define HAL_ENABLE_CRC
#endif
#if 0 && (1 << -1)
#define E 1
#endif
#if 1 ? 1 : (1 >> -1)
#define F 1
#endif
""",
    "arithmetic": """
#pragma once
#define A (-7 / 2)
#define B (-7 % 2)
#define C (1 << 4 | 0x0F & 0b1010)
#define D 'A'
#if A == -3 && B == -1 && C == 26 && D == 65 && 010 == 8 && 10u == 10
#define HAL_ENABLE_CRC
#endif
#if ~0 == -1 && !(3 > 4) && (2 >= 2) && (1 != 2)
#define HAL_ENABLE_TIME
#endif
""",
}
PREDEFINED_SETS = (
    ["HAL_TARGET_RP2040"],
    ["HAL_TARGET_RP2350_ARM"],
    ["HAL_TARGET_STM32G474", "PROBE_ONLY=1"],
    ["HAL_TARGET_RP2350_ARM", "PANEL=2", "EXTRA"],
    ["HAL_TARGET_RP2040", "PANEL=2"],
)

with tempfile.TemporaryDirectory(prefix="jh-project-config-") as temporary:
    root = Path(temporary)

    for label, text in EQUIVALENCE_HEADERS.items():
        header = write(root / label, text)
        for predefined in PREDEFINED_SETS:
            expected, expected_error = compiler_macros(header, predefined)
            config = read_project_config(header, predefined)
            require(
                (config.error is not None) == expected_error,
                f"{label} {predefined}: #error {config.error} vs preprocessor {expected_error}",
            )
            if expected_error:
                continue
            names = set(config.macros) | {
                name for name in expected if name in text or name in " ".join(predefined)
            }
            for name in sorted(names):
                require(
                    name in expected,
                    f"{label} {predefined}: reader defines {name}, preprocessor does not",
                )
                macro = config.macros.get(name)
                require(
                    macro is not None,
                    f"{label} {predefined}: preprocessor defines {name}, reader does not",
                )
                if not macro.function_like:
                    require(
                        normalized(macro.body) == expected[name],
                        f"{label} {predefined}: {name} = {macro.body!r}, "
                        f"preprocessor {expected[name]!r}",
                    )

    # Active negative shifts follow a GCC extension, which Clang does not share.
    negative_shifts = write(root / "gcc_negative_shifts", """
#if (1 << -1) == 0 && (8 >> -1) == 16
#define GCC_NEGATIVE_SHIFT 1
#endif
""")
    negative_config = read_project_config(negative_shifts)
    require("GCC_NEGATIVE_SHIFT" in negative_config.macros, "GCC negative shifts")
    gnu_compiler = shutil.which("gcc") or shutil.which("arm-none-eabi-gcc")
    if gnu_compiler:
        expected, expected_error = compiler_macros(negative_shifts, [], gnu_compiler)
        require(not expected_error and "GCC_NEGATIVE_SHIFT" in expected,
                "GCC preprocessor negative-shift behavior changed")

    # Values the build reads are integers, evaluated the way C does.
    values = read_project_config(root / "arithmetic/hal_project_config.h")
    require(values.integer("A") == -3 and values.integer("B") == -1, "C division")
    require(values.integer("C") == 26, "bitwise precedence")
    defaults = read_project_config(
        root / "defaults_and_conditions/hal_project_config.h", ["HAL_TARGET_RP2040"]
    )
    require(defaults.integer("HAL_RP_FLASH_EEPROM_SIZE") == 8192, "nested arithmetic")
    # BASE was redefined under a feature that MISSING decides, so neither the
    # reader nor CMake can know the stack size the compiler will use.
    require(
        defaults.integer("HAL_RP_CORE0_STACK_SIZE") is None
        and defaults.unresolved("HAL_RP_CORE0_STACK_SIZE") == {"MISSING"},
        "a value behind an undecided redefinition",
    )
    later = read_project_config(
        write(
            root / "later",
            "#define BASE 4\n#define SIZE (BASE * 1024)\n#undef BASE\n#define BASE 5\n",
        )
    )
    require(later.integer("SIZE") == 5120 and not later.unresolved("SIZE"), "macro uses later value")
    require(
        sorted(macro.name for macro in defaults.features())
        == ["HAL_ENABLE_KV", "HAL_ENABLE_SPI"],
        "features are the header's own definitions",
    )
    predefined_feature = read_project_config(
        root / "defaults_and_conditions/hal_project_config.h", ["HAL_ENABLE_CRC"]
    )
    require(
        "HAL_ENABLE_CRC" not in {macro.name for macro in predefined_feature.features()},
        "a -D feature is not a header feature",
    )

    # Variants come from the X-macro, whatever the target; a formatted
    # "PANEL = 2" reaches the build as the -D form.
    variants = read_project_config(root / "variant_switches/hal_project_config.h")
    require(
        [(item.id, item.description, item.definitions) for item in variants.variants]
        == [
            ("PROBE", "Smoke test", ("PROBE_ONLY=1",)),
            ("WIDE", "Wide panel", ("PANEL=2", "EXTRA")),
        ],
        f"variants: {variants.variants}",
    )
    blocked = read_project_config(
        root / "variant_switches/hal_project_config.h", ["HAL_TARGET_RP2040", "PANEL=2"]
    )
    require(blocked.error is not None and "RP2350" in blocked.error[0], "variant #error")
    require(
        not variants.uncertain("HAL_ENABLE_ST7796S"),
        "a declared variant switch is a known input",
    )

    # The file's own include guard does not make a declaration conditional.
    guarded = write(
        root / "guarded",
        "#ifndef HAL_PROJECT_CONFIG_H\n#define HAL_PROJECT_CONFIG_H\n"
        "#define JH_PROJECT_TARGETS(X) X(HAL_TARGET_RP2040)\n"
        "#define JH_PROJECT_VARIANTS(X) X(PROBE, \"Probe\", PROBE_ONLY=1)\n"
        "#if PROBE_ONLY\n#define HAL_ENABLE_CRC\n#endif\n"
        "#endif\n",
    )
    guarded_config = read_project_config(guarded, ["HAL_TARGET_RP2040", "PROBE_ONLY=1"])
    require(
        guarded_config.targets == ("HAL_TARGET_RP2040",)
        and [item.id for item in guarded_config.variants] == ["PROBE"]
        and guarded_config.defined("HAL_ENABLE_CRC"),
        "declarations inside an include guard",
    )

    # A build-read macro decided by an identifier the reader cannot see.
    uncertain = write(
        root / "uncertain",
        """
#if defined(PICO_RP2040)
#define HAL_RP_CORE1_STACK_SIZE 4096
#endif
#ifdef SDK_ONLY
#define HAL_ENABLE_WIFI
#else
#define HAL_ENABLE_UART
#endif
#if defined(OTHER)
#define LOCAL_ONLY 1
#endif
#undef HAL_RP_CORE1_STACK_SIZE
#define HAL_RP_CORE1_STACK_SIZE 2048
""",
    )
    config = read_project_config(uncertain)
    require(config.uncertain("HAL_ENABLE_WIFI") == {"SDK_ONLY"}, "inactive branch dependency")
    require(config.uncertain("HAL_ENABLE_UART") == {"SDK_ONLY"}, "else branch dependency")
    require(config.uncertain("LOCAL_ONLY") == {"OTHER"}, "any macro records its inputs")
    require(
        not config.uncertain("HAL_RP_CORE1_STACK_SIZE"),
        "an exact later definition replaces an uncertain one",
    )

    # Values the compiler computes from names this configuration does not
    # define have no value here, also through a local alias.
    sdk = read_project_config(
        write(
            root / "sdk_value",
            "#define FLASH_BYTES PICO_FLASH_SIZE_BYTES\n"
            "#define HAL_RP_FLASH_EEPROM_SIZE (FLASH_BYTES / 256)\n"
            "#define HAL_RP_CORE1_STACK_SIZE (2u * 1024u)\n"
            "#define HAL_EEPROM_TYPE EEPROM_TYPE_FLASH\n"
            "#define KIB(x) ((x) * 1024)\n"
            "#define HAL_RP_FLASH_LITTLEFS_SIZE KIB(64)\n"
            "#define TEXT \"PICO_FLASH_SIZE_BYTES\"\n",
        )
    )
    require(
        sdk.integer("HAL_RP_FLASH_EEPROM_SIZE") is None
        and sdk.unresolved("HAL_RP_FLASH_EEPROM_SIZE") == {"PICO_FLASH_SIZE_BYTES"},
        "an SDK macro behind a local alias",
    )
    require(sdk.integer("HAL_RP_CORE1_STACK_SIZE") == 2048, "suffixed literals")
    require(sdk.unresolved("HAL_EEPROM_TYPE") == {"EEPROM_TYPE_FLASH"}, "a C name")
    require(sdk.unresolved("HAL_RP_FLASH_LITTLEFS_SIZE") == {"KIB"}, "a function-like macro")
    require(not sdk.unresolved("TEXT"), "names inside a string literal")
    cmake_text = "\n".join(
        project_config.cmake_lines(
            project_config.BuildConfig(root / "sdk_value", project_config.TargetFacts("rp2040", "HAL_TARGET_RP2040", ()), None, (), sdk),
            {},
        )
    )
    require(
        "set(JH_PROJECT_UNRESOLVED_HAL_RP_FLASH_EEPROM_SIZE [=[PICO_FLASH_SIZE_BYTES]=])" in cmake_text
        and "JH_PROJECT_INT_HAL_RP_FLASH_EEPROM_SIZE" not in cmake_text
        and "set(JH_PROJECT_INT_HAL_RP_CORE1_STACK_SIZE [=[2048]=])" in cmake_text,
        "CMake output of unresolved values",
    )

    # A condition decided through a local alias depends on what the alias
    # expands to; numbers and character literals are not names.
    aliased = read_project_config(
        write(
            root / "aliased",
            "#define ON_RP2040 PICO_RP2040\n"
            "#if ON_RP2040\n#define HAL_ENABLE_KV\n#endif\n"
            "#if 1u && 0x10 == 16 && 'a' == 97\n#define HAL_ENABLE_CRC\n#endif\n",
        ),
        ["HAL_TARGET_RP2040"],
    )
    require(aliased.uncertain("HAL_ENABLE_KV") == {"PICO_RP2040"}, "a condition through an alias")
    relayed = read_project_config(
        write(
            root / "relayed",
            "#if defined(SDK_SWITCH)\n#define LOCAL_SWITCH 1\n#endif\n"
            "#if LOCAL_SWITCH\n#define HAL_ENABLE_KV\n#endif\n",
        )
    )
    require(
        relayed.uncertain("HAL_ENABLE_KV") == {"SDK_SWITCH"},
        "a condition on a macro an unknown name decided",
    )
    require(
        aliased.defined("HAL_ENABLE_CRC") and not aliased.uncertain("HAL_ENABLE_CRC"),
        "literal suffixes and hex digits are not names",
    )

    # C does not evaluate the operand a decided && or || skips, nor the ?:
    # branch it does not take.
    short = read_project_config(
        write(
            root / "short_circuit",
            "#if 0 && (1 / 0)\n#define A\n#endif\n"
            "#if 1 || (1 % 0)\n#define B\n#endif\n"
            "#if 1 ? 2 : (1 / 0)\n#define C\n#endif\n"
            "#if 0 ? (1 / 0) : 0\n#define D\n#endif\n",
        )
    )
    require(
        not short.defined("A") and short.defined("B") and short.defined("C") and not short.defined("D"),
        "short-circuit evaluation",
    )

    # The header's final state decides the features: a variant feature the
    # header removes is not requested, so it cannot conflict with its
    # replacement.
    radio = write(
        root / "radio",
        "#define JH_PROJECT_VARIANTS(X) X(NEW_RADIO, \"SX126X\", HAL_ENABLE_SX127X, NEW_RADIO=1)\n"
        "#if defined(NEW_RADIO)\n#undef HAL_ENABLE_SX127X\n#define HAL_ENABLE_SX126X\n#endif\n",
    )
    radio_build = project_config.evaluate_build(
        radio.parent, project_config.load_targets(ROOT)["rp2040"], "NEW_RADIO"
    )
    import generate_hal_features  # noqa: E402

    radio_resolution, radio_findings = project_config.resolve_build_features(
        radio_build, generate_hal_features.load_registry(ROOT / "config")
    )
    require(
        radio_build.requested_features() == ["HAL_ENABLE_SX126X"]
        and not radio_findings
        and "HAL_ENABLE_SX127X" not in radio_resolution.resolved,
        f"a removed variant feature came back: {radio_build.requested_features()} {radio_findings}",
    )

    # Quoted includes resolve next to the including file; nothing else does.
    shared = write(root / "shared", "#pragma once\n#define SHARED_TIMEOUT 2000U\n", "common.h")
    including = write(
        root / "project",
        '#pragma once\n#include "../shared/common.h"\n#include "../shared/common.h"\n',
    )
    included = read_project_config(including)
    require(included.value("SHARED_TIMEOUT") == "2000U", "relative include")
    require(included.files == (including.resolve(), shared.resolve()), "include inputs")

    def rejected(text: str, diagnostic: str, predefined: list[str] | None = None) -> None:
        header = write(root / "rejected", text)
        try:
            read_project_config(header, predefined or [])
        except ProjectConfigError as error:
            require(diagnostic in str(error), f"{diagnostic!r} not in {error}")
        else:
            raise AssertionError(f"accepted: {text!r}")

    rejected("#include <stdint.h>\n", "only #include")
    rejected("#if 1\n#define A\n", "unterminated #if")
    rejected("#endif\n", "#endif without #if")
    rejected("#define F(x) x\n#if F(1)\n#endif\n", "function-like macro F")
    rejected("#if 1 +\n#endif\n", "expected operand")
    rejected("#if 1 && (1 / 0)\n#endif\n", "division by zero")
    rejected("#if FOO\n#define JH_PROJECT_VARIANTS(X) X(A, \"A\", B)\n#endif\n", "unconditionally")
    rejected(
        "#ifndef G\n#define G\n#else\n#define JH_PROJECT_VARIANTS(X) X(A, \"A\", B)\n#endif\n",
        "unconditionally",
    )
    rejected(
        "#ifndef G\n#define G\n#endif\n#ifndef H\n#define JH_PROJECT_VARIANTS(X) X(A, \"A\", B)\n#endif\n",
        "unconditionally",
    )
    rejected(
        "#ifndef G\n#define OTHER\n#define JH_PROJECT_VARIANTS(X) X(A, \"A\", B)\n#endif\n",
        "unconditionally",
    )
    rejected("#define JH_PROJECT_VARIANTS X(A, \"A\", B)\n", "one parameter")
    rejected("#define JH_PROJECT_VARIANTS(X) X(A, \"A\")\n", "at least one definition")
    rejected("#define JH_PROJECT_VARIANTS(X) X(A-B, \"A\", B)\n", "capital letters")
    rejected(
        "#define JH_PROJECT_VARIANTS(X) X(A, \"A\", B) X(A, \"Again\", C)\n",
        "must be unique",
    )
    rejected("#define JH_PROJECT_VARIANTS(X) X(A, A, B)\n", "string description")
    # One letter case keeps variant directories distinct everywhere.
    rejected("#define JH_PROJECT_VARIANTS(X) X(bench, \"Bench\", B)\n", "capital letters")
    rejected("#define JH_PROJECT_VARIANTS(X) X(Bench, \"Bench\", B)\n", "capital letters")
    rejected("#define JH_PROJECT_VARIANTS(X) X(_BENCH, \"Bench\", B)\n", "capital letters")
    rejected("#define JH_PROJECT_VARIANTS(X) X(1BENCH, \"Bench\", B)\n", "capital letters")
    rejected(
        "#define JH_PROJECT_VARIANTS(X) X(A, \"A\", HAL_TARGET_RP2040)\n",
        "invalid definition",
    )
    rejected("#define JH_PROJECT_VARIANTS(X) Y(a, \"A\", B)\n", "only X(")

    empty = read_project_config(root / "missing.h", ["HAL_TARGET_MOCK"])
    require(empty.files == () and empty.variants == (), "missing header is empty")
    require(empty.defined("HAL_TARGET_MOCK"), "predefined macros stay visible")

print("project configuration reader tests passed")
