#include "hal/spi/hal_spi_device.h"

#include "hal/gpio/hal_gpio.h"

static bool settings_valid(const hal_spi_settings_t *settings) {
  return settings == NULL || ((settings->bit_order == HAL_SPI_LSBFIRST ||
                               settings->bit_order == HAL_SPI_MSBFIRST) &&
                              settings->data_mode <= HAL_SPI_MODE3);
}

static hal_spi_settings_t
normalized_settings(const hal_spi_settings_t *settings) {
  hal_spi_settings_t normalized = {HAL_SPI_CLOCK_DEFAULT_HZ, HAL_SPI_MSBFIRST,
                                   HAL_SPI_MODE0};
  if (settings != NULL) {
    normalized = *settings;
    if (normalized.clock_hz == 0u) {
      normalized.clock_hz = HAL_SPI_CLOCK_DEFAULT_HZ;
    }
  }
  return normalized;
}

static bool device_valid(const hal_spi_device_t *device) {
  return device != NULL && device->initialized && device->bus <= 1u &&
         settings_valid(&device->settings);
}

static bool operation_valid(const hal_spi_device_operation_t *operation) {
  if (operation == NULL) {
    return false;
  }
  if (operation->length == 0u) {
    return operation->type >= HAL_SPI_DEVICE_OP_READ &&
           operation->type <= HAL_SPI_DEVICE_OP_TRANSFER_IN_PLACE;
  }

  switch (operation->type) {
  case HAL_SPI_DEVICE_OP_READ:
  case HAL_SPI_DEVICE_OP_TRANSFER_IN_PLACE:
    return operation->rx_data != NULL;
  case HAL_SPI_DEVICE_OP_WRITE:
    return operation->tx_data != NULL;
  case HAL_SPI_DEVICE_OP_TRANSFER:
    return operation->tx_data != NULL && operation->rx_data != NULL;
  default:
    return false;
  }
}

static hal_status_t
execute_operation(const hal_spi_device_t *device,
                  const hal_spi_device_operation_t *operation) {
  switch (operation->type) {
  case HAL_SPI_DEVICE_OP_READ:
    return hal_spi_transfer_txrx(device->bus, NULL, operation->rx_data,
                                 operation->length);
  case HAL_SPI_DEVICE_OP_WRITE:
    return hal_spi_write(device->bus, operation->tx_data, operation->length);
  case HAL_SPI_DEVICE_OP_TRANSFER:
    return hal_spi_transfer_txrx(device->bus, operation->tx_data,
                                 operation->rx_data, operation->length);
  case HAL_SPI_DEVICE_OP_TRANSFER_IN_PLACE:
    return hal_spi_transfer_buffer(device->bus, operation->rx_data,
                                   operation->length);
  default:
    return HAL_EINVAL;
  }
}

hal_status_t hal_spi_device_init(hal_spi_device_t *device, uint8_t bus,
                                 uint8_t cs_pin,
                                 const hal_spi_settings_t *settings) {
  if (device == NULL || bus > 1u || !settings_valid(settings)) {
    return HAL_EINVAL;
  }

  device->settings = normalized_settings(settings);
  device->bus = bus;
  device->cs_pin = cs_pin;
  device->initialized = true;
  device->acquired = false;

  if (cs_pin != HAL_SPI_DEVICE_CS_NONE) {
    hal_gpio_set_mode(cs_pin, HAL_GPIO_OUTPUT_HIGH);
  }
  return HAL_OK;
}

hal_status_t hal_spi_device_acquire(hal_spi_device_t *device) {
  if (!device_valid(device)) {
    return HAL_EINVAL;
  }
  if (device->acquired) {
    return HAL_ESTATE;
  }

  hal_spi_lock(device->bus);
  if (device->acquired) {
    hal_spi_unlock(device->bus);
    return HAL_ESTATE;
  }

  const hal_status_t status =
      hal_spi_begin_transaction(device->bus, &device->settings);
  if (hal_status_is_error(status)) {
    hal_spi_unlock(device->bus);
    return status;
  }

  if (device->cs_pin != HAL_SPI_DEVICE_CS_NONE) {
    hal_gpio_write(device->cs_pin, false);
  }
  device->acquired = true;
  return HAL_OK;
}

hal_status_t hal_spi_device_release(hal_spi_device_t *device) {
  if (!device_valid(device)) {
    return HAL_EINVAL;
  }
  if (!device->acquired) {
    return HAL_ESTATE;
  }

  const hal_status_t status = hal_spi_end_transaction(device->bus);
  if (device->cs_pin != HAL_SPI_DEVICE_CS_NONE) {
    hal_gpio_write(device->cs_pin, true);
  }
  device->acquired = false;
  hal_spi_unlock(device->bus);
  return status;
}

hal_status_t hal_spi_device_finish(hal_spi_device_t *device,
                                   hal_status_t operation_status) {
  const hal_status_t release_status = hal_spi_device_release(device);
  return hal_status_is_error(operation_status) ? operation_status
                                               : release_status;
}

hal_status_t
hal_spi_device_transaction(hal_spi_device_t *device,
                           const hal_spi_device_operation_t *operations,
                           size_t operation_count) {
  if (!device_valid(device) || (operation_count > 0u && operations == NULL)) {
    return HAL_EINVAL;
  }
  for (size_t i = 0u; i < operation_count; ++i) {
    if (!operation_valid(&operations[i])) {
      return HAL_EINVAL;
    }
  }

  hal_status_t status = hal_spi_device_acquire(device);
  if (hal_status_is_error(status)) {
    return status;
  }

  for (size_t i = 0u; i < operation_count; ++i) {
    status = execute_operation(device, &operations[i]);
    if (hal_status_is_error(status)) {
      break;
    }
  }

  return hal_spi_device_finish(device, status);
}
