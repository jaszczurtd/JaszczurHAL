#include "hal/nfc/hal_pn532.h"

#ifdef HAL_ENABLE_PN532

#include "hal/core/hal_array.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/core/jh_handle_pool.h"
#include "hal/gpio/hal_gpio_common.h"
#include "hal/nfc/pn532/pn532.h"
#ifdef HAL_ENABLE_UART
#include "hal/serial/hal_uart_internal.h"
#endif
#include "hal/system/hal_sync.h"

#include <new>
#include <string.h>

#define JH_PN532_TRANSPORT_HANDLE_KIND 23u
#define JH_PN532_READER_HANDLE_KIND 24u

static constexpr size_t kDataExchangeMaxRequestSize =
    HAL_PN532_PACKET_BUFFER_SIZE - 10u;
static constexpr size_t kDataExchangeMaxResponseSize =
    HAL_PN532_PACKET_BUFFER_SIZE - 10u;

enum class pn532_transport_kind_t : uint8_t {
  spi,
#ifdef HAL_ENABLE_I2C
  i2c,
#endif
#ifdef HAL_ENABLE_UART
  uart,
#endif
};

union pn532_transport_storage_t {
  PN532_SPI spi;
#ifdef HAL_ENABLE_I2C
  PN532_I2C i2c;
#endif
#ifdef HAL_ENABLE_UART
  PN532_UART uart;
#endif

  pn532_transport_storage_t() {}
  ~pn532_transport_storage_t() {}
};

union pn532_reader_storage_t {
  PN532 reader;

  pn532_reader_storage_t() {}
  ~pn532_reader_storage_t() {}
};

struct pn532_transport_context_t {
  bool allocated = false;
  uint8_t reader_count = 0u;
  pn532_transport_kind_t kind = pn532_transport_kind_t::spi;
  PN532_BUS_DEVICE *native = nullptr;
  pn532_transport_storage_t storage;
};

struct pn532_reader_context_t {
  bool allocated = false;
  bool begun = false;
  pn532_transport_context_t *transport = nullptr;
  PN532 *native = nullptr;
  hal_mutex_t mutex = nullptr;
  pn532_reader_storage_t storage;
};

static_assert(HAL_PN532_MAX_TRANSPORTS > 0u,
              "HAL_PN532_MAX_TRANSPORTS must be positive");
static_assert(HAL_PN532_MAX_TRANSPORTS <= 255u,
              "HAL_PN532_MAX_TRANSPORTS exceeds handle capacity");
static_assert(HAL_PN532_MAX_READERS > 0u,
              "HAL_PN532_MAX_READERS must be positive");
static_assert(HAL_PN532_MAX_READERS <= 255u,
              "HAL_PN532_MAX_READERS exceeds handle capacity");

static pn532_transport_context_t s_transport_contexts[HAL_PN532_MAX_TRANSPORTS];
static pn532_reader_context_t s_reader_contexts[HAL_PN532_MAX_READERS];
static jh_handle_slot_t s_transport_handle_slots[HAL_PN532_MAX_TRANSPORTS];
static jh_handle_slot_t s_reader_handle_slots[HAL_PN532_MAX_READERS];
static jh_handle_pool_t s_transport_handle_pool = {};
static jh_handle_pool_t s_reader_handle_pool = {};
static hal_mutex_t s_pool_mutex = nullptr;
static bool s_pools_initialized = false;

static hal_status_t pool_lock() {
  hal_mutex_t mutex = jh_hal_mutex_try_create_once(&s_pool_mutex);
  if (mutex == nullptr) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_pools_initialized) {
    hal_status_t status = jh_handle_pool_init(
        &s_transport_handle_pool, s_transport_handle_slots,
        COUNTOF(s_transport_handle_slots), JH_PN532_TRANSPORT_HANDLE_KIND);
    if (status == HAL_OK) {
      status = jh_handle_pool_init(&s_reader_handle_pool, s_reader_handle_slots,
                                   COUNTOF(s_reader_handle_slots),
                                   JH_PN532_READER_HANDLE_KIND);
    }
    if (status != HAL_OK) {
      hal_mutex_unlock(mutex);
      return status;
    }
    s_pools_initialized = true;
  }
  return HAL_OK;
}

