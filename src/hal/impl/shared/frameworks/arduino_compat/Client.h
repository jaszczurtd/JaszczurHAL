#pragma once

#include "IPAddress.h"
#include "Stream.h"

namespace arduino {

class Client : public Stream {
public:
  ~Client() override = default;

  virtual int connect(IPAddress ip, uint16_t port) = 0;
  virtual int connect(const char *host, uint16_t port) = 0;
  virtual int read() = 0;
  virtual int read(uint8_t *buffer, size_t size) = 0;
  virtual void flush() = 0;
  virtual void stop() = 0;
  virtual uint8_t connected() = 0;
  virtual operator bool() = 0;
};

} // namespace arduino

using arduino::Client;
