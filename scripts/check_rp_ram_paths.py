#!/usr/bin/env python3
"""Check that the RP functions HAL places in RAM run without flash code.

A linked RP image is disassembled. Every listed function present in it must
lie in SRAM, and so must everything it reaches through direct branches and
calls: each RAM function reached is checked the same way, and a target in
flash must be a callee allowed for the listed function the path starts from.
A target is taken from the branch address, so conditional branches and
labels with an offset count as well; a veneer counts as the symbol it jumps
to. Calls through a register (callbacks) are allowed. Functions are told
apart by address, so C++ overloads and anonymous-namespace functions sharing
a name are each checked.

Atomics the core has no instructions for are calls into toolchain or Pico
SDK helpers in flash, allowed only where the core needs them: on RP2040 the
read-modify-write helpers of every width and the 8-byte loads and stores
(the Cortex-M0+ has no exclusive access), on RP2350 the 8-byte ones
(ARMv8-M and RV32 atomics are 4 bytes wide). Aligned loads and stores up to
4 bytes need no helper.
"""
import argparse
import bisect
import re
import subprocess
import sys

SRAM = (0x20000000, 0x20082000)

# Function -> callees outside RAM it, or a RAM function it reaches, may call.
FUNCTIONS = {
    "hal_micros": (),
    "hal_micros64": (),
    # Only the error path and the CYW43 pins leave RAM.
    "hal_gpio_write": ("hal_assert_fail", "hal_cyw43_digitalWrite", "cyw43_gpio_write"),
    "timer_dispatch_callback": (),
    "timer_internal_alarm_cb": (),
    "timer_internal_alarm_cb_body": (),
    "timer_alarm_owned": (),
}
# Present in every RP image built with the HAL runtime.
REQUIRED = ("hal_micros", "hal_gpio_write")
ATOMIC_HELPERS = {
    "none": None,
    "all": re.compile(r"^__atomic_\w+$"),
    "wide": re.compile(r"^__atomic_\w+_8$"),
}

LINE = re.compile(r"^\s*([0-9a-f]+):\s+([a-z][\w.]*)\s*(.*)$")
# Direct branches and calls of ARMv6-M/ARMv8-M Thumb and RV32 with the C
# extension: on ARM b, bl and blx with any condition (bl takes one inside an
# IT block) and an optional .w/.n width suffix, and cbz/cbnz.
ARM_BRANCH = re.compile(
    r"^(?:(?:blx?|b)(?:eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)?"
    r"|cbn?z)(?:\.[wn])?$")
RISCV_BRANCH = re.compile(
    r"^(?:c\.)?(?:j|jal|jalr|call|tail|beqz?|bnez?|bltu?|bgeu?|bgtu?|bleu?"
    r"|bltz|bgez|blez|bgtz)$")
COMMENT_TARGET = re.compile(r"#\s*([0-9a-f]+)\s+<")
OPERAND_TARGET = re.compile(r"\b([0-9a-f]+)\s+<")


def run(*command):
    return subprocess.run(command, check=True, text=True, capture_output=True).stdout


def without_parameters(name):
    """A demangled C++ name without its parameter list (and const)."""
    if name.endswith(" const"):
        name = name[: -len(" const")]
    if not name.endswith(")"):
        return name
    depth = 0
    for index in range(len(name) - 1, -1, -1):
        depth += {")": 1, "(": -1}.get(name[index], 0)
        if depth == 0:
            return name[:index]
    return name


