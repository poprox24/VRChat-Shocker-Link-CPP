#pragma once

#include <array>
#include <random>
#include <vector>

struct CurvePoint {
  double x;
  double y;
};

bool operator==(const CurvePoint& a, const CurvePoint& b);

std::vector<CurvePoint> bezierInterpolate(CurvePoint p0, CurvePoint p1,
                                          CurvePoint p2, int steps = 100);

int sampleIntensity(std::array<CurvePoint, 3>& pts, std::mt19937& rng);

int sampleIntensityLowerHalf(std::array<CurvePoint, 3>& pts, std::mt19937& rng);

int sampleIntensityUpperHalf(std::array<CurvePoint, 3>& pts, std::mt19937& rng);
