#include "../../hal_target.h"
#if HAL_TARGET_IS_RP2040
#include "../../hal_config.h"

#ifdef HAL_ENABLE_OTA

#include "../../hal_ota.h"
#include "../../hal_serial.h"
#include "../../hal_sync.h"

#include <ArduinoOTA.h>
#include <stdio.h>
#include <string.h>

#define HAL_OTA_TEXT_BUF_SIZE 96u
#define HAL_OTA_EVENT_QUEUE_SIZE 12u
#define HAL_OTA_DEFAULT_PORT 8266u

typedef enum {
    HAL_OTA_EVENT_NONE = 0,
    HAL_OTA_EVENT_START,
    HAL_OTA_EVENT_END,
    HAL_OTA_EVENT_PROGRESS,
    HAL_OTA_EVENT_ERROR
} hal_ota_event_type_t;

typedef struct {
    hal_ota_event_type_t type;
    hal_ota_command_t command;
    hal_ota_error_t error;
    uint32_t progress;
    uint32_t total;
} hal_ota_event_t;

static struct {
    hal_mutex_t mutex;
    bool started;

    uint16_t port;
    bool password_set;

    char hostname[HAL_OTA_TEXT_BUF_SIZE];
    char password[HAL_OTA_TEXT_BUF_SIZE];

    hal_ota_on_start_callback_t on_start;
    void *on_start_user;
    hal_ota_on_end_callback_t on_end;
    void *on_end_user;
    hal_ota_on_progress_callback_t on_progress;
    void *on_progress_user;
    hal_ota_on_error_callback_t on_error;
    void *on_error_user;

    hal_ota_event_t queue[HAL_OTA_EVENT_QUEUE_SIZE];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} s_ota;

static inline void ota_ensure_mutex(void) {
    if (s_ota.mutex == NULL) {
        hal_critical_section_enter();
        if (s_ota.mutex == NULL) {
            s_ota.mutex = hal_mutex_create();
            s_ota.port = HAL_OTA_DEFAULT_PORT;
        }
        hal_critical_section_exit();
    }
}

static bool validate_non_empty(const char *value, const char *fn, const char *name) {
    if (!value || value[0] == '\0') {
        hal_derr("%s: %s is NULL/empty", fn, name);
        return false;
    }
    return true;
}

static void queue_clear_no_lock(void) {
    s_ota.head = 0u;
    s_ota.tail = 0u;
    s_ota.count = 0u;
}

static void queue_push_no_lock(const hal_ota_event_t *event_in) {
    if (!event_in) {
        return;
    }

    if (s_ota.count >= HAL_OTA_EVENT_QUEUE_SIZE) {
        s_ota.head = (uint8_t)((s_ota.head + 1u) % HAL_OTA_EVENT_QUEUE_SIZE);
        s_ota.count--;
    }

    s_ota.queue[s_ota.tail] = *event_in;
    s_ota.tail = (uint8_t)((s_ota.tail + 1u) % HAL_OTA_EVENT_QUEUE_SIZE);
    s_ota.count++;
}

static bool queue_pop_no_lock(hal_ota_event_t *event_out) {
    if (!event_out || s_ota.count == 0u) {
        return false;
    }

    *event_out = s_ota.queue[s_ota.head];
    s_ota.head = (uint8_t)((s_ota.head + 1u) % HAL_OTA_EVENT_QUEUE_SIZE);
    s_ota.count--;
    return true;
}

static hal_ota_command_t map_arduino_command(void) {
    const int command = (int)ArduinoOTA.getCommand();
#ifdef U_FLASH
    if (command == (int)U_FLASH) {
        return HAL_OTA_COMMAND_SKETCH;
    }
#endif
#ifdef U_FS
    if (command == (int)U_FS) {
        return HAL_OTA_COMMAND_FILESYSTEM;
    }
#endif
    return HAL_OTA_COMMAND_UNKNOWN;
}

static hal_ota_error_t map_arduino_error(ota_error_t error) {
    switch (error) {
        case OTA_AUTH_ERROR:
            return HAL_OTA_ERROR_AUTH;
        case OTA_BEGIN_ERROR:
            return HAL_OTA_ERROR_BEGIN;
        case OTA_CONNECT_ERROR:
            return HAL_OTA_ERROR_CONNECT;
        case OTA_RECEIVE_ERROR:
            return HAL_OTA_ERROR_RECEIVE;
        case OTA_END_ERROR:
            return HAL_OTA_ERROR_END;
        default:
            return HAL_OTA_ERROR_UNKNOWN;
    }
}

static void ota_internal_on_start(void) {
    hal_ota_event_t event{};
    event.type = HAL_OTA_EVENT_START;
    event.command = map_arduino_command();
    queue_push_no_lock(&event);
}

static void ota_internal_on_end(void) {
    hal_ota_event_t event{};
    event.type = HAL_OTA_EVENT_END;
    queue_push_no_lock(&event);
}

static void ota_internal_on_progress(unsigned int progress, unsigned int total) {
    hal_ota_event_t event{};
    event.type = HAL_OTA_EVENT_PROGRESS;
    event.progress = (uint32_t)progress;
    event.total = (uint32_t)total;
    queue_push_no_lock(&event);
}

static void ota_internal_on_error(ota_error_t error) {
    hal_ota_event_t event{};
    event.type = HAL_OTA_EVENT_ERROR;
    event.error = map_arduino_error(error);
    queue_push_no_lock(&event);
}

bool hal_ota_set_port(uint16_t port) {
    if (port == 0u) {
        hal_derr("hal_ota_set_port: port must be > 0");
        return false;
    }

    ota_ensure_mutex();
    hal_mutex_lock(s_ota.mutex);

    s_ota.port = port;
    ArduinoOTA.setPort(port);

    hal_mutex_unlock(s_ota.mutex);
    return true;
}

