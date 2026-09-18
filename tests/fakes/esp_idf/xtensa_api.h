#pragma once

#include <stdint.h>

#define XCHAL_EXCCAUSE_NUM 64

typedef struct {
  uint32_t pc;
  uint32_t ps;
  uint32_t a0;
  uint32_t exccause;
  uint32_t excvaddr;
} XtExcFrame;

typedef void (*xt_exc_handler)(XtExcFrame *frame);

xt_exc_handler xt_set_exception_handler(int n, xt_exc_handler f);