static void pool_unlock() { hal_mutex_unlock(s_pool_mutex); }

static pn532_transport_context_t *allocate_transport_context() {
  for (size_t i = 0u; i < COUNTOF(s_transport_contexts); ++i) {
    if (!s_transport_contexts[i].allocated) {
      s_transport_contexts[i].allocated = true;
      s_transport_contexts[i].reader_count = 0u;
      s_transport_contexts[i].native = nullptr;
      return &s_transport_contexts[i];
    }
  }
  return nullptr;
}

static pn532_reader_context_t *allocate_reader_context() {
  for (size_t i = 0u; i < COUNTOF(s_reader_contexts); ++i) {
    if (!s_reader_contexts[i].allocated) {
      s_reader_contexts[i].allocated = true;
      s_reader_contexts[i].begun = false;
      s_reader_contexts[i].transport = nullptr;
      s_reader_contexts[i].native = nullptr;
      s_reader_contexts[i].mutex = nullptr;
      return &s_reader_contexts[i];
    }
  }
  return nullptr;
}

static void release_transport_context(pn532_transport_context_t *context) {
  if (context == nullptr || !context->allocated) {
    return;
  }
  if (context->native != nullptr) {
    switch (context->kind) {
    case pn532_transport_kind_t::spi:
      context->storage.spi.~PN532_SPI();
      break;
#ifdef HAL_ENABLE_I2C
    case pn532_transport_kind_t::i2c:
      context->storage.i2c.~PN532_I2C();
      break;
#endif
#ifdef HAL_ENABLE_UART
    case pn532_transport_kind_t::uart:
      context->storage.uart.~PN532_UART();
      break;
#endif
    }
  }
  context->native = nullptr;
  context->reader_count = 0u;
  context->allocated = false;
}

static void release_reader_context(pn532_reader_context_t *context) {
  if (context == nullptr || !context->allocated) {
    return;
  }
  if (context->native != nullptr) {
    context->storage.reader.~PN532();
  }
  if (context->mutex != nullptr) {
    hal_mutex_destroy(context->mutex);
  }
  if (context->transport != nullptr && context->transport->reader_count > 0u) {
    --context->transport->reader_count;
  }
  context->native = nullptr;
  context->begun = false;
  context->mutex = nullptr;
  context->transport = nullptr;
  context->allocated = false;
}

static hal_status_t acquire_reader(hal_pn532_t reader,
                                   jh_handle_lease_t *out_lease,
                                   pn532_reader_context_t **out_context) {
  if (out_lease == nullptr || out_context == nullptr) {
    return HAL_EINVAL;
  }
  *out_context = nullptr;
  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  status = jh_handle_acquire(&s_reader_handle_pool, reader, out_lease);
  if (status == HAL_OK) {
    *out_context = static_cast<pn532_reader_context_t *>(out_lease->token);
  }
  pool_unlock();
  return status;
}

static hal_status_t end_reader_operation(jh_handle_lease_t *lease) {
  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  void *deferred_token = nullptr;
  status =
      jh_handle_end_operation(&s_reader_handle_pool, lease, &deferred_token);
  if (deferred_token != nullptr) {
    release_reader_context(
        static_cast<pn532_reader_context_t *>(deferred_token));
  }
  pool_unlock();
  return status;
}

