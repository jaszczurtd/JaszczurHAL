#!/usr/bin/env python3
"""Host contracts for the link_libraries/ runners and their dispatcher."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import tempfile


from repo_root import repo_root  # noqa: E402

ROOT = repo_root(sys.argv, __file__)
sys.path.insert(0, str(ROOT / "scripts"))

import board_registry
import build_esp_idf
from generate_board_config import load_registry


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


RUNNER_BY_PROVIDER = {
    "pico-sdk": ("build_rp_pico_lib.sh", "rp_pico_lib"),
    "jh-stm32-baremetal": ("build_stm32_lib.sh", "stm32_lib"),
    "esp-idf": ("build_esp32_lib.sh", "esp32_lib"),
}
COMMON_OPTIONS = (
    "--target",
    "--board",
    "--all-features",
    "--library-only",
    "--freertos",
    "--project-config",
    "-D",
    "--output",
    "--clean",
    "--jobs",
)

targets, _, _ = load_registry(ROOT / "boards")
for target_id, target in sorted(targets.items()):
    provider = target["build"]["provider"]
    if provider == "host":
        continue
    require(
        provider in RUNNER_BY_PROVIDER,
        f"{target_id}: provider {provider!r} has no linkable-library runner",
    )
    script, directory = RUNNER_BY_PROVIDER[provider]
    require(
        (ROOT / "scripts" / script).is_file(),
        f"{target_id}: runner script is missing: scripts/{script}",
    )
    require(
        (ROOT / "link_libraries" / directory).is_dir(),
        f"{target_id}: runner directory is missing: link_libraries/{directory}",
    )
    facts = board_registry.target_facts(ROOT, target_id)
    require(
        facts["provider"] == provider
        and facts["defaultBoard"] == target["defaultBoard"],
        f"{target_id}: target-facts drifted from the registry",
    )
require(
    set(board_registry.LINK_LIBRARY_PROVIDERS) == set(RUNNER_BY_PROVIDER),
    "board_registry.LINK_LIBRARY_PROVIDERS drifted from the runner table",
)
try:
    board_registry.target_facts(ROOT, "no-such-target")
except KeyError:
    pass
else:
    raise AssertionError("target_facts accepted an unknown target")

helper = (ROOT / "scripts" / "lib" / "build_artifacts.sh").read_text(encoding="utf-8")
for provider, (script, _) in RUNNER_BY_PROVIDER.items():
    require(
        f"{provider}) echo \"{script}\"" in helper,
        f"shared helper does not map {provider} to {script}",
    )
    text = (ROOT / "scripts" / script).read_text(encoding="utf-8")
    for helper_name in (
        "jh_validate_hal_defines",
        "jh_target_facts",
        "jh_resolve_build_output",
    ):
        require(helper_name in text, f"{script} does not use {helper_name}")
    require("--platform" not in text, f"{script} still exposes --platform")
    usage = text.split("USAGE", 2)[1]
    for option in COMMON_OPTIONS:
        require(option in usage, f"{script} usage does not document {option}")

dispatcher = (ROOT / "scripts" / "build_link_library.sh").read_text(encoding="utf-8")
require(
    "jh_link_library_runner" in dispatcher and 'exec "${SCRIPT_DIR}/${RUNNER}"' in dispatcher,
    "dispatcher does not select and exec the family runner",
)
for relative in ("runalltests.sh", ".github/workflows/ci.yml"):
    text = (ROOT / relative).read_text(encoding="utf-8")
    require("--platform" not in text, f"{relative} still passes --platform")
    require("rp_native" not in text, f"{relative} still names the removed rp_native layout")

probe_dir = ROOT / "link_libraries" / "esp32_lib"
probe = (probe_dir / "link_probe.c").read_text(encoding="utf-8")
for symbol in ("void app_start(void)", "void app_task0(void)", "void app_task1(void)"):
    require(symbol in probe, f"ESP32 link probe does not define {symbol}")
require(
    (probe_dir / "hal_project_config.h").is_file(),
    "ESP32 link probe has no hal_project_config.h",
)
esp32_runner = (ROOT / "scripts" / "build_esp32_lib.sh").read_text(encoding="utf-8")
for fragment in (
    "esp-idf/jaszczurhal/libjaszczurhal.a",
    "libJaszczurHAL.a",
    "include/generated",
    "--project-config",
    "--all-features",
):
    require(fragment in esp32_runner, f"ESP32 runner is missing {fragment!r}")

model = build_esp_idf.resolve_build_model(
    ROOT,
    probe_dir,
    target="esp32s3",
    board="",
    project_name="esp32_lib",
    requested_sources=[],
    features=[],
    definitions=[],
)
require(
    model["requestedFeatures"] == [] and "HAL_ENABLE_FREERTOS" in model["resolvedFeatures"],
    "default ESP32 library profile is not core-only",
)
full = build_esp_idf.resolve_build_model(
    ROOT,
    probe_dir,
    target="esp32s3",
    board="",
    project_name="esp32_lib",
    requested_sources=[],
    features=[],
    definitions=[],
    all_features=True,
)
supported = {
    item.removesuffix("=1") for item in targets["esp32s3"]["supportedFeatures"]
}
require(
    set(full["resolvedFeatures"]) == supported,
    "--all-features does not resolve to the ESP32-S3 supportedFeatures closure",
)

with tempfile.TemporaryDirectory(prefix="jh-esp32-lib-config-") as temporary:
    config_dir = Path(temporary)
    (config_dir / "hal_project_config.h").write_text(
        "#pragma once\n#define HAL_ENABLE_UART 1\n", encoding="utf-8"
    )
    external = build_esp_idf.resolve_build_model(
        ROOT,
        probe_dir,
        target="esp32s3",
        board="",
        project_name="esp32_lib",
        requested_sources=[],
        features=[],
        definitions=[],
        project_config_dir=config_dir,
    )
    require(
        external["requestedFeatures"] == ["HAL_ENABLE_UART"],
        "--project-config header requests were ignored",
    )
    require(
        config_dir.resolve() in external["projectIncludeDirs"],
        "--project-config directory is not an include directory",
    )
    contract = build_esp_idf._project_config_contract(
        probe_dir, ROOT / ".build" / "unused", external, []
    )
    require(
        Path(contract["projectConfigHeader"]["path"]).is_absolute(),
        "external project config header is not recorded by absolute path",
    )

if sys.platform != "win32":
    result = subprocess.run(
        [str(ROOT / "scripts" / "build_link_library.sh"), "--target", "mock"],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        result.returncode != 0 and "no linkable-library runner" in result.stderr,
        "dispatcher accepted the host mock target",
    )
    result = subprocess.run(
        [str(ROOT / "scripts" / "build_link_library.sh"), "--target", "esp32s3", "--help"],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        result.returncode == 0 and "build_esp32_lib.sh" in result.stdout,
        "dispatcher did not forward --help to the ESP-IDF runner",
    )
    result = subprocess.run(
        [str(ROOT / "scripts" / "build_stm32_lib.sh"), "--target", "rp2040"],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        result.returncode != 0 and "build_link_library.sh" in result.stderr,
        "STM32 runner accepted a Pico SDK target",
    )

print("link_libraries runner contracts verified")
