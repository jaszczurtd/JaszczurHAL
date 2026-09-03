#include <hal/bluetooth/hal_gamepad.h>
#include <hal/core/hal_app.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>

#if defined(HAL_GAMEPAD_EXAMPLE_ENABLE_BLE)
#include <hal/bluetooth/hal_ble.h>
#endif

static hal_gamepad_t s_gamepad = NULL;
static hal_status_t s_runtimeStatus = HAL_NONE;
static hal_gamepad_state_t s_previousState = HAL_GAMEPAD_STATE_UNINITIALIZED;
static bool s_started = false;
static bool s_pairingOpened = false;
static bool s_reconnectStarted = false;

static const char *stateName(hal_gamepad_state_t state) {
  switch (state) {
  case HAL_GAMEPAD_STATE_UNINITIALIZED:
    return "uninitialized";
  case HAL_GAMEPAD_STATE_STARTING:
    return "starting";
  case HAL_GAMEPAD_STATE_READY:
    return "ready";
  case HAL_GAMEPAD_STATE_DISCOVERING:
    return "discovering";
  case HAL_GAMEPAD_STATE_CONNECTING:
    return "connecting";
  case HAL_GAMEPAD_STATE_CONNECTED:
    return "connected";
  case HAL_GAMEPAD_STATE_FAILED:
    return "failed";
  default:
    return "unknown";
  }
}

static hal_status_t initializeRuntime(void) {
  hal_status_t status = HAL_OK;
#if defined(HAL_GAMEPAD_EXAMPLE_ENABLE_BLE)
  status = hal_ble_initialize();
  if (status != HAL_OK) {
    return status;
  }
#endif

  status = hal_gamepad_open(&s_gamepad);
#if defined(HAL_GAMEPAD_EXAMPLE_ENABLE_BLE)
  if (status != HAL_OK) {
    (void)hal_ble_deinitialize();
  }
#endif
  return status;
}

static void printSnapshot(const hal_gamepad_snapshot_t *snapshot) {
  deb("gamepad generation=%lu connected=%u buttons=0x%08lX dpad=0x%02X "
      "axes=0x%03X x=%d y=%d rx=%d ry=%d",
      (unsigned long)snapshot->generation, snapshot->connected ? 1u : 0u,
      (unsigned long)snapshot->buttons, (unsigned)snapshot->dpad,
      (unsigned)snapshot->axes_present, (int)snapshot->axes[HAL_GAMEPAD_AXIS_X],
      (int)snapshot->axes[HAL_GAMEPAD_AXIS_Y],
      (int)snapshot->axes[HAL_GAMEPAD_AXIS_RX],
      (int)snapshot->axes[HAL_GAMEPAD_AXIS_RY]);
}

static void drainSnapshots(void) {
  for (;;) {
    hal_gamepad_snapshot_t snapshot = {0};
    const hal_status_t status = hal_gamepad_snapshot_next(s_gamepad, &snapshot);
    if (status == HAL_OK) {
      printSnapshot(&snapshot);
      continue;
    }
    if (status == HAL_EOVERFLOW) {
      derr("gamepad input queue overflow; continuing with retained state");
      continue;
    }
    if (status != HAL_EAGAIN) {
      derr("gamepad snapshot failed: %s", hal_status_to_string(status));
    }
    return;
  }
}

static void handleState(const hal_gamepad_info_t *info) {
  if (info->state != s_previousState) {
    deb("gamepad state=%s status=%s known=%u pending=%u dropped=%lu",
        stateName(info->state), hal_status_to_string(info->last_status),
        info->known_device ? 1u : 0u, info->pairing_pending ? 1u : 0u,
        (unsigned long)info->dropped_snapshots);
    s_previousState = info->state;
  }

  if (info->pairing_pending) {
    const hal_status_t status = hal_gamepad_pairing_authorize(s_gamepad);
    if (status != HAL_OK) {
      derr("gamepad pairing authorization failed: %s",
           hal_status_to_string(status));
    }
  }

  if (info->state == HAL_GAMEPAD_STATE_CONNECTED) {
    s_reconnectStarted = false;
    return;
  }
  if (info->state != HAL_GAMEPAD_STATE_READY) {
    return;
  }
  if (!info->known_device && !info->pairing_window_open) {
    s_pairingOpened = false;
  }
  if (info->known_device && !s_reconnectStarted) {
    const hal_status_t status = hal_gamepad_reconnect(s_gamepad);
    if (status == HAL_OK) {
      s_reconnectStarted = true;
    } else {
      derr("gamepad reconnect failed: %s", hal_status_to_string(status));
    }
  } else if (!info->known_device && !s_pairingOpened) {
    const hal_status_t status = hal_gamepad_pairing_open(s_gamepad);
    if (status == HAL_OK) {
      s_pairingOpened = true;
      deb("gamepad pairing window opened");
    } else {
      derr("gamepad pairing window failed: %s", hal_status_to_string(status));
    }
  }
}

void app_start(void) {
  hal_debug_init_default();
  deb("JaszczurHAL Bluetooth Classic gamepad example");
#if defined(HAL_GAMEPAD_EXAMPLE_ENABLE_BLE)
  deb("BLE and Bluetooth Classic share one controller runtime");
#endif
}

void app_task0(void) {
  if (!s_started) {
    s_started = true;
    s_runtimeStatus = initializeRuntime();
    if (s_runtimeStatus != HAL_OK) {
      derr("gamepad initialize failed: %s",
           hal_status_to_string(s_runtimeStatus));
    }
  }
  if (s_runtimeStatus != HAL_OK) {
    hal_delay_ms(1u);
    return;
  }

#if defined(HAL_GAMEPAD_EXAMPLE_ENABLE_BLE)
  const hal_status_t bleStatus = hal_ble_poll();
  if (bleStatus != HAL_OK && bleStatus != HAL_EOVERFLOW) {
    s_runtimeStatus = bleStatus;
    derr("BLE poll failed: %s", hal_status_to_string(bleStatus));
    hal_delay_ms(1u);
    return;
  }
#endif

  const hal_status_t pollStatus = hal_gamepad_poll(s_gamepad);
  if (pollStatus != HAL_OK && pollStatus != HAL_EOVERFLOW) {
    s_runtimeStatus = pollStatus;
    derr("gamepad poll failed: %s", hal_status_to_string(pollStatus));
    hal_delay_ms(1u);
    return;
  }

  hal_gamepad_info_t info = {0};
  const hal_status_t infoStatus = hal_gamepad_get_info(s_gamepad, &info);
  if (infoStatus == HAL_OK) {
    handleState(&info);
  } else {
    derr("gamepad info failed: %s", hal_status_to_string(infoStatus));
  }
  drainSnapshots();
  hal_delay_ms(1u);
}
