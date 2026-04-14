#include "draw7Segment.h"
#ifndef HAL_DISABLE_DISPLAY

static const uint8_t SEGMENT_MAP[16][7] = {
  {1,1,1,1,1,1,0}, // 0
  {0,1,1,0,0,0,0}, // 1
  {1,1,0,1,1,0,1}, // 2
  {1,1,1,1,0,0,1}, // 3
  {0,1,1,0,0,1,1}, // 4
  {1,0,1,1,0,1,1}, // 5
  {1,0,1,1,1,1,1}, // 6
  {1,1,1,0,0,0,0}, // 7
  {1,1,1,1,1,1,1}, // 8
  {1,1,1,1,0,1,1}, // 9
  {1,1,1,0,1,1,1}, // A
  {0,0,1,1,1,1,1}, // B
  {1,0,0,1,1,1,0}, // C
  {0,1,1,1,1,0,1}, // D
  {1,0,0,1,1,1,1}, // E
  {1,0,0,0,1,1,1}  // F
};

static void drawHorizontalSegment(int x, int y, int length, float thickness, uint16_t color) {
  int r = thickness / 2;
  for (int i = 0; i < r; i++) {
    hal_display_fill_round_rect(x + i, y - r + i, length - 2 * i, 2 * (r - i), r - i, color);
  }
}

static void drawVerticalSegment(int x, int y, int length, float thickness, uint16_t color) {
  int r = thickness / 2;
  for (int i = 0; i < r; i++) {
    hal_display_fill_round_rect(x - r + i, y + i, 2 * (r - i), length - 2 * i, r - i, color);
  }
}

static void drawDigit(char ch, int x, int y, int width, int height, float thickness, uint16_t color) {
  int index;
  if (ch >= 'A' && ch <= 'F') {
    index = ch - 'A' + 10;
  } else if (ch >= '0' && ch <= '9') {
    index = ch - '0';
  } else {
    return;
  }

  const uint8_t* segments = SEGMENT_MAP[index];
  int verticalLength = (height - 3 * thickness) / 2;
  int horizontalLength = width - thickness;

  if(segments[0]) drawHorizontalSegment(x + thickness / 2, y, horizontalLength, thickness, color);
  if(segments[1]) drawVerticalSegment(x + width - thickness / 2, y + thickness / 2, verticalLength, thickness, color);
  if(segments[2]) drawVerticalSegment(x + width - thickness / 2, y + height / 2 + thickness / 2, verticalLength, thickness, color);
  if(segments[3]) drawHorizontalSegment(x + thickness / 2, y + height - thickness, horizontalLength, thickness, color);
  if(segments[4]) drawVerticalSegment(x + thickness / 2, y + height / 2 + thickness / 2, verticalLength, thickness, color);
  if(segments[5]) drawVerticalSegment(x + thickness / 2, y + thickness / 2, verticalLength, thickness, color);
  if(segments[6]) drawHorizontalSegment(x + thickness / 2, y + height / 2 - thickness / 2, horizontalLength, thickness, color);
}

static float getCharProportionalFactor(char c) {
  if (c == '1') return 0.35f;
  if (c == ' ') return 0.45f;
  if (c == '+') return 0.65f;
  if (c == '-') return 0.6f;
  if (c == '^') return 1.1f;
  return 1.0f;
}

void draw7SegString(const char* str, int x, int y, int digitWidth, int digitHeight, float thickness, uint16_t color) {
  if(!str) {
    hal_derr("Null string passed to draw7SegString\n");
    return;
  }

  int cursorX = x;
  int baseSpacing = (int)(thickness * 1.5f);
  int dotSize = (thickness / 2 > 2.0f) ? (int)(thickness / 2) : 2;

  for (unsigned i = 0; i < strlen(str); i++) {
    char c = str[i];
    int effectiveWidth = static_cast<int>(digitWidth * getCharProportionalFactor(c));

    if (c == '^') {
      int radius = thickness;
      hal_display_draw_circle(cursorX + radius * 2, y + radius * 2, radius * 2, color);
      cursorX += (dotSize * 3) + radius + baseSpacing;
    } else if (c == '%') {
      int radius = thickness;
      int lineOffset = thickness / 2;
      for (int j = -lineOffset; j <= lineOffset; ++j) {
        hal_display_draw_line(cursorX + j + lineOffset, y + digitHeight - lineOffset,
                              cursorX + digitWidth + j, y, color);
      }
      hal_display_draw_circle(cursorX + radius * 2, y + radius * 2, radius * 2, color);
      hal_display_draw_circle(cursorX + digitWidth - radius * 2, y + digitHeight - radius * 3, radius * 2, color);
      cursorX += digitWidth + baseSpacing;
    } else if (c == ':') {
      int centerY = y + digitHeight / 2;
      cursorX += dotSize;
      hal_display_fill_circle(cursorX + thickness, centerY - digitHeight / 5, dotSize, color);
      hal_display_fill_circle(cursorX + thickness, centerY + digitHeight / 5, dotSize, color);
      cursorX += thickness * 2 + baseSpacing / 2;
      cursorX += dotSize;
    } else if (c == '.') {
      cursorX += dotSize;
      hal_display_fill_circle(cursorX + thickness, y + digitHeight - (thickness * 2), dotSize, color);
      cursorX += thickness * 2 + baseSpacing / 2;
      cursorX += dotSize;
    } else if (c == '-') {
      int lineLength = effectiveWidth - thickness;
      int centerY = y + digitHeight / 2;
      drawHorizontalSegment(cursorX + thickness / 2, centerY - thickness / 2, lineLength, thickness, color);
      cursorX += effectiveWidth + baseSpacing;
    } else if (c == '+') {
      int lineLength = digitWidth - (thickness * 2);
      int centerY = y + digitHeight / 2;
      drawHorizontalSegment(cursorX - (thickness / 2), centerY - thickness / 2, lineLength, thickness, color);
      drawVerticalSegment(cursorX - (thickness / 2) + (lineLength / 2),
                          y + ((digitHeight - lineLength) / 2) - (thickness / 2),
                          lineLength, thickness, color);
      cursorX += effectiveWidth + baseSpacing;
    } else {
      drawDigit(c, cursorX, y, effectiveWidth, digitHeight, thickness, color);
      cursorX += effectiveWidth + baseSpacing;
    }
  }
}

int get7SegStringWidth(const char* str, int digitWidth, float thickness) {
  if(!str) {
    hal_derr("Null string passed to get7SegStringWidth\n");
    return 0;
  }

  float totalWidth = 0;
  float baseSpacing = thickness * 1.5f;
  int dotSize = (thickness / 2 > 2.0f) ? (int)(thickness / 2) : 2;

  for (unsigned i = 0; i < strlen(str); i++) {
    char c = str[i];
    int effectiveWidth = static_cast<int>(digitWidth * getCharProportionalFactor(c));

    if (c == '^') {
      totalWidth += (dotSize * 3) + thickness + baseSpacing;
    } else if (c == '%') {
      totalWidth += digitWidth + baseSpacing;
    } else if (c == ':') {
      totalWidth += (dotSize * 2) + (thickness * 2 + baseSpacing / 2);
    } else if (c == '.') {
      totalWidth += (dotSize * 2) + (thickness * 2 + baseSpacing / 2);
    } else {
      totalWidth += effectiveWidth + baseSpacing;
    }
  }
  return static_cast<int>(totalWidth);
}

#endif /* HAL_DISABLE_DISPLAY */
