#pragma once

/**
 * @file hal_uart_config.h
 * @brief HAL-owned UART frame-format constants (HAL_UART_CFG_*).
 *
 * Covers 5/6/7/8 data bits, N/E/O parity, 1/2 stop bits.
 * On Arduino targets the values map directly to SERIAL_* constants;
 * on host/mock builds canonical ArduinoCore-API fallback values are used.
 */

#include <stdint.h>

/*
 * HAL-owned UART frame format constants (5/6/7/8 data bits, N/E/O, 1/2 stop).
 *
 * Values are intentionally kept numerically compatible with Arduino
 * SERIAL_* constants used by SoftwareSerial/HardwareSerial begin().
 *
 * If the toolchain already provides SERIAL_* macros (Arduino build), HAL
 * constants map directly to those values. For host/mock builds, canonical
 * ArduinoCore-API fallback values are used.
 */

/* Fallback bit layout (matches ArduinoCore-API). */
#define HAL_UART_PARITY_EVEN   ((uint16_t)0x0001u)
#define HAL_UART_PARITY_ODD    ((uint16_t)0x0002u)
#define HAL_UART_PARITY_NONE   ((uint16_t)0x0003u)

#define HAL_UART_STOP_BIT_1    ((uint16_t)0x0010u)
#define HAL_UART_STOP_BIT_2    ((uint16_t)0x0030u)

#define HAL_UART_DATA_5        ((uint16_t)0x0100u)
#define HAL_UART_DATA_6        ((uint16_t)0x0200u)
#define HAL_UART_DATA_7        ((uint16_t)0x0300u)
#define HAL_UART_DATA_8        ((uint16_t)0x0400u)

/* N parity, 1 stop bit */
#ifdef SERIAL_5N1
#define HAL_UART_CFG_5N1 ((uint16_t)(SERIAL_5N1))
#else
#define HAL_UART_CFG_5N1 ((uint16_t)(HAL_UART_STOP_BIT_1 | HAL_UART_PARITY_NONE | HAL_UART_DATA_5))
#endif
#ifdef SERIAL_6N1
#define HAL_UART_CFG_6N1 ((uint16_t)(SERIAL_6N1))
#else
#define HAL_UART_CFG_6N1 ((uint16_t)(HAL_UART_STOP_BIT_1 | HAL_UART_PARITY_NONE | HAL_UART_DATA_6))
#endif
#ifdef SERIAL_7N1
#define HAL_UART_CFG_7N1 ((uint16_t)(SERIAL_7N1))
#else
#define HAL_UART_CFG_7N1 ((uint16_t)(HAL_UART_STOP_BIT_1 | HAL_UART_PARITY_NONE | HAL_UART_DATA_7))
#endif
#ifdef SERIAL_8N1
#define HAL_UART_CFG_8N1 ((uint16_t)(SERIAL_8N1))
#else
#define HAL_UART_CFG_8N1 ((uint16_t)(HAL_UART_STOP_BIT_1 | HAL_UART_PARITY_NONE | HAL_UART_DATA_8))
#endif

/* N parity, 2 stop bits */
#ifdef SERIAL_5N2
#define HAL_UART_CFG_5N2 ((uint16_t)(SERIAL_5N2))
#else
#define HAL_UART_CFG_5N2 ((uint16_t)(HAL_UART_STOP_BIT_2 | HAL_UART_PARITY_NONE | HAL_UART_DATA_5))
#endif
#ifdef SERIAL_6N2
#define HAL_UART_CFG_6N2 ((uint16_t)(SERIAL_6N2))
#else
#define HAL_UART_CFG_6N2 ((uint16_t)(HAL_UART_STOP_BIT_2 | HAL_UART_PARITY_NONE | HAL_UART_DATA_6))
#endif
#ifdef SERIAL_7N2
#define HAL_UART_CFG_7N2 ((uint16_t)(SERIAL_7N2))
#else
#define HAL_UART_CFG_7N2 ((uint16_t)(HAL_UART_STOP_BIT_2 | HAL_UART_PARITY_NONE | HAL_UART_DATA_7))
#endif
#ifdef SERIAL_8N2
#define HAL_UART_CFG_8N2 ((uint16_t)(SERIAL_8N2))
#else
#define HAL_UART_CFG_8N2 ((uint16_t)(HAL_UART_STOP_BIT_2 | HAL_UART_PARITY_NONE | HAL_UART_DATA_8))
#endif

