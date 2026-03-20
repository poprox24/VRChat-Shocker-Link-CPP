#pragma once

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <string>
#include <vector>

#include "imgui.h"
#include "logger.h"

class Config {
 private:
  static ImVec4 hexToImVec4(const std::string& hex) {
    unsigned int r = 0, g = 0, b = 0;
    sscanf_s(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
    return {r / 255.f, g / 255.f, b / 255.f, 1.f};
  }

  static constexpr const char* DEFAULT_CONFIG = R"(# Serial Config
SHOCK_PARAMETER: "Shock" # Input the parameter name you want to use for the shock (for example for touches)
SECOND_SHOCK_PARAMETER: "" # Optional second parameter for stronger shocks, takes only the second half of the curve into account (for example for slaps)
USE_PISHOCK: False # Set to True if using PiShock, False for OpenShock
SHOCKER_IDS: [41838] # Shocker IDs, if you have multiple, split by comma (eg.: [12345, 23456]), PiShock should find them automatically(OpenShock doesn't save them on the hub)
RANDOM_OR_SEQUENTIAL: False # If using multiple shockers, this option chooses between randomizing or using them sequentially, False for random // True for sequential
SERIAL_PORT: "" # Leave blank to auto-detect

# Cooldown settings
# Math explanation:
# --- Base_cooldown + Cooldown_factor * Amount of boops in Cooldown_window = Cooldown (s) ---
BASE_COOLDOWN_S: 2 # Default cooldown (in seconds)
MAX_COOLDOWN_S: 6 # Maximum cooldown (in seconds)
COOLDOWN_FACTOR_S: 0.4 # How much cooldown to add per each shock within the window
COOLDOWN_WINDOW_S: 30 # How big is the window for the factor (in seconds), will count all boops in this timeframe
COOLDOWN_ENABLED: True # Changes default state of cooldown (Cooldown is not saved in presets)

# Notifications -- Sends a notification about the strength and duration as a notification to your headset
XSOVERLAY_NOTIFICATIONS: False
OVRTOOLKIT_NOTIFICATIONS: False

# Style config
PRESET_COUNT: 3 # Amount of presets
TOUCH_SELECT_THRESHOLD: 8 # Touch treshold of the points in the curve
TOUCH_MARKER_SIZE: 140 # Actual size of points in the curve
LINE_WIDTH: 3 # Width of the curve line
OUTSIDE_CURVE_BG: "#2A313D" # Background color outside of the curve area
INSIDE_CURVE_BG: "#2C3749" # Background color inside of the curve area
BACKGROUND_COLOR: "#202630" # Background color of the rest of the window
CURVE_LINE_COLOR: "#00C2FF" # Color of the curve line
MARKER_COLOR: "#D88A91" # Color of the points in the curve
LABEL_COLOR: "#E6EEF6" # Color of the text labels
GRADIENT_LEFT_COLOR: "#42953b" # Left background gradient color for the curve
GRADIENT_RIGHT_COLOR: "#6e173b" # Right background gradient color for the curve

# Vrchat Config (usually don't need to change)
VRCHAT_HOST: "127.0.0.1"
)";

  static void generateConfigFile(const std::string& path) {
    std::ofstream f(path);
    f << DEFAULT_CONFIG;
  }

  static void patchConfigFile(const std::string& path) {
    std::vector<std::pair<std::string, std::string>> defaults;
    std::istringstream def(DEFAULT_CONFIG);
    std::string defLine;
    while (std::getline(def, defLine)) {
      auto colon = defLine.find(':');
      if (colon != std::string::npos && defLine[0] != '#' && defLine[0] != '\0')
        defaults.push_back({defLine.substr(0, colon), defLine});
      else
        defaults.push_back({"", defLine});
    }

    std::ifstream in(path);
    if (!in.is_open()) return;
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
    in.close();

    auto hasKey = [&](const std::string& key) {
      for (auto& l : lines)
        if (l.find(key + ":") == 0) return true;
      return false;
    };

    auto lineExistsInFile = [&](const std::string& l) {
      if (l.empty()) return false;
      for (auto& fl : lines)
        if (fl == l) return true;
      return false;
    };

    bool changed = false;
    for (int di = 0; di < (int)defaults.size(); di++) {
      auto& [key, defVal] = defaults[di];
      if (key.empty() || hasKey(key)) continue;

      int insertAfterLine = (int)lines.size() - 1;
      for (int prev = di - 1; prev >= 0; prev--) {
        if (defaults[prev].first.empty()) continue;
        for (int li = (int)lines.size() - 1; li >= 0; li--) {
          if (lines[li].find(defaults[prev].first + ":") == 0) {
            insertAfterLine = li;
            goto found;
          }
        }
      }
    found:
      std::set<int> consumedPreamble;
      for (int prev = di - 1; prev >= 0 && defaults[prev].first.empty();
           prev--) {
        const std::string& preambleLine = defaults[prev].second;
        if (preambleLine.empty()) continue;
        for (int li = insertAfterLine + 1; li < (int)lines.size(); li++) {
          if (lines[li] == preambleLine) {
            insertAfterLine = li;
            consumedPreamble.insert(prev);
            for (int p2 = prev - 1; p2 >= 0 && defaults[p2].second.empty();
                 p2--)
              consumedPreamble.insert(p2);
            break;
          }
        }
      }

      std::vector<std::string> toInsert;
      for (int prev = di - 1; prev >= 0 && defaults[prev].first.empty();
           prev--) {
        if (consumedPreamble.count(prev)) continue;
        if (!lineExistsInFile(defaults[prev].second))
          toInsert.insert(toInsert.begin(), defaults[prev].second);
      }
      toInsert.push_back(defVal);

      lines.insert(lines.begin() + insertAfterLine + 1, toInsert.begin(),
                   toInsert.end());
      changed = true;
    }

    if (!changed) return;
    std::ofstream out(path);
    for (auto& l : lines) out << l << '\n';
    logMsg("[Update] Config updated with missing keys");
  }

