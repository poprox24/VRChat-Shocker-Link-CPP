#pragma once

#include <fmt/base.h>

#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "config.h"

struct Preset {
  std::string name;
  float minShockDuration;
  float maxShockDuration;
};

class Settings {
 private:
  Config& config;

 public:
  int minShockDuration;
  int maxShockDuration;
  int defaultPreset;

  std::vector<std::optional<Preset>> presets;

  Settings(std::string path, Config& cfg)
      : config(cfg),
        presets(config.presetCount),
        minShockDuration(1),
        maxShockDuration(2),
        defaultPreset(-1) {
    try {
      std::ifstream file(path);
      if (file.is_open()) {
        nlohmann::json j = nlohmann::json::parse(file);
        int i = 0;
        for (auto& item : j["presets"]) {
          if (i >= (int)presets.size()) break;
          if (item.is_null()) {
            presets[i] = std::nullopt;
          } else {
            presets[i] = Preset{item["name"].get<std::string>(),
                                item["minShockDuration"].get<float>(),
                                item["maxShockDuration"].get<float>()};
          }
          i++;
        }
        defaultPreset = j.value("defaultPreset", -1);
        if (defaultPreset >= 0 && defaultPreset < (int)presets.size() &&
            presets[defaultPreset].has_value()) {
          minShockDuration = presets[defaultPreset]->minShockDuration;
          maxShockDuration = presets[defaultPreset]->maxShockDuration;
        }
      }
    } catch (std::exception& e) {
      fmt::print("Settings parse error: {}\n", e.what());
    }
  }

  void save(std::string path) {
    nlohmann::json j;
    j["defaultPreset"] = defaultPreset;
    j["presets"] = nlohmann::json::array();
    for (auto& p : presets) {
      if (p.has_value()) {
        j["presets"].push_back({{"name", p->name},
                                {"minShockDuration", p->minShockDuration},
                                {"maxShockDuration", p->maxShockDuration}});
      } else {
        j["presets"].push_back(nullptr);
      }
    }
    std::ofstream file(path);
    file << j.dump(2);
  }
};