#pragma once

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

/* Entry point is selected by the build system:
 * RP and STM32 use the HAL-owned application entry point. */

#define HAL_ENABLE_BSD_SOCKETS

#ifndef BSD_EXAMPLE_WIFI_SSID
#define BSD_EXAMPLE_WIFI_SSID "your-ssid"
#endif

#ifndef BSD_EXAMPLE_WIFI_PASSWORD
#define BSD_EXAMPLE_WIFI_PASSWORD "your-password"
#endif

#ifndef BSD_EXAMPLE_SERVER_IP
#define BSD_EXAMPLE_SERVER_IP "192.168.1.50"
#endif

#ifndef BSD_EXAMPLE_SERVER_HOST
#define BSD_EXAMPLE_SERVER_HOST BSD_EXAMPLE_SERVER_IP
#endif

#ifndef BSD_EXAMPLE_TCP_PORT
#define BSD_EXAMPLE_TCP_PORT 8080u
#endif

#ifndef BSD_EXAMPLE_UDP_PORT
#define BSD_EXAMPLE_UDP_PORT 9000u
#endif