/* E parity, 1 stop bit */
#ifdef SERIAL_5E1
#define HAL_UART_CFG_5E1 ((uint16_t)(SERIAL_5E1))
#else
#define HAL_UART_CFG_5E1 ((uint16_t)(HAL_UART_STOP_BIT_1 | HAL_UART_PARITY_EVEN | HAL_UART_DATA_5))
#endif
#ifdef SERIAL_6E1
#define HAL_UART_CFG_6E1 ((uint16_t)(SERIAL_6E1))
#else
#define HAL_UART_CFG_6E1 ((uint16_t)(HAL_UART_STOP_BIT_1 | HAL_UART_PARITY_EVEN | HAL_UART_DATA_6))
#endif
#ifdef SERIAL_7E1
#define HAL_UART_CFG_7E1 ((uint16_t)(SERIAL_7E1))
#else
#define HAL_UART_CFG_7E1 ((uint16_t)(HAL_UART_STOP_BIT_1 | HAL_UART_PARITY_EVEN | HAL_UART_DATA_7))
#endif
#ifdef SERIAL_8E1
#define HAL_UART_CFG_8E1 ((uint16_t)(SERIAL_8E1))
#else
#define HAL_UART_CFG_8E1 ((uint16_t)(HAL_UART_STOP_BIT_1 | HAL_UART_PARITY_EVEN | HAL_UART_DATA_8))
#endif

/* E parity, 2 stop bits */
#ifdef SERIAL_5E2
#define HAL_UART_CFG_5E2 ((uint16_t)(SERIAL_5E2))
#else
#define HAL_UART_CFG_5E2 ((uint16_t)(HAL_UART_STOP_BIT_2 | HAL_UART_PARITY_EVEN | HAL_UART_DATA_5))
#endif
#ifdef SERIAL_6E2
#define HAL_UART_CFG_6E2 ((uint16_t)(SERIAL_6E2))
#else
#define HAL_UART_CFG_6E2 ((uint16_t)(HAL_UART_STOP_BIT_2 | HAL_UART_PARITY_EVEN | HAL_UART_DATA_6))
#endif
#ifdef SERIAL_7E2
#define HAL_UART_CFG_7E2 ((uint16_t)(SERIAL_7E2))
#else
#define HAL_UART_CFG_7E2 ((uint16_t)(HAL_UART_STOP_BIT_2 | HAL_UART_PARITY_EVEN | HAL_UART_DATA_7))
#endif
#ifdef SERIAL_8E2
#define HAL_UART_CFG_8E2 ((uint16_t)(SERIAL_8E2))
#else
#define HAL_UART_CFG_8E2 ((uint16_t)(HAL_UART_STOP_BIT_2 | HAL_UART_PARITY_EVEN | HAL_UART_DATA_8))
#endif

/* O parity, 1 stop bit */
#ifdef SERIAL_5O1
#define HAL_UART_CFG_5O1 ((uint16_t)(SERIAL_5O1))
#else
#define HAL_UART_CFG_5O1 ((uint16_t)(HAL_UART_STOP_BIT_1 | HAL_UART_PARITY_ODD | HAL_UART_DATA_5))
#endif
#ifdef SERIAL_6O1
#define HAL_UART_CFG_6O1 ((uint16_t)(SERIAL_6O1))
#else
#define HAL_UART_CFG_6O1 ((uint16_t)(HAL_UART_STOP_BIT_1 | HAL_UART_PARITY_ODD | HAL_UART_DATA_6))
#endif
#ifdef SERIAL_7O1
#define HAL_UART_CFG_7O1 ((uint16_t)(SERIAL_7O1))
#else
#define HAL_UART_CFG_7O1 ((uint16_t)(HAL_UART_STOP_BIT_1 | HAL_UART_PARITY_ODD | HAL_UART_DATA_7))
#endif
#ifdef SERIAL_8O1
#define HAL_UART_CFG_8O1 ((uint16_t)(SERIAL_8O1))
#else
#define HAL_UART_CFG_8O1 ((uint16_t)(HAL_UART_STOP_BIT_1 | HAL_UART_PARITY_ODD | HAL_UART_DATA_8))
#endif

