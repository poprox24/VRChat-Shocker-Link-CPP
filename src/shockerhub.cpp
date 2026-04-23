#include "shockerhub.h"

ShockerHub::ShockerHub(Settings& set)
    : settings(set), chatbox(set.vrchatHost) {}

bool ShockerHub::connectSerial() {
  if (!settings.useSerial) {
    isConnected = true;
    startWorkerThread();
    return true;
  }

  logMsg("[ShockerHub] Attempting to connect to the shocker hub");

  if (!settings.serialPort.empty()) {
    bool opened =
        serial.openDevice(settings.serialPort.c_str(), settings.baudRate) == 1;
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

void ShockerHub::queueShock(int duration, bool vibrate) {
  {
    std::lock_guard<std::mutex> lock(queueMutex);
    shockQueue.push(
        {duration == -1 ? std::nullopt : std::optional<int>(duration), -1,
         CurveRange::Full, vibrate});
  }
  queueCV.notify_one();
}

void ShockerHub::queueShockUpperHalf(int duration, bool vibrate) {
  {
    std::lock_guard<std::mutex> lock(queueMutex);
    shockQueue.push(
        {duration == -1 ? std::nullopt : std::optional<int>(duration), -1,
         CurveRange::SecondHalf, vibrate});
  }
  queueCV.notify_one();
}

void ShockerHub::queueShockFor(int parameterIndex, int duration, bool vibrate) {
  CurveRange range = CurveRange::Full;
  if (parameterIndex >= 0 && parameterIndex < (int)settings.parameters.size())
    range = settings.parameters[parameterIndex].range;
  {
    std::lock_guard<std::mutex> lock(queueMutex);
    shockQueue.push(
        {duration == -1 ? std::nullopt : std::optional<int>(duration),
         parameterIndex, range, vibrate});
  }
  queueCV.notify_one();
}

void ShockerHub::emptyQueue() {
  std::lock_guard<std::mutex> lock(queueMutex);
  decltype(shockQueue) emptyQ;
  shockQueue.swap(emptyQ);
}

bool ShockerHub::reconnectSerial() {
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

bool ShockerHub::tryReconnect() {
  if (!settings.useSerial) return true;
  serial.closeDevice();
  settings.serialPort = "";
  return connectSerial();
}

bool ShockerHub::listShockers() {
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

void ShockerHub::enableShocks() {
  shocksDisabled = false;
  logMsg("[ShockerHub] Shocks re-enabled");
}

void ShockerHub::shutdown() {
  stopWorker = true;
  queueCV.notify_one();
  if (workerThread.joinable()) workerThread.join();
  serial.closeDevice();
}

double ShockerHub::getCurrentTime() {
  return std::chrono::duration<double>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

bool ShockerHub::resolveOpenShockApi() {
  std::string serverUrl = settings.openshockServerUrl;
  if (serverUrl.starts_with("https://"))
    serverUrl = serverUrl.substr(8);
  else if (serverUrl.starts_with("http://"))
    serverUrl = serverUrl.substr(7);
  if (!serverUrl.empty() && serverUrl.back() == '/') serverUrl.pop_back();

  std::string url = "https://" + serverUrl + "/1/shockers/own";
  auto resp = winHttpRequest("GET", url, "",
                             {"openshocktoken: " + settings.openshockApiToken});
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
  {
    std::lock_guard<std::mutex> lock(queueMutex);
    if (settings.shockerIDs.empty()) {
      for (auto& s : j["data"])
        if (s.contains("id"))
          settings.pushShockerId(s["id"].get<std::string>());
      logMsg("[OpenShock] Auto-populated {} shocker ID(s): {}\n",
             settings.shockerIDs.size(), fmt::join(settings.shockerIDs, ", "));
    }
  }

  logMsg("[OpenShock] Resolved {} shocker(s)\n", j["data"].size());
  return true;
}

// Connect to PiShock API
// Handles Authenticating and getting shocker IDs
bool ShockerHub::resolvePiShockApi() {
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
  pishockUserId_ = authJson.contains("UserId") ? authJson["UserId"].get<int>()
                                               : authJson["UserID"].get<int>();
  logMsg("[PiShock] Authenticated as userId {}\n", pishockUserId_);

  // Get all owned devices to map shockerId -> clientId
  std::string devUrl = "https://ps.pishock.com/PiShock/GetUserDevices?UserId=" +
                       std::to_string(pishockUserId_) +
                       "&Token=" + settings.pishockApiKey + "&api=true";
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

  {
    std::lock_guard<std::mutex> lock(queueMutex);
    if (settings.shockerIDs.empty()) {
      for (auto& [sid, cid] : pishockShockerToClient)
        settings.pushShockerId(std::to_string(sid));
      logMsg("[PiShock] Auto-populated {} shocker ID(s): {}\n",
             settings.shockerIDs.size(), fmt::join(settings.shockerIDs, ", "));
    }
  }

  pishockResolved = true;
  return true;
}

// ADDED: curl write callback
size_t ShockerHub::curlWrite(void* ptr, size_t sz, size_t n, std::string* out) {
  out->append(static_cast<char*>(ptr), sz * n);
  return sz * n;
}

std::string ShockerHub::httpGet(const std::string& url,
                                const std::vector<std::string>& extraHeaders) {
  CURL* c = curl_easy_init();
  if (!c) return "";
  std::string result;
  curl_slist* hdrs = nullptr;
  for (auto& h : extraHeaders) hdrs = curl_slist_append(hdrs, h.c_str());
  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWrite);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &result);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 5L);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(c, CURLOPT_SSL_OPTIONS,
                   CURLSSLOPT_NATIVE_CA);  // ADDED: use Windows cert store
  if (hdrs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
  CURLcode res = curl_easy_perform(c);  // CHANGED: capture return value
  if (res != CURLE_OK)                  // ADDED: log failures
    logMsg("[HTTP] GET failed: {}\n", curl_easy_strerror(res));
  if (hdrs) curl_slist_free_all(hdrs);
  curl_easy_cleanup(c);
  return result;
}

std::string ShockerHub::winHttpRequest(
    const std::string& method, const std::string& url, const std::string& body,
    const std::vector<std::string>& extraHeaders) {
  CURL* c = curl_easy_init();
  if (!c) return "";
  std::string result;
  curl_slist* hdrs = nullptr;
  hdrs = curl_slist_append(hdrs, "Accept: application/json");
  if (!body.empty())
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
  for (auto& h : extraHeaders) hdrs = curl_slist_append(hdrs, h.c_str());
  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWrite);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &result);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 5L);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(c, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
  curl_easy_setopt(
      c, CURLOPT_USERAGENT,
      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
  if (hdrs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
  if (method == "POST") {
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body.size());
  }
  CURLcode res = curl_easy_perform(c);
  if (res != CURLE_OK)
    logMsg("[HTTP] {} {} failed: {}\n", method, url, curl_easy_strerror(res));
  if (hdrs) curl_slist_free_all(hdrs);
  curl_easy_cleanup(c);
  return result;
}

bool ShockerHub::sendPiShockWs(int durationMs, int strength, int shockerId,
                               int clientId, bool vibrate) {
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
  nlohmann::json cmd = {{"Operation", "PUBLISH"},
                        {"PublishCommands",
                         {{{"Target", "c" + std::to_string(clientId) + "-ops"},
                           {"Body", body}}}}};
  std::string msgStr = cmd.dump();

  CURL* c = curl_easy_init();
  if (!c) {
    logMsg("[PiShock] curl init failed");
    return false;
  }

  char* eu = curl_easy_escape(c, settings.pishockUsername.c_str(), 0);
  char* ek = curl_easy_escape(c, settings.pishockApiKey.c_str(), 0);
  std::string url = std::string("wss://broker.pishock.com/v2?Username=") + eu +
                    "&ApiKey=" + ek;
  curl_free(eu);
  curl_free(ek);

  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_CONNECT_ONLY, 2L);  // WebSocket mode
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 3L);

  CURLcode res = curl_easy_perform(c);
  if (res != CURLE_OK) {
    logMsg("[PiShock] WS connect failed: {}", curl_easy_strerror(res));
    curl_easy_cleanup(c);
    return false;
  }

  size_t sent = 0;
  res = curl_ws_send(c, msgStr.c_str(), msgStr.size(), &sent, 0, CURLWS_TEXT);
  if (res != CURLE_OK) {
    logMsg("[PiShock] WS send failed: {}", curl_easy_strerror(res));
    curl_easy_cleanup(c);
    return false;
  }

  char rbuf[4096] = {};
  size_t rlen = 0;
  const struct curl_ws_frame* frame = nullptr;
  res = curl_ws_recv(c, rbuf, sizeof(rbuf) - 1, &rlen, &frame);
  bool ok = true;
  if (res == CURLE_OK && rlen > 0) {
    auto rj = nlohmann::json::parse(std::string(rbuf, rlen), nullptr, false);
    if (!rj.is_discarded() && rj.value("IsError", false)) {
      logMsg("[PiShock] Broker error: {}",
             rj.value("Message", std::string("?")));
      ok = false;
    }
  }

  curl_ws_send(c, "", 0, &sent, 0, CURLWS_CLOSE);
  curl_easy_cleanup(c);
  return ok;
}

