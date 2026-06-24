#include "hal/hal_target.h"
#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK)

#include "hal/hal_config.h"
#ifdef HAL_ENABLE_HD44780

#include "hd44780.h"

#include "hal/hal_gpio.h"
#include "hal/hal_system.h"
#include "hal/impl/shared/hal_mutex_once.h"

#include <stddef.h>

// When the display powers up, it is configured as follows:
//
// 1. Display clear
// 2. Function set:
//    DL = 1; 8-bit interface data
//    N = 0; 1-line display
//    F = 0; 5x8 dot character font
// 3. Display on/off control:
//    D = 0; Display off
//    C = 0; Cursor off
//    B = 0; Blinking off
// 4. Entry mode set:
//    I/D = 1; Increment by 1
//    S = 0; No shift
//
// Note, however, that resetting the MCU does not reset the LCD, so we cannot
// assume that state when a program starts and the constructor is called.

HD44780::HD44780(uint8_t rs, uint8_t rw, uint8_t enable, uint8_t d0, uint8_t d1,
                 uint8_t d2, uint8_t d3, uint8_t d4, uint8_t d5, uint8_t d6,
                 uint8_t d7)
    : _mutex(NULL), _rs_pin(0u), _rw_pin(HD44780_NO_PIN), _enable_pin(0u),
      _data_pins{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}, _displayfunction(0u),
      _displaycontrol(0u), _displaymode(0u), _initialized(0u), _numlines(0u),
      _row_offsets{0u, 0u, 0u, 0u}, _currow(0u) {
  init(0, rs, rw, enable, d0, d1, d2, d3, d4, d5, d6, d7);
}

HD44780::HD44780(uint8_t rs, uint8_t enable, uint8_t d0, uint8_t d1, uint8_t d2,
                 uint8_t d3, uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7)
    : HD44780(rs, HD44780_NO_PIN, enable, d0, d1, d2, d3, d4, d5, d6, d7) {}

HD44780::HD44780(uint8_t rs, uint8_t rw, uint8_t enable, uint8_t d0, uint8_t d1,
                 uint8_t d2, uint8_t d3)
    : _mutex(NULL), _rs_pin(0u), _rw_pin(HD44780_NO_PIN), _enable_pin(0u),
      _data_pins{0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}, _displayfunction(0u),
      _displaycontrol(0u), _displaymode(0u), _initialized(0u), _numlines(0u),
      _row_offsets{0u, 0u, 0u, 0u}, _currow(0u) {
  init(1, rs, rw, enable, d0, d1, d2, d3, 0, 0, 0, 0);
}

HD44780::HD44780(uint8_t rs, uint8_t enable, uint8_t d0, uint8_t d1, uint8_t d2,
                 uint8_t d3)
    : HD44780(rs, HD44780_NO_PIN, enable, d0, d1, d2, d3) {}

HD44780::~HD44780() {
  if (_mutex != NULL) {
    hal_mutex_destroy(_mutex);
    _mutex = NULL;
  }
}

bool HD44780::ensureMutex() {
  return jh_hal_mutex_create_once(&_mutex) != NULL;
}

bool HD44780::lock() {
  if (!ensureMutex()) {
    return false;
  }
  hal_mutex_lock(_mutex);
  return true;
}

void HD44780::unlock() {
  if (_mutex != NULL) {
    hal_mutex_unlock(_mutex);
  }
}

void HD44780::init(uint8_t fourbitmode, uint8_t rs, uint8_t rw, uint8_t enable,
                   uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3, uint8_t d4,
                   uint8_t d5, uint8_t d6, uint8_t d7) {
  if (!lock()) {
    return;
  }

  _rs_pin = rs;
  _rw_pin = rw;
  _enable_pin = enable;

  _data_pins[0] = d0;
  _data_pins[1] = d1;
  _data_pins[2] = d2;
  _data_pins[3] = d3;
  _data_pins[4] = d4;
  _data_pins[5] = d5;
  _data_pins[6] = d6;
  _data_pins[7] = d7;

  _initialized = 0u;

  if (fourbitmode) {
    _displayfunction = LCD_4BITMODE;
  } else {
    _displayfunction = LCD_8BITMODE;
  }

  beginUnlocked(16, 1, LCD_5x8DOTS);
  unlock();
}

