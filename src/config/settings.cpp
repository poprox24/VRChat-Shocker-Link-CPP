#include "settings.h"

#include <exception>

#include "logger.h"

Settings::Settings() : presets(3), parameters(1) {
  curves.push_back(Preset());
  curves[0].name = "Default";
}

Settings::Settings(const std::string& path) : presets(3), parameters(1) {
  curves.push_back(Preset());
  curves[0].name = "Default";

  try {
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    if (content.empty()) return;

    nlohmann::json j = nlohmann::json::parse(content);

    windowX = j.value("windowX", 100);
    windowY = j.value("windowY", 100);
    windowW = j.value("windowW", 750);
    windowH = j.value("windowH", 520);

    showStats = j.value("showStats", false);

    defaultPreset = j.value("defaultPreset", -1);
    lastSerialPort = j.value("lastSerialPort", "");

    shockParameter = j.value("shockParameter", "Shock");
    secondShockParameter = j.value("secondShockParameter", "");
    usePishock = j.value("usePishock", true);
    shockerIDs = j.value("shockerIDs", std::vector<std::string>{});

    if (j.contains("parameters") && j["parameters"].is_array()) {
      parameters.clear();
      for (auto& item : j["parameters"]) {
        if (!item.is_object()) continue;
        Parameter p;
        p.name = item.value("name", "Shock");
        p.curveIndex = item.value("curveIndex", 0);
        p.range =
            static_cast<CurveRange>(item.value("range", (int)CurveRange::Full));
        parameters.push_back(p);
      }
      if (parameters.empty()) parameters.push_back(Parameter());
    } else {
      parameters.clear();
      if (!shockParameter.empty())
        parameters.push_back({shockParameter, 0, CurveRange::Full});
      if (!secondShockParameter.empty())
        parameters.push_back({secondShockParameter, 0, CurveRange::SecondHalf});
      if (parameters.empty()) parameters.push_back(Parameter());
    }

    randomOrSeq = j.value("randomOrSeq", false);
    serialPort = j.value("serialPort", "");

    useSerial = j.value("useSerial", true);
    pishockUsername = j.value("pishockUsername", "");
    pishockApiKey = j.value("pishockApiKey", "");
    openshockApiToken = j.value("openshockApiToken", "");
    openshockServerUrl = j.value("openshockServerUrl", "api.openshock.app");

    baseCooldown = j.value("baseCooldown", 2);
    maxCooldown = j.value("maxCooldown", 6);
    cooldownFactor = j.value("cooldownFactor", 0.4f);
    cooldownWindow = j.value("cooldownWindow", 30);
    cooldownEnabled = j.value("cooldownEnabled", true);

    hotkeyVk = j.value("hotkeyVk", 298);
    hotkeyMods = j.value("hotkeyMods", 0);

    {
      bool oldXs = j.value("xsoverlayNotifications", false);
      bool oldOvr = j.value("ovrToolkitNotifications", false);
      notificationsEnabled = j.value("notificationsEnabled", oldXs || oldOvr);
      notifUseOvrToolkit = j.value("notifUseOvrToolkit", oldOvr);
    }

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
    gradientRightColor = loadColor(j, "gradientRightColor", gradientRightColor);

    vrchatHost = j.value("vrchatHost", "127.0.0.1");
    chatboxShockEnabled = j.value("chatboxShockEnabled", true);
    chatboxCooldownEnabled = j.value("chatboxCooldownEnabled", true);

    presets.resize(presetCount);
    if (j.contains("presets")) {
      int i = 0;
      for (auto& item : j["presets"]) {
        if (i >= static_cast<int>(presets.size())) break;
        if (item.is_null()) {
          presets[i] = std::nullopt;
        } else {
          SavedPreset sp;
          sp.name = item.value("name", "Preset");
          sp.activeCurveIndex = item.value("activeCurveIndex", 0);

          if (item.contains("curves") && item["curves"].is_array()) {
            for (auto& ci : item["curves"]) {
              Preset cp;
              cp.name = ci.value("name", "Curve");
              cp.minShockDuration = ci.value("minShockDuration", 1.f);
              cp.maxShockDuration = ci.value("maxShockDuration", 2.f);
              cp.xViewMin = ci.value("xViewMin", 0.f);
              cp.xViewMax = ci.value("xViewMax", 100.f);
              if (ci.contains("curvePoints") && ci["curvePoints"].size() == 3) {
                for (int k = 0; k < 3; k++) {
                  cp.curvePoints[k] = {ci["curvePoints"][k]["x"].get<double>(),
                                       ci["curvePoints"][k]["y"].get<double>()};
                }
              }
              sp.curves.push_back(cp);
            }
          } else if (item.contains("curvePoints")) {
            Preset cp;
            cp.name = sp.name;
            cp.minShockDuration = item.value("minShockDuration", 1.f);
            cp.maxShockDuration = item.value("maxShockDuration", 2.f);
            cp.xViewMin = item.value("xViewMin", 0.f);
            cp.xViewMax = item.value("xViewMax", 100.f);
            if (item["curvePoints"].size() == 3) {
              for (int k = 0; k < 3; k++) {
                cp.curvePoints[k] = {
                    item["curvePoints"][k]["x"].get<double>(),
                    item["curvePoints"][k]["y"].get<double>()};
              }
            }
            sp.curves.push_back(cp);
          }

          if (sp.curves.empty()) {
            sp.curves.push_back(Preset());
            sp.curves[0].name = "Default";
          }
          if (sp.activeCurveIndex >= static_cast<int>(sp.curves.size())) {
            sp.activeCurveIndex = 0;
          }

          presets[i] = sp;
        }
        i++;
      }
    }

    if (j.contains("curves") && j["curves"].is_array()) {
      curves.clear();
      for (auto& item : j["curves"]) {
        if (!item.is_object()) continue;

        Preset p;
        p.name = item.value("name", "Curve");
        p.minShockDuration = item.value("minShockDuration", 1.f);
        p.maxShockDuration = item.value("maxShockDuration", 2.f);
        p.xViewMin = item.value("xViewMin", 0.f);
        p.xViewMax = item.value("xViewMax", 100.f);
        if (item.contains("curvePoints") && item["curvePoints"].size() == 3) {
          for (int k = 0; k < 3; k++) {
            p.curvePoints[k] = {item["curvePoints"][k]["x"].get<double>(),
                                item["curvePoints"][k]["y"].get<double>()};
          }
        }
        curves.push_back(p);
      }
    }

    if (curves.empty()) {
      curves.push_back(Preset());
      curves[0].name = "Default";
    }

    if (defaultPreset >= 0 && defaultPreset < static_cast<int>(presets.size()) &&
        presets[defaultPreset].has_value()) {
      auto& dp = presets[defaultPreset];
      int ai = dp->activeCurveIndex;
      if (!dp->curves.empty()) {
        if (ai >= static_cast<int>(dp->curves.size())) ai = 0;
        minShockDuration = dp->curves[ai].minShockDuration;
        maxShockDuration = dp->curves[ai].maxShockDuration;
      }
    }

  } catch (std::exception& e) {
    logMsg("Settings parse error: {}", e.what());
  }
}

