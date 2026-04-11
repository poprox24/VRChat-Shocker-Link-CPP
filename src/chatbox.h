#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
#define SOCK_INVAL INVALID_SOCKET
#define sock_close closesocket
void chatbox_sock_init();
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
#define SOCK_INVAL (-1)
#define sock_close close
void chatbox_sock_init();
#endif

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class ChatboxSender {
 public:
  static constexpr float MESSAGE_COOLDOWN_S = 1.2f;
  static constexpr float CLEAR_AFTER_S = 4.0f;

  ChatboxSender(const std::string& host = "127.0.0.1", int port = 9000);
  ~ChatboxSender();

  void send(const std::string& message, bool clearAfter = true);

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
  static std::vector<uint8_t> buildPacket(const std::string& msg);
  void sendRaw(const std::string& message);
};