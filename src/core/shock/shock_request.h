#pragma once

#include <optional>

#include "config/parameter.h"

struct ShockRequest {
  std::optional<int> duration;
  int parameterIndex = -1;
  CurveRange range = CurveRange::Full;
  bool vibrate = false;
};
