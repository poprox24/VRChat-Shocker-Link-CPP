#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <unordered_map>

#include "httplib.h"
#include "mdns_advertiser.h"

class OscListener {
 public:
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
    logMsg("[OSC] Listening on UDP port {}\n", port_);
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

  static std::string readOscString(const uint8_t* data, size_t len,
                                   size_t& offset) {
    std::string result;
    size_t start = offset;

    while (offset < len && data[offset] != 0) {
      result += (char)data[offset];
      offset++;
    }

    size_t lengthIncludingNull = (offset - start) + 1;
    offset = start + ((lengthIncludingNull + 3) & ~3);

    return result;
  }

  void parseMessage(const uint8_t* data, size_t len) {
    if (len < 8) return;

    size_t offset = 0;

    // First field is the OSC address/path (e.g. "/avatar/parameters/Shock")
    std::string path = readOscString(data, len, offset);

    if (offset >= len || data[offset] != ',') return;
    std::string typeTags = readOscString(data, len, offset);

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

  void parseBundle(const uint8_t* data, size_t len) {
    if (len < 16) return;
    size_t offset = 16;

    while (offset + 4 <= len) {
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

class OscQueryServer {
 public:
  std::mutex shockMutex;
  std::condition_variable shockCV;
  bool shockPending = false;

  OscQueryServer(int oscPort, std::string serviceName)
      : oscPort_(oscPort),
        serviceName_(serviceName),
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
      logMsg("[OSCQuery] Failed to bind HTTP server\n");
      return false;
    }
    logMsg("[OSCQuery] HTTP server on port {}\n", httpPort_);

    httpThread_ = std::thread([this]() { httpServer_.listen_after_bind(); });

    if (!oscListener_.start()) {
      logMsg("[OSCQuery] Failed to start OSC listener\n");
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

  httplib::Server httpServer_;
  std::thread httpThread_;
  OscListener oscListener_;
  std::unique_ptr<MdnsAdvertiser> mdns_;

  // Remembers the last value for each OSC path so we can ignore duplicates
  std::unordered_map<std::string, float> lastValues_;

  void handleHttpRequest(const httplib::Request& req, httplib::Response& res) {
    bool isHostInfo = req.params.find("HOST_INFO") != req.params.end() ||
                      req.target.find("HOST_INFO") != std::string::npos;

    if (isHostInfo) {
      nlohmann::json response = {{"NAME", serviceName_},
                                 {"OSC_PORT", oscPort_}};
      res.set_content(response.dump(), "application/json");

    } else {
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
    auto it = lastValues_.find(path);
    if (it != lastValues_.end() && it->second == value) return;
    lastValues_[path] = value;

    if (path == shockPath_ && value > 0.0f) {
      {
        std::lock_guard<std::mutex> lock(shockMutex);
        shockPending = true;
      }
      shockCV.notify_one();
    }
  }
};