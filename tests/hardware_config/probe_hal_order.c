/* The generated pair in the HAL's own include order: the hook loads the
 * jh_hardware.h that JH_HARDWARE_HEADER names before hal_project_config.h,
 * then hal_board.h loads jh_board_config.h. PROBE_TOOL_<feature> marks each
 * feature the build tools resolved from the same configuration. */
#include <hal/core/hal_config.h>

#if HAL_BOARD_HARDWARE_ID != JH_HW_HARDWARE_ID
#error "jh_board_config.h belongs to another generation"
#endif
#if HAL_BOARD_HAS_CYW43 != JH_HW_HAS_CYW43
#error "the board header does not take CYW43 from the assembly"
#endif
#if defined(HAL_ENABLE_WIFI) != defined(PROBE_TOOL_HAL_ENABLE_WIFI)
#error "the compiler and the build tools disagree on HAL_ENABLE_WIFI"
#endif
#if defined(HAL_ENABLE_NETWORK_CORE) !=                                        \
    defined(PROBE_TOOL_HAL_ENABLE_NETWORK_CORE)
#error "the compiler and the build tools disagree on HAL_ENABLE_NETWORK_CORE"
#endif
#if JH_HW_HAS_CYW43 && HAL_CYW43_PIN_WL_ON != JH_HW_NODE_WIFI_RADIO_PIN_WL_ON
#error "the CYW43 pins do not come from the module"
#endif

int main(void) { return 0; }
