#pragma once
#include <yaml-cpp/yaml.h>

#include <filesystem>

#include "logger.h"
#include "settings.h"

// Migrate old config.yml file if exists
inline void migrateConfigYmlIfPresent(Settings& s,
                                      const std::string& settingsPath) {
  if (!std::filesystem::exists("config.yml")) return;
  try {
    YAML::Node c = YAML::LoadFile("config.yml");
    s.shockParameter = c["SHOCK_PARAMETER"].as<std::string>("Shock");
    s.secondShockParameter = c["SECOND_SHOCK_PARAMETER"].as<std::string>("");
    s.usePishock = c["USE_PISHOCK"].as<bool>(false);
    s.shockerIDs.clear();
    for (auto id : c["SHOCKER_IDS"])
      s.shockerIDs.push_back(std::to_string(id.as<int>()));
    if (s.shockerIDs.empty()) s.shockerIDs = {"41838"};
    s.randomOrSeq = c["RANDOM_OR_SEQUENTIAL"].as<bool>(false);
    s.serialPort = c["SERIAL_PORT"].as<std::string>("");
    s.baseCooldown = c["BASE_COOLDOWN_S"].as<int>(2);
    s.maxCooldown = c["MAX_COOLDOWN_S"].as<int>(6);
    s.cooldownFactor = c["COOLDOWN_FACTOR_S"].as<float>(0.4f);
    s.cooldownWindow = c["COOLDOWN_WINDOW_S"].as<int>(30);
    s.cooldownEnabled = c["COOLDOWN_ENABLED"].as<bool>(true);

    // Migrate old separate notification booleans to the new unified fields
    bool oldXs = c["XSOVERLAY_NOTIFICATIONS"].as<bool>(false);
    bool oldOvr = c["OVRTOOLKIT_NOTIFICATIONS"].as<bool>(false);
    s.notificationsEnabled = oldXs || oldOvr;
    s.notifUseOvrToolkit = oldOvr;

    s.presetCount = c["PRESET_COUNT"].as<int>(3);
    s.touchSelectThreshold = c["TOUCH_SELECT_THRESHOLD"].as<float>(8.f);
    s.touchMarkerSize = c["TOUCH_MARKER_SIZE"].as<float>(140.f);
    s.lineWidth = c["LINE_WIDTH"].as<float>(3.f);
    auto hex = [](const std::string& h) {
      unsigned r = 0, g = 0, b = 0;
#ifdef _WIN32
      sscanf_s(h.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
#else
      sscanf(h.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
#endif
      return ImVec4{r / 255.f, g / 255.f, b / 255.f, 1.f};
    };
    s.outsideCurveBg = hex(c["inside_CURVE_BG"].as<std::string>(
        "#2C3749"));  // Outside curve was replaced with inside curve bg
    s.backgroundColor = hex(c["BACKGROUND_COLOR"].as<std::string>("#202630"));
    s.curveLineColor = hex(c["CURVE_LINE_COLOR"].as<std::string>("#00C2FF"));
    s.markerColor = hex(c["MARKER_COLOR"].as<std::string>("#D88A91"));
    s.labelColor = hex(c["LABEL_COLOR"].as<std::string>("#E6EEF6"));
    s.gradientLeftColor =
        hex(c["GRADIENT_LEFT_COLOR"].as<std::string>("#42953b"));
    s.gradientRightColor =
        hex(c["GRADIENT_RIGHT_COLOR"].as<std::string>("#6e173b"));
    s.vrchatHost = c["VRCHAT_HOST"].as<std::string>("127.0.0.1");
    s.save(settingsPath);
    std::filesystem::remove("config.yml");
    logMsg("[Migration] config.yml imported and removed");
  } catch (std::exception& e) {
    logMsg("[Migration] config.yml import failed: {}", e.what());
  }
}