void HD44780::begin(uint8_t cols, uint8_t lines, uint8_t dotsize) {
  if (!lock()) {
    return;
  }
  beginUnlocked(cols, lines, dotsize);
  unlock();
}

void HD44780::beginUnlocked(uint8_t cols, uint8_t lines, uint8_t dotsize) {
  if (lines > 1) {
    _displayfunction |= LCD_2LINE;
  }
  _numlines = lines;

  setRowOffsetsUnlocked(0x00, 0x40, 0x00 + cols, 0x40 + cols);

  // For some one-line displays, a 10-pixel-high font can be selected.
  if ((dotsize != LCD_5x8DOTS) && (lines == 1)) {
    _displayfunction |= LCD_5x10DOTS;
  }

  hal_gpio_set_mode(_rs_pin, HAL_GPIO_OUTPUT);
  // Save one pin by not using RW. Indicate by passing 255 instead of a pin.
  if (_rw_pin != HD44780_NO_PIN) {
    hal_gpio_set_mode(_rw_pin, HAL_GPIO_OUTPUT);
  }
  hal_gpio_set_mode(_enable_pin, HAL_GPIO_OUTPUT);

  // Do these once, instead of every time a character is drawn, for speed.
  for (int i = 0; i < ((_displayfunction & LCD_8BITMODE) ? 8 : 4); ++i) {
    hal_gpio_set_mode(_data_pins[i], HAL_GPIO_OUTPUT);
  }

  // SEE PAGE 45/46 FOR INITIALIZATION SPECIFICATION.
  // According to the datasheet, wait at least 40 ms after power rises above
  // 2.7 V before sending commands.
  hal_delay_us(50000u);

  // Pull both RS and RW low to begin commands.
  hal_gpio_write(_rs_pin, false);
  hal_gpio_write(_enable_pin, false);
  if (_rw_pin != HD44780_NO_PIN) {
    hal_gpio_write(_rw_pin, false);
  }

  // Put the LCD into 4-bit or 8-bit mode.
  if (!(_displayfunction & LCD_8BITMODE)) {
    // This follows the Hitachi HD44780 datasheet, figure 24, page 46.
    write4bits(0x03);
    hal_delay_us(4500u);

    write4bits(0x03);
    hal_delay_us(4500u);

    write4bits(0x03);
    hal_delay_us(150u);

    write4bits(0x02);
  } else {
    // This follows the Hitachi HD44780 datasheet, figure 23, page 45.
    commandUnlocked(LCD_FUNCTIONSET | _displayfunction);
    hal_delay_us(4500u);

    commandUnlocked(LCD_FUNCTIONSET | _displayfunction);
    hal_delay_us(150u);

    commandUnlocked(LCD_FUNCTIONSET | _displayfunction);
  }

  // Finally, set number of lines, font size, etc.
  commandUnlocked(LCD_FUNCTIONSET | _displayfunction);

  // Turn the display on with no cursor or blinking by default.
  _displaycontrol = LCD_DISPLAYON;
  commandUnlocked(LCD_DISPLAYCONTROL | _displaycontrol);

  clearUnlocked();

  // Initialize to default text direction for left-to-right languages.
  _displaymode = LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT;
  commandUnlocked(LCD_ENTRYMODESET | _displaymode);
  _initialized = 1u;
}

void HD44780::setRowOffsets(int row0, int row1, int row2, int row3) {
  if (!lock()) {
    return;
  }
  setRowOffsetsUnlocked(row0, row1, row2, row3);
  unlock();
}

void HD44780::setRowOffsetsUnlocked(int row0, int row1, int row2, int row3) {
  _row_offsets[0] = (uint8_t)row0;
  _row_offsets[1] = (uint8_t)row1;
  _row_offsets[2] = (uint8_t)row2;
  _row_offsets[3] = (uint8_t)row3;
}