void Settings::pushShockerId(const std::string& id) { shockerIDs.push_back(id); }

std::string Settings::makeOscPath(const std::string& name) {
  return std::string("/avatar/parameters/") + name;
}

std::vector<std::string> Settings::getParameterPaths() const {
  std::vector<std::string> out;
  out.reserve(parameters.size());
  for (auto& p : parameters) out.push_back(makeOscPath(p.name));
  return out;
}

DeviceConfig Settings::deviceConfig() const {
  DeviceConfig cfg;
  cfg.shockParameter = shockParameter;
  cfg.secondShockParameter = secondShockParameter;
  cfg.usePishock = usePishock;
  cfg.useSerial = useSerial;
  cfg.randomOrSeq = randomOrSeq;
  cfg.shockerIDs = shockerIDs;
  cfg.serialPort = serialPort;
  cfg.lastSerialPort = lastSerialPort;
  cfg.pishockUsername = pishockUsername;
  cfg.pishockApiKey = pishockApiKey;
  cfg.openshockApiToken = openshockApiToken;
  cfg.openshockServerUrl = openshockServerUrl;
  cfg.vrchatHost = vrchatHost;
  cfg.chatboxShockEnabled = chatboxShockEnabled;
  cfg.chatboxCooldownEnabled = chatboxCooldownEnabled;
  cfg.notificationsEnabled = notificationsEnabled;
  cfg.notifUseOvrToolkit = notifUseOvrToolkit;
  cfg.parameters = parameters;
  return cfg;
}

