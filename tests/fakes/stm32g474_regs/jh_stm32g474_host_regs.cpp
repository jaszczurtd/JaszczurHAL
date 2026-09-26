#include "jh_stm32g474_host_regs.h"

#include <cstring>
#include <map>

namespace {

// Cells are 32-bit words keyed by their aligned address; narrower accesses
// land inside the word, little-endian as on the chip. Map elements keep their
// address across insertions, so handed-out pointers stay valid.
std::map<uintptr_t, uint32_t> s_cells;

volatile uint8_t *cell_bytes(uintptr_t address) {
  const uintptr_t word = address & ~(uintptr_t)3u;
  auto it = s_cells.find(word);
  if (it == s_cells.end()) {
    it = s_cells.emplace(word, 0u).first;
  }
  return reinterpret_cast<volatile uint8_t *>(&it->second) + (address - word);
}

} // namespace

extern "C" {

volatile uint8_t *jh_stm32g474_host_reg8(uintptr_t address) {
  return cell_bytes(address);
}

volatile uint16_t *jh_stm32g474_host_reg16(uintptr_t address) {
  return reinterpret_cast<volatile uint16_t *>(
      cell_bytes(address & ~(uintptr_t)1u));
}

volatile uint32_t *jh_stm32g474_host_reg32(uintptr_t address) {
  return reinterpret_cast<volatile uint32_t *>(
      cell_bytes(address & ~(uintptr_t)3u));
}

void jh_stm32g474_host_regs_reset(void) { s_cells.clear(); }

} // extern "C"
