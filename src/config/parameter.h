#pragma once

#include <string>

enum class CurveRange { Full = 0, FirstHalf = 1, SecondHalf = 2 };

struct Parameter {
  std::string name = "Shock";
  int curveIndex = 0;
  CurveRange range = CurveRange::Full;
};

bool operator==(const Parameter& a, const Parameter& b);
