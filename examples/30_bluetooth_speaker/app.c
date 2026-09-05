#include <hal/audio/hal_dma_pwm_audio.h>
#ifdef HAL_SPEAKER_EXAMPLE_ENABLE_BLE
#include <hal/bluetooth/hal_ble.h>
#endif
#include <hal/bluetooth/hal_bluetooth_a2dp_sink.h>
#include <hal/bluetooth/jh_bluetooth_a2dp_memory_probe.h>
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
#include <hal/bluetooth/hal_bluetooth_avrcp_target.h>
#endif
#include <hal/bluetooth/hal_bluetooth_classic.h>
#include <hal/bluetooth/jh_bluetooth_classic_bond_kv_provider.h>
#include <hal/core/hal_app.h>
#include <hal/core/hal_array.h>
#include <hal/serial/hal_serial.h>
#include <hal/storage/hal_eeprom.h>
#include <hal/storage/hal_kv.h>
#include <hal/system/hal_sync.h>
#include <hal/system/hal_system.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  SPEAKER_PWM_PIN = 6u,
  SPEAKER_PWM_PERIOD = 256u,
  SPEAKER_PWM_IDLE = 128u,
  SPEAKER_DMA_FRAMES = 128u,
  SPEAKER_READY_SLOTS = 97u,
  SPEAKER_READY_TARGET = 80u,
  SPEAKER_PREBUFFER_SLOTS = 64u,
  SPEAKER_COMMAND_CAPACITY = 24u,
  SPEAKER_PAIRING_WINDOW_MS = 60000u,
  SPEAKER_WATCHDOG_MS = 4000u,
  SPEAKER_BOND_KEY = 0x30a2u,
  SPEAKER_CLASS_OF_DEVICE = 0x240414u,
  SPEAKER_STACK_GUARD_BYTES = 32u,
  SPEAKER_STACK_PROBE_SAFETY_BYTES = 256u,
  SPEAKER_STACK_PROBE_PATTERN = 0xa5u,
};

extern char __StackBottom;
extern char __StackTop;

static hal_bluetooth_classic_t s_classic = NULL;
static hal_bluetooth_a2dp_sink_t s_a2dp = NULL;
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
static hal_bluetooth_avrcp_target_t s_avrcp = NULL;
#endif
static hal_dma_pwm_audio_t s_audio = NULL;
static jh_bluetooth_classic_bond_kv_context_t s_bond_context;
static uint16_t s_dma_a[SPEAKER_DMA_FRAMES];
static uint16_t s_dma_b[SPEAKER_DMA_FRAMES];
static uint16_t s_ready[SPEAKER_READY_SLOTS][SPEAKER_DMA_FRAMES];
static int16_t s_pcm[HAL_BLUETOOTH_A2DP_PCM_MAX_FRAMES * 2u];
static uint16_t s_staging[SPEAKER_DMA_FRAMES];
static volatile uint8_t s_ready_head;
static volatile uint8_t s_ready_tail;
static volatile uint32_t s_dma_underruns;
static uint8_t s_ready_high_water;
static size_t s_staging_count;
static uint32_t s_audio_rate_hz;
static uint32_t s_adapter_drops;
static uint32_t s_last_report_ms;
static uint32_t s_pairing_decoded_frames;
static uint64_t s_service_busy_us;
static uint64_t s_measurement_started_us;
static uint32_t s_service_max_us;
static hal_status_t s_runtime_status = HAL_NONE;
static hal_reset_reason_t s_boot_reset_reason = HAL_RESET_REASON_UNKNOWN;
static char s_command[SPEAKER_COMMAND_CAPACITY];
static size_t s_command_length;
static bool s_initialized;
static bool s_measurement_started;
static bool s_pairing_window_attempted;
static bool s_pairing_reply_sent;
static bool s_pairing_audio_baseline_valid;
static bool s_boot_watchdog;
#ifdef HAL_SPEAKER_EXAMPLE_ENABLE_BLE
static bool s_ble_initialized;
#endif

