#pragma once

#include <fmt/base.h>
#include <serialib.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "config.h"
#include "settings.h"

class ShockerHub {
 public:
  ShockerHub(Config& cfg, Settings& set) : config(cfg), settings(set) {}

  bool connectSerial() {
    if (config.serialPort != "") {
      bool opened =
          serial.openDevice(config.serialPort.c_str(), config.baudRate) == 1;
      if (!opened) {
        fmt::print("Could not open port {}\n", config.serialPort);
        return false;
      }
      startWorkerThread();
      return true;
    }

    if (config.usePishock) {
      return scanForPishock();
    } else {
      return scanForOpenshock();
    }
  }

  void queueShock(int strength, int duration = -1) {
    std::lock_guard<std::mutex> lock(queueMutex);
    shockQueue.push({duration, strength});
  }

  void emptyQueue() {
    decltype(shockQueue) emptyQ;
    shockQueue.swap(emptyQ);
  }

  bool reconnectSerial() {
    while (true) {
      serial.closeDevice();
      if (connectSerial()) return true;
      fmt::print(
          "Reconnect failed, all queued shocks dropped.\nPress any key to "
          "retry...\n");
      emptyQueue();
      system("pause");
    }
  }

  bool listShockers() {
    if (config.ShockerIDs.empty()) {
      fmt::print(
          "No shockers configured and none found automatically.\n"
          "Please set them up in config.yml\n"
          "The program will now exit...\n");
      system("pause");
      return false;
    }

    std::string ids = "";
    for (int i = 0; i < (int)config.ShockerIDs.size(); i++) {
      if (i > 0) ids += ", ";
      ids += config.ShockerIDs[i];
    }
    fmt::print("Shockers found: {}\n", ids);
    return true;
  }

  void shutdown() {
    stopWorker = true;
    if (workerThread.joinable()) {
      workerThread.join();
    }
    serial.closeDevice();
  }

 private:
  Config& config;
  Settings& settings;
  int lastShockerIndex = -1;
  double lastTriggerTime = 0.0;
  std::vector<double> shockTimestamps;
  serialib serial;

  std::queue<std::pair<std::optional<int>, int>> shockQueue;
  std::mutex queueMutex;
  std::thread workerThread;
  std::atomic<bool> stopWorker = false;

  bool scanForPishock() {
    for (int i = 1; i <= 50; i++) {
      config.serialPort = "COM" + std::to_string(i);

      bool opened =
          serial.openDevice(config.serialPort.c_str(), config.baudRate) == 1;
      if (!opened) continue;

      serial.writeString("{\"cmd\": \"info\"}\n");

      bool found = false;
      for (int attempt = 0; attempt < 40; attempt++) {
        char buf[1024] = {0};
        serial.readString(buf, '\n', 1024, 1000);
        std::string response(buf);

        if (response.starts_with("TERMINALINFO: ") &&
            response.find("pishock") != std::string::npos) {
          found = true;
          if (config.ShockerIDs.empty()) {
            std::string jsonStr =
                response.substr(14);  // strip "TERMINALINFO: "
            auto json = nlohmann::json::parse(jsonStr, nullptr, false);
            if (!json.is_discarded() && json.contains("shockers")) {
              for (auto& s : json["shockers"]) {
                config.pushShockerId(std::to_string(s["id"].get<int>()));
              }
            }
          }
          break;
        }
      }

      if (found) {
        startWorkerThread();
        return true;
      }
      serial.closeDevice();
    }

    fmt::print(
        "Couldn't connect to PiShock HUB, check connection and press any key "
        "to retry...\n");
    system("pause");
    return reconnectSerial();
  }

