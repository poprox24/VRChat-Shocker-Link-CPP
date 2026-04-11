#pragma once

#include "imgui.h"

struct UiConfig {
  int windowX = 100;
  int windowY = 100;
  int windowW = 750;
  int windowH = 520;

  bool showStats = false;

  int hotkeyVk = 298;
  int hotkeyMods = 0;

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
};
