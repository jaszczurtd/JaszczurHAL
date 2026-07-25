#include "jh_flash_transaction_engine.h"

hal_status_t jh_flash_transaction_engine_execute(
    const jh_flash_transaction_backend_t *backend, void *backend_context,
    jh_flash_transaction_operation_t operation, void *operation_context,
    uint32_t timeout_ms) {
  if (backend == nullptr || backend->acquire == nullptr ||
      backend->quiesce == nullptr || backend->execute == nullptr ||
      backend->resume == nullptr || backend->release == nullptr ||
      operation == nullptr) {
    return HAL_EINVAL;
  }

  hal_status_t status = backend->acquire(backend_context, timeout_ms);
  if (status != HAL_OK) {
    return status;
  }

  bool quiesced = false;
  status = backend->quiesce(backend_context, timeout_ms);
  if (status == HAL_OK) {
    quiesced = true;
    status = backend->execute(backend_context, operation, operation_context,
                              timeout_ms);
  }

  if (quiesced) {
    const hal_status_t resume_status = backend->resume(backend_context);
    if (status == HAL_OK) {
      status = resume_status;
    }
  }

  const hal_status_t release_status = backend->release(backend_context);
  if (status == HAL_OK) {
    status = release_status;
  }
  return status;
}