template <typename Operation>
static hal_status_t invoke_reader(hal_pn532_t reader, Operation operation,
                                  bool require_begun = true,
                                  bool mark_begun_on_success = false) {
  jh_handle_lease_t lease = {};
  pn532_reader_context_t *context = nullptr;
  hal_status_t status = acquire_reader(reader, &lease, &context);
  if (status != HAL_OK) {
    return status;
  }
  hal_mutex_lock(context->mutex);
  status = require_begun && !context->begun ? HAL_ESTATE : HAL_OK;
  if (status == HAL_OK) {
    if (mark_begun_on_success) {
      context->begun = false;
    }
    status = operation(context->native);
  }
  if (status == HAL_OK && mark_begun_on_success) {
    context->begun = true;
  }
  hal_mutex_unlock(context->mutex);
  const hal_status_t end_status = end_reader_operation(&lease);
  return status != HAL_OK ? status : end_status;
}

hal_pn532_spi_config_t hal_pn532_spi_default_config(uint8_t chip_select_pin) {
  hal_pn532_spi_config_t config = {};
  config.chip_select_pin = chip_select_pin;
  config.reset_pin = HAL_PN532_PIN_NONE;
  config.spi_bus = 0u;
  return config;
}

hal_pn532_i2c_config_t hal_pn532_i2c_default_config(void) {
  hal_pn532_i2c_config_t config = {};
  config.reset_pin = HAL_PN532_PIN_NONE;
  config.address = HAL_PN532_I2C_DEFAULT_ADDRESS;
  config.i2c_bus = 0u;
  return config;
}

#ifdef HAL_ENABLE_UART
hal_pn532_uart_config_t hal_pn532_uart_default_config(hal_uart_port_t port,
                                                      uint8_t rx_pin,
                                                      uint8_t tx_pin) {
  hal_pn532_uart_config_t config = {};
  config.port = port;
  config.rx_pin = rx_pin;
  config.tx_pin = tx_pin;
  config.reset_pin = HAL_PN532_PIN_NONE;
  return config;
}
#endif

hal_status_t
hal_pn532_transport_create_spi(const hal_pn532_spi_config_t *config,
                               hal_pn532_transport_t *out_transport) {
  if (out_transport != nullptr) {
    *out_transport = nullptr;
  }
  if (config == nullptr || out_transport == nullptr ||
      config->chip_select_pin == HAL_PN532_PIN_NONE ||
      !jh_hal_gpio_pin_valid(config->chip_select_pin) ||
      (config->reset_pin != HAL_PN532_PIN_NONE &&
       (!jh_hal_gpio_pin_valid(config->reset_pin) ||
        config->reset_pin == config->chip_select_pin)) ||
      config->spi_bus > 1u) {
    return HAL_EINVAL;
  }

  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  pn532_transport_context_t *context = allocate_transport_context();
  if (context == nullptr) {
    pool_unlock();
    return HAL_ENOMEM;
  }
  context->kind = pn532_transport_kind_t::spi;
  context->native = new (&context->storage.spi)
      PN532_SPI(config->chip_select_pin, config->reset_pin, config->spi_bus);
  void *handle = nullptr;
  status = jh_handle_allocate(&s_transport_handle_pool, context, &handle);
  if (status == HAL_OK) {
    *out_transport = static_cast<hal_pn532_transport_t>(handle);
  } else {
    release_transport_context(context);
  }
  pool_unlock();
  return status;
}

#ifdef HAL_ENABLE_I2C
hal_status_t
hal_pn532_transport_create_i2c(const hal_pn532_i2c_config_t *config,
                               hal_pn532_transport_t *out_transport) {
  if (out_transport != nullptr) {
    *out_transport = nullptr;
  }
  if (config == nullptr || out_transport == nullptr ||
      (config->reset_pin != HAL_PN532_PIN_NONE &&
       !jh_hal_gpio_pin_valid(config->reset_pin)) ||
      config->i2c_bus > 1u || config->address == 0u ||
      config->address > 0x7fu) {
    return HAL_EINVAL;
  }

  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  pn532_transport_context_t *context = allocate_transport_context();
  if (context == nullptr) {
    pool_unlock();
    return HAL_ENOMEM;
  }
  context->kind = pn532_transport_kind_t::i2c;
  context->native = new (&context->storage.i2c)
      PN532_I2C(config->reset_pin, config->address, config->i2c_bus);
  void *handle = nullptr;
  status = jh_handle_allocate(&s_transport_handle_pool, context, &handle);
  if (status == HAL_OK) {
    *out_transport = static_cast<hal_pn532_transport_t>(handle);
  } else {
    release_transport_context(context);
  }
  pool_unlock();
  return status;
}
#endif

