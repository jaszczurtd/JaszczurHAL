#pragma once

/*
 * HD44780-compatible character LCD driver for JaszczurHAL.
 *
 * This implementation is based on the Arduino LiquidCrystal library authored
 * by Hans-Christoph Steiner and maintained by Arduino/Adafruit. The command
 * sequence, row-offset defaults, 4-bit/8-bit transfer order and timing values
 * are preserved, while GPIO, delays and synchronization are routed through
 * JaszczurHAL.
 *
 * Original notices:
 *   Copyright (C) 2006-2008 Hans-Christoph Steiner. All rights reserved.
 *   Copyright (c) 2010 Arduino LLC. All rights reserved.
 *
 * The original library is distributed under the GNU Lesser General Public
 * License, version 2.1 or later.
 */

#include "../../../hal_config.h"

#if (HAL_TARGET_IS_RP2040 || HAL_TARGET_IS_STM32G474 || HAL_TARGET_IS_MOCK) && \
    defined(HAL_ENABLE_HD44780)

#include "../../../hal_sync.h"

#include <stddef.h>
#include <stdint.h>

// commands
#ifndef LCD_CLEARDISPLAY
#define LCD_CLEARDISPLAY 0x01
#endif
#ifndef LCD_RETURNHOME
#define LCD_RETURNHOME 0x02
#endif
#ifndef LCD_ENTRYMODESET
#define LCD_ENTRYMODESET 0x04
#endif
#ifndef LCD_DISPLAYCONTROL
#define LCD_DISPLAYCONTROL 0x08
#endif
#ifndef LCD_CURSORSHIFT
#define LCD_CURSORSHIFT 0x10
#endif
#ifndef LCD_FUNCTIONSET
#define LCD_FUNCTIONSET 0x20
#endif
#ifndef LCD_SETCGRAMADDR
#define LCD_SETCGRAMADDR 0x40
#endif
#ifndef LCD_SETDDRAMADDR
#define LCD_SETDDRAMADDR 0x80
#endif

// flags for display entry mode
#ifndef LCD_ENTRYRIGHT
#define LCD_ENTRYRIGHT 0x00
#endif
#ifndef LCD_ENTRYLEFT
#define LCD_ENTRYLEFT 0x02
#endif
#ifndef LCD_ENTRYSHIFTINCREMENT
#define LCD_ENTRYSHIFTINCREMENT 0x01
#endif
#ifndef LCD_ENTRYSHIFTDECREMENT
#define LCD_ENTRYSHIFTDECREMENT 0x00
#endif

// flags for display on/off control
#ifndef LCD_DISPLAYON
#define LCD_DISPLAYON 0x04
#endif
#ifndef LCD_DISPLAYOFF
#define LCD_DISPLAYOFF 0x00
#endif
#ifndef LCD_CURSORON
#define LCD_CURSORON 0x02
#endif
#ifndef LCD_CURSOROFF
#define LCD_CURSOROFF 0x00
#endif
#ifndef LCD_BLINKON
#define LCD_BLINKON 0x01
#endif
#ifndef LCD_BLINKOFF
#define LCD_BLINKOFF 0x00
#endif

// flags for display/cursor shift
#ifndef LCD_DISPLAYMOVE
#define LCD_DISPLAYMOVE 0x08
#endif
#ifndef LCD_CURSORMOVE
#define LCD_CURSORMOVE 0x00
#endif
#ifndef LCD_MOVERIGHT
#define LCD_MOVERIGHT 0x04
#endif
#ifndef LCD_MOVELEFT
#define LCD_MOVELEFT 0x00
#endif

// flags for function set
#ifndef LCD_8BITMODE
#define LCD_8BITMODE 0x10
#endif
#ifndef LCD_4BITMODE
#define LCD_4BITMODE 0x00
#endif
#ifndef LCD_2LINE
#define LCD_2LINE 0x08
#endif
#ifndef LCD_1LINE
#define LCD_1LINE 0x00
#endif
#ifndef LCD_5x10DOTS
#define LCD_5x10DOTS 0x04
#endif
#ifndef LCD_5x8DOTS
#define LCD_5x8DOTS 0x00
#endif

