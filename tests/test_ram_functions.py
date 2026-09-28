#!/usr/bin/env python3
"""Compile RAM functions in C/C++ and check STM32's startup copy range."""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(sys.argv[1]).resolve()
SOURCE = r'''
#include "hal/core/hal_memory.h"
#ifdef __cplusplus
extern "C" {
#endif
volatile int result;
int HAL_RAM_FUNC(ram_probe)(int value);
int HAL_RAM_FUNC(ram_probe)(int value) { return value + 7; }
void Reset_Handler(void) { result = ram_probe(result); }
int main(void) { return ram_probe(5) != 12; }
#ifdef __cplusplus
}
#endif
'''


def run(*args):
    try:
        return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as error:
        print(error.output, file=sys.stderr)
        raise


def main():
    with tempfile.TemporaryDirectory(prefix="jh-ram-functions-") as directory:
        work = Path(directory)
        for language, compiler in (("c", "cc"), ("cpp", "c++")):
            source = work / f"probe.{language}"
            source.write_text(SOURCE)
            output = work / f"host-{language}"
            run(compiler, "-Wall", "-Wextra", "-Werror", "-O2", "-I", str(ROOT / "src"),
                str(source), "-o", str(output))
            run(str(output))
            for target in ("ESP32", "ESP32_S3"):
                obj = work / f"{target}-{language}.o"
                run(compiler, "-Wall", "-Wextra", "-Werror", "-O2", "-I", str(ROOT / "src"),
                    f"-DHAL_TARGET_{target}", "-c", str(source), "-o", str(obj))
                table = run("objdump", "-t", str(obj))
                assert any(".iram1.ram_probe" in line and line.endswith(" ram_probe")
                           for line in table.splitlines()), table
        arm = shutil.which("arm-none-eabi-gcc")
        if arm is None:
            print("Host C/C++ PASS; ARM placement SKIP (arm-none-eabi-gcc absent)")
            return
        for target, cpu, section in (
            ("RP2040", "cortex-m0plus", ".time_critical.ram_probe"),
            ("RP2350_ARM", "cortex-m33", ".time_critical.ram_probe"),
            ("STM32G474", "cortex-m4", ".ram_func.ram_probe"),
        ):
            for language in ("c", "cpp"):
                obj = work / f"{target}-{language}.o"
                run(arm, "-Wall", "-Wextra", "-Werror", "-O2", "-ffreestanding",
                    "-fno-exceptions", "-mthumb", f"-mcpu={cpu}", f"-DHAL_TARGET_{target}",
                    "-I", str(ROOT / "src"), "-c", str(work / f"probe.{language}"),
                    "-o", str(obj))
                table = run("arm-none-eabi-objdump", "-t", str(obj))
                assert any(section in line and line.endswith(" ram_probe")
                           for line in table.splitlines()), table
                # The caller must retain a relocation to the RAM function.
                relocations = run("arm-none-eabi-objdump", "-r", str(obj))
                assert "ram_probe" in relocations, relocations
                if target == "STM32G474":
                    elf = obj.with_suffix(".elf")
                    script = ROOT / "link_libraries/stm32_lib/STM32G474RETx_FLASH.ld"
                    run(arm, "-nostdlib", "-mthumb", f"-mcpu={cpu}",
                        "-T", str(script), str(obj), "-o", str(elf))
                    symbols = {}
                    for line in run("arm-none-eabi-nm", str(elf)).splitlines():
                        parts = line.split()
                        if len(parts) == 3:
                            symbols[parts[2]] = int(parts[0], 16)
                    assert 0x20000000 <= symbols["_sdata"] <= symbols["ram_probe"] < symbols["_edata"]
                    assert 0x08000000 <= symbols["_sidata"] < 0x08080000
        riscv = ROOT / "third_party/riscv-toolchain/bin/riscv32-unknown-elf-gcc"
        if riscv.exists():
            for language in ("c", "cpp"):
                obj = work / f"riscv-{language}.o"
                run(str(riscv), "-Wall", "-Wextra", "-Werror", "-O2", "-ffreestanding",
                    "-DHAL_TARGET_RP2350_RISCV", "-I", str(ROOT / "src"),
                    "-c", str(work / f"probe.{language}"), "-o", str(obj))
                objdump = riscv.with_name("riscv32-unknown-elf-objdump")
                table = run(str(objdump), "-t", str(obj))
                assert any(".time_critical.ram_probe" in line and line.endswith(" ram_probe")
                           for line in table.splitlines()), table
        print("RAM functions: C/C++ host, target sections and STM32 copy range PASS")


if __name__ == "__main__":
    main()
