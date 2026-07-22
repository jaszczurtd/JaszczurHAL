#pragma once

#include "Print.h"

namespace arduino {

class Stream : public Print {
public:
  Stream() : timeout_(1000ul) {}
  ~Stream() override = default;

  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;

  void setTimeout(unsigned long timeout) { timeout_ = timeout; }
  unsigned long getTimeout() const { return timeout_; }

private:
  unsigned long timeout_;
};

} // namespace arduino

using arduino::Stream;
