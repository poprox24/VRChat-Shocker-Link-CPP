#pragma once

#include <fmt/base.h>
#include <yaml-cpp/yaml.h>

#include <string>
#include <vector>

class Config {
 public:
  // Serial Config
  std::string shockParameter;
  std::string secondShockParameter;
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

  int shockStrength;

  int presetCount;

  static const int baudRate = 115200;
  static const int oscPort = 39570;
  static constexpr std::string_view serviceName = "ShockerLink";

  Config(std::string path) {
    YAML::Node config;

    try {
      config = YAML::LoadFile(path);
    } catch (YAML::BadFile) {
      fmt::print("Config file missing.\n");
      return;
    } catch (std::exception& e) {
      fmt::print("Config parse error: {}\n", e.what());
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

    // Cooldown Config
    baseCooldown = config["BASE_COOLDOWN_S"].as<int>(2);
    maxCooldown = config["MAX_COOLDOWN_S"].as<int>(6);
    cooldownFactorS = config["COOLDOWN_FACTOR_S"].as<float>(0.4);
    cooldownWindowS = config["COOLDOWN_WINDOW_S"].as<int>(30);
    cooldownEnabled = config["COOLDOWN_ENABLED"].as<bool>(true);

    // Style Config
    presetCount = config["PRESET_COUNT"].as<int>(3);

    shockStrength = 20;
  }

  void pushShockerId(std::string id) { ShockerIDs.push_back(id); }
};