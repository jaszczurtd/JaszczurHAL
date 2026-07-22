#include "../../../../hal_target.h"

#include "jh_cyw43_driver.h"

#if HAL_TARGET_IS_STM32G474 && defined(HAL_CYW43_BUS_STM32_GSPI)

#if defined(HAL_CYW43_STACK_LWIP)
#include "jh_cyw43_lwip.h"
#endif

extern "C" {
#include "vendor/src/cyw43_internal.h"
#include "vendor/src/cyw43_spi.h"
#if defined(HAL_CYW43_STACK_LWIP)
#include "lwip/init.h"
#include "vendor/src/cyw43_country.h"
#endif
}

#include <hal/hal_system.h>

#include <string.h>

namespace {

constexpr uint32_t kIdentification = 0xFEEDBEADu;
constexpr uint32_t kConfiguredBusControl = 0x000200B3u;

#if defined(HAL_CYW43_STACK_LWIP)
bool s_lwip_initialized;
#else
cyw43_ll_t s_low_level{};
#endif
jh_cyw43_gspi_transport_t *s_transport;
uint8_t s_mac[6];
uint32_t s_generation;
hal_status_t s_port_status = HAL_OK;
bool s_ready;

cyw43_ll_t *low_level(void) {
#if defined(HAL_CYW43_STACK_LWIP)
  return &cyw43_state.cyw43_ll;
#else
  return &s_low_level;
#endif
}

int cyw43_error(hal_status_t status) {
  if (status == HAL_OK) {
    return 0;
  }
  if (status == HAL_EINVAL || status == HAL_ECONFIG) {
    return -CYW43_EINVAL;
  }
  if (status == HAL_ETIMEOUT) {
    return -CYW43_ETIMEDOUT;
  }
  return -CYW43_EIO;
}

void reset_result(jh_cyw43_driver_result_t *result) {
  memset(result, 0, sizeof(*result));
  result->generation = s_generation;
}

hal_status_t start_low_level(jh_cyw43_driver_result_t *result) {
  reset_result(result);
  result->stage = JH_CYW43_DRIVER_STAGE_LOW_LEVEL;
  s_port_status = HAL_OK;
  s_ready = false;

#if defined(HAL_CYW43_STACK_LWIP)
  if (!s_lwip_initialized) {
    lwip_init();
    s_lwip_initialized = true;
  }
  cyw43_init(&cyw43_state);
  /* The SPI HOST_WAKE line is the only indication that an asynchronous
   * SDPCM response is ready.  It must already be armed while wifi_set_up()
   * performs its first control ioctls, not only after those ioctls return. */
  const hal_status_t wake_status = jh_cyw43_gspi_host_wake_attach(s_transport);
  if (wake_status != HAL_OK) {
    cyw43_ll_deinit(low_level());
    return wake_status;
  }
  result->stage = JH_CYW43_DRIVER_STAGE_BUS;
  cyw43_wifi_set_up(&cyw43_state, CYW43_ITF_STA, true, CYW43_COUNTRY_POLAND);
  const int error = cyw43_poll == nullptr ? -CYW43_EIO : 0;
#else
  cyw43_ll_init(low_level(), nullptr);
  result->stage = JH_CYW43_DRIVER_STAGE_BUS;
  const int error = cyw43_ll_bus_init(low_level(), s_mac);
#endif
  result->cyw43_error = error;
  if (error != 0 || s_port_status != HAL_OK) {
#if defined(HAL_CYW43_STACK_LWIP)
    (void)jh_cyw43_gspi_host_wake_detach(s_transport);
#endif
    cyw43_ll_deinit(low_level());
    return s_port_status != HAL_OK ? s_port_status : HAL_EIO;
  }

  ++s_generation;
  s_ready = true;
  result->stage = JH_CYW43_DRIVER_STAGE_READY;
  result->identification = kIdentification;
  result->bus_control = kConfiguredBusControl;
  result->generation = s_generation;
  result->resources_verified = true;
  result->f2_ready = true;
  return HAL_OK;
}

} // namespace

