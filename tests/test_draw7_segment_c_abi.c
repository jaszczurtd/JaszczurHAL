#include "utils/draw7Segment.h"

int main(void) {
  if (get7SegStringWidth("12", 20, 2.0f) != 33) {
    return 1;
  }
  if (get7SegStringWidth("00000000000000000000", 16, 2.2f) != 385) {
    return 2;
  }
  draw7SegString("", 0, 0, 20, 30, 2.0f, 0u);
  return 0;
}
