#pragma once

#include <fmt/ranges.h>
#include <serialib.h>
#include <winhttp.h>
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
#include <unordered_map>
#include <vector>

#include "chatbox.h"
#include "curve.h"
#include "logger.h"
#include "notifications.h"
#include "settings.h"
#include "version.h"

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "winhttp.lib")

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
          "[ShockerHub] No shockers configured. Shocks will be disabled until "
          "IDs are set in settings.\n");
      return true;
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

  bool resolvePiShockApi() {
    // 1. Get userId
    std::string authUrl =
        "https://auth.pishock.com/Auth/GetUserIfAPIKeyValid?apikey=" +
        settings.pishockApiKey + "&username=" + settings.pishockUsername;
    auto authResp = httpGet(authUrl);
    if (authResp.empty()) {
      logMsg("[PiShock] Auth request failed (check username/apikey)\n");
      return false;
    }
    auto authJson = nlohmann::json::parse(authResp, nullptr, false);
    if (authJson.is_discarded() ||
        (!authJson.contains("UserId") && !authJson.contains("UserID"))) {
      logMsg("[PiShock] Auth parse failed: {}\n", authResp.substr(0, 200));
      return false;
    }
    pishockUserId_ = authJson.contains("UserId")
                         ? authJson["UserId"].get<int>()
                         : authJson["UserID"].get<int>();
    logMsg("[PiShock] Authenticated as userId {}\n", pishockUserId_);

    // 2. Get all owned devices to map shockerId -> clientId
    std::string devUrl =
        "https://ps.pishock.com/PiShock/GetUserDevices?UserId=" +
        std::to_string(pishockUserId_) + "&Token=" + settings.pishockApiKey +
        "&api=true";
    auto devResp = httpGet(devUrl);
    if (devResp.empty()) {
      logMsg("[PiShock] GetUserDevices failed\n");
      return false;
    }
    auto devJson = nlohmann::json::parse(devResp, nullptr, false);
    if (devJson.is_discarded() || !devJson.is_array()) {
      logMsg("[PiShock] GetUserDevices parse failed\n");
      return false;
    }

    pishockShockerToClient_.clear();
    for (auto& dev : devJson) {
      int clientId = dev.value("clientId", -1);
      if (clientId == -1 || !dev.contains("shockers")) continue;
      for (auto& s : dev["shockers"]) {
        int sid = s.value("shockerId", -1);
        if (sid != -1) pishockShockerToClient_[sid] = clientId;
      }
    }

    if (pishockShockerToClient_.empty()) {
      logMsg("[PiShock] No shockers found on account\n");
      return false;
    }
    logMsg("[PiShock] Resolved {} shocker(s)\n",
           pishockShockerToClient_.size());
    if (settings.shockerIDs.empty()) {
      for (auto& [sid, cid] : pishockShockerToClient_) {
        settings.pushShockerId(std::to_string(sid));
      }
      logMsg("[PiShock] Auto-populated {} shocker ID(s): {}\n",
             settings.shockerIDs.size(), fmt::join(settings.shockerIDs, ", "));
    }

    pishockResolved_ = true;
    return true;
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

  int pishockUserId_ = -1;
  std::unordered_map<int, int>
      pishockShockerToClient_;  // shockerId -> clientId
  bool pishockResolved_ = false;

  static std::string httpGet(const std::string& url) {
    HINTERNET hNet =
        InternetOpenA("ShockerLink/" APP_VERSION, 0, nullptr, nullptr, 0);
    if (!hNet) return "";
    HINTERNET hUrl =
        InternetOpenUrlA(hNet, url.c_str(), nullptr, 0,
                         INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD |
                             INTERNET_FLAG_NO_CACHE_WRITE,
                         0);
    if (!hUrl) {
      InternetCloseHandle(hNet);
      return "";
    }
    std::string result;
    char buf[4096];
    DWORD read;
    while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read > 0)
      result.append(buf, read);
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hNet);
    return result;
  }

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

  bool sendPiShockWs(int durationMs, int strength, int shockerId, int clientId,
                     bool vibrate) {
    nlohmann::json body = {{"id", shockerId},
                           {"m", vibrate ? "v" : "s"},
                           {"i", strength},
                           {"d", durationMs},
                           {"r", true},
                           {"l",
                            {{"u", pishockUserId_},
                             {"ty", "api"},
                             {"w", false},
                             {"h", false},
                             {"o", "ShockerLink"}}}};
    nlohmann::json cmd = {
        {"Operation", "PUBLISH"},
        {"PublishCommands",
         {{{"Target", "c" + std::to_string(clientId) + "-ops"},
           {"Body", body}}}}};
    std::string msgStr = cmd.dump();

    std::wstring wUser(settings.pishockUsername.begin(),
                       settings.pishockUsername.end());
    std::wstring wKey(settings.pishockApiKey.begin(),
                      settings.pishockApiKey.end());
    std::wstring path = L"/v2?Username=" + wUser + L"&ApiKey=" + wKey;

    HINTERNET hSession =
        WinHttpOpen(L"ShockerLink", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
      logMsg("[PiShock] WinHttpOpen failed ({})\n", GetLastError());
      return false;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, L"broker.pishock.com",
                                        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
      WinHttpCloseHandle(hSession);
      logMsg("[PiShock] WinHttpConnect failed ({})\n", GetLastError());
      return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
      WinHttpCloseHandle(hConnect);
      WinHttpCloseHandle(hSession);
      logMsg("[PiShock] WinHttpOpenRequest failed ({})\n", GetLastError());
      return false;
    }

    WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr,
                     0);

    DWORD timeout = 5000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout,
                     sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout,
                     sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout,
                     sizeof(timeout));

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr,
                            0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
      logMsg("[PiShock] WS handshake failed ({})\n", GetLastError());
      WinHttpCloseHandle(hRequest);
      WinHttpCloseHandle(hConnect);
      WinHttpCloseHandle(hSession);
      return false;
    }

    HINTERNET hWs = WinHttpWebSocketCompleteUpgrade(hRequest, 0);
    WinHttpCloseHandle(hRequest);
    if (!hWs) {
      logMsg("[PiShock] WS upgrade failed ({})\n", GetLastError());
      WinHttpCloseHandle(hConnect);
      WinHttpCloseHandle(hSession);
      return false;
    }

    DWORD sendErr =
        WinHttpWebSocketSend(hWs, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                             (PVOID)msgStr.data(), (DWORD)msgStr.size());

    bool ok = (sendErr == ERROR_SUCCESS);
    if (!ok) {
      logMsg("[PiShock] WS send failed ({})\n", sendErr);
    } else {
      char recvBuf[4096] = {};
      DWORD bytesRead = 0;
      WINHTTP_WEB_SOCKET_BUFFER_TYPE bufType{};
      DWORD recvErr = WinHttpWebSocketReceive(
          hWs, recvBuf, (DWORD)sizeof(recvBuf) - 1, &bytesRead, &bufType);
      if (recvErr == ERROR_SUCCESS && bytesRead > 0) {
        std::string resp(recvBuf, bytesRead);
        auto rj = nlohmann::json::parse(resp, nullptr, false);
        if (!rj.is_discarded() && rj.value("IsError", false)) {
          logMsg("[PiShock] Broker error: {}\n", rj.value("Message", resp));
          ok = false;
        }
      }
    }

    WinHttpWebSocketClose(hWs, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr,
                          0);
    WinHttpCloseHandle(hWs);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
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
      if (ids.empty()) {
        logMsg("[ShockerHub] No shocker IDs configured, dropping shock\n");
        continue;
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
        if (!pishockResolved_ && !resolvePiShockApi()) {
          logMsg("[ShockerHub] PiShock API setup failed\n");
          return;
        }

        int sid = -1;
        try {
          sid = std::stoi(shockerID);
        } catch (...) {
        }
        if (sid == -1) {
          logMsg("[ShockerHub] Invalid shocker ID: {}\n", shockerID);
          return;
        }

        auto it = pishockShockerToClient_.find(sid);
        if (it == pishockShockerToClient_.end()) {
          // Not found — re-resolve once (account may have changed)
          logMsg("[ShockerHub] Shocker {} not in device list, re-resolving\n",
                 sid);
          pishockResolved_ = false;
          if (!resolvePiShockApi()) return;
          it = pishockShockerToClient_.find(sid);
          if (it == pishockShockerToClient_.end()) {
            logMsg(
                "[ShockerHub] Shocker {} not found after re-resolve. "
                "Ensure the ID matches your PiShock dashboard.\n",
                sid);
            return;
          }
        }

        if (!sendPiShockWs(durationMs, strength, sid, it->second, vibrate)) {
          // Mark unresolved so next attempt re-fetches clientId mapping
          pishockResolved_ = false;
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