#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "logger.h"

class ChatboxSender {
 public:
  static constexpr float MESSAGE_COOLDOWN_S = 1.2f;
  static constexpr float CLEAR_AFTER_S = 4.0f;

  ChatboxSender(const std::string& host = "127.0.0.1", int port = 9000)
      : host_(host), port_(port) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  }

  ~ChatboxSender() {
    clearGeneration_++;
    SOCKET s = sock_;
    sock_ = INVALID_SOCKET;
    if (s != INVALID_SOCKET) closesocket(s);
  }

  void send(const std::string& message, bool clearAfter = true) {
    std::lock_guard<std::mutex> lock(sendMutex_);

    bool isShock = message.find("\xe2\x9a\xa1") != std::string::npos;

    if (!isShock) {
      auto now = std::chrono::steady_clock::now();
      float elapsed = std::chrono::duration<float>(now - lastSendTime_).count();
      if (elapsed < MESSAGE_COOLDOWN_S) return;
    }

    lastSendTime_ = std::chrono::steady_clock::now();
    sendRaw(message);

    if (clearAfter) {
      int gen = ++clearGeneration_;

      std::thread([this, gen]() {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(static_cast<int>(CLEAR_AFTER_S * 1000)));
        if (clearGeneration_.load() == gen) {
          std::lock_guard<std::mutex> l(sendMutex_);
          sendRaw("");
        }
      }).detach();
    }
  }

 private:
  std::string host_;
  int port_;
  SOCKET sock_ = INVALID_SOCKET;
  std::mutex sendMutex_;
  std::chrono::steady_clock::time_point lastSendTime_{};
  std::atomic<int> clearGeneration_{0};

  static std::vector<uint8_t> buildPacket(const std::string& msg) {
    std::vector<uint8_t> pkt;

    auto appendOscStr = [&](const std::string& s) {
      for (char c : s) pkt.push_back(static_cast<uint8_t>(c));
      pkt.push_back(0);
      while (pkt.size() % 4 != 0) pkt.push_back(0);
    };

    appendOscStr("/chatbox/input");
    appendOscStr(",sTF");
    appendOscStr(msg);

    return pkt;
  }

  void sendRaw(const std::string& message) {
    if (sock_ == INVALID_SOCKET) return;
    auto pkt = buildPacket(message);
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(static_cast<u_short>(port_));
    inet_pton(AF_INET, host_.c_str(), &dest.sin_addr);
    sendto(sock_, reinterpret_cast<const char*>(pkt.data()),
           static_cast<int>(pkt.size()), 0, reinterpret_cast<sockaddr*>(&dest),
           sizeof(dest));
  }
};