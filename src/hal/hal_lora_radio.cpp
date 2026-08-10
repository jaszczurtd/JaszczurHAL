#include "hal_lora_radio.h"

#ifdef HAL_ENABLE_LORA

#include "hal_board.h"
#include "hal_system.h"
#include "impl/shared/drivers/sx126x/jh_sx126x_adapter.h"
#include "impl/shared/hal_mutex_once.h"
#include "impl/shared/jh_board_runtime.h"
#include "impl/shared/jh_handle_pool.h"
#include "impl/shared/radio/jh_lora_radio_internal.h"

#include <string.h>

#define JH_LORA_HANDLE_KIND 8u
#define JH_LORA_AUTO_TX_MARGIN_MS UINT32_C(1000)

static jh_lora_radio_context_t s_contexts[HAL_LORA_RADIO_MAX_INSTANCES] = {};
static jh_handle_slot_t s_handle_slots[HAL_LORA_RADIO_MAX_INSTANCES] = {};
static jh_handle_pool_t s_handle_pool = {};
static hal_mutex_t s_pool_mutex = NULL;
static bool s_pool_initialized = false;
static const jh_lora_radio_provider_ops_t *s_provider = NULL;

static bool provider_valid(const jh_lora_radio_provider_ops_t *provider) {
  return provider != NULL && provider->initialize != NULL &&
         provider->deinitialize != NULL && provider->configure != NULL &&
         provider->transmit_start != NULL && provider->receive_start != NULL &&
         provider->process != NULL && provider->cancel != NULL &&
         provider->sleep != NULL && provider->standby != NULL;
}

static hal_status_t pool_lock(void) {
  hal_mutex_t mutex = jh_hal_mutex_create_once(&s_pool_mutex);
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_pool_initialized) {
    const hal_status_t status =
        jh_handle_pool_init(&s_handle_pool, s_handle_slots,
                            HAL_LORA_RADIO_MAX_INSTANCES, JH_LORA_HANDLE_KIND);
    if (status != HAL_OK) {
      hal_mutex_unlock(mutex);
      return status;
    }
    s_provider = jh_lora_radio_default_provider();
    s_pool_initialized = true;
  }
  return HAL_OK;
}

static void pool_unlock(void) { hal_mutex_unlock(s_pool_mutex); }

static bool enum_config_valid(const hal_lora_radio_config_t *config) {
  const hal_lora_sx126x_hardware_config_t *hardware = &config->hardware.sx126x;
  if (config->model != HAL_LORA_RADIO_SX1262 || config->spi_bus > 1u) {
    return false;
  }
  switch (hardware->rf_switch_mode) {
  case HAL_LORA_RF_SWITCH_NONE:
  case HAL_LORA_RF_SWITCH_DIO2:
  case HAL_LORA_RF_SWITCH_SINGLE_GPIO:
  case HAL_LORA_RF_SWITCH_DIO2_SINGLE_GPIO:
  case HAL_LORA_RF_SWITCH_DUAL_GPIO:
    break;
  default:
    return false;
  }
  if (hardware->regulator_mode != HAL_LORA_REGULATOR_LDO &&
      hardware->regulator_mode != HAL_LORA_REGULATOR_DCDC) {
    return false;
  }
  if (hardware->tcxo_control != HAL_LORA_TCXO_CONTROL_NONE &&
      hardware->tcxo_control != HAL_LORA_TCXO_CONTROL_DIO3) {
    return false;
  }
  return hardware->tcxo_control == HAL_LORA_TCXO_CONTROL_NONE ||
         (hardware->tcxo_voltage >= HAL_LORA_TCXO_1V6 &&
          hardware->tcxo_voltage <= HAL_LORA_TCXO_3V3 &&
          hardware->tcxo_startup_us > 0u);
}

static bool
rf_switch_pins_valid(const hal_lora_sx126x_hardware_config_t *hardware) {
  switch (hardware->rf_switch_mode) {
  case HAL_LORA_RF_SWITCH_NONE:
  case HAL_LORA_RF_SWITCH_DIO2:
    return hardware->rf_switch_pin_a == HAL_LORA_PIN_NONE &&
           hardware->rf_switch_pin_b == HAL_LORA_PIN_NONE;
  case HAL_LORA_RF_SWITCH_SINGLE_GPIO:
  case HAL_LORA_RF_SWITCH_DIO2_SINGLE_GPIO:
    return hardware->rf_switch_pin_a != HAL_LORA_PIN_NONE &&
           hardware->rf_switch_pin_b == HAL_LORA_PIN_NONE;
  case HAL_LORA_RF_SWITCH_DUAL_GPIO:
    return hardware->rf_switch_pin_a != HAL_LORA_PIN_NONE &&
           hardware->rf_switch_pin_b != HAL_LORA_PIN_NONE;
  default:
    return false;
  }
}

static bool required_pins_valid(const hal_lora_radio_config_t *config) {
  const hal_lora_sx126x_hardware_config_t *hardware = &config->hardware.sx126x;
  const uint8_t pins[] = {
      config->spi_miso_pin,      config->spi_mosi_pin,
      config->spi_sck_pin,       config->cs_pin,
      hardware->reset_pin,       hardware->dio1_pin,
      hardware->busy_pin,        hardware->rf_switch_pin_a,
      hardware->rf_switch_pin_b,
  };
  for (size_t index = 0u; index < 7u; ++index) {
    if (pins[index] == HAL_LORA_PIN_NONE) {
      return false;
    }
  }
  for (size_t left = 0u; left < sizeof(pins) / sizeof(pins[0]); ++left) {
    if (pins[left] == HAL_LORA_PIN_NONE) {
      continue;
    }
    for (size_t right = left + 1u; right < sizeof(pins) / sizeof(pins[0]);
         ++right) {
      if (pins[left] == pins[right]) {
        return false;
      }
    }
  }
  return true;
}

static bool limits_valid(const hal_lora_radio_config_t *config) {
  const hal_lora_sx126x_hardware_config_t *hardware = &config->hardware.sx126x;
  const uint32_t spi_clock_hz = config->spi_clock_hz == 0u
                                    ? HAL_LORA_SPI_CLOCK_DEFAULT_HZ
                                    : config->spi_clock_hz;
  return hardware->min_frequency_hz > 0u &&
         hardware->min_frequency_hz <= hardware->max_frequency_hz &&
         hardware->max_spi_clock_hz > 0u &&
         spi_clock_hz <= hardware->max_spi_clock_hz &&
         hardware->min_tx_power_dbm <= hardware->max_tx_power_dbm;
}

