#pragma comment(linker, "/ENTRY:mainCRTStartup")

#include <atomic>
#include <csignal>

#include "logger.h"
#include "migration.h"
#include "oscclient.h"
#include "settings.h"
#include "shockerhub.h"
#include "ui.h"
#include "updater.h"

std::string settingsLocation = "settings.json";
std::atomic<bool> running = true;
std::atomic<bool> shouldRestart = false;

void signalHandler(int) { running = false; }

int main() {
  Settings settings(settingsLocation);

  migrateConfigYmlIfPresent(settings, settingsLocation);

  ShockerHub hub(settings);

  std::signal(SIGINT, signalHandler);

  hub.connectSerial();
  if (!hub.listShockers()) {
    hub.shutdown();
    return 1;
  }

  OscQueryServer oscQuery(Settings::oscPort,
                          std::string(Settings::serviceName));
  oscQuery.setShockPath("/avatar/parameters/" + settings.shockParameter);
  if (!settings.secondShockParameter.empty())
    oscQuery.setSecondShockPath("/avatar/parameters/" +
                                settings.secondShockParameter);
  if (!oscQuery.start()) {
    logMsg("Failed to start OSCQuery\n");
    hub.shutdown();
    return 1;
  }

  std::thread oscBridge([&]() {
    while (running) {
      std::unique_lock<std::mutex> lock(oscQuery.shockMutex);
      oscQuery.shockCV.wait_for(lock, std::chrono::milliseconds(100), [&] {
        return oscQuery.shockPending || oscQuery.secondShockPending || !running;
      });
      if (oscQuery.shockPending) {
        oscQuery.shockPending = false;
        lock.unlock();
        hub.queueShock();
      } else if (oscQuery.secondShockPending) {
        oscQuery.secondShockPending = false;
        lock.unlock();
        hub.queueShockUpperHalf();
      }
    }
  });

  Updater::checkAsync();
  ui_run(settings, hub, settingsLocation);
  running = false;
  oscBridge.join();
  oscQuery.stop();
  hub.shutdown();
  settings.save(settingsLocation);

  if (shouldRestart) {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    ShellExecuteA(
        nullptr, "open", "cmd.exe",
        ("/c timeout /t 1 /nobreak && \"" + std::string(exePath) + "\"")
            .c_str(),
        nullptr, SW_HIDE);
  }
  return 0;
}