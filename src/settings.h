#pragma once

#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "config/device_config.h"
#include "config/parameter.h"
#include "config/preset.h"
#include "config/shock_config.h"
#include "config/ui_config.h"
#include "imgui.h"

class Settings {
 public:
  // Window
  int windowX = 100;
  int windowY = 100;
  int windowW = 750;
  int windowH = 520;

  bool showStats = false;

  // Preset state
  int defaultPreset = -1;
  float minShockDuration = 1.f;
  float maxShockDuration = 2.f;
  float xViewMin = 0.f;
  float xViewMax = 100.f;

  std::vector<std::optional<SavedPreset>> presets;

  // Curves
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
  std::string pishockUsername = "";
  std::string pishockApiKey = "";
  std::string openshockApiToken = "";
  std::string openshockServerUrl = "api.openshock.app";

  // Cooldown Config
  int baseCooldown = 2;
  int maxCooldown = 6;
  float cooldownFactor = 0.4f;
  int cooldownWindow = 30;
  bool cooldownEnabled = true;

  // Panic hotkey
  int hotkeyVk = 298;
  int hotkeyMods = 0;

  // Notification Config
  bool notificationsEnabled = false;
  bool notifUseOvrToolkit = false;

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

  Settings();
  explicit Settings(const std::string& path);

  void pushShockerId(const std::string& id);

  static std::string makeOscPath(const std::string& name);
  std::vector<std::string> getParameterPaths() const;

  DeviceConfig deviceConfig() const;
  ShockConfig shockConfig() const;
  UiConfig uiConfig() const;

  void applyDeviceConfig(const DeviceConfig& cfg);
  void applyShockConfig(const ShockConfig& cfg);
  void applyUiConfig(const UiConfig& cfg);

  void save(const std::string& path);
  nlohmann::json toJson() const;

  bool operator==(const Settings& other) const;
  bool operator!=(const Settings& other) const;

 private:
  static nlohmann::json saveColor(const ImVec4& c);
  static ImVec4 loadColor(const nlohmann::json& j, const std::string& key,
                          const ImVec4& fallback);
};