static bool config_valid(const hal_lora_radio_config_t *config) {
  return config != NULL && enum_config_valid(config) &&
         rf_switch_pins_valid(&config->hardware.sx126x) &&
         required_pins_valid(config) && limits_valid(config);
}

static void clear_context(jh_lora_radio_context_t *context) {
  hal_mutex_t mutex = context->mutex;
  memset(context, 0, sizeof(*context));
  context->mutex = mutex;
  context->state = HAL_LORA_RADIO_STATE_ERROR;
  context->tx_status.state = HAL_LORA_OPERATION_IDLE;
  context->tx_status.result = HAL_NONE;
  context->rx_result = HAL_NONE;
  context->diagnostics.last_error = HAL_NONE;
}

static bool board_config_matches(const hal_lora_radio_config_t *config) {
#if HAL_BOARD_LORA_RADIO_PRESENT
  return config->spi_bus == HAL_BOARD_LORA_RADIO_SPI_BUS &&
         config->spi_miso_pin == HAL_BOARD_LORA_RADIO_PIN_MISO &&
         config->spi_mosi_pin == HAL_BOARD_LORA_RADIO_PIN_MOSI &&
         config->spi_sck_pin == HAL_BOARD_LORA_RADIO_PIN_SCK &&
         config->cs_pin == HAL_BOARD_LORA_RADIO_PIN_CS &&
         config->hardware.sx126x.reset_pin == HAL_BOARD_LORA_RADIO_PIN_RESET &&
         config->hardware.sx126x.dio1_pin == HAL_BOARD_LORA_RADIO_PIN_DIO1 &&
         config->hardware.sx126x.busy_pin == HAL_BOARD_LORA_RADIO_PIN_BUSY;
#else
  (void)config;
  return false;
#endif
}

static void set_board_radio_state(bool board_device, hal_status_t status) {
#if HAL_BOARD_LORA_RADIO_PRESENT
  if (!board_device) {
    return;
  }
  if (status == HAL_OK) {
    (void)jh_board_runtime_set_available(HAL_BOARD_CAP_SX1262_RADIO);
  } else if (status == HAL_EUNINIT) {
    (void)jh_board_runtime_set_inactive(HAL_BOARD_CAP_SX1262_RADIO);
  } else {
    (void)jh_board_runtime_set_failed(HAL_BOARD_CAP_SX1262_RADIO);
  }
#else
  (void)board_device;
  (void)status;
#endif
}

static hal_status_t allocate_context(const hal_lora_radio_config_t *config,
                                     hal_lora_radio_t *out_radio,
                                     jh_lora_radio_context_t **out_context) {
  const hal_status_t lock_status = pool_lock();
  if (lock_status != HAL_OK) {
    return lock_status;
  }

  jh_lora_radio_context_t *context = NULL;
  for (size_t index = 0u; index < HAL_LORA_RADIO_MAX_INSTANCES; ++index) {
    if (!s_contexts[index].allocated) {
      context = &s_contexts[index];
      break;
    }
  }
  if (context == NULL || !provider_valid(s_provider)) {
    pool_unlock();
    return context == NULL ? HAL_ENOMEM : HAL_ECONFIG;
  }
  if (jh_hal_mutex_create_once(&context->mutex) == NULL) {
    pool_unlock();
    return HAL_ENOMEM;
  }

  clear_context(context);
  context->allocated = true;
  context->config = *config;
  context->provider = s_provider;
  context->board_device = board_config_matches(config);
  void *handle = NULL;
  const hal_status_t status =
      jh_handle_allocate(&s_handle_pool, context, &handle);
  if (status != HAL_OK) {
    clear_context(context);
    pool_unlock();
    return status;
  }
  pool_unlock();

  hal_mutex_lock(context->mutex);
  *out_radio = reinterpret_cast<hal_lora_radio_t>(handle);
  context->handle = *out_radio;
  *out_context = context;
  return HAL_OK;
}

static void abandon_context(hal_lora_radio_t radio,
                            jh_lora_radio_context_t *context) {
  void *released = NULL;
  if (pool_lock() == HAL_OK) {
    (void)jh_handle_release(&s_handle_pool, radio, &released);
    clear_context(context);
    pool_unlock();
  }
  hal_mutex_unlock(context->mutex);
}

hal_status_t jh_lora_radio_context_lock(hal_lora_radio_t radio,
                                        jh_lora_radio_context_t **out_context) {
  if (out_context == NULL) {
    return HAL_EINVAL;
  }
  *out_context = NULL;
  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  void *token = NULL;
  status = jh_handle_resolve(&s_handle_pool, radio, &token, NULL);
  pool_unlock();
  if (status != HAL_OK || token == NULL) {
    return HAL_EUNINIT;
  }

  jh_lora_radio_context_t *context =
      static_cast<jh_lora_radio_context_t *>(token);
  hal_mutex_lock(context->mutex);
  status = pool_lock();
  if (status != HAL_OK) {
    hal_mutex_unlock(context->mutex);
    return status;
  }
  void *verified = NULL;
  status = jh_handle_resolve(&s_handle_pool, radio, &verified, NULL);
  pool_unlock();
  if (status != HAL_OK || verified != context || !context->allocated) {
    hal_mutex_unlock(context->mutex);
    return HAL_EUNINIT;
  }
  *out_context = context;
  return HAL_OK;
}

void jh_lora_radio_context_unlock(jh_lora_radio_context_t *context) {
  if (context != NULL) {
    hal_mutex_unlock(context->mutex);
  }
}

static void record_error(jh_lora_radio_context_t *context,
                         hal_status_t status) {
  if (status == HAL_OK || status == HAL_EAGAIN || status == HAL_EOVERFLOW) {
    return;
  }
  context->diagnostics.last_error = status;
  if (status == HAL_EBUS || status == HAL_EIO || status == HAL_EHW) {
    ++context->diagnostics.bus_errors;
  }
  if (status != HAL_ETIMEOUT && status != HAL_ECANCELED &&
      status != HAL_EPROTO) {
    ++context->diagnostics.operation_errors;
  }
}