 public:
  // Serial Config
  std::string shockParameter;
  std::string secondShockParameter;
  bool hasSecondShockParameter = false;
  bool usePishock;
  std::vector<std::string> ShockerIDs;
  bool randomOrSeq;
  std::string serialPort;

  // Cooldown Config
  int baseCooldown;
  int maxCooldown;
  float cooldownFactorS;
  int cooldownWindowS;
  bool cooldownEnabled;

  // Notification Config
  bool xsoverlayNotifications;
  bool ovrToolkitNotifications;

  // Style config
  int presetCount;
  float touchSelectThreshold;
  float touchMarkerSize;
  float lineWidth;
  ImVec4 outsideCurveBg;
  ImVec4 insideCurveBg;
  ImVec4 backgroundColor;
  ImVec4 curveLineColor;
  ImVec4 markerColor;
  ImVec4 labelColor;
  ImVec4 gradientLeftColor;
  ImVec4 gradientRightColor;

  // VRChat config
  std::string vrchatHost;

  static const int baudRate = 115200;
  static const int oscPort = 39570;
  static constexpr std::string_view serviceName = "ShockerLink";

  Config(const std::string& path) {
    YAML::Node config;

    try {
      config = YAML::LoadFile(path);
    } catch (YAML::BadFile) {
      logMsg("Config file missing, generating default config.yml\n");
      generateConfigFile(path);
      try {
        config = YAML::LoadFile(path);
      } catch (...) {
        return;
      }
    } catch (std::exception& e) {
      logMsg("Config parse error: {}\n", e.what());
      return;
    }

    patchConfigFile(path);

    // Serial Config
    shockParameter = "/avatar/parameters/" +
                     config["SHOCK_PARAMETER"].as<std::string>("Shock");
    secondShockParameter = "/avatar/parameters/" +
                           config["SECOND_SHOCK_PARAMETER"].as<std::string>("");
    hasSecondShockParameter =
        !config["SECOND_SHOCK_PARAMETER"].as<std::string>("").empty();
    usePishock = config["USE_PISHOCK"].as<bool>(false);
    for (auto id : config["SHOCKER_IDS"])
      ShockerIDs.push_back(std::to_string(id.as<int>()));
    randomOrSeq = config["RANDOM_OR_SEQUENTIAL"].as<bool>(false);
    serialPort = config["SERIAL_PORT"].as<std::string>("");

    // Cooldown Config
    baseCooldown = config["BASE_COOLDOWN_S"].as<int>(2);
    maxCooldown = config["MAX_COOLDOWN_S"].as<int>(6);
    cooldownFactorS = config["COOLDOWN_FACTOR_S"].as<float>(0.4f);
    cooldownWindowS = config["COOLDOWN_WINDOW_S"].as<int>(30);
    cooldownEnabled = config["COOLDOWN_ENABLED"].as<bool>(true);

    // Notification Config
    xsoverlayNotifications = config["XSOVERLAY_NOTIFICATIONS"].as<bool>(false);
    ovrToolkitNotifications =
        config["OVRTOOLKIT_NOTIFICATIONS"].as<bool>(false);

    // Style config
    presetCount = config["PRESET_COUNT"].as<int>(3);
    touchSelectThreshold = config["TOUCH_SELECT_THRESHOLD"].as<float>(8.f);
    touchMarkerSize = config["TOUCH_MARKER_SIZE"].as<float>(140.f);
    lineWidth = config["LINE_WIDTH"].as<float>(3.f);
    outsideCurveBg =
        hexToImVec4(config["OUTSIDE_CURVE_BG"].as<std::string>("#2A313D"));
    insideCurveBg =
        hexToImVec4(config["INSIDE_CURVE_BG"].as<std::string>("#2C3749"));
    backgroundColor =
        hexToImVec4(config["BACKGROUND_COLOR"].as<std::string>("#202630"));
    curveLineColor =
        hexToImVec4(config["CURVE_LINE_COLOR"].as<std::string>("#00C2FF"));
    markerColor =
        hexToImVec4(config["MARKER_COLOR"].as<std::string>("#D88A91"));
    labelColor = hexToImVec4(config["LABEL_COLOR"].as<std::string>("#E6EEF6"));
    gradientLeftColor =
        hexToImVec4(config["GRADIENT_LEFT_COLOR"].as<std::string>("#42953b"));
    gradientRightColor =
        hexToImVec4(config["GRADIENT_RIGHT_COLOR"].as<std::string>("#6e173b"));

    // VRChat Config
    vrchatHost = config["VRCHAT_HOST"].as<std::string>("127.0.0.1");
  }

  void pushShockerId(const std::string& id) { ShockerIDs.push_back(id); }
};