#include "hal/core/hal_text.h"
#include "hal/system/hal_system.h"

/* Text forms of the device identifiers, shared by every target; each backend
 * only supplies the raw bytes. */

hal_status_t hal_get_device_uid_hex_ex(char *buf, size_t buflen) {
  if (buf == nullptr) {
    return HAL_EINVAL;
  }
  if (buflen < HAL_DEVICE_UID_HEX_BUF_SIZE) {
    return HAL_EOVERFLOW;
  }
  uint8_t uid[HAL_DEVICE_UID_BYTES] = {};
  const hal_status_t status = hal_get_device_uid(uid);
  return status == HAL_OK
             ? hal_text_format_hex_ex(uid, sizeof(uid), true, buf, buflen)
             : status;
}

bool hal_get_device_uid_hex(char *buf, size_t buflen) {
  return hal_status_to_bool(hal_get_device_uid_hex_ex(buf, buflen));
}

hal_status_t hal_get_device_serial_hex_ex(char *buf, size_t buflen) {
  if (buf == nullptr) {
    return HAL_EINVAL;
  }
  uint8_t serial[HAL_DEVICE_SERIAL_MAX_BYTES] = {};
  size_t length = 0u;
  const hal_status_t status =
      hal_get_device_serial_ex(serial, sizeof(serial), &length);
  return status == HAL_OK
             ? hal_text_format_hex_ex(serial, length, true, buf, buflen)
             : status;
}
