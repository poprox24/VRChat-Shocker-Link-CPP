#pragma once

#include <fmt/base.h>
#include <fmt/chrono.h>
#include <fmt/format.h>

#include <chrono>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>

struct Logger {
  static constexpr int MAX_LINES = 100;
  std::deque<std::string> lines;
  std::mutex mtx;
  std::ofstream logFile;

  Logger() { logFile.open("latest.log", std::ios::out | std::ios::trunc); }

  void add(std::string msg) {
    if (!msg.empty() && msg.back() == '\n') msg.pop_back();
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &time);
    std::string stamped = fmt::format("[{:02d}:{:02d}:{:02d}] {}", tm.tm_hour,
                                      tm.tm_min, tm.tm_sec, msg);
    std::lock_guard<std::mutex> lock(mtx);
    lines.push_back(stamped);
    if ((int)lines.size() > MAX_LINES) lines.pop_front();
    if (logFile.is_open()) {
      logFile << stamped << '\n';
      logFile.flush();
    }
  }
};

inline Logger gLog;

template <typename... Args>
void logMsg(fmt::format_string<Args...> fmt_str, Args&&... args) {
  gLog.add(fmt::format(fmt_str, std::forward<Args>(args)...));
}