#pragma once

#include "hal/commands/hal_command_router.h"

#ifdef HAL_ENABLE_COMMAND_ROUTER

/**
 * @file hal_command_wire.h
 * @brief Versioned binary command messages for stream and packet adapters.
 */

#define HAL_COMMAND_WIRE_VERSION 1u
#define HAL_COMMAND_WIRE_HEADER_SIZE 16u
#define HAL_COMMAND_WIRE_MAX_FRAME_SIZE                                        \
  (HAL_COMMAND_WIRE_HEADER_SIZE + HAL_COMMAND_ROUTER_NAME_MAX - 1u +           \
   HAL_COMMAND_MESSAGE_MAX_PAYLOAD)

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  HAL_COMMAND_MESSAGE_REQUEST = 1,
  HAL_COMMAND_MESSAGE_RESPONSE = 2,
  HAL_COMMAND_MESSAGE_EVENT = 3,
} hal_command_message_type_t;

/** @brief Owned decoded message with bounded name and payload storage. */
typedef struct {
  hal_command_message_type_t type;
  hal_command_encoding_t encoding;
  uint32_t request_id;
  hal_status_t status;
  char name[HAL_COMMAND_ROUTER_NAME_MAX];
  uint8_t payload[HAL_COMMAND_MESSAGE_MAX_PAYLOAD];
  size_t payload_length;
} hal_command_message_t;

/**
 * @brief Inspect the first encoded message in a packet or stream buffer.
 *
 * Once the fixed header is available, @p out_frame_length receives the full
 * encoded length. HAL_EAGAIN means more bytes are required. HAL_OK permits
 * trailing bytes, so stream adapters can decode one frame and retain the rest.
 */
hal_status_t hal_command_message_frame_size(const uint8_t *data,
                                            size_t available_length,
                                            size_t *out_frame_length);

/** @brief Encode one validated message into caller-owned storage. */
hal_status_t hal_command_message_encode(const hal_command_message_t *message,
                                        uint8_t *output, size_t output_capacity,
                                        size_t *out_length);

/** @brief Decode and copy exactly one complete message. */
hal_status_t hal_command_message_decode(const uint8_t *data, size_t length,
                                        hal_command_message_t *out_message);

const char *hal_command_message_type_to_string(hal_command_message_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ENABLE_COMMAND_ROUTER */
