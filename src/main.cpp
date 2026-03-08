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
  std::vector<int> ShockerIDs;
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
      ShockerIDs.push_back(id.as<int>());
    }
  }
};

class ShockerHub {
 private:
  Config& config;

  std::string serialPort;
  int baudRate;
  bool randomOrSeq;
  std::vector<std::string> shockerIDs;
  bool usePishock;

  int lastShockerIndex = -1;

  serialib serial;
  std::queue<std::pair<int, int>> shockQueue;
  std::mutex queueMutex;
  std::thread workerThread;
  std::atomic<bool> stopWorker = false;

 public:
  ShockerHub(Config& cfg) : config(cfg) {
    this->baudRate = config.baudRate;
    this->serialPort = config.serialPort;
    this->randomOrSeq = config.randomOrSeq;
    for (int id : config.ShockerIDs) {
      this->shockerIDs.push_back(std::to_string(id));
    }
    this->usePishock = config.usePishock;
  }

  bool hasSerial() { return serial.isDeviceOpen(); }

  bool connectSerial() {
    if (serialPort != "") {
      if (serial.openDevice(serialPort.c_str(), baudRate) != 1) {
        fmt::print("Connected\n");
        return false;
      }
      if (!workerThread.joinable()) {
        workerThread = std::thread(&ShockerHub::workerLoop, this);
      }
      return 1;
    } else if (usePishock) {
      for (int i = 1; i <= 50; i++) {
        serialPort = "COM" + std::to_string(i);
        fmt::print("port: {}\n", serialPort);
        if (serial.openDevice(serialPort.c_str(), baudRate) == 1) {
          bool found = false;
          serial.writeString("{\"cmd\": \"info\"}\n");

          for (int attempt = 0; attempt < 40; attempt++) {
            char buf[1024] = {0};
            serial.readString(buf, '\n', 1024, 1000);
            std::string response(buf);
            fmt::print("response: {}\n", response);
            if (response.starts_with("TERMINALINFO: ")) {
              if (response.find("pishock") != std::string::npos) {
                found = true;
                if (shockerIDs.empty()) {
                  std::string json_str =
                      response.substr(14);  // Remove "TERMINALINFO: ", 14 chars
                  auto json = nlohmann::json::parse(json_str, nullptr, false);
                  if (!json.is_discarded() && json.contains("shockers")) {
                    for (auto& s : json["shockers"]) {
                      shockerIDs.push_back(std::to_string(s["id"].get<int>()));
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
        serialPort = "COM" + std::to_string(i);
        if (serial.openDevice(serialPort.c_str(), baudRate) == 1) {
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
      serialPort = config.serialPort;  // Reset port to default value
      if (connectSerial()) {
        return true;
      } else {
        fmt::print(
            "Reconnect failed, all shocks have been dropped.\npress any key to "
            "retry...\n");
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
        if (randomOrSeq) {
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

  void listShockers() {
    std::string ids;
    for (size_t i = 0; i < shockerIDs.size(); ++i) {
      if (i > 0) ids += ", ";
      ids += shockerIDs[i];
    }
    fmt::print("Shockers found: {}\n", ids);
  }

  void sendShock(int durationMs, int strength, std::string shockerID) {
    std::string command;
    if (usePishock) {
      nlohmann::json payload = {{"cmd", "operate"},
                                {"value",
                                 {{"id", std::stoi(shockerID)},
                                  {"op", "shock"},
                                  {"duration", durationMs},
                                  {"intensity", strength}}}};
      command = payload.dump() + "\n";
    } else {
      command =
          "rftransmit {\"model\":\"caixianlin\",\"id\":" + shockerID +
          ",\"type\":\"shock\",\"intensity\":" + std::to_string(strength) +
          ",\"durationMs\":" + std::to_string(durationMs) + "}\n";
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
  shockerHub.listShockers();

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