static const char *s_device_name = "JaszczurHAL Speaker";

static void stack_probe_start(void) {
  volatile uint8_t marker = 0u;
  const uintptr_t bottom =
      (uintptr_t)&__StackBottom + SPEAKER_STACK_GUARD_BYTES;
  const uintptr_t current = (uintptr_t)&marker;
  if (current <= bottom + SPEAKER_STACK_PROBE_SAFETY_BYTES) {
    return;
  }
  volatile uint8_t *cursor = (volatile uint8_t *)bottom;
  const uintptr_t limit = current - SPEAKER_STACK_PROBE_SAFETY_BYTES;
  while ((uintptr_t)cursor < limit) {
    *cursor++ = SPEAKER_STACK_PROBE_PATTERN;
  }
}

static size_t stack_high_water(void) {
  const uintptr_t bottom =
      (uintptr_t)&__StackBottom + SPEAKER_STACK_GUARD_BYTES;
  const uintptr_t top = (uintptr_t)&__StackTop;
  const volatile uint8_t *cursor = (const volatile uint8_t *)bottom;
  while ((uintptr_t)cursor < top && *cursor == SPEAKER_STACK_PROBE_PATTERN) {
    ++cursor;
  }
  return top - (uintptr_t)cursor;
}

static void fill_silence(uint16_t *buffer) {
  for (size_t index = 0u; index < SPEAKER_DMA_FRAMES; ++index) {
    buffer[index] = SPEAKER_PWM_IDLE;
  }
}

static uint8_t ready_count_snapshot(uint8_t head, uint8_t tail) {
  return tail >= head ? (uint8_t)(tail - head)
                      : (uint8_t)(SPEAKER_READY_SLOTS - head + tail);
}

static uint8_t ready_count(void) {
  return ready_count_snapshot(s_ready_head, s_ready_tail);
}

static void dma_buffer_done(void *user, uint16_t *buffer,
                            uint8_t buffer_index) {
  (void)user;
  (void)buffer_index;
  const uint8_t head = s_ready_head;
  if (head == s_ready_tail) {
    fill_silence(buffer);
    ++s_dma_underruns;
    return;
  }
  memcpy(buffer, s_ready[head], sizeof(s_ready[head]));
  s_ready_head = (uint8_t)((head + 1u) % SPEAKER_READY_SLOTS);
}

static void reset_adapter_queue(void) {
  hal_critical_section_enter();
  s_ready_head = 0u;
  s_ready_tail = 0u;
  s_staging_count = 0u;
  hal_critical_section_exit();
}

static bool publish_staging(void) {
  bool published = false;
  hal_critical_section_enter();
  const uint8_t tail = s_ready_tail;
  const uint8_t next = (uint8_t)((tail + 1u) % SPEAKER_READY_SLOTS);
  if (next != s_ready_head) {
    memcpy(s_ready[tail], s_staging, sizeof(s_staging));
    s_ready_tail = next;
    const uint8_t count = ready_count_snapshot(s_ready_head, next);
    if (count > s_ready_high_water) {
      s_ready_high_water = count;
    }
    published = true;
  }
  hal_critical_section_exit();
  if (!published) {
    ++s_adapter_drops;
  }
  s_staging_count = 0u;
  return published;
}

static void enqueue_pcm(const int16_t *samples, size_t frames) {
  for (size_t index = 0u; index < frames; ++index) {
    const int32_t shifted = (int32_t)samples[index] + 32768;
    s_staging[s_staging_count++] = (uint16_t)((uint32_t)shifted >> 8u);
    if (s_staging_count == SPEAKER_DMA_FRAMES) {
      (void)publish_staging();
    }
  }
}

static void stop_audio(void) {
  if (s_audio != NULL) {
    hal_dma_pwm_audio_destroy(s_audio);
    s_audio = NULL;
  }
  s_audio_rate_hz = 0u;
  reset_adapter_queue();
}

