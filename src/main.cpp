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

void signalHandler(int) { running = false; }

int main() {
  Config config(configLocation);
  Settings settings(settingsLocation, config);
  ShockerHub hub(config, settings);

  std::signal(SIGINT, signalHandler);

  hub.connectSerial();
  if (!hub.listShockers()) {
    hub.shutdown();
    return 0;
  }

  OscQueryServer oscQuery(config.oscPort, std::string(config.serviceName));
  oscQuery.setShockPath(config.shockParameter);
  if (!oscQuery.start()) {
    logMsg("Failed to start OSCQuery\n");
    hub.shutdown();
    return 1;
  }

  std::thread oscBridge([&]() {
    while (running) {
      if (oscQuery.shockPending) {
        oscQuery.shockPending = false;
        hub.queueShock(config.shockStrength);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });

  ui_run(config, settings, hub, settingsLocation);
  running = false;

  oscBridge.join();
  oscQuery.stop();
  hub.shutdown();
  settings.save(settingsLocation);
  return 0;
}