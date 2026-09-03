#ifndef JH_HAL_PCNT_COMMON_H
#define JH_HAL_PCNT_COMMON_H

#include "hal/analog/hal_pcnt.h"

static inline bool jh_hal_pcnt_edge_valid(hal_pcnt_edge_t edge) {
  return edge == HAL_PCNT_EDGE_RISING || edge == HAL_PCNT_EDGE_FALLING ||
         edge == HAL_PCNT_EDGE_BOTH;
}

#endif
