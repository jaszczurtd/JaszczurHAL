# doc/datasheets

Reference datasheets (PDF) for the chips that JaszczurHAL drives, plus the
testing methodology those datasheets exist to support.

PDFs dropped here are the **source of truth** for "ground-truth" host tests:
the exact bytes a device expects on the wire, and the exact meaning of every
register/bit, come from these documents - never from running the driver and
trusting whatever it happens to produce.

## Why this folder exists - lessons from the 2026-06-08 audit

A review + audit of the shared drivers surfaced two real hardware-correctness
bugs that the existing test suite did **not** catch:

- **PGA2311** sent the SPI volume word as `{left, right}`; the chip latches the
  **right** channel byte first (`{right, left}`) - channels were swapped.
- **PCF8563** treated the century bit as `C=1 -> 20xx`; the NXP datasheet
  (Table 13) defines `C=0 -> 20xx, C=1 -> 19xx` - inverted in both read and write.

Both passed CI because the tests were **self-consistent round-trips**: they set
a value through the driver and read it back through the same driver. A driver
that is wrong in a symmetric way (swapped pair, inverted polarity) round-trips
perfectly and the test stays green. The mistake never touched a fact that came
from outside the driver.

A third, lower-severity issue (MCP2515 `mcp2515_init` swallowing a baud-rate
failure and returning `OK`) had no test at all because the device driver's
register path is only reachable on real targets, and the host suite exercised
the HAL facade against a mock backend instead.

## Methodology: ground-truth, not round-trip

Every I2C/SPI **device driver** gets a host test that asserts against
datasheet-derived facts, not against the driver's own output:

1. **Write path** - for representative API calls, assert the **exact bytes on
   the bus** (via `hal_mock_i2c_get_write_frame` / `hal_mock_spi` tail). The
   expected bytes are taken from the datasheet (cite the table/section in a
   comment), not produced by running the code.
2. **Read path** - inject **known raw register bytes** (`hal_mock_i2c_inject_rx`)
   and assert the decoded value. The injected bytes are a worked datasheet
   example.
3. **Round-trip** (set -> get through the same driver) is allowed only as an
   *additional* check, **never** as the only test for a register encoding.

Pattern: small golden-vector tables with a datasheet citation, e.g.

```c
/* DS3231 datasheet (DS3231 Rev., "TIMEKEEPING REGISTERS"): month reg 0x05,
 * bit7 = century; hours reg 0x02, bit6 = 12/24 mode, bit5 = AM/PM. */
static const struct { byte reg; bool h12; bool pm; byte hour; } kHourVectors[] = {
    {0x23, false, false, 23},  /* 24h: BCD 23 */
    {0x67, true,  true,   7},  /* 12h PM: BCD 7 | 0x40 | 0x20 */
    {0x51, true,  false, 11},  /* 12h AM: BCD 11 | 0x40 */
};
```

## Target-only drivers must be compiled into the host suite

Drivers guarded by `HAL_ENABLE_*` (e.g. `ds3231.cpp`, `pcf8563.cpp`) are not in
`hal_mock`; without an explicit test target their register logic is invisible
to host coverage. Add the source to the test executable in `tests/CMakeLists.txt`
the same way `test_mcp9600_driver` / `test_pcf8563_driver` do.

## Status / backlog

| Driver  | Ground-truth host test | Notes |
|---------|------------------------|-------|
| PGA2311 | (ok) wire order fixed + asserted | `test_hal_pga2311` |
| PCF8563 | (ok) century bit, both directions | `test_pcf8563_driver` |
| DS3231  | (ok) century / 12-24h / BCD / temp | `test_ds3231_driver` |
| MCP2515 | (~) only `set_gpo` - ID std/ext packing, RTR/DLC untested | Tier 2 |
| DS18B20 | (~) scratchpad sign-extend known-answer vectors | Tier 3 |
| digipot | (ok) already wire-level | `test_hal_digipot` (reference pattern) |

Process guard worth adding: a CI check that every datasheet-backed driver
directory under `src/hal/impl/shared/drivers/` has a corresponding `tests/`
file, so a driver with zero host coverage can't slip in unnoticed.

## Naming

Suggested: `<part-number>.pdf` (e.g. `PCF8563.pdf`, `DS3231.pdf`, `MCP2515.pdf`,
`PGA2311.pdf`) so a test's datasheet citation maps to a file by name.
