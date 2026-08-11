#pragma once

#include "hal/network/tls/hal_tls.h"

static inline bool jh_tls_operation_timeout_applies(hal_tls_state_t state) {
  return state == HAL_TLS_STATE_CONNECTING || state == HAL_TLS_STATE_CLOSING;
}
