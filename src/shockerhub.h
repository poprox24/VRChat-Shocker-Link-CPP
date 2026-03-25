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
#include "stats.h"
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
    if (!settings.useSerial) {
      isConnected = true;
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
            "last known working port.\n",
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
    return settings.usePishock ? scanForPishock() : scanForOpenshock();
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
    while (!stopWorker) {
      serial.closeDevice();
      isConnected = false;
      if (connectSerial()) return true;
      logMsg("[ShockerHub] Reconnect failed, retrying in {}s...", delay / 1000);
      emptyQueue();
      std::this_thread::sleep_for(std::chrono::milliseconds(delay));
      delay = std::min(delay * 2, 15000);
    }
    return false;
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
  }

  void shutdown() {
    stopWorker = true;
    queueCV.notify_one();
    if (workerThread.joinable()) workerThread.join();
    serial.closeDevice();
  }

  double getCurrentTime() {
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  bool resolveOpenShockApi() {
    std::string serverUrl = settings.openshockServerUrl;
    if (serverUrl.starts_with("https://"))
      serverUrl = serverUrl.substr(8);
    else if (serverUrl.starts_with("http://"))
      serverUrl = serverUrl.substr(7);
    if (!serverUrl.empty() && serverUrl.back() == '/') serverUrl.pop_back();

    std::string url = "https://" + serverUrl + "/1/shockers/own";
    auto resp =
        httpGetWinHttp(url, {"openshocktoken: " + settings.openshockApiToken});
    if (resp.empty()) {
      logMsg("[OpenShock] GET /1/shockers/own failed (check token / server)\n");
      return false;
    }

    auto j = nlohmann::json::parse(resp, nullptr, false);
    if (j.is_discarded() || !j.contains("data") || !j["data"].is_array()) {
      logMsg("[OpenShock] GET /1/shockers/own parse failed: {}\n",
             resp.substr(0, 200));
      return false;
    }

    if (settings.shockerIDs.empty()) {
      for (auto& s : j["data"])
        if (s.contains("id"))
          settings.pushShockerId(s["id"].get<std::string>());
      logMsg("[OpenShock] Auto-populated {} shocker ID(s): {}\n",
             settings.shockerIDs.size(), fmt::join(settings.shockerIDs, ", "));
    }

    logMsg("[OpenShock] Resolved {} shocker(s)\n", j["data"].size());
    return true;
  }

  // Connect to PiShock API
  // Handles Authenticating and getting shocker IDs
  bool resolvePiShockApi() {
    // Authenticate and get userId
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

    // Get all owned devices to map shockerId -> clientId
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

    pishockShockerToClient.clear();
    for (auto& dev : devJson) {
      int clientId = dev.value("clientId", -1);
      if (clientId == -1 || !dev.contains("shockers")) continue;
      for (auto& s : dev["shockers"]) {
        int sid = s.value("shockerId", -1);
        if (sid != -1) pishockShockerToClient[sid] = clientId;
      }
    }

    if (pishockShockerToClient.empty()) {
      logMsg("[PiShock] No shockers found on account\n");
      return false;
    }
    logMsg("[PiShock] Resolved {} shocker(s)\n", pishockShockerToClient.size());

    if (settings.shockerIDs.empty()) {
      for (auto& [sid, cid] : pishockShockerToClient)
        settings.pushShockerId(std::to_string(sid));
      logMsg("[PiShock] Auto-populated {} shocker ID(s): {}\n",
             settings.shockerIDs.size(), fmt::join(settings.shockerIDs, ", "));
    }

    pishockResolved = true;
    return true;
  }

 private:
  Settings& settings;
  int lastShockerIndex = -1;
  std::vector<double> shockTimestamps;
  serialib serial;

  // Duration, useUpperHalf, vibrate
  std::queue<std::tuple<std::optional<int>, bool, bool>> shockQueue;
  std::condition_variable queueCV;
  std::thread workerThread;
  std::atomic<bool> stopWorker = false;

  int pishockUserId_ = -1;
  // shockerId -> clientId
  std::unordered_map<int, int> pishockShockerToClient;
  bool pishockResolved = false;

  // WinINet GET - used for PiShock endpoints
  static std::string httpGet(
      const std::string& url,
      const std::vector<std::string>& extraHeaders = {}) {
    HINTERNET hNet =
        InternetOpenA("ShockerLink/" APP_VERSION, 0, nullptr, nullptr, 0);
    if (!hNet) return "";
    std::string hdrs;
    for (auto& h : extraHeaders) hdrs += h + "\r\n";
    HINTERNET hUrl = InternetOpenUrlA(
        hNet, url.c_str(), hdrs.empty() ? nullptr : hdrs.c_str(),
        hdrs.empty() ? 0 : (DWORD)hdrs.size(),
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

  // WinHTTP GET - used for OpenShock endpoints
  static std::string httpGetWinHttp(
      const std::string& url,
      const std::vector<std::string>& extraHeaders = {}) {
    HINTERNET hSession = WinHttpOpen(
        L"ShockerLink/" APP_VERSION, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    URL_COMPONENTSA uc{};
    uc.dwStructSize = sizeof(uc);
    char host[256] = {}, path[512] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = sizeof(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = sizeof(path);
    if (!InternetCrackUrlA(url.c_str(), 0, 0, &uc)) {
      WinHttpCloseHandle(hSession);
      return "";
    }

    HINTERNET hConnect = WinHttpConnect(
        hSession, std::wstring(host, host + uc.dwHostNameLength).c_str(),
        uc.nPort, 0);
    if (!hConnect) {
      WinHttpCloseHandle(hSession);
      return "";
    }

    DWORD flags = WINHTTP_FLAG_REFRESH;
    if (url.rfind("https://", 0) == 0) flags |= WINHTTP_FLAG_SECURE;

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"GET", std::wstring(path, path + uc.dwUrlPathLength).c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
      WinHttpCloseHandle(hConnect);
      WinHttpCloseHandle(hSession);
      return "";
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy,
                     sizeof(redirectPolicy));

    std::string hdrStr;
    for (auto& h : extraHeaders) hdrStr += h + "\r\n";
    if (!hdrStr.empty()) {
      std::wstring whdr(hdrStr.begin(), hdrStr.end());
      WinHttpAddRequestHeaders(hRequest, whdr.c_str(), (DWORD)whdr.size(),
                               WINHTTP_ADDREQ_FLAG_ADD);
    }

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
      WinHttpCloseHandle(hRequest);
      WinHttpCloseHandle(hConnect);
      WinHttpCloseHandle(hSession);
      return "";
    }

    std::string result;
    char buf[4096];
    DWORD read;
    while (WinHttpReadData(hRequest, buf, sizeof(buf), &read) && read > 0)
      result.append(buf, read);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
  }

  // WinHTTP POST with JSON body - used for OpenShock API endpoints
  static std::string postJsonWinHttp(
      const std::string& url, const std::string& body,
      const std::vector<std::string>& extraHeaders = {}) {
    HINTERNET hSession = WinHttpOpen(
        L"ShockerLink/" APP_VERSION, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    DWORD disableCookies = WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(hSession, WINHTTP_OPTION_DISABLE_FEATURE, &disableCookies,
                     sizeof(disableCookies));

    URL_COMPONENTSA uc{};
    uc.dwStructSize = sizeof(uc);
    char host[256] = {}, path[512] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = sizeof(host);
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = sizeof(path);
    if (!InternetCrackUrlA(url.c_str(), 0, 0, &uc)) {
      WinHttpCloseHandle(hSession);
      return "";
    }

    HINTERNET hConnect = WinHttpConnect(
        hSession, std::wstring(host, host + uc.dwHostNameLength).c_str(),
        uc.nPort, 0);
    if (!hConnect) {
      WinHttpCloseHandle(hSession);
      return "";
    }

    DWORD flags = WINHTTP_FLAG_REFRESH;
    if (url.rfind("https://", 0) == 0) flags |= WINHTTP_FLAG_SECURE;

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"POST",
        std::wstring(path, path + uc.dwUrlPathLength).c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
      WinHttpCloseHandle(hConnect);
      WinHttpCloseHandle(hSession);
      return "";
    }

    std::string hdrStr = "Content-Type: application/json\r\n";
    for (auto& h : extraHeaders) hdrStr += h + "\r\n";
    std::wstring whdr(hdrStr.begin(), hdrStr.end());
    WinHttpAddRequestHeaders(hRequest, whdr.c_str(), (DWORD)whdr.size(),
                             WINHTTP_ADDREQ_FLAG_ADD);

    DWORD timeout = 5000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout,
                     sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SEND_TIMEOUT, &timeout,
                     sizeof(timeout));
    WinHttpSetOption(hRequest, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout,
                     sizeof(timeout));

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            (LPVOID)body.c_str(), (DWORD)body.size(),
                            (DWORD)body.size(), 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
      WinHttpCloseHandle(hRequest);
      WinHttpCloseHandle(hConnect);
      WinHttpCloseHandle(hSession);
      return "";
    }

    std::string result;
    char buf[4096];
    DWORD read;
    while (WinHttpReadData(hRequest, buf, sizeof(buf), &read) && read > 0)
      result.append(buf, read);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
  }

  // PiShock WebSocket send
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
      if (serial.openDevice(settings.serialPort.c_str(), settings.baudRate) !=
          1)
        continue;

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
            auto json = nlohmann::json::parse(response.substr(14), nullptr,
                                              false);  // strip "TERMINALINFO: "
            if (!json.is_discarded() && json.contains("shockers"))
              for (auto& s : json["shockers"])
                settings.pushShockerId(std::to_string(s["id"].get<int>()));
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
      if (serial.openDevice(settings.serialPort.c_str(), settings.baudRate) !=
          1)
        continue;

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

  // Does all the repetitive logic
  void workerLoop() {
    std::mt19937 rng((unsigned)std::chrono::high_resolution_clock::now()
                         .time_since_epoch()
                         .count());
    while (!stopWorker) {
      std::unique_lock<std::mutex> lock(queueMutex);
      queueCV.wait(lock, [this] { return !shockQueue.empty() || stopWorker; });
      if (stopWorker) break;

      auto item = shockQueue.front();
      shockQueue.pop();
      lock.unlock();

      // Ignore shocks on panic button
      if (shocksDisabled) {
        logMsg("[ShockerHub] Shocks disabled, ignoring\n");
        continue;
      }

      int durationMs = std::get<0>(item).value_or(-1);
      bool useUpperHalf = std::get<1>(item);
      bool vibrate = std::get<2>(item);

      if (settings.cooldownEnabled) {
        double now = getCurrentTime();

        // Remove recorded shocks after cooldown window
        int cooldownWindowS = settings.cooldownWindow;
        shockTimestamps.erase(
            std::remove_if(shockTimestamps.begin(), shockTimestamps.end(),
                           [now, cooldownWindowS](double t) {
                             return now - t > cooldownWindowS;
                           }),
            shockTimestamps.end());

        // Calculate how many shocks there were in the last x(window) seconds
        double dynamicCooldown = std::min(
            (double)settings.baseCooldown +
                (double)settings.cooldownFactor * (int)shockTimestamps.size(),
            (double)settings.maxCooldown);

        // Calculate remaining cooldown
        double remaining = dynamicCooldown - (now - lastTriggerTimeAtomic);
        activeCooldownDuration.store(dynamicCooldown);
        if (remaining > 0) {
          std::string cooldownMsg =
              fmt::format("[ShockerHub] On cooldown: {:.1f}s", remaining);
          logMsg("{}\n", cooldownMsg);
          chatbox.send(cooldownMsg);

          // Record to stats
          gStats.recordCooldownHit();
          continue;
        }
      }

      std::vector<std::string> ids;
      {
        std::lock_guard<std::mutex> lock(queueMutex);
        ids = settings.shockerIDs;
      }

      if (ids.empty()) {
        logMsg("[ShockerHub] No shocker IDs configured, dropping shock\n");
        continue;
      }

      // Choose shocker depending on settings
      std::string chosenShocker;
      if (settings.randomOrSeq) {
        lastShockerIndex = (lastShockerIndex + 1) % (int)ids.size();
        chosenShocker = ids[lastShockerIndex];
      } else {
        std::uniform_int_distribution<int> idxDist(0, (int)ids.size() - 1);
        chosenShocker = ids[idxDist(rng)];
      }

      // Calculate duration if no duration is set yet
      // Duration is only set already when shocks are being re-sent
      if (durationMs == -1) {
        std::uniform_real_distribution<float> durDist(
            settings.minShockDuration,
            std::nextafter(settings.maxShockDuration,
                           std::numeric_limits<float>::infinity()));
        durationMs = std::max(100, (int)(durDist(rng) * 1000));
      }

      int intensity = useUpperHalf ? sampleIntensityUpperHalf(curvePoints)
                                   : sampleIntensity(curvePoints);
      sendShock(durationMs, intensity, chosenShocker, vibrate);
    }
  }

  void sendShock(int durationMs, int strength, const std::string& shockerID,
                 bool vibrate) {
    if (settings.useSerial)
      sendShockSerial(durationMs, strength, shockerID, vibrate);
    else
      sendShockApi(durationMs, strength, shockerID, vibrate);
  }

  // Stuff to do after the shock was sent
  // (Send chat message, handle cooldown math, send notifications, record shock)
  void afterShockSent(int durationMs, int strength, const std::string& opType) {
    shockTimestamps.push_back(getCurrentTime());
    lastTriggerTimeAtomic = getCurrentTime();

    if (settings.cooldownEnabled) {
      double now = lastTriggerTimeAtomic;
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

    if (opType == "vibrate" || opType == "Vibrate")
      gStats.recordVibration(durationMs);
    else
      gStats.recordShock(durationMs, strength);
    gStats.save("stats.json");
    logMsg("[ShockerHub] Sent {}: {}%, {:.1f}s\n", opType, strength,
           durationMs / 1000.0f);
  }

  void sendShockSerial(int durationMs, int strength,
                       const std::string& shockerID, bool vibrate) {
    std::string opType = vibrate ? "vibrate" : "shock";
    std::string command;

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

    // Re-send shock if serial write failed
    if (serial.writeString(command.c_str()) <= 0) {
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
        if (!pishockResolved && !resolvePiShockApi()) {
          logMsg("[ShockerHub] PiShock API setup failed\n");
          return;
        }

        int id = -1;
        try {
          id = std::stoi(shockerID);
        } catch (...) {
        }
        if (id == -1) {
          logMsg("[ShockerHub] Invalid shocker ID: {}\n", shockerID);
          return;
        }

        auto it = pishockShockerToClient.find(id);
        if (it == pishockShockerToClient.end()) {
          logMsg("[ShockerHub] Shocker {} not in device list, re-resolving\n",
                 id);
          pishockResolved = false;
          if (!resolvePiShockApi()) return;
          it = pishockShockerToClient.find(id);
          if (it == pishockShockerToClient.end()) {
            logMsg(
                "[ShockerHub] Shocker {} not found after re-resolve. "
                "Ensure the ID matches your PiShock dashboard.\n",
                id);
            return;
          }
        }

        if (!sendPiShockWs(durationMs, strength, id, it->second, vibrate)) {
          pishockResolved = false;
          return;
        }

      } else {
        if (settings.shockerIDs.empty() && !resolveOpenShockApi()) {
          logMsg("[ShockerHub] OpenShock shocker resolution failed\n");
          return;
        }

        std::string type = vibrate ? "Vibrate" : "Shock";
        nlohmann::json payload = {
            {"shocks", nlohmann::json::array({{{"id", shockerID},
                                               {"type", type},
                                               {"intensity", strength},
                                               {"duration", durationMs},
                                               {"exclusive", true}}})},
            {"customName", "ShockerLink"}};

        std::string serverUrl = settings.openshockServerUrl;
        if (serverUrl.starts_with("https://"))
          serverUrl = serverUrl.substr(8);
        else if (serverUrl.starts_with("http://"))
          serverUrl = serverUrl.substr(7);
        if (!serverUrl.empty() && serverUrl.back() == '/') serverUrl.pop_back();

        auto resp = postJsonWinHttp(
            "https://" + serverUrl + "/2/shockers/control", payload.dump(),
            {"openshocktoken: " + settings.openshockApiToken});

        if (resp.empty()) {
          logMsg("[OpenShock] API: no response (check token or network)\n");
          return;
        }

        auto respJson = nlohmann::json::parse(resp, nullptr, false);
        if (!respJson.is_discarded() && respJson.contains("message") &&
            !respJson.contains("data")) {
          logMsg("[OpenShock] API error: {}\n",
                 respJson.value("message", resp));
          return;
        }
      }
    } catch (const std::exception& e) {
      logMsg("[ShockerHub] API send error: {}\n", e.what());
      return;
    }

    afterShockSent(durationMs, strength, opType);
  }
};