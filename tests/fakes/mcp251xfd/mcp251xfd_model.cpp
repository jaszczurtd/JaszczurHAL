#include "mcp251xfd_model.h"

#include "hal/core/jh_endian.h"
#include "hal/impl/.mock/hal_mock.h"

#include <stdio.h>
#include <string.h>

namespace {

/* Register addresses (DS20006027B, table 3-1). */
constexpr uint16_t kCon = 0x000u;
constexpr uint16_t kTdc = 0x00Cu;
constexpr uint16_t kInt = 0x01Cu;
constexpr uint16_t kTrec = 0x034u;
constexpr uint16_t kBdiag1 = 0x03Cu;
constexpr uint16_t kTefcon = 0x040u;
constexpr uint16_t kFltcon = 0x1D0u;
constexpr uint16_t kFltobj = 0x1F0u;
constexpr uint16_t kSfrEnd = 0x2F0u;
constexpr uint16_t kRamStart = 0x400u;
constexpr uint16_t kRamEnd = 0xC00u;
constexpr uint16_t kOsc = 0xE00u;
constexpr uint16_t kIocon = 0xE04u;
constexpr uint16_t kMcpEnd = 0xE18u;

constexpr uint16_t fifo_reg(uint8_t m) {
  return (uint16_t)(0x050u + 0x0Cu * m);
}

/* CiFIFOSTAm / CiTXQSTA flags cleared by writing 0 (HS/C). */
constexpr uint8_t kStaTxabt = 0x80u, kStaTxlarb = 0x40u, kStaTxerr = 0x20u,
                  kStaTxatif = 0x10u, kStaRxovif = 0x08u;
constexpr uint8_t kStaWriteClear =
    kStaTxabt | kStaTxlarb | kStaTxerr | kStaTxatif | kStaRxovif;

/* CiFIFOCONm byte 1. */
constexpr uint8_t kUinc = 0x01u, kTxreq = 0x02u, kFreset = 0x04u;

constexpr uint8_t kPayload[8] = {8u, 12u, 16u, 20u, 24u, 32u, 48u, 64u};
constexpr uint8_t kDlcBytes[16] = {0u, 1u,  2u,  3u,  4u,  5u,  6u,  7u,
                                   8u, 12u, 16u, 20u, 24u, 32u, 48u, 64u};
constexpr uint8_t kUnlimited = 0xFFu;

/* Payload bytes a frame carries: a classic DLC above 8 still means 8. */
uint8_t frame_bytes(const Mcp251xfdFrame &frame) {
  if (frame.rtr) {
    return 0u;
  }
  return frame.fdf || frame.dlc <= 8u ? kDlcBytes[frame.dlc] : 8u;
}

const hal_mock_spi_device_t kDevice = {
    [](void *u) { static_cast<Mcp251xfdModel *>(u)->select(); },
    [](void *u, uint8_t mosi) {
      return static_cast<Mcp251xfdModel *>(u)->exchange(mosi);
    },
    [](void *u) { static_cast<Mcp251xfdModel *>(u)->deselect(); }};

bool in_iocon(uint16_t addr) { return addr >= kIocon && addr < kIocon + 4u; }

} // namespace

Mcp251xfdModel::Mcp251xfdModel(uint8_t cs_pin, uint8_t bus) : bus_(bus) {
  hal_mock_spi_attach_device(bus, cs_pin, &kDevice, this);
  power_on();
}

Mcp251xfdModel::~Mcp251xfdModel() {
  hal_mock_spi_attach_device(bus_, 0u, nullptr, nullptr);
}

void Mcp251xfdModel::violation(const std::string &what) {
  violations_.push_back(what);
}

/* Registers and message objects are little-endian, as on the chip. */
uint32_t Mcp251xfdModel::sfr32(uint16_t addr) const {
  return jh_load_le32(&sfr_[addr]);
}

void Mcp251xfdModel::put_sfr32(uint16_t addr, uint32_t value) {
  jh_store_le32(&sfr_[addr], value);
}