ShockConfig Settings::shockConfig() const {
  ShockConfig cfg;
  cfg.defaultPreset = defaultPreset;
  cfg.minShockDuration = minShockDuration;
  cfg.maxShockDuration = maxShockDuration;
  cfg.xViewMin = xViewMin;
  cfg.xViewMax = xViewMax;
  cfg.baseCooldown = baseCooldown;
  cfg.maxCooldown = maxCooldown;
  cfg.cooldownFactor = cooldownFactor;
  cfg.cooldownWindow = cooldownWindow;
  cfg.cooldownEnabled = cooldownEnabled;
  cfg.curves = curves;
  cfg.presets = presets;
  return cfg;
}

UiConfig Settings::uiConfig() const {
  UiConfig cfg;
  cfg.windowX = windowX;
  cfg.windowY = windowY;
  cfg.windowW = windowW;
  cfg.windowH = windowH;
  cfg.showStats = showStats;
  cfg.hotkeyVk = hotkeyVk;
  cfg.hotkeyMods = hotkeyMods;
  cfg.presetCount = presetCount;
  cfg.touchSelectThreshold = touchSelectThreshold;
  cfg.touchMarkerSize = touchMarkerSize;
  cfg.lineWidth = lineWidth;
  cfg.outsideCurveBg = outsideCurveBg;
  cfg.backgroundColor = backgroundColor;
  cfg.accentColor = accentColor;
  cfg.curveLineColor = curveLineColor;
  cfg.markerColor = markerColor;
  cfg.labelColor = labelColor;
  cfg.gradientLeftColor = gradientLeftColor;
  cfg.gradientRightColor = gradientRightColor;
  return cfg;
}

void Settings::applyDeviceConfig(const DeviceConfig& cfg) {
  shockParameter = cfg.shockParameter;
  secondShockParameter = cfg.secondShockParameter;
  usePishock = cfg.usePishock;
  useSerial = cfg.useSerial;
  randomOrSeq = cfg.randomOrSeq;
  shockerIDs = cfg.shockerIDs;
  serialPort = cfg.serialPort;
  lastSerialPort = cfg.lastSerialPort;
  pishockUsername = cfg.pishockUsername;
  pishockApiKey = cfg.pishockApiKey;
  openshockApiToken = cfg.openshockApiToken;
  openshockServerUrl = cfg.openshockServerUrl;
  vrchatHost = cfg.vrchatHost;
  chatboxShockEnabled = cfg.chatboxShockEnabled;
  chatboxCooldownEnabled = cfg.chatboxCooldownEnabled;
  notificationsEnabled = cfg.notificationsEnabled;
  notifUseOvrToolkit = cfg.notifUseOvrToolkit;
  parameters = cfg.parameters;

  if (parameters.empty()) {
    if (!shockParameter.empty())
      parameters.push_back({shockParameter, 0, CurveRange::Full});
    if (!secondShockParameter.empty())
      parameters.push_back({secondShockParameter, 0, CurveRange::SecondHalf});
    if (parameters.empty()) parameters.push_back(Parameter());
  } else {
    shockParameter = parameters[0].name;
    secondShockParameter.clear();
    for (size_t i = 1; i < parameters.size(); ++i) {
      if (parameters[i].range == CurveRange::SecondHalf) {
        secondShockParameter = parameters[i].name;
        break;
      }
    }
  }
}

