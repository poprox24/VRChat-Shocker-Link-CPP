#pragma once

#include <curl/curl.h>
#include <serialib.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "chatbox.h"
#include "core/shock/shock_request.h"
#include "curve.h"
#include "settings.h"

class ShockerHub {
 public:
  std::atomic<double> cooldownUntil{0.0};
  std::atomic<double> lastTriggerTimeAtomic{0.0};
  std::atomic<double> activeCooldownDuration{0.0};

  std::condition_variable queueCV;
  ChatboxSender chatbox;

  bool isConnected = false;
  std::atomic<bool> shocksDisabled{false};

  std::mutex queueMutex;
  std::array<CurvePoint, 3> curvePoints = {
      {{20.0, 0.8}, {50.0, 0.5}, {80.0, 0.2}}};

  explicit ShockerHub(Settings& set);

  bool connectSerial();
  void queueShock(int duration = -1, bool vibrate = false);
  void queueShockUpperHalf(int duration = -1, bool vibrate = false);
  void queueShockFor(int parameterIndex, int duration = -1,
                     bool vibrate = false);
  void emptyQueue();

  bool reconnectSerial();
  bool tryReconnect();
  bool listShockers();
  void enableShocks();
  void shutdown();

  double getCurrentTime();

  bool resolveOpenShockApi();
  bool resolvePiShockApi();

 private:
  Settings& settings;
  int lastShockerIndex = -1;
  std::vector<double> shockTimestamps;
  serialib serial;

  std::queue<ShockRequest> shockQueue;
  std::thread workerThread;
  std::atomic<bool> stopWorker = false;

  int pishockUserId_ = -1;
  std::unordered_map<int, int> pishockShockerToClient;
  bool pishockResolved = false;

  static size_t curlWrite(void* ptr, size_t sz, size_t n, std::string* out);

  static std::string httpGet(
      const std::string& url,
      const std::vector<std::string>& extraHeaders = {});

  static std::string winHttpRequest(
      const std::string& method, const std::string& url,
      const std::string& body = {},
      const std::vector<std::string>& extraHeaders = {});

  bool sendPiShockWs(int durationMs, int strength, int shockerId, int clientId,
                     bool vibrate);

  static std::vector<std::string> serialCandidates();
  bool scanForPishock();
  bool scanForOpenshock();

  void startWorkerThread();
  void workerLoop();

  void sendShock(int durationMs, int strength, const std::string& shockerID,
                 bool vibrate);
  void afterShockSent(int durationMs, int strength, const std::string& opType);
  void sendShockSerial(int durationMs, int strength,
                       const std::string& shockerID, bool vibrate);
  double calcDynamicCooldown() const;
  void sendShockApi(int durationMs, int strength, const std::string& shockerID,
                    bool vibrate);
};
