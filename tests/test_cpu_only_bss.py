#!/usr/bin/env python3
"""Verify CPU-buffer placement, section collection and production startup."""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(sys.argv[1]).resolve()
STM = ROOT / "link_libraries/stm32_lib"
SOURCE = r'''
#include "hal/core/hal_memory.h"
#include <stdint.h>
static uint64_t HAL_CPU_ONLY_BSS(cpu_buffer)[128];
static uint64_t HAL_CPU_ONLY_BSS(unused_buffer)[128] __attribute__((used));
static uint32_t ordinary_buffer;
static uint32_t fault_stack[128] __attribute__((section(".ccmram.fault")));
#ifdef WITH_TLS
static uint32_t tls_storage[5000] __attribute__((section(".ccmram.tls")));
#endif
extern char end;
volatile uintptr_t sink;
void SystemInit(void) {}
int main(void) {
    sink = (uintptr_t)cpu_buffer;
    sink = (uintptr_t)&ordinary_buffer;
    sink = (uintptr_t)fault_stack;
    sink = (uintptr_t)&end;
#ifdef WITH_TLS
    sink = (uintptr_t)tls_storage;
#endif
    return 0;
}
'''


def run(*args):
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, check=False)
    if result.returncode:
        raise AssertionError(f"Command failed: {args}\n{result.stdout}")
    return result.stdout


def symbols(elf):
    result = {}
    for line in run("arm-none-eabi-nm", "-S", str(elf)).splitlines():
        parts = line.split()
        if len(parts) in (3, 4):
            result[parts[-1]] = int(parts[0], 16)
    return result


def check_initializers(work):
    for suffix, compiler in (("c", "cc"), ("cpp", "c++")):
        source = work / f"initialized.{suffix}"
        source.write_text('#include "hal/core/hal_memory.h"\n'
                          'int HAL_CPU_ONLY_BSS(invalid_buffer) = 42;\n')
        for section in (".bss.hal_cpu.sram", ".bss.hal_cpu.ccm"):
            command = [compiler, "-I", str(ROOT / "src"),
                       f'-DJH_CPU_ONLY_BSS_SECTION="{section}"',
                       "-c", str(source), "-o", str(work / "invalid.o")]
            result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, check=False)
            assert result.returncode != 0, "Nonzero initializer was accepted"
            assert "zero" in result.stdout.lower(), result.stdout


def check_startup(work):
    # Execute the production reset routine with synthetic linker boundaries.
    # Vector-table addresses are checked separately by the ARM link probe.
    text = (ROOT / "src/hal/impl/stm32g474/port/startup_stm32g474.c").read_text()
    start = text.index("void Reset_Handler(void) {")
    stop = text.index("void Default_Handler(void) {", start)
    reset = text[start:stop]
    fixture = r'''
#include <stdint.h>
#include <stdlib.h>
typedef void (*init_function_t)(void);
static uint32_t source[2] = {23u, 47u};
static uint32_t data[2], bss[2], ccm[2], retained[2] = {71u, 93u};
static unsigned stage;
#define _sidata source[0]
#define _sdata data[0]
#define _edata data[2]
#define _sbss bss[0]
#define _ebss bss[2]
#define _sccmbss ccm[0]
#define _eccmbss ccm[CCM_WORDS]
static void verify(void) {
    if (data[0] != 23u || data[1] != 47u || bss[0] || bss[1] ||
        ccm[0] != (CCM_WORDS ? 0u : 19u) ||
        ccm[1] != (CCM_WORDS ? 0u : 19u) ||
        retained[0] != 71u || retained[1] != 93u) exit(1);
}
void SystemInit(void) { verify(); if (stage++ != 0u) exit(2); }
static void preinit(void) { verify(); if (stage++ != 1u) exit(3); }
static void constructor(void) { verify(); if (stage++ != 2u) exit(4); }
static init_function_t preinit_array[] = {preinit};
static init_function_t init_array[] = {constructor};
#define __preinit_array_start preinit_array
#define __preinit_array_end (preinit_array + 1)
#define __init_array_start init_array
#define __init_array_end (init_array + 1)
int probe_main(void) { verify(); exit(stage == 3u ? 0 : 5); }
#define main probe_main
'''
    tail = r'''
#undef main
int main(void) {
    data[0] = data[1] = bss[0] = bss[1] = ccm[0] = ccm[1] = 19u;
    Reset_Handler();
    return 6;
}
'''
    source = work / "startup.c"
    source.write_text(fixture + reset + tail)
    for words in (0, 2):
        output = work / f"startup-{words}"
        run("cc", "-Wall", "-Wextra", "-Werror", "-O2",
            f"-DCCM_WORDS={words}", str(source), "-o", str(output))
        run(str(output))


