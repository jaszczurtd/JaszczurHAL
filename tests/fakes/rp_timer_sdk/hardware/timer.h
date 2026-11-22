#pragma once
#include <hardware/sync.h>
#include <stdbool.h>
#define NUM_ALARMS 4u
typedef unsigned int uint;
#ifdef __cplusplus
extern "C" {
#endif
bool hardware_alarm_is_claimed(uint alarm);
int hardware_alarm_claim_unused(bool required);
void hardware_alarm_unclaim(uint alarm);
#ifdef __cplusplus
}
#endif