static hal_status_t prepare_audio(uint32_t sample_rate_hz) {
  if (s_audio != NULL && s_audio_rate_hz == sample_rate_hz) {
    return HAL_OK;
  }
  stop_audio();
  fill_silence(s_dma_a);
  fill_silence(s_dma_b);
  const hal_dma_pwm_audio_config_t config = {
      .pwm_pin = SPEAKER_PWM_PIN,
      .sample_rate_hz = sample_rate_hz,
      .period_ticks = SPEAKER_PWM_PERIOD,
      .buffer_a = s_dma_a,
      .buffer_b = s_dma_b,
      .block_size = SPEAKER_DMA_FRAMES,
      .idle_value = SPEAKER_PWM_IDLE,
      .adc_pins = NULL,
      .adc_count = 0u,
      .adc_buffer = NULL,
      .buffer_done_cb = dma_buffer_done,
      .user = NULL,
  };
  const hal_status_t status = hal_dma_pwm_audio_create_ex(&config, &s_audio);
  if (status == HAL_OK) {
    s_audio_rate_hz = sample_rate_hz;
  }
  return status;
}

static void start_audio_when_ready(void) {
  if (s_audio != NULL && !hal_dma_pwm_audio_is_running(s_audio) &&
      ready_count() >= SPEAKER_PREBUFFER_SLOTS) {
    const hal_status_t status = hal_dma_pwm_audio_start_ex(s_audio);
    if (status != HAL_OK) {
      derr("Speaker DMA start failed: %s", hal_status_to_string(status));
    }
  }
}

