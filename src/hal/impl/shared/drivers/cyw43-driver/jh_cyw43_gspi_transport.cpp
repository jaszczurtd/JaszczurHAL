#include "jh_cyw43_gspi_transport.h"

#include <limits.h>
#include <string.h>

namespace {

constexpr uint32_t kBackplaneFunction = 1u;
constexpr uint32_t kPowerOffDelayMs = 20u;
constexpr uint32_t kPowerOnDelayMs = 250u;

bool ops_valid(const jh_cyw43_gspi_platform_ops_t *ops) {
  return ops != nullptr && ops->initialize != nullptr &&
         ops->deinitialize != nullptr && ops->set_power != nullptr &&
         ops->release_data != nullptr && ops->transfer != nullptr &&
         ops->host_wake_attach != nullptr && ops->host_wake_detach != nullptr &&
         ops->host_wake_mask != nullptr && ops->host_wake_rearm != nullptr &&
         ops->delay_ms != nullptr;
}

uint32_t pack_command(bool write, bool increment, uint32_t function,
                      uint32_t address, uint32_t size) {
  return ((uint32_t)write << 31u) | ((uint32_t)increment << 30u) |
         (function << 28u) | ((address & 0x1FFFFu) << 11u) | size;
}

void put_boot_word(uint8_t output[4], uint32_t value) {
  output[0] = (uint8_t)(value >> 8u);
  output[1] = (uint8_t)value;
  output[2] = (uint8_t)(value >> 24u);
  output[3] = (uint8_t)(value >> 16u);
}

uint32_t get_boot_word(const uint8_t input[4]) {
  return (uint32_t)input[1] | ((uint32_t)input[0] << 8u) |
         ((uint32_t)input[3] << 16u) | ((uint32_t)input[2] << 24u);
}

void put_le32(uint8_t output[4], uint32_t value) {
  output[0] = (uint8_t)value;
  output[1] = (uint8_t)(value >> 8u);
  output[2] = (uint8_t)(value >> 16u);
  output[3] = (uint8_t)(value >> 24u);
}

void host_wake_callback(void *callback_context) {
  auto *transport = static_cast<jh_cyw43_gspi_transport_t *>(callback_context);
  if (transport == nullptr || !transport->initialized) {
    return;
  }
  transport->host_wake_pending = true;
  ++transport->stats.host_wake_irqs;
}

hal_status_t rearm_host_wake(jh_cyw43_gspi_transport_t *transport) {
  if (!transport->host_wake_attached ||
      transport->host_wake_suspend_depth != 0u) {
    return HAL_OK;
  }
  bool asserted = false;
  const hal_status_t status =
      transport->ops->host_wake_rearm(transport->platform_context, &asserted);
  if (status == HAL_OK && asserted) {
    transport->host_wake_pending = true;
    ++transport->stats.host_wake_levels;
  }
  return status;
}

} // namespace

extern "C" hal_status_t
jh_cyw43_gspi_transport_init(jh_cyw43_gspi_transport_t *transport,
                             const jh_cyw43_gspi_platform_ops_t *ops,
                             void *platform_context,
                             size_t max_transaction_bytes) {
  if (transport == nullptr || !ops_valid(ops)) {
    return HAL_EINVAL;
  }
  if (transport->initialized) {
    return HAL_EEXIST;
  }
  if (max_transaction_bytes < 8u || (max_transaction_bytes & 3u) != 0u) {
    return HAL_ECONFIG;
  }

  memset(transport, 0, sizeof(*transport));
  transport->ops = ops;
  transport->platform_context = platform_context;
  transport->max_transaction_bytes = max_transaction_bytes;
  const hal_status_t status = ops->initialize(platform_context);
  if (status != HAL_OK) {
    memset(transport, 0, sizeof(*transport));
    return status;
  }
  transport->initialized = true;
  return HAL_OK;
}

extern "C" hal_status_t
jh_cyw43_gspi_transport_deinit(jh_cyw43_gspi_transport_t *transport) {
  if (transport == nullptr) {
    return HAL_EINVAL;
  }
  if (!transport->initialized) {
    return HAL_EUNINIT;
  }
  hal_status_t status = HAL_OK;
  if (transport->host_wake_attached) {
    status = jh_cyw43_gspi_host_wake_detach(transport);
  }
  const hal_status_t power_status =
      transport->ops->set_power(transport->platform_context, false);
  const hal_status_t deinit_status =
      transport->ops->deinitialize(transport->platform_context);
  if (status == HAL_OK) {
    status = power_status;
  }
  if (status == HAL_OK) {
    status = deinit_status;
  }
  memset(transport, 0, sizeof(*transport));
  return status;
}

extern "C" hal_status_t
jh_cyw43_gspi_power_off(jh_cyw43_gspi_transport_t *transport) {
  if (transport == nullptr) {
    return HAL_EINVAL;
  }
  if (!transport->initialized) {
    return HAL_EUNINIT;
  }
  if (transport->host_wake_attached) {
    transport->ops->host_wake_mask(transport->platform_context);
  }
  const hal_status_t status =
      transport->ops->set_power(transport->platform_context, false);
  if (status == HAL_OK) {
    transport->powered = false;
  }
  return status;
}

