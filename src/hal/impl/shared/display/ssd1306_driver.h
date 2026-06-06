#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SSD1306 monochrome OLED panel driver.
 *
 * Command sequences and the framebuffer transfer logic are adapted from the
 * Adafruit SSD1306 library (BSD license).  This implementation talks to the
 * panel over the JaszczurHAL I2C bus only, leaving the pixel framebuffer to a
 * caller-owned buffer so that drawing can be delegated to the shared GFX
 * engine.  See jh_gfx.* for the rendering side.
 */

#define JH_SSD1306_DEFAULT_I2C_HZ 400000UL

/* Logical pixel colours (match the values used by the GFX integration). */
#define JH_SSD1306_BLACK   0u  /* draw 'off' pixels   */
#define JH_SSD1306_WHITE   1u  /* draw 'on' pixels    */
#define JH_SSD1306_INVERSE 2u  /* invert pixels       */

/* Power-source selection, mirrors the Adafruit SSD1306 vccstate constants. */
#define JH_SSD1306_EXTERNALVCC  0x01u
#define JH_SSD1306_SWITCHCAPVCC 0x02u

typedef struct {
    uint8_t bus;        /* I2C bus index (0 = primary). */
    uint8_t i2c_addr;   /* 7-bit address; 0 selects the size-based default. */
    uint16_t width;     /* Panel width in pixels.  */
    uint16_t height;    /* Panel height in pixels. */
    int16_t rst_pin;    /* Reset GPIO, or -1 when not connected. */
    uint8_t vccstate;   /* JH_SSD1306_SWITCHCAPVCC / JH_SSD1306_EXTERNALVCC. */
    uint32_t clock_hz;  /* I2C clock; 0 selects JH_SSD1306_DEFAULT_I2C_HZ. */
} jh_ssd1306_config_t;

typedef struct {
    jh_ssd1306_config_t config;
    uint16_t width;
    uint16_t height;
    uint8_t i2c_addr;
    uint8_t contrast;
    bool initialized;
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
 * @brief Resolve the framebuffer size in bytes for the configured geometry.
 * @param dev Initialised driver instance.
 * @return Buffer size in bytes, or 0 when not initialised.
 */
size_t jh_ssd1306_buffer_size(const jh_ssd1306_t *dev);

#ifdef __cplusplus
}
#endif
