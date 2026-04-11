#include "core/curves/bezier_curve.h"

#include <algorithm>
#include <vector>

bool operator==(const CurvePoint& a, const CurvePoint& b) {
  return a.x == b.x && a.y == b.y;
}

std::vector<CurvePoint> bezierInterpolate(CurvePoint p0, CurvePoint p1,
                                          CurvePoint p2, int steps) {
  std::vector<CurvePoint> out(steps);
  for (int i = 0; i < steps; i++) {
    double t = static_cast<double>(i) / (steps - 1);
    double mt = 1.0 - t;
    out[i] = {mt * mt * p0.x + 2 * mt * t * p1.x + t * t * p2.x,
              mt * mt * p0.y + 2 * mt * t * p1.y + t * t * p2.y};
  }
  return out;
}

int sampleIntensity(std::array<CurvePoint, 3>& pts, std::mt19937& rng) {
  auto sorted = pts;
  std::sort(sorted.begin(), sorted.end(),
            [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });

  auto curve = bezierInterpolate(sorted[0], sorted[1], sorted[2]);

  std::vector<double> xs, ys;
  for (auto& p : curve) {
    if (p.y <= 0.0) continue;
    xs.push_back(std::clamp(p.x, 1.0, 100.0));
    ys.push_back(p.y);
  }

  if (xs.empty()) return 50;

  double totalWeight = 0.0;
  for (double w : ys) totalWeight += w;
  std::uniform_real_distribution<double> dist(0.0, totalWeight);
  double r = dist(rng);
  double acc = 0.0;
  for (size_t i = 0; i < xs.size(); i++) {
    acc += ys[i];
    if (r <= acc) return static_cast<int>(xs[i]);
  }
  return static_cast<int>(xs.back());
}

int sampleIntensityLowerHalf(std::array<CurvePoint, 3>& pts,
                             std::mt19937& rng) {
  auto sorted = pts;
  std::sort(sorted.begin(), sorted.end(),
            [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });

  auto curve = bezierInterpolate(sorted[0], sorted[1], sorted[2]);

  std::vector<double> xs, ys;
  for (auto& p : curve) {
    if (p.y <= 0.0) continue;
    xs.push_back(std::clamp(p.x, 1.0, 100.0));
    ys.push_back(p.y);
  }

  if (xs.empty()) return 25;

  size_t end = xs.size();
  while (end > 0 && xs[end - 1] >= 50.0) end--;

  if (end == 0) return 25;

  double totalWeight = 0.0;
  for (size_t i = 0; i < end; i++) totalWeight += ys[i];
  std::uniform_real_distribution<double> dist(0.0, totalWeight);
  double r = dist(rng);
  double acc = 0.0;
  for (size_t i = 0; i < end; i++) {
    acc += ys[i];
    if (r <= acc) return static_cast<int>(xs[i]);
  }
  return static_cast<int>(xs.front());
}

int sampleIntensityUpperHalf(std::array<CurvePoint, 3>& pts,
                             std::mt19937& rng) {
  auto sorted = pts;
  std::sort(sorted.begin(), sorted.end(),
            [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });

  auto curve = bezierInterpolate(sorted[0], sorted[1], sorted[2]);

  std::vector<double> xs, ys;
  for (auto& p : curve) {
    if (p.y <= 0.0) continue;
    xs.push_back(std::clamp(p.x, 1.0, 100.0));
    ys.push_back(p.y);
  }

  if (xs.empty()) return 75;

  size_t start = 0;
  while (start < xs.size() && xs[start] < 50.0) start++;

  if (start >= xs.size()) return 75;

  double totalWeight = 0.0;
  for (size_t i = start; i < ys.size(); i++) totalWeight += ys[i];
  std::uniform_real_distribution<double> dist(0.0, totalWeight);
  double r = dist(rng);
  double acc = 0.0;
  for (size_t i = start; i < xs.size(); i++) {
    acc += ys[i];
    if (r <= acc) return static_cast<int>(xs[i]);
  }
  return static_cast<int>(xs.back());
}
