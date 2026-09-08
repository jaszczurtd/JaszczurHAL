#include "hal/nfc/hal_mfrc522.h"

#ifdef HAL_ENABLE_MFRC522

#include "hal/core/hal_array.h"
#include "hal/core/hal_mutex_once.h"
#include "hal/core/jh_handle_pool.h"
#include "hal/gpio/hal_gpio_common.h"
#include "hal/nfc/mfrc522/mfrc522.h"
#include "hal/system/hal_sync.h"

#include <new>
#include <string.h>

#define JH_MFRC522_TRANSPORT_HANDLE_KIND 21u
#define JH_MFRC522_READER_HANDLE_KIND 22u

enum class mfrc522_transport_kind_t : uint8_t { spi, i2c };

union mfrc522_transport_storage_t {
  MFRC522_SPI spi;
  MFRC522_I2C i2c;

  mfrc522_transport_storage_t() {}
  ~mfrc522_transport_storage_t() {}
};

union mfrc522_reader_storage_t {
  MFRC522 reader;

  mfrc522_reader_storage_t() {}
  ~mfrc522_reader_storage_t() {}
};

struct mfrc522_transport_context_t {
  bool allocated = false;
  uint8_t reader_count = 0u;
  mfrc522_transport_kind_t kind = mfrc522_transport_kind_t::spi;
  MFRC522_BUS_DEVICE *native = nullptr;
  mfrc522_transport_storage_t storage;
};

struct mfrc522_reader_context_t {
  bool allocated = false;
  bool begun = false;
  mfrc522_transport_context_t *transport = nullptr;
  MFRC522 *native = nullptr;
  hal_mutex_t mutex = nullptr;
  mfrc522_reader_storage_t storage;
};

static_assert(HAL_MFRC522_MAX_TRANSPORTS > 0u,
              "HAL_MFRC522_MAX_TRANSPORTS must be positive");
static_assert(HAL_MFRC522_MAX_TRANSPORTS <= 255u,
              "HAL_MFRC522_MAX_TRANSPORTS exceeds handle capacity");
static_assert(HAL_MFRC522_MAX_READERS > 0u,
              "HAL_MFRC522_MAX_READERS must be positive");
static_assert(HAL_MFRC522_MAX_READERS <= 255u,
              "HAL_MFRC522_MAX_READERS exceeds handle capacity");

static mfrc522_transport_context_t
    s_transport_contexts[HAL_MFRC522_MAX_TRANSPORTS];
