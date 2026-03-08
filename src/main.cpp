#include <fmt/base.h>
#include <serialib.h>
#include <yaml-cpp/yaml.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <queue>
#include <string>
#include <thread>
#include <vector>

std::string configLocation = "config.yml";
std::atomic<bool> running = true;

void signalHandler(int signal) { running = false; }

class Config {
 private:
  YAML::Node config;

 public:
  std::string serialPort;
  int baudRate;
  bool randomOrSeq;
  std::vector<std::string> ShockerIDs;
  bool usePishock;

  Config(std::string path) {
    try {
      config = YAML::LoadFile(path);

      serialPort = config["serial_port"].as<std::string>("");
      baudRate = config["baud_rate"].as<int>(115200);
      randomOrSeq = config["random_or_sequential"].as<bool>(false);
      usePishock = config["use_pishock"].as<bool>(true);
    } catch (YAML::BadFile fileNotFound) {
      fmt::print("Config file missing.\n");
    } catch (std::exception& e) {
      fmt::print("Config parse error: {}\n", e.what());
    }

    for (auto id : config["shocker_ids"]) {
      ShockerIDs.push_back(std::to_string(id.as<int>()));
    }
  }

  void pushShockerId(std::string id) { ShockerIDs.push_back(id); }
};

class ShockerHub {
 private:
  Config& config;

  int lastShockerIndex = -1;

  serialib serial;
  std::queue<std::pair<int, int>> shockQueue;
  std::mutex queueMutex;
  std::thread workerThread;
  std::atomic<bool> stopWorker = false;

 public:
  ShockerHub(Config& cfg) : config(cfg) {}

  bool hasSerial() { return serial.isDeviceOpen(); }

  bool connectSerial() {
    std::string serialPort = config.serialPort;
    if (config.serialPort != "") {
      if (serial.openDevice(config.serialPort.c_str(), config.baudRate) != 1) {
        fmt::print("Connected\n");
        return false;
      }
      if (!workerThread.joinable()) {
        workerThread = std::thread(&ShockerHub::workerLoop, this);
      }
      return 1;
    } else if (config.usePishock) {
      for (int i = 1; i <= 50; i++) {
        config.serialPort = "COM" + std::to_string(i);
        fmt::print("port: {}\n", config.serialPort);
        if (serial.openDevice(config.serialPort.c_str(), config.baudRate) ==
            1) {
          bool found = false;
          serial.writeString("{\"cmd\": \"info\"}\n");

          for (int attempt = 0; attempt < 40; attempt++) {
            char buf[1024] = {0};
            serial.readString(buf, '\n', 1024, 1000);
            std::string response(buf);
            if (response.starts_with("TERMINALINFO: ")) {
              if (response.find("pishock") != std::string::npos) {
                found = true;
                if (config.ShockerIDs.empty()) {
                  std::string json_str =
                      response.substr(14);  // Remove "TERMINALINFO: ", 14 chars
                  auto json = nlohmann::json::parse(json_str, nullptr, false);
                  if (!json.is_discarded() && json.contains("shockers")) {
                    for (auto& s : json["shockers"]) {
                      config.pushShockerId(std::to_string(s["id"].get<int>()));
                    }
                  }
                }
                break;
              }
            } else {
              continue;
            }
          }
          if (found) {
            if (!workerThread.joinable()) {
              fmt::print("Connected\n");
              workerThread = std::thread(&ShockerHub::workerLoop, this);
            }
            return true;
          }
          serial.closeDevice();
        } else {
          continue;
        }
      }
      fmt::print(
          "Couldn't connect to PiShock HUB, check connection and press any key "
          "to retry...");
      system("pause");
      return reconnect_serial();
    } else {
      for (int i = 1; i <= 50; i++) {
        config.serialPort = "COM" + std::to_string(i);
        if (serial.openDevice(config.serialPort.c_str(), config.baudRate) ==
            1) {
          bool found = false;
          serial.writeString("domain\n");

          for (int attempt = 0; attempt < 5; attempt++) {
            char buf[64] = {0};
            serial.readString(buf, '\n', 64, 1000);
            std::string response(buf);

            if (response.find("openshock") != std::string::npos) {
              found = true;
              break;
            }
          }
          if (found) {
            if (!workerThread.joinable()) {
              fmt::print("Connected\n");
              workerThread = std::thread(&ShockerHub::workerLoop, this);
            }
            return true;
          }
          serial.closeDevice();
        } else {
          continue;
        }
      }
      fmt::print(
          "Couldn't connect to OpenShock HUB, check connection and press any "
          "key to retry...\n");
      system("pause");
      return reconnect_serial();
    }
    return 0;
  }

