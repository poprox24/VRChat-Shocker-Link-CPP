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
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "logger.h"

class OscSender {
 public:
  static constexpr float MESSAGE_COOLDOWN_S = 1.2f;
  static constexpr float CLEAR_AFTER_S = 4.0f;

  OscSender(const std::string& host = "127.0.0.1", int port = 9000);
  ~OscSender();

  // VRChat chatbox: sends to /chatbox/input with sTF type tags.
  void send(const std::string& message, bool clearAfter = true);

  // Generic OSC parameter sends — no cooldown, fire-and-forget.
  void sendFloat(const std::string& path, float value);
  void sendBool(const std::string& path, bool value);
  void sendInt(const std::string& path, int value);
  void sendString(const std::string& path, const std::string& value);

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

  void clearLoop();

  // Shared OSC string helper: appends s + null + padding to 4-byte boundary.
  static void appendOscString(std::vector<uint8_t>& pkt, const std::string& s);

  // Per-type packet builders.
  static std::vector<uint8_t> buildChatboxPacket(const std::string& msg);
  static std::vector<uint8_t> buildFloatPacket(const std::string& path,
                                               float value);
  static std::vector<uint8_t> buildBoolPacket(const std::string& path,
                                              bool value);
  static std::vector<uint8_t> buildIntPacket(const std::string& path,
                                             int value);
  static std::vector<uint8_t> buildStringPacket(const std::string& path,
                                                const std::string& value);

  void sendRawPacket(const std::vector<uint8_t>& packet);
};