static bool state_transition_valid(hal_lora_radio_state_t from,
                                   hal_lora_radio_state_t to) {
  if (from == to || to == HAL_LORA_RADIO_STATE_ERROR) {
    return true;
  }
  switch (from) {
  case HAL_LORA_RADIO_STATE_STANDBY:
    return to == HAL_LORA_RADIO_STATE_RX || to == HAL_LORA_RADIO_STATE_TX ||
           to == HAL_LORA_RADIO_STATE_CAD || to == HAL_LORA_RADIO_STATE_SLEEP;
  case HAL_LORA_RADIO_STATE_RX:
  case HAL_LORA_RADIO_STATE_TX:
  case HAL_LORA_RADIO_STATE_CAD:
  case HAL_LORA_RADIO_STATE_SLEEP:
  case HAL_LORA_RADIO_STATE_ERROR:
    return to == HAL_LORA_RADIO_STATE_STANDBY;
  default:
    return false;
  }
}

static void set_state(jh_lora_radio_context_t *context,
                      hal_lora_radio_state_t state) {
  if (!state_transition_valid(context->state, state)) {
    context->state = HAL_LORA_RADIO_STATE_ERROR;
    context->diagnostics.last_error = HAL_EINTERNAL;
    ++context->diagnostics.operation_errors;
  } else {
    context->state = state;
  }
  context->diagnostics.last_state_change_ms = hal_millis();
}

static void queue_event(jh_lora_radio_context_t *context,
                        hal_lora_radio_event_type_t type,
                        hal_lora_operation_kind_t operation,
                        hal_status_t result) {
  const uint32_t now = hal_millis();
  context->diagnostics.last_event_timestamp_ms = now;
  if (context->event_callback == NULL) {
    return;
  }
  if (context->event_pending) {
    ++context->diagnostics.dropped_events;
  }
  context->pending_event.type = type;
  context->pending_event.operation = operation;
  context->pending_event.result = result;
  context->pending_event.timestamp_ms = now;
  context->event_pending = true;
}

hal_status_t
hal_lora_radio_config_from_board(hal_lora_radio_config_t *out_config) {
  if (out_config == NULL) {
    return HAL_EINVAL;
  }
  memset(out_config, 0, sizeof(*out_config));
#if !HAL_BOARD_LORA_RADIO_PRESENT
  return HAL_EUNSUPPORTED;
#else
  out_config->model = HAL_LORA_RADIO_SX1262;
  out_config->spi_bus = HAL_BOARD_LORA_RADIO_SPI_BUS;
  out_config->spi_miso_pin = HAL_BOARD_LORA_RADIO_PIN_MISO;
  out_config->spi_mosi_pin = HAL_BOARD_LORA_RADIO_PIN_MOSI;
  out_config->spi_sck_pin = HAL_BOARD_LORA_RADIO_PIN_SCK;
  out_config->cs_pin = HAL_BOARD_LORA_RADIO_PIN_CS;
  out_config->spi_clock_hz = HAL_BOARD_LORA_RADIO_DEFAULT_SPI_CLOCK_HZ;
  hal_lora_sx126x_hardware_config_t *hardware = &out_config->hardware.sx126x;
  hardware->reset_pin = HAL_BOARD_LORA_RADIO_PIN_RESET;
  hardware->dio1_pin = HAL_BOARD_LORA_RADIO_PIN_DIO1;
  hardware->busy_pin = HAL_BOARD_LORA_RADIO_PIN_BUSY;
#if HAL_BOARD_LORA_RADIO_RF_SWITCH_MODE_IS_NONE
  hardware->rf_switch_mode = HAL_LORA_RF_SWITCH_NONE;
#elif HAL_BOARD_LORA_RADIO_RF_SWITCH_MODE_IS_DIO2
  hardware->rf_switch_mode = HAL_LORA_RF_SWITCH_DIO2;
#elif HAL_BOARD_LORA_RADIO_RF_SWITCH_MODE_IS_SINGLE_GPIO
  hardware->rf_switch_mode = HAL_LORA_RF_SWITCH_SINGLE_GPIO;
#elif HAL_BOARD_LORA_RADIO_RF_SWITCH_MODE_IS_DIO2_SINGLE_GPIO
  hardware->rf_switch_mode = HAL_LORA_RF_SWITCH_DIO2_SINGLE_GPIO;
#else
  hardware->rf_switch_mode = HAL_LORA_RF_SWITCH_DUAL_GPIO;
#endif
  hardware->rf_switch_pin_a = HAL_BOARD_LORA_RADIO_PIN_RF_SWITCH_A;
  hardware->rf_switch_pin_b = HAL_BOARD_LORA_RADIO_PIN_RF_SWITCH_B;
  hardware->rf_switch_idle_level_a =
      HAL_BOARD_LORA_RADIO_RF_SWITCH_IDLE_LEVEL_A != 0;
  hardware->rf_switch_rx_level_a =
      HAL_BOARD_LORA_RADIO_RF_SWITCH_RX_LEVEL_A != 0;
  hardware->rf_switch_tx_level_a =
      HAL_BOARD_LORA_RADIO_RF_SWITCH_TX_LEVEL_A != 0;
#if HAL_BOARD_LORA_RADIO_RF_SWITCH_MODE_IS_DUAL_GPIO
  hardware->rf_switch_idle_level_b =
      HAL_BOARD_LORA_RADIO_RF_SWITCH_IDLE_LEVEL_B != 0;
  hardware->rf_switch_rx_level_b =
      HAL_BOARD_LORA_RADIO_RF_SWITCH_RX_LEVEL_B != 0;
  hardware->rf_switch_tx_level_b =
      HAL_BOARD_LORA_RADIO_RF_SWITCH_TX_LEVEL_B != 0;
#endif
  hardware->regulator_mode = HAL_BOARD_LORA_RADIO_REGULATOR_IS_DCDC
                                 ? HAL_LORA_REGULATOR_DCDC
                                 : HAL_LORA_REGULATOR_LDO;
  hardware->tcxo_control = HAL_BOARD_LORA_RADIO_TCXO_CONTROL_IS_DIO3
                               ? HAL_LORA_TCXO_CONTROL_DIO3
                               : HAL_LORA_TCXO_CONTROL_NONE;
#if HAL_BOARD_LORA_RADIO_TCXO_CONTROL_IS_DIO3
#if HAL_BOARD_LORA_RADIO_TCXO_VOLTAGE_IS_1V6
  hardware->tcxo_voltage = HAL_LORA_TCXO_1V6;
#elif HAL_BOARD_LORA_RADIO_TCXO_VOLTAGE_IS_1V7
  hardware->tcxo_voltage = HAL_LORA_TCXO_1V7;
#elif HAL_BOARD_LORA_RADIO_TCXO_VOLTAGE_IS_1V8
  hardware->tcxo_voltage = HAL_LORA_TCXO_1V8;
#elif HAL_BOARD_LORA_RADIO_TCXO_VOLTAGE_IS_2V2
  hardware->tcxo_voltage = HAL_LORA_TCXO_2V2;
#elif HAL_BOARD_LORA_RADIO_TCXO_VOLTAGE_IS_2V4
  hardware->tcxo_voltage = HAL_LORA_TCXO_2V4;
#elif HAL_BOARD_LORA_RADIO_TCXO_VOLTAGE_IS_2V7
  hardware->tcxo_voltage = HAL_LORA_TCXO_2V7;
#elif HAL_BOARD_LORA_RADIO_TCXO_VOLTAGE_IS_3V0
  hardware->tcxo_voltage = HAL_LORA_TCXO_3V0;
#else
  hardware->tcxo_voltage = HAL_LORA_TCXO_3V3;
#endif
  hardware->tcxo_startup_us = HAL_BOARD_LORA_RADIO_TCXO_STARTUP_US;
#else
  hardware->tcxo_voltage = HAL_LORA_TCXO_1V6;
  hardware->tcxo_startup_us = 0u;
#endif
  hardware->min_frequency_hz = HAL_BOARD_LORA_RADIO_MIN_FREQUENCY_HZ;
  hardware->max_frequency_hz = HAL_BOARD_LORA_RADIO_MAX_FREQUENCY_HZ;
  hardware->max_spi_clock_hz = HAL_BOARD_LORA_RADIO_MAX_SPI_CLOCK_HZ;
  hardware->min_tx_power_dbm = HAL_BOARD_LORA_RADIO_MIN_TX_POWER_DBM;
  hardware->max_tx_power_dbm = HAL_BOARD_LORA_RADIO_MAX_TX_POWER_DBM;
  return HAL_OK;
#endif
}