std::vector<std::string> ShockerHub::serialCandidates() {
#ifdef _WIN32
  std::vector<std::string> ports;
  for (int i = 1; i <= 24; i++) ports.push_back("COM" + std::to_string(i));
  return ports;
#else
  std::vector<std::string> ports;
  for (int i = 0; i < 20; i++) {
    ports.push_back("/dev/ttyUSB" + std::to_string(i));
    ports.push_back("/dev/ttyACM" + std::to_string(i));
  }
  return ports;
#endif
}

bool ShockerHub::scanForPishock() {
  for (auto& port : serialCandidates()) {
    settings.serialPort = port;
    if (serial.openDevice(settings.serialPort.c_str(), settings.baudRate) != 1)
      continue;
    serial.writeString("{\"cmd\": \"info\"}\n");
    bool found = false;
    for (int attempt = 0; attempt < 20; attempt++) {
      char buf[1024] = {0};
      serial.readString(buf, '\n', 1024, 500);
      std::string response(buf);
      if (response.starts_with("TERMINALINFO: ") &&
          response.find("pishock") != std::string::npos) {
        found = true;
        if (settings.shockerIDs.empty()) {
          auto json =
              nlohmann::json::parse(response.substr(14), nullptr, false);
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

bool ShockerHub::scanForOpenshock() {
  for (auto& port : serialCandidates()) {
    settings.serialPort = port;
    if (serial.openDevice(settings.serialPort.c_str(), settings.baudRate) != 1)
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
      "press reconnect.\n");
  return false;
}

void ShockerHub::startWorkerThread() {
  settings.lastSerialPort = settings.serialPort;
  isConnected = true;
  if (!workerThread.joinable()) {
    if (settings.useSerial)
      logMsg("[ShockerHub] Connected on {}\n", settings.serialPort);
    workerThread = std::thread([this]() { workerLoop(); });
  }
}

// Does all the repetitive logic
void ShockerHub::workerLoop() {
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

    int durationMs = item.duration.value_or(-1);
    bool vibrate = item.vibrate;
    int parameterIndex = item.parameterIndex;
    CurveRange range = item.range;

    if (settings.cooldownEnabled) {
      double now = getCurrentTime();

      // Remove recorded shocks after cooldown window
      int cooldownWindowS = settings.cooldownWindow;
      while (!shockTimestamps.empty() &&
             now - shockTimestamps.front() > cooldownWindowS)
        shockTimestamps.erase(shockTimestamps.begin());

      double dynamicCooldown = calcDynamicCooldown();

      // Calculate remaining cooldown
      double remaining = dynamicCooldown - (now - lastTriggerTimeAtomic);
      activeCooldownDuration.store(dynamicCooldown);
      if (remaining > 0) {
        std::string cooldownMsg =
            fmt::format("On cooldown: {:.1f}s", remaining);
        logMsg("{}\n", cooldownMsg);
        if (settings.chatboxCooldownEnabled) chatbox.send(cooldownMsg);

        // Record to stats
        gStats.recordCooldownHit();
        continue;
      }
    }

    std::vector<std::string> ids;
    bool useSeq = settings.randomOrSeq;  // Start with global fallback
    {
      std::lock_guard<std::mutex> lock(queueMutex);
      // Use per-parameter list if set, else fall back to global
      if (parameterIndex >= 0 &&
          parameterIndex < (int)settings.parameters.size()) {
        const auto& param = settings.parameters[parameterIndex];
        ids = param.shockerIDs.empty() ? settings.shockerIDs : param.shockerIDs;
        useSeq = param.randomOrSeq;
      } else {
        ids = settings.shockerIDs;
      }
      // Aggregate all per-parameter IDs (if no global shocker IDs are set)
      if (ids.empty()) {
        for (const auto& param : settings.parameters)
          for (const auto& id : param.shockerIDs)
            if (std::find(ids.begin(), ids.end(), id) == ids.end())
              ids.push_back(id);
      }
    }

    if (ids.empty()) {
      logMsg("[ShockerHub] No shocker IDs configured, dropping shock\n");
      continue;
    }

    // Choose shocker depending on settings
    std::string chosenShocker;
    if (useSeq) {
      // Per-parameter index
      int& idx = lastShockerIndexPerParam[parameterIndex];
      idx = (idx + 1) % (int)ids.size();
      chosenShocker = ids[idx];
    } else {
      std::uniform_int_distribution<int> idxDist(0, (int)ids.size() - 1);
      chosenShocker = ids[idxDist(rng)];
    }

    // Calculate duration if no duration is set yet
    // Duration is only set already when shocks are being re-sent
    // This is just a fallback if multiple curves fail
    float minD = settings.minShockDuration;
    float maxD = settings.maxShockDuration;

    // If multiple curves exist and parameter curve index is not 0 set min/max
    // duration from the correct curve
    if (parameterIndex >= 0 &&
        parameterIndex < (int)settings.parameters.size()) {
      int curveIndex = settings.parameters[parameterIndex].curveIndex;

      if (curveIndex >= 0 && curveIndex < (int)settings.curves.size()) {
        minD = settings.curves[curveIndex].minShockDuration;
        maxD = settings.curves[curveIndex].maxShockDuration;
      }
    }
    std::uniform_real_distribution<float> durDist(
        minD, std::nextafter(maxD, std::numeric_limits<float>::infinity()));

    // If shock is being re-sent, don't recalculate duration
    if (!item.duration.has_value()) {
      durationMs = std::max(100, (int)(durDist(rng) * 1000));
    } else {
      durationMs = item.duration.value();
    }

    std::array<CurvePoint, 3>* pts = &curvePoints;
    if (parameterIndex >= 0 &&
        parameterIndex < (int)settings.parameters.size()) {
      int curveIndex = settings.parameters[parameterIndex].curveIndex;
      if (curveIndex >= 0 && curveIndex < (int)settings.curves.size())
        pts = &settings.curves[curveIndex].curvePoints;
    }
    int intensity;
    if (range == CurveRange::SecondHalf)
      intensity = sampleIntensityUpperHalf(*pts, rng);
    else if (range == CurveRange::FirstHalf)
      intensity = sampleIntensityLowerHalf(*pts, rng);
    else
      intensity = sampleIntensity(*pts, rng);
    sendShock(durationMs, intensity, chosenShocker, vibrate);

    if (settings.cooldownEnabled)
      cooldownUntil.store(getCurrentTime() + calcDynamicCooldown());
  }
}

void ShockerHub::sendShock(int durationMs, int strength,
                           const std::string& shockerID, bool vibrate) {
  if (settings.useSerial)
    sendShockSerial(durationMs, strength, shockerID, vibrate);
  else
    sendShockApi(durationMs, strength, shockerID, vibrate);
}

// Stuff to do after the shock was sent
// (Send chat message, handle cooldown math, send notifications, record shock)
void ShockerHub::afterShockSent(int durationMs, int strength,
                                const std::string& opType) {
  lastTriggerTimeAtomic = getCurrentTime();
  shockTimestamps.push_back(lastTriggerTimeAtomic);

  if (!settings.cooldownEnabled) cooldownUntil.store(0.0);

  if (settings.chatboxShockEnabled)
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
  logMsg("[ShockerHub] Sent {}: {}%, {:.1f}s\n", opType, strength,
         durationMs / 1000.0f);
}

void ShockerHub::sendShockSerial(int durationMs, int strength,
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
    queueShock(durationMs, vibrate);
    return;
  }

  afterShockSent(durationMs, strength, opType);
}

double ShockerHub::calcDynamicCooldown() const {
  return std::min((double)settings.baseCooldown +
                      (double)settings.cooldownFactor *
                          std::max(0, (int)shockTimestamps.size() - 1),
                  (double)settings.maxCooldown);
}

void ShockerHub::sendShockApi(int durationMs, int strength,
                              const std::string& shockerID, bool vibrate) {
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

      auto resp = winHttpRequest(
          "POST", "https://" + serverUrl + "/2/shockers/control",
          payload.dump(), {"openshocktoken: " + settings.openshockApiToken});

      if (resp.empty()) {
        logMsg("[OpenShock] API: no response (check token or network)\n");
        return;
      }

      auto respJson = nlohmann::json::parse(resp, nullptr, false);
      if (!respJson.is_discarded() && respJson.contains("message") &&
          !respJson.contains("data")) {
        logMsg("[OpenShock] API error: {}\n", respJson.value("message", resp));
        return;
      }
    }
  } catch (const std::exception& e) {
    logMsg("[ShockerHub] API send error: {}\n", e.what());
    return;
  }

  afterShockSent(durationMs, strength, opType);
}