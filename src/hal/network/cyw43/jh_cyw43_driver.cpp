#include "hal/core/hal_config.h"
#include "hal/core/hal_target.h"

#include "jh_cyw43_driver.h"

#include <string.h>

#if (HAL_TARGET_IS_RP && defined(HAL_CYW43_BUS_PICO_PIO)) ||                   \
    (HAL_TARGET_IS_STM32G474 && defined(HAL_CYW43_BUS_STM32_GSPI))

#if defined(HAL_CYW43_STACK_LWIP)
#include "jh_cyw43_lwip.h"
#endif

extern "C" {
#include "vendor/src/cyw43_country.h"
#include "vendor/src/cyw43_internal.h"
#include "vendor/src/cyw43_spi.h"
#if defined(HAL_CYW43_STACK_LWIP)
#include "lwip/init.h"
#endif
}

#include <hal/system/hal_system.h>

namespace {

constexpr uint32_t kIdentification = 0xFEEDBEADu;
constexpr uint32_t kConfiguredBusControl = 0x000200B3u;

#if defined(HAL_CYW43_STACK_LWIP)
bool s_lwip_initialized;
#endif
jh_cyw43_gspi_transport_t *s_transport;
uint32_t s_generation;
hal_status_t s_port_status = HAL_OK;
bool s_ready;
bool s_in_service;

cyw43_ll_t *low_level(void) { return &cyw43_state.cyw43_ll; }

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
#endif
  /* Bring the chip up through the ctrl layer (cyw43_init + wifi_set_up), the
   * same path Pico SDK uses for both networking and the on-board LED. The
   * LED-only build compiles cyw43_ctrl without lwIP; driving
   * cyw43_ll_bus_init directly (bypassing ctrl) is not a valid bring-up. */
  cyw43_init(&cyw43_state);
  /* HOST_WAKE must be armed while wifi_set_up() performs its first control
   * ioctls, not only after they return. */
  const hal_status_t wake_status = jh_cyw43_gspi_host_wake_attach(s_transport);
  if (wake_status != HAL_OK) {
    cyw43_ll_deinit(low_level());
    return wake_status;
  }
  result->stage = JH_CYW43_DRIVER_STAGE_BUS;
  cyw43_wifi_set_up(&cyw43_state, CYW43_ITF_STA, true,
                    (uint32_t)HAL_CYW43_COUNTRY_CODE);
  const int error = cyw43_poll == nullptr ? -CYW43_EIO : 0;
  result->cyw43_error = error;
  if (error != 0 || s_port_status != HAL_OK) {
    (void)jh_cyw43_gspi_host_wake_detach(s_transport);
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
                      jh_cyw43_driver_result_t *result) {
  if (transport == nullptr || result == nullptr) {
    return HAL_EINVAL;
  }
  if (!transport->initialized) {
    return HAL_EUNINIT;
  }
  if (s_transport != nullptr) {
    return HAL_EEXIST;
  }
  s_transport = transport;
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
  cyw43_deinit(&cyw43_state);
  const hal_status_t wake_status = jh_cyw43_gspi_host_wake_detach(s_transport);
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
  cyw43_deinit(&cyw43_state);
  const hal_status_t wake_status = jh_cyw43_gspi_host_wake_detach(s_transport);
  if (wake_status != HAL_OK) {
    return wake_status;
  }
  s_ready = false;
  return start_low_level(result);
}

extern "C" bool jh_cyw43_driver_is_ready(void) { return s_ready; }

extern "C" hal_status_t jh_cyw43_driver_service(bool *out_host_wake) {
  if (out_host_wake != nullptr) {
    *out_host_wake = false;
  }
  if (!s_ready || s_transport == nullptr) {
    return HAL_EUNINIT;
  }
  if (s_in_service) {
    return HAL_EBUSY;
  }
  s_in_service = true;
  hal_status_t status = jh_cyw43_gspi_host_wake_refresh(s_transport);
  if (status == HAL_OK) {
    const bool host_wake = jh_cyw43_gspi_host_wake_pending(s_transport);
    if (cyw43_poll != nullptr) {
      cyw43_poll();
    }
    if (host_wake) {
      status = jh_cyw43_gspi_host_wake_clear(s_transport);
    }
    if (out_host_wake != nullptr) {
      *out_host_wake = host_wake;
    }
  }
  s_in_service = false;
  return status;
}

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
  /*
   * With CYW43_USE_OTP_MAC enabled, cyw43_ensure_up() stores the address read
   * from the radio in cyw43_state.mac before lwIP initializes its netif.
   * Mirror Pico SDK's cyw43_hal_get_mac() exactly: returning an independently
   * generated cache here would replace the factory Raspberry Pi address in
   * both the public API and lwIP's netif.
   */
  memcpy(mac, cyw43_state.mac, sizeof(cyw43_state.mac));
}

