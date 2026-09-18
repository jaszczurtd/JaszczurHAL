#pragma once

/**
 * @file hal_hd44780.h
 * @brief C facade and legacy C++ API for HD44780 character LCDs.
 *
 * Enable with HAL_ENABLE_HD44780. C applications use the opaque handle API
 * declared here. C++ applications may keep using the HD44780 class exposed by
 * hal/display/hd44780/hd44780.h.
 */

#include "hal/core/hal_config.h"

#if defined(HAL_ENABLE_HD44780)

#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @def HAL_HD44780_MAX_INSTANCES
 * @brief Maximum number of simultaneous handles stored in the static pool.
 *
 * Override before including JaszczurHAL headers when more displays are needed.
 */
#ifndef HAL_HD44780_MAX_INSTANCES
#define HAL_HD44780_MAX_INSTANCES 4u
#endif

#if HAL_HD44780_MAX_INSTANCES < 1u || HAL_HD44780_MAX_INSTANCES > 255u
#error "HAL_HD44780_MAX_INSTANCES must be in range 1..255"
#endif

/** @brief Pin value used when the LCD `RW` line is tied to ground. */
#define HAL_HD44780_PIN_NONE (-INT16_C(1))

/** @brief Opaque HD44780 handle implementation type. */
typedef struct hal_hd44780_impl_s hal_hd44780_impl_t;

/** @brief Handle returned by hal_hd44780_create(). */
typedef hal_hd44780_impl_t *hal_hd44780_t;

/** @brief Parallel bus width used by an HD44780 display. */
typedef enum {
  HAL_HD44780_BUS_4_BIT = 4, /**< Four data pins connected to LCD D4..D7. */
  HAL_HD44780_BUS_8_BIT = 8  /**< Eight data pins connected to LCD D0..D7. */
} hal_hd44780_bus_width_t;

/** @brief Character dot size selected during display initialization. */
typedef enum {
  HAL_HD44780_FONT_5X8 = 0, /**< Standard 5x8 character font. */
  HAL_HD44780_FONT_5X10 = 1 /**< 5x10 font, valid only for one-line displays. */
} hal_hd44780_font_t;

/** @brief Direction used by hal_hd44780_scroll(). */
typedef enum {
  HAL_HD44780_SCROLL_LEFT = 0, /**< Shift displayed content left. */
  HAL_HD44780_SCROLL_RIGHT = 1 /**< Shift displayed content right. */
} hal_hd44780_scroll_direction_t;

/** @brief Text direction used for subsequent character writes. */
typedef enum {
  HAL_HD44780_TEXT_LEFT_TO_RIGHT = 0, /**< Advance the cursor to the right. */
  HAL_HD44780_TEXT_RIGHT_TO_LEFT = 1  /**< Advance the cursor to the left. */
} hal_hd44780_text_direction_t;

/** @brief GPIO connections and bus width for one HD44780 display. */
typedef struct {
  /** Register-select GPIO. Must be connected. */
  int16_t rs_pin;
  /** Read/write GPIO, or HAL_HD44780_PIN_NONE when grounded. */
  int16_t rw_pin;
  /** Enable GPIO. Must be connected. */
  int16_t enable_pin;
  /**
   * Data GPIOs in increasing LCD-bit order. Four-bit mode uses entries 0..3
   * for LCD D4..D7 and ignores entries 4..7. Eight-bit mode uses all entries
   * for LCD D0..D7.
   */
  int16_t data_pins[8];
  /** Four-bit or eight-bit transfer mode. */
  hal_hd44780_bus_width_t bus_width;
} hal_hd44780_config_t;

/**
 * @brief Create and initialize an HD44780 handle from a pin configuration.
 * @param config Pin configuration. Required pins must be valid and unique.
 * @param out_lcd Receives the handle on success; set to NULL on failure.
 * @return HAL_OK on success, HAL_EINVAL for invalid arguments, or HAL_ENOMEM
 *         when the static handle pool or synchronization resources are full.
 *
 * The driver performs the legacy 16x1 power-on sequence during creation. Call
 * hal_hd44780_begin() afterwards to select the actual display geometry.
 * Creation and destruction are single-owner operations.
 */
