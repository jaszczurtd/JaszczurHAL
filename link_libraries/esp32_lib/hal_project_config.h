#pragma once

/* Default ESP-IDF library profile: core runtime only. Pass -D HAL_ENABLE_*
 * or --all-features to scripts/build_esp32_lib.sh, or point -p at a project
 * configuration directory, to widen the archive. ESP-IDF always provides
 * FreeRTOS, so the target adds HAL_ENABLE_FREERTOS itself. */
