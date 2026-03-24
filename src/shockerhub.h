#pragma once

#include <fmt/ranges.h>
#include <serialib.h>
#include <wininet.h>

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
#include "curve.h"
#include "logger.h"
#include "notifications.h"
#include "settings.h"
#include "version.h"

#pragma comment(lib, "wininet.lib")

class ShockerHub {
 public:
  std::atomic<double> cooldownUntil{0.0};

  std::atomic<double> lastTriggerTimeAtomic{0.0};
  std::atomic<double> activeCooldownDuration{0.0};

  ChatboxSender chatbox;

  bool isConnected = false;
  std::atomic<bool> shocksDisabled{false};

  std::mutex queueMutex;

  std::array<CurvePoint, 3> curvePoints = {
      {{20.0, 0.8}, {50.0, 0.5}, {80.0, 0.2}}};

  ShockerHub(Settings& set) : settings(set), chatbox(set.vrchatHost) {}

  bool connectSerial() {
    // In API mode there's no serial connection to establish
    if (!settings.useSerial) {
      isConnected = true;
      logMsg("[ShockerHub] API mode - skipping serial connection");
      startWorkerThread();
      return true;
    }

    logMsg("[ShockerHub] Attempting to connect to the shocker hub");

    if (!settings.serialPort.empty()) {
      bool opened = serial.openDevice(settings.serialPort.c_str(),
                                      settings.baudRate) == 1;
      if (!opened) {
        logMsg(
            "[ShockerHub] Could not open port saved in settings: {}, trying "
            "last "
            "known working port.\n",
            settings.serialPort);
      } else {
        startWorkerThread();
        return true;
      }
    }

    if (!settings.lastSerialPort.empty()) {
      bool opened = serial.openDevice(settings.lastSerialPort.c_str(),
                                      settings.baudRate) == 1;
      if (opened) {
        settings.serialPort = settings.lastSerialPort;
        startWorkerThread();
        return true;
      }
    }
    logMsg(
        "[ShockerHub] Could not open last known working port, running a port "
        "scan");
    if (settings.usePishock) {
      return scanForPishock();
    } else {
      return scanForOpenshock();
    }
  }

  void queueShock(int duration = -1, bool vibrate = false) {
    {
      std::lock_guard<std::mutex> lock(queueMutex);
      shockQueue.push({duration, false, vibrate});
    }
    queueCV.notify_one();
  }

  void queueShockUpperHalf(int duration = -1, bool vibrate = false) {
    {
      std::lock_guard<std::mutex> lock(queueMutex);
      shockQueue.push({duration, true, vibrate});
    }
    queueCV.notify_one();
  }

  void emptyQueue() {
    std::lock_guard<std::mutex> lock(queueMutex);
    decltype(shockQueue) emptyQ;
    shockQueue.swap(emptyQ);
  }

  bool reconnectSerial() {
    if (!settings.useSerial) return true;
    int delay = 1000;
    while (true) {
      serial.closeDevice();
      isConnected = false;
      if (connectSerial()) return true;
      logMsg("[ShockerHub] Reconnect failed, retrying in {}s...", delay / 1000);
      emptyQueue();
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
      delay = std::min(delay * 2, 15000);
    }
  }

  bool tryReconnect() {
    if (!settings.useSerial) return true;
    serial.closeDevice();
    settings.serialPort = "";
    return connectSerial();
  }

  bool listShockers() {
    if (settings.shockerIDs.empty()) {
      logMsg(
          "[ShockerHub] No shockers configured and none found "
          "automatically.\nPlease set them up in settings.\nThe program "
          "will "
          "now exit...\n");
      return false;
    }
    logMsg("[ShockerHub] Shockers found: {}\n",
           fmt::join(settings.shockerIDs, ", "));
    return true;
  }

