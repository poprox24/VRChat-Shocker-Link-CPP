#pragma once

#include <string>

#include "settings.h"
#include "shockerhub.h"

void runUI(Settings& settings, ShockerHub& hub,
           const std::string& settingsPath);