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

struct Preset {
  std::string name;
  float minShockDuration = 1.f;
  float maxShockDuration = 2.f;
  std::array<CurvePoint, 3> curvePoints = {
      {{20.0, 0.8}, {50.0, 0.5}, {80.0, 0.2}}};
  float xViewMin = 0.f;
  float xViewMax = 100.f;
};

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
  std::vector<std::optional<Preset>> presets;
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
  int hotkeyVk = VK_F9;
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

  // Constants
  static constexpr int baudRate = 115200;
  static constexpr int oscPort = 39570;
  static constexpr std::string_view serviceName = "ShockerLink";

  void pushShockerId(const std::string& id) { shockerIDs.push_back(id); }

  Settings() : presets(3) {}

  Settings(const std::string& path) : presets(3) {
    try {
      std::ifstream file(path);
      if (!file.is_open()) return;
      nlohmann::json j = nlohmann::json::parse(file);

      windowX = j.value("windowX", 100);
      windowY = j.value("windowY", 100);
      windowW = j.value("windowW", 750);
      windowH = j.value("windowH", 520);

      showStats = j.value("showStats", false);

      defaultPreset = j.value("defaultPreset", -1);
      lastSerialPort = j.value("lastSerialPort", "");

      // Serial Config
      shockParameter = j.value("shockParameter", "Shock");
      secondShockParameter = j.value("secondShockParameter", "");
      usePishock = j.value("usePishock", true);
      shockerIDs = j.value("shockerIDs", std::vector<std::string>{});
      randomOrSeq = j.value("randomOrSeq", false);
      serialPort = j.value("serialPort", "");

      // Connection Mode
      useSerial = j.value("useSerial", true);
      pishockUsername = j.value("pishockUsername", "");
      pishockApiKey = j.value("pishockApiKey", "");
      openshockApiToken = j.value("openshockApiToken", "");
      openshockServerUrl = j.value("openshockServerUrl", "api.openshock.app");

      // Cooldown Config
      baseCooldown = j.value("baseCooldown", 2);
      maxCooldown = j.value("maxCooldown", 6);
      cooldownFactor = j.value("cooldownFactor", 0.4f);
      cooldownWindow = j.value("cooldownWindow", 30);
      cooldownEnabled = j.value("cooldownEnabled", true);

      // Panic hotkey
      hotkeyVk = j.value("hotkeyVk", VK_F9);
      hotkeyMods = j.value("hotkeyMods", 0);

      // Notification Config (migrate if old values are present)
      {
        bool oldXs = j.value("xsoverlayNotifications", false);
        bool oldOvr = j.value("ovrToolkitNotifications", false);
        notificationsEnabled = j.value("notificationsEnabled", oldXs || oldOvr);
        notifUseOvrToolkit = j.value("notifUseOvrToolkit", oldOvr);
      }

      // Style Config
      presetCount = j.value("presetCount", 3);
      touchSelectThreshold = j.value("touchSelectThreshold", 8.f);
      touchMarkerSize = j.value("touchMarkerSize", 140.f);
      lineWidth = j.value("lineWidth", 3.f);
      backgroundColor = loadColor(j, "backgroundColor", backgroundColor);
      outsideCurveBg = loadColor(j, "outsideCurveBg", outsideCurveBg);
      accentColor = loadColor(j, "accentColor", accentColor);
      curveLineColor = loadColor(j, "curveLineColor", curveLineColor);
      markerColor = loadColor(j, "markerColor", markerColor);
      labelColor = loadColor(j, "labelColor", labelColor);
      gradientLeftColor = loadColor(j, "gradientLeftColor", gradientLeftColor);
      gradientRightColor =
          loadColor(j, "gradientRightColor", gradientRightColor);

      // VRChat Config
      vrchatHost = j.value("vrchatHost", "127.0.0.1");

      // Resize and load presets
      presets.resize(presetCount);
      if (j.contains("presets")) {
        int i = 0;
        for (auto& item : j["presets"]) {
          if (i >= (int)presets.size()) break;
          if (item.is_null()) {
            presets[i] = std::nullopt;
          } else {
            Preset p;
            p.name = item["name"].get<std::string>();
            p.minShockDuration = item["minShockDuration"].get<float>();
            p.maxShockDuration = item["maxShockDuration"].get<float>();
            p.xViewMin = item.value("xViewMin", 0.f);
            p.xViewMax = item.value("xViewMax", 100.f);
            if (item.contains("curvePoints") && item["curvePoints"].size() == 3)
              for (int k = 0; k < 3; k++)
                p.curvePoints[k] = {item["curvePoints"][k]["x"].get<double>(),
                                    item["curvePoints"][k]["y"].get<double>()};
            presets[i] = p;
          }
          i++;
        }
      }

      if (defaultPreset >= 0 && defaultPreset < (int)presets.size() &&
          presets[defaultPreset].has_value()) {
        minShockDuration = presets[defaultPreset]->minShockDuration;
        maxShockDuration = presets[defaultPreset]->maxShockDuration;
      }

    } catch (std::exception& e) {
      logMsg("Settings parse error: {}", e.what());
    }
  }

  void save(const std::string& path) {
    std::ofstream file(path);
    file << toJson().dump(2);
    logMsg("Settings saved");
  }

  nlohmann::json toJson() const {
    nlohmann::json j;

    j["windowX"] = windowX;
    j["windowY"] = windowY;
    j["windowW"] = windowW;
    j["windowH"] = windowH;

    j["showStats"] = showStats;

    j["defaultPreset"] = defaultPreset;
    j["lastSerialPort"] = lastSerialPort;

    j["shockParameter"] = shockParameter;
    j["secondShockParameter"] = secondShockParameter;
    j["usePishock"] = usePishock;
    j["shockerIDs"] = shockerIDs;
    j["randomOrSeq"] = randomOrSeq;
    j["serialPort"] = serialPort;

    j["useSerial"] = useSerial;
    j["pishockUsername"] = pishockUsername;
    j["pishockApiKey"] = pishockApiKey;
    j["openshockApiToken"] = openshockApiToken;
    j["openshockServerUrl"] = openshockServerUrl;

    j["baseCooldown"] = baseCooldown;
    j["maxCooldown"] = maxCooldown;
    j["cooldownFactor"] = cooldownFactor;
    j["cooldownWindow"] = cooldownWindow;
    j["cooldownEnabled"] = cooldownEnabled;

    j["hotkeyVk"] = hotkeyVk;
    j["hotkeyMods"] = hotkeyMods;

    j["notificationsEnabled"] = notificationsEnabled;
    j["notifUseOvrToolkit"] = notifUseOvrToolkit;

    j["presetCount"] = presetCount;
    j["touchSelectThreshold"] = touchSelectThreshold;
    j["touchMarkerSize"] = touchMarkerSize;
    j["lineWidth"] = lineWidth;
    j["backgroundColor"] = saveColor(backgroundColor);
    j["outsideCurveBg"] = saveColor(outsideCurveBg);
    j["accentColor"] = saveColor(accentColor);
    j["curveLineColor"] = saveColor(curveLineColor);
    j["markerColor"] = saveColor(markerColor);
    j["labelColor"] = saveColor(labelColor);
    j["gradientLeftColor"] = saveColor(gradientLeftColor);
    j["gradientRightColor"] = saveColor(gradientRightColor);

    j["vrchatHost"] = vrchatHost;

    j["presets"] = nlohmann::json::array();
    for (auto& p : presets) {
      if (!p.has_value()) {
        j["presets"].push_back(nullptr);
        continue;
      }
      nlohmann::json pts = nlohmann::json::array();
      for (auto& cp : p->curvePoints) pts.push_back({{"x", cp.x}, {"y", cp.y}});
      j["presets"].push_back({
          {"name", p->name},
          {"minShockDuration", p->minShockDuration},
          {"maxShockDuration", p->maxShockDuration},
          {"curvePoints", pts},
          {"xViewMin", p->xViewMin},
          {"xViewMax", p->xViewMax},
      });
    }

    return j;
  }

  bool operator==(const Settings& other) const {
    return windowX == other.windowX && windowY == other.windowY &&
           windowW == other.windowW && windowH == other.windowH &&
           showStats == other.showStats &&
           defaultPreset == other.defaultPreset &&
           minShockDuration == other.minShockDuration &&
           maxShockDuration == other.maxShockDuration &&
           xViewMin == other.xViewMin && xViewMax == other.xViewMax &&
           shockParameter == other.shockParameter &&
           secondShockParameter == other.secondShockParameter &&
           usePishock == other.usePishock && shockerIDs == other.shockerIDs &&
           randomOrSeq == other.randomOrSeq && serialPort == other.serialPort &&
           useSerial == other.useSerial &&
           pishockUsername == other.pishockUsername &&
           pishockApiKey == other.pishockApiKey &&
           openshockApiToken == other.openshockApiToken &&
           openshockServerUrl == other.openshockServerUrl &&
           baseCooldown == other.baseCooldown &&
           maxCooldown == other.maxCooldown &&
           cooldownFactor == other.cooldownFactor &&
           cooldownWindow == other.cooldownWindow &&
           cooldownEnabled == other.cooldownEnabled &&
           hotkeyVk == other.hotkeyVk && hotkeyMods == other.hotkeyMods &&
           notificationsEnabled == other.notificationsEnabled &&
           notifUseOvrToolkit == other.notifUseOvrToolkit &&
           presetCount == other.presetCount &&
           touchSelectThreshold == other.touchSelectThreshold &&
           touchMarkerSize == other.touchMarkerSize &&
           lineWidth == other.lineWidth && vrchatHost == other.vrchatHost &&
           presets.size() == other.presets.size();
  }

  bool operator!=(const Settings& other) const { return !(*this == other); }

 private:
  static nlohmann::json saveColor(const ImVec4& c) {
    return {c.x, c.y, c.z, c.w};
  }
  static ImVec4 loadColor(const nlohmann::json& j, const std::string& key,
                          const ImVec4& fallback) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() < 4)
      return fallback;
    return {j[key][0].get<float>(), j[key][1].get<float>(),
            j[key][2].get<float>(), j[key][3].get<float>()};
  }
};