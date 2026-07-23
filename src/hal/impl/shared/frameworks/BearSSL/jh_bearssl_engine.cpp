#include "jh_bearssl_engine.h"
#include "hal/hal_config.h"

#ifdef HAL_ENABLE_TLS

#include "vendor/inc/bearssl.h"

#include <string.h>

static hal_status_t poll_failed(jh_bearssl_poll_result_t *result,
                                int32_t engine_error, hal_status_t status) {
  result->event = JH_BEARSSL_EVENT_FAILED;
  result->engine_error = engine_error;
  return status;
}

static hal_status_t
engine_poll_with_ops(void *engine, const jh_bearssl_engine_ops_t *ops,
                     const jh_bearssl_transport_t *transport,
                     uint16_t step_budget, bool prefer_application_writable,
                     jh_bearssl_poll_result_t *out_result) {
  if (out_result != NULL) {
    memset(out_result, 0, sizeof(*out_result));
  }
  if (engine == NULL || ops == NULL || ops->current_state == NULL ||
      ops->last_error == NULL || ops->send_record_buffer == NULL ||
      ops->send_record_ack == NULL || ops->receive_record_buffer == NULL ||
      ops->receive_record_ack == NULL || transport == NULL ||
      transport->context == NULL || transport->send == NULL ||
      transport->receive == NULL || step_budget == 0u || out_result == NULL) {
    return HAL_EINVAL;
  }

  while (out_result->steps < step_budget) {
    const unsigned state = ops->current_state(engine);
    if ((state & BR_SSL_CLOSED) != 0u) {
      const int32_t error = ops->last_error(engine);
      out_result->engine_error = error;
      out_result->event = error == BR_ERR_OK ? JH_BEARSSL_EVENT_CLOSED
                                             : JH_BEARSSL_EVENT_FAILED;
      return error == BR_ERR_OK ? HAL_OK : HAL_EPROTO;
    }

    /* Application readiness takes precedence when BearSSL also advertises
     * record I/O. Otherwise a completed handshake can starve forever waiting
     * for peer data while writable application space is already available. */
    if ((state & BR_SSL_RECVAPP) != 0u) {
      out_result->event = JH_BEARSSL_EVENT_APPLICATION_READABLE;
      return HAL_OK;
    }
    if (prefer_application_writable && (state & BR_SSL_SENDAPP) != 0u) {
      out_result->event = JH_BEARSSL_EVENT_APPLICATION_WRITABLE;
      return HAL_OK;
    }

    if ((state & BR_SSL_SENDREC) != 0u) {
      size_t length = 0u;
      unsigned char *buffer = ops->send_record_buffer(engine, &length);
      if (buffer == NULL || length == 0u) {
        return poll_failed(out_result, ops->last_error(engine), HAL_EINTERNAL);
      }
      size_t sent = 0u;
      const hal_status_t transport_status =
          transport->send(transport->context, buffer, length, &sent);
      if (transport_status == HAL_OK && sent > 0u && sent <= length) {
        ops->send_record_ack(engine, sent);
        ++out_result->steps;
        continue;
      }
      if (transport_status == HAL_EAGAIN) {
        return HAL_EAGAIN;
      }
      return poll_failed(out_result, ops->last_error(engine),
                         transport_status == HAL_OK ? HAL_EINTERNAL
                                                    : transport_status);
    }

    if ((state & BR_SSL_RECVREC) != 0u) {
      size_t length = 0u;
      unsigned char *buffer = ops->receive_record_buffer(engine, &length);
      if (buffer == NULL || length == 0u) {
        return poll_failed(out_result, ops->last_error(engine), HAL_EINTERNAL);
      }
      size_t received = 0u;
      const hal_status_t transport_status =
          transport->receive(transport->context, buffer, length, &received);
      if (transport_status == HAL_OK && received > 0u && received <= length) {
        ops->receive_record_ack(engine, received);
        ++out_result->steps;
        continue;
      }
      if (transport_status == HAL_EAGAIN) {
        return HAL_EAGAIN;
      }
      return poll_failed(out_result, ops->last_error(engine),
                         transport_status == HAL_OK ? HAL_EINTERNAL
                                                    : transport_status);
    }

    return poll_failed(out_result, ops->last_error(engine), HAL_EINTERNAL);
  }

  /* Observing a terminal/application boundary does not consume transport
   * budget, so report it even when the last permitted I/O step reached it. */
  const unsigned final_state = ops->current_state(engine);
  if ((final_state & BR_SSL_CLOSED) != 0u) {
    const int32_t error = ops->last_error(engine);
    out_result->engine_error = error;
    out_result->event =
        error == BR_ERR_OK ? JH_BEARSSL_EVENT_CLOSED : JH_BEARSSL_EVENT_FAILED;
    return error == BR_ERR_OK ? HAL_OK : HAL_EPROTO;
  }
  if ((final_state & BR_SSL_RECVAPP) != 0u) {
    out_result->event = JH_BEARSSL_EVENT_APPLICATION_READABLE;
    return HAL_OK;
  }
  if ((final_state & BR_SSL_SENDAPP) != 0u) {
    out_result->event = JH_BEARSSL_EVENT_APPLICATION_WRITABLE;
    return HAL_OK;
  }
  return HAL_EAGAIN;
}

