#include "hal/core/hal_target.h"

#include "hal/core/hal_config.h"

#ifdef HAL_ENABLE_HD44780

#include "hal/display/hal_hd44780.h"

#include "hal/core/hal_mutex_once.h"
#include "hal/core/jh_handle_pool.h"
#include "hal/display/drivers/display_spi_transport.h"
#include "hal/display/hd44780/hd44780.h"
#include "hal/gpio/hal_gpio_common.h"
#include "hal/system/hal_sync.h"

#include <new>
#include <string.h>

#define JH_HD44780_HANDLE_KIND 20u

typedef struct {
  alignas(HD44780) unsigned char storage[sizeof(HD44780)];
  bool allocated;
} jh_hd44780_context_t;

static jh_hd44780_context_t s_contexts[HAL_HD44780_MAX_INSTANCES] = {};
static jh_handle_slot_t s_handle_slots[HAL_HD44780_MAX_INSTANCES] = {};
static jh_handle_pool_t s_handle_pool = {};
static hal_mutex_t s_pool_mutex = NULL;
static bool s_pool_initialized = false;

static hal_status_t pool_lock(void) {
  hal_mutex_t mutex = jh_hal_mutex_try_create_once(&s_pool_mutex);
  if (mutex == NULL) {
    return HAL_ENOMEM;
  }
  hal_mutex_lock(mutex);
  if (!s_pool_initialized) {
    const hal_status_t status =
        jh_handle_pool_init(&s_handle_pool, s_handle_slots,
                            HAL_HD44780_MAX_INSTANCES, JH_HD44780_HANDLE_KIND);
    if (status != HAL_OK) {
      hal_mutex_unlock(mutex);
      return status;
    }
    s_pool_initialized = true;
  }
  return HAL_OK;
}

static void pool_unlock(void) { hal_mutex_unlock(s_pool_mutex); }

static HD44780 *context_lcd(jh_hd44780_context_t *context) {
  return reinterpret_cast<HD44780 *>(context->storage);
}

static bool pin_valid(int16_t pin) {
  return jh_display_pin_connected(pin) &&
         jh_display_pin_u8(pin) != HD44780_NO_PIN &&
         jh_hal_gpio_pin_valid(jh_display_pin_u8(pin));
}

static bool config_valid(const hal_hd44780_config_t *config) {
  if (config == NULL || !pin_valid(config->rs_pin) ||
      !pin_valid(config->enable_pin) ||
      (config->rw_pin != HAL_HD44780_PIN_NONE && !pin_valid(config->rw_pin)) ||
      (config->bus_width != HAL_HD44780_BUS_4_BIT &&
       config->bus_width != HAL_HD44780_BUS_8_BIT)) {
    return false;
  }

  const size_t data_pin_count =
      config->bus_width == HAL_HD44780_BUS_8_BIT ? 8u : 4u;
  int16_t pins[11] = {config->rs_pin, config->enable_pin};
  size_t pin_count = 2u;
  if (config->rw_pin != HAL_HD44780_PIN_NONE) {
    pins[pin_count++] = config->rw_pin;
  }
  for (size_t index = 0u; index < data_pin_count; ++index) {
    if (!pin_valid(config->data_pins[index])) {
      return false;
    }
    pins[pin_count++] = config->data_pins[index];
  }
  for (size_t left = 0u; left < pin_count; ++left) {
    for (size_t right = left + 1u; right < pin_count; ++right) {
      if (pins[left] == pins[right]) {
        return false;
      }
    }
  }
  return true;
}

