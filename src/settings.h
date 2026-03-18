#pragma once
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "config.h"
#include "curve.h"
#include "logger.h"

struct Preset {
  std::string name;
  float minShockDuration;
  float maxShockDuration;
  std::array<CurvePoint, 3> curvePoints = {
      {{20.0, 0.8}, {50.0, 0.5}, {80.0, 0.2}}};
  float xViewMin = 0.f;
  float xViewMax = 100.f;
};

class Settings {
 private:
  Config& config;

 public:
  float minShockDuration;
  float maxShockDuration;
  int defaultPreset;
  float xViewMin;
  float xViewMax;
  std::vector<std::optional<Preset>> presets;

  Settings(std::string path, Config& cfg)
      : config(cfg),
        presets(config.presetCount),
        minShockDuration(1),
        maxShockDuration(2),
        defaultPreset(-1),
        xViewMin(0.f),
        xViewMax(100.f) {
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
            presets[i]->xViewMin = item.value("xViewMin", 0.f);
            presets[i]->xViewMax = item.value("xViewMax", 100.f);
            if (item.contains("curvePoints") &&
                item["curvePoints"].size() == 3) {
              for (int k = 0; k < 3; k++) {
                presets[i]->curvePoints[k] = {
                    item["curvePoints"][k]["x"].get<double>(),
                    item["curvePoints"][k]["y"].get<double>()};
              }
            }
          }
          i++;
        }
        defaultPreset = j.value("defaultPreset", -1);
        if (defaultPreset >= 0 && defaultPreset < (int)presets.size() &&
            presets[defaultPreset].has_value()) {
          minShockDuration = presets[defaultPreset]->minShockDuration;
          maxShockDuration = presets[defaultPreset]->maxShockDuration;
        }
        xViewMin = j.value("xViewMin", 0.f);
        xViewMax = j.value("xViewMax", 100.f);
      }
    } catch (std::exception& e) {
      logMsg("Settings parse error: {}\n", e.what());
    }
  }

  void save(std::string path) {
    nlohmann::json j;
    j["defaultPreset"] = defaultPreset;
    j["xViewMin"] = xViewMin;
    j["xViewMax"] = xViewMax;
    j["presets"] = nlohmann::json::array();
    for (auto& p : presets) {
      if (p.has_value()) {
        nlohmann::json pts = nlohmann::json::array();
        for (auto& cp : p->curvePoints)
          pts.push_back({{"x", cp.x}, {"y", cp.y}});
        j["presets"].push_back({
            {"name", p->name},
            {"minShockDuration", p->minShockDuration},
            {"maxShockDuration", p->maxShockDuration},
            {"curvePoints", pts},
            {"xViewMin", p->xViewMin},
            {"xViewMax", p->xViewMax},
        });
      } else {
        j["presets"].push_back(nullptr);
      }
    }
    std::ofstream file(path);
    file << j.dump(2);
    logMsg("Preset updated");
  }
};