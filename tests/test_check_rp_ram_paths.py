#!/usr/bin/env python3
"""Check scripts/check_rp_ram_paths.py on small linked ARM and RISC-V images.

Each image places the checked functions in RAM or flash as a case needs and
lets them call a flash function, an allowed error path or an atomic helper.
Branch forms a compiler rarely emits on demand come from listings that stand
in for nm and objdump.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(sys.argv[1]).resolve()
CHECK = ROOT / "scripts/check_rp_ram_paths.py"

LINKER_SCRIPT = """
MEMORY {
  FLASH (rx) : ORIGIN = 0x10000000, LENGTH = 1M
  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}
SECTIONS {
  .text : { *(.text*) } > FLASH
  .data : { *(.time_critical*) *(.data*) } > RAM AT > FLASH
}
"""

# MICROS_CALLS_FLASH: hal_micros calls a flash function.
# MICROS_IT_CALL: the same call made conditional by an IT block (Thumb-2).
# MICROS_IN_FLASH: hal_micros itself stays in flash.
# NO_MICROS: hal_micros is not linked.
# ATOMIC: the timer path calls this atomic helper (in flash).
SOURCE = r"""
#define RAM(name) __attribute__((section(".time_critical." #name), noinline)) name
#define FLASH __attribute__((noinline, used))
#define STR2(x) #x
#define STR(x) STR2(x)
volatile unsigned sink;
FLASH void flash_helper(void) { sink++; }
FLASH void hal_assert_fail(void) { sink += 2u; }
#ifdef ATOMIC
/* The helper's symbol, not the compiler builtin of that name. */
void atomic_helper(void) __asm__(STR(ATOMIC));
FLASH void atomic_helper(void) { sink += 3u; }
#endif
#ifdef MICROS_VIA_RAM_HELPER
void RAM(ram_helper)(void) { flash_helper(); }
#endif
#ifndef NO_MICROS
#ifdef MICROS_IN_FLASH
FLASH unsigned hal_micros(void) { return sink; }
#else
unsigned RAM(hal_micros)(void) {
#ifdef MICROS_CALLS_FLASH
  flash_helper();
#endif
#ifdef MICROS_IT_CALL
  __asm__ volatile("cmp %0, #0\n\tit ne\n\tblne flash_helper"
                   : : "r"(sink) : "r0", "r1", "r2", "r3", "r12", "lr", "cc", "memory");
#endif
#ifdef MICROS_VIA_RAM_HELPER
  ram_helper();
#endif
  return sink;
}
#endif
#endif
void RAM(hal_gpio_write)(unsigned pin) {
  if (pin > 29u) {
    hal_assert_fail();
  }
  sink = pin;
}
void RAM(timer_alarm_owned)(void) {
#ifdef ATOMIC
  atomic_helper();
#endif
  sink = 0u;
}
void _start(void) {
#ifndef NO_MICROS
  sink = hal_micros();
#endif
  hal_gpio_write(sink);
  timer_alarm_owned();
}
"""


def tools(prefix):
    gcc = shutil.which(f"{prefix}gcc") if "/" not in prefix else f"{prefix}gcc"
    if gcc is None or not Path(gcc).exists():
        return None
    base = gcc[: -len("gcc")]
    return base, [f"{base}nm", f"{base}objdump"]


CXX_SOURCE = r"""
#define RAM(name) __attribute__((section(".time_critical." #name), noinline)) name
extern "C" {
volatile unsigned sink;
__attribute__((noinline, used)) void flash_helper(void) { sink++; }
}
namespace {
void RAM(helper)(long) { flash_helper(); }
void RAM(helper)(int value) { sink = (unsigned)value; }
}  // namespace
extern "C" {
// The overload that calls flash comes first, so a check that tells the two
// apart by name has already marked "helper" as checked when it gets there.
unsigned RAM(hal_micros)(void) {
  helper(1L);
  helper(1);
  return sink;
}
void RAM(hal_gpio_write)(unsigned pin) { sink = pin; }
void _start(void) { hal_gpio_write(hal_micros()); }
}
"""


def link(work, base, flags, name, defines, compiler="gcc", source="probe.c"):
    elf = work / f"{name}.elf"
    subprocess.run([f"{base}{compiler}", *flags, "-O1", "-ffreestanding", "-nostdlib",
                    "-Wl,--no-warn-rwx-segments",
                    *[f"-D{define}" for define in defines],
                    "-T", str(work / "link.ld"), str(work / source), "-o", str(elf)],
                   check=True)
    return elf


def check(elf, nm, objdump, atomics="none", env=None):
    result = subprocess.run([sys.executable, str(CHECK), str(elf), "--nm", nm,
                             "--objdump", objdump, "--atomic-helpers", atomics],
                            text=True, capture_output=True, env=env)
    return result.returncode, result.stdout + result.stderr


def expect(result, code, text=None):
    assert result[0] == code, result
    if text is not None:
        assert text in result[1], result


def run_cases(work, base, nm, objdump, flags, label):
    def build(name, *defines):
        return link(work, base, flags, f"{label}-{name}", defines)

    expect(check(build("clean"), nm, objdump), 0, "PASS")
    expect(check(build("micros-calls-flash", "MICROS_CALLS_FLASH"), nm, objdump), 1,
           "hal_micros: calls flash_helper")
    expect(check(build("micros-via-ram", "MICROS_VIA_RAM_HELPER"), nm, objdump), 1,
           "hal_micros -> ram_helper: calls flash_helper")
    expect(check(build("micros-in-flash", "MICROS_IN_FLASH"), nm, objdump), 1,
           "hal_micros: at 0x1")
    expect(check(build("no-micros", "NO_MICROS"), nm, objdump), 1,
           "hal_micros: missing from the image")
    narrow = build("atomic-4", "ATOMIC=__atomic_fetch_add_4")
    expect(check(narrow, nm, objdump, "none"), 1, "calls __atomic_fetch_add_4")
    expect(check(narrow, nm, objdump, "wide"), 1, "calls __atomic_fetch_add_4")
    expect(check(narrow, nm, objdump, "all"), 0)
    wide = build("atomic-8", "ATOMIC=__atomic_store_8")
    expect(check(wide, nm, objdump, "none"), 1, "calls __atomic_store_8")
    expect(check(wide, nm, objdump, "wide"), 0)
    if Path(f"{base}g++").exists() or shutil.which(f"{base}g++"):
        cxx = link(work, base, [*flags, "-fno-exceptions", "-fno-rtti"], f"{label}-cxx", (),
                   compiler="g++", source="probe.cpp")
        expect(check(cxx, nm, objdump), 1,
               "hal_micros -> (anonymous namespace)::helper: calls flash_helper")


# Stands in for nm (symbol table) or objdump (the listing lines in the
# requested address range) from the fixture JH_RP_RAM_FIXTURE names.
FAKE_TOOL = """import json, os, sys
fixture = json.load(open(os.environ["JH_RP_RAM_FIXTURE"]))
if sys.argv[1] == "nm":
    names = fixture["nm_demangled"] if "-C" in sys.argv else fixture["nm"]
    print("\\n".join(names))
    sys.exit(0)
