#pragma once

#include "esp_err.h"

#include <stdint.h>

typedef void (*esp_ipc_func_t)(void *arg);

esp_err_t esp_ipc_call_blocking(uint32_t cpu_id, esp_ipc_func_t func,
                                void *arg);
