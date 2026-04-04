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

  int sessionShocks = 0;
  int sessionVibrations = 0;
  double sessionShockDurationMs = 0.0;
  int sessionCooldownHits = 0;

  static std::string today() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[12];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900,
             tm.tm_mon + 1, tm.tm_mday);
    return {buf};
  }

  void recordShock(int durationMs, int intensity) {
    totalShocks++;
    sessionShocks++;
    totalShockDurationMs += durationMs;
    sessionShockDurationMs += durationMs;
    totalIntensitySum += intensity;
    if (intensity > highestIntensity) highestIntensity = intensity;
    if (durationMs > longestShockMs) longestShockMs = durationMs;
    dailyShocks[today()]++;
  }

  void recordVibration(int durationMs) {
    totalVibrations++;
    sessionVibrations++;
    totalVibrationDurationMs += durationMs;
  }

  void recordCooldownHit() {
    totalCooldownHits++;
    sessionCooldownHits++;
  }

  double averageIntensity() const {
    return totalShocks > 0 ? totalIntensitySum / totalShocks : 0.0;
  }

  std::pair<std::string, int> mostShockedDay() const {
    if (dailyShocks.empty()) return {"N/A", 0};
    auto it = std::max_element(
        dailyShocks.begin(), dailyShocks.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });
    return {it->first, it->second};
  }

  int todayCount() const {
    auto it = dailyShocks.find(today());
    return it != dailyShocks.end() ? it->second : 0;
  }

  // Returns the last N days oldest to newest, filling gaps with 0
  std::vector<std::pair<std::string, int>> lastNDays(int n) const {
    std::vector<std::pair<std::string, int>> result;
    auto now = std::chrono::system_clock::now();
    for (int i = n - 1; i >= 0; i--) {
      auto day = now - std::chrono::hours(24 * i);
      auto t = std::chrono::system_clock::to_time_t(day);
      std::tm tm{};
#ifdef _WIN32
      localtime_s(&tm, &t);
#else
      localtime_r(&t, &tm);
#endif
      char buf[12];
      snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900,
               tm.tm_mon + 1, tm.tm_mday);
      std::string ds(buf);
      auto it = dailyShocks.find(ds);
      result.push_back({ds, it != dailyShocks.end() ? it->second : 0});
    }
    return result;
  }

  void save(const std::string& path) {
    try {
      nlohmann::json j;
      j["totalShocks"] = totalShocks;
      j["totalVibrations"] = totalVibrations;
      j["totalShockDurationMs"] = totalShockDurationMs;
      j["totalVibrationDurationMs"] = totalVibrationDurationMs;
      j["totalCooldownHits"] = totalCooldownHits;
      j["totalIntensitySum"] = totalIntensitySum;
      j["highestIntensity"] = highestIntensity;
      j["longestShockMs"] = longestShockMs;
      nlohmann::json ds = nlohmann::json::object();
      for (auto& [k, v] : dailyShocks) ds[k] = v;
      j["dailyShocks"] = ds;
      std::ofstream f(path);
      f << j.dump(2);
    } catch (...) {
    }
  }

  void load(const std::string& path) {
    try {
      std::ifstream f(path);
      if (!f.is_open()) return;
      auto j = nlohmann::json::parse(f);
      totalShocks = j.value("totalShocks", 0);
      totalVibrations = j.value("totalVibrations", 0);
      totalShockDurationMs = j.value("totalShockDurationMs", 0.0);
      totalVibrationDurationMs = j.value("totalVibrationDurationMs", 0.0);
      totalCooldownHits = j.value("totalCooldownHits", 0);
      totalIntensitySum = j.value("totalIntensitySum", 0.0);
      highestIntensity = j.value("highestIntensity", 0);
      longestShockMs = j.value("longestShockMs", 0.0);
      if (j.contains("dailyShocks"))
        for (auto& [k, v] : j["dailyShocks"].items())
          dailyShocks[k] = v.get<int>();
    } catch (...) {
    }
  }

  void reset() { *this = Stats{}; }
};

inline Stats gStats;