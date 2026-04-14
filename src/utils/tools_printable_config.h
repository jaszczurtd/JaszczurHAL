#ifndef JASZCZURHAL_TOOLS_PRINTABLE_CONFIG_H
#define JASZCZURHAL_TOOLS_PRINTABLE_CONFIG_H

/**
 * @file tools_printable_config.h
 * @brief String/print buffer defaults and compatibility aliases.
 */

/** @brief Default generic text buffer size used by printable helpers. */
#ifndef HAL_TOOLS_PRINTABLE_BUFFER_SIZE
#define HAL_TOOLS_PRINTABLE_BUFFER_SIZE 512
#endif

/** @brief Default prefix buffer size used by printable/debug helpers. */
#ifndef HAL_TOOLS_PRINTABLE_PREFIX_SIZE
#define HAL_TOOLS_PRINTABLE_PREFIX_SIZE 16
#endif

/* Backward-compatible aliases used by legacy code. */
/** @brief Legacy alias for @ref HAL_TOOLS_PRINTABLE_BUFFER_SIZE. */
#ifndef PRINTABLE_BUFFER_SIZE
#define PRINTABLE_BUFFER_SIZE HAL_TOOLS_PRINTABLE_BUFFER_SIZE
#endif
/** @brief Legacy alias for @ref HAL_TOOLS_PRINTABLE_PREFIX_SIZE. */
#ifndef PRINTABLE_PREFIX_SIZE
#define PRINTABLE_PREFIX_SIZE HAL_TOOLS_PRINTABLE_PREFIX_SIZE
#endif

#endif
