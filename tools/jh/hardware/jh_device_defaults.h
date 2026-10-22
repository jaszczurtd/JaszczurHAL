#ifndef JH_DEVICE_DEFAULTS_H
#define JH_DEVICE_DEFAULTS_H

/** @file jh_device_defaults.h
 * The C view of the default values of the HAL component types: enum
 * positions, property defaults and child defaults after the values a module
 * sets, as one generated header. The text is byte-identical to the header
 * the Python generator wrote for the same catalogue.
 */
#include "jh_type_catalogue.h"
#include <string>

namespace jh::hardware {

/** Macro token of the device tree specification: camel-case boundaries and
 * runs of other characters become single underscores, upper-cased.
 */
std::string macro_token(const std::string &name);

/** Header text for a checked catalogue. A macro produced twice fails with
 * both origins instead of being renamed; a default the declared C type cannot
 * spell fails with its origin, property and reason.
 */
std::string render_device_defaults(const catalogue &types);

} // namespace jh::hardware
#endif
