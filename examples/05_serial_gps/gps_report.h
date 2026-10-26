/**
 * @file gps_report.h
 * @brief GPS status line printed by both serial variants of the example.
 */

#pragma once

#include <stdint.h>

/**
 * @brief Print the GPS state at most once per second.
 *
 * The first line shows the position, or the reception counters while the
 * receiver has no fix. The second line shows the UTC date and time of the
 * latest RMC sentence with a fix and marks it as stale after two seconds.
 *
 * @param now   Current hal_millis() value.
 * @param label Prefix that names the variant in the log.
 */
void gps_report(uint32_t now, const char *label);
