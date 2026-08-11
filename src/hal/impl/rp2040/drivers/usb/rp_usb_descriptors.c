#include "hal/core/hal_target.h"

#if HAL_TARGET_IS_RP

#include <pico/unique_id.h>
#include <tusb.h>

#ifndef JH_USB_MANUFACTURER
#define JH_USB_MANUFACTURER "Jaszczur"
#endif

#ifndef JH_USB_PRODUCT
#define JH_USB_PRODUCT "JaszczurHAL RP"
#endif

#ifndef JH_USB_VID
#define JH_USB_VID 0x2E8Au
#endif

#ifndef JH_USB_PID
#if HAL_TARGET_IS_RP2040
#define JH_USB_PID 0x000Au
#else
#define JH_USB_PID 0x0009u
#endif
#endif

enum {
  JH_USB_INTERFACE_CDC = 0,
  JH_USB_INTERFACE_COUNT = 2,
};

enum {
  JH_USB_STRING_LANGUAGE = 0,
  JH_USB_STRING_MANUFACTURER,
  JH_USB_STRING_PRODUCT,
  JH_USB_STRING_SERIAL,
  JH_USB_STRING_CDC,
  JH_USB_STRING_COUNT,
};

#define JH_USB_CONFIG_TOTAL_LENGTH (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)
#define JH_USB_ENDPOINT_CDC_COMMAND 0x81u
#define JH_USB_ENDPOINT_CDC_OUT 0x02u
#define JH_USB_ENDPOINT_CDC_IN 0x82u

static const tusb_desc_device_t s_device_descriptor = {
    sizeof(tusb_desc_device_t),
    TUSB_DESC_DEVICE,
    0x0200,
    TUSB_CLASS_MISC,
    MISC_SUBCLASS_COMMON,
    MISC_PROTOCOL_IAD,
    CFG_TUD_ENDPOINT0_SIZE,
    JH_USB_VID,
    JH_USB_PID,
    0x0100,
    JH_USB_STRING_MANUFACTURER,
    JH_USB_STRING_PRODUCT,
    JH_USB_STRING_SERIAL,
    1,
};

static const uint8_t s_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, JH_USB_INTERFACE_COUNT, 0,
                          JH_USB_CONFIG_TOTAL_LENGTH,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 250),
    TUD_CDC_DESCRIPTOR(JH_USB_INTERFACE_CDC, JH_USB_STRING_CDC,
                       JH_USB_ENDPOINT_CDC_COMMAND, 8, JH_USB_ENDPOINT_CDC_OUT,
                       JH_USB_ENDPOINT_CDC_IN, 64),
};

static char s_serial_number[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2u + 1u];
static const char *const s_strings[JH_USB_STRING_COUNT] = {
    "", JH_USB_MANUFACTURER, JH_USB_PRODUCT, s_serial_number, "JaszczurHAL CDC",
};

const uint8_t *tud_descriptor_device_cb(void) {
  return (const uint8_t *)&s_device_descriptor;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return s_configuration_descriptor;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t language_id) {
  (void)language_id;

  enum { JH_USB_DESCRIPTOR_STRING_MAX = 64 };
  static uint16_t descriptor[JH_USB_DESCRIPTOR_STRING_MAX];
  uint8_t length = 0u;

  if (index == JH_USB_STRING_LANGUAGE) {
    descriptor[1] = 0x0409u;
    length = 1u;
  } else {
    if (index >= JH_USB_STRING_COUNT) {
      return NULL;
    }
    if (index == JH_USB_STRING_SERIAL && s_serial_number[0] == '\0') {
      pico_get_unique_board_id_string(s_serial_number, sizeof(s_serial_number));
    }

    const char *text = s_strings[index];
    while (length < (JH_USB_DESCRIPTOR_STRING_MAX - 1u) &&
           text[length] != '\0') {
      descriptor[1u + length] = (uint8_t)text[length];
      ++length;
    }
  }

  descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8u) | (2u * length + 2u));
  return descriptor;
}

#endif