void Mcp251xfdModel::power_on() {
  memset(sfr_, 0, sizeof(sfr_));
  memset(mcp_, 0, sizeof(mcp_));
  /* POR values, DS20006027B registers 3-1 to 3-34. */
  put_sfr32(kCon, 0x04980760u);
  put_sfr32(0x004u, 0x003E0F0Fu); /* NBTCFG */
  put_sfr32(0x008u, 0x000E0303u); /* DBTCFG */
  put_sfr32(kTdc, 0x00021000u);
  put_sfr32(kTefcon, 0x00000400u);
  for (uint8_t m = 0; m < 32u; ++m) {
    put_sfr32(fifo_reg(m), 0x00600400u);
    fifo_[m] = Fifo();
  }
  sfr_[fifo_reg(0)] |= 0x80u; /* TXQCON.TXEN reads 1 */
  mcp_[0] = 0x60u;            /* OSC.CLKODIV = 10 */
  mcp_[4] = 0x03u;            /* IOCON.TRIS1:0 */
  mcp_[7] = 0x03u;            /* IOCON.PM1:0 */
  mcp_[0x14] = 0x14u;         /* DEVID: MCP2518FD */
  opmod_ = kConfig;
  tec_ = 0u;
  rec_ = 0u;
}

void Mcp251xfdModel::force_mode(Mode mode) {
  compute_layout();
  enter_mode(mode);
}

uint32_t Mcp251xfdModel::reg32(uint16_t addr) const {
  uint8_t bytes[4];
  for (uint8_t i = 0; i < 4u; ++i) {
    bytes[i] =
        const_cast<Mcp251xfdModel *>(this)->read_byte((uint16_t)(addr + i));
  }
  return jh_load_le32(bytes);
}

/* ── FIFO geometry and state ───────────────────────────────────────────── */

uint32_t Mcp251xfdModel::fifocon_value(uint8_t m) const {
  return sfr32(fifo_reg(m));
}

bool Mcp251xfdModel::is_tx(uint8_t m) const {
  return m == 0u || (sfr_[fifo_reg(m)] & 0x80u) != 0u;
}

uint8_t Mcp251xfdModel::capacity(uint8_t m) const {
  return (uint8_t)(((fifocon_value(m) >> 24) & 0x1Fu) + 1u);
}

uint16_t Mcp251xfdModel::object_size(uint8_t m) const {
  const bool ts = !is_tx(m) && (sfr_[fifo_reg(m)] & 0x20u) != 0u;
  return (uint16_t)(8u + (ts ? 4u : 0u) + kPayload[fifocon_value(m) >> 29]);
}

void Mcp251xfdModel::compute_layout() {
  uint32_t offset = 0u;
  if ((sfr_[kCon + 2u] & 0x08u) != 0u) { /* STEF */
    const uint32_t ts = (sfr_[kTefcon] & 0x20u) != 0u ? 4u : 0u;
    offset += (((sfr32(kTefcon) >> 24) & 0x1Fu) + 1u) * (8u + ts);
  }
  for (uint8_t m = 0; m < 32u; ++m) {
    if (m == 0u && (sfr_[kCon + 2u] & 0x10u) == 0u) { /* no TXQEN */
      continue;
    }
    fifo_[m].start = (uint16_t)offset;
    offset += (uint32_t)capacity(m) * object_size(m);
  }
}

void Mcp251xfdModel::reset_fifo(uint8_t m) {
  const uint16_t start = fifo_[m].start;
  fifo_[m] = Fifo();
  fifo_[m].start = start;
  sfr_[fifo_reg(m) + 1u] &= (uint8_t)~kTxreq;
  sfr_[fifo_reg(m) + 4u] = 0u; /* status flags */
}

uint32_t Mcp251xfdModel::user_address(uint8_t m) const {
  const Fifo &f = fifo_[m];
  return (uint32_t)f.start +
         (uint32_t)(is_tx(m) ? f.head : f.tail) * object_size(m);
}

uint8_t Mcp251xfdModel::status_byte(uint8_t m) const {
  const Fifo &f = fifo_[m];
  const uint8_t cap = capacity(m);
  uint8_t s = sfr_[fifo_reg(m) + 4u];
  if (m == 0u) {
    s |= (f.count < cap ? 0x01u : 0u) | (f.count == 0u ? 0x04u : 0u);
  } else if (is_tx(m)) {
    s |= (f.count < cap ? 0x01u : 0u) | (f.count <= cap / 2u ? 0x02u : 0u) |
         (f.count == 0u ? 0x04u : 0u);
  } else {
    s |= (f.count > 0u ? 0x01u : 0u) | (f.count >= cap / 2u ? 0x02u : 0u) |
         (f.count == cap ? 0x04u : 0u);
  }
  return s;
}

void Mcp251xfdModel::set_status_flags(uint8_t m, uint8_t set, uint8_t clear) {
  uint8_t &s = sfr_[fifo_reg(m) + 4u];
  s = (uint8_t)((s & ~clear) | set);
}

/* ── Modes ──────────────────────────────────────────────────────────────── */

void Mcp251xfdModel::request_mode(uint8_t requested) {
  if (requested == opmod_) {
    return;
  }
  const bool from_normal = opmod_ == kMixed || opmod_ == kCan20;
  const bool allowed =
      opmod_ == kConfig || requested == kConfig ||
      (from_normal && (requested == kSleep || requested == kListenOnly ||
                       requested == kRestricted));
  if (!allowed) {
    char msg[96];
    snprintf(msg, sizeof(msg),
             "mode %u requested from mode %u without Configuration mode",
             (unsigned)requested, (unsigned)opmod_);
    violation(msg);
    return;
  }
  enter_mode(requested);
}

void Mcp251xfdModel::enter_mode(uint8_t mode) {
  if (mode == kConfig) {
    for (uint8_t m = 0; m < 32u; ++m) {
      reset_fifo(m);
      sfr_[fifo_reg(m) + 1u] |= kFreset;
    }
  } else if (opmod_ == kConfig) {
    compute_layout();
    for (uint8_t m = 0; m < 32u; ++m) {
      sfr_[fifo_reg(m) + 1u] &= (uint8_t)~kFreset;
    }
    tec_ = 0u;
    rec_ = 0u;
  }
  opmod_ = mode;
  sfr_[kCon + 2u] = (uint8_t)((sfr_[kCon + 2u] & 0x1Fu) | (mode << 5));
}

/* Configuration-only bits of an SFR byte (FRM 2.2). */
bool Mcp251xfdModel::config_only(uint16_t addr, uint8_t *mask) const {
  *mask = 0u;
  if (addr == kCon) {
    *mask = 0x60u; /* PXEDIS, ISOCRCEN */
  } else if (addr == kCon + 1u) {
    *mask = 0x01u; /* WAKFIL */
  } else if (addr == kCon + 2u) {
    *mask = 0x1Fu; /* TXQEN, STEF, SERR2LOM, ESIGM, RTXAT */
  } else if (addr >= 0x004u && addr < 0x010u) {
    *mask = 0xFFu; /* NBTCFG, DBTCFG, TDC */
  } else if (addr == kTefcon) {
    *mask = 0x20u; /* TEFTSEN */
  } else if (addr == kTefcon + 3u) {
    *mask = 0x1Fu; /* FSIZE */
  } else if (addr >= fifo_reg(0) && addr < kFltcon) {
    const uint16_t rel = (uint16_t)((addr - fifo_reg(0)) % 0x0Cu);
    if (rel == 0u) {
      *mask = 0xA0u; /* TXEN, RXTSEN */
    } else if (rel == 3u) {
      *mask = 0xFFu; /* PLSIZE, FSIZE */
    }
  }
  return *mask != 0u;
}

/* ── SPI instructions ───────────────────────────────────────────────────── */

void Mcp251xfdModel::select() {
  cmd_bytes_ = 0u;
  cmd_ = 0u;
  data_bytes_ = 0u;
  iocon_touched_ = false;
  word_fill_ = 0u;
  /* Bus time passes between instructions: pending frames retry. */
  for (uint8_t m = 0; m < 32u; ++m) {
    if (is_tx(m) && (sfr_[fifo_reg(m) + 1u] & kTxreq) != 0u) {
      run_tx(m);
    }
  }
}

uint8_t Mcp251xfdModel::exchange(uint8_t mosi) {
  if (cmd_bytes_ < 2u) {
    cmd_buf_[cmd_bytes_] = mosi;
    if (++cmd_bytes_ == 2u) {
      cmd_ = jh_load_be16(cmd_buf_); /* command and address, MSB first */
      addr_ = (uint16_t)(cmd_ & 0x0FFFu);
      const uint8_t c = (uint8_t)(cmd_ >> 12);
      if ((c == 0x2u || c == 0x3u) && addr_ >= kRamStart && addr_ < kRamEnd &&
          (addr_ & 3u) != 0u) {
        violation("unaligned RAM access");
        addr_ = (uint16_t)(addr_ & ~3u);
      }
    }
    return 0u;
  }
  const uint8_t c = (uint8_t)(cmd_ >> 12);
  const uint16_t addr = addr_;
  addr_ = (uint16_t)(addr_ + 1u == kRamEnd ? kRamStart : addr_ + 1u);
  ++data_bytes_;
  if (c == 0x3u) {
    return read_byte(addr);
  }
  if (c == 0x2u) {
    iocon_touched_ = iocon_touched_ || in_iocon(addr);
    if (addr >= kRamStart && addr < kRamEnd) {
      word_[word_fill_++] = mosi;
      if (word_fill_ == 4u) {
        memcpy(&ram_[(addr - kRamStart) & ~3u], word_, 4u);
        word_fill_ = 0u;
      }
    } else {
      write_byte(addr, mosi);
    }
    return 0u;
  }
  if (data_bytes_ == 1u) {
    violation("unsupported SPI instruction");
  }
  return 0u;
}

void Mcp251xfdModel::deselect() {
  const uint8_t c = (uint8_t)(cmd_ >> 12);
  if (cmd_bytes_ == 2u && c == 0x0u && data_bytes_ == 0u) {
    if (opmod_ != kConfig) {
      violation("RESET outside Configuration mode");
    } else {
      power_on();
      ++resets_;
    }
  }
  const uint16_t start = (uint16_t)(cmd_ & 0x0FFFu);
  const bool ram = start >= kRamStart && start < kRamEnd;
  if (ram && (c == 0x2u || c == 0x3u) && (data_bytes_ % 4u) != 0u) {
    violation(c == 0x2u ? "RAM write not a multiple of 4 bytes"
                        : "RAM read not a multiple of 4 bytes");
  }
  if (c == 0x2u && iocon_touched_ && data_bytes_ > 1u) {
    violation("IOCON written with a multi-byte instruction");
  }
  cmd_bytes_ = 0u;
}

uint8_t Mcp251xfdModel::read_byte(uint16_t addr) {
  if (addr >= kRamStart && addr < kRamEnd) {
    return ram_[addr - kRamStart];
  }
  if (addr >= kOsc && addr < kMcpEnd) {
    uint8_t v = mcp_[addr - kOsc];
    if (addr == kOsc + 1u) {
      v = (uint8_t)(((mcp_[0] & 0x04u) == 0u ? 0x04u : 0u) | /* OSCRDY */
                    ((mcp_[0] & 0x01u) != 0u ? 0x01u : 0u) | /* PLLRDY */
                    ((mcp_[0] & 0x10u) != 0u ? 0x10u : 0u)); /* SCLKRDY */
    }
    return v;
  }
  if (addr >= kSfrEnd) {
    return 0u;
  }
  if (addr >= kTrec && addr < kTrec + 4u) {
    uint32_t trec = 0x00200000u;
    if (opmod_ != kConfig) {
      trec = (uint32_t)rec_ | ((uint32_t)tec_ << 8) |
             (tec_ >= 96u || rec_ >= 96u ? (1u << 16) : 0u) |
             (rec_ >= 96u ? (1u << 17) : 0u) | (tec_ >= 96u ? (1u << 18) : 0u) |
             (rec_ >= 128u ? (1u << 19) : 0u) |
             (tec_ >= 128u ? (1u << 20) : 0u);
    }
    uint8_t bytes[4];
    jh_store_le32(bytes, trec);
    return bytes[addr - kTrec];
  }
  if (addr >= fifo_reg(0) && addr < kFltcon) {
    const uint8_t m = (uint8_t)((addr - fifo_reg(0)) / 0x0Cu);
    const uint16_t rel = (uint16_t)((addr - fifo_reg(0)) % 0x0Cu);
    if (rel == 4u) {
      return status_byte(m);
    }
    if (rel == 5u) {
      return is_tx(m) ? fifo_[m].tail : fifo_[m].head; /* FIFOCI */
    }
    if (rel >= 8u) {
      uint8_t bytes[4];
      jh_store_le32(bytes, user_address(m));
      return bytes[rel - 8u];
    }
    if (rel == 6u || rel == 7u) {
      return 0u;
    }
  }
  return sfr_[addr];
}

void Mcp251xfdModel::write_byte(uint16_t addr, uint8_t value) {
  if (addr >= kOsc && addr < kMcpEnd) {
    if (addr == kOsc && opmod_ != kConfig &&
        ((mcp_[0] ^ value) & 0x11u) != 0u) {
      violation("OSC PLLEN/SCLKDIV changed outside Configuration mode");
      value = (uint8_t)((value & ~0x11u) | (mcp_[0] & 0x11u));
    }
    if (addr != kOsc + 1u && addr < 0xE14u) {
      mcp_[addr - kOsc] = value;
    }
    return;
  }
  if (addr < kSfrEnd) {
    write_sfr(addr, value);
  }
}

void Mcp251xfdModel::write_sfr(uint16_t addr, uint8_t value) {
  uint8_t mask = 0u;
  if (config_only(addr, &mask) && opmod_ != kConfig &&
      ((sfr_[addr] ^ value) & mask) != 0u) {
    char msg[80];
    snprintf(msg, sizeof(msg), "SFR 0x%03X changed outside Configuration mode",
             (unsigned)addr);
    violation(msg);
    value = (uint8_t)((value & ~mask) | (sfr_[addr] & mask));
  }
  if (addr == kCon + 2u) {
    sfr_[addr] = (uint8_t)((value & 0x1Fu) | (opmod_ << 5));
    return;
  }
  if (addr == kCon + 3u) {
    sfr_[addr] = value;
    if ((value & 0x08u) != 0u) { /* ABAT */
      for (uint8_t m = 0; m < 32u; ++m) {
        if ((sfr_[fifo_reg(m) + 1u] & kTxreq) != 0u) {
          sfr_[fifo_reg(m) + 1u] &= (uint8_t)~kTxreq;
          set_status_flags(m, kStaTxabt, 0u);
        }
      }
    }
    request_mode((uint8_t)(value & 0x07u));
    return;
  }
  if (addr == kTdc) {
    sfr_[addr] = (uint8_t)(sfr_[addr] & 0x3Fu); /* TDCV is read-only */
    return;
  }
  if (addr >= kTrec && addr < kTefcon) {
    return; /* TREC, BDIAG0/1 */
  }
  if (addr >= fifo_reg(0) && addr < kFltcon) {
    const uint8_t m = (uint8_t)((addr - fifo_reg(0)) / 0x0Cu);
    const uint8_t rel = (uint8_t)((addr - fifo_reg(0)) % 0x0Cu);
    if (rel < 4u) {
      write_fifo_control(m, rel, value);
    } else if (rel == 4u) {
      sfr_[addr] = (uint8_t)(sfr_[addr] & (value | ~kStaWriteClear));
    }
    return; /* FIFOCI and the user address are read-only */
  }
  if (addr >= kFltobj) {
    const uint8_t f = (uint8_t)((addr - kFltobj) / 8u);
    if ((sfr_[kFltcon + f] & 0x80u) != 0u) {
      char msg[80];
      snprintf(msg, sizeof(msg),
               "filter %u object or mask changed while enabled", (unsigned)f);
      violation(msg);
      return;
    }
  }
  sfr_[addr] = value;
}

void Mcp251xfdModel::write_fifo_control(uint8_t m, uint8_t byte,
                                        uint8_t value) {
  const uint16_t base = fifo_reg(m);
  if (byte != 1u) {
    if (m == 0u && byte == 0u) {
      value |= 0x80u; /* TXQ: TXEN reads 1 */
    }
    sfr_[base + byte] = value;
    return;
  }
  if (opmod_ == kConfig) {
    if ((value & (kUinc | kTxreq)) != 0u) {
      violation("UINC or TXREQ in Configuration mode");
    }
    return;
  }
  if ((value & kFreset) != 0u) {
    reset_fifo(m);
  }
  Fifo &f = fifo_[m];
  const uint8_t cap = capacity(m);
  if ((value & kUinc) != 0u) {
    if (is_tx(m)) {
      if (f.count == cap) {
        violation("UINC on a full transmit FIFO");
      } else {
        f.head = (uint8_t)((f.head + 1u) % cap);
        ++f.count;
      }
    } else if (f.count == 0u) {
      violation("UINC on an empty receive FIFO");
    } else {
      f.tail = (uint8_t)((f.tail + 1u) % cap);
      --f.count;
    }
  }
  const bool pending = (sfr_[base + 1u] & kTxreq) != 0u;
  if (is_tx(m) && (value & kTxreq) != 0u) {
    request_tx(m);
  } else if (is_tx(m) && pending && (value & kTxreq) == 0u) {
    sfr_[base + 1u] &= (uint8_t)~kTxreq; /* abort, FRM 4.10 */
    set_status_flags(m, kStaTxabt, 0u);
  }
}

/* ── Transmission ───────────────────────────────────────────────────────── */

void Mcp251xfdModel::request_tx(uint8_t m) {
  if (opmod_ != kMixed && opmod_ != kCan20 && opmod_ != kIntLoopback &&
      opmod_ != kExtLoopback) {
    return; /* TXREQ ignored */
  }
  sfr_[fifo_reg(m) + 1u] |= kTxreq;
  set_status_flags(m, 0u, kStaTxabt | kStaTxlarb | kStaTxerr | kStaTxatif);
  uint8_t attempts = kUnlimited;
  if ((sfr_[kCon + 2u] & 0x01u) != 0u) { /* RTXAT */
    const uint8_t txat = (uint8_t)((sfr_[fifo_reg(m) + 2u] >> 5) & 0x03u);
    attempts = txat == 0u ? 1u : (txat == 1u ? 3u : kUnlimited);
  }
  fifo_[m].attempts_left = attempts;
  run_tx(m);
}

Mcp251xfdFrame Mcp251xfdModel::read_tx_object(uint8_t m) const {
  const Fifo &f = fifo_[m];
  const uint32_t at = (uint32_t)f.start + (uint32_t)f.tail * object_size(m);
  Mcp251xfdFrame frame;
  const uint32_t t0 = jh_load_le32(&ram_[at]);
  const uint32_t t1 = jh_load_le32(&ram_[at + 4u]);
  frame.ext = (t1 & 0x10u) != 0u;
  frame.rtr = (t1 & 0x20u) != 0u;
  frame.brs = (t1 & 0x40u) != 0u;
  frame.fdf = (t1 & 0x80u) != 0u;
  frame.esi = (t1 & 0x100u) != 0u;
  frame.dlc = (uint8_t)(t1 & 0x0Fu);
  const uint32_t sid = t0 & 0x7FFu;
  frame.id = frame.ext ? (sid << 18) | ((t0 >> 11) & 0x3FFFFu) : sid;
  memcpy(frame.data, &ram_[at + 8u], frame_bytes(frame));
  return frame;
}

void Mcp251xfdModel::run_tx(uint8_t m) {
  Fifo &f = fifo_[m];
  const uint16_t base = fifo_reg(m);
  if ((uint32_t)f.start + (uint32_t)capacity(m) * object_size(m) > 2048u) {
    violation("transmit FIFO beyond the end of RAM");
    return;
  }
  while ((sfr_[base + 1u] & kTxreq) != 0u && f.count > 0u) {
    Mcp251xfdFrame frame = read_tx_object(m);
    const uint8_t payload = kPayload[fifocon_value(m) >> 29];
    if (frame_bytes(frame) > payload) { /* FRM 4.12 */
      sfr_[kInt + 1u] |= 0x80u;         /* IVMIF */
      sfr_[kBdiag1 + 3u] |= 0x80u;      /* DLCMM */
      sfr_[base + 1u] &= (uint8_t)~kTxreq;
      return;
    }
    if (opmod_ == kCan20) {
      frame.fdf = frame.brs = frame.esi = false;
    }
    bool ok = true;
    if (opmod_ == kIntLoopback || opmod_ == kExtLoopback) {
      (void)store(frame);
    } else if (bus_acks) {
      sent.push_back(frame);
    } else {
      ok = false;
    }
    if (!ok) {
      tec_ = (uint8_t)(tec_ + 8u > 128u ? 128u : tec_ + 8u);
      set_status_flags(m, kStaTxerr, 0u);
      if (f.attempts_left != kUnlimited && --f.attempts_left == 0u) {
        set_status_flags(m, kStaTxatif, 0u);
        sfr_[base + 1u] &= (uint8_t)~kTxreq; /* index stays, FRM 4.9.1 */
      }
      return;
    }
    if (tec_ > 0u) {
      --tec_;
    }
    f.tail = (uint8_t)((f.tail + 1u) % capacity(m));
    --f.count;
  }
  if (f.count == 0u) {
    sfr_[base + 1u] &= (uint8_t)~kTxreq;
  }
}

/* ── Reception ──────────────────────────────────────────────────────────── */

bool Mcp251xfdModel::filter_matches(uint8_t f,
                                    const Mcp251xfdFrame &frame) const {
  const uint32_t obj = sfr32((uint16_t)(kFltobj + 8u * f));
  const uint32_t mask = sfr32((uint16_t)(kFltobj + 4u + 8u * f));
  const bool exide = (obj & (1u << 30)) != 0u;
  if ((mask & (1u << 30)) != 0u && exide != frame.ext) { /* MIDE */
    return false;
  }
  const uint32_t sid =
      frame.ext ? (frame.id >> 18) & 0x7FFu : frame.id & 0x7FFu;
  if (((obj ^ sid) & mask & 0x7FFu) != 0u) {
    return false;
  }
  if (frame.ext) {
    const uint32_t eid = frame.id & 0x3FFFFu;
    if ((((obj >> 11) ^ eid) & (mask >> 11) & 0x3FFFFu) != 0u) {
      return false;
    }
  }
  return true;
}

bool Mcp251xfdModel::store(const Mcp251xfdFrame &frame) {
  for (uint8_t flt = 0; flt < 32u; ++flt) {
    const uint8_t con = sfr_[kFltcon + flt];
    if ((con & 0x80u) == 0u || !filter_matches(flt, frame)) {
      continue;
    }
    const uint8_t m = (uint8_t)(con & 0x1Fu);
    if (m == 0u || is_tx(m)) {
      continue; /* remote request answering is not modelled */
    }
    Fifo &f = fifo_[m];
    const uint8_t cap = capacity(m);
    if ((uint32_t)f.start + (uint32_t)cap * object_size(m) > 2048u) {
      violation("receive FIFO beyond the end of RAM");
      return false;
    }
    if (f.count == cap) {
      set_status_flags(m, kStaRxovif, 0u);
      ++rx_overflows_;
      continue;
    }
    const uint32_t at = (uint32_t)f.start + (uint32_t)f.head * object_size(m);
    const uint32_t sid =
        frame.ext ? (frame.id >> 18) & 0x7FFu : frame.id & 0x7FFu;
    const uint32_t r0 = sid | (frame.ext ? (frame.id & 0x3FFFFu) << 11 : 0u);
    const uint32_t r1 = (uint32_t)(frame.dlc & 0x0Fu) |
                        (frame.ext ? 0x10u : 0u) | (frame.rtr ? 0x20u : 0u) |
                        (frame.brs ? 0x40u : 0u) | (frame.fdf ? 0x80u : 0u) |
                        (frame.esi ? 0x100u : 0u) | ((uint32_t)flt << 11);
    jh_store_le32(&ram_[at], r0);
    jh_store_le32(&ram_[at + 4u], r1);
    const uint32_t data_at =
        at + ((sfr_[fifo_reg(m)] & 0x20u) != 0u ? 12u : 8u);
    const uint8_t payload = kPayload[fifocon_value(m) >> 29];
    const uint8_t bytes = frame_bytes(frame);
    memset(&ram_[data_at], 0, payload);
    memcpy(&ram_[data_at], frame.data, bytes < payload ? bytes : payload);
    f.head = (uint8_t)((f.head + 1u) % cap);
    ++f.count;
    return true;
  }
  return false;
}

bool Mcp251xfdModel::deliver(const Mcp251xfdFrame &frame) {
  if (opmod_ != kMixed && opmod_ != kCan20 && opmod_ != kListenOnly &&
      opmod_ != kRestricted) {
    return false; /* RXCAN not connected */
  }
  if (opmod_ == kCan20 && frame.fdf) {
    if (rec_ < 255u) {
      ++rec_; /* error frame on a CAN FD frame, FRM 2.3.2 */
    }
    return false;
  }
  return store(frame);
}