static mfrc522_reader_context_t s_reader_contexts[HAL_MFRC522_MAX_READERS];
static jh_handle_slot_t s_transport_handle_slots[HAL_MFRC522_MAX_TRANSPORTS];
static jh_handle_slot_t s_reader_handle_slots[HAL_MFRC522_MAX_READERS];
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
        COUNTOF(s_transport_handle_slots), JH_MFRC522_TRANSPORT_HANDLE_KIND);
    if (status == HAL_OK) {
      status = jh_handle_pool_init(&s_reader_handle_pool, s_reader_handle_slots,
                                   COUNTOF(s_reader_handle_slots),
                                   JH_MFRC522_READER_HANDLE_KIND);
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

static mfrc522_transport_context_t *allocate_transport_context() {
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

static mfrc522_reader_context_t *allocate_reader_context() {
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

static void release_transport_context(mfrc522_transport_context_t *context) {
  if (context == nullptr || !context->allocated) {
    return;
  }
  if (context->native != nullptr) {
    (void)context->native->PCD_ReleaseTransportError();
    if (context->kind == mfrc522_transport_kind_t::spi) {
      context->storage.spi.~MFRC522_SPI();
    } else {
      context->storage.i2c.~MFRC522_I2C();
    }
  }
  context->native = nullptr;
  context->reader_count = 0u;
  context->allocated = false;
}

static void release_reader_context(mfrc522_reader_context_t *context) {
  if (context == nullptr || !context->allocated) {
    return;
  }
  if (context->native != nullptr) {
    context->storage.reader.~MFRC522();
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

static hal_status_t acquire_reader(hal_mfrc522_t reader,
                                   jh_handle_lease_t *out_lease,
                                   mfrc522_reader_context_t **out_context) {
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
    *out_context = static_cast<mfrc522_reader_context_t *>(out_lease->token);
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
        static_cast<mfrc522_reader_context_t *>(deferred_token));
  }
  pool_unlock();
  return status;
}

template <typename Operation>
static hal_status_t invoke_reader(hal_mfrc522_t reader, Operation operation,
                                  bool require_begun = true,
                                  bool mark_begun_on_success = false) {
  jh_handle_lease_t lease = {};
  mfrc522_reader_context_t *context = nullptr;
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
    status = context->transport->native->PCD_ClearTransportError();
    if (status == HAL_OK) {
      status = operation(context->native);
      const hal_status_t transport_status =
          context->transport->native->PCD_GetTransportError();
      if (transport_status != HAL_OK) {
        status = transport_status;
      }
      if (status == HAL_OK && mark_begun_on_success) {
        context->begun = true;
      }
    }
  }
  hal_mutex_unlock(context->mutex);
  const hal_status_t end_status = end_reader_operation(&lease);
  return status != HAL_OK ? status : end_status;
}

static hal_status_t protocol_status(MFRC522::StatusCode status) {
  return MFRC522::StatusCodeToHalStatus(status);
}

static void copy_uid_to_c(const MFRC522::Uid &native, hal_mfrc522_uid_t *uid) {
  memset(uid, 0, sizeof(*uid));
  uid->size = native.size;
  uid->sak = native.sak;
  memcpy(uid->bytes, native.uidByte, native.size);
}

static bool uid_storage_valid(const hal_mfrc522_uid_t *uid) {
  return uid != nullptr && uid->size > 0u &&
         uid->size <= HAL_MFRC522_UID_MAX_SIZE;
}

static bool uid_authentication_valid(const hal_mfrc522_uid_t *uid) {
  return uid != nullptr &&
         (uid->size == 4u || uid->size == 7u || uid->size == 10u);
}

static bool native_uid_authentication_valid(const MFRC522::Uid &uid) {
  return uid.size == 4u || uid.size == 7u || uid.size == 10u;
}

static MFRC522::Uid copy_uid_to_native(const hal_mfrc522_uid_t *uid) {
  MFRC522::Uid native = {};
  native.size = uid->size;
  native.sak = uid->sak;
  memcpy(native.uidByte, uid->bytes, uid->size);
  return native;
}

hal_mfrc522_spi_config_t
hal_mfrc522_spi_default_config(uint8_t chip_select_pin) {
  hal_mfrc522_spi_config_t config = {};
  config.chip_select_pin = chip_select_pin;
  config.reset_pin = HAL_MFRC522_PIN_NONE;
  config.spi_bus = 0u;
  config.settings.clock_hz = HAL_SPI_CLOCK_DEFAULT_HZ;
  config.settings.bit_order = HAL_SPI_MSBFIRST;
  config.settings.data_mode = HAL_SPI_MODE0;
  return config;
}

hal_mfrc522_i2c_config_t hal_mfrc522_i2c_default_config(void) {
  hal_mfrc522_i2c_config_t config = {};
  config.reset_pin = HAL_MFRC522_PIN_NONE;
  config.address = HAL_MFRC522_I2C_DEFAULT_ADDRESS;
  config.i2c_bus = 0u;
  return config;
}

hal_status_t
hal_mfrc522_transport_create_spi(const hal_mfrc522_spi_config_t *config,
                                 hal_mfrc522_transport_t *out_transport) {
  if (out_transport != nullptr) {
    *out_transport = nullptr;
  }
  if (config == nullptr || out_transport == nullptr ||
      config->chip_select_pin == HAL_MFRC522_PIN_NONE ||
      !jh_hal_gpio_pin_valid(config->chip_select_pin) ||
      (config->reset_pin != HAL_MFRC522_PIN_NONE &&
       (!jh_hal_gpio_pin_valid(config->reset_pin) ||
        config->reset_pin == config->chip_select_pin)) ||
      config->spi_bus > 1u ||
      (config->settings.bit_order != HAL_SPI_LSBFIRST &&
       config->settings.bit_order != HAL_SPI_MSBFIRST) ||
      config->settings.data_mode > HAL_SPI_MODE3) {
    return HAL_EINVAL;
  }

  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  mfrc522_transport_context_t *context = allocate_transport_context();
  if (context == nullptr) {
    pool_unlock();
    return HAL_ENOMEM;
  }
  context->kind = mfrc522_transport_kind_t::spi;
  context->native = new (&context->storage.spi)
      MFRC522_SPI(config->chip_select_pin, config->reset_pin, config->spi_bus,
                  config->settings);
  void *handle = nullptr;
  status = jh_handle_allocate(&s_transport_handle_pool, context, &handle);
  if (status != HAL_OK) {
    release_transport_context(context);
  } else {
    *out_transport = static_cast<hal_mfrc522_transport_t>(handle);
  }
  pool_unlock();
  return status;
}

#ifdef HAL_ENABLE_I2C
hal_status_t
hal_mfrc522_transport_create_i2c(const hal_mfrc522_i2c_config_t *config,
                                 hal_mfrc522_transport_t *out_transport) {
  if (out_transport != nullptr) {
    *out_transport = nullptr;
  }
  if (config == nullptr || out_transport == nullptr ||
      (config->reset_pin != HAL_MFRC522_PIN_NONE &&
       !jh_hal_gpio_pin_valid(config->reset_pin)) ||
      config->i2c_bus > 1u || config->address == 0u ||
      config->address > 0x7fu) {
    return HAL_EINVAL;
  }

  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  mfrc522_transport_context_t *context = allocate_transport_context();
  if (context == nullptr) {
    pool_unlock();
    return HAL_ENOMEM;
  }
  context->kind = mfrc522_transport_kind_t::i2c;
  context->native = new (&context->storage.i2c)
      MFRC522_I2C(config->reset_pin, config->address, config->i2c_bus);
  void *handle = nullptr;
  status = jh_handle_allocate(&s_transport_handle_pool, context, &handle);
  if (status != HAL_OK) {
    release_transport_context(context);
  } else {
    *out_transport = static_cast<hal_mfrc522_transport_t>(handle);
  }
  pool_unlock();
  return status;
}
#endif

hal_status_t hal_mfrc522_transport_destroy(hal_mfrc522_transport_t transport) {
  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  void *resolved = nullptr;
  status = jh_handle_resolve(&s_transport_handle_pool, transport, &resolved,
                             nullptr);
  auto *context = static_cast<mfrc522_transport_context_t *>(resolved);
  if (status == HAL_OK && context->reader_count != 0u) {
    status = HAL_EBUSY;
  }
  if (status == HAL_OK) {
    void *token = nullptr;
    status = jh_handle_release(&s_transport_handle_pool, transport, &token);
    if (status == HAL_OK) {
      release_transport_context(
          static_cast<mfrc522_transport_context_t *>(token));
    }
  }
  pool_unlock();
  return status;
}

hal_status_t hal_mfrc522_create(hal_mfrc522_transport_t transport,
                                hal_mfrc522_t *out_reader) {
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
  auto *transport_context =
      static_cast<mfrc522_transport_context_t *>(resolved);
  if (status == HAL_OK && transport_context->reader_count != 0u) {
    status = HAL_EBUSY;
  }

  mfrc522_reader_context_t *context = nullptr;
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
        new (&context->storage.reader) MFRC522(transport_context->native);
    ++transport_context->reader_count;
    void *handle = nullptr;
    status = jh_handle_allocate(&s_reader_handle_pool, context, &handle);
    if (status == HAL_OK) {
      *out_reader = static_cast<hal_mfrc522_t>(handle);
    } else {
      release_reader_context(context);
    }
  }
  pool_unlock();
  return status;
}

hal_status_t hal_mfrc522_destroy(hal_mfrc522_t reader) {
  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  void *token = nullptr;
  status = jh_handle_begin_close(&s_reader_handle_pool, reader, &token);
  if (token != nullptr) {
    release_reader_context(static_cast<mfrc522_reader_context_t *>(token));
  }
  pool_unlock();
  return status;
}

hal_status_t hal_mfrc522_begin(hal_mfrc522_t reader) {
  return invoke_reader(
      reader,
      [](MFRC522 *native) {
        native->PCD_Init();
        const uint8_t version = native->PCD_GetVersion();
        return (version == 0x00u || version == 0xffu) ? HAL_ENOENT : HAL_OK;
      },
      false, true);
}

hal_status_t hal_mfrc522_get_version(hal_mfrc522_t reader,
                                     uint8_t *out_version) {
  if (out_version == nullptr) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [out_version](MFRC522 *native) {
    *out_version = native->PCD_GetVersion();
    return HAL_OK;
  });
}

