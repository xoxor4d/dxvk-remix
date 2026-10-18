// Fork-owned. Bindings of the rain map pass (rtx_fork_rain_map.cpp): a top-down height field of the
// static geometry around the camera, read by the per draw wetness model through the common binding
// BINDING_FORK_RAIN_MAP.
#pragma once

#include "rtx/pass/common_binding_indices.h"

#define RAIN_MAP_BINDING_HEIGHT_OUTPUT 130
#define RAIN_MAP_BINDING_HEIGHT_INPUT  131
#define RAIN_MAP_BINDING_MAP_OUTPUT    132

#define RAIN_MAP_MIN_BINDING RAIN_MAP_BINDING_HEIGHT_OUTPUT

#if RAIN_MAP_MIN_BINDING <= COMMON_MAX_BINDING
#error "Increase the base index of the rain map bindings to avoid overlap with common bindings!"
#endif