#ifdef HAL_ENABLE_UART
static bool uart_config_valid(const hal_pn532_uart_config_t *config) {
  return config != nullptr && config->rx_pin != HAL_PN532_PIN_NONE &&
         config->tx_pin != HAL_PN532_PIN_NONE &&
         config->rx_pin != config->tx_pin &&
         jh_hal_uart_validate_config_for_target(config->port, config->rx_pin,
                                                config->tx_pin) == HAL_OK &&
         (config->reset_pin == HAL_PN532_PIN_NONE ||
          (jh_hal_gpio_pin_valid(config->reset_pin) &&
           config->reset_pin != config->rx_pin &&
           config->reset_pin != config->tx_pin));
}

hal_status_t
hal_pn532_transport_create_uart(const hal_pn532_uart_config_t *config,
                                hal_pn532_transport_t *out_transport) {
  if (out_transport != nullptr) {
    *out_transport = nullptr;
  }
  if (!uart_config_valid(config) || out_transport == nullptr) {
    return HAL_EINVAL;
  }

  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  pn532_transport_context_t *context = allocate_transport_context();
  if (context == nullptr) {
    pool_unlock();
    return HAL_ENOMEM;
  }
  context->kind = pn532_transport_kind_t::uart;
  context->native = new (&context->storage.uart) PN532_UART(
      config->port, config->rx_pin, config->tx_pin, config->reset_pin);
  void *handle = nullptr;
  status = jh_handle_allocate(&s_transport_handle_pool, context, &handle);
  if (status == HAL_OK) {
    *out_transport = static_cast<hal_pn532_transport_t>(handle);
  } else {
    release_transport_context(context);
  }
  pool_unlock();
  return status;
}

hal_status_t
hal_pn532_transport_create_uart_handle(hal_uart_t uart, uint8_t reset_pin,
                                       hal_pn532_transport_t *out_transport) {
  if (out_transport != nullptr) {
    *out_transport = nullptr;
  }
  if (uart == nullptr || out_transport == nullptr ||
      (reset_pin != HAL_PN532_PIN_NONE && !jh_hal_gpio_pin_valid(reset_pin))) {
    return HAL_EINVAL;
  }

  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  pn532_transport_context_t *context = allocate_transport_context();
  if (context == nullptr) {
    pool_unlock();
    return HAL_ENOMEM;
  }
  context->kind = pn532_transport_kind_t::uart;
  context->native = new (&context->storage.uart) PN532_UART(uart, reset_pin);
  void *handle = nullptr;
  status = jh_handle_allocate(&s_transport_handle_pool, context, &handle);
  if (status == HAL_OK) {
    *out_transport = static_cast<hal_pn532_transport_t>(handle);
  } else {
    release_transport_context(context);
  }
  pool_unlock();
  return status;
}
#endif

hal_status_t hal_pn532_transport_destroy(hal_pn532_transport_t transport) {
  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  void *resolved = nullptr;
  status = jh_handle_resolve(&s_transport_handle_pool, transport, &resolved,
                             nullptr);
  auto *context = static_cast<pn532_transport_context_t *>(resolved);
  if (status == HAL_OK && context->reader_count != 0u) {
    status = HAL_EBUSY;
  }
  if (status == HAL_OK) {
    void *token = nullptr;
    status = jh_handle_release(&s_transport_handle_pool, transport, &token);
    if (status == HAL_OK) {
      release_transport_context(
          static_cast<pn532_transport_context_t *>(token));
    }
  }
  pool_unlock();
  return status;
}