hal_status_t hal_mfrc522_antenna_on(hal_mfrc522_t reader) {
  return invoke_reader(reader, [](MFRC522 *native) {
    native->PCD_AntennaOn();
    return HAL_OK;
  });
}

hal_status_t hal_mfrc522_antenna_off(hal_mfrc522_t reader) {
  return invoke_reader(reader, [](MFRC522 *native) {
    native->PCD_AntennaOff();
    return HAL_OK;
  });
}

hal_status_t hal_mfrc522_get_antenna_gain(hal_mfrc522_t reader,
                                          uint8_t *out_gain) {
  if (out_gain == nullptr) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [out_gain](MFRC522 *native) {
    *out_gain = native->PCD_GetAntennaGain();
    return HAL_OK;
  });
}

hal_status_t hal_mfrc522_set_antenna_gain(hal_mfrc522_t reader, uint8_t gain) {
  return invoke_reader(reader, [gain](MFRC522 *native) {
    native->PCD_SetAntennaGain(gain);
    return HAL_OK;
  });
}

hal_status_t hal_mfrc522_perform_self_test(hal_mfrc522_t reader,
                                           bool *out_passed) {
  if (out_passed == nullptr) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [out_passed](MFRC522 *native) {
    *out_passed = native->PCD_PerformSelfTest();
    return HAL_OK;
  });
}