static void print_info(void) {
  hal_bluetooth_classic_info_t classic = {0};
  hal_bluetooth_a2dp_sink_info_t a2dp = {0};
  const char *avrcp_status_text = hal_status_to_string(HAL_EUNSUPPORTED);
  unsigned avrcp_state = 0u;
  uint32_t avrcp_changes = 0u;
  const hal_status_t classic_status =
      hal_bluetooth_classic_get_info(s_classic, &classic);
  const hal_status_t a2dp_status =
      hal_bluetooth_a2dp_sink_get_info(s_a2dp, &a2dp);
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
  hal_bluetooth_avrcp_target_info_t avrcp = {0};
  const hal_status_t avrcp_status =
      hal_bluetooth_avrcp_target_get_info(s_avrcp, &avrcp);
  avrcp_status_text = hal_status_to_string(avrcp_status);
  avrcp_state = (unsigned)avrcp.state;
  avrcp_changes = avrcp.volume_changes;
#endif
  const uint64_t measurement_us =
      s_measurement_started ? hal_micros64() - s_measurement_started_us : 0u;
  const uint32_t cpu_per_mille =
      measurement_us == 0u
          ? 0u
          : (uint32_t)((s_service_busy_us * UINT64_C(1000)) / measurement_us);
  deb("JHC95 INFO boot=%s watchdog=%u classic=%s/%u peers=%u pairing=%u "
      "window=%u "
      "a2dp=%s/%u avrcp=%s/%u changes=%lu stream=%luHz/%uch packets=%lu "
      "decoded=%lu missing=%lu drop=%lu corrupt=%lu packetQ=%u/%u "
      "pcmQ=%u/%u pcmDrop=%lu underrun=%lu ppm=%ld volume=%u "
      "dmaReady=%u/%u dmaUnderrun=%lu adapterDrop=%lu cpuPermille=%lu "
      "pollMaxUs=%lu",
      hal_reset_reason_str(s_boot_reset_reason), s_boot_watchdog ? 1u : 0u,
      hal_status_to_string(classic_status), (unsigned)classic.state,
      (unsigned)classic.peer_count, classic.pairing_pending ? 1u : 0u,
      classic.pairing_window_open ? 1u : 0u, hal_status_to_string(a2dp_status),
      (unsigned)a2dp.state, avrcp_status_text, avrcp_state,
      (unsigned long)avrcp_changes, (unsigned long)a2dp.format.sample_rate_hz,
      (unsigned)a2dp.format.channels, (unsigned long)a2dp.media_packets,
      (unsigned long)a2dp.decoded_frames, (unsigned long)a2dp.missing_packets,
      (unsigned long)a2dp.dropped_packets, (unsigned long)a2dp.corrupt_frames,
      (unsigned)a2dp.pending_packets, (unsigned)a2dp.packet_queue_high_water,
      (unsigned)a2dp.pending_pcm_blocks, (unsigned)a2dp.pcm_queue_high_water,
      (unsigned long)a2dp.pcm_overflows, (unsigned long)a2dp.pcm_underruns,
      (long)a2dp.clock_correction_ppm, (unsigned)a2dp.volume,
      (unsigned)ready_count(), (unsigned)s_ready_high_water,
      (unsigned long)s_dma_underruns, (unsigned long)s_adapter_drops,
      (unsigned long)cpu_per_mille, (unsigned long)s_service_max_us);

  jh_bluetooth_a2dp_memory_snapshot_t pools = {0};
  jh_bluetooth_a2dp_memory_probe_snapshot(&pools);
  const uint32_t pool_failures =
      (uint32_t)pools.hci_connections.allocation_failures +
      pools.l2cap_services.allocation_failures +
      pools.l2cap_channels.allocation_failures +
      pools.link_keys.allocation_failures +
      pools.service_records.allocation_failures +
      pools.avdtp_endpoints.allocation_failures +
      pools.avdtp_connections.allocation_failures +
      pools.avrcp_connections.allocation_failures;
  deb("JHC95 RESOURCES stack=%u/%u dma=2 pools hci=%u/%u l2svc=%u/%u "
      "l2ch=%u/%u keys=%u/%u sdp=%u/%u avdtpSep=%u/%u avdtpConn=%u/%u "
      "avrcp=%u/%u failures=%lu",
      (unsigned)stack_high_water(),
      (unsigned)((uintptr_t)&__StackTop - (uintptr_t)&__StackBottom),
      (unsigned)pools.hci_connections.high_water,
      (unsigned)pools.hci_connections.capacity,
      (unsigned)pools.l2cap_services.high_water,
      (unsigned)pools.l2cap_services.capacity,
      (unsigned)pools.l2cap_channels.high_water,
      (unsigned)pools.l2cap_channels.capacity,
      (unsigned)pools.link_keys.high_water, (unsigned)pools.link_keys.capacity,
      (unsigned)pools.service_records.high_water,
      (unsigned)pools.service_records.capacity,
      (unsigned)pools.avdtp_endpoints.high_water,
      (unsigned)pools.avdtp_endpoints.capacity,
      (unsigned)pools.avdtp_connections.high_water,
      (unsigned)pools.avdtp_connections.capacity,
      (unsigned)pools.avrcp_connections.high_water,
      (unsigned)pools.avrcp_connections.capacity, (unsigned long)pool_failures);
}

static hal_status_t open_pairing_window(void) {
  const hal_status_t status = hal_bluetooth_classic_pairing_window_open(
      s_classic, SPEAKER_PAIRING_WINDOW_MS);
  if (status == HAL_OK) {
    hal_bluetooth_a2dp_sink_info_t a2dp = {0};
    s_pairing_audio_baseline_valid =
        hal_bluetooth_a2dp_sink_get_info(s_a2dp, &a2dp) == HAL_OK;
    s_pairing_decoded_frames = a2dp.decoded_frames;
    s_pairing_window_attempted = true;
    deb("JHC95 PAIRING open=%ums", SPEAKER_PAIRING_WINDOW_MS);
  }
  return status;
}

