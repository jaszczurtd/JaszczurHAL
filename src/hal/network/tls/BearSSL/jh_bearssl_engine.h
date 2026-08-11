#pragma once

#include "hal/core/hal_status.h"
#include "jh_bearssl_transport.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
  JH_BEARSSL_EVENT_NONE = 0,
  JH_BEARSSL_EVENT_APPLICATION_READABLE,
  JH_BEARSSL_EVENT_APPLICATION_WRITABLE,
  JH_BEARSSL_EVENT_CLOSED,
  JH_BEARSSL_EVENT_FAILED
} jh_bearssl_event_t;

typedef struct {
  jh_bearssl_event_t event;
  uint16_t steps;
  int32_t engine_error;
} jh_bearssl_poll_result_t;

/* Private engine seam used to contract-test bounded progression without
 * forging BearSSL internals. State bits match BR_SSL_* values. */
typedef struct {
  unsigned (*current_state)(const void *engine);
  int32_t (*last_error)(const void *engine);
  unsigned char *(*send_record_buffer)(void *engine, size_t *length);
  void (*send_record_ack)(void *engine, size_t length);
  unsigned char *(*receive_record_buffer)(void *engine, size_t *length);
  void (*receive_record_ack)(void *engine, size_t length);
} jh_bearssl_engine_ops_t;

hal_status_t jh_bearssl_engine_poll_with_ops(
    void *engine, const jh_bearssl_engine_ops_t *ops,
    const jh_bearssl_transport_t *transport, uint16_t step_budget,
    jh_bearssl_poll_result_t *out_result);

hal_status_t jh_bearssl_engine_poll_for_read_with_ops(
    void *engine, const jh_bearssl_engine_ops_t *ops,
    const jh_bearssl_transport_t *transport, uint16_t step_budget,
    jh_bearssl_poll_result_t *out_result);

/**
 * Advance a BearSSL engine through at most step_budget transport actions.
 * The transport is always treated as nonblocking. HAL_EAGAIN means that the
 * caller should service the network and invoke this function again.
 */
hal_status_t jh_bearssl_engine_poll(void *engine,
                                    const jh_bearssl_transport_t *transport,
                                    uint16_t step_budget,
                                    jh_bearssl_poll_result_t *out_result);

/** Progress records for a pending application read even when SENDAPP is also
 * advertised. This prevents writable readiness from starving peer records. */
hal_status_t jh_bearssl_engine_poll_for_read(
    void *engine, const jh_bearssl_transport_t *transport, uint16_t step_budget,
    jh_bearssl_poll_result_t *out_result);