extern "C" hal_status_t
jh_cyw43_driver_start(jh_cyw43_gspi_transport_t *transport,
                      const uint8_t mac[6], jh_cyw43_driver_result_t *result) {
  if (transport == nullptr || mac == nullptr || result == nullptr) {
    return HAL_EINVAL;
  }
  if (!transport->initialized) {
    return HAL_EUNINIT;
  }
  if (s_transport != nullptr) {
    return HAL_EEXIST;
  }
  s_transport = transport;
  memcpy(s_mac, mac, sizeof(s_mac));
  const hal_status_t status = start_low_level(result);
  if (status != HAL_OK) {
    s_transport = nullptr;
  }
  return status;
}

extern "C" hal_status_t jh_cyw43_driver_stop(void) {
  if (s_transport == nullptr) {
    return HAL_EUNINIT;
  }
#if defined(HAL_CYW43_STACK_LWIP)
  cyw43_deinit(&cyw43_state);
  const hal_status_t wake_status = jh_cyw43_gspi_host_wake_detach(s_transport);
#else
  cyw43_ll_deinit(low_level());
  const hal_status_t wake_status = HAL_OK;
#endif
  s_transport = nullptr;
  s_ready = false;
  return wake_status;
}

extern "C" hal_status_t
jh_cyw43_driver_restart(jh_cyw43_driver_result_t *result) {
  if (result == nullptr) {
    return HAL_EINVAL;
  }
  if (s_transport == nullptr) {
    return HAL_EUNINIT;
  }
#if defined(HAL_CYW43_STACK_LWIP)
  cyw43_deinit(&cyw43_state);
  const hal_status_t wake_status = jh_cyw43_gspi_host_wake_detach(s_transport);
  if (wake_status != HAL_OK) {
    return wake_status;
  }
#else
  cyw43_ll_deinit(low_level());
#endif
  s_ready = false;
  return start_low_level(result);
}

extern "C" bool jh_cyw43_driver_is_ready(void) { return s_ready; }

extern "C" cyw43_ll_t *jh_cyw43_driver_low_level(void) {
  return s_ready ? low_level() : nullptr;
}

extern "C" jh_cyw43_gspi_transport_t *jh_cyw43_driver_transport_internal(void) {
  return s_transport;
}

extern "C" uint32_t jh_cyw43_driver_generation_internal(void) {
  return s_generation;
}

extern "C" uint32_t jh_cyw43_port_ticks_us(void) { return hal_micros(); }
extern "C" uint32_t jh_cyw43_port_ticks_ms(void) { return hal_millis(); }
extern "C" void jh_cyw43_port_delay_us(uint32_t delay_us) {
  hal_delay_us(delay_us);
}
extern "C" void jh_cyw43_port_delay_ms(uint32_t delay_ms) {
  hal_delay_ms(delay_ms);
}
extern "C" void jh_cyw43_port_control_wait(void) {
  /* cyw43_do_ioctl() and cyw43_sdpcm_send_common() consume their own SDPCM
   * responses.  Calling the general poll here can steal a CONTROL_HEADER and
   * turn a completed ioctl into a timeout. */
  hal_delay_us(10u);
}
extern "C" void jh_cyw43_port_event_poll(void) {
#if defined(HAL_CYW43_STACK_LWIP)
  if (s_ready) {
    (void)jh_cyw43_lwip_service();
  }
#endif
}

extern "C" void cyw43_schedule_internal_poll_dispatch(void (*function)(void)) {
  (void)function;
}

extern "C" void jh_cyw43_port_get_mac(int, uint8_t mac[6]) {
  memcpy(mac, s_mac, sizeof(s_mac));
}

extern "C" void jh_cyw43_port_generate_laa_mac(int, uint8_t mac[6]) {
  uint8_t uid[HAL_DEVICE_UID_BYTES]{};
  (void)hal_get_device_uid(uid);
  mac[0] = 0x02u;
  for (size_t index = 1u; index < 6u; ++index) {
    mac[index] = uid[index - 1u];
  }
}