hal_status_t hal_pn532_create(hal_pn532_transport_t transport,
                              hal_pn532_t *out_reader) {
  if (out_reader != nullptr) {
    *out_reader = nullptr;
  }
  if (out_reader == nullptr) {
    return HAL_EINVAL;
  }

  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  void *resolved = nullptr;
  status = jh_handle_resolve(&s_transport_handle_pool, transport, &resolved,
                             nullptr);
  auto *transport_context = static_cast<pn532_transport_context_t *>(resolved);
  if (status == HAL_OK && transport_context->reader_count != 0u) {
    status = HAL_EBUSY;
  }

  pn532_reader_context_t *context = nullptr;
  if (status == HAL_OK) {
    context = allocate_reader_context();
    if (context == nullptr) {
      status = HAL_ENOMEM;
    }
  }
  if (status == HAL_OK) {
    context->mutex = jh_hal_mutex_try_create();
    if (context->mutex == nullptr) {
      context->allocated = false;
      status = HAL_ENOMEM;
    }
  }
  if (status == HAL_OK) {
    context->transport = transport_context;
    context->native =
        new (&context->storage.reader) PN532(transport_context->native);
    ++transport_context->reader_count;
    void *handle = nullptr;
    status = jh_handle_allocate(&s_reader_handle_pool, context, &handle);
    if (status == HAL_OK) {
      *out_reader = static_cast<hal_pn532_t>(handle);
    } else {
      release_reader_context(context);
    }
  }
  pool_unlock();
  return status;
}

hal_status_t hal_pn532_destroy(hal_pn532_t reader) {
  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  void *token = nullptr;
  status = jh_handle_begin_close(&s_reader_handle_pool, reader, &token);
  if (token != nullptr) {
    release_reader_context(static_cast<pn532_reader_context_t *>(token));
  }
  pool_unlock();
  return status;
}

hal_status_t hal_pn532_begin(hal_pn532_t reader) {
  return invoke_reader(
      reader, [](PN532 *native) { return native->begin(); }, false, true);
}

hal_status_t hal_pn532_wakeup(hal_pn532_t reader) {
  return invoke_reader(reader, [](PN532 *native) { return native->wakeup(); });
}

hal_status_t hal_pn532_sam_configure(hal_pn532_t reader) {
  return invoke_reader(reader,
                       [](PN532 *native) { return native->SAMConfig(); });
}

hal_status_t hal_pn532_get_firmware_version(hal_pn532_t reader,
                                            uint32_t *out_version) {
  if (out_version == nullptr) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [out_version](PN532 *native) {
    return native->getFirmwareVersion(out_version);
  });
}

hal_status_t hal_pn532_send_command_check_ack(hal_pn532_t reader,
                                              const uint8_t *command,
                                              size_t command_size,
                                              uint16_t timeout_ms) {
  if (command == nullptr || command_size == 0u) {
    return HAL_EINVAL;
  }
  return invoke_reader(
      reader, [command, command_size, timeout_ms](PN532 *native) {
        return native->sendCommandCheckAck(command, command_size, timeout_ms);
      });
}

hal_status_t hal_pn532_read_passive_target(hal_pn532_t reader,
                                           hal_pn532_modulation_t modulation,
                                           uint16_t timeout_ms,
                                           hal_pn532_uid_t *out_uid) {
  if (out_uid == nullptr || modulation != HAL_PN532_MODULATION_ISO14443A) {
    return HAL_EINVAL;
  }
  memset(out_uid, 0, sizeof(*out_uid));
  return invoke_reader(
      reader, [modulation, timeout_ms, out_uid](PN532 *native) {
        uint8_t uid[HAL_PN532_UID_MAX_SIZE] = {};
        uint8_t uid_size = 0u;
        const hal_status_t status = native->readPassiveTargetID(
            static_cast<uint8_t>(modulation), uid, &uid_size, timeout_ms);
        if (status == HAL_OK && uid_size > 0u &&
            uid_size <= HAL_PN532_UID_MAX_SIZE) {
          out_uid->size = uid_size;
          memcpy(out_uid->bytes, uid, uid_size);
        }
        return (status == HAL_OK &&
                (uid_size == 0u || uid_size > HAL_PN532_UID_MAX_SIZE))
                   ? HAL_EPROTO
                   : status;
      });
}