  void enableShocks() {
    shocksDisabled = false;
    logMsg("[ShockerHub] Shocks re-enabled");
    return;
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
  Settings& settings;
  int lastShockerIndex = -1;
  double lastTriggerTime = 0.0;
  std::vector<double> shockTimestamps;
  serialib serial;

  // Duration, upperHalf, vibrate
  std::queue<std::tuple<std::optional<int>, bool, bool>> shockQueue;
  std::condition_variable queueCV;
  std::thread workerThread;
  std::atomic<bool> stopWorker = false;

  static std::string postJson(
      const std::string& url, const std::string& body,
      const std::vector<std::string>& extraHeaders = {}) {
    HINTERNET hNet =
        InternetOpenA("ShockerLink/" APP_VERSION, 0, nullptr, nullptr, 0);
    if (!hNet) return "";

    URL_COMPONENTSA uc{};
    uc.dwStructSize = sizeof(uc);
    char host[256] = {}, path[512] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = sizeof(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = sizeof(path);
    if (!InternetCrackUrlA(url.c_str(), 0, 0, &uc)) {
      InternetCloseHandle(hNet);
      return "";
    }

    HINTERNET hConn = InternetConnectA(hNet, host, uc.nPort, nullptr, nullptr,
                                       INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConn) {
      InternetCloseHandle(hNet);
      return "";
    }

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
    if (uc.nScheme == INTERNET_SCHEME_HTTPS) flags |= INTERNET_FLAG_SECURE;

    HINTERNET hReq = HttpOpenRequestA(hConn, "POST", path, nullptr, nullptr,
                                      nullptr, flags, 0);
    if (!hReq) {
      InternetCloseHandle(hConn);
      InternetCloseHandle(hNet);
      return "";
    }

    DWORD timeout = 5000;
    InternetSetOption(hReq, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout,
                      sizeof(timeout));
    InternetSetOption(hReq, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout,
                      sizeof(timeout));

    std::string headers = "Content-Type: application/json\r\n";
    for (auto& h : extraHeaders) headers += h + "\r\n";

    HttpSendRequestA(hReq, headers.c_str(), (DWORD)headers.size(),
                     (LPVOID)body.c_str(), (DWORD)body.size());

    std::string result;
    char buf[4096];
    DWORD read;
    while (InternetReadFile(hReq, buf, sizeof(buf) - 1, &read) && read > 0) {
      buf[read] = 0;
      result += buf;
    }

    InternetCloseHandle(hReq);
    InternetCloseHandle(hConn);
    InternetCloseHandle(hNet);
    return result;
  }

  bool scanForPishock() {
    for (int i = 1; i <= 50; i++) {
      settings.serialPort = "COM" + std::to_string(i);

      bool opened = serial.openDevice(settings.serialPort.c_str(),
                                      settings.baudRate) == 1;
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
          if (settings.shockerIDs.empty()) {
            std::string jsonStr =
                response.substr(14);  // strip "TERMINALINFO: "
            auto json = nlohmann::json::parse(jsonStr, nullptr, false);
            if (!json.is_discarded() && json.contains("shockers")) {
              for (auto& s : json["shockers"]) {
                settings.pushShockerId(std::to_string(s["id"].get<int>()));
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
        "[ShockerHub] Couldn't connect to PiShock HUB, check connection and "
        "reconnect.\n");
    return false;
  }

  bool scanForOpenshock() {
    for (int i = 1; i <= 24; i++) {
      settings.serialPort = "COM" + std::to_string(i);

      bool opened = serial.openDevice(settings.serialPort.c_str(),
                                      settings.baudRate) == 1;
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
        "[ShockerHub] Couldn't connect to OpenShock HUB, check connection and "
        "press the reconnect button.\n");
    return false;
  }

  void startWorkerThread() {
    settings.lastSerialPort = settings.serialPort;
    isConnected = true;
    if (!workerThread.joinable()) {
      if (settings.useSerial)
        logMsg("[ShockerHub] Connected on {}\n", settings.serialPort);
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

      if (shocksDisabled) {
        logMsg("[ShockerHub] Shocks disabled, ignoring\n");
        continue;
      }

      int durationMs = std::get<0>(item).value_or(-1);
      bool upperHalf = std::get<1>(item);
      bool vibrate = std::get<2>(item);

      if (settings.cooldownEnabled) {
        double now = getCurrentTime();
        int cooldownWindowS = settings.cooldownWindow;
        shockTimestamps.erase(
            std::remove_if(shockTimestamps.begin(), shockTimestamps.end(),
                           [now, cooldownWindowS](double t) {
                             return now - t > cooldownWindowS;
                           }),
            shockTimestamps.end());
        double dynamicCooldown = std::min(
            (double)settings.baseCooldown +
                (double)settings.cooldownFactor * (int)shockTimestamps.size(),
            (double)settings.maxCooldown);
        double remaining = dynamicCooldown - (now - lastTriggerTime);
        activeCooldownDuration.store(dynamicCooldown);
        if (remaining > 0) {
          std::string cooldownMsg =
              fmt::format("[ShockerHub] On cooldown: {:.1f}s", remaining);
          logMsg("{}\n", cooldownMsg);
          chatbox.send(cooldownMsg);
          continue;
        }
      }

      std::string chosenShocker;
      std::vector<std::string> ids;
      {
        std::lock_guard<std::mutex> lock(queueMutex);
        ids = settings.shockerIDs;
      }
      if (settings.randomOrSeq) {
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
      sendShock(durationMs, intensity, chosenShocker, vibrate);
    }
  }

  // Routes to serial or API depending on settings.useSerial
  void sendShock(int durationMs, int strength, const std::string& shockerID,
                 bool vibrate) {
    if (settings.useSerial)
      sendShockSerial(durationMs, strength, shockerID, vibrate);
    else
      sendShockApi(durationMs, strength, shockerID, vibrate);
  }

  // Called after a shock is successfully sent (serial or API)
  void afterShockSent(int durationMs, int strength, const std::string& opType) {
    shockTimestamps.push_back(getCurrentTime());
    lastTriggerTime = getCurrentTime();

    if (settings.cooldownEnabled) {
      double now = lastTriggerTime;
      int windowS = settings.cooldownWindow;
      shockTimestamps.erase(
          std::remove_if(
              shockTimestamps.begin(), shockTimestamps.end(),
              [now, windowS](double t) { return now - t > windowS; }),
          shockTimestamps.end());
      double dynamicCooldown = std::min(
          (double)settings.baseCooldown +
              (double)settings.cooldownFactor * (int)shockTimestamps.size(),
          (double)settings.maxCooldown);
      cooldownUntil.store(now + dynamicCooldown);
    } else {
      cooldownUntil.store(0.0);
    }

    // \xe2\x9a\xa1 = ⚡ symbol
    chatbox.send(fmt::format("\xe2\x9a\xa1 {}% | {:.1f}s", strength,
                             durationMs / 1000.0f));
    std::string notifMsg =
        fmt::format("{}% | {:.1f}s", strength, durationMs / 1000.0f);
    if (settings.notificationsEnabled) {
      if (!settings.notifUseOvrToolkit)
        Notifications::sendXSOverlay("⚡ Shock", notifMsg);
      else
        Notifications::sendOVRToolkit("⚡ Shock", notifMsg);
    }

    logMsg("[ShockerHub] Sent {}: {}%, {:.1f}s\n", opType, strength,
           durationMs / 1000.0f);
  }

  void sendShockSerial(int durationMs, int strength,
                       const std::string& shockerID, bool vibrate) {
    std::string command;
    std::string opType = vibrate ? "vibrate" : "shock";

    if (settings.usePishock) {
      nlohmann::json payload = {{"cmd", "operate"},
                                {"value",
                                 {{"id", std::stoi(shockerID)},
                                  {"op", opType},
                                  {"duration", durationMs},
                                  {"intensity", strength}}}};
      command = payload.dump() + "\n";
    } else {
      nlohmann::json payload = {{"model", "caixianlin"},
                                {"id", std::stoi(shockerID)},
                                {"type", opType},
                                {"intensity", strength},
                                {"durationMs", durationMs}};
      command = "rftransmit " + payload.dump() + "\n";
    }

    int result = serial.writeString(command.c_str());
    if (result <= 0) {
      logMsg("[ShockerHub] Serial write failed, reconnecting...\n");
      reconnectSerial();
      queueShock(durationMs);
      return;
    }

    afterShockSent(durationMs, strength, opType);
  }

  void sendShockApi(int durationMs, int strength, const std::string& shockerID,
                    bool vibrate) {
    std::string opType = vibrate ? "vibrate" : "shock";

    try {
      if (settings.usePishock) {
        // PiShock API (latest legacy endpoint)
        int durationSec = std::clamp((durationMs + 500) / 1000, 1, 15);
        nlohmann::json payload = {
            {"Username", settings.pishockUsername},
            {"Apikey", settings.pishockApiKey},
            {"Code", shockerID},
            {"Name", "ShockerLink"},
            {"Op", vibrate ? 1 : 0},  // 0=shock, 1=vibrate
            {"Duration", durationSec},
            {"Intensity", strength}};

        auto resp =
            postJson("https://do.pishock.com/api/apioperate", payload.dump());

        if (resp.empty()) {
          logMsg("[ShockerHub] PiShock API: no response (check credentials)\n");
          return;
        }
        if (resp.find("Succeeded") == std::string::npos &&
            resp.find("200") == std::string::npos) {
          logMsg("[ShockerHub] PiShock API error: {}\n", resp);
          return;
        }

      } else {
        // OpenShock API v2
        std::string type = vibrate ? "Vibrate" : "Shock";
        nlohmann::json shockEntry = {{"id", shockerID},
                                     {"type", type},
                                     {"intensity", strength},
                                     {"duration", durationMs},
                                     {"exclusive", true}};
        nlohmann::json payload = {
            {"shocks", nlohmann::json::array({shockEntry})},
            {"customName", "ShockerLink"}};

        std::string serverUrl = settings.openshockServerUrl;
        if (serverUrl.starts_with("https://"))
          serverUrl = serverUrl.substr(8);
        else if (serverUrl.starts_with("http://"))
          serverUrl = serverUrl.substr(7);

        auto resp = postJson(
            "https://" + serverUrl + "/2/shockers/control", payload.dump(),
            {"Open-Shock-Token: " + settings.openshockApiToken});

        if (resp.empty()) {
          logMsg(
              "[ShockerHub] OpenShock API: no response (check token / "
              "server)\n");
          return;
        }
      }
    } catch (std::exception& e) {
      logMsg("[ShockerHub] API send error: {}\n", e.what());
      return;
    }

    afterShockSent(durationMs, strength, opType);
  }
};