static void execute_command(void) {
  s_command[s_command_length] = '\0';
  hal_status_t status = HAL_EINVAL;
  if (strcmp(s_command, "INFO") == 0) {
    print_info();
    status = HAL_OK;
  } else if (strcmp(s_command, "PAIR") == 0) {
    status = open_pairing_window();
  } else if (strcmp(s_command, "RESET") == 0) {
    status = hal_bluetooth_classic_peer_forget_all(s_classic);
    if (status == HAL_OK) {
      s_pairing_window_attempted = true;
      s_pairing_reply_sent = false;
      s_pairing_audio_baseline_valid = false;
      deb("JHC95 FACTORY_RESET bonds=0");
    }
  } else if (strcmp(s_command, "WATCHDOG") == 0) {
    deb("JHC95 WATCHDOG trigger=1");
    for (;;) {
    }
  }
  deb("JHC95 COMMAND name=%s status=%s", s_command,
      hal_status_to_string(status));
  s_command_length = 0u;
}

static void service_commands(void) {
  while (hal_serial_available() > 0) {
    const int value = hal_serial_read();
    if (value < 0) {
      return;
    }
    if (value == '\r') {
      continue;
    }
    if (value == '\n') {
      if (s_command_length != 0u) {
        execute_command();
      }
      continue;
    }
    if (s_command_length + 1u >= sizeof(s_command)) {
      s_command_length = 0u;
      derr("JHC95 command overflow");
      continue;
    }
    s_command[s_command_length++] = (char)value;
  }
}

static void service_pairing_policy(void) {
  hal_bluetooth_classic_info_t info = {0};
  if (hal_bluetooth_classic_get_info(s_classic, &info) != HAL_OK) {
    return;
  }
  if (info.state == HAL_BLUETOOTH_CLASSIC_STATE_READY &&
      info.peer_count == 0u && !info.pairing_window_open &&
      !s_pairing_window_attempted) {
    const hal_status_t status = open_pairing_window();
    if (status != HAL_OK && status != HAL_EBUSY) {
      derr("Speaker pairing window failed: %s", hal_status_to_string(status));
    }
  }
  if (info.peer_count != 0u && info.pairing_window_open &&
      s_pairing_audio_baseline_valid) {
    hal_bluetooth_a2dp_sink_info_t a2dp = {0};
    if (hal_bluetooth_a2dp_sink_get_info(s_a2dp, &a2dp) == HAL_OK &&
        a2dp.decoded_frames != s_pairing_decoded_frames) {
      (void)hal_bluetooth_classic_pairing_window_close(s_classic);
      s_pairing_audio_baseline_valid = false;
      deb("JHC95 PAIRING closed=audio");
    }
  }
  if (!info.pairing_pending) {
    s_pairing_reply_sent = false;
  } else if (info.pairing_window_open && !s_pairing_reply_sent) {
    const hal_status_t status =
        hal_bluetooth_classic_pairing_authorize(s_classic);
    if (status == HAL_OK) {
      s_pairing_reply_sent = true;
      deb("JHC95 PAIRING authorized=1");
    } else {
      derr("Speaker pairing authorization failed: %s",
           hal_status_to_string(status));
    }
  }
}

#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
static void service_avrcp(void) {
  uint8_t volume = 0u;
  const hal_status_t status =
      hal_bluetooth_avrcp_target_volume_next(s_avrcp, &volume);
  if (status == HAL_OK) {
    (void)hal_bluetooth_a2dp_sink_set_volume(s_a2dp, volume);
    (void)hal_bluetooth_avrcp_target_set_volume(s_avrcp, volume);
    deb("JHC95 VOLUME value=%u", (unsigned)volume);
  } else if (status != HAL_EAGAIN) {
    derr("Speaker AVRCP poll failed: %s", hal_status_to_string(status));
  }
}
#endif

