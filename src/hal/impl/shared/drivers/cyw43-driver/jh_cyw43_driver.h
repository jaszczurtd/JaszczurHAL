#ifndef JASZCZURHAL_IMPL_SHARED_FRAMEWORKS_CYW43_DRIVER_H
#define JASZCZURHAL_IMPL_SHARED_FRAMEWORKS_CYW43_DRIVER_H

/*
 * Stable include boundary for the pinned CYW43 radio driver. Platform code
 * must include this wrapper instead of resolving cyw43.h from its carrier.
 */
extern "C" {
#include "vendor/src/cyw43.h"
}

#endif
