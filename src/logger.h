#pragma once

#include <fmt/base.h>
#include <fmt/chrono.h>
#include <fmt/format.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>

inline void (*g_wakeUiFunc)() = nullptr;

inline void safeWakeUi() {
  if (g_wakeUiFunc) {
    static std::atomic<bool> waking{false};
    if (waking.exchange(true)) return;

    g_wakeUiFunc();
    waking = false;
  }
}

struct Logger {
  static constexpr int MAX_LINES = 100;
  std::deque<std::string> lines;
  std::mutex mtx;
  std::ofstream logFile;

  Logger();
  void add(std::string msg);
};

inline Logger gLog;

template <typename... Args>
void logMsg(fmt::format_string<Args...> fmt_str, Args&&... args) {
  gLog.add(fmt::format(fmt_str, std::forward<Args>(args)...));
}