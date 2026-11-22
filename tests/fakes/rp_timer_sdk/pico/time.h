#pragma once
#include <hardware/timer.h>
#include <stdbool.h>
#include <stdint.h>
#define PICO_TIME_DEFAULT_ALARM_POOL_MAX_TIMERS 4u
typedef int32_t alarm_id_t;
typedef struct alarm_pool alarm_pool_t;
typedef int64_t (*alarm_callback_t)(alarm_id_t, void *);
#ifdef __cplusplus
extern "C" {
#endif
alarm_pool_t *alarm_pool_get_default(void);
alarm_pool_t *alarm_pool_create(uint alarm, uint count);
alarm_pool_t *alarm_pool_create_with_unused_hardware_alarm(uint count);
void alarm_pool_destroy(alarm_pool_t *pool);
alarm_id_t alarm_pool_add_alarm_in_us(alarm_pool_t *pool, uint64_t delay,
                                      alarm_callback_t callback, void *user,
                                      bool fire_if_past);
bool alarm_pool_cancel_alarm(alarm_pool_t *pool, alarm_id_t id);
#ifdef __cplusplus
}
#endif