hal_status_t hal_mfrc522_power_down(hal_mfrc522_t reader) {
  return invoke_reader(reader, [](MFRC522 *native) {
    native->PCD_SoftPowerDown();
    return HAL_OK;
  });
}

hal_status_t hal_mfrc522_power_up(hal_mfrc522_t reader) {
  return invoke_reader(reader, [](MFRC522 *native) {
    native->PCD_SoftPowerUp();
    return HAL_OK;
  });
}

hal_status_t hal_mfrc522_is_new_card_present(hal_mfrc522_t reader,
                                             bool *out_present) {
  if (out_present == nullptr) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [out_present](MFRC522 *native) {
    *out_present = false;
    native->PCD_WriteRegister(MFRC522::TxModeReg, 0x00u);
    native->PCD_WriteRegister(MFRC522::RxModeReg, 0x00u);
    native->PCD_WriteRegister(MFRC522::ModWidthReg, 0x26u);
    uint8_t atqa[2] = {};
    uint8_t atqa_size = sizeof(atqa);
    const MFRC522::StatusCode status = native->PICC_RequestA(atqa, &atqa_size);
    if (status == MFRC522::STATUS_OK || status == MFRC522::STATUS_COLLISION) {
      *out_present = true;
      return HAL_OK;
    }
    return status == MFRC522::STATUS_TIMEOUT ? HAL_OK : protocol_status(status);
  });
}

hal_status_t hal_mfrc522_read_uid(hal_mfrc522_t reader,
                                  hal_mfrc522_uid_t *out_uid) {
  if (out_uid == nullptr) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [out_uid](MFRC522 *native) {
    MFRC522::StatusCode status = native->PICC_Select(&native->uid);
    if (status == MFRC522::STATUS_OK && native->uid.size > 0u &&
        native->uid.size <= HAL_MFRC522_UID_MAX_SIZE) {
      copy_uid_to_c(native->uid, out_uid);
    } else {
      memset(out_uid, 0, sizeof(*out_uid));
      if (status == MFRC522::STATUS_OK) {
        status = MFRC522::STATUS_INTERNAL_ERROR;
      }
    }
    return protocol_status(status);
  });
}

static hal_status_t request_card(hal_mfrc522_t reader, uint8_t out_atqa[2],
                                 bool wakeup) {
  if (out_atqa == nullptr) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [out_atqa, wakeup](MFRC522 *native) {
    uint8_t size = 2u;
    const MFRC522::StatusCode status =
        wakeup ? native->PICC_WakeupA(out_atqa, &size)
               : native->PICC_RequestA(out_atqa, &size);
    return protocol_status(status);
  });
}

hal_status_t hal_mfrc522_request_a(hal_mfrc522_t reader, uint8_t out_atqa[2]) {
  return request_card(reader, out_atqa, false);
}

hal_status_t hal_mfrc522_wakeup_a(hal_mfrc522_t reader, uint8_t out_atqa[2]) {
  return request_card(reader, out_atqa, true);
}