  bool scanForOpenshock() {
    for (int i = 1; i <= 50; i++) {
      config.serialPort = "COM" + std::to_string(i);

      bool opened =
          serial.openDevice(config.serialPort.c_str(), config.baudRate) == 1;
      if (!opened) continue;

      serial.writeString("domain\n");

      bool found = false;
      for (int attempt = 0; attempt < 5; attempt++) {
        char buf[64] = {0};
        serial.readString(buf, '\n', 64, 1000);
        if (std::string(buf).find("openshock") != std::string::npos) {
          found = true;
          break;
        }
      }

      if (found) {
        startWorkerThread();
        return true;
      }

      serial.closeDevice();
    }

    fmt::print(
        "Couldn't connect to OpenShock HUB, check connection and press any key "
        "to retry...\n");
    system("pause");
    return reconnectSerial();
  }

  void startWorkerThread() {
    if (!workerThread.joinable()) {
      fmt::print("Connected\n");
      workerThread = std::thread([this]() { workerLoop(); });
    }
  }

  double getCurrentTime() {
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  void workerLoop() {
    // Set seed for random
    srand((unsigned)std::chrono::high_resolution_clock::now()
              .time_since_epoch()
              .count());
    while (!stopWorker) {
      std::unique_lock<std::mutex> lock(queueMutex);

      if (!shockQueue.empty()) {
        if (config.cooldownEnabled) {
          double now = getCurrentTime();
          int cooldownWindowS = config.cooldownWindowS;
          // Remove older than cooldownWindowS seconds from shockTimestamps
          shockTimestamps.erase(
              std::remove_if(shockTimestamps.begin(), shockTimestamps.end(),
                             [now, cooldownWindowS](double time) {
                               return now - time > cooldownWindowS;
                             }),
              shockTimestamps.end());
          int triggerCount = shockTimestamps.size();
          double dynamicCooldown =
              std::min((double)config.baseCooldown +
                           (double)config.cooldownFactorS * triggerCount,
                       (double)config.maxCooldown);

          // Check if still on cooldown
          if (now - lastTriggerTime <= dynamicCooldown) {
            fmt::print("On cooldown: {:.1f}s\n",
                       lastTriggerTime - now + dynamicCooldown);
            shockQueue.pop();
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
          }
        }
        int durationMs = shockQueue.front().first.value_or(-1);
        int strength = shockQueue.front().second;
        shockQueue.pop();
        lock.unlock();

        // Pick which shocker to use (random or sequential)
        std::string chosenShocker;
        std::vector<std::string>& ids = config.ShockerIDs;
        if (config.randomOrSeq) {
          chosenShocker = ids[rand() % ids.size()];
        } else {
          lastShockerIndex = (lastShockerIndex + 1) % (int)ids.size();
          chosenShocker = ids[lastShockerIndex];
        }

        if (durationMs == -1) {
          durationMs = (int)((settings.minShockDuration +
                              (float)rand() / RAND_MAX *
                                  (settings.maxShockDuration -
                                   settings.minShockDuration)) *
                             1000);
        }

        sendShock(durationMs, strength, chosenShocker);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      lock.unlock();
    }
  }

  void sendShock(int durationMs, int strength, std::string shockerID) {
    std::string command;

    if (config.usePishock) {
      nlohmann::json payload = {{"cmd", "operate"},
                                {"value",
                                 {{"id", std::stoi(shockerID)},
                                  {"op", "shock"},
                                  {"duration", durationMs},
                                  {"intensity", strength}}}};
      command = payload.dump() + "\n";
    } else {
      nlohmann::json payload = {{"model", "caixianlin"},
                                {"id", std::stoi(shockerID)},
                                {"type", "shock"},
                                {"intensity", strength},
                                {"durationMs", durationMs}};
      command = "rftransmit " + payload.dump() + "\n";
    }

    int result = serial.writeString(command.c_str());
    if (result <= 0) {
      fmt::print("Serial write failed, reconnecting...\n");
      reconnectSerial();
      queueShock(strength, durationMs);
      return;
    }

    shockTimestamps.push_back(getCurrentTime());
    lastTriggerTime = getCurrentTime();

    fmt::print("Sent shock: {}%, {:.1f}s\n", strength, (durationMs / 1000.0f));
  }
};