/* O parity, 2 stop bits */
#ifdef SERIAL_5O2
#define HAL_UART_CFG_5O2 ((uint16_t)(SERIAL_5O2))
#else
#define HAL_UART_CFG_5O2 ((uint16_t)(HAL_UART_STOP_BIT_2 | HAL_UART_PARITY_ODD | HAL_UART_DATA_5))
#endif
#ifdef SERIAL_6O2
#define HAL_UART_CFG_6O2 ((uint16_t)(SERIAL_6O2))
#else
#define HAL_UART_CFG_6O2 ((uint16_t)(HAL_UART_STOP_BIT_2 | HAL_UART_PARITY_ODD | HAL_UART_DATA_6))
#endif
#ifdef SERIAL_7O2
#define HAL_UART_CFG_7O2 ((uint16_t)(SERIAL_7O2))
#else
#define HAL_UART_CFG_7O2 ((uint16_t)(HAL_UART_STOP_BIT_2 | HAL_UART_PARITY_ODD | HAL_UART_DATA_7))
#endif
#ifdef SERIAL_8O2
#define HAL_UART_CFG_8O2 ((uint16_t)(SERIAL_8O2))
#else
#define HAL_UART_CFG_8O2 ((uint16_t)(HAL_UART_STOP_BIT_2 | HAL_UART_PARITY_ODD | HAL_UART_DATA_8))
#endif

/*
 * Backward-compat aliases for existing code that still passes SERIAL_*.
 * Arduino cores provide these names in core headers. Defining them here
 * on Arduino targets causes macro redefinition warnings when core headers
 * are included later, so keep aliases only for host/mock builds.
 */
#if !defined(ARDUINO)
#ifndef SERIAL_7N1
#define SERIAL_7N1 HAL_UART_CFG_7N1
#endif

#ifndef SERIAL_8N1
#define SERIAL_8N1 HAL_UART_CFG_8N1
#endif

#ifndef SERIAL_5N1
#define SERIAL_5N1 HAL_UART_CFG_5N1
#endif
#ifndef SERIAL_6N1
#define SERIAL_6N1 HAL_UART_CFG_6N1
#endif
#ifndef SERIAL_5N2
#define SERIAL_5N2 HAL_UART_CFG_5N2
#endif
#ifndef SERIAL_6N2
#define SERIAL_6N2 HAL_UART_CFG_6N2
#endif
#ifndef SERIAL_7N2
#define SERIAL_7N2 HAL_UART_CFG_7N2
#endif
#ifndef SERIAL_8N2
#define SERIAL_8N2 HAL_UART_CFG_8N2
#endif

#ifndef SERIAL_5E1
#define SERIAL_5E1 HAL_UART_CFG_5E1
#endif
#ifndef SERIAL_6E1
#define SERIAL_6E1 HAL_UART_CFG_6E1
#endif
#ifndef SERIAL_7E1
#define SERIAL_7E1 HAL_UART_CFG_7E1
#endif
#ifndef SERIAL_8E1
#define SERIAL_8E1 HAL_UART_CFG_8E1
#endif
#ifndef SERIAL_5E2
#define SERIAL_5E2 HAL_UART_CFG_5E2
#endif
#ifndef SERIAL_6E2
#define SERIAL_6E2 HAL_UART_CFG_6E2
#endif
#ifndef SERIAL_7E2
#define SERIAL_7E2 HAL_UART_CFG_7E2
#endif
#ifndef SERIAL_8E2
#define SERIAL_8E2 HAL_UART_CFG_8E2
#endif

#ifndef SERIAL_5O1
#define SERIAL_5O1 HAL_UART_CFG_5O1
#endif
#ifndef SERIAL_6O1
#define SERIAL_6O1 HAL_UART_CFG_6O1
#endif
#ifndef SERIAL_7O1
#define SERIAL_7O1 HAL_UART_CFG_7O1
#endif
#ifndef SERIAL_8O1
#define SERIAL_8O1 HAL_UART_CFG_8O1
#endif
#ifndef SERIAL_5O2
#define SERIAL_5O2 HAL_UART_CFG_5O2
#endif
#ifndef SERIAL_6O2
#define SERIAL_6O2 HAL_UART_CFG_6O2
#endif
#ifndef SERIAL_7O2
#define SERIAL_7O2 HAL_UART_CFG_7O2
#endif
#ifndef SERIAL_8O2
#define SERIAL_8O2 HAL_UART_CFG_8O2
#endif
#endif
