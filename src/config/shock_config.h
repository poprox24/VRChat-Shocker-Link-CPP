#pragma once

#include <optional>
#include <vector>

#include "config/preset.h"

struct ShockConfig {
  int defaultPreset = -1;

  float minShockDuration = 1.f;
  float maxShockDuration = 2.f;
  float xViewMin = 0.f;
  float xViewMax = 100.f;

  int baseCooldown = 2;
  int maxCooldown = 6;
  float cooldownFactor = 0.4f;
  int cooldownWindow = 30;
  bool cooldownEnabled = true;

  std::vector<Preset> curves;
  std::vector<std::optional<SavedPreset>> presets;
};
