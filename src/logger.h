#pragma once

#include <fmt/base.h>
#include <fmt/format.h>

#include <deque>
#include <mutex>
#include <string>

struct Logger {
  static constexpr int MAX_LINES = 100;
  std::deque<std::string> lines;
  std::mutex mtx;

  void add(std::string msg) {
    if (!msg.empty() && msg.back() == '\n') msg.pop_back();
    std::lock_guard<std::mutex> lock(mtx);
    lines.push_back(std::move(msg));
    if ((int)lines.size() > MAX_LINES) lines.pop_front();
  }
};

inline Logger gLog;

template <typename... Args>
void logMsg(fmt::format_string<Args...> fmt_str, Args&&... args) {
  auto msg = fmt::format(fmt_str, std::forward<Args>(args)...);
  gLog.add(msg);
}