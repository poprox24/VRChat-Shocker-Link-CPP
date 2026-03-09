#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fmt/base.h>
#include <serialib.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <yaml-cpp/yaml.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "httplib.h"
#include "mdns_advertiser.h"

// ─────────────────────────────────────────────────────────────────────────────

std::string configLocation = "config.yml";
std::atomic<bool> running = true;

void signalHandler(int signal) { running = false; }

// ─────────────────────────────────────────────────────────────────────────────
// Config
// Loads settings from config.yml into easy-to-use public fields
// ─────────────────────────────────────────────────────────────────────────────
class Config {
 public:
  std::string serialPort;
  int baudRate;
  bool randomOrSeq;
  std::vector<std::string> ShockerIDs;
  bool usePishock;
  int oscPort;
  std::string oscPath;
  int shockDuration;
  int shockStrength;
  std::string serviceName;

  Config(std::string path) {
    YAML::Node config;

    try {
      config = YAML::LoadFile(path);
    } catch (YAML::BadFile) {
      fmt::print("Config file missing.\n");
      return;
    } catch (std::exception& e) {
      fmt::print("Config parse error: {}\n", e.what());
      return;
    }

    serialPort = config["serial_port"].as<std::string>("");
    baudRate = config["baud_rate"].as<int>(115200);
    randomOrSeq = config["random_or_sequential"].as<bool>(false);
    usePishock = config["use_pishock"].as<bool>(true);
    oscPort = config["osc_port"].as<int>(39570);
    oscPath = config["osc_path"].as<std::string>("/avatar/parameters/Shock");
    shockDuration = config["shock_duration"].as<int>(500);
    shockStrength = config["shock_strength"].as<int>(20);
    serviceName = "ShockerLink";

    for (auto id : config["shocker_ids"]) {
      ShockerIDs.push_back(std::to_string(id.as<int>()));
    }
  }

  void pushShockerId(std::string id) { ShockerIDs.push_back(id); }
};

// ─────────────────────────────────────────────────────────────────────────────
// ShockerHub
// Handles serial connection to PiShock/OpenShock and sends shock commands
// ─────────────────────────────────────────────────────────────────────────────
class ShockerHub {
 public:
  ShockerHub(Config& cfg) : config(cfg) {}

  bool connectSerial() {
    // If serial_port is set in config, connect directly instead of scanning
    if (config.serialPort != "") {
      bool opened =
          serial.openDevice(config.serialPort.c_str(), config.baudRate) == 1;
      if (!opened) {
        fmt::print("Could not open port {}\n", config.serialPort);
        return false;
      }
      startWorkerThread();
      return true;
    }

    // No port set, scan COM1-COM50 to find the device
    if (config.usePishock) {
      return scanForPishock();
    } else {
      return scanForOpenshock();
    }
  }

  // Add a shock to the queue — the worker thread will send it
  void queueShock(int durationMs, int strength) {
    std::lock_guard<std::mutex> lock(queueMutex);
    shockQueue.push({durationMs, strength});
  }

  void emptyQueue() {
    std::queue<std::pair<int, int>> emptyQ;
    shockQueue.swap(emptyQ);
  }

  bool reconnectSerial() {
    while (true) {
      serial.closeDevice();
      if (connectSerial()) return true;
      fmt::print(
          "Reconnect failed, all queued shocks dropped.\nPress any key to "
          "retry...\n");
      emptyQueue();
      system("pause");
    }
  }

  // Print detected shockers and return false if there are none
  bool listShockers() {
    if (config.ShockerIDs.empty()) {
      fmt::print(
          "No shockers configured and none found automatically.\n"
          "Please set them up in config.yml\n"
          "The program will now exit...\n");
      system("pause");
      return false;
    }

    std::string ids = "";
    for (int i = 0; i < (int)config.ShockerIDs.size(); i++) {
      if (i > 0) ids += ", ";
      ids += config.ShockerIDs[i];
    }
    fmt::print("Shockers found: {}\n", ids);
    return true;
  }

  void shutdown() {
    stopWorker = true;
    if (workerThread.joinable()) {
      workerThread.join();
    }
    serial.closeDevice();
  }

 private:
  Config& config;
  int lastShockerIndex = -1;
  serialib serial;

  std::queue<std::pair<int, int>> shockQueue;
  std::mutex queueMutex;
  std::thread workerThread;
  std::atomic<bool> stopWorker = false;

