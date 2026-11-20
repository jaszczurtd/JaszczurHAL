#pragma once

/* Test control of the std::mutex based hal_mutex in hal_sync_host.cpp. */

#ifdef __cplusplus
extern "C" {
#endif

/* Runs hook once inside the next hal_mutex_lock(), before the mutex is
 * taken. That is the point where another thread gets in ahead of the caller,
 * so a test can place a second call exactly between the entry of a function
 * and its lock. The hook may lock the same mutex. */
void hal_sync_host_before_next_lock(void (*hook)(void));

#ifdef __cplusplus
}
#endif
