#pragma once

#include "../../../../hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JH_CYW43_GSPI_DIAGNOSTIC_MAX_PAYLOAD 64u
#define JH_CYW43_GSPI_BACKPLANE_READ_PADDING 16u

typedef void (*jh_cyw43_gspi_host_wake_callback_t)(void *callback_context);

/** Target callbacks for a half-duplex, shared-data-line CYW43 gSPI bus. */
typedef struct {
  hal_status_t (*initialize)(void *context);
  hal_status_t (*deinitialize)(void *context);
  hal_status_t (*set_power)(void *context, bool enabled);
  hal_status_t (*release_data)(void *context);
  hal_status_t (*transfer)(void *context, const uint8_t *tx, size_t tx_length,
                           uint8_t *rx, size_t rx_length);
  hal_status_t (*host_wake_attach)(void *context,
                                   jh_cyw43_gspi_host_wake_callback_t callback,
                                   void *callback_context);
  hal_status_t (*host_wake_detach)(void *context);
  void (*host_wake_mask)(void *context);
  hal_status_t (*host_wake_rearm)(void *context, bool *asserted);
  void (*delay_ms)(void *context, uint32_t delay_ms);
} jh_cyw43_gspi_platform_ops_t;

typedef struct {
  uint32_t transactions;
  uint32_t transmitted_bytes;
  uint32_t received_bytes;
  uint32_t transfer_errors;
  uint32_t recoveries;
  volatile uint32_t host_wake_irqs;
  uint32_t host_wake_levels;
  uint32_t cold_power_cycles;
} jh_cyw43_gspi_stats_t;

typedef struct {
  const jh_cyw43_gspi_platform_ops_t *ops;
  void *platform_context;
  size_t max_transaction_bytes;
  jh_cyw43_gspi_stats_t stats;
  volatile bool host_wake_pending;
  bool initialized;
  bool powered;
  bool host_wake_attached;
  uint32_t host_wake_suspend_depth;
  uint8_t transfer_buffer[4u + JH_CYW43_GSPI_DIAGNOSTIC_MAX_PAYLOAD];
  uint8_t read_buffer[JH_CYW43_GSPI_BACKPLANE_READ_PADDING +
                      JH_CYW43_GSPI_DIAGNOSTIC_MAX_PAYLOAD];
} jh_cyw43_gspi_transport_t;

hal_status_t
jh_cyw43_gspi_transport_init(jh_cyw43_gspi_transport_t *transport,
                             const jh_cyw43_gspi_platform_ops_t *ops,
                             void *platform_context,
                             size_t max_transaction_bytes);
hal_status_t
jh_cyw43_gspi_transport_deinit(jh_cyw43_gspi_transport_t *transport);

hal_status_t jh_cyw43_gspi_power_off(jh_cyw43_gspi_transport_t *transport);
hal_status_t jh_cyw43_gspi_power_cycle(jh_cyw43_gspi_transport_t *transport);
hal_status_t jh_cyw43_gspi_transfer(jh_cyw43_gspi_transport_t *transport,
                                    const uint8_t *tx, size_t tx_length,
                                    uint8_t *rx, size_t rx_length);

hal_status_t jh_cyw43_gspi_boot_read_u32(jh_cyw43_gspi_transport_t *transport,
                                         uint32_t function, uint32_t address,
                                         uint32_t *value,
                                         uint8_t raw_response[4]);
hal_status_t jh_cyw43_gspi_boot_write_u32(jh_cyw43_gspi_transport_t *transport,
                                          uint32_t function, uint32_t address,
                                          uint32_t value);
hal_status_t jh_cyw43_gspi_read(jh_cyw43_gspi_transport_t *transport,
                                uint32_t function, uint32_t address, void *dest,
                                size_t length);
hal_status_t jh_cyw43_gspi_write(jh_cyw43_gspi_transport_t *transport,
                                 uint32_t function, uint32_t address,
                                 const void *source, size_t length);

hal_status_t
jh_cyw43_gspi_host_wake_attach(jh_cyw43_gspi_transport_t *transport);
hal_status_t
jh_cyw43_gspi_host_wake_detach(jh_cyw43_gspi_transport_t *transport);
hal_status_t
jh_cyw43_gspi_host_wake_suspend(jh_cyw43_gspi_transport_t *transport);
hal_status_t
jh_cyw43_gspi_host_wake_resume(jh_cyw43_gspi_transport_t *transport);
bool jh_cyw43_gspi_host_wake_pending(
    const jh_cyw43_gspi_transport_t *transport);
hal_status_t
jh_cyw43_gspi_host_wake_refresh(jh_cyw43_gspi_transport_t *transport);
hal_status_t
jh_cyw43_gspi_host_wake_clear(jh_cyw43_gspi_transport_t *transport);

hal_status_t jh_cyw43_gspi_note_recovery(jh_cyw43_gspi_transport_t *transport);
hal_status_t jh_cyw43_gspi_get_stats(const jh_cyw43_gspi_transport_t *transport,
                                     jh_cyw43_gspi_stats_t *out_stats);

#ifdef __cplusplus
}
#endif