  bool scanForPishock() {
    for (int i = 1; i <= 50; i++) {
      config.serialPort = "COM" + std::to_string(i);
      fmt::print("Trying {}\n", config.serialPort);

      bool opened =
          serial.openDevice(config.serialPort.c_str(), config.baudRate) == 1;
      if (!opened) continue;

      serial.writeString("{\"cmd\": \"info\"}\n");

      bool found = false;
      for (int attempt = 0; attempt < 40; attempt++) {
        char buf[1024] = {0};
        serial.readString(buf, '\n', 1024, 1000);
        std::string response(buf);

        if (response.starts_with("TERMINALINFO: ") &&
            response.find("pishock") != std::string::npos) {
          found = true;
          // If no shockers are set in config, parse them from the response
          if (config.ShockerIDs.empty()) {
            std::string jsonStr =
                response.substr(14);  // strip "TERMINALINFO: "
            auto json = nlohmann::json::parse(jsonStr, nullptr, false);
            if (!json.is_discarded() && json.contains("shockers")) {
              for (auto& s : json["shockers"]) {
                config.pushShockerId(std::to_string(s["id"].get<int>()));
              }
            }
          }
          break;
        }
      }

      if (found) {
        startWorkerThread();
        return true;
      }
      serial.closeDevice();
    }

    fmt::print(
        "Couldn't connect to PiShock HUB, check connection and press any key "
        "to retry...\n");
    system("pause");
    return reconnectSerial();
  }

  bool scanForOpenshock() {
    for (int i = 1; i <= 50; i++) {
      config.serialPort = "COM" + std::to_string(i);

      bool opened =
          serial.openDevice(config.serialPort.c_str(), config.baudRate) == 1;
      if (!opened) continue;

      serial.writeString("domain\n");

      bool found = false;
      for (int attempt = 0; attempt < 5; attempt++) {
        char buf[64] = {0};
        serial.readString(buf, '\n', 64, 1000);
        if (std::string(buf).find("openshock") != std::string::npos) {
          found = true;
          break;
        }
      }

      if (found) {
        startWorkerThread();
        return true;
      }

      serial.closeDevice();
    }

    fmt::print(
        "Couldn't connect to OpenShock HUB, check connection and press any key "
        "to retry...\n");
    system("pause");
    return reconnectSerial();
  }

  void startWorkerThread() {
    if (!workerThread.joinable()) {
      fmt::print("Connected\n");
      workerThread = std::thread([this]() { workerLoop(); });
    }
  }

  // Runs on a background thread, pulls shocks from the queue and sends them
  void workerLoop() {
    while (!stopWorker) {
      std::unique_lock<std::mutex> lock(queueMutex);

      if (!shockQueue.empty()) {
        int durationMs = shockQueue.front().first;
        int strength = shockQueue.front().second;
        shockQueue.pop();
        lock.unlock();

        // Pick which shocker to use (random or sequential)
        std::string chosenShocker;
        std::vector<std::string>& ids = config.ShockerIDs;
        if (config.randomOrSeq) {
          chosenShocker = ids[rand() % ids.size()];
        } else {
          lastShockerIndex = (lastShockerIndex + 1) % (int)ids.size();
          chosenShocker = ids[lastShockerIndex];
        }

        sendShock(durationMs, strength, chosenShocker);
      } else {
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
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
      reconnectSerial();
      queueShock(durationMs, strength);
      return;
    }

    fmt::print("Sent shock: {}%, {}s\n", strength,
               round(durationMs / 100) / 10);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// OscListener
// Listens for OSC UDP packets and calls a callback with the path and value
// ─────────────────────────────────────────────────────────────────────────────
class OscListener {
 public:
  // "Callback" is just a shorthand for: a function that takes a path string and
  // a float
  using Callback = std::function<void(const std::string& path, float value)>;

  OscListener(int port, Callback callback) : port_(port), callback_(callback) {}

  bool start() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == INVALID_SOCKET) return false;

    BOOL reuse = TRUE;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock_, (sockaddr*)&addr, sizeof(addr)) != 0) {
      closesocket(sock_);
      return false;
    }

