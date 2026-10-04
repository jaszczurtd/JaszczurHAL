/* Shared by the C link tests: collect the functions a header declares in a
 * table, so a C translation unit must link each of them by its C name. The
 * table is volatile and read through a volatile pointer, so an optimising
 * compiler keeps every reference instead of folding the check away. */
#pragma once

#include "hal/core/hal_array.h"

#include <stdbool.h>
#include <stddef.h>

typedef void (*c_link_probe_fn_t)(void);

#define C_LINK_PROBE_FN(name) ((c_link_probe_fn_t)(name))
#define C_LINK_PROBE_ALL(table) c_link_probe_all_linked((table), COUNTOF(table))

static inline bool
c_link_probe_all_linked(const volatile c_link_probe_fn_t *functions,
                        size_t count) {
  for (size_t i = 0u; i < count; i++) {
    if (functions[i] == NULL) {
      return false;
    }
  }
  return true;
}
