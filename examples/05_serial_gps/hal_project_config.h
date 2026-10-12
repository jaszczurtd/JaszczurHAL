#pragma once

/* Targets and wiring come from the assemblies in device_tree.json. */

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                 \
  X(SWSERIAL, "GPS and loopback example with software serial",                 \
    EXAMPLE_SERIAL_GPS_USE_SWSERIAL = 1)

#if defined(EXAMPLE_SERIAL_GPS_USE_SWSERIAL) &&                                \
    !(defined(HAL_TARGET_RP2040) || defined(HAL_TARGET_RP2350_ARM) ||          \
      defined(HAL_TARGET_RP2350_RISCV))
#error "05_serial_gps SWSERIAL: software serial runs only on RP targets"
#endif

#define HAL_ENABLE_GPS
#define HAL_ENABLE_SWSERIAL
#define HAL_ENABLE_UART

#if defined(EXAMPLE_SERIAL_GPS_USE_SWSERIAL)
#define HAL_GPS_TRANSPORT_SWSERIAL
#else
#define HAL_GPS_TRANSPORT_UART
/* The GPS opens the UART port of the gpsUart bus in device_tree.json. */
#define HAL_GPS_UART_PORT ((hal_uart_port_t)JH_HW_BUS_GPS_UART_UART_PORT)
#endif

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif
