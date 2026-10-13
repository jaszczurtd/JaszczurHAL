/*
 * Compiles a backend's FreeRTOSConfig.h the way the kernel sources do and
 * checks that it applies the tunables of the project configuration in
 * fixtures/freertos_config/project.
 */
#include FREERTOS_CONFIG_UNDER_TEST

_Static_assert(
    configTOTAL_HEAP_SIZE == 12345u,
    "FreeRTOSConfig.h ignores HAL_FREERTOS_HEAP_SIZE of the project");
#if defined(PROBE_CORE_COUNT)
_Static_assert(
    configNUMBER_OF_CORES == PROBE_CORE_COUNT,
    "FreeRTOSConfig.h ignores HAL_FREERTOS_CORE_COUNT of the project");
#endif

int main(void) { return 0; }
