#ifdef _WIN32
#pragma comment(linker, "/ENTRY:mainCRTStartup")
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on
#else
#include <unistd.h>
#endif

#include <atomic>
#include <csignal>
#include <future>

#include "logger.h"
#include "oscqclient.h"
#include "settings.h"
#include "shockerhub.h"
#include "stats.h"
#include "ui.h"
#include "updater.h"

std::string settingsLocation = "settings.json";
std::atomic<bool> running = true;
std::atomic<bool> shouldRestart = false;

void signalHandler(int) { running = false; }

int main() {
  // STARUP
  Settings settings(settingsLocation);
  gStats.load("stats.json");

  ShockerHub hub(settings);

  std::signal(SIGINT, signalHandler);

  std::future<void> shockFuture;
  if (!settings.useSerial) {
    if (settings.usePishock) {
      shockFuture =
          std::async(std::launch::async, [&hub] { hub.resolvePiShockApi(); });
    } else if (settings.shockerIDs.empty()) {
      shockFuture =
          std::async(std::launch::async, [&hub] { hub.resolveOpenShockApi(); });
    }
  }
  hub.connectSerial();
  if (!hub.listShockers()) {
    hub.shutdown();
    return 1;
  }

  OscQueryServer oscQuery(Settings::oscPort, std::string(Settings::serviceName),
                          settings.vrchatHost);
  oscQuery.setParameterPaths(settings.getParameterPaths());
  if (!oscQuery.start()) {
    logMsg("Failed to start OSCQuery\n");
    hub.shutdown();
    return 1;
  }

  // Connects OSCQuery server and ShorkerHub
  std::thread oscBridge([&]() {
    while (running) {
      std::unique_lock<std::mutex> lock(oscQuery.shockMutex);
      oscQuery.shockCV.wait_for(lock, std::chrono::milliseconds(100), [&] {
        return !oscQuery.pendingParameterIndexes.empty() || !running;
      });
      if (!oscQuery.pendingParameterIndexes.empty()) {
        int index = oscQuery.pendingParameterIndexes.front();
        oscQuery.pendingParameterIndexes.pop_front();
        lock.unlock();
        hub.queueShockFor(index);
      }
    }
  });

  Updater::checkAsync();
  runUI(settings, hub, settingsLocation);

  // SHUTDOWN
  running = false;

  oscQuery.shockCV.notify_all();
  hub.queueCV.notify_all();

  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  if (oscBridge.joinable()) oscBridge.join();

  oscQuery.stop();
  hub.shutdown();

  settings.save(settingsLocation);
  gStats.save("stats.json");

  if (shouldRestart) {
#ifdef _WIN32
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    ShellExecuteA(
        nullptr, "open", "cmd.exe",
        ("/c timeout /t 1 /nobreak && \"" + std::string(exePath) + "\"")
            .c_str(),
        nullptr, SW_HIDE);
#else
    char exePath[4096] = {};
    ssize_t n = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (n > 0) {
      exePath[n] = '\0';
      if (fork() == 0) {
        sleep(1);
        execl(exePath, exePath, nullptr);
        _exit(1);
      }
    }
    _exit(0);
#endif
  }
  return 0;
}