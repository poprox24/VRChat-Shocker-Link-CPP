#pragma once

#include <fmt/base.h>
#include <fmt/format.h>

#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>

extern void (*g_wakeUiFunc)();

void safeWakeUi();

struct Logger {
  static constexpr int MAX_LINES = 100;
  std::deque<std::string> lines;
  std::mutex mtx;
  std::ofstream logFile;

  Logger();
  void add(std::string msg);
};

extern Logger gLog;

// Keep call sites ergonomic while avoiding inline function bodies in headers.
#define logMsg(...)                                \
  do {                                             \
    ::gLog.add(fmt::format(__VA_ARGS__));          \
  } while (false)
