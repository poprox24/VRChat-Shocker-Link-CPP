#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fmt/base.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

#include "config.h"
#include "httplib.h"
#include "mdns_advertiser.h"
#include "oscclient.h"
#include "settings.h"
#include "shockerhub.h"

std::string configLocation = "config.yml";
std::string settingsLocation = "settings.json";
std::atomic<bool> running = true;

void signalHandler(int signal) { running = false; }

int main() {
  Config config(configLocation);
  Settings settings(settingsLocation, config);
  ShockerHub shockerHub(config, settings);

  std::signal(SIGINT, signalHandler);

  fmt::print("Attempting to connect\n");
  shockerHub.connectSerial();
  if (!shockerHub.listShockers()) {
    shockerHub.shutdown();
    return 0;
  }

  OscQueryServer oscQuery(config.oscPort, std::string(config.serviceName));
  oscQuery.setShockPath(config.shockParameter);

  if (!oscQuery.start()) {
    fmt::print("Failed to start OSCQuery server\n");
    shockerHub.shutdown();
    return 1;
  }

  shockerHub.queueShock(config.shockStrength);

  fmt::print("Running. Ctrl+C to quit.\n");
  while (running) {
    if (oscQuery.shockPending) {
      oscQuery.shockPending = false;
      shockerHub.queueShock(config.shockStrength);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  settings.save(settingsLocation);

  fmt::print("Shutting down...\n");
  oscQuery.stop();
  shockerHub.shutdown();
  return 0;
}