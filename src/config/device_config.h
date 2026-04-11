#pragma once

#include <string>
#include <vector>

#include "config/parameter.h"

struct DeviceConfig {
  std::string shockParameter = "Shock";
  std::string secondShockParameter = "";

  bool usePishock = true;
  bool useSerial = true;
  bool randomOrSeq = false;

  std::vector<std::string> shockerIDs = {};

  std::string serialPort = "";
  std::string lastSerialPort = "";

  std::string pishockUsername = "";
  std::string pishockApiKey = "";
  std::string openshockApiToken = "";
  std::string openshockServerUrl = "api.openshock.app";

  std::string vrchatHost = "127.0.0.1";
  bool chatboxShockEnabled = true;
  bool chatboxCooldownEnabled = true;

  bool notificationsEnabled = false;
  bool notifUseOvrToolkit = false;

  std::vector<Parameter> parameters = {Parameter()};
};
