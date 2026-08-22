#ifndef JH_NET_VALIDATION_H
#define JH_NET_VALIDATION_H

#include "hal/network/hal_net.h"
#include "hal/serial/hal_serial.h"
#include "jh_net_address_utils.h"

static inline bool jh_net_validate_output(char *out, size_t out_size,
                                          const char *function_name) {
  if (out == NULL || out_size == 0u) {
    hal_derr("%s: output buffer %s", function_name,
             out == NULL ? "is NULL" : "size is 0");
    return false;
  }
  return true;
}

static inline bool jh_net_validate_non_empty(const char *value,
                                             const char *function_name,
                                             const char *argument_name) {
  if (value == NULL || value[0] == '\0') {
    hal_derr("%s: %s is NULL/empty", function_name, argument_name);
    return false;
  }
  return true;
}

static inline hal_status_t
jh_net_validate_supported_endpoint(const hal_net_endpoint_t *endpoint,
                                   bool allow_unspecified_address) {
  const hal_status_t shape =
      jh_net_validate_endpoint_shape(endpoint, true, allow_unspecified_address);
  if (shape != HAL_OK) {
    return shape;
  }
  const hal_net_capabilities_t required =
      endpoint->family == HAL_NET_AF_INET ? HAL_NET_CAP_IPV4 : HAL_NET_CAP_IPV6;
  hal_net_capabilities_t capabilities = 0u;
  const hal_status_t status = hal_net_get_capabilities_ex(&capabilities);
  return status != HAL_OK
             ? status
             : ((capabilities & required) != 0u ? HAL_OK : HAL_EUNSUPPORTED);
}

static inline hal_status_t jh_net_validate_supported_endpoint_logged(
    const hal_net_endpoint_t *endpoint, bool allow_unspecified_address,
    const char *function_name, const char *argument_name) {
  const hal_status_t status =
      jh_net_validate_supported_endpoint(endpoint, allow_unspecified_address);
  if (status != HAL_OK) {
    hal_derr("%s: %s endpoint is %s", function_name, argument_name,
             status == HAL_EUNSUPPORTED ? "unsupported" : "malformed");
  }
  return status;
}

static inline hal_status_t jh_net_prepare_receive_buffer(void *buffer,
                                                         size_t capacity,
                                                         size_t *out_received) {
  if (out_received != NULL) {
    *out_received = 0u;
  }
  return out_received == NULL || (capacity > 0u && buffer == NULL) ? HAL_EINVAL
                                                                   : HAL_OK;
}

static inline hal_status_t
jh_net_public_handle_status(hal_status_t acquire_status) {
  return acquire_status == HAL_ENOMEM ? HAL_ENOMEM : HAL_EINVAL;
}

static inline int jh_net_receive_count(hal_status_t status, size_t received) {
  return status == HAL_OK || status == HAL_EAGAIN || status == HAL_ETIMEOUT
             ? (int)received
             : -1;
}

#endif
