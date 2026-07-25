#pragma once

#define HAL_ENABLE_HTTP_CLIENT
#ifndef HAL_ENABLE_TLS
#define HAL_ENABLE_TLS
#endif
#define HAL_ENABLE_TIME

#define HTTP_EXAMPLE_WIFI_SSID "your-ssid"
#define HTTP_EXAMPLE_WIFI_PASSWORD "your-password"
#define HTTP_EXAMPLE_HOST "example.com"

/* #define HTTP_EXAMPLE_CA_AVAILABLE */

/* Define HTTP_EXAMPLE_CA_AVAILABLE and provide ca_certificate.h with:
 *   const unsigned char http_example_ca_der[] = {...};
 *   const unsigned int http_example_ca_der_len = sizeof(...);
 * to enable the HTTPS request alongside plaintext HTTP. */
