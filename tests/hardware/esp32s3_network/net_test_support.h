#pragma once

/**
 * @file net_test_support.h
 * @brief Report lines, serial commands and connection helpers shared by the
 *        suites of the ESP32-S3 network hardware test.
 *
 * Every suite prints `JH_ESP32_NET check=<name> pass=<0|1> status=<status>`
 * per check; verify_network.py reads those lines from the debug serial port.
 */
#include <hal/core/hal_status.h>
#include <hal/network/hal_net.h>
#include <hal/network/hal_tcp.h>
#include <hal/network/hal_wifi.h>

#include <stddef.h>
#include <stdint.h>

#if !__has_include("net_test_config.h")
#error                                                                         \
    "Create net_test_config.h with JH_NET_TEST_SSID, JH_NET_TEST_PASSWORD and JH_NET_TEST_HOST (see README.md)"
#endif
#include "net_test_config.h"

constexpr const char *kNetTestPrefix = "JH_ESP32_NET ";
constexpr uint32_t kNetTestJoinTimeoutMs = 20000u;
constexpr uint32_t kNetTestWaitMs = 3000u;

/** Board address after the last successful join. */
extern char g_net_test_local_ip[20];

/** Print one check line and count it as passed or failed. */
void net_test_report(const char *check, bool pass, hal_status_t status,
                     const char *extra);

/** Print `JH_ESP32_NET <text>` without counting a check. */
void net_test_announce(const char *text);

/** Number of failed checks so far. */
unsigned net_test_failed(void);

/** Print the final `check=result` line. */
void net_test_report_result(void);

/** The completed serial line, or nullptr while a line is still arriving. */
const char *net_test_read_line(void);

/** Wait until the host sends @p command, or until @p timeout_ms passes. */
bool net_test_wait_for_line(const char *command, uint32_t timeout_ms);

/** Resolve an IPv4 literal or name into @p out with @p port. */
bool net_test_endpoint(const char *address, uint16_t port,
                       hal_net_endpoint_t *out);

/** Wildcard IPv4 endpoint on @p port. */
hal_net_endpoint_t net_test_any_ipv4(uint16_t port);

/** Join the network with the test timeout and report the resulting state. */
hal_status_t net_test_join(const char *ssid, const char *password,
                           hal_wifi_state_t *out_state);

/** Join the test network, record the board address and report @p check. */
bool net_test_check_join(const char *check);

/** Open a TCP socket and connect it to @p address:@p port. */
hal_status_t net_test_connect(const char *address, uint16_t port,
                              hal_tcp_socket_t *out_socket);

/** Send all of @p data and read the same number of bytes back. */
hal_status_t net_test_tcp_roundtrip(hal_tcp_socket_t socket, const char *data,
                                    size_t length, uint32_t timeout_ms);

/** Basic suite parts: both application tasks, then the servers. */
void net_test_run_tasks(void);
void net_test_run_servers(void);

/** Suite entry: runs every check of the selected suite. */
void net_test_run_suite(void);

/** Continuous traffic of the selected suite for @p seconds; reports
 *  `soak` progress lines and one `check=soak` result. */
void net_test_run_soak(uint32_t seconds);

/** Soak bookkeeping shared by the suites. */
struct NetTestSoak {
  uint32_t started_ms;
  uint32_t last_report_ms;
  uint32_t heap_start;
  unsigned ok;
  unsigned errors;
};

/** Start the bookkeeping: zero counts, record the time and the free heap. */
void net_test_soak_begin(NetTestSoak *soak);

/** Count one exchange and print a progress line every 30 seconds. */
void net_test_soak_count(NetTestSoak *soak, hal_status_t status);

/** Report `check=soak`: at most 0.5 % failed exchanges and a free heap that
 *  ends within 8 KiB of its start. */
void net_test_soak_finish(const NetTestSoak *soak, const char *extra);

/** Suite name and host ports announced on the `ready` line. */
const char *net_test_ready_fields(void);