static HD44780 *construct_lcd(jh_hd44780_context_t *context,
                              const hal_hd44780_config_t *config) {
  const uint8_t rs = jh_display_pin_u8(config->rs_pin);
  const uint8_t enable = jh_display_pin_u8(config->enable_pin);
  const uint8_t rw = config->rw_pin == HAL_HD44780_PIN_NONE
                         ? HD44780_NO_PIN
                         : jh_display_pin_u8(config->rw_pin);
  const uint8_t d0 = jh_display_pin_u8(config->data_pins[0]);
  const uint8_t d1 = jh_display_pin_u8(config->data_pins[1]);
  const uint8_t d2 = jh_display_pin_u8(config->data_pins[2]);
  const uint8_t d3 = jh_display_pin_u8(config->data_pins[3]);

  if (config->bus_width == HAL_HD44780_BUS_4_BIT) {
    if (rw == HD44780_NO_PIN) {
      return new (context->storage) HD44780(rs, enable, d0, d1, d2, d3);
    }
    return new (context->storage) HD44780(rs, rw, enable, d0, d1, d2, d3);
  }

  const uint8_t d4 = jh_display_pin_u8(config->data_pins[4]);
  const uint8_t d5 = jh_display_pin_u8(config->data_pins[5]);
  const uint8_t d6 = jh_display_pin_u8(config->data_pins[6]);
  const uint8_t d7 = jh_display_pin_u8(config->data_pins[7]);
  if (rw == HD44780_NO_PIN) {
    return new (context->storage)
        HD44780(rs, enable, d0, d1, d2, d3, d4, d5, d6, d7);
  }
  return new (context->storage)
      HD44780(rs, rw, enable, d0, d1, d2, d3, d4, d5, d6, d7);
}

static hal_status_t resolve_lcd(hal_hd44780_t handle, HD44780 **out_lcd) {
  if (out_lcd == NULL) {
    return HAL_EINVAL;
  }
  *out_lcd = NULL;
  const hal_status_t lock_status = pool_lock();
  if (lock_status != HAL_OK) {
    return lock_status;
  }
  void *token = NULL;
  const hal_status_t resolve_status =
      jh_handle_resolve(&s_handle_pool, handle, &token, NULL);
  if (resolve_status == HAL_OK && token != NULL) {
    jh_hd44780_context_t *context = static_cast<jh_hd44780_context_t *>(token);
    if (context->allocated) {
      *out_lcd = context_lcd(context);
    }
  }
  pool_unlock();
  return *out_lcd != NULL ? HAL_OK : HAL_EUNINIT;
}

hal_status_t hal_hd44780_create(const hal_hd44780_config_t *config,
                                hal_hd44780_t *out_lcd) {
  if (out_lcd == NULL) {
    return HAL_EINVAL;
  }
  *out_lcd = NULL;
  if (!config_valid(config)) {
    return HAL_EINVAL;
  }

  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  jh_hd44780_context_t *context = NULL;
  for (size_t index = 0u; index < HAL_HD44780_MAX_INSTANCES; ++index) {
    if (!s_contexts[index].allocated) {
      context = &s_contexts[index];
      break;
    }
  }
  if (context == NULL) {
    pool_unlock();
    return HAL_ENOMEM;
  }

  context->allocated = true;
  HD44780 *lcd = construct_lcd(context, config);
  if (!lcd->isInitialized()) {
    lcd->~HD44780();
    context->allocated = false;
    pool_unlock();
    return HAL_ENOMEM;
  }

  void *handle = NULL;
  status = jh_handle_allocate(&s_handle_pool, context, &handle);
  if (status != HAL_OK) {
    lcd->~HD44780();
    context->allocated = false;
    pool_unlock();
    return status;
  }
  *out_lcd = reinterpret_cast<hal_hd44780_t>(handle);
  pool_unlock();
  return HAL_OK;
}

hal_status_t hal_hd44780_destroy(hal_hd44780_t lcd) {
  hal_status_t status = pool_lock();
  if (status != HAL_OK) {
    return status;
  }
  void *token = NULL;
  status = jh_handle_release(&s_handle_pool, lcd, &token);
  if (status != HAL_OK || token == NULL) {
    pool_unlock();
    return status == HAL_OK ? HAL_EINTERNAL : HAL_EUNINIT;
  }
  jh_hd44780_context_t *context = static_cast<jh_hd44780_context_t *>(token);
  context_lcd(context)->~HD44780();
  context->allocated = false;
  pool_unlock();
  return HAL_OK;
}