/********** high level commands, for the user! */
void HD44780::clear() {
  if (!lock()) {
    return;
  }
  clearUnlocked();
  unlock();
}

void HD44780::clearUnlocked() {
  commandUnlocked(LCD_CLEARDISPLAY);
  hal_delay_us(2000u);
  _currow = 0u; // clear returns the cursor to the home position
}

void HD44780::home() {
  if (!lock()) {
    return;
  }
  homeUnlocked();
  unlock();
}

void HD44780::homeUnlocked() {
  commandUnlocked(LCD_RETURNHOME);
  hal_delay_us(2000u);
  _currow = 0u; // home returns the cursor to the home position
}

void HD44780::setCursor(uint8_t col, uint8_t row) {
  if (!lock()) {
    return;
  }
  setCursorUnlocked(col, row);
  unlock();
}

void HD44780::setCursorUnlocked(uint8_t col, uint8_t row) {
  const size_t max_lines = sizeof(_row_offsets) / sizeof(*_row_offsets);
  if (row >= max_lines) {
    row = (uint8_t)(max_lines - 1u);
  }
  if (row >= _numlines) {
    row = (uint8_t)(_numlines - 1u);
  }

  commandUnlocked((uint8_t)(LCD_SETDDRAMADDR | (col + _row_offsets[row])));
  _currow = row;
}

void HD44780::noDisplay() {
  if (!lock()) {
    return;
  }
  _displaycontrol &= (uint8_t)~LCD_DISPLAYON;
  commandUnlocked(LCD_DISPLAYCONTROL | _displaycontrol);
  unlock();
}

void HD44780::display() {
  if (!lock()) {
    return;
  }
  _displaycontrol |= LCD_DISPLAYON;
  commandUnlocked(LCD_DISPLAYCONTROL | _displaycontrol);
  unlock();
}

void HD44780::noCursor() {
  if (!lock()) {
    return;
  }
  _displaycontrol &= (uint8_t)~LCD_CURSORON;
  commandUnlocked(LCD_DISPLAYCONTROL | _displaycontrol);
  unlock();
}

void HD44780::cursor() {
  if (!lock()) {
    return;
  }
  _displaycontrol |= LCD_CURSORON;
  commandUnlocked(LCD_DISPLAYCONTROL | _displaycontrol);
  unlock();
}

void HD44780::noBlink() {
  if (!lock()) {
    return;
  }
  _displaycontrol &= (uint8_t)~LCD_BLINKON;
  commandUnlocked(LCD_DISPLAYCONTROL | _displaycontrol);
  unlock();
}

void HD44780::blink() {
  if (!lock()) {
    return;
  }
  _displaycontrol |= LCD_BLINKON;
  commandUnlocked(LCD_DISPLAYCONTROL | _displaycontrol);
  unlock();
}

void HD44780::scrollDisplayLeft() {
  if (!lock()) {
    return;
  }
  commandUnlocked(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVELEFT);
  unlock();
}

void HD44780::scrollDisplayRight() {
  if (!lock()) {
    return;
  }
  commandUnlocked(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVERIGHT);
  unlock();
}

void HD44780::leftToRight() {
  if (!lock()) {
    return;
  }
  _displaymode |= LCD_ENTRYLEFT;
  commandUnlocked(LCD_ENTRYMODESET | _displaymode);
  unlock();
}

void HD44780::rightToLeft() {
  if (!lock()) {
    return;
  }
  _displaymode &= (uint8_t)~LCD_ENTRYLEFT;
  commandUnlocked(LCD_ENTRYMODESET | _displaymode);
  unlock();
}

void HD44780::autoscroll() {
  if (!lock()) {
    return;
  }
  _displaymode |= LCD_ENTRYSHIFTINCREMENT;
  commandUnlocked(LCD_ENTRYMODESET | _displaymode);
  unlock();
}