extern "C" void jh_cyw43_port_generate_laa_mac(int, uint8_t mac[6]) {
  uint8_t uid[HAL_DEVICE_UID_BYTES]{};
  if (hal_get_device_uid(uid) != HAL_OK ||
      jh_cyw43_make_laa_mac_from_uid(uid, mac) != HAL_OK) {
    memset(mac, 0, 6u);
    mac[0] = 0x02u;
  }
}

extern "C" void jh_cyw43_port_pin_config(int, int, int, int) {}
extern "C" void jh_cyw43_port_pin_config_irq_falling(int, int) {}
extern "C" int jh_cyw43_port_pin_read(int) {
  if (s_transport == nullptr) {
    return 0;
  }

  /* The RP2040 GSPI port polls the shared DATA/HOST_WAKE line.  A control
   * response can assert it after the transfer's final rearm, so the cached
   * latch alone is insufficient here and makes cyw43_do_ioctl() wait for its
   * full timeout. */
  (void)jh_cyw43_gspi_host_wake_refresh(s_transport);
  return jh_cyw43_gspi_host_wake_pending(s_transport);
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
/* Without lwIP, cyw43_lwip.c is not compiled; supply the tcpip/ethernet
 * callbacks that cyw43_ctrl.c and cyw43_ll.c reference as no-ops (the LED-only
 * path carries no network traffic). host-wake / ensure-awake / async-event
 * callbacks come from cyw43_ctrl.c, now compiled in this build too. */
extern "C" void cyw43_cb_tcpip_init(cyw43_t *, int) {}
extern "C" void cyw43_cb_tcpip_deinit(cyw43_t *, int) {}
extern "C" void cyw43_cb_tcpip_set_link_up(cyw43_t *, int) {}
extern "C" void cyw43_cb_tcpip_set_link_down(cyw43_t *, int) {}
extern "C" void cyw43_cb_process_ethernet(void *, int, size_t,
                                          const uint8_t *) {}
#endif

#else

extern "C" hal_status_t jh_cyw43_driver_start(jh_cyw43_gspi_transport_t *,
                                              jh_cyw43_driver_result_t *) {
  return HAL_EUNSUPPORTED;
}
extern "C" hal_status_t jh_cyw43_driver_stop(void) { return HAL_EUNSUPPORTED; }
extern "C" hal_status_t jh_cyw43_driver_restart(jh_cyw43_driver_result_t *) {
  return HAL_EUNSUPPORTED;
}
extern "C" bool jh_cyw43_driver_is_ready(void) { return false; }
extern "C" hal_status_t jh_cyw43_driver_service(bool *) {
  return HAL_EUNSUPPORTED;
}

#endif

extern "C" hal_status_t
jh_cyw43_make_laa_mac_from_uid(const uint8_t uid[HAL_DEVICE_UID_BYTES],
                               uint8_t mac[6]) {
  if (uid == nullptr || mac == nullptr) {
    return HAL_EINVAL;
  }
  static_assert(HAL_DEVICE_UID_BYTES >= 6u,
                "CYW43 fallback MAC requires at least six UID bytes");
  memcpy(mac, &uid[HAL_DEVICE_UID_BYTES - 6u], 6u);
  mac[0] &= (uint8_t)~0x01u;
  mac[0] |= 0x02u;
  return HAL_OK;
}

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
