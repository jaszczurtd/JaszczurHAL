#!/usr/bin/env python3
"""Link the STM32 entropy provider with CRYPTO and no network features."""

import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(sys.argv[1]).resolve()
COMPILER = sys.argv[2] if len(sys.argv) > 2 else "c++"

SOURCE = r'''
#include "hal/core/hal_array.h"
#include "hal/core/hal_config.h"
#include "hal/security/jh_secure_random.h"
#include <stdint.h>
#include <string.h>

#if !defined(HAL_ENABLE_CRYPTO) || defined(HAL_ENABLE_TLS) || \
    defined(HAL_ENABLE_WIREGUARD) || defined(HAL_ENABLE_BLE_STREAM)
#error "Probe requires CRYPTO without TLS, WireGuard or BLE Stream"
#endif

int main() {
    uint8_t bytes[17];
    memset(bytes, 0xa5, sizeof(bytes));
    if (jh_secure_random_bytes(nullptr, sizeof(bytes)) != HAL_EINVAL ||
        jh_secure_random_bytes(bytes, 0u) != HAL_EINVAL || bytes[0] != 0xa5) {
        return 1;
    }
    const size_t lengths[] = {1u, 7u, sizeof(bytes)};
    for (size_t n = 0u; n < COUNTOF(lengths); ++n) {
        memset(bytes, 0xa5, sizeof(bytes));
        if (jh_secure_random_bytes(bytes, lengths[n]) != HAL_EUNSUPPORTED) {
            return 2;
        }
        for (size_t i = 0u; i < sizeof(bytes); ++i) {
            if (bytes[i] != (i < lengths[n] ? 0u : 0xa5u)) {
                return 3;
            }
        }
    }
    return 0;
}
'''


with tempfile.TemporaryDirectory(prefix="jh-stm32-crypto-entropy-") as directory:
    work = Path(directory)
    (work / "hal_project_config.h").write_text(
        "#pragma once\n#define HAL_ENABLE_CRYPTO\n", encoding="utf-8"
    )
    source = work / "probe.cpp"
    source.write_text(SOURCE, encoding="utf-8")
    executable = work / "probe"
    subprocess.run(
        [COMPILER, "-std=c++17", "-Wall", "-Wextra", "-Werror",
         "-DHAL_TARGET_STM32G474=1", "-I", str(work), "-I", str(ROOT / "src"),
         str(source), str(ROOT / "src/hal/security/jh_secure_random.cpp"),
         str(ROOT / "src/hal/impl/stm32g474/stm32g474_tls_entropy.cpp"),
         "-o", str(executable)],
        check=True,
    )
    subprocess.run([str(executable)], check=True)

print("STM32 CRYPTO entropy: link and host fail-closed behavior PASS")
