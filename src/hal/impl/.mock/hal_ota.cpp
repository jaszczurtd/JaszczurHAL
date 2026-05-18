#include "../../hal_config.h"

#ifdef HAL_ENABLE_OTA

#include "../../hal_ota.h"
#include "../../hal_serial.h"
#include "hal_mock.h"

#include <stdio.h>
#include <string.h>

#define MOCK_OTA_TEXT_BUF_SIZE 96u
#define MOCK_OTA_EVENT_QUEUE_SIZE 12u
#define MOCK_OTA_DEFAULT_PORT 8266u

typedef enum {
    MOCK_OTA_EVENT_NONE = 0,
    MOCK_OTA_EVENT_START,
    MOCK_OTA_EVENT_END,
    MOCK_OTA_EVENT_PROGRESS,
    MOCK_OTA_EVENT_ERROR
} mock_ota_event_type_t;

typedef struct {
    mock_ota_event_type_t type;
    hal_ota_command_t command;
    hal_ota_error_t error;
    uint32_t progress;
    uint32_t total;
} mock_ota_event_t;

static struct {
    bool started;
    bool begin_result;

    uint16_t port;
    char hostname[MOCK_OTA_TEXT_BUF_SIZE];
    char password[MOCK_OTA_TEXT_BUF_SIZE];

    hal_ota_on_start_callback_t on_start;
    void *on_start_user;
    hal_ota_on_end_callback_t on_end;
    void *on_end_user;
    hal_ota_on_progress_callback_t on_progress;
    void *on_progress_user;
    hal_ota_on_error_callback_t on_error;
    void *on_error_user;

    mock_ota_event_t queue[MOCK_OTA_EVENT_QUEUE_SIZE];
    uint8_t head;
    uint8_t tail;
    uint8_t count;

    uint32_t handle_count;
} s_ota;

static bool validate_non_empty(const char *value, const char *fn, const char *name) {
    if (!value || value[0] == '\0') {
        hal_derr("%s: %s is NULL/empty", fn, name);
        return false;
    }
    return true;
}

static void queue_clear(void) {
    s_ota.head = 0u;
    s_ota.tail = 0u;
    s_ota.count = 0u;
}

static void queue_push(const mock_ota_event_t *event_in) {
    if (!event_in) {
        return;
    }

    if (s_ota.count >= MOCK_OTA_EVENT_QUEUE_SIZE) {
        s_ota.head = (uint8_t)((s_ota.head + 1u) % MOCK_OTA_EVENT_QUEUE_SIZE);
        s_ota.count--;
    }

    s_ota.queue[s_ota.tail] = *event_in;
    s_ota.tail = (uint8_t)((s_ota.tail + 1u) % MOCK_OTA_EVENT_QUEUE_SIZE);
    s_ota.count++;
}

static bool queue_pop(mock_ota_event_t *event_out) {
    if (!event_out || s_ota.count == 0u) {
        return false;
    }

    *event_out = s_ota.queue[s_ota.head];
    s_ota.head = (uint8_t)((s_ota.head + 1u) % MOCK_OTA_EVENT_QUEUE_SIZE);
    s_ota.count--;
    return true;
}

void hal_mock_ota_reset(void) {
    memset(&s_ota, 0, sizeof(s_ota));
    s_ota.begin_result = true;
    s_ota.port = MOCK_OTA_DEFAULT_PORT;
}

void hal_mock_ota_set_begin_result(bool result) {
    s_ota.begin_result = result;
}

void hal_mock_ota_inject_start(hal_ota_command_t command) {
    mock_ota_event_t event{};
    event.type = MOCK_OTA_EVENT_START;
    event.command = command;
    queue_push(&event);
}

void hal_mock_ota_inject_end(void) {
    mock_ota_event_t event{};
    event.type = MOCK_OTA_EVENT_END;
    queue_push(&event);
}

void hal_mock_ota_inject_progress(uint32_t progress, uint32_t total) {
    mock_ota_event_t event{};
    event.type = MOCK_OTA_EVENT_PROGRESS;
    event.progress = progress;
    event.total = total;
    queue_push(&event);
}

void hal_mock_ota_inject_error(hal_ota_error_t error) {
    mock_ota_event_t event{};
    event.type = MOCK_OTA_EVENT_ERROR;
    event.error = error;
    queue_push(&event);
}

uint16_t hal_mock_ota_get_port(void) {
    return s_ota.port;
}

const char *hal_mock_ota_get_hostname(void) {
    return s_ota.hostname;
}

const char *hal_mock_ota_get_password(void) {
    return s_ota.password;
}

uint32_t hal_mock_ota_get_handle_count(void) {
    return s_ota.handle_count;
}

bool hal_ota_set_port(uint16_t port) {
    if (port == 0u) {
        hal_derr("hal_ota_set_port: port must be > 0");
        return false;
    }

    s_ota.port = port;
    return true;
}

bool hal_ota_set_hostname(const char *hostname) {
    if (!validate_non_empty(hostname, "hal_ota_set_hostname", "hostname")) {
        return false;
    }

    snprintf(s_ota.hostname, sizeof(s_ota.hostname), "%s", hostname);
    return true;
}

bool hal_ota_set_password(const char *password) {
    if (!password) {
        hal_derr("hal_ota_set_password: password pointer is NULL");
        return false;
    }

    snprintf(s_ota.password, sizeof(s_ota.password), "%s", password);
    return true;
}

bool hal_ota_on_start(hal_ota_on_start_callback_t callback, void *user) {
    s_ota.on_start = callback;
    s_ota.on_start_user = user;
    return true;
}

bool hal_ota_on_end(hal_ota_on_end_callback_t callback, void *user) {
    s_ota.on_end = callback;
    s_ota.on_end_user = user;
    return true;
}

bool hal_ota_on_progress(hal_ota_on_progress_callback_t callback, void *user) {
    s_ota.on_progress = callback;
    s_ota.on_progress_user = user;
    return true;
}

bool hal_ota_on_error(hal_ota_on_error_callback_t callback, void *user) {
    s_ota.on_error = callback;
    s_ota.on_error_user = user;
    return true;
}

bool hal_ota_begin(void) {
    queue_clear();
    s_ota.started = s_ota.begin_result;
    return s_ota.begin_result;
}

void hal_ota_handle(void) {
    if (!s_ota.started) {
        return;
    }

    s_ota.handle_count++;

    for (;;) {
        mock_ota_event_t event{};
        if (!queue_pop(&event)) {
            break;
        }

        switch (event.type) {
            case MOCK_OTA_EVENT_START:
                if (s_ota.on_start) {
                    s_ota.on_start(event.command, s_ota.on_start_user);
                }
                break;
            case MOCK_OTA_EVENT_END:
                if (s_ota.on_end) {
                    s_ota.on_end(s_ota.on_end_user);
                }
                break;
            case MOCK_OTA_EVENT_PROGRESS:
                if (s_ota.on_progress) {
                    s_ota.on_progress(event.progress, event.total, s_ota.on_progress_user);
                }
                break;
            case MOCK_OTA_EVENT_ERROR:
                if (s_ota.on_error) {
                    s_ota.on_error(event.error, s_ota.on_error_user);
                }
                break;
            default:
                break;
        }
    }
}

bool hal_ota_is_started(void) {
    return s_ota.started;
}

#endif /* HAL_ENABLE_OTA */
