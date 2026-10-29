#ifndef JH_DISPLAY_SPI_PANEL_MODEL_H
#define JH_DISPLAY_SPI_PANEL_MODEL_H

#include "hal/core/jh_endian.h"
#include "hal/impl/.mock/hal_mock.h"

#include <stdint.h>
#include <string.h>

/* A small MIPI DCS panel on the mock SPI bus. Bytes sent with DC low are
 * commands and with DC high their data: CASET and PASET set the window,
 * RAMWR writes big-endian RGB565 pixels into it row by row. The model also
 * counts the pixel bytes of every chip-select cycle, which shows how long a
 * single transaction kept the shared bus. */

#define PANEL_MODEL_SIZE 320u

typedef struct {
  uint8_t dc_pin;
  uint16_t pixels[PANEL_MODEL_SIZE][PANEL_MODEL_SIZE]; /* [y][x] */
  uint8_t command;
  uint8_t args[4];
  size_t arg_count;
  uint16_t x0, x1, y0, y1, x, y;
  bool writing;
  bool have_high_byte;
  uint8_t high_byte;
  size_t select_pixel_bytes;
  size_t max_select_pixel_bytes;
  size_t pixel_selections;
  size_t selections;
} panel_model_t;

static void panel_model_select(void *user) {
  panel_model_t *panel = (panel_model_t *)user;
  panel->select_pixel_bytes = 0u;
  panel->selections++;
}

static uint8_t panel_model_exchange(void *user, uint8_t mosi) {
  panel_model_t *m = (panel_model_t *)user;
  if (!hal_mock_gpio_get_state(m->dc_pin)) {
    m->command = mosi;
    m->arg_count = 0u;
    m->writing = (mosi == 0x2Cu);
    m->x = m->x0;
    m->y = m->y0;
    m->have_high_byte = false;
    return 0xFFu;
  }
  if (m->command == 0x2Au || m->command == 0x2Bu) {
    if (m->arg_count < sizeof(m->args)) {
      m->args[m->arg_count++] = mosi;
    }
    if (m->arg_count == sizeof(m->args)) {
      const uint16_t first = jh_load_be16(&m->args[0]);
      const uint16_t last = jh_load_be16(&m->args[2]);
      if (m->command == 0x2Au) {
        m->x0 = first;
        m->x1 = last;
      } else {
        m->y0 = first;
        m->y1 = last;
      }
    }
    return 0xFFu;
  }
  if (!m->writing) {
    return 0xFFu;
  }
  m->select_pixel_bytes++;
  if (!m->have_high_byte) {
    m->high_byte = mosi;
    m->have_high_byte = true;
    return 0xFFu;
  }
  m->have_high_byte = false;
  if (m->x < PANEL_MODEL_SIZE && m->y < PANEL_MODEL_SIZE) {
    m->pixels[m->y][m->x] = (uint16_t)(((uint16_t)m->high_byte << 8) | mosi);
  }
  if (m->x >= m->x1) {
    m->x = m->x0;
    m->y++;
  } else {
    m->x++;
  }
  return 0xFFu;
}

static void panel_model_deselect(void *user) {
  panel_model_t *m = (panel_model_t *)user;
  if (m->select_pixel_bytes > 0u) {
    m->pixel_selections++;
    if (m->select_pixel_bytes > m->max_select_pixel_bytes) {
      m->max_select_pixel_bytes = m->select_pixel_bytes;
    }
  }
}

/* Attach @p m on @p bus behind @p cs_pin; call after the driver init. */
static void panel_model_attach(panel_model_t *m, uint8_t bus, uint8_t cs_pin,
                               uint8_t dc_pin) {
  static const hal_mock_spi_device_t device = {
      panel_model_select, panel_model_exchange, panel_model_deselect};
  memset(m, 0, sizeof(*m));
  m->dc_pin = dc_pin;
  hal_mock_spi_attach_device(bus, cs_pin, &device, m);
}

/* True when the @p w x @p h panel area at (@p x, @p y) holds @p expected,
 * row by row. */
static bool panel_model_area_equals(const panel_model_t *m, uint16_t x,
                                    uint16_t y, uint16_t w, uint16_t h,
                                    const uint16_t *expected) {
  for (uint16_t row = 0u; row < h; ++row) {
    for (uint16_t col = 0u; col < w; ++col) {
      if (m->pixels[y + row][x + col] != expected[(size_t)row * w + col]) {
        return false;
      }
    }
  }
  return true;
}

#endif
