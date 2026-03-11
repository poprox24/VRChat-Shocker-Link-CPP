#pragma once

#include <yaml-cpp/yaml.h>

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

 public:
  // Serial Config
  std::string shockParameter;
  std::string secondShockParameter;
  bool usePishock;
  std::vector<std::string> ShockerIDs;
  bool randomOrSeq;
  std::string serialPort;

  bool hasSecondShockParameter = false;

  // Cooldown Config
  int baseCooldown;
  int maxCooldown;
  float cooldownFactorS;
  int cooldownWindowS;
  bool cooldownEnabled;

  int shockStrength;

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

  static const int baudRate = 115200;
  static const int oscPort = 39570;
  static constexpr std::string_view serviceName = "ShockerLink";

  Config(std::string path) {
    YAML::Node config;

    try {
      config = YAML::LoadFile(path);
    } catch (YAML::BadFile) {
      logMsg("Config file missing.\n");
      return;
    } catch (std::exception& e) {
      logMsg("Config parse error: {}\n", e.what());
      return;
    }

    // Serial Config
    shockParameter = "/avatar/parameters/" +
                     config["SHOCK_PARAMETER"].as<std::string>("Shock");
    secondShockParameter = "/avatar/parameters/" +
                           config["SECOND_SHOCK_PARAMETER"].as<std::string>("");
    usePishock = config["USE_PISHOCK"].as<bool>(true);
    for (auto id : config["SHOCKER_IDS"]) {
      ShockerIDs.push_back(std::to_string(id.as<int>()));
    }
    randomOrSeq = config["RANDOM_OR_SEQUENTIAL"].as<bool>(false);
    serialPort = config["SERIAL_PORT"].as<std::string>("");

    hasSecondShockParameter =
        !config["SECOND_SHOCK_PARAMETER"].as<std::string>("").empty();

    // Cooldown Config
    baseCooldown = config["BASE_COOLDOWN_S"].as<int>(2);
    maxCooldown = config["MAX_COOLDOWN_S"].as<int>(6);
    cooldownFactorS = config["COOLDOWN_FACTOR_S"].as<float>(0.4);
    cooldownWindowS = config["COOLDOWN_WINDOW_S"].as<int>(30);
    cooldownEnabled = config["COOLDOWN_ENABLED"].as<bool>(true);

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

    shockStrength = 20;
  }

  void pushShockerId(std::string id) { ShockerIDs.push_back(id); }
};