#include "../../hal_eeprom.h"
#include "../../hal_sync.h"
#include "../../hal_config.h"
#include "hal_mock.h"

#include <string.h>

/* ── Mock storage ───────────────────────────────────────────────────────────── */

static uint8_t           s_mem[MOCK_EEPROM_BUF_SIZE] = {};
static hal_eeprom_type_t s_type                       = HAL_EEPROM_AT24C256;
static uint16_t          s_size                       = 0;
static bool              s_committed                  = false;
static uint32_t          s_write_count                = 0;
static hal_mutex_t       s_eeprom_mutex               = NULL;

static void eeprom_ensure_mutex(void) {
    if (s_eeprom_mutex == NULL) {
        s_eeprom_mutex = hal_mutex_create();
    }
}

/* ── Public API (mock implementations) ─────────────────────────────────── */

void hal_eeprom_init(hal_eeprom_type_t type, uint16_t size, uint8_t i2c_addr) {
    (void)i2c_addr;
    eeprom_ensure_mutex();
    s_type      = type;
    s_size      = (type == HAL_EEPROM_RP2040) ? size : (uint16_t)32768U;
    s_committed = false;
    memset(s_mem, 0, sizeof(s_mem));
}

/* ── Lock-free helpers (caller holds s_eeprom_mutex) ────────────────── */

static void write_byte_nolock(uint16_t addr, uint8_t val) {
    if (addr < MOCK_EEPROM_BUF_SIZE) {
        s_mem[addr] = val;
        s_write_count++;
    }
}

static uint8_t read_byte_nolock(uint16_t addr) {
    return (addr < MOCK_EEPROM_BUF_SIZE) ? s_mem[addr] : 0;
}

void hal_eeprom_write_byte(uint16_t addr, uint8_t val) {
    hal_mutex_lock(s_eeprom_mutex);
    write_byte_nolock(addr, val);
    hal_mutex_unlock(s_eeprom_mutex);
}

uint8_t hal_eeprom_read_byte(uint16_t addr) {
    hal_mutex_lock(s_eeprom_mutex);
    uint8_t val = read_byte_nolock(addr);
    hal_mutex_unlock(s_eeprom_mutex);
    return val;
}

void hal_eeprom_write_int(uint16_t addr, int32_t val) {
    hal_mutex_lock(s_eeprom_mutex);
    write_byte_nolock(addr + 0, (uint8_t)((val >>  0) & 0xFF));
    write_byte_nolock(addr + 1, (uint8_t)((val >>  8) & 0xFF));
    write_byte_nolock(addr + 2, (uint8_t)((val >> 16) & 0xFF));
    write_byte_nolock(addr + 3, (uint8_t)((val >> 24) & 0xFF));
    hal_mutex_unlock(s_eeprom_mutex);
}

int32_t hal_eeprom_read_int(uint16_t addr) {
    hal_mutex_lock(s_eeprom_mutex);
    int32_t val = (int32_t)(
        ((uint32_t)read_byte_nolock(addr + 0))        |
        ((uint32_t)read_byte_nolock(addr + 1) <<  8)  |
        ((uint32_t)read_byte_nolock(addr + 2) << 16)  |
        ((uint32_t)read_byte_nolock(addr + 3) << 24)
    );
    hal_mutex_unlock(s_eeprom_mutex);
    return val;
}

void hal_eeprom_commit(void) {
    hal_mutex_lock(s_eeprom_mutex);
    s_committed = true;
    hal_mutex_unlock(s_eeprom_mutex);
}

void hal_eeprom_reset(void) {
    hal_mutex_lock(s_eeprom_mutex);
    memset(s_mem, 0, sizeof(s_mem));
    s_committed = true;
    hal_mutex_unlock(s_eeprom_mutex);
}

uint16_t hal_eeprom_size(void) {
    return s_size;
}

/* ── Mock helpers ───────────────────────────────────────────────────────── */

uint8_t hal_mock_eeprom_get_byte(uint16_t addr) {
    return (addr < MOCK_EEPROM_BUF_SIZE) ? s_mem[addr] : 0;
}

hal_eeprom_type_t hal_mock_eeprom_get_type(void) {
    return s_type;
}

bool hal_mock_eeprom_was_committed(void) {
    return s_committed;
}

void hal_mock_eeprom_clear_committed_flag(void) {
    s_committed = false;
}

uint32_t hal_mock_eeprom_get_write_count(void) {
    hal_mutex_lock(s_eeprom_mutex);
    uint32_t v = s_write_count;
    hal_mutex_unlock(s_eeprom_mutex);
    return v;
}

void hal_mock_eeprom_clear_write_count(void) {
    hal_mutex_lock(s_eeprom_mutex);
    s_write_count = 0;
    hal_mutex_unlock(s_eeprom_mutex);
}

void hal_mock_eeprom_reset(void) {
    memset(s_mem, 0, sizeof(s_mem));
    s_type      = HAL_EEPROM_AT24C256;
    s_size      = 0;
    s_committed = false;
    s_write_count = 0;
}