static void drain_pcm(void) {
  hal_bluetooth_a2dp_sink_info_t info = {0};
  if (hal_bluetooth_a2dp_sink_get_info(s_a2dp, &info) != HAL_OK) {
    return;
  }
  for (size_t index = 0u;
       index < info.pending_pcm_blocks && ready_count() < SPEAKER_READY_TARGET;
       ++index) {
    hal_bluetooth_a2dp_pcm_block_t block = {0};
    const hal_status_t status =
        hal_bluetooth_a2dp_sink_pcm_next(s_a2dp, s_pcm, COUNTOF(s_pcm), &block);
    if (status == HAL_EAGAIN) {
      return;
    }
    if (status != HAL_OK) {
      derr("Speaker PCM read failed: %s", hal_status_to_string(status));
      return;
    }
    enqueue_pcm(s_pcm, block.frames);
  }
}

static void service_a2dp(void) {
  hal_bluetooth_a2dp_sink_info_t info = {0};
  if (hal_bluetooth_a2dp_sink_get_info(s_a2dp, &info) != HAL_OK) {
    return;
  }
  if (info.state != HAL_BLUETOOTH_A2DP_STATE_STREAMING || !info.format_valid) {
    if (s_audio != NULL) {
      stop_audio();
    }
    return;
  }
  const hal_status_t audio_status = prepare_audio(info.format.sample_rate_hz);
  if (audio_status != HAL_OK) {
    derr("Speaker DMA configure failed: %s",
         hal_status_to_string(audio_status));
    return;
  }
  const hal_status_t status = hal_bluetooth_a2dp_sink_poll(s_a2dp);
  drain_pcm();
  if (status != HAL_OK && status != HAL_EAGAIN && status != HAL_EBUSY &&
      status != HAL_EPROTO && status != HAL_EUNSUPPORTED) {
    derr("Speaker A2DP poll failed: %s", hal_status_to_string(status));
    return;
  }

  hal_bluetooth_classic_info_t classic = {0};
  if (hal_bluetooth_a2dp_sink_get_info(s_a2dp, &info) == HAL_OK &&
      info.first_audio_valid &&
      hal_bluetooth_classic_get_info(s_classic, &classic) == HAL_OK &&
      classic.peer_count == 0u) {
    const hal_status_t save_status = hal_bluetooth_classic_poll(s_classic);
    if (save_status != HAL_OK && save_status != HAL_EBUSY) {
      derr("Speaker bond persistence failed: %s",
           hal_status_to_string(save_status));
      return;
    }
    if (hal_bluetooth_classic_get_info(s_classic, &classic) != HAL_OK ||
        classic.peer_count == 0u) {
      return;
    }
  }
  start_audio_when_ready();
}

static void storage_progress(void *context) {
  (void)context;
  hal_watchdog_feed();
}

static hal_status_t initialize_speaker(void) {
  jh_bluetooth_a2dp_memory_probe_reset();
  hal_status_t status = hal_eeprom_init(HAL_EEPROM_FLASH, 0u, 0u);
  if (status != HAL_OK) {
    return status;
  }
  (void)hal_eeprom_set_progress_callback(storage_progress, NULL);
  status = hal_kv_init_ex(0u, HAL_RP_FLASH_EEPROM_SIZE);
  if (status != HAL_OK) {
    return status;
  }
  const hal_bluetooth_classic_bond_provider_t provider =
      jh_bluetooth_classic_bond_kv_provider(&s_bond_context, SPEAKER_BOND_KEY,
                                            1u);
  status = hal_bluetooth_classic_open_ex(&s_classic, &provider);
  if (status != HAL_OK) {
    return status;
  }
  hal_bluetooth_classic_identity_t identity = {0};
  memcpy(identity.name, s_device_name, strlen(s_device_name) + 1);
  /* Audio + Rendering service classes, Audio/Video major class and the
   * Loudspeaker minor class. Android uses Rendering when matching A2DP sinks.
   */
  identity.class_of_device = SPEAKER_CLASS_OF_DEVICE;
  status = hal_bluetooth_classic_set_identity(s_classic, &identity);
  if (status != HAL_OK) {
    return status;
  }
  const hal_bluetooth_a2dp_sink_config_t a2dp_config = {
      .output_mode = HAL_BLUETOOTH_A2DP_OUTPUT_MONO,
      .initial_volume = 96u,
  };
  status = hal_bluetooth_a2dp_sink_open(s_classic, &a2dp_config, &s_a2dp);
  if (status != HAL_OK) {
    return status;
  }
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
  status = hal_bluetooth_avrcp_target_open(s_classic, 96u, &s_avrcp);
  if (status != HAL_OK) {
    return status;
  }
#endif
#ifdef HAL_SPEAKER_EXAMPLE_ENABLE_BLE
  status = hal_ble_initialize();
  if (status != HAL_OK) {
    return status;
  }
  s_ble_initialized = true;
#endif
  return HAL_OK;
}

