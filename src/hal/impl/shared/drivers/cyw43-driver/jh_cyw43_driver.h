#ifndef JASZCZURHAL_IMPL_SHARED_FRAMEWORKS_CYW43_DRIVER_H
#define JASZCZURHAL_IMPL_SHARED_FRAMEWORKS_CYW43_DRIVER_H

#include "../../../../hal_status.h"
#include "jh_cyw43_gspi_transport.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Stable include boundary for the pinned CYW43 radio driver. Platform code
 * must include this wrapper instead of resolving cyw43.h from its carrier.
 */
#ifdef __cplusplus
extern "C" {
#endif

#if defined(HAL_CYW43_BUS_PICO_PIO) || defined(HAL_CYW43_BUS_STM32_GSPI)
#ifndef CYW43_CONFIG_FILE
#define CYW43_CONFIG_FILE "../../cyw43_configport.h"
#define JH_CYW43_DRIVER_UNDEF_CONFIG_FILE
#endif
#include "vendor/src/cyw43.h"
#include "vendor/src/cyw43_country.h"
#ifdef JH_CYW43_DRIVER_UNDEF_CONFIG_FILE
#undef JH_CYW43_DRIVER_UNDEF_CONFIG_FILE
#undef CYW43_CONFIG_FILE
#endif
#endif

typedef enum {
  JH_CYW43_DRIVER_STAGE_NONE = 0,
  JH_CYW43_DRIVER_STAGE_LOW_LEVEL,
  JH_CYW43_DRIVER_STAGE_BUS,
  JH_CYW43_DRIVER_STAGE_READY,
} jh_cyw43_driver_stage_t;

typedef struct {
  jh_cyw43_driver_stage_t stage;
  int cyw43_error;
  uint32_t identification;
  uint32_t bus_control;
  uint32_t generation;
  bool resources_verified;
  bool f2_ready;
} jh_cyw43_driver_result_t;

/** Start the single vendored CYW43 low-level driver instance on a gSPI bus. */
hal_status_t jh_cyw43_driver_start(jh_cyw43_gspi_transport_t *transport,
                                   const uint8_t mac[6],
                                   jh_cyw43_driver_result_t *result);

/** Tear down the active low-level instance without destroying the transport. */
hal_status_t jh_cyw43_driver_stop(void);

/** Perform a bounded driver-owned reset, resource reload and F2 bring-up. */
hal_status_t jh_cyw43_driver_restart(jh_cyw43_driver_result_t *result);

bool jh_cyw43_driver_is_ready(void);
const char *jh_cyw43_driver_stage_string(jh_cyw43_driver_stage_t stage);

#if defined(HAL_CYW43_BUS_PICO_PIO) || defined(HAL_CYW43_BUS_STM32_GSPI)
/** Internal bridge for the following lwIP integration stage. */
cyw43_ll_t *jh_cyw43_driver_low_level(void);
jh_cyw43_gspi_transport_t *jh_cyw43_driver_transport_internal(void);
uint32_t jh_cyw43_driver_generation_internal(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
