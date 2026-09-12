#include "hal/core/hal_compiler.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t marker;
} atomic_pointer_probe_t;

static uint32_t *select_counter(uint32_t *counter, uint32_t *calls) {
  ++*calls;
  return counter;
}

int compiler_atomic_c_probe(void) {
  bool flag = false;
  HAL_ATOMIC_STORE(&flag, true, HAL_ATOMIC_RELEASE);
  if (!HAL_ATOMIC_LOAD(&flag, HAL_ATOMIC_ACQUIRE) ||
      !HAL_ATOMIC_EXCHANGE(&flag, false, HAL_ATOMIC_ACQ_REL) || flag) {
    return 1;
  }
  if (HAL_ATOMIC_TEST_AND_SET(&flag, HAL_ATOMIC_ACQUIRE) ||
      !HAL_ATOMIC_TEST_AND_SET(&flag, HAL_ATOMIC_ACQUIRE)) {
    return 2;
  }
  HAL_ATOMIC_CLEAR(&flag, HAL_ATOMIC_RELEASE);
  if (HAL_ATOMIC_LOAD(&flag, HAL_ATOMIC_RELAXED)) {
    return 3;
  }

  uint32_t counter = 5u;
  if (HAL_ATOMIC_FETCH_ADD(&counter, 3u, HAL_ATOMIC_RELAXED) != 5u ||
      HAL_ATOMIC_FETCH_SUB(&counter, 2u, HAL_ATOMIC_ACQ_REL) != 8u ||
      HAL_ATOMIC_FETCH_OR(&counter, 0x10u, HAL_ATOMIC_SEQ_CST) != 6u ||
      counter != 22u) {
    return 4;
  }
  if (HAL_ATOMIC_ADD_FETCH(&counter, 2u, HAL_ATOMIC_RELAXED) != 24u ||
      HAL_ATOMIC_SUB_FETCH(&counter, 4u, HAL_ATOMIC_RELAXED) != 20u) {
    return 5;
  }

  uint32_t expected = 20u;
  if (!HAL_ATOMIC_COMPARE_EXCHANGE(&counter, &expected, 9u, HAL_ATOMIC_ACQ_REL,
                                   HAL_ATOMIC_ACQUIRE) ||
      counter != 9u) {
    return 6;
  }
  expected = 7u;
  if (HAL_ATOMIC_COMPARE_EXCHANGE(&counter, &expected, 11u, HAL_ATOMIC_ACQ_REL,
                                  HAL_ATOMIC_ACQUIRE) ||
      expected != 9u || counter != 9u) {
    return 7;
  }

  uint64_t wide = UINT64_C(0x1122334455667788);
  HAL_ATOMIC_STORE(&wide, UINT64_C(0x8877665544332211), HAL_ATOMIC_RELEASE);
  if (HAL_ATOMIC_LOAD(&wide, HAL_ATOMIC_ACQUIRE) !=
      UINT64_C(0x8877665544332211)) {
    return 8;
  }

  uint16_t half = UINT16_C(0x1234);
  if (HAL_ATOMIC_EXCHANGE(&half, UINT16_C(0x5678), HAL_ATOMIC_SEQ_CST) !=
          UINT16_C(0x1234) ||
      half != UINT16_C(0x5678)) {
    return 9;
  }

  atomic_pointer_probe_t first = {1u};
  atomic_pointer_probe_t second = {2u};
  atomic_pointer_probe_t *pointer = &first;
  if (HAL_ATOMIC_POINTER_LOAD(&pointer, HAL_ATOMIC_ACQUIRE) != &first) {
    return 10;
  }
  atomic_pointer_probe_t *expected_pointer = &first;
  if (!HAL_ATOMIC_POINTER_COMPARE_EXCHANGE(&pointer, &expected_pointer, &second,
                                           HAL_ATOMIC_ACQ_REL,
                                           HAL_ATOMIC_ACQUIRE) ||
      pointer != &second) {
    return 11;
  }

  uint32_t object_evaluations = 0u;
  uint32_t operand = 3u;
  counter = 1u;
  if (HAL_ATOMIC_ADD_FETCH(select_counter(&counter, &object_evaluations),
                           operand++, HAL_ATOMIC_RELAXED) != 4u ||
      object_evaluations != 1u || operand != 4u) {
    return 12;
  }

  HAL_ATOMIC_THREAD_FENCE(HAL_ATOMIC_SEQ_CST);
  return 0;
}