#ifndef HD44780_NO_PIN
#define HD44780_NO_PIN 255u
#endif

#ifdef __cplusplus

class HD44780 {
public:
  HD44780(uint8_t rs, uint8_t enable, uint8_t d0, uint8_t d1, uint8_t d2,
          uint8_t d3, uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7);
  HD44780(uint8_t rs, uint8_t rw, uint8_t enable, uint8_t d0, uint8_t d1,
          uint8_t d2, uint8_t d3, uint8_t d4, uint8_t d5, uint8_t d6,
          uint8_t d7);
  HD44780(uint8_t rs, uint8_t rw, uint8_t enable, uint8_t d0, uint8_t d1,
          uint8_t d2, uint8_t d3);
  HD44780(uint8_t rs, uint8_t enable, uint8_t d0, uint8_t d1, uint8_t d2,
          uint8_t d3);
  ~HD44780();

  HD44780(const HD44780 &) = delete;
  HD44780 &operator=(const HD44780 &) = delete;

  void init(uint8_t fourbitmode, uint8_t rs, uint8_t rw, uint8_t enable,
            uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3, uint8_t d4,
            uint8_t d5, uint8_t d6, uint8_t d7);

  void begin(uint8_t cols, uint8_t rows, uint8_t charsize = LCD_5x8DOTS);

  void clear();
  void home();

  void noDisplay();
  void display();
  void noBlink();
  void blink();
  void noCursor();
  void cursor();
  void scrollDisplayLeft();
  void scrollDisplayRight();
  void leftToRight();
  void rightToLeft();
  void autoscroll();
  void noAutoscroll();

  void setRowOffsets(int row0, int row1, int row2, int row3);
  void createChar(uint8_t location, uint8_t charmap[]);
  void createChar(uint8_t location, const uint8_t charmap[]);
  void setCursor(uint8_t col, uint8_t row);
  size_t write(uint8_t value);
  size_t write(const uint8_t *buffer, size_t size);
  size_t write(const char *str);
  void command(uint8_t value);

  size_t print(const char *str);
  size_t print(char c);
  size_t print(unsigned char value);
  size_t print(int value);
  size_t print(unsigned int value);
  size_t print(long value);
  size_t print(unsigned long value);
  // println variants emit the argument (if any) and then move the cursor to
  // column 0 of the next row, wrapping back to the first row past the bottom
  // line. Unlike a serial Print, no CR/LF bytes are written to the display.
  size_t println();
  size_t println(const char *str);
  size_t println(char c);
  size_t println(int value);
  size_t println(unsigned int value);
  size_t println(long value);
  size_t println(unsigned long value);

private:
  bool ensureMutex();
  bool lock();
  void unlock();

  void beginUnlocked(uint8_t cols, uint8_t rows, uint8_t charsize);
  void clearUnlocked();
  void homeUnlocked();
  void setRowOffsetsUnlocked(int row0, int row1, int row2, int row3);
  void setCursorUnlocked(uint8_t col, uint8_t row);
  void commandUnlocked(uint8_t value);
  size_t writeByteUnlocked(uint8_t value);
  size_t writeBufferUnlocked(const uint8_t *buffer, size_t size);
  size_t writeStringUnlocked(const char *str);
  size_t printUnsignedUnlocked(unsigned long value);
  size_t printlnUnlocked();

  void send(uint8_t value, bool mode);
  void write4bits(uint8_t value);
  void write8bits(uint8_t value);
  void pulseEnable();

  hal_mutex_t _mutex;

  uint8_t _rs_pin;     // false: command. true: character.
  uint8_t _rw_pin;     // false: write to LCD. true: read from LCD.
  uint8_t _enable_pin; // activated by a high pulse.
  uint8_t _data_pins[8];

  uint8_t _displayfunction;
  uint8_t _displaycontrol;
  uint8_t _displaymode;

  uint8_t _initialized;

  uint8_t _numlines;
  uint8_t _row_offsets[4];
  uint8_t _currow; // tracks the active row so println() can advance lines
};

#endif /* __cplusplus */

#endif /* supported target && HAL_ENABLE_HD44780 */
