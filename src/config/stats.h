#pragma once
#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct Stats {
  int totalShocks = 0;
  int totalVibrations = 0;
  double totalShockDurationMs = 0.0;
  double totalVibrationDurationMs = 0.0;
  int totalCooldownHits = 0;
  double totalIntensitySum = 0.0;
  int highestIntensity = 0;
  double longestShockMs = 0.0;

  std::map<std::string, int> dailyShocks;
  std::map<std::string, int> dailyMaxIntensity;

  int sessionShocks = 0;
  int sessionVibrations = 0;
  double sessionShockDurationMs = 0.0;
  int sessionCooldownHits = 0;

  static std::string today();

  void recordShock(int durationMs, int intensity);
  void recordVibration(int durationMs);
  void recordCooldownHit();

  double averageIntensity() const;
  std::pair<std::string, int> mostShockedDay() const;
  int todayCount() const;
  int todayMaxIntensity() const;

  // Returns the last N days oldest to newest, filling gaps with 0
  std::vector<std::pair<std::string, int>> lastNDays(int n) const;

  void save(const std::string& path);
  void load(const std::string& path);
  void reset();
};

inline Stats gStats;