hal_status_t jh_bearssl_engine_poll_with_ops(
    void *engine, const jh_bearssl_engine_ops_t *ops,
    const jh_bearssl_transport_t *transport, uint16_t step_budget,
    jh_bearssl_poll_result_t *out_result) {
  return engine_poll_with_ops(engine, ops, transport, step_budget, true,
                              out_result);
}

hal_status_t jh_bearssl_engine_poll_for_read_with_ops(
    void *engine, const jh_bearssl_engine_ops_t *ops,
    const jh_bearssl_transport_t *transport, uint16_t step_budget,
    jh_bearssl_poll_result_t *out_result) {
  return engine_poll_with_ops(engine, ops, transport, step_budget, false,
                              out_result);
}

static unsigned bearssl_current_state(const void *engine) {
  return br_ssl_engine_current_state(
      static_cast<const br_ssl_engine_context *>(engine));
}

static int32_t bearssl_last_error(const void *engine) {
  return br_ssl_engine_last_error(
      static_cast<const br_ssl_engine_context *>(engine));
}

static unsigned char *bearssl_send_record_buffer(void *engine, size_t *length) {
  return br_ssl_engine_sendrec_buf(static_cast<br_ssl_engine_context *>(engine),
                                   length);
}

static void bearssl_send_record_ack(void *engine, size_t length) {
  br_ssl_engine_sendrec_ack(static_cast<br_ssl_engine_context *>(engine),
                            length);
}

static unsigned char *bearssl_receive_record_buffer(void *engine,
                                                    size_t *length) {
  return br_ssl_engine_recvrec_buf(static_cast<br_ssl_engine_context *>(engine),
                                   length);
}

static void bearssl_receive_record_ack(void *engine, size_t length) {
  br_ssl_engine_recvrec_ack(static_cast<br_ssl_engine_context *>(engine),
                            length);
}

hal_status_t jh_bearssl_engine_poll(void *engine,
                                    const jh_bearssl_transport_t *transport,
                                    uint16_t step_budget,
                                    jh_bearssl_poll_result_t *out_result) {
  static const jh_bearssl_engine_ops_t ops = {
      bearssl_current_state,         bearssl_last_error,
      bearssl_send_record_buffer,    bearssl_send_record_ack,
      bearssl_receive_record_buffer, bearssl_receive_record_ack};
  return jh_bearssl_engine_poll_with_ops(engine, &ops, transport, step_budget,
                                         out_result);
}

hal_status_t jh_bearssl_engine_poll_for_read(
    void *engine, const jh_bearssl_transport_t *transport, uint16_t step_budget,
    jh_bearssl_poll_result_t *out_result) {
  static const jh_bearssl_engine_ops_t ops = {
      bearssl_current_state,         bearssl_last_error,
      bearssl_send_record_buffer,    bearssl_send_record_ack,
      bearssl_receive_record_buffer, bearssl_receive_record_ack};
  return jh_bearssl_engine_poll_for_read_with_ops(engine, &ops, transport,
                                                  step_budget, out_result);
}

#endif
