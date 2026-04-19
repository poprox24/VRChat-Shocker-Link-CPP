#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
#define SOCK_INVAL INVALID_SOCKET
#define sock_close closesocket
#define SOCK_NFDS(s) 0
inline void mdns_sock_init() {
  WSADATA w;
  WSAStartup(MAKEWORD(2, 2), &w);
}
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
#define SOCK_INVAL (-1)
#define sock_close close
#define SOCK_NFDS(s) ((s) + 1)
inline void mdns_sock_init() {}
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "logger.h"

class MdnsAdvertiser {
 public:
  MdnsAdvertiser(const std::string& sn, int port, const std::string& hostIp);

  bool start();
  void stop();

 private:
  std::string serviceName_, hostname_, hostIp_;
  int httpPort_;
  sock_t sock_ = SOCK_INVAL;
  std::atomic<bool> running_{false};
  std::thread listenerThread_;

  static void appendName(std::vector<uint8_t>& buf, const std::string& name);
  static void appendU16(std::vector<uint8_t>& b, uint16_t v);
  static void appendU32(std::vector<uint8_t>& b, uint32_t v);
  std::vector<uint8_t> buildPacket();
  void sendAnnouncement();
  static std::string parseName(const uint8_t* data, size_t len, size_t& offset);
  void listenerLoop();
};