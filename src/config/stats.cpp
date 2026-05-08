#include "stats.h"

std::string Stats::today() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char buf[12];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1,
           tm.tm_mday);
  return {buf};
}

int Stats::todayMaxIntensity() const {
  auto it = dailyMaxIntensity.find(today());
  return it != dailyMaxIntensity.end() ? it->second : 0;
}

void Stats::recordShock(int durationMs, int intensity) {
  totalShocks++;
  sessionShocks++;
  totalShockDurationMs += durationMs;
  sessionShockDurationMs += durationMs;
  totalIntensitySum += intensity;
  if (intensity > highestIntensity) highestIntensity = intensity;
  if (durationMs > longestShockMs) longestShockMs = durationMs;
  dailyShocks[today()]++;
  auto& todayMax = dailyMaxIntensity[today()];
  if (intensity > todayMax) todayMax = intensity;
}

void Stats::recordVibration(int durationMs) {
  totalVibrations++;
  sessionVibrations++;
  totalVibrationDurationMs += durationMs;
}

void Stats::recordCooldownHit() {
  totalCooldownHits++;
  sessionCooldownHits++;
}

double Stats::averageIntensity() const {
  return totalShocks > 0 ? totalIntensitySum / totalShocks : 0.0;
}

std::pair<std::string, int> Stats::mostShockedDay() const {
  if (dailyShocks.empty()) return {"N/A", 0};
  auto it = std::max_element(
      dailyShocks.begin(), dailyShocks.end(),
      [](const auto& a, const auto& b) { return a.second < b.second; });
  return {it->first, it->second};
}

int Stats::todayCount() const {
  auto it = dailyShocks.find(today());
  return it != dailyShocks.end() ? it->second : 0;
}

// Returns the last N days oldest to newest, filling gaps with 0
std::vector<std::pair<std::string, int>> Stats::lastNDays(int n) const {
  std::vector<std::pair<std::string, int>> result;
  auto now = std::chrono::system_clock::now();
  for (int i = n - 1; i >= 0; i--) {
    auto day = now - std::chrono::hours(24 * i);
    auto t = std::chrono::system_clock::to_time_t(day);
    std::tm tm{};
#if defined(_WIN32)
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

void Stats::save(const std::string& path) {
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
    nlohmann::json dm = nlohmann::json::object();
    for (auto& [k, v] : dailyMaxIntensity) dm[k] = v;
    j["dailyMaxIntensity"] = dm;
    std::ofstream f(path);
    f << j.dump(2);
  } catch (...) {
  }
}

void Stats::load(const std::string& path) {
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
    if (j.contains("dailyShocks"))
      for (auto& [k, v] : j["dailyShocks"].items())
        dailyShocks[k] = v.get<int>();
    if (j.contains("dailyMaxIntensity"))
      for (auto& [k, v] : j["dailyMaxIntensity"].items())
        dailyMaxIntensity[k] = v.get<int>();
  } catch (...) {
  }
}

void Stats::reset() { *this = Stats{}; }