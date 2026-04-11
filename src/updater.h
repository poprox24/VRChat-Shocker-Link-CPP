#pragma once

#include <atomic>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

extern std::atomic<bool> updateReady;
extern std::string pendingExePath;

namespace Updater {

bool newerThan(const std::string& remote, const std::string& local);
void checkAsync();

#ifdef _WIN32
void applyAndRestart(HWND hwnd);
#else
void applyAndRestart();
#endif

}  // namespace Updater
