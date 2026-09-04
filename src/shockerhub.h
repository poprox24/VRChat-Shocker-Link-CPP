#pragma once

#include <curl/curl.h>
#include <fmt/ranges.h>
#include <serialib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "curve.h"
#include "logger.h"
#include "notifications.h"
#include "oscsender.h"
#include "session.h"
#include "settings.h"
#include "stats.h"
#include "version.h"

#ifdef _WIN32
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "winhttp.lib")
#endif

class ShockerHub {
 public:
  std::atomic<double> cooldownUntil{0.0};
  std::atomic<double> lastTriggerTimeAtomic{0.0};
  std::atomic<double> activeCooldownDuration{0.0};

  std::condition_variable queueCV;

  OscSender oscSender;

  static constexpr std::string_view kOscIntensityPct =
      "/avatar/parameters/ShockerLink_IntensityPercentage";
  static constexpr std::string_view kOscCooldownPct =
      "/avatar/parameters/ShockerLink_CooldownPercentage";
  static constexpr std::string_view kOscDurationSecs =
      "/avatar/parameters/ShockerLink_DurationSeconds";

  bool isConnected = false;
  std::atomic<bool> shocksDisabled{false};

  std::mutex queueMutex;

  std::array<CurvePoint, 3> curvePoints = {
      {{20.0, 0.8}, {50.0, 0.5}, {80.0, 0.2}}};

  std::unique_ptr<SessionManager> session;
  void applyRemoteShock(int strength, int durationMs, bool vibrate);
  bool broadcastIfNeeded(int parameterIndex, int strength, int durationMs,
                         bool vibrate);

  ShockerHub(Settings& set);

  bool connectSerial();

  struct ShockRequest {
    std::optional<int> duration;
    int parameterIndex = -1;
    CurveRange range = CurveRange::Full;
    bool vibrate = false;

    // Remote/Shared shocks
    std::optional<int> forcedStrength;
    std::optional<int> forcedDurationMs;
    bool fromRemote = false;
  };

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

  // Manual "hold to shock" control, driven by the fixed SHOCK/Intensity
  // (int 1-5) and Shock/IsShocking (bool) avatar parameters. Independent of
  // the curve/cooldown-driven queue above.
  void setIntensityLevel(float level);  // raw OSC value, expected 1-5
  void setIsShocking(bool active);

 private:
  Settings& settings;
  std::unordered_map<int, int> lastShockerIndexPerParam;
  std::vector<double> shockTimestamps;
  serialib serial;

  std::queue<ShockRequest> shockQueue;
  std::thread workerThread;
  std::atomic<bool> stopWorker = false;

  int pishockUserId_ = -1;
  std::unordered_map<int, int> pishockShockerToClient;
  bool pishockResolved = false;

  static size_t curlWrite(void* ptr, size_t sz, size_t n, std::string* out);

  static std::string httpGet(const std::string& url,
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
                 bool vibrate, bool silent = false);
  void afterShockSent(int durationMs, int strength, const std::string& opType,
                      bool silent);
  void sendShockSerial(int durationMs, int strength,
                       const std::string& shockerID, bool vibrate, bool silent);
  double calcDynamicCooldown() const;
  void sendShockApi(int durationMs, int strength, const std::string& shockerID,
                    bool vibrate, bool silent);

  // Manual hold-shock state
  std::atomic<int> intensityLevel_{-1};  // 1-5, -1 = not yet received
  std::atomic<bool> continuousActive_{false};
  std::thread continuousThread_;
  std::mutex continuousMutex_;
  std::condition_variable continuousCV_;
  std::vector<std::string> pickIdsForContinuous();
  void continuousShockLoop();
  void sendStopSignal(const std::string& shockerID);

  struct VisualUpdate {
    float intensityPct;
    float durationSecs;
    double cooldownEnd;
    double cooldownDuration;
  };

  std::optional<VisualUpdate> pendingVisual;
  std::thread visualParamThread;
  std::mutex visualParamMutex_;
  std::condition_variable visualParamCV_;
  std::atomic<bool> stopVisual{false};

  void triggerVisualParams(float intensityPct, float durationSecs);
  void visualParamLoop();
};