hal_status_t hal_mfrc522_halt(hal_mfrc522_t reader) {
  return invoke_reader(reader, [](MFRC522 *native) {
    return protocol_status(native->PICC_HaltA());
  });
}

hal_status_t hal_mfrc522_mifare_authenticate(
    hal_mfrc522_t reader, hal_mfrc522_key_type_t key_type, uint8_t block,
    const hal_mfrc522_mifare_key_t *key, const hal_mfrc522_uid_t *uid) {
  if (key == nullptr || !uid_authentication_valid(uid) ||
      (key_type != HAL_MFRC522_KEY_A && key_type != HAL_MFRC522_KEY_B)) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [key_type, block, key, uid](MFRC522 *native) {
    MFRC522::MIFARE_Key native_key = {};
    memcpy(native_key.keyByte, key->bytes, sizeof(native_key.keyByte));
    MFRC522::Uid native_uid = copy_uid_to_native(uid);
    const uint8_t command = key_type == HAL_MFRC522_KEY_A
                                ? MFRC522::PICC_CMD_MF_AUTH_KEY_A
                                : MFRC522::PICC_CMD_MF_AUTH_KEY_B;
    return protocol_status(
        native->PCD_Authenticate(command, block, &native_key, &native_uid));
  });
}

hal_status_t hal_mfrc522_mifare_stop_crypto(hal_mfrc522_t reader) {
  return invoke_reader(reader, [](MFRC522 *native) {
    native->PCD_StopCrypto1();
    return HAL_OK;
  });
}

hal_status_t hal_mfrc522_mifare_read(hal_mfrc522_t reader, uint8_t block,
                                     uint8_t *buffer, size_t *inout_size) {
  if (buffer == nullptr || inout_size == nullptr || *inout_size == 0u ||
      *inout_size > UINT8_MAX) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [block, buffer, inout_size](MFRC522 *native) {
    uint8_t native_size = static_cast<uint8_t>(*inout_size);
    const MFRC522::StatusCode status =
        native->MIFARE_Read(block, buffer, &native_size);
    *inout_size = native_size;
    return protocol_status(status);
  });
}

hal_status_t
hal_mfrc522_mifare_write(hal_mfrc522_t reader, uint8_t block,
                         const uint8_t data[HAL_MFRC522_MIFARE_BLOCK_SIZE]) {
  if (data == nullptr) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [block, data](MFRC522 *native) {
    uint8_t copy[HAL_MFRC522_MIFARE_BLOCK_SIZE];
    memcpy(copy, data, sizeof(copy));
    return protocol_status(native->MIFARE_Write(block, copy, sizeof(copy)));
  });
}

hal_status_t hal_mfrc522_mifare_ultralight_write(
    hal_mfrc522_t reader, uint8_t page,
    const uint8_t data[HAL_MFRC522_ULTRALIGHT_PAGE_SIZE]) {
  if (data == nullptr) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [page, data](MFRC522 *native) {
    uint8_t copy[HAL_MFRC522_ULTRALIGHT_PAGE_SIZE];
    memcpy(copy, data, sizeof(copy));
    return protocol_status(
        native->MIFARE_Ultralight_Write(page, copy, sizeof(copy)));
  });
}

hal_status_t hal_mfrc522_mifare_decrement(hal_mfrc522_t reader, uint8_t block,
                                          int32_t delta) {
  return invoke_reader(reader, [block, delta](MFRC522 *native) {
    return protocol_status(native->MIFARE_Decrement(block, delta));
  });
}

hal_status_t hal_mfrc522_mifare_increment(hal_mfrc522_t reader, uint8_t block,
                                          int32_t delta) {
  return invoke_reader(reader, [block, delta](MFRC522 *native) {
    return protocol_status(native->MIFARE_Increment(block, delta));
  });
}

hal_status_t hal_mfrc522_mifare_restore(hal_mfrc522_t reader, uint8_t block) {
  return invoke_reader(reader, [block](MFRC522 *native) {
    return protocol_status(native->MIFARE_Restore(block));
  });
}

hal_status_t hal_mfrc522_mifare_transfer(hal_mfrc522_t reader, uint8_t block) {
  return invoke_reader(reader, [block](MFRC522 *native) {
    return protocol_status(native->MIFARE_Transfer(block));
  });
}

