#ifndef _Timers_h
#define _Timers_h

/**
 * @file SmartTimers.h
 * @brief Lightweight, non-blocking software timer driven by hal_millis().
 */

#include "libConfig.h"
#include <hal/hal_sync.h>
#include <hal/hal_system.h>
#include <inttypes.h>
#include <stddef.h>

/** @brief Pass as interval to stop the timer. */
#define STOP 0

/**
 * @brief Non-blocking software timer.
 *
 * Call begin() to configure, then call tick() periodically from your loop.
 * The registered callback fires every time the interval elapses.
 */
class SmartTimers {
private:
  uint32_t _time;
  uint32_t _lastTime;
  void (*clb)(void);
  hal_mutex_t _mutex;

  void ensureMutex();

public:
  /** @brief Default constructor. Keeps the timer in a safe stopped state. */
  SmartTimers();

  /** @brief Destructor. Releases host/mock mutex for explicit destroy paths. */
  ~SmartTimers();

  /* Owns a raw hal_mutex_t freed in the destructor, so it must not be copied:
   * a copy would share the handle and double-free it (Rule of Three). Timers
   * are used as long-lived singletons, so non-copyable is the correct contract.
   */
  SmartTimers(const SmartTimers &) = delete;
  SmartTimers &operator=(const SmartTimers &) = delete;

  /**
   * @brief Configure the timer.
   * @param callback Function to call when the interval elapses.
   * @param interval Timer interval in milliseconds (use SECS/MINS/HOURS
   * macros).
   */
  void begin(void (*callback)(void), const uint32_t);

  /** @brief Restart the timer from the current moment. */
  void restart();

  /**
   * @brief Check if the timer interval has elapsed (non-blocking).
   * @return true if the interval has passed since the last restart.
   */
  bool available();

  /**
   * @brief Get the remaining time until the next callback.
   *
   * Returns the number of milliseconds left until the interval elapses.
   * Returns 0 when the timer has already elapsed or is stopped.
   * Does NOT return the configured interval - use a separate variable for that.
   *
   * @return Milliseconds remaining, or 0 if elapsed / stopped.
   */
  uint32_t time();

  /**
   * @brief Set a new timer interval.
   *
   * Only updates the interval - does NOT reset the internal timestamp.
   * Call restart() afterwards if you want the new interval to start counting
   * from now.
   *
   * @param interval New interval in milliseconds.
   */
  void time(const uint32_t);

  /**
   * @brief Advance the timer. Call this in your main loop.
   *
   * When the interval elapses, the registered callback is invoked
   * and the timer restarts automatically.
   */
  void tick();

  /** @brief Stop the timer (set interval to STOP). */
  void abort();
};

#endif
