/**
 * @file artifact_probe.cpp
 * @brief Build-only native Pico SDK link and artifact-generation probe.
 */

#include "hal/system/hal_system.h"

#include <pico/stdlib.h>

int main() {
  (void)hal_millis();
  while (true) {
    tight_loop_contents();
  }
}