def check_targets(work):
    source = work / "placement.c"
    source.write_text(SOURCE)
    for target in ("MOCK", "RP2040", "RP2350_ARM", "RP2350_RISCV",
                   "ESP32", "ESP32_S3"):
        obj = work / f"{target}.o"
        run("cc", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "src"),
            f"-DHAL_TARGET_{target}", "-c", str(source), "-o", str(obj))
        table = run("objdump", "-t", str(obj))
        assert any(".bss.hal_cpu.sram.cpu_buffer" in line
                   and line.endswith(" cpu_buffer")
                   for line in table.splitlines()), table


def check_stm32(work):
    arm = shutil.which("arm-none-eabi-gcc")
    if arm is None:
        print("STM32 ELF placement SKIP (arm-none-eabi-gcc absent)")
        return
    project = work / "stm32"
    project.mkdir()
    (project / "probe.c").write_text(SOURCE)
    (project / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 3.20)
project(cpu_bss_probe C)
include("{STM.as_posix()}/jh_stm32g474_memory.cmake")
add_library(buffers STATIC probe.c)
target_include_directories(buffers PUBLIC "{ROOT.as_posix()}/src")
target_compile_definitions(buffers PUBLIC HAL_TARGET_STM32G474=1)
if(WITH_TLS)
    target_compile_definitions(buffers PUBLIC WITH_TLS=1)
    set(features HAL_ENABLE_TLS)
endif()
jh_stm32g474_configure_memory(buffers PUBLIC FEATURES ${{features}})
add_executable(probe
    "{ROOT.as_posix()}/src/hal/impl/stm32g474/port/startup_stm32g474.c")
target_link_libraries(probe PRIVATE buffers)
target_link_options(probe PRIVATE -nostdlib -Wl,--gc-sections
    "-T{STM.as_posix()}/STM32G474RETx_FLASH.ld")
''')
    for tls in (False, True):
        build = project / ("tls" if tls else "plain")
        run("cmake", "-S", str(project), "-B", str(build),
            f"-DCMAKE_C_COMPILER={arm}", "-DCMAKE_SYSTEM_NAME=Generic",
            "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
            "-DCMAKE_C_FLAGS=-Os -ffreestanding -fno-builtin -mthumb "
            "-mcpu=cortex-m4 -ffunction-sections -fdata-sections "
            "-Wall -Wextra -Werror", f"-DWITH_TLS={'ON' if tls else 'OFF'}")
        run("cmake", "--build", str(build))
        found = symbols(build / "probe")
        assert "unused_buffer" not in found, "Unused CPU buffer survives GC"
        start, end = ("_sbss", "_ebss") if tls else ("_sccmbss", "_eccmbss")
        assert found[start] <= found["cpu_buffer"] < found[end], found
        assert found["cpu_buffer"] % 8 == 0, found
        assert 0x20000000 <= found["ordinary_buffer"] < found["end"], found
        assert 0x10000000 <= found["fault_stack"] < 0x10008000, found
        assert found["fault_stack"] >= found["_eccmbss"], found
        if tls:
            assert found["_sccmbss"] == found["_eccmbss"], found
            assert 0x10000000 <= found["tls_storage"] < 0x10008000, found
        else:
            assert 0x10000000 <= found["cpu_buffer"] < 0x10008000, found


def check_macho(work):
    clang = shutil.which("clang") or shutil.which("clang-18")
    if clang is None:
        print("Mach-O host headers SKIP (Clang absent)")
        return
    source = work / "macho.c"
    source.write_text('#include "hal/core/hal_memory.h"\n'
                      'volatile uint64_t HAL_CPU_ONLY_BSS(host_buffer);\n'
                      'void HAL_RAM_FUNC(host_probe)(void);\n'
                      'void HAL_RAM_FUNC(host_probe)(void) { host_buffer = 1u; }\n')
    for language in ("c", "c++"):
        run(clang, "--target=x86_64-apple-darwin", "-ffreestanding",
            "-Wall", "-Wextra", "-Werror", "-DHAL_TARGET_MOCK=1",
            "-I", str(ROOT / "src"), "-x", language, "-c", str(source),
            "-o", str(work / f"macho-{language}.o"))


def main():
    with tempfile.TemporaryDirectory(prefix="jh-cpu-bss-") as directory:
        work = Path(directory)
        check_initializers(work)
        check_startup(work)
        check_targets(work)
        check_stm32(work)
        check_macho(work)
    print("CPU-only BSS: initialization, placement, TLS fallback and GC PASS")


if __name__ == "__main__":
    main()