  void queueShock(int durationMs, int strength) {
    std::lock_guard<std::mutex> lock(queueMutex);
    shockQueue.push(std::pair(durationMs, strength));
  }

  void emptyQueue() {
    std::queue<std::pair<int, int>> empty;
    shockQueue.swap(empty);
  }

  bool reconnect_serial() {
    while (true) {
      serial.closeDevice();
      if (connectSerial()) {
        return true;
      } else {
        fmt::print(
            "Reconnect failed, all shocks have been dropped.\n"
            "Press any key to retry...\n");
        emptyQueue();
        system("pause");
      }
    }
  }

  void workerLoop() {
    while (!stopWorker) {
      std::unique_lock<std::mutex> lock(queueMutex);
      if (!shockQueue.empty()) {
        int durationMs = shockQueue.front().first;
        int strength = shockQueue.front().second;

        shockQueue.pop();
        lock.unlock();

        // False for random, True for sequential
        std::string chosenShocker;
        std::vector<std::string> shockerIDs = config.ShockerIDs;
        if (config.randomOrSeq) {
          int index = rand() % shockerIDs.size();
          chosenShocker = shockerIDs[index];
        } else {
          lastShockerIndex = (lastShockerIndex + 1) % shockerIDs.size();
          chosenShocker = shockerIDs[lastShockerIndex];
        }
        sendShock(durationMs, strength, chosenShocker);
      } else {
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }
  }

  bool listShockers() {
    int sizeShockerIDs = config.ShockerIDs.size();
    if (sizeShockerIDs == 0) {
      fmt::print(
          "No shockers configured and none found automatically.\n"
          "Please set them up in config.yml\n"
          "The program will now exit...\n");
      system("pause");
      return false;
    } else {
      std::string ids;
      for (size_t i = 0; i < sizeShockerIDs; ++i) {
        if (i > 0) ids += ", ";
        ids += config.ShockerIDs[i];
      }
      fmt::print("Shockers found: {}\n", ids);
      return true;
    }
  }

  void sendShock(int durationMs, int strength, std::string shockerID) {
    std::string command;
    if (config.usePishock) {
      nlohmann::json payload = {{"cmd", "operate"},
                                {"value",
                                 {{"id", std::stoi(shockerID)},
                                  {"op", "shock"},
                                  {"duration", durationMs},
                                  {"intensity", strength}}}};
      command = payload.dump() + "\n";
    } else {
      nlohmann::json payload = {{"model", "caixianlin"},
                                {"id", std::stoi(shockerID)},
                                {"type", "shock"},
                                {"intensity", strength},
                                {"durationMs", durationMs}};
      command = "rftransmit " + payload.dump() + "\n";
    }
    int result = serial.writeString(command.c_str());
    if (result <= 0) {
      fmt::print("Serial write failed, reconnecting...\n");
      reconnect_serial();
      queueShock(durationMs, strength);
      return;
    }
    fmt::print("Sent shock: {0}%, {1}s\n", strength,
               round(durationMs / 100) / 10);
  }
  void shutdown() {
    stopWorker = true;
    if (workerThread.joinable()) {
      workerThread.join();
    }
    serial.closeDevice();
  }
};

int main() {
  Config config(configLocation);
  ShockerHub shockerHub(config);

  std::signal(SIGINT, signalHandler);

  fmt::print("Attempting to connect\n");
  shockerHub.connectSerial();
  if (!shockerHub.listShockers()) {
    shockerHub.shutdown();
    return 0;
  }

  // Keep app running until CTRL + C
  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    if (shockerHub.hasSerial()) {
      shockerHub.queueShock(500, 20);
    }
  }

  shockerHub.shutdown();
  return 0;
}