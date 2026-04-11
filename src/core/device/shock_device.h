#pragma once

#include <string>
#include <vector>

enum class ShockOperation { Shock, Vibrate };

struct DeviceShockCommand {
  std::string shockerId;
  int durationMs = 0;
  int intensity = 0;
  ShockOperation operation = ShockOperation::Shock;
};

class IShockDevice {
 public:
  virtual ~IShockDevice() = default;

  virtual bool connect() = 0;
  virtual bool isConnected() const = 0;
  virtual bool send(const DeviceShockCommand& command) = 0;
  virtual std::vector<std::string> listShockers() = 0;
};
