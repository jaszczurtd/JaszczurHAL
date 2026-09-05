#pragma once

#include "hal/bluetooth/hal_bluetooth_classic.h"
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
#include "hal/bluetooth/hal_bluetooth_hid_host.h"
#endif
#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK
#include "hal/bluetooth/hal_bluetooth_a2dp_sink.h"
#endif
#include "hal/core/hal_status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  JH_BLUETOOTH_CLASSIC_EVENT_READY = 0,
  JH_BLUETOOTH_CLASSIC_EVENT_SCAN_RESULT,
  JH_BLUETOOTH_CLASSIC_EVENT_SCAN_STOPPED,
  JH_BLUETOOTH_CLASSIC_EVENT_PAIRING_REQUEST,
  JH_BLUETOOTH_CLASSIC_EVENT_AUTHENTICATION,
  JH_BLUETOOTH_CLASSIC_EVENT_LINK_KEY,
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  JH_BLUETOOTH_CLASSIC_EVENT_HID_CONNECTING,
  JH_BLUETOOTH_CLASSIC_EVENT_HID_CONNECTED,
  JH_BLUETOOTH_CLASSIC_EVENT_HID_DESCRIPTOR,
  JH_BLUETOOTH_CLASSIC_EVENT_HID_REPORT,
  JH_BLUETOOTH_CLASSIC_EVENT_HID_DISCONNECTED,
#endif
#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK
  JH_BLUETOOTH_CLASSIC_EVENT_A2DP_CONNECTED,
  JH_BLUETOOTH_CLASSIC_EVENT_A2DP_FORMAT,
  JH_BLUETOOTH_CLASSIC_EVENT_A2DP_STARTED,
  JH_BLUETOOTH_CLASSIC_EVENT_A2DP_SUSPENDED,
  JH_BLUETOOTH_CLASSIC_EVENT_A2DP_STOPPED,
  JH_BLUETOOTH_CLASSIC_EVENT_A2DP_DISCONNECTED,
  JH_BLUETOOTH_CLASSIC_EVENT_A2DP_MEDIA,
  JH_BLUETOOTH_CLASSIC_EVENT_A2DP_PCM,
#endif
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
  JH_BLUETOOTH_CLASSIC_EVENT_AVRCP_CONNECTED,
  JH_BLUETOOTH_CLASSIC_EVENT_AVRCP_DISCONNECTED,
  JH_BLUETOOTH_CLASSIC_EVENT_AVRCP_VOLUME,
#endif
  JH_BLUETOOTH_CLASSIC_EVENT_ERROR,
} jh_bluetooth_classic_backend_event_type_t;

typedef struct {
  jh_bluetooth_classic_backend_event_type_t type;
  hal_status_t status;
  hal_bluetooth_classic_address_t address;
  hal_bluetooth_classic_scan_result_t scan_result;
  hal_bluetooth_classic_pairing_method_t pairing_method;
  uint8_t link_key[16];
  uint8_t link_key_type;
  bool fatal;
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  uint8_t descriptor[HAL_BLUETOOTH_HID_DESCRIPTOR_MAX_LEN];
  size_t descriptor_length;
  hal_bluetooth_hid_report_t hid_report;
#endif
#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK
  hal_bluetooth_a2dp_sbc_format_t a2dp_format;
  const uint8_t *media_data;
  size_t media_length;
  const int16_t *pcm_data;
  size_t pcm_frames;
  uint8_t pcm_channels;
  uint32_t pcm_sample_rate_hz;
#endif
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
  uint8_t absolute_volume;
#endif
} jh_bluetooth_classic_backend_event_t;

typedef void (*jh_bluetooth_classic_backend_event_fn)(
    void *context, const jh_bluetooth_classic_backend_event_t *event);

typedef struct {
  void *context;
  hal_status_t (*start)(void *context,
                        jh_bluetooth_classic_backend_event_fn event_handler,
                        void *event_context);
  hal_status_t (*stop)(void *context);
  hal_status_t (*service)(void *context);
  hal_status_t (*scan_start)(void *context, uint32_t duration_ms);
  hal_status_t (*scan_stop)(void *context);
  hal_status_t (*sdp_query)(void *context,
                            const hal_bluetooth_classic_address_t *address);
  hal_status_t (*pair)(void *context,
                       const hal_bluetooth_classic_address_t *address);
  hal_status_t (*pairing_reply)(void *context, bool accept);
  hal_status_t (*peer_restore)(void *context,
                               const hal_bluetooth_classic_address_t *address,
                               const uint8_t link_key[16],
                               uint8_t link_key_type);
  hal_status_t (*peer_forget)(void *context,
                              const hal_bluetooth_classic_address_t *address);
  hal_status_t (*identity_set)(
      void *context, const hal_bluetooth_classic_identity_t *identity);
  hal_status_t (*visibility_set)(void *context, bool connectable,
                                 bool discoverable, bool pairing_allowed);
#ifdef HAL_ENABLE_BLUETOOTH_HID_HOST
  hal_status_t (*hid_connect)(void *context,
                              const hal_bluetooth_classic_address_t *address);
  hal_status_t (*hid_disconnect)(void *context);
  hal_status_t (*hid_report_send)(void *context,
                                  const hal_bluetooth_hid_report_t *report);
  hal_status_t (*hid_report_request)(void *context,
                                     hal_bluetooth_hid_report_type_t type,
                                     uint8_t report_id);
#endif
#ifdef HAL_ENABLE_BLUETOOTH_A2DP_SINK
  hal_status_t (*a2dp_attach)(void *context);
  hal_status_t (*a2dp_detach)(void *context);
  hal_status_t (*a2dp_decode)(void *context, const uint8_t *data,
                              size_t length);
#endif
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
  hal_status_t (*avrcp_attach)(void *context, uint8_t initial_volume);
  hal_status_t (*avrcp_detach)(void *context);
  hal_status_t (*avrcp_volume_set)(void *context, uint8_t absolute_volume);
#endif
} jh_bluetooth_classic_backend_t;

const jh_bluetooth_classic_backend_t *
jh_bluetooth_classic_backend_instance(void);

#ifdef __cplusplus
}
#endif
