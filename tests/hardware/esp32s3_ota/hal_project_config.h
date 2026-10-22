#pragma once

/* Targets and wiring come from the assemblies in device_tree.json. */

/* Variants: id, description and the definitions each one adds to this
 * configuration. The verifier builds both images and moves the board
 * between them over the air. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(IMAGE_B, "Second image the verifier uploads over the air",                 \
    JH_OTA_TEST_IMAGE_B = 1)

/* Raw ESP-IDF application OTA into the inactive app slot, with trial boot,
 * confirmation and rollback. */
#define HAL_ENABLE_WIFI 1
#define HAL_ENABLE_OTA 1

#define HAL_FREERTOS_TASK0_CORE 0
#define HAL_FREERTOS_TASK0_STACK 8192u