hal_status_t hal_lora_sx126x_core1262_hf_defaults(
    hal_lora_sx126x_hardware_config_t *out_hardware) {
  if (out_hardware == NULL) {
    return HAL_EINVAL;
  }
  memset(out_hardware, 0, sizeof(*out_hardware));
  out_hardware->reset_pin = HAL_LORA_PIN_NONE;
  out_hardware->dio1_pin = HAL_LORA_PIN_NONE;
  out_hardware->busy_pin = HAL_LORA_PIN_NONE;
  out_hardware->rf_switch_mode = HAL_LORA_RF_SWITCH_DUAL_GPIO;
  out_hardware->rf_switch_pin_a = HAL_LORA_PIN_NONE;
  out_hardware->rf_switch_pin_b = HAL_LORA_PIN_NONE;
  out_hardware->rf_switch_rx_level_b = true;
  out_hardware->rf_switch_tx_level_a = true;
  out_hardware->regulator_mode = HAL_LORA_REGULATOR_DCDC;
  out_hardware->tcxo_control = HAL_LORA_TCXO_CONTROL_DIO3;
  out_hardware->tcxo_voltage = HAL_LORA_TCXO_1V8;
  out_hardware->tcxo_startup_us = UINT32_C(5000);
  out_hardware->min_frequency_hz = UINT32_C(850000000);
  out_hardware->max_frequency_hz = UINT32_C(930000000);
  out_hardware->max_spi_clock_hz = UINT32_C(18000000);
  out_hardware->min_tx_power_dbm = -9;
  out_hardware->max_tx_power_dbm = 22;
  return HAL_OK;
}

hal_status_t hal_lora_radio_create(const hal_lora_radio_config_t *config,
                                   hal_lora_radio_t *out_radio) {
  if (out_radio != NULL) {
    *out_radio = NULL;
  }
  if (out_radio == NULL || !config_valid(config)) {
    return HAL_EINVAL;
  }

  jh_lora_radio_context_t *context = NULL;
  hal_status_t status = allocate_context(config, out_radio, &context);
  if (status != HAL_OK) {
    return status;
  }
  status = context->provider->initialize(context);
  if (status != HAL_OK) {
    const bool board_device = context->board_device;
    hal_lora_radio_t failed_handle = *out_radio;
    *out_radio = NULL;
    set_board_radio_state(board_device, status);
    abandon_context(failed_handle, context);
    return status;
  }
  set_state(context, HAL_LORA_RADIO_STATE_STANDBY);
  context->diagnostics.resets = 1u;
  set_board_radio_state(context->board_device, HAL_OK);
  hal_mutex_unlock(context->mutex);
  return HAL_OK;
}

hal_status_t hal_lora_radio_destroy(hal_lora_radio_t radio) {
  jh_lora_radio_context_t *context = NULL;
  hal_status_t status = jh_lora_radio_context_lock(radio, &context);
  if (status != HAL_OK) {
    return status;
  }
  if (context->operation_busy || context->state == HAL_LORA_RADIO_STATE_TX ||
      context->state == HAL_LORA_RADIO_STATE_RX ||
      context->state == HAL_LORA_RADIO_STATE_CAD) {
    jh_lora_radio_context_unlock(context);
    return HAL_EBUSY;
  }

  void *released = NULL;
  status = pool_lock();
  if (status == HAL_OK) {
    status = jh_handle_release(&s_handle_pool, radio, &released);
    pool_unlock();
  }
  if (status != HAL_OK || released != context) {
    jh_lora_radio_context_unlock(context);
    return status == HAL_OK ? HAL_EINTERNAL : status;
  }

  const bool board_device = context->board_device;
  const hal_status_t provider_status = context->provider->deinitialize(context);
  status = pool_lock();
  if (status == HAL_OK) {
    clear_context(context);
    pool_unlock();
  }
  jh_lora_radio_context_unlock(context);
  set_board_radio_state(board_device, HAL_EUNINIT);
  return provider_status != HAL_OK ? provider_status : status;
}