hal_status_t hal_hd44780_begin(hal_hd44780_t lcd, uint8_t columns, uint8_t rows,
                               hal_hd44780_font_t font) {
  if (columns == 0u || rows == 0u || rows > 4u ||
      (font != HAL_HD44780_FONT_5X8 && font != HAL_HD44780_FONT_5X10) ||
      (font == HAL_HD44780_FONT_5X10 && rows != 1u)) {
    return HAL_EINVAL;
  }
  HD44780 *driver = NULL;
  hal_status_t status = resolve_lcd(lcd, &driver);
  if (status != HAL_OK) {
    return status;
  }
  const uint8_t native_font =
      font == HAL_HD44780_FONT_5X10 ? LCD_5x10DOTS : LCD_5x8DOTS;
  driver->begin(columns, rows, native_font);
  return driver->isInitialized() ? HAL_OK : HAL_ENOMEM;
}

hal_status_t hal_hd44780_clear(hal_hd44780_t lcd) {
  HD44780 *driver = NULL;
  const hal_status_t status = resolve_lcd(lcd, &driver);
  if (status != HAL_OK) {
    return status;
  }
  driver->clear();
  return HAL_OK;
}

hal_status_t hal_hd44780_home(hal_hd44780_t lcd) {
  HD44780 *driver = NULL;
  const hal_status_t status = resolve_lcd(lcd, &driver);
  if (status != HAL_OK) {
    return status;
  }
  driver->home();
  return HAL_OK;
}

hal_status_t hal_hd44780_set_row_offsets(hal_hd44780_t lcd, int row0, int row1,
                                         int row2, int row3) {
  HD44780 *driver = NULL;
  const hal_status_t status = resolve_lcd(lcd, &driver);
  if (status != HAL_OK) {
    return status;
  }
  driver->setRowOffsets(row0, row1, row2, row3);
  return HAL_OK;
}

hal_status_t hal_hd44780_set_cursor(hal_hd44780_t lcd, uint8_t column,
                                    uint8_t row) {
  HD44780 *driver = NULL;
  const hal_status_t status = resolve_lcd(lcd, &driver);
  if (status != HAL_OK) {
    return status;
  }
  driver->setCursor(column, row);
  return HAL_OK;
}

hal_status_t hal_hd44780_set_display_enabled(hal_hd44780_t lcd, bool enabled) {
  HD44780 *driver = NULL;
  const hal_status_t status = resolve_lcd(lcd, &driver);
  if (status != HAL_OK) {
    return status;
  }
  if (enabled) {
    driver->display();
  } else {
    driver->noDisplay();
  }
  return HAL_OK;
}

hal_status_t hal_hd44780_set_cursor_enabled(hal_hd44780_t lcd, bool enabled) {
  HD44780 *driver = NULL;
  const hal_status_t status = resolve_lcd(lcd, &driver);
  if (status != HAL_OK) {
    return status;
  }
  if (enabled) {
    driver->cursor();
  } else {
    driver->noCursor();
  }
  return HAL_OK;
}

hal_status_t hal_hd44780_set_blink_enabled(hal_hd44780_t lcd, bool enabled) {
  HD44780 *driver = NULL;
  const hal_status_t status = resolve_lcd(lcd, &driver);
  if (status != HAL_OK) {
    return status;
  }
  if (enabled) {
    driver->blink();
  } else {
    driver->noBlink();
  }
  return HAL_OK;
}