void Settings::applyShockConfig(const ShockConfig& cfg) {
  defaultPreset = cfg.defaultPreset;
  minShockDuration = cfg.minShockDuration;
  maxShockDuration = cfg.maxShockDuration;
  xViewMin = cfg.xViewMin;
  xViewMax = cfg.xViewMax;
  baseCooldown = cfg.baseCooldown;
  maxCooldown = cfg.maxCooldown;
  cooldownFactor = cfg.cooldownFactor;
  cooldownWindow = cfg.cooldownWindow;
  cooldownEnabled = cfg.cooldownEnabled;
  curves = cfg.curves;
  presets = cfg.presets;

  if (curves.empty()) {
    curves.push_back(Preset());
    curves[0].name = "Default";
  }
}

void Settings::applyUiConfig(const UiConfig& cfg) {
  windowX = cfg.windowX;
  windowY = cfg.windowY;
  windowW = cfg.windowW;
  windowH = cfg.windowH;
  showStats = cfg.showStats;
  hotkeyVk = cfg.hotkeyVk;
  hotkeyMods = cfg.hotkeyMods;
  presetCount = cfg.presetCount;
  touchSelectThreshold = cfg.touchSelectThreshold;
  touchMarkerSize = cfg.touchMarkerSize;
  lineWidth = cfg.lineWidth;
  outsideCurveBg = cfg.outsideCurveBg;
  backgroundColor = cfg.backgroundColor;
  accentColor = cfg.accentColor;
  curveLineColor = cfg.curveLineColor;
  markerColor = cfg.markerColor;
  labelColor = cfg.labelColor;
  gradientLeftColor = cfg.gradientLeftColor;
  gradientRightColor = cfg.gradientRightColor;
}

void Settings::save(const std::string& path) {
  std::ofstream file(path);
  file << toJson().dump(2);
  logMsg("Settings saved");
}

nlohmann::json Settings::toJson() const {
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

  if (!parameters.empty()) {
    j["shockParameter"] = parameters[0].name;
    std::string secondName;
    for (size_t i = 1; i < parameters.size(); ++i) {
      if (parameters[i].range == CurveRange::SecondHalf) {
        secondName = parameters[i].name;
        break;
      }
    }
    j["secondShockParameter"] = secondName;
  } else {
    j["shockParameter"] = shockParameter;
    j["secondShockParameter"] = secondShockParameter;
  }

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
  j["chatboxShockEnabled"] = chatboxShockEnabled;
  j["chatboxCooldownEnabled"] = chatboxCooldownEnabled;

  j["parameters"] = nlohmann::json::array();
  for (auto& p : parameters) {
    j["parameters"].push_back(
        {{"name", p.name}, {"curveIndex", p.curveIndex}, {"range", (int)p.range}});
  }

  j["presets"] = nlohmann::json::array();
  for (auto& p : presets) {
    if (!p.has_value()) {
      j["presets"].push_back(nullptr);
      continue;
    }

    nlohmann::json jp;
    jp["name"] = p->name;
    jp["activeCurveIndex"] = p->activeCurveIndex;
    jp["curves"] = nlohmann::json::array();

    for (auto& c : p->curves) {
      nlohmann::json pts = nlohmann::json::array();
      for (auto& cp : c.curvePoints) {
        pts.push_back({{"x", cp.x}, {"y", cp.y}});
      }
      jp["curves"].push_back({
          {"name", c.name},
          {"minShockDuration", c.minShockDuration},
          {"maxShockDuration", c.maxShockDuration},
          {"curvePoints", pts},
          {"xViewMin", c.xViewMin},
          {"xViewMax", c.xViewMax},
      });
    }

    j["presets"].push_back(jp);
  }

  j["curves"] = nlohmann::json::array();
  for (auto& c : curves) {
    nlohmann::json pts = nlohmann::json::array();
    for (auto& cp : c.curvePoints) {
      pts.push_back({{"x", cp.x}, {"y", cp.y}});
    }

    j["curves"].push_back({
        {"name", c.name},
        {"minShockDuration", c.minShockDuration},
        {"maxShockDuration", c.maxShockDuration},
        {"curvePoints", pts},
        {"xViewMin", c.xViewMin},
        {"xViewMax", c.xViewMax},
    });
  }

  return j;
}