extern "C" hal_status_t
jh_cyw43_gspi_power_cycle(jh_cyw43_gspi_transport_t *transport) {
  hal_status_t status = jh_cyw43_gspi_power_off(transport);
  if (status != HAL_OK) {
    return status;
  }
  transport->ops->delay_ms(transport->platform_context, kPowerOffDelayMs);
  status = transport->ops->set_power(transport->platform_context, true);
  if (status != HAL_OK) {
    return status;
  }
  transport->ops->delay_ms(transport->platform_context, kPowerOnDelayMs);
  status = transport->ops->release_data(transport->platform_context);
  if (status != HAL_OK) {
    (void)transport->ops->set_power(transport->platform_context, false);
    return status;
  }
  transport->powered = true;
  ++transport->stats.cold_power_cycles;
  return rearm_host_wake(transport);
}

extern "C" hal_status_t
jh_cyw43_gspi_transfer(jh_cyw43_gspi_transport_t *transport, const uint8_t *tx,
                       size_t tx_length, uint8_t *rx, size_t rx_length) {
  if (transport == nullptr || tx == nullptr || tx_length == 0u ||
      (tx_length & 3u) != 0u || (rx_length & 3u) != 0u ||
      (rx_length != 0u && rx == nullptr)) {
    return HAL_EINVAL;
  }
  if (!transport->initialized) {
    return HAL_EUNINIT;
  }
  if (!transport->powered) {
    return HAL_ESTATE;
  }
  if (tx_length > transport->max_transaction_bytes ||
      rx_length > transport->max_transaction_bytes - tx_length) {
    return HAL_EINVAL;
  }

  hal_status_t status = jh_cyw43_gspi_host_wake_suspend(transport);
  if (status != HAL_OK) {
    return status;
  }
  status = transport->ops->transfer(transport->platform_context, tx, tx_length,
                                    rx, rx_length);
  const hal_status_t resume_status = jh_cyw43_gspi_host_wake_resume(transport);
  if (status == HAL_OK) {
    status = resume_status;
  }
  if (status != HAL_OK) {
    ++transport->stats.transfer_errors;
    return status;
  }
  ++transport->stats.transactions;
  transport->stats.transmitted_bytes += (uint32_t)tx_length;
  transport->stats.received_bytes += (uint32_t)rx_length;
  return HAL_OK;
}

extern "C" hal_status_t
jh_cyw43_gspi_boot_read_u32(jh_cyw43_gspi_transport_t *transport,
                            uint32_t function, uint32_t address,
                            uint32_t *value, uint8_t raw_response[4]) {
  if (transport == nullptr || value == nullptr) {
    return HAL_EINVAL;
  }
  uint8_t command[4];
  uint8_t response[4];
  put_boot_word(command, pack_command(false, true, function, address, 4u));
  const hal_status_t status = jh_cyw43_gspi_transfer(
      transport, command, sizeof(command), response, sizeof(response));
  if (status != HAL_OK) {
    return status;
  }
  if (raw_response != nullptr) {
    memcpy(raw_response, response, sizeof(response));
  }
  *value = get_boot_word(response);
  return HAL_OK;
}

extern "C" hal_status_t
jh_cyw43_gspi_boot_write_u32(jh_cyw43_gspi_transport_t *transport,
                             uint32_t function, uint32_t address,
                             uint32_t value) {
  if (transport == nullptr) {
    return HAL_EINVAL;
  }
  uint8_t transfer[8];
  put_boot_word(&transfer[0], pack_command(true, true, function, address, 4u));
  put_boot_word(&transfer[4], value);
  return jh_cyw43_gspi_transfer(transport, transfer, sizeof(transfer), nullptr,
                                0u);
}

extern "C" hal_status_t jh_cyw43_gspi_read(jh_cyw43_gspi_transport_t *transport,
                                           uint32_t function, uint32_t address,
                                           void *dest, size_t length) {
  if (transport == nullptr || dest == nullptr || length == 0u ||
      length > JH_CYW43_GSPI_DIAGNOSTIC_MAX_PAYLOAD) {
    return HAL_EINVAL;
  }
  const size_t aligned_length = (length + 3u) & ~3u;
  const size_t padding = function == kBackplaneFunction
                             ? JH_CYW43_GSPI_BACKPLANE_READ_PADDING
                             : 0u;
  put_le32(transport->transfer_buffer,
           pack_command(false, true, function, address, (uint32_t)length));
  const hal_status_t status =
      jh_cyw43_gspi_transfer(transport, transport->transfer_buffer, 4u,
                             transport->read_buffer, aligned_length + padding);
  if (status == HAL_OK) {
    memcpy(dest, &transport->read_buffer[padding], length);
  }
  return status;
}

