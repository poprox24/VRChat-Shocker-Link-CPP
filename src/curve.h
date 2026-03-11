#pragma once
#include <algorithm>
#include <array>
#include <cstdlib>
#include <vector>

struct CurvePoint {
  double x, y;
};

inline bool operator==(const CurvePoint& a, const CurvePoint& b) {
  return a.x == b.x && a.y == b.y;
}

inline std::vector<CurvePoint> bezierInterpolate(CurvePoint p0, CurvePoint p1,
                                                 CurvePoint p2,
                                                 int steps = 100) {
  std::vector<CurvePoint> out(steps);
  for (int i = 0; i < steps; i++) {
    double t = (double)i / (steps - 1);
    double mt = 1.0 - t;
    out[i] = {mt * mt * p0.x + 2 * mt * t * p1.x + t * t * p2.x,
              mt * mt * p0.y + 2 * mt * t * p1.y + t * t * p2.y};
  }
  return out;
}

inline int sampleIntensity(std::array<CurvePoint, 3> pts) {
  std::sort(pts.begin(), pts.end(),
            [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });

  auto curve = bezierInterpolate(pts[0], pts[1], pts[2]);

  std::vector<double> xs, ys;
  for (auto& p : curve) {
    if (p.y <= 0.0) continue;
    xs.push_back(std::clamp(p.x, 1.0, 100.0));
    ys.push_back(p.y);
  }

  if (xs.empty()) return 50;

  double totalWeight = 0.0;
  for (double w : ys) totalWeight += w;
  double r = ((double)rand() / RAND_MAX) * totalWeight;
  double acc = 0.0;
  for (size_t i = 0; i < xs.size(); i++) {
    acc += ys[i];
    if (r <= acc) return (int)xs[i];
  }
  return (int)xs.back();
}

inline int sampleIntensityUpperHalf(std::array<CurvePoint, 3> pts) {
  std::sort(pts.begin(), pts.end(),
            [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });

  auto curve = bezierInterpolate(pts[0], pts[1], pts[2]);

  std::vector<double> xs, ys;
  for (auto& p : curve) {
    if (p.y <= 0.0) continue;
    xs.push_back(std::clamp(p.x, 1.0, 100.0));
    ys.push_back(p.y);
  }

  if (xs.empty()) return 75;

  // Upper half by sorted-index
  size_t start = xs.size() / 2;

  double totalWeight = 0.0;
  for (size_t i = start; i < ys.size(); i++) totalWeight += ys[i];
  double r = ((double)rand() / RAND_MAX) * totalWeight;
  double acc = 0.0;
  for (size_t i = start; i < xs.size(); i++) {
    acc += ys[i];
    if (r <= acc) return (int)xs[i];
  }
  return (int)xs.back();
}