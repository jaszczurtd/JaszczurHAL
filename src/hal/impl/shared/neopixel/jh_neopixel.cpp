#include "jh_neopixel.h"

#include "../../../hal_system.h"

#include <stdlib.h>
#include <string.h>

static bool uses_three_bytes(const jh_neopixel_t *strip) {
    return strip->w_offset == strip->r_offset;
}

void jh_neopixel_update_type(jh_neopixel_t *strip, uint16_t neo_type) {
    if (!strip) {
        return;
    }

    strip->w_offset = (uint8_t)((neo_type >> 6) & 0x3u);
    strip->r_offset = (uint8_t)((neo_type >> 4) & 0x3u);
    strip->g_offset = (uint8_t)((neo_type >> 2) & 0x3u);
    strip->b_offset = (uint8_t)(neo_type & 0x3u);
    strip->is800khz = (neo_type < 256u);
}

bool jh_neopixel_init(jh_neopixel_t *strip, uint16_t n, uint8_t pin, uint16_t neo_type) {
    if (!strip) {
        return false;
    }

    memset(strip, 0, sizeof(*strip));
    strip->pin = pin;
    jh_neopixel_update_type(strip, neo_type);

    const bool three_bytes = uses_three_bytes(strip);
    strip->num_leds = n;
    strip->num_bytes = (uint16_t)(n * (three_bytes ? 3u : 4u));

    if (strip->num_bytes == 0u) {
        strip->pixels = NULL;
        return true;
    }

    strip->pixels = (uint8_t *)malloc(strip->num_bytes);
    if (!strip->pixels) {
        strip->num_leds = 0u;
        strip->num_bytes = 0u;
        return false;
    }

    memset(strip->pixels, 0, strip->num_bytes);
    return true;
}

void jh_neopixel_deinit(jh_neopixel_t *strip) {
    if (!strip) {
        return;
    }

    free(strip->pixels);
    memset(strip, 0, sizeof(*strip));
}

void jh_neopixel_set_pixel_color_rgb(jh_neopixel_t *strip,
                                     uint16_t n,
                                     uint8_t r,
                                     uint8_t g,
                                     uint8_t b) {
    if (!strip || !strip->pixels || n >= strip->num_leds) {
        return;
    }

    if (strip->brightness) {
        r = (uint8_t)((r * strip->brightness) >> 8);
        g = (uint8_t)((g * strip->brightness) >> 8);
        b = (uint8_t)((b * strip->brightness) >> 8);
    }

    uint8_t *p;
    if (uses_three_bytes(strip)) {
        p = &strip->pixels[n * 3u];
    } else {
        p = &strip->pixels[n * 4u];
        p[strip->w_offset] = 0u;
    }

    p[strip->r_offset] = r;
    p[strip->g_offset] = g;
    p[strip->b_offset] = b;
}

void jh_neopixel_set_pixel_color_rgbw(jh_neopixel_t *strip,
                                      uint16_t n,
                                      uint8_t r,
                                      uint8_t g,
                                      uint8_t b,
                                      uint8_t w) {
    if (!strip || !strip->pixels || n >= strip->num_leds) {
        return;
    }

    if (strip->brightness) {
        r = (uint8_t)((r * strip->brightness) >> 8);
        g = (uint8_t)((g * strip->brightness) >> 8);
        b = (uint8_t)((b * strip->brightness) >> 8);
        w = (uint8_t)((w * strip->brightness) >> 8);
    }

    uint8_t *p;
    if (uses_three_bytes(strip)) {
        p = &strip->pixels[n * 3u];
    } else {
        p = &strip->pixels[n * 4u];
        p[strip->w_offset] = w;
    }

    p[strip->r_offset] = r;
    p[strip->g_offset] = g;
    p[strip->b_offset] = b;
}

void jh_neopixel_set_pixel_color_packed(jh_neopixel_t *strip, uint16_t n, uint32_t c) {
    if (!strip || !strip->pixels || n >= strip->num_leds) {
        return;
    }

    uint8_t r = (uint8_t)(c >> 16);
    uint8_t g = (uint8_t)(c >> 8);
    uint8_t b = (uint8_t)c;

    if (strip->brightness) {
        r = (uint8_t)((r * strip->brightness) >> 8);
        g = (uint8_t)((g * strip->brightness) >> 8);
        b = (uint8_t)((b * strip->brightness) >> 8);
    }

    uint8_t *p;
    if (uses_three_bytes(strip)) {
        p = &strip->pixels[n * 3u];
    } else {
        p = &strip->pixels[n * 4u];
        uint8_t w = (uint8_t)(c >> 24);
        p[strip->w_offset] = strip->brightness ? (uint8_t)((w * strip->brightness) >> 8) : w;
    }

    p[strip->r_offset] = r;
    p[strip->g_offset] = g;
    p[strip->b_offset] = b;
}

uint32_t jh_neopixel_color(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void jh_neopixel_set_brightness(jh_neopixel_t *strip, uint8_t brightness) {
    if (!strip || !strip->pixels) {
        return;
    }

    const uint8_t new_brightness = (uint8_t)(brightness + 1u);
    if (new_brightness == strip->brightness) {
        return;
    }

    const uint8_t old_brightness = (uint8_t)(strip->brightness - 1u);
    uint16_t scale;
    if (old_brightness == 0u) {
        scale = 0u;
    } else if (brightness == 255u) {
        scale = (uint16_t)(65535u / old_brightness);
    } else {
        scale = (uint16_t)((((uint16_t)new_brightness << 8) - 1u) / old_brightness);
    }

    for (uint16_t i = 0; i < strip->num_bytes; ++i) {
        const uint8_t c = strip->pixels[i];
        strip->pixels[i] = (uint8_t)((c * scale) >> 8);
    }

    strip->brightness = new_brightness;
}

void jh_neopixel_clear(jh_neopixel_t *strip) {
    if (!strip || !strip->pixels) {
        return;
    }
    memset(strip->pixels, 0, strip->num_bytes);
}

bool jh_neopixel_can_show(jh_neopixel_t *strip) {
    if (!strip) {
        return false;
    }

    const uint32_t now = hal_micros();
    if (strip->end_time_us > now) {
        strip->end_time_us = now;
    }
    return (uint32_t)(now - strip->end_time_us) >= 300u;
}

bool jh_neopixel_show(jh_neopixel_t *strip, jh_neopixel_write_fn writer, void *user) {
    if (!strip || !strip->pixels || !writer) {
        return false;
    }

    while (!jh_neopixel_can_show(strip)) {
    }

    if (!writer(strip->pixels, strip->num_bytes, strip->is800khz, strip->pin, user)) {
        return false;
    }

    strip->end_time_us = hal_micros();
    return true;
}
