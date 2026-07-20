#pragma once

#include "../../../hal_status.h"
#include <lwip/err.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline hal_status_t jh_lwip_status_to_hal(err_t status) {
  switch (status) {
  case ERR_OK:
    return HAL_OK;
  case ERR_MEM:
  case ERR_BUF:
    return HAL_ENOMEM;
  case ERR_TIMEOUT:
    return HAL_ETIMEOUT;
  case ERR_INPROGRESS:
  case ERR_ALREADY:
  case ERR_USE:
    return HAL_EBUSY;
  case ERR_WOULDBLOCK:
    return HAL_EAGAIN;
  case ERR_VAL:
  case ERR_ARG:
    return HAL_EINVAL;
  case ERR_RTE:
  case ERR_ABRT:
  case ERR_RST:
  case ERR_CLSD:
  case ERR_CONN:
  case ERR_IF:
    return HAL_EIO;
  case ERR_ISCONN:
    return HAL_ESTATE;
  default:
    return HAL_EUNKNOWN;
  }
}

#ifdef __cplusplus
}
#endif
