#include "hal/commands/hal_command_wire.h"

#ifdef HAL_ENABLE_COMMAND_ROUTER

#include "hal/commands/jh_command_router_internal.h"
#include "hal/core/jh_endian.h"

#include <limits.h>
#include <string.h>

static bool message_type_valid(hal_command_message_type_t type) {
  return type == HAL_COMMAND_MESSAGE_REQUEST ||
         type == HAL_COMMAND_MESSAGE_RESPONSE ||
         type == HAL_COMMAND_MESSAGE_EVENT;
}

static bool message_status_valid(hal_status_t status) {
  return status >= HAL_EUNKNOWN && status <= HAL_OK;
}

static hal_status_t validate_message(const hal_command_message_t *message,
                                     size_t *out_name_length) {
  if (message == NULL || !message_type_valid(message->type) ||
      !jh_command_encoding_valid(message->encoding) ||
      !message_status_valid(message->status) ||
      message->payload_length > HAL_COMMAND_MESSAGE_MAX_PAYLOAD) {
    return HAL_EINVAL;
  }

  size_t name_length = 0u;
  if (message->type == HAL_COMMAND_MESSAGE_RESPONSE) {
    if (message->request_id == 0u || message->status == HAL_NONE ||
        message->name[0] != '\0') {
      return HAL_EINVAL;
    }
  } else {
    if (!jh_command_name_valid(message->name, &name_length) ||
        message->status != HAL_NONE) {
      return HAL_EINVAL;
    }
    if ((message->type == HAL_COMMAND_MESSAGE_REQUEST &&
         message->request_id == 0u) ||
        (message->type == HAL_COMMAND_MESSAGE_EVENT &&
         message->request_id != 0u)) {
      return HAL_EINVAL;
    }
  }
  *out_name_length = name_length;
  return HAL_OK;
}

hal_status_t hal_command_message_frame_size(const uint8_t *data,
                                            size_t available_length,
                                            size_t *out_frame_length) {
  if (data == NULL || out_frame_length == NULL) {
    return HAL_EINVAL;
  }
  *out_frame_length = HAL_COMMAND_WIRE_HEADER_SIZE;
  if (available_length < HAL_COMMAND_WIRE_HEADER_SIZE) {
    return HAL_EAGAIN;
  }
  if (data[0] != (uint8_t)'J' || data[1] != (uint8_t)'C' ||
      data[2] != HAL_COMMAND_WIRE_VERSION || data[6] != 0u || data[7] != 0u) {
    return HAL_EPROTO;
  }

  const hal_command_message_type_t type = (hal_command_message_type_t)data[3];
  const hal_command_encoding_t encoding = (hal_command_encoding_t)data[4];
  const size_t name_length = data[5];
  const uint32_t request_id = jh_load_be32(data + 8u);
  const uint16_t raw_status = jh_load_be16(data + 12u);
  const int32_t signed_status = raw_status <= INT16_MAX
                                    ? (int32_t)raw_status
                                    : (int32_t)raw_status - 65536;
  const hal_status_t status = (hal_status_t)signed_status;
  const size_t payload_length = jh_load_be16(data + 14u);

  if (!message_type_valid(type) || !jh_command_encoding_valid(encoding) ||
      !message_status_valid(status) ||
      name_length >= HAL_COMMAND_ROUTER_NAME_MAX ||
      payload_length > HAL_COMMAND_MESSAGE_MAX_PAYLOAD) {
    return HAL_EPROTO;
  }
  if ((type == HAL_COMMAND_MESSAGE_RESPONSE &&
       (request_id == 0u || status == HAL_NONE || name_length != 0u)) ||
      (type == HAL_COMMAND_MESSAGE_REQUEST &&
       (request_id == 0u || status != HAL_NONE || name_length == 0u)) ||
      (type == HAL_COMMAND_MESSAGE_EVENT &&
       (request_id != 0u || status != HAL_NONE || name_length == 0u))) {
    return HAL_EPROTO;
  }

  *out_frame_length =
      HAL_COMMAND_WIRE_HEADER_SIZE + name_length + payload_length;
  return available_length < *out_frame_length ? HAL_EAGAIN : HAL_OK;
}