hal_status_t hal_lora_radio_configure(hal_lora_radio_t radio,
                                      const hal_lora_modem_config_t *config) {
  jh_lora_radio_context_t *context = NULL;
  hal_status_t status = jh_lora_radio_context_lock(radio, &context);
  if (status != HAL_OK) {
    return status;
  }
  if (!jh_sx126x_modem_config_valid(config, &context->config.hardware.sx126x)) {
    jh_lora_radio_context_unlock(context);
    return HAL_EINVAL;
  }
  if (context->operation_busy || context->state == HAL_LORA_RADIO_STATE_RX ||
      context->state == HAL_LORA_RADIO_STATE_TX ||
      context->state == HAL_LORA_RADIO_STATE_CAD) {
    jh_lora_radio_context_unlock(context);
    return HAL_EBUSY;
  }
  const hal_lora_modem_config_t previous = context->modem;
  const bool previously_configured = context->configured;
  context->modem = *config;
  context->operation_busy = true;
  jh_lora_radio_context_unlock(context);

  status = context->provider->configure(context);
  hal_mutex_lock(context->mutex);
  context->operation_busy = false;
  if (status == HAL_OK) {
    context->configured = true;
    set_state(context, HAL_LORA_RADIO_STATE_STANDBY);
  } else {
    context->modem = previous;
    context->configured = previously_configured;
    set_state(context, HAL_LORA_RADIO_STATE_ERROR);
    record_error(context, status);
  }
  hal_mutex_unlock(context->mutex);
  return status;
}

static hal_lora_modem_config_t eu868_preset(uint8_t spreading_factor) {
  hal_lora_modem_config_t config = {};
  config.frequency_hz = UINT32_C(868100000);
  config.bandwidth_hz = UINT32_C(125000);
  config.spreading_factor = spreading_factor;
  config.coding_rate = 5u;
  config.tx_power_dbm = 14;
  config.preamble_symbols = 8u;
  config.sync_word = 0x12u;
  config.explicit_header = true;
  config.crc_enabled = true;
  return config;
}

hal_lora_modem_config_t hal_lora_default_eu868(void) {
  return eu868_preset(9u);
}

hal_lora_modem_config_t hal_lora_default_long_range_eu868(void) {
  return eu868_preset(12u);
}

hal_lora_modem_config_t hal_lora_default_fast_eu868(void) {
  return eu868_preset(7u);
}

static hal_status_t auto_tx_timeout(const hal_lora_modem_config_t *modem,
                                    size_t length, uint32_t *out_timeout_ms) {
  uint32_t airtime_ms = 0u;
  const hal_status_t status = jh_sx126x_time_on_air(modem, length, &airtime_ms);
  if (status != HAL_OK) {
    return status;
  }
  if (airtime_ms > UINT32_MAX - JH_LORA_AUTO_TX_MARGIN_MS) {
    return HAL_EOVERFLOW;
  }
  *out_timeout_ms = airtime_ms + JH_LORA_AUTO_TX_MARGIN_MS;
  return HAL_OK;
}

static void complete_tx_locked(jh_lora_radio_context_t *context,
                               hal_status_t status) {
  if (status == HAL_OK) {
    context->tx_status.state = HAL_LORA_OPERATION_SUCCEEDED;
    context->tx_status.result = HAL_OK;
    ++context->diagnostics.transmitted_packets;
    set_state(context, HAL_LORA_RADIO_STATE_STANDBY);
    queue_event(context, HAL_LORA_RADIO_EVENT_TX_COMPLETE,
                HAL_LORA_OPERATION_KIND_TRANSMIT, HAL_OK);
    return;
  }
  if (status == HAL_ETIMEOUT) {
    context->tx_status.state = HAL_LORA_OPERATION_TIMED_OUT;
    context->tx_status.result = HAL_ETIMEOUT;
    ++context->diagnostics.tx_timeouts;
    set_state(context, HAL_LORA_RADIO_STATE_STANDBY);
    record_error(context, status);
    queue_event(context, HAL_LORA_RADIO_EVENT_TIMEOUT,
                HAL_LORA_OPERATION_KIND_TRANSMIT, status);
    return;
  }
  if (status == HAL_ECANCELED) {
    context->tx_status.state = HAL_LORA_OPERATION_CANCELLED;
    context->tx_status.result = HAL_ECANCELED;
    ++context->diagnostics.cancelled_operations;
    set_state(context, HAL_LORA_RADIO_STATE_STANDBY);
    queue_event(context, HAL_LORA_RADIO_EVENT_CANCELLED,
                HAL_LORA_OPERATION_KIND_TRANSMIT, status);
    return;
  }
  context->tx_status.state = HAL_LORA_OPERATION_FAILED;
  context->tx_status.result = status;
  set_state(context, HAL_LORA_RADIO_STATE_ERROR);
  record_error(context, status);
  queue_event(context, HAL_LORA_RADIO_EVENT_ERROR,
              HAL_LORA_OPERATION_KIND_TRANSMIT, status);
}

static void complete_rx_locked(jh_lora_radio_context_t *context,
                               hal_status_t status,
                               jh_lora_provider_events_t events) {
  if (status != HAL_OK && status != HAL_ETIMEOUT && status != HAL_EPROTO) {
    context->rx_result = status;
    set_state(context, HAL_LORA_RADIO_STATE_ERROR);
    record_error(context, status);
    queue_event(context, HAL_LORA_RADIO_EVENT_ERROR,
                HAL_LORA_OPERATION_KIND_RECEIVE, status);
    return;
  }
  if ((events & (JH_LORA_PROVIDER_EVENT_CRC_ERROR |
                 JH_LORA_PROVIDER_EVENT_HEADER_ERROR)) != 0u) {
    if ((events & JH_LORA_PROVIDER_EVENT_CRC_ERROR) != 0u) {
      ++context->diagnostics.crc_errors;
    }
    if ((events & JH_LORA_PROVIDER_EVENT_HEADER_ERROR) != 0u) {
      ++context->diagnostics.header_errors;
    }
    context->rx_result = HAL_EPROTO;
    if (!context->receive_continuous) {
      set_state(context, HAL_LORA_RADIO_STATE_STANDBY);
    }
    record_error(context, HAL_EPROTO);
    queue_event(context, HAL_LORA_RADIO_EVENT_ERROR,
                HAL_LORA_OPERATION_KIND_RECEIVE, HAL_EPROTO);
    return;
  }
  if ((events & JH_LORA_PROVIDER_EVENT_RX_DONE) != 0u && status == HAL_OK) {
    context->rx_result = HAL_OK;
    if (!context->receive_continuous) {
      set_state(context, HAL_LORA_RADIO_STATE_STANDBY);
    }
    queue_event(context, HAL_LORA_RADIO_EVENT_RX_READY,
                HAL_LORA_OPERATION_KIND_RECEIVE, HAL_OK);
    return;
  }
  if ((events & JH_LORA_PROVIDER_EVENT_TIMEOUT) != 0u ||
      status == HAL_ETIMEOUT) {
    context->rx_result = HAL_ETIMEOUT;
    ++context->diagnostics.rx_timeouts;
    set_state(context, HAL_LORA_RADIO_STATE_STANDBY);
    record_error(context, HAL_ETIMEOUT);
    queue_event(context, HAL_LORA_RADIO_EVENT_TIMEOUT,
                HAL_LORA_OPERATION_KIND_RECEIVE, HAL_ETIMEOUT);
    return;
  }
  context->rx_result = status;
  set_state(context, HAL_LORA_RADIO_STATE_ERROR);
  record_error(context, status);
  queue_event(context, HAL_LORA_RADIO_EVENT_ERROR,
              HAL_LORA_OPERATION_KIND_RECEIVE, status);
}

