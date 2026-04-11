#pragma once

#include <array>
#include <string>
#include <vector>

#include "curve.h"

struct Preset {
  std::string name;
  float minShockDuration = 1.f;
  float maxShockDuration = 2.f;
  std::array<CurvePoint, 3> curvePoints = {
      {{20.0, 0.8}, {50.0, 0.5}, {80.0, 0.2}}};
  float xViewMin = 0.f;
  float xViewMax = 100.f;
};

struct SavedPreset {
  std::string name;
  std::vector<Preset> curves;
  int activeCurveIndex = 0;
};