hal_status_t hal_mfrc522_mifare_get_value(hal_mfrc522_t reader, uint8_t block,
                                          int32_t *out_value) {
  if (out_value == nullptr) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [block, out_value](MFRC522 *native) {
    return protocol_status(native->MIFARE_GetValue(block, out_value));
  });
}

hal_status_t hal_mfrc522_mifare_set_value(hal_mfrc522_t reader, uint8_t block,
                                          int32_t value) {
  return invoke_reader(reader, [block, value](MFRC522 *native) {
    return protocol_status(native->MIFARE_SetValue(block, value));
  });
}

hal_status_t hal_mfrc522_ntag216_authenticate(hal_mfrc522_t reader,
                                              const uint8_t password[4],
                                              uint8_t out_pack[2]) {
  if (password == nullptr || out_pack == nullptr) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [password, out_pack](MFRC522 *native) {
    uint8_t copy[4];
    memcpy(copy, password, sizeof(copy));
    return protocol_status(native->PCD_NTAG216_AUTH(copy, out_pack));
  });
}

hal_status_t hal_mfrc522_mifare_set_access_bits(
    hal_mfrc522_t reader, uint8_t out_access[HAL_MFRC522_ACCESS_BITS_SIZE],
    uint8_t g0, uint8_t g1, uint8_t g2, uint8_t g3) {
  if (out_access == nullptr || g0 > 7u || g1 > 7u || g2 > 7u || g3 > 7u) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [out_access, g0, g1, g2, g3](MFRC522 *native) {
    native->MIFARE_SetAccessBits(out_access, g0, g1, g2, g3);
    return HAL_OK;
  });
}

hal_status_t hal_mfrc522_mifare_open_uid_backdoor(hal_mfrc522_t reader,
                                                  bool log_errors) {
  return invoke_reader(reader, [log_errors](MFRC522 *native) {
    return native->MIFARE_OpenUidBackdoor(log_errors) ? HAL_OK : HAL_EIO;
  });
}

hal_status_t hal_mfrc522_mifare_set_uid(hal_mfrc522_t reader,
                                        const hal_mfrc522_uid_t *uid,
                                        bool log_errors) {
  if (!uid_storage_valid(uid)) {
    return HAL_EINVAL;
  }
  return invoke_reader(reader, [uid, log_errors](MFRC522 *native) {
    if (!native_uid_authentication_valid(native->uid)) {
      return HAL_ESTATE;
    }
    uint8_t copy[HAL_MFRC522_UID_MAX_SIZE];
    memcpy(copy, uid->bytes, uid->size);
    return native->MIFARE_SetUid(copy, uid->size, log_errors) ? HAL_OK
                                                              : HAL_EIO;
  });
}

hal_status_t hal_mfrc522_mifare_unbrick_uid_sector(hal_mfrc522_t reader,
                                                   bool log_errors) {
  return invoke_reader(reader, [log_errors](MFRC522 *native) {
    return native->MIFARE_UnbrickUidSector(log_errors) ? HAL_OK : HAL_EIO;
  });
}

hal_mfrc522_card_type_t hal_mfrc522_card_type_from_sak(uint8_t sak) {
  return static_cast<hal_mfrc522_card_type_t>(MFRC522::PICC_GetType(sak));
}

const char *hal_mfrc522_card_type_name(hal_mfrc522_card_type_t type) {
  switch (type) {
  case HAL_MFRC522_CARD_UNKNOWN:
  case HAL_MFRC522_CARD_ISO_14443_4:
  case HAL_MFRC522_CARD_ISO_18092:
  case HAL_MFRC522_CARD_MIFARE_MINI:
  case HAL_MFRC522_CARD_MIFARE_1K:
  case HAL_MFRC522_CARD_MIFARE_4K:
  case HAL_MFRC522_CARD_MIFARE_ULTRALIGHT:
  case HAL_MFRC522_CARD_MIFARE_PLUS:
  case HAL_MFRC522_CARD_MIFARE_DESFIRE:
  case HAL_MFRC522_CARD_TNP3XXX:
  case HAL_MFRC522_CARD_UID_INCOMPLETE:
    break;
  default:
    return MFRC522::PICC_GetTypeName(MFRC522::PICC_TYPE_UNKNOWN);
  }
  return MFRC522::PICC_GetTypeName(static_cast<MFRC522::PICC_Type>(type));
}

#endif /* HAL_ENABLE_MFRC522 */
