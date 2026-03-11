#pragma comment(linker, "/ENTRY:mainCRTStartup")

#include <atomic>
#include <csignal>

#include "config.h"
#include "logger.h"
#include "oscclient.h"
#include "settings.h"
#include "shockerhub.h"
#include "ui.h"

std::string configLocation = "config.yml";
std::string settingsLocation = "settings.json";
std::atomic<bool> running = true;
std::atomic<bool> shouldRestart = false;

void signalHandler(int) { running = false; }

int main() {
  Config config(configLocation);
  Settings settings(settingsLocation, config);
  ShockerHub hub(config, settings);

  std::signal(SIGINT, signalHandler);

  std::thread([&]() {
    hub.connectSerial();
    hub.listShockers();
  }).detach();

  OscQueryServer oscQuery(config.oscPort, std::string(config.serviceName));
  oscQuery.setShockPath(config.shockParameter);
  if (config.hasSecondShockParameter)
    oscQuery.setSecondShockPath(config.secondShockParameter);
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
        hub.queueShock(config.shockStrength);
      } else if (oscQuery.secondShockPending) {
        oscQuery.secondShockPending = false;
        lock.unlock();
        hub.queueShockUpperHalf(config.shockStrength);
      }
    }
  });

  ui_run(config, settings, hub, settingsLocation);
  running = false;
  oscBridge.join();
  oscQuery.stop();
  hub.shutdown();
  settings.save(settingsLocation);

  if (shouldRestart) {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    ShellExecuteA(nullptr, "open", exePath, nullptr, nullptr, SW_SHOWNORMAL);
  }
  return 0;
}