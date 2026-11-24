#ifndef JH_USB_IDENTIFIER_H
#define JH_USB_IDENTIFIER_H

/** @file jh_usb_identifier.h
 * A USB vendor or product identifier of a project's identity section, read
 * as _optional_usb_id of vscode/runtime/serial_identity.py reads it. C11.
 */
#include <jb/json_tree.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What a manifest value gives. */
typedef enum {
  JH_USB_ID_ABSENT,  /**< null, a missing member, a blank string, a float. */
  JH_USB_ID_VALUE,   /**< An identifier in 0..65535. */
  JH_USB_ID_RANGE,   /**< An integer outside 0..65535. */
  JH_USB_ID_INVALID, /**< A string int() rejects. */
} jh_usb_id_kind_t;

/** Read value (a view without a tree means a missing member). A JSON
 * integer is taken as it is. A string is stripped and parsed in base 16 when
 * it starts with 0x, has four characters or holds a-f, otherwise in base 10,
 * with the signs, underscores and Unicode decimal digits int() accepts. Any
 * other value is absent. *out receives the identifier for JH_USB_ID_VALUE.
 * Returns HAL_OK, HAL_EINVAL for NULL outputs, or HAL_ENOMEM/HAL_EINTERNAL
 * when the digit classification cannot run.
 */
hal_status_t jh_usb_identifier(jb_json_tree_view_t value,
                               jh_usb_id_kind_t *kind, uint32_t *out);

#ifdef __cplusplus
}
#endif
#endif