bounds = dict(arg.split("=", 1) for arg in sys.argv if arg.startswith("--st"))
start = int(bounds["--start-address"], 16)
stop = int(bounds["--stop-address"], 16)
for line in fixture["listing"]:
    if start <= int(line.split(":")[0], 16) < stop:
        print(line)
"""

SYMBOLS = [
    "20000000 00000010 T hal_gpio_write",
    "20000100 00000020 T hal_micros",
    "20000200 00000010 t __flash_helper_veneer",
    "20000300 00000010 t ram_helper",
    "20000400 00000010 t __mystery_veneer",
    "10000020 00000010 T flash_helper",
]
# Functions that share a name: the same anonymous-namespace function in two
# files, two overloads, two copies of a static listed function. Raw name,
# demangled name.
TWINS = [
    ("20000500 00000010 t _ZN12_GLOBAL__N_16helperEv",
     "20000500 00000010 t (anonymous namespace)::helper()"),
    ("20000600 00000010 t _ZN12_GLOBAL__N_16helperEv",
     "20000600 00000010 t (anonymous namespace)::helper()"),
    ("20000700 00000010 t _Z8overloadi", "20000700 00000010 t overload(int)"),
    ("20000800 00000010 t _Z8overloadl", "20000800 00000010 t overload(long)"),
    ("20000900 00000010 t timer_alarm_owned", "20000900 00000010 t timer_alarm_owned"),
    ("20000a00 00000010 t timer_alarm_owned", "20000a00 00000010 t timer_alarm_owned"),
]
GPIO = ["20000000:\tstr\tr3, [r2, #24]"]


def listing_cases(work):
    script = work / "fake_tool.py"
    script.write_text(FAKE_TOOL)
    tools = {}
    for name in ("nm", "objdump"):
        tool = work / f"fake-{name}"
        tool.write_text(f"#!/bin/sh\nexec {sys.executable} {script} {name} \"$@\"\n")
        tool.chmod(0o755)
        tools[name] = str(tool)
    env = dict(os.environ, JH_RP_RAM_FIXTURE=str(work / "fixture.json"))

    def run_listing(micros, extra=()):
        fixture = {"nm": SYMBOLS + [raw for raw, _ in TWINS],
                   "nm_demangled": SYMBOLS + [shown for _, shown in TWINS],
                   "listing": [*GPIO, *micros, *extra]}
        (work / "fixture.json").write_text(json.dumps(fixture))
        return check(work / "image.elf", tools["nm"], tools["objdump"], env=env)

    # Each form reaches flash_helper in flash.
    for form in ("20000104:\tbne.w\t20000200 <__flash_helper_veneer>",
                 "20000104:\tbl\t10000024 <flash_helper+0x4>",
                 "20000104:\tcbz\tr0, 10000020 <flash_helper>",
                 "20000104:\tbeq\ta0,zero,10000020 <flash_helper>",
                 "20000104:\tjalr\t-1020(ra) # 10000020 <flash_helper>",
                 "20000104:\tj\t10000020 <flash_helper>",
                 "20000104:\tblne\t10000020 <flash_helper>",
                 "20000104:\tbleq.w\t10000020 <flash_helper>"):
        expect(run_listing([form]), 1, "hal_micros: calls flash_helper")
    expect(run_listing(["20000104:\tbl\t20000300 <ram_helper>"],
                       ["20000304:\tbl\t10000020 <flash_helper>"]),
           1, "hal_micros -> ram_helper: calls flash_helper")
    # Of two functions sharing a name, the one calling flash is found whichever
    # of them is reached first.
    for name, first, second in (("(anonymous namespace)::helper", 0x20000500, 0x20000600),
                                ("overload", 0x20000700, 0x20000800)):
        for flash in (first, second):
            calls = [f"2000010{4 * index:x}:\tbl\t{address:08x} <{name}>"
                     for index, address in enumerate((first, second), 1)]
            expect(run_listing(calls, [f"{flash + 4:08x}:\tbl\t10000020 <flash_helper>"]),
                   1, f"hal_micros -> {name}: calls flash_helper")
    for flash in (0x20000900, 0x20000a00):
        expect(run_listing([], [f"{flash + 4:08x}:\tbl\t10000020 <flash_helper>"]),
               1, "timer_alarm_owned: calls flash_helper")
    expect(run_listing(["20000104:\tbl\t20000400 <__mystery_veneer>"]),
           1, "a veneer to an unknown symbol")
    # A branch inside the function, a register call and a literal load.
    expect(run_listing(["20000104:\tbne.n\t20000108 <hal_micros+0x8>",
                        "2000010a:\tblx\tr3",
                        "2000010c:\tldr\tr0, [pc, #8]\t@ (10000024 <flash_helper+0x4>)"],
                       ["20000304:\tbl\t10000020 <flash_helper>"]), 0, "PASS")


def main():
    with tempfile.TemporaryDirectory(prefix="jh-rp-ram-listing-") as directory:
        listing_cases(Path(directory))
    arm = tools("arm-none-eabi-")
    riscv = tools(str(ROOT / "third_party/riscv-toolchain/bin/riscv32-unknown-elf-"))
    if arm is None and riscv is None:
        print("RP RAM path check: listings PASS; images SKIP (no ARM or RISC-V toolchain)")
        return
    with tempfile.TemporaryDirectory(prefix="jh-rp-ram-paths-") as directory:
        work = Path(directory)
        (work / "link.ld").write_text(LINKER_SCRIPT)
        (work / "probe.c").write_text(SOURCE)
        (work / "probe.cpp").write_text(CXX_SOURCE)
        checked = []
        if arm is not None:
            base, (nm, objdump) = arm
            run_cases(work, base, nm, objdump, ["-mthumb", "-mcpu=cortex-m0plus"], "arm")
            it_call = link(work, base, ["-mthumb", "-mcpu=cortex-m33"], "arm-it-call",
                           ("MICROS_IT_CALL",))
            expect(check(it_call, nm, objdump), 1, "hal_micros: calls flash_helper")
            checked.append("ARM")
        if riscv is not None:
            base, (nm, objdump) = riscv
            run_cases(work, base, nm, objdump, ["-march=rv32imac", "-mabi=ilp32"], "riscv")
            checked.append("RISC-V")
    print(f"RP RAM path check: listings, {', '.join(checked)} images PASS")


if __name__ == "__main__":
    main()