    running_ = true;
    thread_ = std::thread([this]() { loop(); });
    fmt::print("[OSC] Listening on UDP port {}\n", port_);
    return true;
  }

  void stop() {
    running_ = false;
    closesocket(sock_);
    if (thread_.joinable()) thread_.join();
  }

 private:
  int port_;
  Callback callback_;
  SOCKET sock_ = INVALID_SOCKET;
  std::atomic<bool> running_{false};
  std::thread thread_;

  // OSC strings are null-terminated and padded to the next 4-byte boundary
  // e.g. "hi" is stored as: 'h' 'i' '\0' '\0'  (4 bytes total)
  static std::string readOscString(const uint8_t* data, size_t len,
                                   size_t& offset) {
    std::string result;
    size_t start = offset;

    // Read until null terminator
    while (offset < len && data[offset] != 0) {
      result += (char)data[offset];
      offset++;
    }

    // Skip to the next 4-byte boundary (past the null terminator)
    size_t lengthIncludingNull = (offset - start) + 1;
    offset = start + ((lengthIncludingNull + 3) & ~3);

    return result;
  }

  // Parse one OSC message and fire the callback for each value inside it
  void parseMessage(const uint8_t* data, size_t len) {
    if (len < 8) return;

    size_t offset = 0;

    // First field is the OSC address/path (e.g. "/avatar/parameters/Shock")
    std::string path = readOscString(data, len, offset);

    // Next field is the type tag string (e.g. ",f" for one float, ",T" for bool
    // true) It must start with a comma
    if (offset >= len || data[offset] != ',') return;
    std::string typeTags = readOscString(data, len, offset);

    // Each character after the comma is the type of one argument
    for (int i = 1; i < (int)typeTags.size(); i++) {
      char tag = typeTags[i];

      if (tag == 'f') {
        // Float: 4 bytes
        if (offset + 4 > len) return;
        uint32_t raw = ((uint32_t)data[offset] << 24) |
                       ((uint32_t)data[offset + 1] << 16) |
                       ((uint32_t)data[offset + 2] << 8) |
                       (uint32_t)data[offset + 3];
        float value;
        memcpy(&value, &raw, 4);
        offset += 4;
        callback_(path, value);

      } else if (tag == 'T') {
        // Bool true
        callback_(path, 1.0f);

      } else if (tag == 'F') {
        // Bool false
        callback_(path, 0.0f);

      } else if (tag == 'i') {
        // Int32: 4 bytes
        if (offset + 4 > len) return;
        uint32_t raw = ((uint32_t)data[offset] << 24) |
                       ((uint32_t)data[offset + 1] << 16) |
                       ((uint32_t)data[offset + 2] << 8) |
                       (uint32_t)data[offset + 3];
        offset += 4;
        callback_(path, (float)(int32_t)raw);

      } else if (tag == 's') {
        // String: Skip it, we don't use strings
        readOscString(data, len, offset);
      }
    }
  }

  // Container that holds multiple OSC messages
  void parseBundle(const uint8_t* data, size_t len) {
    if (len < 16) return;
    size_t offset = 16;  // skip "#bundle\0" (8 bytes) + timetag (8 bytes)

    while (offset + 4 <= len) {
      // Each entry is prefixed with its size as a 4-byte int
      uint32_t messageSize =
          ((uint32_t)data[offset] << 24) | ((uint32_t)data[offset + 1] << 16) |
          ((uint32_t)data[offset + 2] << 8) | (uint32_t)data[offset + 3];
      offset += 4;

      if (offset + messageSize > len) break;
      if (messageSize > 0) {
        if (data[offset] == '#') {
          parseBundle(data + offset, messageSize);
        } else {
          parseMessage(data + offset, messageSize);
        }
      }
      offset += messageSize;
    }
  }

  void loop() {
    uint8_t buf[4096];
    sockaddr_in src{};
    int srcLen = sizeof(src);

    while (running_) {
      // Wait up to 200ms for a packet, then loop back to check running
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(sock_, &fds);
      timeval timeout{0, 200000};
      if (select(0, &fds, nullptr, nullptr, &timeout) <= 0) continue;

      int bytesReceived =
          recvfrom(sock_, (char*)buf, sizeof(buf), 0, (sockaddr*)&src, &srcLen);
      if (bytesReceived <= 0) continue;

      bool isBundle = bytesReceived >= 8 && memcmp(buf, "#bundle", 7) == 0;
      if (isBundle) {
        parseBundle(buf, bytesReceived);
      } else {
        parseMessage(buf, bytesReceived);
      }
    }
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// OscQueryServer:
//    HTTP server
//    OSC listener
//    mDNS advertiser
// ─────────────────────────────────────────────────────────────────────────────
class OscQueryServer {
 public:
  OscQueryServer(int oscPort, std::string serviceName,
                 std::function<void(float)> onShock)
      : oscPort_(oscPort),
        serviceName_(serviceName),
        onShock_(onShock),
        oscListener_(oscPort, [this](const std::string& path, float value) {
          onOscMessage(path, value);
        }) {}

  void setShockPath(std::string path) { shockPath_ = path; }

  bool start() {
    httpServer_.Get(
        ".*", [this](const httplib::Request& req, httplib::Response& res) {
          handleHttpRequest(req, res);
        });

    httpPort_ = httpServer_.bind_to_any_port("127.0.0.1");
    if (httpPort_ < 0) {
      fmt::print("[OSCQuery] Failed to bind HTTP server\n");
      return false;
    }
    fmt::print("[OSCQuery] HTTP server on port {}\n", httpPort_);

    httpThread_ = std::thread([this]() { httpServer_.listen_after_bind(); });

    if (!oscListener_.start()) {
      fmt::print("[OSCQuery] Failed to start OSC listener\n");
      return false;
    }

    mdns_ = std::make_unique<MdnsAdvertiser>(serviceName_, httpPort_);
    return mdns_->start();
  }

  void stop() {
    httpServer_.stop();
    if (httpThread_.joinable()) httpThread_.join();
    oscListener_.stop();
    if (mdns_) mdns_->stop();
  }

 private:
  int oscPort_;
  int httpPort_ = -1;
  std::string serviceName_;
  std::string shockPath_ = "/avatar/parameters/Shock";
  std::function<void(float)> onShock_;

  httplib::Server httpServer_;
  std::thread httpThread_;
  OscListener oscListener_;
  std::unique_ptr<MdnsAdvertiser> mdns_;

  // Remembers the last value for each OSC path so we can ignore duplicates
  std::unordered_map<std::string, float> lastValues_;

  void handleHttpRequest(const httplib::Request& req, httplib::Response& res) {
    // VRChat queries HOST_INFO as a query parameter: /?HOST_INFO
    bool isHostInfo = req.params.find("HOST_INFO") != req.params.end() ||
                      req.target.find("HOST_INFO") != std::string::npos;

    if (isHostInfo) {
      // Tell VRChat our name and which UDP port to send OSC to
      nlohmann::json response = {{"NAME", serviceName_},
                                 {"OSC_PORT", oscPort_}};
      res.set_content(response.dump(), "application/json");

    } else {
      // Return our parameter tree so VRChat knows we want to receive
      // /avatar/parameters/Shock
      std::string paramName = shockPath_.substr(shockPath_.rfind('/') + 1);
      nlohmann::json response = {
          {"FULL_PATH", "/"},
          {"CONTENTS",
           {{"avatar",
             {{"FULL_PATH", "/avatar"},
              {"CONTENTS",
               {{"parameters",
                 {{"FULL_PATH", "/avatar/parameters"},
                  {"CONTENTS",
                   {{paramName, {{"FULL_PATH", shockPath_}}}}}}}}}}}}}};
      res.set_content(response.dump(), "application/json");
    }
  }

  void onOscMessage(const std::string& path, float value) {
    // Skip if the value hasn't changed (Windows loopback can send duplicate
    // packets)
    auto it = lastValues_.find(path);
    if (it != lastValues_.end() && it->second == value) return;
    lastValues_[path] = value;

    // Log everything except leash params (too spammy)
    if (path.find("Leash") == std::string::npos) {
      fmt::print("[OSC] {} = {}\n", path, value);
    }

    if (path == shockPath_ && value > 0.0f) {
      onShock_(value);
    }
  }
};

// ─────────────────────────────────────────────────────────────────────────────
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

  OscQueryServer oscQuery(config.oscPort, config.serviceName, [&](float value) {
    shockerHub.queueShock(config.shockDuration, config.shockStrength);
  });
  oscQuery.setShockPath(config.oscPath);

  if (!oscQuery.start()) {
    fmt::print("Failed to start OSCQuery server\n");
    shockerHub.shutdown();
    return 1;
  }

  fmt::print("Running. Ctrl+C to quit.\n");
  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  fmt::print("Shutting down...\n");
  oscQuery.stop();
  shockerHub.shutdown();
  return 0;
}