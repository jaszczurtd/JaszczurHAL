#pragma once

/** @file Shared serial/debug formatting helpers. */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef HAL_DEBUG_COLOR_ERRORS
#define HAL_DEBUG_COLOR_ERRORS 1
#endif

#if HAL_DEBUG_COLOR_ERRORS
#define HAL_DEBUG_ERROR_PREFIX "\x1b[1;31mERROR!\x1b[0m "
#else
#define HAL_DEBUG_ERROR_PREFIX "ERROR! "
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*hal_debug_format_write_fn)(void *ctx, const char *data,
                                          size_t len);

void hal_debug_format_write_cstr(hal_debug_format_write_fn write, void *ctx,
                                 const char *text);
void hal_debug_format_write_error_prefix(hal_debug_format_write_fn write,
                                         void *ctx, const char *timestamp);
void hal_debug_format_write_deb_prefix(hal_debug_format_write_fn write,
                                       void *ctx, const char *prefix);
void hal_debug_format_write_source_prefix(hal_debug_format_write_fn write,
                                          void *ctx, const char *source);
void hal_debug_format_write_isr_prefix(hal_debug_format_write_fn write,
                                       void *ctx, bool error_level,
                                       const char *prefix,
                                       const char *timestamp_us);
void hal_debug_vformat(hal_debug_format_write_fn write, void *ctx,
                       const char *format, va_list args);

#ifdef __cplusplus
}
#endif