hal_status_t hal_hd44780_scroll(hal_hd44780_t lcd,
                                hal_hd44780_scroll_direction_t direction) {
  if (direction != HAL_HD44780_SCROLL_LEFT &&
      direction != HAL_HD44780_SCROLL_RIGHT) {
    return HAL_EINVAL;
  }
  HD44780 *driver = NULL;
  const hal_status_t status = resolve_lcd(lcd, &driver);
  if (status != HAL_OK) {
    return status;
  }
  if (direction == HAL_HD44780_SCROLL_LEFT) {
    driver->scrollDisplayLeft();
  } else {
    driver->scrollDisplayRight();
  }
  return HAL_OK;
}

hal_status_t
hal_hd44780_set_text_direction(hal_hd44780_t lcd,
                               hal_hd44780_text_direction_t direction) {
  if (direction != HAL_HD44780_TEXT_LEFT_TO_RIGHT &&
      direction != HAL_HD44780_TEXT_RIGHT_TO_LEFT) {
    return HAL_EINVAL;
  }
  HD44780 *driver = NULL;
  const hal_status_t status = resolve_lcd(lcd, &driver);
  if (status != HAL_OK) {
    return status;
  }
  if (direction == HAL_HD44780_TEXT_LEFT_TO_RIGHT) {
    driver->leftToRight();
  } else {
    driver->rightToLeft();
  }
  return HAL_OK;
}

hal_status_t hal_hd44780_set_autoscroll_enabled(hal_hd44780_t lcd,
                                                bool enabled) {
  HD44780 *driver = NULL;
  const hal_status_t status = resolve_lcd(lcd, &driver);
  if (status != HAL_OK) {
    return status;
  }
  if (enabled) {
    driver->autoscroll();
  } else {
    driver->noAutoscroll();
  }
  return HAL_OK;
}

hal_status_t hal_hd44780_create_char(hal_hd44780_t lcd, uint8_t location,
                                     const uint8_t rows[8]) {
  if (rows == NULL) {
    return HAL_EINVAL;
  }
  HD44780 *driver = NULL;
  const hal_status_t status = resolve_lcd(lcd, &driver);
  if (status != HAL_OK) {
    return status;
  }
  driver->createChar(location, rows);
  return HAL_OK;
}

hal_status_t hal_hd44780_command(hal_hd44780_t lcd, uint8_t command) {
  HD44780 *driver = NULL;
  const hal_status_t status = resolve_lcd(lcd, &driver);
  if (status != HAL_OK) {
    return status;
  }
  driver->command(command);
  return HAL_OK;
}

hal_status_t hal_hd44780_write_byte(hal_hd44780_t lcd, uint8_t value) {
  HD44780 *driver = NULL;
  const hal_status_t status = resolve_lcd(lcd, &driver);
  if (status != HAL_OK) {
    return status;
  }
  return driver->write(value) == 1u ? HAL_OK : HAL_EIO;
}

hal_status_t hal_hd44780_write(hal_hd44780_t lcd, const uint8_t *data,
                               size_t size, size_t *out_written) {
  if (out_written != NULL) {
    *out_written = 0u;
  }
  if (data == NULL && size != 0u) {
    return HAL_EINVAL;
  }
  HD44780 *driver = NULL;
  const hal_status_t status = resolve_lcd(lcd, &driver);
  if (status != HAL_OK) {
    return status;
  }
  const size_t written = driver->write(data, size);
  if (out_written != NULL) {
    *out_written = written;
  }
  return written == size ? HAL_OK : HAL_EIO;
}

hal_status_t hal_hd44780_print(hal_hd44780_t lcd, const char *text,
                               size_t *out_written) {
  if (out_written != NULL) {
    *out_written = 0u;
  }
  if (text == NULL) {
    return HAL_EINVAL;
  }
  HD44780 *driver = NULL;
  const hal_status_t status = resolve_lcd(lcd, &driver);
  if (status != HAL_OK) {
    return status;
  }
  const size_t expected = strlen(text);
  const size_t written = driver->print(text);
  if (out_written != NULL) {
    *out_written = written;
  }
  return written == expected ? HAL_OK : HAL_EIO;
}

#endif /* HAL_ENABLE_HD44780 */