static hal_status_t service_active_operation(hal_lora_radio_t radio) {
  jh_lora_radio_context_t *context = NULL;
  hal_status_t status = jh_lora_radio_context_lock(radio, &context);
  if (status != HAL_OK) {
    return status;
  }
  if (context->operation_busy) {
    jh_lora_radio_context_unlock(context);
    return HAL_EBUSY;
  }
  const hal_lora_radio_state_t active_state = context->state;
  if (active_state != HAL_LORA_RADIO_STATE_TX &&
      active_state != HAL_LORA_RADIO_STATE_RX) {
    jh_lora_radio_context_unlock(context);
    return HAL_OK;
  }
  if (active_state == HAL_LORA_RADIO_STATE_RX &&
      (context->rx_ready || context->rx_result != HAL_EAGAIN)) {
    status = context->rx_result;
    jh_lora_radio_context_unlock(context);
    return status;
  }
  context->operation_busy = true;
  const jh_lora_radio_provider_ops_t *provider = context->provider;
  jh_lora_radio_context_unlock(context);

  jh_lora_provider_events_t events = JH_LORA_PROVIDER_EVENT_NONE;
  status = provider->process(context, &events);
  hal_mutex_lock(context->mutex);
  context->operation_busy = false;
  if ((events & JH_LORA_PROVIDER_EVENT_IRQ) != 0u) {
    ++context->diagnostics.irq_events;
  }
  if (status == HAL_EAGAIN) {
    hal_mutex_unlock(context->mutex);
    return HAL_EAGAIN;
  }
  if (active_state == HAL_LORA_RADIO_STATE_TX) {
    if (status == HAL_OK && (events & JH_LORA_PROVIDER_EVENT_TIMEOUT) != 0u) {
      status = HAL_ETIMEOUT;
      complete_tx_locked(context, status);
    } else if (status == HAL_OK &&
               (events & JH_LORA_PROVIDER_EVENT_TX_DONE) == 0u) {
      status = HAL_EAGAIN;
    } else {
      complete_tx_locked(context, status);
    }
  } else if (status != HAL_OK ||
             (events &
              (JH_LORA_PROVIDER_EVENT_RX_DONE | JH_LORA_PROVIDER_EVENT_TIMEOUT |
               JH_LORA_PROVIDER_EVENT_CRC_ERROR |
               JH_LORA_PROVIDER_EVENT_HEADER_ERROR)) != 0u) {
    if (status == HAL_OK && (events & JH_LORA_PROVIDER_EVENT_TIMEOUT) != 0u) {
      status = HAL_ETIMEOUT;
    } else if (status == HAL_OK &&
               (events & (JH_LORA_PROVIDER_EVENT_CRC_ERROR |
                          JH_LORA_PROVIDER_EVENT_HEADER_ERROR)) != 0u) {
      status = HAL_EPROTO;
    }
    complete_rx_locked(context, status, events);
  } else {
    status = HAL_EAGAIN;
  }
  hal_mutex_unlock(context->mutex);
  return status;
}

static hal_status_t transmit_start_with_timeout(hal_lora_radio_t radio,
                                                const uint8_t *data,
                                                size_t length,
                                                uint32_t timeout_ms) {
  if (data == NULL || length == 0u || length > HAL_LORA_RADIO_MAX_PAYLOAD) {
    return HAL_EINVAL;
  }
  jh_lora_radio_context_t *context = NULL;
  hal_status_t status = jh_lora_radio_context_lock(radio, &context);
  if (status != HAL_OK) {
    return status;
  }
  if (!context->configured) {
    jh_lora_radio_context_unlock(context);
    return HAL_EUNINIT;
  }
  if (context->operation_busy ||
      context->state != HAL_LORA_RADIO_STATE_STANDBY) {
    jh_lora_radio_context_unlock(context);
    return HAL_EBUSY;
  }
  if (timeout_ms == 0u) {
    status = auto_tx_timeout(&context->modem, length, &timeout_ms);
    if (status != HAL_OK) {
      jh_lora_radio_context_unlock(context);
      return status;
    }
  }
  memcpy(context->tx_buffer, data, length);
  context->tx_length = length;
  context->transmit_started_ms = hal_millis();
  context->transmit_timeout_ms = timeout_ms;
  context->tx_status.state = HAL_LORA_OPERATION_IN_PROGRESS;
  context->tx_status.result = HAL_EAGAIN;
  context->operation_busy = true;
  set_state(context, HAL_LORA_RADIO_STATE_TX);
  const jh_lora_radio_provider_ops_t *provider = context->provider;
  jh_lora_radio_context_unlock(context);

  status = provider->transmit_start(context, timeout_ms);
  hal_mutex_lock(context->mutex);
  context->operation_busy = false;
  if (status != HAL_OK) {
    complete_tx_locked(context, status);
  }
  hal_mutex_unlock(context->mutex);
  return status;
}

hal_status_t hal_lora_radio_transmit_start(hal_lora_radio_t radio,
                                           const uint8_t *data, size_t length) {
  return transmit_start_with_timeout(radio, data, length, 0u);
}

hal_status_t
hal_lora_radio_get_tx_status(hal_lora_radio_t radio,
                             hal_lora_operation_status_t *out_status) {
  if (out_status == NULL) {
    return HAL_EINVAL;
  }
  jh_lora_radio_context_t *context = NULL;
  const hal_status_t status = jh_lora_radio_context_lock(radio, &context);
  if (status != HAL_OK) {
    return status;
  }
  *out_status = context->tx_status;
  jh_lora_radio_context_unlock(context);
  return HAL_OK;
}

