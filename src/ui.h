#pragma once

#include <string>

class Settings;
class ShockerHub;

void runUI(Settings& settings, ShockerHub& hub,
           const std::string& settingsLocation);
