#pragma once

#include "libConfig.h"

#ifdef HAL_ENABLE_UNITY

#ifdef UNITY_INCLUDE_CONFIG_H
#include "unity_config.h"
#undef UNITY_INCLUDE_CONFIG_H
#define JH_UNITY_RESTORE_CONFIG_INCLUDE
#endif

#include "../../third_party/Unity/src/unity_internals.h"

#ifdef JH_UNITY_RESTORE_CONFIG_INCLUDE
#define UNITY_INCLUDE_CONFIG_H
#undef JH_UNITY_RESTORE_CONFIG_INCLUDE
#endif

#endif