bool Settings::operator==(const Settings& other) const {
  auto vec4Eq = [](const ImVec4& a, const ImVec4& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
  };

  return windowX == other.windowX && windowY == other.windowY &&
         windowW == other.windowW && windowH == other.windowH &&
         showStats == other.showStats &&
         defaultPreset == other.defaultPreset &&
         minShockDuration == other.minShockDuration &&
         maxShockDuration == other.maxShockDuration && xViewMin == other.xViewMin &&
         xViewMax == other.xViewMax && shockParameter == other.shockParameter &&
         secondShockParameter == other.secondShockParameter &&
         usePishock == other.usePishock && shockerIDs == other.shockerIDs &&
         randomOrSeq == other.randomOrSeq && serialPort == other.serialPort &&
         useSerial == other.useSerial &&
         pishockUsername == other.pishockUsername &&
         pishockApiKey == other.pishockApiKey &&
         openshockApiToken == other.openshockApiToken &&
         openshockServerUrl == other.openshockServerUrl &&
         baseCooldown == other.baseCooldown && maxCooldown == other.maxCooldown &&
         cooldownFactor == other.cooldownFactor &&
         cooldownWindow == other.cooldownWindow &&
         cooldownEnabled == other.cooldownEnabled && hotkeyVk == other.hotkeyVk &&
         hotkeyMods == other.hotkeyMods &&
         notificationsEnabled == other.notificationsEnabled &&
         notifUseOvrToolkit == other.notifUseOvrToolkit &&
         presetCount == other.presetCount &&
         touchSelectThreshold == other.touchSelectThreshold &&
         touchMarkerSize == other.touchMarkerSize &&
         lineWidth == other.lineWidth && vrchatHost == other.vrchatHost &&
         chatboxShockEnabled == other.chatboxShockEnabled &&
         chatboxCooldownEnabled == other.chatboxCooldownEnabled &&
         parameters == other.parameters && presets.size() == other.presets.size() &&
         vec4Eq(backgroundColor, other.backgroundColor) &&
         vec4Eq(outsideCurveBg, other.outsideCurveBg) &&
         vec4Eq(accentColor, other.accentColor) &&
         vec4Eq(curveLineColor, other.curveLineColor) &&
         vec4Eq(markerColor, other.markerColor) &&
         vec4Eq(labelColor, other.labelColor) &&
         vec4Eq(gradientLeftColor, other.gradientLeftColor) &&
         vec4Eq(gradientRightColor, other.gradientRightColor);
}

bool Settings::operator!=(const Settings& other) const { return !(*this == other); }

nlohmann::json Settings::saveColor(const ImVec4& c) {
  return {c.x, c.y, c.z, c.w};
}

ImVec4 Settings::loadColor(const nlohmann::json& j, const std::string& key,
                           const ImVec4& fallback) {
  if (!j.contains(key) || !j[key].is_array() || j[key].size() < 4) {
    return fallback;
  }
  return {j[key][0].get<float>(), j[key][1].get<float>(),
          j[key][2].get<float>(), j[key][3].get<float>()};
}