hal_status_t hal_hd44780_create(const hal_hd44780_config_t *config,
                                hal_hd44780_t *out_lcd);

/**
 * @brief Destroy an HD44780 handle and release its static pool slot.
 * @param lcd Handle returned by hal_hd44780_create().
 * @return HAL_OK on success or HAL_EUNINIT for a NULL, stale, or unknown
 *         handle.
 *
 * Do not destroy a handle while another task is using it.
 */
hal_status_t hal_hd44780_destroy(hal_hd44780_t lcd);

/**
 * @brief Configure display geometry and character font.
 * @param lcd Valid display handle.
 * @param columns Number of character columns; must be greater than zero.
 * @param rows Number of rows in range 1..4.
 * @param font Character font. The 5x10 font is valid only with one row.
 * @return HAL_OK on success, HAL_EINVAL for invalid geometry or font, or
 *         HAL_EUNINIT for an invalid handle.
 */
hal_status_t hal_hd44780_begin(hal_hd44780_t lcd, uint8_t columns, uint8_t rows,
                               hal_hd44780_font_t font);

/**
 * @brief Clear display memory and move the cursor to the home position.
 * @return HAL_OK on success or HAL_EUNINIT for an invalid handle.
 */
hal_status_t hal_hd44780_clear(hal_hd44780_t lcd);

/**
 * @brief Move the cursor to the home position without clearing characters.
 * @return HAL_OK on success or HAL_EUNINIT for an invalid handle.
 */
hal_status_t hal_hd44780_home(hal_hd44780_t lcd);

/**
 * @brief Set DDRAM offsets used for rows zero through three.
 * @param lcd Valid display handle.
 * @param row0 DDRAM offset for row zero; the low eight bits are used.
 * @param row1 DDRAM offset for row one; the low eight bits are used.
 * @param row2 DDRAM offset for row two; the low eight bits are used.
 * @param row3 DDRAM offset for row three; the low eight bits are used.
 * @return HAL_OK on success or HAL_EUNINIT for an invalid handle.
 */
hal_status_t hal_hd44780_set_row_offsets(hal_hd44780_t lcd, int row0, int row1,
                                         int row2, int row3);

/**
 * @brief Move the cursor to a character position.
 * @param lcd Valid display handle.
 * @param column Zero-based column. Values beyond the display width are passed
 *               to the controller unchanged.
 * @param row Zero-based row. Values beyond the configured row count are
 *            clamped by the legacy driver.
 * @return HAL_OK on success or HAL_EUNINIT for an invalid handle.
 */
hal_status_t hal_hd44780_set_cursor(hal_hd44780_t lcd, uint8_t column,
                                    uint8_t row);

/**
 * @brief Enable or blank the LCD output without changing display memory.
 * @param lcd Valid display handle.
 * @param enabled true to show the display, false to blank it.
 * @return HAL_OK on success or HAL_EUNINIT for an invalid handle.
 */
hal_status_t hal_hd44780_set_display_enabled(hal_hd44780_t lcd, bool enabled);

/**
 * @brief Enable or hide the underline cursor.
 * @param lcd Valid display handle.
 * @param enabled true to show the cursor, false to hide it.
 * @return HAL_OK on success or HAL_EUNINIT for an invalid handle.
 */
hal_status_t hal_hd44780_set_cursor_enabled(hal_hd44780_t lcd, bool enabled);

/**
 * @brief Enable or disable cursor blinking.
 * @param lcd Valid display handle.
 * @param enabled true to blink, false for a steady cursor state.
 * @return HAL_OK on success or HAL_EUNINIT for an invalid handle.
 */
