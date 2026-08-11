#pragma once

#include "hal/spi/hal_spi_device.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SSD1306-family monochrome OLED panel driver.
 *
 * Command sequences and framebuffer transfer logic started from the Adafruit
 * SSD1306 library (BSD license), then grew controller-family handling using
 * the same split Zephyr uses for SSD1309 / SSD1315 / SH1106 / CH1115 details.
 * The driver owns only controller I/O; pixels remain in a caller-owned buffer
 * so drawing can be delegated to the shared GFX engine. See jh_gfx.* for the
 * rendering side.
 */

#define JH_SSD1306_DEFAULT_I2C_HZ 400000UL
#define JH_SSD1306_DEFAULT_SPI_HZ 8000000UL

/* Logical pixel colours (match the values used by the GFX integration). */
#define JH_SSD1306_BLACK 0u   /* draw 'off' pixels   */
#define JH_SSD1306_WHITE 1u   /* draw 'on' pixels    */
#define JH_SSD1306_INVERSE 2u /* invert pixels       */

/* Power-source selection, mirrors the Adafruit SSD1306 vccstate constants. */
#define JH_SSD1306_EXTERNALVCC 0x01u
#define JH_SSD1306_SWITCHCAPVCC 0x02u

typedef enum {
  JH_SSD1306_CONTROLLER_SSD1306 = 0,
  JH_SSD1306_CONTROLLER_SSD1309,
  JH_SSD1306_CONTROLLER_SSD1315,
  JH_SSD1306_CONTROLLER_SH1106,
  JH_SSD1306_CONTROLLER_CH1115,
} jh_ssd1306_controller_t;

typedef enum {
  JH_SSD1306_BUS_I2C = 0,
  JH_SSD1306_BUS_SPI,
} jh_ssd1306_bus_t;

typedef enum {
  JH_SSD1306_ORIENTATION_NORMAL = 0,
  JH_SSD1306_ORIENTATION_ROTATED_180,
} jh_ssd1306_orientation_t;

typedef struct {
  uint8_t bus;       /* I2C/SPI bus index (0 = primary). */
  uint8_t i2c_addr;  /* 7-bit address; 0 selects the size-based default. */
  uint16_t width;    /* Panel width in pixels.  */
  uint16_t height;   /* Panel height in pixels. */
  int16_t rst_pin;   /* Reset GPIO, or -1 when not connected. */
  uint8_t vccstate;  /* JH_SSD1306_SWITCHCAPVCC / JH_SSD1306_EXTERNALVCC. */
  uint32_t clock_hz; /* Bus clock; 0 selects I2C/SPI family default. */

  jh_ssd1306_controller_t controller;
  jh_ssd1306_bus_t bus_type;
  int16_t spi_dc_pin; /* SPI data/command GPIO; required for SPI. */
  int16_t spi_cs_pin; /* SPI chip-select GPIO, or -1 when externally held. */
  uint8_t spi_mode;   /* HAL_SPI_MODE0..3; invalid values select mode 0. */

  uint8_t segment_offset; /* Horizontal controller RAM offset. */
  uint8_t page_offset;    /* Vertical page offset for SH/CH/variant panels. */
  uint8_t display_offset; /* Hardware display offset command value. */
  jh_ssd1306_orientation_t orientation;
  bool internal_iref; /* SSD1315/CH1115 current-reference mode. */
} jh_ssd1306_config_t;

typedef struct {
  jh_ssd1306_config_t config;
  hal_spi_device_t spi_device;
  uint16_t width;
  uint16_t height;
  uint8_t i2c_addr;
  uint8_t contrast;
  jh_ssd1306_controller_t controller;
  jh_ssd1306_bus_t bus_type;
  uint8_t segment_offset;
  uint8_t page_offset;
  uint8_t display_offset;
  jh_ssd1306_orientation_t orientation;
  bool initialized;
  bool suspended;
} jh_ssd1306_t;

/**
 * @brief Run the power-on register sequence and turn the panel on.
 *
 * Performs the optional hardware reset, configures the controller for the
 * requested geometry, and leaves the display enabled.  The caller is
 * responsible for the framebuffer; @ref jh_ssd1306_display sends it out.
 *
 * @param dev     Driver instance to initialise.
 * @param config  Geometry and bus configuration.
 * @return true on success.
 */
bool jh_ssd1306_init(jh_ssd1306_t *dev, const jh_ssd1306_config_t *config);

/**
 * @brief Push a full framebuffer to the panel over I2C.
 * @param dev     Initialised driver instance.
 * @param buffer  Framebuffer of width * ((height + 7) / 8) bytes.
 * @return true on success.
 */
bool jh_ssd1306_display(jh_ssd1306_t *dev, const uint8_t *buffer);

/**
 * @brief Enable or disable hardware colour inversion.
 * @param dev     Initialised driver instance.
 * @param invert  true to invert the panel.
 * @return true on success.
 */
bool jh_ssd1306_invert(jh_ssd1306_t *dev, bool invert);

/**
 * @brief Set the panel contrast (0-255).
 * @param dev       Initialised driver instance.
 * @param contrast  Contrast level.
 * @return true on success.
 */
bool jh_ssd1306_set_contrast(jh_ssd1306_t *dev, uint8_t contrast);

/**
 * @brief Set controller orientation for panels that expose SEG/COM direction.
 * @param dev          Initialised driver instance.
 * @param orientation  Native or 180-degree controller scan orientation.
 * @return true on success.
 */
bool jh_ssd1306_set_orientation(jh_ssd1306_t *dev,
                                jh_ssd1306_orientation_t orientation);

/**
 * @brief Enter low-power display-off state.
 * @param dev Initialised driver instance.
 * @return true on success.
 */
bool jh_ssd1306_suspend(jh_ssd1306_t *dev);

/**
 * @brief Leave display-off state.
 * @param dev Initialised driver instance.
 * @return true on success.
 */
bool jh_ssd1306_resume(jh_ssd1306_t *dev);

/**
 * @brief Resolve the framebuffer size in bytes for the configured geometry.
 * @param dev Initialised driver instance.
 * @return Buffer size in bytes, or 0 when not initialised.
 */
size_t jh_ssd1306_buffer_size(const jh_ssd1306_t *dev);

#ifdef __cplusplus
}
#endif