extern "C" void jh_cyw43_port_pin_config(int, int, int, int) {}
extern "C" void jh_cyw43_port_pin_config_irq_falling(int, int) {}
extern "C" int jh_cyw43_port_pin_read(int) {
  return s_transport != nullptr && jh_cyw43_gspi_host_wake_pending(s_transport);
}
extern "C" void jh_cyw43_port_pin_low(int) {
  if (s_transport != nullptr) {
    s_port_status = jh_cyw43_gspi_power_off(s_transport);
  }
}
extern "C" void jh_cyw43_port_pin_high(int) {
  /* cyw43_spi_reset owns the complete, timing-correct power cycle. */
}

extern "C" int cyw43_spi_init(cyw43_int_t *self) {
  if (self == nullptr || s_transport == nullptr || self->bus_data != nullptr) {
    return -CYW43_EIO;
  }
  self->bus_data = s_transport;
  return 0;
}

extern "C" void cyw43_spi_deinit(cyw43_int_t *self) {
  if (self != nullptr) {
    self->bus_data = nullptr;
  }
}

extern "C" void cyw43_spi_gpio_setup(void) {}

extern "C" void cyw43_spi_reset(void) {
  if (s_transport == nullptr) {
    s_port_status = HAL_EUNINIT;
    return;
  }
  s_port_status = jh_cyw43_gspi_power_cycle(s_transport);
}

extern "C" void cyw43_spi_set_polarity(cyw43_int_t *, int polarity) {
  if (polarity != 0) {
    s_port_status = HAL_ECONFIG;
  }
}

extern "C" int cyw43_spi_transfer(cyw43_int_t *self, const uint8_t *tx,
                                  size_t tx_length, uint8_t *rx,
                                  size_t rx_length) {
  if (self == nullptr || self->bus_data == nullptr || tx == nullptr) {
    return -CYW43_EINVAL;
  }
  auto *transport = static_cast<jh_cyw43_gspi_transport_t *>(self->bus_data);
  hal_status_t status;
  if (rx != nullptr) {
    if (rx_length < 8u || tx_length != rx_length) {
      return -CYW43_EINVAL;
    }
    /* The upstream length is the full transaction. Only the first command
     * word is driven; response bytes occupy rx[4..length). */
    status = jh_cyw43_gspi_transfer(transport, tx, 4u, rx + 4u, rx_length - 4u);
    /* tx and rx are normally the same upstream buffer; clear the command
     * area only after it has been clocked out. */
    memset(rx, 0, 4u);
  } else {
    status = jh_cyw43_gspi_transfer(transport, tx, tx_length, nullptr, 0u);
  }
  if (status != HAL_OK) {
    s_port_status = status;
  }
  return cyw43_error(status);
}

#if !defined(HAL_CYW43_STACK_LWIP)
extern "C" int cyw43_cb_read_host_interrupt_pin(void *) {
  return jh_cyw43_port_pin_read(CYW43_PIN_WL_HOST_WAKE);
}
extern "C" void cyw43_cb_ensure_awake(void *) {}
extern "C" void cyw43_cb_process_async_event(void *,
                                             const cyw43_async_event_t *) {}
extern "C" void cyw43_cb_process_ethernet(void *, int, size_t,
                                          const uint8_t *) {}
#endif

#else

extern "C" hal_status_t jh_cyw43_driver_start(jh_cyw43_gspi_transport_t *,
                                              const uint8_t *,
                                              jh_cyw43_driver_result_t *) {
  return HAL_EUNSUPPORTED;
}
extern "C" hal_status_t jh_cyw43_driver_stop(void) { return HAL_EUNSUPPORTED; }
extern "C" hal_status_t jh_cyw43_driver_restart(jh_cyw43_driver_result_t *) {
  return HAL_EUNSUPPORTED;
}
extern "C" bool jh_cyw43_driver_is_ready(void) { return false; }

#endif

extern "C" const char *
jh_cyw43_driver_stage_string(jh_cyw43_driver_stage_t stage) {
  switch (stage) {
  case JH_CYW43_DRIVER_STAGE_LOW_LEVEL:
    return "low-level";
  case JH_CYW43_DRIVER_STAGE_BUS:
    return "bus";
  case JH_CYW43_DRIVER_STAGE_READY:
    return "ready";
  case JH_CYW43_DRIVER_STAGE_NONE:
  default:
    return "none";
  }
}
