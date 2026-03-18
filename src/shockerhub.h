#pragma once

#include <fmt/ranges.h>
#include <serialib.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "chatbox.h"
#include "config.h"
#include "curve.h"
#include "logger.h"
#include "settings.h"

class ShockerHub {
 public:
  std::atomic<double> cooldownUntil{0.0};

  std::atomic<double> lastTriggerTimeAtomic{0.0};
  std::atomic<double> activeCooldownDuration{0.0};

  ChatboxSender chatbox;
  bool isConnected() { return workerThread.joinable(); }

  std::array<CurvePoint, 3> curvePoints = {
      {{20.0, 0.8}, {50.0, 0.5}, {80.0, 0.2}}};

  ShockerHub(Config& cfg, Settings& set)
      : config(cfg), settings(set), chatbox(cfg.vrchatHost) {}

  bool connectSerial() {
    logMsg("Attempting to connect to the shocker hub");
    if (config.serialPort != "") {
      bool opened =
          serial.openDevice(config.serialPort.c_str(), config.baudRate) == 1;
      if (!opened) {
        logMsg("Could not open port {}\n", config.serialPort);
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

  void queueShock(int duration = -1) {
    {
      std::lock_guard<std::mutex> lock(queueMutex);
      shockQueue.push({duration, false});
    }
    queueCV.notify_one();
  }

  void queueShockUpperHalf(int duration = -1) {
    {
      std::lock_guard<std::mutex> lock(queueMutex);
      shockQueue.push({duration, true});
    }
    queueCV.notify_one();
  }

  void emptyQueue() {
    std::lock_guard<std::mutex> lock(queueMutex);
    decltype(shockQueue) emptyQ;
    shockQueue.swap(emptyQ);
  }

  bool reconnectSerial() {
    int delay = 1000;
    while (true) {
      serial.closeDevice();
      if (connectSerial()) return true;
      logMsg("Reconnect failed, retrying in {}s...", delay / 1000);
      emptyQueue();
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
      delay = std::min(delay * 2, 15000);
    }
  }

  bool tryReconnect() {
    serial.closeDevice();
    config.serialPort = "";
    return connectSerial();
  }

  bool listShockers() {
    if (config.ShockerIDs.empty()) {
      logMsg(
          "No shockers configured and none found automatically.\nPlease set "
          "them up in config.yml\nThe program will now exit...\n");
      return false;
    }
    logMsg("Shockers found: {}\n", fmt::join(config.ShockerIDs, ", "));
    return true;
  }

  void shutdown() {
    stopWorker = true;
    queueCV.notify_one();
    if (workerThread.joinable()) {
      workerThread.join();
    }
    serial.closeDevice();
  }

  double getCurrentTime() {
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

 private:
  Config& config;
  Settings& settings;
  int lastShockerIndex = -1;
  double lastTriggerTime = 0.0;
  std::vector<double> shockTimestamps;
  serialib serial;

  std::queue<std::tuple<std::optional<int>, bool>> shockQueue;
  std::mutex queueMutex;
  std::condition_variable queueCV;
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
      for (int attempt = 0; attempt < 20; attempt++) {
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

    logMsg(
        "Couldn't connect to PiShock HUB, check connection and reconnect.\n");
    return false;
  }

  bool scanForOpenshock() {
    for (int i = 1; i <= 24; i++) {
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

    logMsg(
        "Couldn't connect to OpenShock HUB, check connection and press any key "
        "to retry...\n");
    return false;
  }

  void startWorkerThread() {
    if (!workerThread.joinable()) {
      logMsg("Connected\n");
      workerThread = std::thread([this]() { workerLoop(); });
    }
  }

  void workerLoop() {
    std::mt19937 rng((unsigned)std::chrono::high_resolution_clock::now()
                         .time_since_epoch()
                         .count());
    while (!stopWorker) {
      std::unique_lock<std::mutex> lock(queueMutex);

      // Sleep until there's work or shutdown
      queueCV.wait(lock, [this] { return !shockQueue.empty() || stopWorker; });
      if (stopWorker) break;

      // Take item and release lock before doing any work
      auto item = shockQueue.front();
      shockQueue.pop();
      lock.unlock();

      int durationMs = std::get<0>(item).value_or(-1);
      bool upperHalf = std::get<1>(item);

      if (config.cooldownEnabled) {
        double now = getCurrentTime();
        int cooldownWindowS = config.cooldownWindowS;
        shockTimestamps.erase(
            std::remove_if(shockTimestamps.begin(), shockTimestamps.end(),
                           [now, cooldownWindowS](double t) {
                             return now - t > cooldownWindowS;
                           }),
            shockTimestamps.end());
        double dynamicCooldown = std::min(
            (double)config.baseCooldown +
                (double)config.cooldownFactorS * (int)shockTimestamps.size(),
            (double)config.maxCooldown);
        double remaining = dynamicCooldown - (now - lastTriggerTime);
        activeCooldownDuration.store(dynamicCooldown);
        if (remaining > 0) {
          std::string cooldownMsg =
              fmt::format("On cooldown: {:.1f}s", remaining);
          logMsg("{}\n", cooldownMsg);
          chatbox.send(cooldownMsg);
          continue;
        }
      }

      std::string chosenShocker;
      std::vector<std::string>& ids = config.ShockerIDs;
      if (config.randomOrSeq) {
        lastShockerIndex = (lastShockerIndex + 1) % (int)ids.size();
        chosenShocker = ids[lastShockerIndex];
      } else {
        std::uniform_int_distribution<int> idxDist(0, (int)ids.size() - 1);
        chosenShocker = ids[idxDist(rng)];
      }

      if (durationMs == -1) {
        std::uniform_real_distribution<float> durDist(
            settings.minShockDuration,
            std::nextafter(settings.maxShockDuration,
                           std::numeric_limits<float>::infinity()));
        durationMs = std::max(100, (int)(durDist(rng) * 1000));
      }

      int intensity = upperHalf ? sampleIntensityUpperHalf(curvePoints)
                                : sampleIntensity(curvePoints);
      sendShock(durationMs, intensity, chosenShocker);
    }
  }

  void sendShock(int durationMs, int strength, const std::string& shockerID) {
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
      logMsg("Serial write failed, reconnecting...\n");
      reconnectSerial();
      queueShock(durationMs);
      return;
    }

    shockTimestamps.push_back(getCurrentTime());
    lastTriggerTime = getCurrentTime();

    if (config.cooldownEnabled) {
      double now = lastTriggerTime;
      int windowS = config.cooldownWindowS;
      shockTimestamps.erase(
          std::remove_if(
              shockTimestamps.begin(), shockTimestamps.end(),
              [now, windowS](double t) { return now - t > windowS; }),
          shockTimestamps.end());
      double dynamicCooldown = std::min(
          (double)config.baseCooldown +
              (double)config.cooldownFactorS * (int)shockTimestamps.size(),
          (double)config.maxCooldown);
      cooldownUntil.store(now + dynamicCooldown);
    } else {
      cooldownUntil.store(0.0);
    }

    // \xe2\x9a\xa1 =⚡symbol
    chatbox.send(fmt::format("\xe2\x9a\xa1 {}% | {:.1f}s", strength,
                             durationMs / 1000.0f));

    logMsg("Sent shock: {}%, {:.1f}s\n", strength, (durationMs / 1000.0f));
  }
};