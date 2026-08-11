#include "jh_ble_controller.h"

#include "cybt_shared_bus_driver.h"
#include "cyw43.h"
#include "cyw43_configport.h"
#include "hal/network/cyw43/jh_cyw43_radio.h"

static hal_status_t status_from_cybt(int status) {
  switch (status) {
  case CYBT_SUCCESS:
    return HAL_OK;
  case CYBT_ERR_BADARG:
    return HAL_EINVAL;
  case CYBT_ERR_OUT_OF_MEMORY:
  case CYBT_ERR_INIT_MEMPOOL_FAILED:
    return HAL_ENOMEM;
  case CYBT_ERR_TIMEOUT:
    return HAL_ETIMEOUT;
  case CYBT_ERR_QUEUE_ALMOST_FULL:
  case CYBT_ERR_QUEUE_FULL:
  case CYBT_ERR_SEND_QUEUE_FAILED:
    return HAL_EBUSY;
  case CYBT_ERR_HCI_NOT_INITIALIZE:
    return HAL_EUNINIT;
  default:
    return HAL_EIO;
  }
}

static hal_status_t
controller_start(void *context, jh_ble_controller_service_fn service,
                 void *service_context,
                 jh_ble_controller_invalidation_fn invalidation,
                 void *invalidation_context) {
  (void)context;
  if (service == NULL) {
    return HAL_EINVAL;
  }
  hal_status_t status = jh_cyw43_radio_set_service_handler(
      JH_CYW43_RADIO_CLIENT_BLE, service, service_context);
  if (status != HAL_OK) {
    return status;
  }
  status = jh_cyw43_radio_set_invalidation_handler(
      JH_CYW43_RADIO_CLIENT_BLE, invalidation, invalidation_context);
  if (status != HAL_OK) {
    (void)jh_cyw43_radio_set_service_handler(JH_CYW43_RADIO_CLIENT_BLE, NULL,
                                             NULL);
    return status;
  }
  status = jh_cyw43_radio_acquire(JH_CYW43_RADIO_CLIENT_BLE);
  if (status != HAL_OK) {
    (void)jh_cyw43_radio_set_invalidation_handler(JH_CYW43_RADIO_CLIENT_BLE,
                                                  NULL, NULL);
    (void)jh_cyw43_radio_set_service_handler(JH_CYW43_RADIO_CLIENT_BLE, NULL,
                                             NULL);
  }
  return status;
}

static hal_status_t controller_stop(void *context) {
  (void)context;
  hal_status_t status = jh_cyw43_radio_release(JH_CYW43_RADIO_CLIENT_BLE);
  const hal_status_t invalidation_status =
      jh_cyw43_radio_set_invalidation_handler(JH_CYW43_RADIO_CLIENT_BLE, NULL,
                                              NULL);
  const hal_status_t service_status =
      jh_cyw43_radio_set_service_handler(JH_CYW43_RADIO_CLIENT_BLE, NULL, NULL);
  if (status == HAL_OK) {
    status = invalidation_status;
  }
  return status == HAL_OK ? service_status : status;
}

static hal_status_t controller_service(void *context) {
  (void)context;
  return jh_cyw43_radio_service(JH_CYW43_RADIO_CLIENT_BLE);
}

static hal_status_t controller_hci_init(void *context) {
  (void)context;
  return status_from_cybt(cyw43_bluetooth_hci_init());
}

static hal_status_t controller_hci_read(void *context, uint8_t *buffer,
                                        uint32_t capacity,
                                        uint32_t *out_length) {
  (void)context;
  return status_from_cybt(
      cyw43_bluetooth_hci_read(buffer, capacity, out_length));
}

static hal_status_t controller_hci_write(void *context, uint8_t *buffer,
                                         size_t length) {
  (void)context;
  return status_from_cybt(cyw43_bluetooth_hci_write(buffer, length));
}

static hal_status_t controller_read_factory_address(void *context,
                                                    uint8_t address[6]) {
  (void)context;
  if (address == NULL) {
    return HAL_EINVAL;
  }
  jh_cyw43_port_get_mac(0, address);
  return HAL_OK;
}

static const jh_ble_controller_t s_controller = {
    .context = NULL,
    .start = controller_start,
    .stop = controller_stop,
    .service = controller_service,
    .hci_init = controller_hci_init,
    .hci_read = controller_hci_read,
    .hci_write = controller_hci_write,
    .read_factory_address = controller_read_factory_address,
};

const jh_ble_controller_t *jh_ble_controller_cyw43_instance(void) {
  return &s_controller;
}