extern "C" hal_status_t
jh_cyw43_gspi_write(jh_cyw43_gspi_transport_t *transport, uint32_t function,
                    uint32_t address, const void *source, size_t length) {
  if (transport == nullptr || source == nullptr || length == 0u ||
      length > JH_CYW43_GSPI_DIAGNOSTIC_MAX_PAYLOAD) {
    return HAL_EINVAL;
  }
  const size_t aligned_length = (length + 3u) & ~3u;
  put_le32(transport->transfer_buffer,
           pack_command(true, true, function, address, (uint32_t)length));
  memcpy(&transport->transfer_buffer[4], source, length);
  if (aligned_length != length) {
    memset(&transport->transfer_buffer[4u + length], 0,
           aligned_length - length);
  }
  return jh_cyw43_gspi_transfer(transport, transport->transfer_buffer,
                                4u + aligned_length, nullptr, 0u);
}

extern "C" hal_status_t
jh_cyw43_gspi_host_wake_attach(jh_cyw43_gspi_transport_t *transport) {
  if (transport == nullptr) {
    return HAL_EINVAL;
  }
  if (!transport->initialized) {
    return HAL_EUNINIT;
  }
  if (transport->host_wake_attached) {
    return HAL_OK;
  }
  transport->host_wake_pending = false;
  const hal_status_t status = transport->ops->host_wake_attach(
      transport->platform_context, host_wake_callback, transport);
  if (status != HAL_OK) {
    return status;
  }
  transport->host_wake_attached = true;
  const hal_status_t rearm_status = rearm_host_wake(transport);
  if (rearm_status != HAL_OK) {
    (void)transport->ops->host_wake_detach(transport->platform_context);
    transport->host_wake_attached = false;
  }
  return rearm_status;
}

extern "C" hal_status_t
jh_cyw43_gspi_host_wake_detach(jh_cyw43_gspi_transport_t *transport) {
  if (transport == nullptr) {
    return HAL_EINVAL;
  }
  if (!transport->initialized) {
    return HAL_EUNINIT;
  }
  if (!transport->host_wake_attached) {
    return HAL_OK;
  }
  transport->ops->host_wake_mask(transport->platform_context);
  const hal_status_t status =
      transport->ops->host_wake_detach(transport->platform_context);
  if (status == HAL_OK) {
    transport->host_wake_attached = false;
    transport->host_wake_pending = false;
    transport->host_wake_suspend_depth = 0u;
  }
  return status;
}

extern "C" hal_status_t
jh_cyw43_gspi_host_wake_suspend(jh_cyw43_gspi_transport_t *transport) {
  if (transport == nullptr) {
    return HAL_EINVAL;
  }
  if (!transport->initialized) {
    return HAL_EUNINIT;
  }
  if (transport->host_wake_suspend_depth == UINT32_MAX) {
    return HAL_EBUSY;
  }
  if (transport->host_wake_suspend_depth++ == 0u &&
      transport->host_wake_attached) {
    transport->ops->host_wake_mask(transport->platform_context);
  }
  return HAL_OK;
}

extern "C" hal_status_t
jh_cyw43_gspi_host_wake_resume(jh_cyw43_gspi_transport_t *transport) {
  if (transport == nullptr) {
    return HAL_EINVAL;
  }
  if (!transport->initialized) {
    return HAL_EUNINIT;
  }
  if (transport->host_wake_suspend_depth == 0u) {
    return HAL_ESTATE;
  }
  --transport->host_wake_suspend_depth;
  return rearm_host_wake(transport);
}

extern "C" bool
jh_cyw43_gspi_host_wake_pending(const jh_cyw43_gspi_transport_t *transport) {
  return transport != nullptr && transport->initialized &&
         transport->host_wake_pending;
}

extern "C" hal_status_t
jh_cyw43_gspi_host_wake_clear(jh_cyw43_gspi_transport_t *transport) {
  if (transport == nullptr) {
    return HAL_EINVAL;
  }
  if (!transport->initialized) {
    return HAL_EUNINIT;
  }
  if (!transport->host_wake_attached) {
    return HAL_ESTATE;
  }
  transport->host_wake_pending = false;
  return rearm_host_wake(transport);
}

extern "C" hal_status_t
jh_cyw43_gspi_note_recovery(jh_cyw43_gspi_transport_t *transport) {
  if (transport == nullptr) {
    return HAL_EINVAL;
  }
  if (!transport->initialized) {
    return HAL_EUNINIT;
  }
  ++transport->stats.recoveries;
  return HAL_OK;
}

extern "C" hal_status_t
jh_cyw43_gspi_get_stats(const jh_cyw43_gspi_transport_t *transport,
                        jh_cyw43_gspi_stats_t *out_stats) {
  if (transport == nullptr || out_stats == nullptr) {
    return HAL_EINVAL;
  }
  if (!transport->initialized) {
    return HAL_EUNINIT;
  }
  *out_stats = transport->stats;
  return HAL_OK;
}