void app_start(void) {
  stack_probe_start();
  hal_debug_init_default();
  s_boot_reset_reason = hal_get_reset_reason();
  s_boot_watchdog = hal_watchdog_caused_reboot();
  deb("JHC95 Bluetooth speaker target=%s board=%s pwm=GP%u", HAL_TARGET_NAME,
      HAL_BOARD_PROFILE_NAME, SPEAKER_PWM_PIN);
  deb("JHC95 Reset reason=%s watchdog=%u",
      hal_reset_reason_str(s_boot_reset_reason), s_boot_watchdog ? 1u : 0u);
  deb("JHC95 Commands: INFO, PAIR, RESET, WATCHDOG");
  const hal_status_t watchdog_status =
      hal_watchdog_enable(SPEAKER_WATCHDOG_MS, true);
  if (watchdog_status != HAL_OK) {
    derr("Speaker watchdog unavailable: %s",
         hal_status_to_string(watchdog_status));
  }
}

void app_task0(void) {
  hal_watchdog_feed();
  hal_alive_mark();
  if (!s_initialized) {
    s_initialized = true;
    s_runtime_status = initialize_speaker();
    if (s_runtime_status == HAL_OK) {
      deb("JHC95 READY storage=kv a2dp=1 avrcp=%u ble=%u",
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
          1u,
#else
          0u,
#endif
#ifdef HAL_SPEAKER_EXAMPLE_ENABLE_BLE
          1u
#else
          0u
#endif
      );
    } else {
      derr("Speaker initialization failed: %s",
           hal_status_to_string(s_runtime_status));
    }
  }
  if (s_runtime_status != HAL_OK) {
    hal_delay_ms(1u);
    return;
  }

  if (!s_measurement_started) {
    s_measurement_started = true;
    s_measurement_started_us = hal_micros64();
  }
  const uint64_t service_started_us = hal_micros64();

  const hal_status_t classic_status = hal_bluetooth_classic_poll(s_classic);
  if (classic_status != HAL_OK && classic_status != HAL_EBUSY &&
      classic_status != HAL_EOVERFLOW) {
    derr("Speaker Classic poll failed: %s",
         hal_status_to_string(classic_status));
  }
#ifdef HAL_SPEAKER_EXAMPLE_ENABLE_BLE
  if (s_ble_initialized) {
    const hal_status_t ble_status = hal_ble_poll();
    if (ble_status != HAL_OK) {
      derr("Speaker BLE poll failed: %s", hal_status_to_string(ble_status));
    }
  }
#endif
  service_pairing_policy();
#ifdef HAL_ENABLE_BLUETOOTH_AVRCP_TARGET
  service_avrcp();
#endif
  service_a2dp();
  service_commands();

  const uint64_t service_us = hal_micros64() - service_started_us;
  s_service_busy_us += service_us;
  if (service_us > s_service_max_us) {
    s_service_max_us =
        service_us > UINT32_MAX ? UINT32_MAX : (uint32_t)service_us;
  }

  const uint32_t now = hal_millis();
  if (s_audio == NULL && hal_elapsed_u32(now, s_last_report_ms, 5000u)) {
    s_last_report_ms = now;
    print_info();
  }
  hal_delay_ms(1u);
}
