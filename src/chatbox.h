#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
#define SOCK_INVAL INVALID_SOCKET
#define sock_close closesocket
inline void chatbox_sock_init() {
  WSADATA w;
  WSAStartup(MAKEWORD(2, 2), &w);
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
#define SOCK_INVAL (-1)
#define sock_close close
inline void chatbox_sock_init() {}
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
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
    chatbox_sock_init();
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    clearThread_ = std::thread([this] { clearLoop(); });
  }

  ~ChatboxSender() {
    {
      std::lock_guard<std::mutex> l(clearMutex_);
      stopping_ = true;
    }
    clearCV_.notify_all();
    if (clearThread_.joinable()) clearThread_.join();
    sock_t s = sock_;
    sock_ = SOCK_INVAL;
    if (s != SOCK_INVAL) sock_close(s);
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
      std::lock_guard<std::mutex> l(clearMutex_);
      clearDeadline_ =
          std::chrono::steady_clock::now() +
          std::chrono::milliseconds(static_cast<int>(CLEAR_AFTER_S * 1000));
      clearCV_.notify_one();
    }
  }

 private:
  std::string host_;
  int port_;
  sock_t sock_ = SOCK_INVAL;
  std::mutex sendMutex_;
  std::chrono::steady_clock::time_point lastSendTime_{};

  std::thread clearThread_;
  std::mutex clearMutex_;
  std::condition_variable clearCV_;
  std::chrono::steady_clock::time_point clearDeadline_{};
  bool stopping_ = false;

  void clearLoop() {
    while (true) {
      std::unique_lock<std::mutex> l(clearMutex_);
      clearCV_.wait(l, [this] {
        return stopping_ ||
               clearDeadline_ != std::chrono::steady_clock::time_point{};
      });
      if (stopping_) return;
      auto deadline = clearDeadline_;
      l.unlock();
      std::this_thread::sleep_until(deadline);
      {
        std::lock_guard<std::mutex> l2(clearMutex_);
        if (stopping_) return;
        if (std::chrono::steady_clock::now() >= clearDeadline_) {
          clearDeadline_ = {};
          std::lock_guard<std::mutex> sl(sendMutex_);
          sendRaw("");
        }
      }
    }
  }

  static std::vector<uint8_t> buildPacket(const std::string& msg) {
    std::vector<uint8_t> pkt;
    auto appendOscStr = [&](const std::string& s) {
      for (char c : s) pkt.push_back((uint8_t)c);
      pkt.push_back(0);
      while (pkt.size() % 4 != 0) pkt.push_back(0);
    };
    appendOscStr("/chatbox/input");
    appendOscStr(",sTF");
    appendOscStr(msg);
    return pkt;
  }

  void sendRaw(const std::string& message) {
    if (sock_ == SOCK_INVAL) return;
    auto pkt = buildPacket(message);
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons((uint16_t)port_);
    inet_pton(AF_INET, host_.c_str(), &dest.sin_addr);
    sendto(sock_, (const char*)pkt.data(), (int)pkt.size(), 0, (sockaddr*)&dest,
           sizeof(dest));
  }
};