class Symbols:
    """Sized symbols of the image. The raw (mangled) names are unique and
    name veneers; the demangled ones name functions in messages and find
    the listed functions."""

    def __init__(self, nm, elf):
        demangled = {}
        for line in run(nm, "-S", "-C", elf).splitlines():
            parts = line.split(None, 3)
            if len(parts) == 4:
                demangled.setdefault((int(parts[0], 16), int(parts[1], 16)), parts[3])
        self.by_name = {}
        self.by_plain = {}
        self.display = {}
        spans = []
        for line in run(nm, "-S", elf).splitlines():
            parts = line.split(None, 3)
            if len(parts) != 4:
                continue
            address, size, name = int(parts[0], 16), int(parts[1], 16), parts[3]
            self.by_name.setdefault(name, (address, size))
            shown = demangled.get((address, size), name)
            self.display.setdefault(address, shown)
            plain = without_parameters(shown)
            if (address, size) not in self.by_plain.setdefault(plain, []):
                self.by_plain[plain].append((address, size))
            if size:
                spans.append((address, size, name))
        spans.sort()
        self.spans = spans
        self.starts = [span[0] for span in spans]

    def containing(self, address):
        index = bisect.bisect_right(self.starts, address) - 1
        if index >= 0:
            start, size, name = self.spans[index]
            if start <= address < start + size:
                return name, start, size
        return None

    def plain(self, address):
        return without_parameters(self.display.get(address, f"0x{address:08x}"))


def in_sram(address):
    return SRAM[0] <= address < SRAM[1]


def veneer_target(name):
    """The symbol a linker veneer "__<symbol>_veneer" jumps to, or None."""
    match = re.fullmatch(r"__(.+)_veneer", name)
    return match.group(1) if match else None


def branch_targets(objdump, elf, start, size):
    listing = run(objdump, "-d", "--no-show-raw-insn", "-C",
                  f"--start-address=0x{start:x}",
                  f"--stop-address=0x{start + size:x}", elf)
    for line in listing.splitlines():
        parsed = LINE.match(line)
        if parsed is None:
            continue
        mnemonic, operands = parsed.group(2), parsed.group(3)
        if not (ARM_BRANCH.match(mnemonic) or RISCV_BRANCH.match(mnemonic)):
            continue
        found = COMMENT_TARGET.search(operands) or OPERAND_TARGET.search(operands)
        if found is not None:
            yield int(found.group(1), 16)


def check_path(root, start, size, allowed, symbols, objdump, elf, atomic_helpers):
    if not in_sram(start):
        return [f"{root}: at 0x{start:08x}, not in SRAM"]
    problems = []
    pending, seen = [(start, size)], set()
    while pending:
        start, size = pending.pop()
        if start in seen:
            continue  # functions are told apart by address, not by name
        seen.add(start)
        name = symbols.plain(start)
        where = root if name == root else f"{root} -> {name}"
        for target in branch_targets(objdump, elf, start, size):
            if start <= target < start + size:
                continue  # a branch inside the function
            found = symbols.containing(target)
            if found is None:
                problems.append(f"{where}: branches to 0x{target:08x}, no known function")
                continue
            callee, callee_start, callee_size = found
            via = veneer_target(callee)
            if via is not None:
                if via not in symbols.by_name:
                    problems.append(f"{where}: calls {callee}, a veneer to an unknown symbol")
                    continue
                callee_start, callee_size = symbols.by_name[via]
            if in_sram(callee_start):
                pending.append((callee_start, callee_size))
                continue
            shown = symbols.plain(callee_start)
            if shown in allowed or (atomic_helpers and atomic_helpers.match(shown)):
                continue
            problems.append(f"{where}: calls {shown} at 0x{callee_start:08x} in flash")
    return problems


def check(elf, nm, objdump, atomic_helpers):
    symbols = Symbols(nm, elf)
    problems = [f"{name}: missing from the image"
                for name in REQUIRED if name not in symbols.by_plain]
    for root, allowed in FUNCTIONS.items():
        for start, size in symbols.by_plain.get(root, []):
            problems += check_path(root, start, size, allowed, symbols, objdump, elf,
                                   atomic_helpers)
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("elf")
    parser.add_argument("--nm", required=True)
    parser.add_argument("--objdump", required=True)
    parser.add_argument("--atomic-helpers", choices=sorted(ATOMIC_HELPERS), default="none",
                        help="atomic helper calls the core needs: all (RP2040), "
                             "wide (8-byte, RP2350) or none")
    args = parser.parse_args()
    problems = check(args.elf, args.nm, args.objdump, ATOMIC_HELPERS[args.atomic_helpers])
    for problem in problems:
        print(f"[JH-RP-RAM] {problem}", file=sys.stderr)
    if problems:
        return 1
    print(f"RP RAM paths: {args.elf} PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
