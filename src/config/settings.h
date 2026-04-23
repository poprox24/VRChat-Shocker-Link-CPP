#pragma once

#include <array>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "curve.h"
#include "imgui.h"
#include "logger.h"

#ifdef _WIN32
#include <windows.h>
#endif

struct Preset {
  std::string name;
  float minShockDuration = 1.f;
  float maxShockDuration = 2.f;
  std::array<CurvePoint, 3> curvePoints = {
      {{20.0, 0.8}, {50.0, 0.5}, {80.0, 0.2}}};
  float xViewMin = 0.f;
  float xViewMax = 100.f;
};

struct SavedPreset {
  std::string name;
  std::vector<Preset> curves;
  int activeCurveIndex = 0;
};

enum class CurveRange { Full = 0, FirstHalf = 1, SecondHalf = 2 };

struct Parameter {
  std::string name = "Shock";
  int curveIndex = 0;
  CurveRange range = CurveRange::Full;
  std::vector<std::string> shockerIDs;
  bool randomOrSeq = false;
};

inline bool operator==(const Parameter& a, const Parameter& b) {
  return a.name == b.name && a.curveIndex == b.curveIndex &&
         a.range == b.range && a.shockerIDs == b.shockerIDs &&
         a.randomOrSeq == b.randomOrSeq;
}

class Settings {
 public:
  // Window
  int windowX = 100, windowY = 100;
  int windowW = 750, windowH = 520;

  bool showStats = false;

  // Preset state
  int defaultPreset = -1;
  float minShockDuration = 1.f;
  float maxShockDuration = 2.f;
  float xViewMin = 0.f;
  float xViewMax = 100.f;

  std::vector<std::optional<SavedPreset>> presets;

  // Curves - dynamic list of bezier curves
  std::vector<Preset> curves;

  std::string lastSerialPort;

  // Serial Config
  std::string shockParameter = "Shock";
  std::string secondShockParameter = "";
  bool usePishock = true;
  std::vector<std::string> shockerIDs = {};
  bool randomOrSeq = false;
  std::string serialPort = "";

  // Connection Mode
  bool useSerial = true;
  // PiShock API credentials
  std::string pishockUsername = "";
  std::string pishockApiKey = "";
  // OpenShock API credentials
  std::string openshockApiToken = "";
  std::string openshockServerUrl = "api.openshock.app";

  // Cooldown Config
  int baseCooldown = 2;
  int maxCooldown = 6;
  float cooldownFactor = 0.4f;
  int cooldownWindow = 30;
  bool cooldownEnabled = true;

  // Panic hotkey
  int hotkeyVk = 298;  // GLFW_KEY_F9
  int hotkeyMods = 0;  // Alt = 1, Control = 2, Shift = 4

  // Notification Config
  bool notificationsEnabled = false;
  bool notifUseOvrToolkit = false;  // false = XSOverlay, true = OVRToolkit

  // Style Config
  int presetCount = 3;
  float touchSelectThreshold = 8.f;
  float touchMarkerSize = 140.f;
  float lineWidth = 3.f;
  ImVec4 outsideCurveBg = {0.116f, 0.116f, 0.116f, 1.f};
  ImVec4 backgroundColor = {0.116f, 0.116f, 0.116f, 1.f};
  ImVec4 accentColor = {0.282f, 0.282f, 0.282f, 1.f};
  ImVec4 curveLineColor = {0.112f, 0.788f, 1.f, 1.f};
  ImVec4 markerColor = {0.847f, 0.541f, 0.569f, 1.f};
  ImVec4 labelColor = {0.902f, 0.933f, 0.965f, 1.f};
  ImVec4 gradientLeftColor = {0.259f, 0.584f, 0.231f, 1.f};
  ImVec4 gradientRightColor = {0.431f, 0.090f, 0.231f, 1.f};

  // VRChat Config
  std::string vrchatHost = "127.0.0.1";
  bool chatboxShockEnabled = true;
  bool chatboxCooldownEnabled = true;

  std::vector<Parameter> parameters = {Parameter()};

  // Constants
  static constexpr int baudRate = 115200;
  static constexpr int oscPort = 39570;
  static constexpr std::string_view serviceName = "ShockerLink";

  void pushShockerId(const std::string& id);
  static std::string makeOscPath(const std::string& name);
  std::vector<std::string> getParameterPaths() const;

  Settings();
  Settings(const std::string& path);

  void save(const std::string& path);
  nlohmann::json toJson() const;

  bool operator==(const Settings& other) const;
  bool operator!=(const Settings& other) const;

 private:
  static nlohmann::json saveColor(const ImVec4& c);
  static ImVec4 loadColor(const nlohmann::json& j, const std::string& key,
                          const ImVec4& fallback);
};