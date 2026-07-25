#pragma once

#include "../../hal_board.h"

#ifdef __cplusplus
extern "C" {
#endif

hal_status_t
jh_board_runtime_set_available(hal_board_capabilities_t capabilities);
hal_status_t jh_board_runtime_set_failed(hal_board_capabilities_t capabilities);
hal_status_t
jh_board_runtime_set_inactive(hal_board_capabilities_t capabilities);

#ifdef __cplusplus
}
#endif
