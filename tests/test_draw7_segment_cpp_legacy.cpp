#include "utils/draw7Segment.h"

int main() {
  return get7SegStringWidth("00000000000000000000", 16, 2.2f) == 385 ? 0 : 1;
}