void HD44780::noAutoscroll() {
  if (!lock()) {
    return;
  }
  _displaymode &= (uint8_t)~LCD_ENTRYSHIFTINCREMENT;
  commandUnlocked(LCD_ENTRYMODESET | _displaymode);
  unlock();
}

void HD44780::createChar(uint8_t location, uint8_t charmap[]) {
  createChar(location, (const uint8_t *)charmap);
}

void HD44780::createChar(uint8_t location, const uint8_t charmap[]) {
  if (!lock()) {
    return;
  }
  location &= 0x7u;
  commandUnlocked((uint8_t)(LCD_SETCGRAMADDR | (location << 3)));
  for (int i = 0; i < 8; i++) {
    writeByteUnlocked(charmap[i]);
  }
  unlock();
}

/*********** mid level commands, for sending data/cmds */
void HD44780::command(uint8_t value) {
  if (!lock()) {
    return;
  }
  commandUnlocked(value);
  unlock();
}

void HD44780::commandUnlocked(uint8_t value) { send(value, false); }

size_t HD44780::write(uint8_t value) {
  if (!lock()) {
    return 0u;
  }
  const size_t written = writeByteUnlocked(value);
  unlock();
  return written;
}

size_t HD44780::writeByteUnlocked(uint8_t value) {
  send(value, true);
  return 1u;
}

size_t HD44780::write(const uint8_t *buffer, size_t size) {
  if (!lock()) {
    return 0u;
  }
  const size_t written = writeBufferUnlocked(buffer, size);
  unlock();
  return written;
}

size_t HD44780::writeBufferUnlocked(const uint8_t *buffer, size_t size) {
  if (buffer == NULL) {
    return 0u;
  }
  size_t written = 0u;
  while (written < size) {
    writeByteUnlocked(buffer[written]);
    written++;
  }
  return written;
}

size_t HD44780::write(const char *str) {
  if (!lock()) {
    return 0u;
  }
  const size_t written = writeStringUnlocked(str);
  unlock();
  return written;
}

size_t HD44780::writeStringUnlocked(const char *str) {
  if (str == NULL) {
    return 0u;
  }
  size_t written = 0u;
  while (str[written] != '\0') {
    writeByteUnlocked((uint8_t)str[written]);
    written++;
  }
  return written;
}

size_t HD44780::print(const char *str) {
  if (!lock()) {
    return 0u;
  }
  const size_t written = writeStringUnlocked(str);
  unlock();
  return written;
}

size_t HD44780::print(char c) {
  if (!lock()) {
    return 0u;
  }
  const size_t written = writeByteUnlocked((uint8_t)c);
  unlock();
  return written;
}

size_t HD44780::print(unsigned char value) {
  if (!lock()) {
    return 0u;
  }
  const size_t written = printUnsignedUnlocked((unsigned long)value);
  unlock();
  return written;
}

size_t HD44780::print(int value) { return print((long)value); }

size_t HD44780::print(unsigned int value) {
  if (!lock()) {
    return 0u;
  }
  const size_t written = printUnsignedUnlocked((unsigned long)value);
  unlock();
  return written;
}

size_t HD44780::print(long value) {
  if (!lock()) {
    return 0u;
  }

  size_t written = 0u;
  if (value < 0) {
    written += writeByteUnlocked((uint8_t)'-');
    const unsigned long magnitude = (unsigned long)(-(value + 1L)) + 1UL;
    written += printUnsignedUnlocked(magnitude);
  } else {
    written += printUnsignedUnlocked((unsigned long)value);
  }

  unlock();
  return written;
}

size_t HD44780::print(unsigned long value) {
  if (!lock()) {
    return 0u;
  }
  const size_t written = printUnsignedUnlocked(value);
  unlock();
  return written;
}

size_t HD44780::println() {
  if (!lock()) {
    return 0u;
  }
  const size_t written = printlnUnlocked();
  unlock();
  return written;
}

