#ifndef JH_DEVICE_DEFAULTS_H
#define JH_DEVICE_DEFAULTS_H

/** @file jh_device_defaults.h
 * The C view of the default values of the HAL component types: enum
 * positions, property defaults and child defaults after the values a module
 * sets, as one generated header. The text is byte-identical to the header
 * the Python generator wrote for the same catalogue.
 */
#include "jh_type_catalogue.h"
#ifdef __cplusplus
extern "C" {
#endif

/** Macro token of the device tree specification: camel-case boundaries and
 * runs of other characters become single underscores, upper-cased.
 * name may borrow bytes from the initialized out. Failure preserves out;
 * success assigns the token using out's allocator. NULL name.data is allowed
 * only at length zero. Returns HAL_OK, HAL_EINVAL or a string allocation/size
 * error.
 */
hal_status_t jh_macro_token(jb_json_span_t name, jb_string_t *out);

/** Header text for a checked catalogue. A macro produced twice fails with
 * both origins instead of being renamed; a default the declared C type cannot
 * spell fails with its origin, property and reason. out is initialized and
 * replaced only on success; types must have passed jh_catalogue_check().
 */
hal_status_t jh_render_device_defaults(const jh_catalogue_t *types,
                                       jb_string_t *out, jh_failure_t *failure);

#ifdef __cplusplus
}
#endif
#endif
