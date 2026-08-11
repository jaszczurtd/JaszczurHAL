#include "hal/core/hal_config.h"

#ifndef HAL_ENABLE_HTTP_CLIENT
#error "HTTP client must remain enabled"
#endif

#ifndef HAL_ENABLE_TCP
#error "HTTP client must enable its plaintext TCP dependency"
#endif

#ifdef HAL_ENABLE_TLS
#error "Plaintext HTTP client must not implicitly enable TLS"
#endif

int main(void) { return 0; }