hal_status_t hal_pn532_list_passive_target(hal_pn532_t reader,
                                           uint8_t *response,
                                           size_t response_size) {
  if (response == nullptr || response_size == 0u) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [response, response_size](PN532 *native) {
    return native->inListPassiveTarget(response, response_size);
  });
}

hal_status_t hal_pn532_data_exchange(hal_pn532_t reader, const uint8_t *request,
                                     size_t request_size, uint8_t *response,
                                     size_t *inout_response_size) {
  if (request == nullptr || request_size == 0u || response == nullptr ||
      inout_response_size == nullptr ||
      request_size > kDataExchangeMaxRequestSize) {
    return HAL_EINVAL;
  }
  const size_t response_capacity =
      *inout_response_size < kDataExchangeMaxResponseSize
          ? *inout_response_size
          : kDataExchangeMaxResponseSize;
  return invoke_reader(reader, [request, request_size, response,
                                response_capacity,
                                inout_response_size](PN532 *native) {
    size_t response_size = response_capacity;
    const hal_status_t status =
        native->inDataExchange(request, request_size, response, &response_size);
    if (status == HAL_OK) {
      *inout_response_size = response_size;
    }
    return status;
  });
}

hal_status_t hal_pn532_mifare_classic_authenticate(
    hal_pn532_t reader, const hal_pn532_uid_t *uid, uint8_t block,
    hal_pn532_key_type_t key_type,
    const uint8_t key_data[HAL_PN532_MIFARE_KEY_SIZE]) {
  if (uid == nullptr || (uid->size != 4u && uid->size != 7u) ||
      key_data == nullptr ||
      (key_type != HAL_PN532_KEY_A && key_type != HAL_PN532_KEY_B)) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [uid, block, key_type, key_data](PN532 *native) {
    return native->mifareclassic_AuthenticateBlock(
        uid->bytes, uid->size, block, key_type == HAL_PN532_KEY_A ? 0u : 1u,
        key_data);
  });
}

hal_status_t
hal_pn532_mifare_classic_read(hal_pn532_t reader, uint8_t block,
                              uint8_t data[HAL_PN532_MIFARE_BLOCK_SIZE]) {
  if (data == nullptr) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [block, data](PN532 *native) {
    return native->mifareclassic_ReadDataBlock(block, data);
  });
}

hal_status_t hal_pn532_mifare_classic_write(
    hal_pn532_t reader, uint8_t block,
    const uint8_t data[HAL_PN532_MIFARE_BLOCK_SIZE]) {
  if (data == nullptr) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [block, data](PN532 *native) {
    return native->mifareclassic_WriteDataBlock(block, data);
  });
}

hal_status_t
hal_pn532_mifare_ultralight_read(hal_pn532_t reader, uint8_t page,
                                 uint8_t data[HAL_PN532_PAGE_SIZE]) {
  if (data == nullptr) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [page, data](PN532 *native) {
    return native->mifareultralight_ReadPage(page, data);
  });
}

hal_status_t
hal_pn532_mifare_ultralight_write(hal_pn532_t reader, uint8_t page,
                                  const uint8_t data[HAL_PN532_PAGE_SIZE]) {
  if (data == nullptr) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [page, data](PN532 *native) {
    return native->mifareultralight_WritePage(page, data);
  });
}

hal_status_t hal_pn532_ntag2xx_write(hal_pn532_t reader, uint8_t page,
                                     const uint8_t data[HAL_PN532_PAGE_SIZE]) {
  if (data == nullptr) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [page, data](PN532 *native) {
    return native->ntag2xx_WritePage(page, data);
  });
}

hal_status_t hal_pn532_check_response_frame(const uint8_t *frame,
                                            size_t frame_size,
                                            uint8_t command) {
  return PN532::checkResponseFrame(frame, frame_size, command);
}

#endif /* HAL_ENABLE_PN532 */
