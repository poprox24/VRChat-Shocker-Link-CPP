#pragma once

#ifdef _WIN32
// Windows

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <nlohmann/json.hpp>
#include <string>

#include "httplib.h"
#include "logger.h"

namespace Notifications {

inline void sendXSOverlay(const std::string& title, const std::string& content,
                          float timeout = 3.f) {
  SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == INVALID_SOCKET) return;

  nlohmann::json j = {
      {"messageType", 1},       {"index", 1},     {"timeout", timeout},
      {"height", 110.0},        {"opacity", 1.0}, {"volume", 0.5},
      {"audioPath", ""},        {"title", title}, {"content", content},
      {"useBase64Icon", false}, {"icon", ""},     {"sourceApp", "ShockerLink"}};

  std::string payload = j.dump();
  sockaddr_in dest{};
  dest.sin_family = AF_INET;
  dest.sin_port = htons(42069);
  inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);
  sendto(sock, payload.c_str(), (int)payload.size(), 0, (sockaddr*)&dest,
         sizeof(dest));
  closesocket(sock);
}

inline void sendOVRToolkit(const std::string& title,
                           const std::string& content) {
  SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == INVALID_SOCKET) return;

  DWORD tv = 300;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(11450);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

  if (connect(sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
    closesocket(sock);
    return;
  }

  const char* handshake =
      "GET /api HTTP/1.1\r\n"
      "Host: 127.0.0.1:11450\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
      "Sec-WebSocket-Version: 13\r\n\r\n";
  send(sock, handshake, (int)strlen(handshake), 0);

  char buf[512] = {};
  int n = recv(sock, buf, sizeof(buf) - 1, 0);
  if (n <= 0 || std::string(buf).find("101") == std::string::npos) {
    closesocket(sock);
    return;
  }

  std::string innerJson = fmt::format(
      "{{'title': '{}', 'body': '{}', 'icon': null}}", title, content);
  nlohmann::json outer = {{"messageType", "SendNotification"},
                          {"json", innerJson}};
  std::string payload = outer.dump();

  std::vector<uint8_t> frame;
  frame.push_back(0x81);
  size_t len = payload.size();
  if (len <= 125) {
    frame.push_back(0x80 | (uint8_t)len);
  } else {
    frame.push_back(0x80 | 126);
    frame.push_back((len >> 8) & 0xFF);
    frame.push_back(len & 0xFF);
  }
  uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
  for (auto b : mask) frame.push_back(b);
  for (size_t i = 0; i < len; i++)
    frame.push_back((uint8_t)payload[i] ^ mask[i % 4]);

  send(sock, (char*)frame.data(), (int)frame.size(), 0);
  closesocket(sock);
}

}  // namespace Notifications
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>
#include <string>

#include "httplib.h"
#include "logger.h"

namespace Notifications {

// Linux does not have XSOverlay but WayVR uses its notification system
inline void sendXSOverlay(const std::string& title, const std::string& content,
                          float timeout = 3.f) {
  // POSIX UDP implementation - identical JSON payload to Windows
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) return;

  nlohmann::json j = {
      {"messageType", 1},       {"index", 1},     {"timeout", timeout},
      {"height", 110.0},        {"opacity", 1.0}, {"volume", 0.5},
      {"audioPath", ""},        {"title", title}, {"content", content},
      {"useBase64Icon", false}, {"icon", ""},     {"sourceApp", "ShockerLink"}};

  std::string payload = j.dump();
  sockaddr_in dest{};
  dest.sin_family = AF_INET;
  dest.sin_port = htons(42069);
  inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);
  sendto(sock, payload.c_str(), (int)payload.size(), 0, (sockaddr*)&dest,
         sizeof(dest));
  close(sock);
}

inline void sendOVRToolkit(const std::string&, const std::string&) {}

}  // namespace Notifications
#endif