size_t HD44780::printlnUnlocked() {
  // A character LCD has no notion of CR/LF; writing those control bytes would
  // render garbage glyphs into DDRAM. Instead, move the cursor to column 0 of
  // the next row, wrapping back to the first row past the bottom line.
  uint8_t next_row = (uint8_t)(_currow + 1u);
  if (next_row >= _numlines) {
    next_row = 0u;
  }
  setCursorUnlocked(0u, next_row);
  return 0u;
}

size_t HD44780::println(const char *str) {
  if (!lock()) {
    return 0u;
  }
  size_t written = writeStringUnlocked(str);
  written += printlnUnlocked();
  unlock();
  return written;
}

size_t HD44780::println(char c) {
  if (!lock()) {
    return 0u;
  }
  size_t written = writeByteUnlocked((uint8_t)c);
  written += printlnUnlocked();
  unlock();
  return written;
}

size_t HD44780::println(int value) {
  if (!lock()) {
    return 0u;
  }

  size_t written = 0u;
  long long_value = (long)value;
  if (long_value < 0) {
    written += writeByteUnlocked((uint8_t)'-');
    const unsigned long magnitude = (unsigned long)(-(long_value + 1L)) + 1UL;
    written += printUnsignedUnlocked(magnitude);
  } else {
    written += printUnsignedUnlocked((unsigned long)long_value);
  }
  written += printlnUnlocked();

  unlock();
  return written;
}

size_t HD44780::println(unsigned int value) {
  if (!lock()) {
    return 0u;
  }
  size_t written = printUnsignedUnlocked((unsigned long)value);
  written += printlnUnlocked();
  unlock();
  return written;
}

size_t HD44780::println(long value) {
  if (!lock()) {
    return 0u;
  }

  size_t written = 0u;
  if (value < 0) {
    written += writeByteUnlocked((uint8_t)'-');
    const unsigned long magnitude = (unsigned long)(-(value + 1L)) + 1UL;
    written += printUnsignedUnlocked(magnitude);
  } else {
    written += printUnsignedUnlocked((unsigned long)value);
  }
  written += printlnUnlocked();

  unlock();
  return written;
}

size_t HD44780::println(unsigned long value) {
  if (!lock()) {
    return 0u;
  }
  size_t written = printUnsignedUnlocked(value);
  written += printlnUnlocked();
  unlock();
  return written;
}

size_t HD44780::printUnsignedUnlocked(unsigned long value) {
  char buf[3u * sizeof(unsigned long) + 1u];
  size_t i = sizeof(buf);
  buf[--i] = '\0';
  do {
    buf[--i] = (char)('0' + (value % 10u));
    value /= 10u;
  } while (value != 0u);

  return writeStringUnlocked(&buf[i]);
}

/************ low level data pushing commands **********/

void HD44780::send(uint8_t value, bool mode) {
  hal_gpio_write(_rs_pin, mode);

  if (_rw_pin != HD44780_NO_PIN) {
    hal_gpio_write(_rw_pin, false);
  }

  if (_displayfunction & LCD_8BITMODE) {
    write8bits(value);
  } else {
    write4bits((uint8_t)(value >> 4));
    write4bits(value);
  }
}

void HD44780::pulseEnable() {
  hal_gpio_write(_enable_pin, false);
  hal_delay_us(1u);
  hal_gpio_write(_enable_pin, true);
  hal_delay_us(1u);
  hal_gpio_write(_enable_pin, false);
  hal_delay_us(100u);
}

void HD44780::write4bits(uint8_t value) {
  for (int i = 0; i < 4; i++) {
    hal_gpio_write(_data_pins[i], ((value >> i) & 0x01u) != 0u);
  }

  pulseEnable();
}

void HD44780::write8bits(uint8_t value) {
  for (int i = 0; i < 8; i++) {
    hal_gpio_write(_data_pins[i], ((value >> i) & 0x01u) != 0u);
  }

  pulseEnable();
}

#endif /* HAL_ENABLE_HD44780 */
#endif /* HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 ||                   \
          HAL_TARGET_IS_MOCK */
