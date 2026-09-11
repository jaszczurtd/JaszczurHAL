#include "hal/core/hal_array.h"
#include "hal/impl/rp2040/jh_pulse_capture_program.h"
#include "utils/unity.h"
#include <stdint.h>

void setUp(void) {}
void tearDown(void) {}

/* Execute the production instruction words with PIO v0 JMP/MOV/PUSH rules.
 * Time is independent of X. This checks the branch/delay cycle accounting,
 * including X=0 fall-through, instead of reproducing the frequency formula.
 */
static void check_wave(uint32_t period, uint32_t high, uint32_t initial_x) {
  uint32_t pc = 0, x = initial_x, isr = 0;
  uint64_t cycles = 0, previous_cycles = 0;
  uint32_t previous_x = 0, samples = 0;
  for (unsigned step = 0; step < 500000; ++step) {
    const uint16_t instruction = jh_rp_capture_instructions[pc];
    uint32_t next = (pc + 1U) % COUNTOF(jh_rp_capture_instructions);
    const uint32_t opcode = instruction >> 13;
    if (opcode == 0U) {
      const uint32_t condition = (instruction >> 5) & 7U;
      bool jump = condition == 0U;
      if (condition == 2U) {
        jump = x != 0U;
        --x;
      }
      if (condition == 6U)
        jump = cycles % period < high;
      TEST_ASSERT_TRUE(condition == 0U || condition == 2U || condition == 6U);
      if (jump)
        next = instruction & 31U;
    } else if (opcode == 5U) {
      TEST_ASSERT_EQUAL_HEX16(0xa0c1, instruction); /* MOV ISR, X */
      isr = x;
    } else if (opcode == 4U) {
      TEST_ASSERT_EQUAL_HEX16(0x8000, instruction); /* PUSH NOBLOCK */
      if (samples > 0U) {
        TEST_ASSERT_EQUAL_UINT64(cycles - previous_cycles,
                                 (uint64_t)(previous_x - isr) * 8U);
        TEST_ASSERT_UINT32_WITHIN(8U, period, cycles - previous_cycles);
      }
      previous_x = isr;
      previous_cycles = cycles;
      ++samples;
    } else {
      TEST_FAIL_MESSAGE("Unexpected instruction in capture program");
    }
    cycles += 1U + ((instruction >> 8) & 31U);
    pc = next;
  }
  TEST_ASSERT_GREATER_THAN_UINT32(128U, samples);
}

static void asymmetric_duty_and_phase_quantization(void) {
  const uint32_t periods[] = {1250U, 3379U, 5003U};
  for (uint32_t period : periods) {
    check_wave(period, 125U, UINT32_MAX);
    check_wave(period, period / 2U, UINT32_MAX);
    check_wave(period, period - 125U, UINT32_MAX);
  }
}

static void counter_wrap_preserves_instruction_path(void) {
  check_wave(997U, 400U, 5U);
  check_wave(1250U, 500U, 0U);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(asymmetric_duty_and_phase_quantization);
  RUN_TEST(counter_wrap_preserves_instruction_path);
  return UNITY_END();
}