hal_status_t hal_hd44780_set_blink_enabled(hal_hd44780_t lcd, bool enabled);

/**
 * @brief Shift all visible characters by one position.
 * @param lcd Valid display handle.
 * @param direction Shift direction.
 * @return HAL_OK on success, HAL_EINVAL for an invalid direction, or
 *         HAL_EUNINIT for an invalid handle.
 */
hal_status_t hal_hd44780_scroll(hal_hd44780_t lcd,
                                hal_hd44780_scroll_direction_t direction);

/**
 * @brief Set the cursor advance direction for subsequent writes.
 * @param lcd Valid display handle.
 * @param direction Left-to-right or right-to-left text direction.
 * @return HAL_OK on success, HAL_EINVAL for an invalid direction, or
 *         HAL_EUNINIT for an invalid handle.
 */
hal_status_t
hal_hd44780_set_text_direction(hal_hd44780_t lcd,
                               hal_hd44780_text_direction_t direction);

/**
 * @brief Enable or disable automatic display shifting after character writes.
 * @param lcd Valid display handle.
 * @param enabled true to enable automatic shifting, false to disable it.
 * @return HAL_OK on success or HAL_EUNINIT for an invalid handle.
 */
hal_status_t hal_hd44780_set_autoscroll_enabled(hal_hd44780_t lcd,
                                                bool enabled);

/**
 * @brief Store one custom 5x8 glyph in controller CGRAM.
 * @param lcd Valid display handle.
 * @param location CGRAM slot. Only the low three bits are used.
 * @param rows Eight row bitmaps. Must not be NULL.
 * @return HAL_OK on success, HAL_EINVAL when @p rows is NULL, or HAL_EUNINIT
 *         for an invalid handle.
 */
hal_status_t hal_hd44780_create_char(hal_hd44780_t lcd, uint8_t location,
                                     const uint8_t rows[8]);

/**
 * @brief Send one raw command byte to the controller.
 * @param lcd Valid display handle.
 * @param command Command byte.
 * @return HAL_OK on success or HAL_EUNINIT for an invalid handle.
 */
hal_status_t hal_hd44780_command(hal_hd44780_t lcd, uint8_t command);

/**
 * @brief Write one raw character byte to display memory.
 * @param lcd Valid display handle.
 * @param value Character byte.
 * @return HAL_OK on success, HAL_EIO if the byte could not be written, or
 *         HAL_EUNINIT for an invalid handle.
 */
hal_status_t hal_hd44780_write_byte(hal_hd44780_t lcd, uint8_t value);

/**
 * @brief Write an arbitrary byte buffer to display memory.
 * @param lcd Valid display handle.
 * @param data Source buffer. May be NULL only when @p size is zero.
 * @param size Number of bytes to write.
 * @param out_written Optional destination for the number of bytes written;
 *                    may be NULL.
 * @return HAL_OK when all bytes were written, HAL_EINVAL for an invalid
 *         buffer, HAL_EIO for a short write, or HAL_EUNINIT for an invalid
 *         handle.
 */
hal_status_t hal_hd44780_write(hal_hd44780_t lcd, const uint8_t *data,
                               size_t size, size_t *out_written);

/**
 * @brief Write a null-terminated string to display memory.
 * @param lcd Valid display handle.
 * @param text Null-terminated text. Must not be NULL.
 * @param out_written Optional destination for the number of characters
 *                    written; may be NULL.
 * @return HAL_OK when the complete string was written, HAL_EINVAL when
 *         @p text is NULL, HAL_EIO for a short write, or HAL_EUNINIT for an
 *         invalid handle.
 */
hal_status_t hal_hd44780_print(hal_hd44780_t lcd, const char *text,
                               size_t *out_written);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* Keep the original constants and C++ class reachable from this include. */
#include "hal/display/hd44780/hd44780.h"

#endif /* supported target && HAL_ENABLE_HD44780 */