hal_status_t hal_command_message_encode(const hal_command_message_t *message,
                                        uint8_t *output, size_t output_capacity,
                                        size_t *out_length) {
  if (out_length == NULL) {
    return HAL_EINVAL;
  }
  *out_length = 0u;
  size_t name_length = 0u;
  hal_status_t status = validate_message(message, &name_length);
  if (status != HAL_OK || output == NULL) {
    return status == HAL_OK ? HAL_EINVAL : status;
  }
  const size_t required =
      HAL_COMMAND_WIRE_HEADER_SIZE + name_length + message->payload_length;
  *out_length = required;
  if (required > output_capacity) {
    return HAL_EOVERFLOW;
  }

  output[0] = (uint8_t)'J';
  output[1] = (uint8_t)'C';
  output[2] = HAL_COMMAND_WIRE_VERSION;
  output[3] = (uint8_t)message->type;
  output[4] = (uint8_t)message->encoding;
  output[5] = (uint8_t)name_length;
  output[6] = 0u;
  output[7] = 0u;
  jh_store_be32(output + 8u, message->request_id);
  jh_store_be16(output + 12u, (uint16_t)(int16_t)message->status);
  jh_store_be16(output + 14u, (uint16_t)message->payload_length);
  if (name_length > 0u) {
    memcpy(output + HAL_COMMAND_WIRE_HEADER_SIZE, message->name, name_length);
  }
  if (message->payload_length > 0u) {
    memcpy(output + HAL_COMMAND_WIRE_HEADER_SIZE + name_length,
           message->payload, message->payload_length);
  }
  return HAL_OK;
}

hal_status_t hal_command_message_decode(const uint8_t *data, size_t length,
                                        hal_command_message_t *out_message) {
  if (data == NULL || out_message == NULL) {
    return HAL_EINVAL;
  }
  memset(out_message, 0, sizeof(*out_message));
  size_t frame_length = 0u;
  const hal_status_t frame_status =
      hal_command_message_frame_size(data, length, &frame_length);
  if (frame_status != HAL_OK || frame_length != length) {
    return HAL_EPROTO;
  }

  out_message->type = (hal_command_message_type_t)data[3];
  out_message->encoding = (hal_command_encoding_t)data[4];
  const size_t name_length = data[5];
  out_message->request_id = jh_load_be32(data + 8u);
  const uint16_t raw_status = jh_load_be16(data + 12u);
  const int32_t signed_status = raw_status <= INT16_MAX
                                    ? (int32_t)raw_status
                                    : (int32_t)raw_status - 65536;
  out_message->status = (hal_status_t)signed_status;
  out_message->payload_length = jh_load_be16(data + 14u);

  const size_t expected =
      HAL_COMMAND_WIRE_HEADER_SIZE + name_length + out_message->payload_length;
  if (name_length >= sizeof(out_message->name) ||
      out_message->payload_length > sizeof(out_message->payload) ||
      expected != length) {
    memset(out_message, 0, sizeof(*out_message));
    return HAL_EPROTO;
  }
  if (name_length > 0u) {
    memcpy(out_message->name, data + HAL_COMMAND_WIRE_HEADER_SIZE, name_length);
  }
  out_message->name[name_length] = '\0';
  if (out_message->payload_length > 0u) {
    memcpy(out_message->payload,
           data + HAL_COMMAND_WIRE_HEADER_SIZE + name_length,
           out_message->payload_length);
  }

  size_t validated_name_length = 0u;
  hal_status_t status = validate_message(out_message, &validated_name_length);
  if (status != HAL_OK || validated_name_length != name_length) {
    memset(out_message, 0, sizeof(*out_message));
    return HAL_EPROTO;
  }
  return HAL_OK;
}

const char *
hal_command_message_type_to_string(hal_command_message_type_t type) {
  switch (type) {
  case HAL_COMMAND_MESSAGE_REQUEST:
    return "REQUEST";
  case HAL_COMMAND_MESSAGE_RESPONSE:
    return "RESPONSE";
  case HAL_COMMAND_MESSAGE_EVENT:
    return "EVENT";
  default:
    return "UNKNOWN";
  }
}

#endif /* HAL_ENABLE_COMMAND_ROUTER */
