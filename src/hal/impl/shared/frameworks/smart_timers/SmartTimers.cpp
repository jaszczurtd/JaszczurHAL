#include "SmartTimers.h"
#include "hal/impl/shared/hal_mutex_once.h"

SmartTimers::SmartTimers()
    : _time(0), _lastTime(0), clb(NULL), _mutex(hal_mutex_create()) {}

SmartTimers::~SmartTimers() {
  if (_mutex != NULL) {
    hal_mutex_destroy(_mutex);
    _mutex = NULL;
  }
}

void SmartTimers::ensureMutex() { (void)jh_hal_mutex_create_once(&_mutex); }

void SmartTimers::restart() {
  ensureMutex();
  hal_mutex_lock(_mutex);
  _lastTime = hal_millis();
  hal_mutex_unlock(_mutex);
}

void SmartTimers::begin(void (*callback)(void), const uint32_t interval) {
  ensureMutex();
  hal_mutex_lock(_mutex);
  clb = callback;
  _time = interval;
  _lastTime = hal_millis();
  hal_mutex_unlock(_mutex);
}

void SmartTimers::tick() {
  ensureMutex();
  hal_mutex_lock(_mutex);
  if (clb == NULL || _time == 0) {
    hal_mutex_unlock(_mutex);
    return;
  }

  if (_lastTime == 0) {
    _lastTime = hal_millis();
    hal_mutex_unlock(_mutex);
    return;
  }

  uint32_t actualTime = hal_millis();
  uint32_t deltaTime = actualTime - _lastTime;
  if (deltaTime >= _time) {
    void (*cb)(void) = clb;
    _lastTime = actualTime;
    hal_mutex_unlock(_mutex);
    cb();
  } else {
    hal_mutex_unlock(_mutex);
  }
}

void SmartTimers::abort() {
  ensureMutex();
  hal_mutex_lock(_mutex);
  _lastTime = _time = 0;
  hal_mutex_unlock(_mutex);
}

bool SmartTimers::available() {
  ensureMutex();
  hal_mutex_lock(_mutex);
  if (_time == 0) {
    hal_mutex_unlock(_mutex);
    return false;
  }

  if (_lastTime == 0) {
    _lastTime = hal_millis();
    hal_mutex_unlock(_mutex);
    return false;
  }

  uint32_t actualTime = hal_millis();
  uint32_t deltaTime = actualTime - _lastTime;
  bool result = (deltaTime >= _time);
  hal_mutex_unlock(_mutex);
  return result;
}

uint32_t SmartTimers::time() {
  ensureMutex();
  hal_mutex_lock(_mutex);
  if (_time == 0) {
    hal_mutex_unlock(_mutex);
    return 0;
  }

  if (_lastTime == 0) {
    _lastTime = hal_millis();
    uint32_t t = _time;
    hal_mutex_unlock(_mutex);
    return t;
  }

  uint32_t actualTime = hal_millis();
  uint32_t deltaTime = actualTime - _lastTime;
  uint32_t result = (deltaTime >= _time) ? 0 : (_time - deltaTime);
  hal_mutex_unlock(_mutex);
  return result;
}

void SmartTimers::time(const uint32_t interval) {
  ensureMutex();
  hal_mutex_lock(_mutex);
  _time = interval;
  hal_mutex_unlock(_mutex);
}