hal_status_t hal_lora_radio_transmit(hal_lora_radio_t radio,
                                     const uint8_t *data, size_t length,
                                     uint32_t timeout_ms) {
  hal_status_t status =
      transmit_start_with_timeout(radio, data, length, timeout_ms);
  while (status == HAL_OK) {
    hal_lora_operation_status_t operation = {};
    status = hal_lora_radio_get_tx_status(radio, &operation);
    if (status != HAL_OK) {
      return status;
    }
    if (operation.state != HAL_LORA_OPERATION_IN_PROGRESS) {
      return operation.result;
    }
    status = service_active_operation(radio);
    if (status == HAL_EAGAIN) {
      status = HAL_OK;
      hal_delay_ms(1u);
    }
  }
  return status;
}

static hal_status_t receive_start(hal_lora_radio_t radio, uint32_t timeout_ms,
                                  bool continuous) {
  if (!continuous && timeout_ms == 0u) {
    return HAL_EINVAL;
  }
  jh_lora_radio_context_t *context = NULL;
  hal_status_t status = jh_lora_radio_context_lock(radio, &context);
  if (status != HAL_OK) {
    return status;
  }
  if (!context->configured) {
    jh_lora_radio_context_unlock(context);
    return HAL_EUNINIT;
  }
  if (context->operation_busy ||
      context->state != HAL_LORA_RADIO_STATE_STANDBY) {
    jh_lora_radio_context_unlock(context);
    return HAL_EBUSY;
  }
  context->operation_busy = true;
  context->rx_ready = false;
  context->rx_length = 0u;
  context->receive_continuous = continuous;
  context->receive_started_ms = hal_millis();
  context->receive_timeout_ms = timeout_ms;
  context->rx_result = HAL_EAGAIN;
  set_state(context, HAL_LORA_RADIO_STATE_RX);
  const jh_lora_radio_provider_ops_t *provider = context->provider;
  jh_lora_radio_context_unlock(context);

  status = provider->receive_start(context, timeout_ms, continuous);
  hal_mutex_lock(context->mutex);
  context->operation_busy = false;
  if (status != HAL_OK) {
    complete_rx_locked(context, status, JH_LORA_PROVIDER_EVENT_NONE);
  }
  hal_mutex_unlock(context->mutex);
  return status;
}

hal_status_t hal_lora_radio_receive_start(hal_lora_radio_t radio,
                                          uint32_t timeout_ms) {
  return receive_start(radio, timeout_ms, false);
}

hal_status_t hal_lora_radio_receive_start_continuous(hal_lora_radio_t radio) {
  return receive_start(radio, 0u, true);
}

hal_status_t hal_lora_radio_receive(hal_lora_radio_t radio, uint8_t *buffer,
                                    size_t buffer_size, size_t *out_length,
                                    hal_lora_packet_info_t *out_info) {
  if (out_length == NULL || (buffer_size > 0u && buffer == NULL)) {
    return HAL_EINVAL;
  }
  *out_length = 0u;
  if (out_info != NULL) {
    memset(out_info, 0, sizeof(*out_info));
  }
  jh_lora_radio_context_t *context = NULL;
  hal_status_t status = jh_lora_radio_context_lock(radio, &context);
  if (status != HAL_OK) {
    return status;
  }
  if (context->operation_busy) {
    jh_lora_radio_context_unlock(context);
    return HAL_EBUSY;
  }
  const bool should_service = context->state == HAL_LORA_RADIO_STATE_RX &&
                              !context->rx_ready &&
                              context->rx_result == HAL_EAGAIN;
  if (!should_service && !context->rx_ready && context->rx_result == HAL_NONE) {
    jh_lora_radio_context_unlock(context);
    return HAL_ESTATE;
  }
  jh_lora_radio_context_unlock(context);

  if (should_service) {
    (void)service_active_operation(radio);
  }
  status = jh_lora_radio_context_lock(radio, &context);
  if (status != HAL_OK) {
    return status;
  }
  if (context->rx_ready) {
    const size_t copy_length =
        context->rx_length < buffer_size ? context->rx_length : buffer_size;
    if (copy_length > 0u) {
      memcpy(buffer, context->rx_buffer, copy_length);
    }
    *out_length = context->rx_length;
    if (out_info != NULL) {
      *out_info = context->rx_info;
    }
    context->diagnostics.last_rssi_dbm = context->rx_info.rssi_dbm;
    context->diagnostics.last_signal_rssi_dbm =
        context->rx_info.signal_rssi_dbm;
    context->diagnostics.last_snr_db = context->rx_info.snr_db;
    ++context->diagnostics.received_packets;
    context->rx_ready = false;
    context->rx_result = context->receive_continuous ? HAL_EAGAIN : HAL_NONE;
    status = HAL_OK;
    if (copy_length < context->rx_length) {
      ++context->diagnostics.dropped_packets;
      status = HAL_EOVERFLOW;
    }
  } else if (context->rx_result != HAL_EAGAIN &&
             context->rx_result != HAL_NONE) {
    status = context->rx_result;
    context->rx_result = context->receive_continuous ? HAL_EAGAIN : HAL_NONE;
  } else {
    status =
        context->state == HAL_LORA_RADIO_STATE_RX ? HAL_EAGAIN : HAL_ESTATE;
  }
  jh_lora_radio_context_unlock(context);
  return status;
}

hal_status_t hal_lora_radio_process(hal_lora_radio_t radio) {
  hal_status_t status = service_active_operation(radio);
  if (status == HAL_EBUSY || status == HAL_EUNINIT) {
    return status;
  }

  jh_lora_radio_context_t *context = NULL;
  const hal_status_t lock_status = jh_lora_radio_context_lock(radio, &context);
  if (lock_status != HAL_OK) {
    return lock_status;
  }
  hal_lora_radio_event_callback_t callback = NULL;
  void *user_data = NULL;
  hal_lora_radio_event_t event = {};
  if (context->event_pending && context->event_callback != NULL) {
    callback = context->event_callback;
    user_data = context->event_user_data;
    event = context->pending_event;
    context->event_pending = false;
    ++context->diagnostics.callback_events;
  }
  jh_lora_radio_context_unlock(context);
  if (callback != NULL) {
    callback(radio, &event, user_data);
    if (event.result != HAL_OK) {
      status = event.result;
    } else if (status == HAL_EAGAIN) {
      status = HAL_OK;
    }
  }
  return status;
}

