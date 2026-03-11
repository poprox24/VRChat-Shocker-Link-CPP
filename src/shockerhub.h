#pragma once

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
  ChatboxSender chatbox;

  std::array<CurvePoint, 3> curvePoints = {
      {{20.0, 0.8}, {50.0, 0.5}, {80.0, 0.2}}};

  ShockerHub(Config& cfg, Settings& set)
      : config(cfg), settings(set), chatbox(cfg.vrchatHost) {}

  bool connectSerial() {
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

  void queueShock(int strength, int duration = -1) {
    {
      std::lock_guard<std::mutex> lock(queueMutex);
      shockQueue.push({duration, strength, false});
    }
    queueCV.notify_one();
  }

  void queueShockUpperHalf(int strength, int duration = -1) {
    {
      std::lock_guard<std::mutex> lock(queueMutex);
      shockQueue.push({duration, strength, true});
    }
    queueCV.notify_one();
  }

  void emptyQueue() {
    std::lock_guard<std::mutex> lock(queueMutex);
    decltype(shockQueue) emptyQ;
    shockQueue.swap(emptyQ);
  }

  bool reconnectSerial() {
    while (true) {
      serial.closeDevice();
      if (connectSerial()) return true;
      logMsg(
          "Reconnect failed, all queued shocks dropped.\nPress any key to "
          "retry...\n");
      emptyQueue();
      MessageBoxA(
          nullptr,
          "Reconnect failed. All queued shocks dropped. Click OK to retry.",
          "ShockerLink", MB_OK | MB_ICONWARNING);
    }
  }

  bool listShockers() {
    if (config.ShockerIDs.empty()) {
      logMsg(
          "No shockers configured and none found automatically.\n"
          "Please set them up in config.yml\n"
          "The program will now exit...\n");
      MessageBoxA(nullptr, "No shockers configured. Set them up in config.yml.",
                  "ShockerLink", MB_OK | MB_ICONERROR);
      return false;
    }

    std::string ids = "";
    for (int i = 0; i < (int)config.ShockerIDs.size(); i++) {
      if (i > 0) ids += ", ";
      ids += config.ShockerIDs[i];
    }
    logMsg("Shockers found: {}\n", ids);
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

 private:
  Config& config;
  Settings& settings;
  int lastShockerIndex = -1;
  double lastTriggerTime = 0.0;
  std::vector<double> shockTimestamps;
  serialib serial;

  std::queue<std::tuple<std::optional<int>, int, bool>> shockQueue;
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

    logMsg(
        "Couldn't connect to PiShock HUB, check connection and press any key "
        "to retry...\n");
    MessageBoxA(nullptr,
                "Couldn't connect to PiShock HUB. Check connection then click "
                "OK to retry.",
                "ShockerLink", MB_OK | MB_ICONWARNING);
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

    logMsg(
        "Couldn't connect to OpenShock HUB, check connection and press any key "
        "to retry...\n");
    MessageBoxA(nullptr,
                "Couldn't connect to OpenShock HUB. Check connection then "
                "click OK to retry.",
                "ShockerLink", MB_OK | MB_ICONWARNING);
    return reconnectSerial();
  }

  void startWorkerThread() {
    if (!workerThread.joinable()) {
      logMsg("Connected\n");
      workerThread = std::thread([this]() { workerLoop(); });
    }
  }

  double getCurrentTime() {
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
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
        if (remaining > 0) {
          std::string cooldownMsg =
              fmt::format("On cooldown: {:.1f}s", remaining);
          logMsg("{}\n", cooldownMsg);
          chatbox.send(cooldownMsg);
          shockQueue.pop();
          lock.unlock();
          std::this_thread::sleep_for(
              std::chrono::milliseconds(static_cast<int>(remaining * 1000)));
          continue;
        }
      }

      int durationMs = std::get<0>(shockQueue.front()).value_or(-1);
      int strength = std::get<1>(shockQueue.front());
      bool upperHalf = std::get<2>(shockQueue.front());
      shockQueue.pop();
      lock.unlock();

      std::string chosenShocker;
      std::vector<std::string>& ids = config.ShockerIDs;
      if (config.randomOrSeq) {
        std::uniform_int_distribution<int> idxDist(0, (int)ids.size() - 1);
        chosenShocker = ids[idxDist(rng)];
      } else {
        lastShockerIndex = (lastShockerIndex + 1) % (int)ids.size();
        chosenShocker = ids[lastShockerIndex];
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
      logMsg("Serial write failed, reconnecting...\n");
      reconnectSerial();
      queueShock(strength, durationMs);
      return;
    }

    shockTimestamps.push_back(getCurrentTime());
    lastTriggerTime = getCurrentTime();

    // \xe2\x9a\xa1 =⚡symbol
    chatbox.send(fmt::format("\xe2\x9a\xa1 {}% | {:.1f}s", strength,
                             durationMs / 1000.0f));

    shockTimestamps.push_back(getCurrentTime());
    lastTriggerTime = getCurrentTime();
    logMsg("Sent shock: {}%, {:.1f}s\n", strength, (durationMs / 1000.0f));
  }
};