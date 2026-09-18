#!/usr/bin/env python3
"""Host contracts for the ESP32 sample-paced PWM audio backend."""

from __future__ import annotations

import json
from pathlib import Path
import re
import sys
import tempfile


ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import build_esp_idf
import generate_hal_features


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def function_body(text: str, signature: str) -> str:
    """Return the braces block of the first function matching a signature."""
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def resolve(project: Path, features: list[str]) -> dict:
    return build_esp_idf.resolve_build_model(
        ROOT,
        project,
        target="esp32s3",
        board="",
        project_name="audio_probe",
        requested_sources=[],
        features=features,
        definitions=[],
    )


# The allowlist must carry the whole audio closure; otherwise a project that
# asks for DACless is rejected before the backend is ever compiled.
target = json.loads(read("boards/targets/esp32s3.json"))
supported = set(target["supportedFeatures"])
for symbol in (
    "HAL_ENABLE_DACLESS",
    "HAL_ENABLE_DMA_PWM_AUDIO",
    "HAL_ENABLE_PWM_FREQ",
):
    require(symbol in supported, f"esp32s3 allowlist is missing {symbol}")

model = generate_hal_features.load_registry(ROOT / "config")
for symbol, source in (
    ("HAL_ENABLE_DMA_PWM_AUDIO", "src/hal/audio/hal_dma_pwm_audio.cpp"),
    ("HAL_ENABLE_DACLESS", "src/hal/audio/hal_dacless.cpp"),
    ("HAL_ENABLE_DACLESS", "src/hal/audio/dacless/dacless.cpp"),
):
    require(
        source in model.features[symbol].portable_sources,
        f"{symbol} does not register {source}",
    )

require(
    build_esp_idf.ESP_IDF_TARGET_SOURCES["HAL_ENABLE_DMA_PWM_AUDIO"]
    == ("src/hal/impl/esp32/hal_dma_pwm_audio.cpp",),
    "the ESP-IDF graph does not select the ESP32 audio backend",
)

with tempfile.TemporaryDirectory(prefix="jh-esp32-audio-") as temporary:
    project = Path(temporary)
    (project / "app.c").write_text(
        "void app_start(void) {}\nvoid app_task0(void) {}\n", encoding="utf-8"
    )
    (project / "hal_project_config.h").write_text(
        "#pragma once\n#define HAL_ENABLE_DACLESS 1\n", encoding="utf-8"
    )
    audio_model = resolve(project, [])
    resolved = set(audio_model["resolvedFeatures"])
    for symbol in (
        "HAL_ENABLE_DACLESS",
        "HAL_ENABLE_DMA_PWM_AUDIO",
        "HAL_ENABLE_PWM_FREQ",
    ):
        require(symbol in resolved, f"DACless does not resolve {symbol}")

    sources = set(audio_model["integrationSources"])
    for source in (
        "src/hal/impl/esp32/hal_dma_pwm_audio.cpp",
        "src/hal/impl/esp32/hal_pwm_freq.cpp",
        "src/hal/audio/hal_dma_pwm_audio.cpp",
        "src/hal/audio/hal_dacless.cpp",
        "src/hal/audio/dacless/dacless.cpp",
    ):
        require(source in sources, f"the audio build graph is missing {source}")
    for source in sources:
        require((ROOT / source).is_file(), f"missing audio source: {source}")

    # The sample clock is an interrupt that writes LEDC while the flash cache
    # may be disabled, so both drivers must be reachable from IRAM.
    defaults = build_esp_idf._render_sdkconfig_defaults(audio_model)
    for option in (
        "CONFIG_GPTIMER_ISR_HANDLER_IN_IRAM=y",
        "CONFIG_LEDC_CTRL_FUNC_IN_IRAM=y",
    ):
        require(option in defaults, f"generated sdkconfig is missing {option}")

    plain = build_esp_idf._render_sdkconfig_defaults(resolve(project, []))
    require("CONFIG_LEDC_CTRL_FUNC_IN_IRAM=y" in plain, "audio IRAM option drifted")

backend = read("src/hal/impl/esp32/hal_dma_pwm_audio.cpp")
require(
    "bool IRAM_ATTR audio_alarm(" in backend,
    "the audio alarm handler is not placed in IRAM",
)
alarm = function_body(backend, "bool IRAM_ATTR audio_alarm(")
require(
    "jh_esp32_ledc_write_from_isr(" in alarm,
    "the alarm handler does not use the interrupt-safe LEDC write",
)
for forbidden in ("hal_mutex_lock", "hal_adc_read", "ledc_channel_config"):
    require(
        forbidden not in alarm,
        f"the alarm handler calls {forbidden}, which may block or touch flash",
    )
require(
    "xTaskNotifyFromISR(" in alarm,
    "finished buffers are not handed to the service task",
)
for entry in (
    "hal_dma_pwm_audio_create_ex",
    "hal_dma_pwm_audio_start_ex",
    "hal_dma_pwm_audio_stop",
    "hal_dma_pwm_audio_pause",
    "hal_dma_pwm_audio_resume",
    "hal_dma_pwm_audio_destroy_ex",
    "hal_dma_pwm_audio_is_running",
    "hal_dma_pwm_audio_is_paused",
):
    require(f"{entry}(" in backend, f"the backend does not implement {entry}")
require(
    "hal_dma_pwm_audio_supported" not in backend,
    "the backend repeats the shared facade instead of reusing it",
)

ledc = read("src/hal/impl/esp32/jh_esp32_ledc.cpp")
isr_write = function_body(ledc, "bool IRAM_ATTR jh_esp32_ledc_write_from_isr(")
require(
    "hal_mutex_lock" not in isr_write and "ledc_mutex" not in isr_write,
    "the interrupt-safe LEDC write takes the module mutex",
)
require(
    "ledc_set_duty_and_update(" in isr_write,
    "the interrupt-safe LEDC write does not update the duty cycle",
)

pwm_freq = read("src/hal/impl/esp32/hal_pwm_freq.cpp")
require(
    "jh_hal_pwm_freq_try_create(" in pwm_freq,
    "ESP32 has no status-returning PWM frequency create",
)
require(
    "jh_hal_pwm_freq_create_compat(" in pwm_freq,
    "ESP32 PWM frequency create duplicates the shared compatibility wrapper",
)

# The portable audio sources must stay free of target enumerations so every
# backend, including ESP32, compiles them.
target_guard = re.compile(r"HAL_TARGET_IS_RP\s*\|\|\s*HAL_TARGET_IS_STM32G474")
for relative in (
    "src/hal/audio/hal_dma_pwm_audio.cpp",
    "src/hal/audio/hal_dacless.cpp",
    "src/hal/audio/hal_dacless.h",
    "src/hal/audio/dacless/dacless.cpp",
    "src/hal/audio/dacless/dacless.h",
):
    require(
        target_guard.search(read(relative)) is None,
        f"{relative} still enumerates supported targets",
    )

example = read("examples/17_audio_output/app.c")
require(
    "HAL_TARGET_IS_ESP32_FAMILY" in example,
    "17_audio_output has no ESP32 pin assignment",
)
manifest = json.loads(read("examples/17_audio_output/.vscode/jaszczurhal.project.json"))
require(
    "esp32s3" in manifest["example"]["targets"],
    "17_audio_output does not declare esp32s3",
)

print("ESP32 PWM audio backend contracts verified")