hal_status_t
hal_lora_radio_set_event_callback(hal_lora_radio_t radio,
                                  hal_lora_radio_event_callback_t callback,
                                  void *user_data) {
  jh_lora_radio_context_t *context = NULL;
  const hal_status_t status = jh_lora_radio_context_lock(radio, &context);
  if (status != HAL_OK) {
    return status;
  }
  context->event_callback = callback;
  context->event_user_data = callback == NULL ? NULL : user_data;
  if (callback == NULL) {
    context->event_pending = false;
  }
  jh_lora_radio_context_unlock(context);
  return HAL_OK;
}

hal_status_t hal_lora_radio_cancel(hal_lora_radio_t radio) {
  jh_lora_radio_context_t *context = NULL;
  hal_status_t status = jh_lora_radio_context_lock(radio, &context);
  if (status != HAL_OK) {
    return status;
  }
  if (context->operation_busy) {
    jh_lora_radio_context_unlock(context);
    return HAL_EBUSY;
  }
  const hal_lora_radio_state_t active_state = context->state;
  if (active_state != HAL_LORA_RADIO_STATE_TX &&
      active_state != HAL_LORA_RADIO_STATE_RX &&
      active_state != HAL_LORA_RADIO_STATE_CAD) {
    jh_lora_radio_context_unlock(context);
    return HAL_ESTATE;
  }
  context->operation_busy = true;
  const jh_lora_radio_provider_ops_t *provider = context->provider;
  jh_lora_radio_context_unlock(context);

  status = provider->cancel(context);
  hal_mutex_lock(context->mutex);
  context->operation_busy = false;
  if (status == HAL_OK) {
    if (active_state == HAL_LORA_RADIO_STATE_TX) {
      complete_tx_locked(context, HAL_ECANCELED);
    } else {
      context->rx_result = HAL_ECANCELED;
      ++context->diagnostics.cancelled_operations;
      set_state(context, HAL_LORA_RADIO_STATE_STANDBY);
      queue_event(context, HAL_LORA_RADIO_EVENT_CANCELLED,
                  HAL_LORA_OPERATION_KIND_RECEIVE, HAL_ECANCELED);
    }
  } else {
    set_state(context, HAL_LORA_RADIO_STATE_ERROR);
    record_error(context, status);
    queue_event(context, HAL_LORA_RADIO_EVENT_ERROR,
                active_state == HAL_LORA_RADIO_STATE_TX
                    ? HAL_LORA_OPERATION_KIND_TRANSMIT
                    : HAL_LORA_OPERATION_KIND_RECEIVE,
                status);
  }
  hal_mutex_unlock(context->mutex);
  return status;
}

hal_status_t hal_lora_radio_get_state(hal_lora_radio_t radio,
                                      hal_lora_radio_state_t *out_state) {
  if (out_state == NULL) {
    return HAL_EINVAL;
  }
  jh_lora_radio_context_t *context = NULL;
  const hal_status_t status = jh_lora_radio_context_lock(radio, &context);
  if (status != HAL_OK) {
    return status;
  }
  *out_state = context->state;
  jh_lora_radio_context_unlock(context);
  return HAL_OK;
}

hal_status_t
hal_lora_radio_get_diagnostics(hal_lora_radio_t radio,
                               hal_lora_radio_diagnostics_t *out_diagnostics) {
  if (out_diagnostics == NULL) {
    return HAL_EINVAL;
  }
  jh_lora_radio_context_t *context = NULL;
  const hal_status_t status = jh_lora_radio_context_lock(radio, &context);
  if (status != HAL_OK) {
    return status;
  }
  *out_diagnostics = context->diagnostics;
  jh_lora_radio_context_unlock(context);
  return HAL_OK;
}

static hal_status_t set_power_state(hal_lora_radio_t radio, bool sleep) {
  jh_lora_radio_context_t *context = NULL;
  hal_status_t status = jh_lora_radio_context_lock(radio, &context);
  if (status != HAL_OK) {
    return status;
  }
  if (context->operation_busy || context->state == HAL_LORA_RADIO_STATE_TX ||
      context->state == HAL_LORA_RADIO_STATE_RX ||
      context->state == HAL_LORA_RADIO_STATE_CAD) {
    jh_lora_radio_context_unlock(context);
    return HAL_EBUSY;
  }
  if (sleep && context->state != HAL_LORA_RADIO_STATE_STANDBY) {
    jh_lora_radio_context_unlock(context);
    return HAL_ESTATE;
  }
  context->operation_busy = true;
  const jh_lora_radio_provider_ops_t *provider = context->provider;
  jh_lora_radio_context_unlock(context);
  status = sleep ? provider->sleep(context) : provider->standby(context);
  hal_mutex_lock(context->mutex);
  context->operation_busy = false;
  if (status == HAL_OK) {
    set_state(context, sleep ? HAL_LORA_RADIO_STATE_SLEEP
                             : HAL_LORA_RADIO_STATE_STANDBY);
  } else {
    set_state(context, HAL_LORA_RADIO_STATE_ERROR);
    record_error(context, status);
  }
  hal_mutex_unlock(context->mutex);
  return status;
}

hal_status_t hal_lora_radio_sleep(hal_lora_radio_t radio) {
  return set_power_state(radio, true);
}

hal_status_t hal_lora_radio_standby(hal_lora_radio_t radio) {
  return set_power_state(radio, false);
}

hal_status_t hal_lora_time_on_air(const hal_lora_modem_config_t *config,
                                  size_t payload_length,
                                  uint32_t *out_time_ms) {
  return jh_sx126x_time_on_air(config, payload_length, out_time_ms);
}

#if HAL_TARGET_IS_MOCK || defined(JH_LORA_PROVIDER_TESTING)
hal_status_t jh_lora_radio_set_provider_for_test(
    const jh_lora_radio_provider_ops_t *provider) {
  if (provider != NULL && !provider_valid(provider)) {
    return HAL_EINVAL;
  }
  const hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  for (size_t index = 0u; index < HAL_LORA_RADIO_MAX_INSTANCES; ++index) {
    if (s_contexts[index].allocated) {
      pool_unlock();
      return HAL_EBUSY;
    }
  }
  s_provider = provider == NULL ? jh_lora_radio_default_provider() : provider;
  pool_unlock();
  return provider_valid(s_provider) ? HAL_OK : HAL_ECONFIG;
}
#endif

#endif /* HAL_ENABLE_LORA */