bool hal_ota_set_hostname(const char *hostname) {
    if (!validate_non_empty(hostname, "hal_ota_set_hostname", "hostname")) {
        return false;
    }

    ota_ensure_mutex();
    hal_mutex_lock(s_ota.mutex);

    snprintf(s_ota.hostname, sizeof(s_ota.hostname), "%s", hostname);
    ArduinoOTA.setHostname(s_ota.hostname);

    hal_mutex_unlock(s_ota.mutex);
    return true;
}

bool hal_ota_set_password(const char *password) {
    if (!password) {
        hal_derr("hal_ota_set_password: password pointer is NULL");
        return false;
    }

    ota_ensure_mutex();
    hal_mutex_lock(s_ota.mutex);

    snprintf(s_ota.password, sizeof(s_ota.password), "%s", password);
    s_ota.password_set = true;
    ArduinoOTA.setPassword(s_ota.password);

    hal_mutex_unlock(s_ota.mutex);
    return true;
}

bool hal_ota_on_start(hal_ota_on_start_callback_t callback, void *user) {
    ota_ensure_mutex();
    hal_mutex_lock(s_ota.mutex);

    s_ota.on_start = callback;
    s_ota.on_start_user = user;

    hal_mutex_unlock(s_ota.mutex);
    return true;
}

bool hal_ota_on_end(hal_ota_on_end_callback_t callback, void *user) {
    ota_ensure_mutex();
    hal_mutex_lock(s_ota.mutex);

    s_ota.on_end = callback;
    s_ota.on_end_user = user;

    hal_mutex_unlock(s_ota.mutex);
    return true;
}

bool hal_ota_on_progress(hal_ota_on_progress_callback_t callback, void *user) {
    ota_ensure_mutex();
    hal_mutex_lock(s_ota.mutex);

    s_ota.on_progress = callback;
    s_ota.on_progress_user = user;

    hal_mutex_unlock(s_ota.mutex);
    return true;
}

bool hal_ota_on_error(hal_ota_on_error_callback_t callback, void *user) {
    ota_ensure_mutex();
    hal_mutex_lock(s_ota.mutex);

    s_ota.on_error = callback;
    s_ota.on_error_user = user;

    hal_mutex_unlock(s_ota.mutex);
    return true;
}

bool hal_ota_begin(void) {
    ota_ensure_mutex();
    hal_mutex_lock(s_ota.mutex);

    if (s_ota.port == 0u) {
        s_ota.port = HAL_OTA_DEFAULT_PORT;
    }

    ArduinoOTA.setPort(s_ota.port);
    if (s_ota.hostname[0] != '\0') {
        ArduinoOTA.setHostname(s_ota.hostname);
    }
    if (s_ota.password_set) {
        ArduinoOTA.setPassword(s_ota.password);
    }

    queue_clear_no_lock();
    ArduinoOTA.onStart(ota_internal_on_start);
    ArduinoOTA.onEnd(ota_internal_on_end);
    ArduinoOTA.onProgress(ota_internal_on_progress);
    ArduinoOTA.onError(ota_internal_on_error);
    ArduinoOTA.begin();

    s_ota.started = true;

    hal_mutex_unlock(s_ota.mutex);
    return true;
}

void hal_ota_handle(void) {
    ota_ensure_mutex();

    hal_mutex_lock(s_ota.mutex);
    if (!s_ota.started) {
        hal_mutex_unlock(s_ota.mutex);
        return;
    }
    ArduinoOTA.handle();
    hal_mutex_unlock(s_ota.mutex);

    for (;;) {
        hal_ota_event_t event{};

        hal_ota_on_start_callback_t on_start = NULL;
        void *on_start_user = NULL;
        hal_ota_on_end_callback_t on_end = NULL;
        void *on_end_user = NULL;
        hal_ota_on_progress_callback_t on_progress = NULL;
        void *on_progress_user = NULL;
        hal_ota_on_error_callback_t on_error = NULL;
        void *on_error_user = NULL;

        hal_mutex_lock(s_ota.mutex);
        const bool has_event = queue_pop_no_lock(&event);
        if (has_event) {
            on_start = s_ota.on_start;
            on_start_user = s_ota.on_start_user;
            on_end = s_ota.on_end;
            on_end_user = s_ota.on_end_user;
            on_progress = s_ota.on_progress;
            on_progress_user = s_ota.on_progress_user;
            on_error = s_ota.on_error;
            on_error_user = s_ota.on_error_user;
        }
        hal_mutex_unlock(s_ota.mutex);

        if (!has_event) {
            break;
        }

        switch (event.type) {
            case HAL_OTA_EVENT_START:
                if (on_start) {
                    on_start(event.command, on_start_user);
                }
                break;
            case HAL_OTA_EVENT_END:
                if (on_end) {
                    on_end(on_end_user);
                }
                break;
            case HAL_OTA_EVENT_PROGRESS:
                if (on_progress) {
                    on_progress(event.progress, event.total, on_progress_user);
                }
                break;
            case HAL_OTA_EVENT_ERROR:
                if (on_error) {
                    on_error(event.error, on_error_user);
                }
                break;
            default:
                break;
        }
    }
}

bool hal_ota_is_started(void) {
    ota_ensure_mutex();
    hal_mutex_lock(s_ota.mutex);

    const bool started = s_ota.started;

    hal_mutex_unlock(s_ota.mutex);
    return started;
}

#endif /* HAL_ENABLE_OTA */
#endif  // HAL_TARGET_IS_RP2040
