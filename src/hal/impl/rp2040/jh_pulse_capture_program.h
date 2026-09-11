#pragma once
#include <stdint.h>

/* PIO v0 instructions. Kept here so the instruction-level host test executes
 * exactly the program loaded by the backend. No sideset; wrap 0..6.
 */
static const uint16_t jh_rp_capture_instructions[] = {
    0x0241, /* high: jmp x-- high_check [2] */
    0x04c0, /* high_check: jmp pin high [4] */
    0xa0c1, /* mov isr, x */
    0x8000, /* push noblock */
    0x0245, /* low: jmp x-- low_check [2] */
    0x02c0, /* low_check: jmp pin high [2] */
    0x0104, /* jmp low [1] */
};
