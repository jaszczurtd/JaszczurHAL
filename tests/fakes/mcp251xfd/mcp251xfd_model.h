#pragma once

/*
 * Behavioural model of an MCP2518FD on the mock SPI bus, for host tests of
 * the MCP251XFD driver. It follows the MCP2518FD data sheet (DS20006027B) and
 * the MCP25XXFD Family Reference Manual (DS20005678E):
 *
 * - SPI instructions RESET, READ and WRITE; SFR bytes take effect as they
 *   arrive, RAM is written and read in whole words (DS 4.2);
 * - operating modes requested through CiCON.REQOP, with the transitions of
 *   FRM 2.1 (normal and debug modes are only reached from Configuration);
 * - registers and fields that only Configuration mode may change (FRM 2.2);
 * - the TXQ and FIFO 1..31 laid out in RAM after TEF and TXQ (FRM 3.5), user
 *   addresses relative to 0x400 (FRM equations 4-1 and 7-1), UINC, TXREQ,
 *   FRESET, the status flags and the message indexes;
 * - the 32 acceptance filters with FLTEN/FnBP bytes, MIDE/EXIDE and masks
 *   (FRM 6.2, without data-byte filtering and SID11);
 * - one-shot, three-shot and unlimited retransmission against a bus that
 *   acknowledges or not (FRM 4.9), internal and external loopback.
 *
 * Every rule of the data sheet the driver breaks is recorded in violations()
 * so a test can require none. The bus is not timed: a requested frame goes
 * out at once.
 */

#include <stdint.h>

#include <string>
#include <vector>

struct Mcp251xfdFrame {
  uint32_t id = 0u; /**< 11 or 29 bits. */
  bool ext = false;
  bool rtr = false;
  bool fdf = false;
  bool brs = false;
  bool esi = false;
  uint8_t dlc = 0u;
  uint8_t data[64] = {};
};

class Mcp251xfdModel {
public:
  enum Mode : uint8_t {
    kMixed = 0u,
    kSleep = 1u,
    kIntLoopback = 2u,
    kListenOnly = 3u,
    kConfig = 4u,
    kExtLoopback = 5u,
    kCan20 = 6u,
    kRestricted = 7u
  };

  /** Attach to the mock SPI @p bus with chip select @p cs_pin. */
  Mcp251xfdModel(uint8_t cs_pin, uint8_t bus = 0u);
  ~Mcp251xfdModel();

  /** Power-on reset: registers at their POR values, Configuration mode. */
  void power_on();
  /** Simulate a chip left running by a previous program (MCU-only reset). */
  void force_mode(Mode mode);

  /** A frame from another node; true when a filter stored it. */
  bool deliver(const Mcp251xfdFrame &frame);

  /** Another node acknowledges frames this chip sends. */
  bool bus_acks = true;
  /** Frames this chip put on the bus. */
  std::vector<Mcp251xfdFrame> sent;

  Mode mode() const { return static_cast<Mode>(opmod_); }
  uint32_t reg32(uint16_t addr) const;
  uint8_t tec() const { return tec_; }
  uint32_t resets() const { return resets_; }
  uint32_t rx_overflows() const { return rx_overflows_; }
  /** Data sheet rules broken so far, one line each. */
  const std::vector<std::string> &violations() const { return violations_; }

  /* Mock SPI device callbacks. */
  void select();
  uint8_t exchange(uint8_t mosi);
  void deselect();

private:
  struct Fifo {
    uint8_t head = 0u; /* next object written (TX) or stored (RX) */
    uint8_t tail = 0u; /* next object sent (TX) or read (RX) */
    uint8_t count = 0u;
    uint16_t start = 0u; /* RAM offset from 0x400 */
    uint8_t attempts_left = 0u;
  };

  uint8_t bus_;
  uint8_t sfr_[0x300] = {};
  uint8_t ram_[2048] = {};
  uint8_t mcp_[0x18] = {};
  Fifo fifo_[32]; /* 0 = TXQ */
  uint8_t opmod_ = kConfig;
  uint8_t tec_ = 0u;
  uint8_t rec_ = 0u;
  uint32_t resets_ = 0u;
  uint32_t rx_overflows_ = 0u;
  std::vector<std::string> violations_;

  /* SPI instruction state. */
  uint8_t cmd_bytes_ = 0u;
  uint8_t cmd_buf_[2] = {};
  uint16_t cmd_ = 0u;
  uint16_t addr_ = 0u;
  uint32_t data_bytes_ = 0u;
  bool iocon_touched_ = false;
  uint8_t word_[4] = {};
  uint8_t word_fill_ = 0u;

  void violation(const std::string &what);
  uint8_t read_byte(uint16_t addr);
  void write_byte(uint16_t addr, uint8_t value);
  void write_sfr(uint16_t addr, uint8_t value);
  void write_fifo_control(uint8_t m, uint8_t byte, uint8_t value);
  bool config_only(uint16_t addr, uint8_t *mask) const;
  void request_mode(uint8_t requested);
  void enter_mode(uint8_t mode);
  void reset_fifo(uint8_t m);
  void compute_layout();
  uint32_t fifocon_value(uint8_t m) const;
  bool is_tx(uint8_t m) const;
  uint8_t capacity(uint8_t m) const;
  uint16_t object_size(uint8_t m) const;
  uint8_t status_byte(uint8_t m) const;
  uint32_t user_address(uint8_t m) const;
  void set_status_flags(uint8_t m, uint8_t set, uint8_t clear);
  void request_tx(uint8_t m);
  void run_tx(uint8_t m);
  Mcp251xfdFrame read_tx_object(uint8_t m) const;
  bool filter_matches(uint8_t f, const Mcp251xfdFrame &frame) const;
  bool store(const Mcp251xfdFrame &frame);
  uint32_t sfr32(uint16_t addr) const;
  void put_sfr32(uint16_t addr, uint32_t value);
};
