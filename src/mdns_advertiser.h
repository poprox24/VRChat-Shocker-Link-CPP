#pragma once
#ifdef _WIN32
// Windows

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "logger.h"

// I have no idea how this works, I didn't write it but it works with VRChats
// stupid implementation of OSCQuery
class MdnsAdvertiser {
 public:
  MdnsAdvertiser(const std::string& serviceName, int httpPort)
      : serviceName_(serviceName), httpPort_(httpPort) {}

  bool start() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == INVALID_SOCKET) return false;

    BOOL reuse = TRUE;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    // Get local IP
    char hostname[256] = {};
    gethostname(hostname, sizeof(hostname));
    addrinfo hints{}, *addrRes = nullptr;
    hints.ai_family = AF_INET;
    ULONG localAddr = INADDR_ANY;
    if (getaddrinfo(hostname, nullptr, &hints, &addrRes) == 0 && addrRes) {
      localAddr = ((sockaddr_in*)addrRes->ai_addr)->sin_addr.s_addr;
      char ipStr[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &localAddr, ipStr, sizeof(ipStr));
      freeaddrinfo(addrRes);
    }

    // Set outgoing multicast interface
    setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_IF, (char*)&localAddr,
               sizeof(localAddr));

    BOOL loop = FALSE;  // disable loopback so we don't receive our own packets
    setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_LOOP, (char*)&loop,
               sizeof(loop));

    int ttl = 255;
    setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_TTL, (char*)&ttl, sizeof(ttl));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5353);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock_, (sockaddr*)&addr, sizeof(addr)) != 0) {
      closesocket(sock_);
      return false;
    }

    ip_mreq mreq{};
    inet_pton(AF_INET, "224.0.0.251", &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = localAddr;
    if (setsockopt(sock_, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq,
                   sizeof(mreq)) != 0) {
    }

    hostname_ = std::string(hostname);

    for (int i = 0; i < 3; i++) {
      sendAnnouncement();
      if (i < 2) std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    running_ = true;
    listenerThread_ = std::thread(&MdnsAdvertiser::listenerLoop, this);
    logMsg("[mDNS] Advertising {} on port {}\n", serviceName_, httpPort_);
    return true;
  }

  void stop() {
    running_ = false;
    closesocket(sock_);
    if (listenerThread_.joinable()) listenerThread_.join();
  }

 private:
  std::string serviceName_;
  int httpPort_;
  std::string hostname_;
  SOCKET sock_ = INVALID_SOCKET;
  std::atomic<bool> running_{false};
  std::thread listenerThread_;

  static void appendName(std::vector<uint8_t>& buf, const std::string& name) {
    std::string n = name;
    if (!n.empty() && n.back() == '.') n.pop_back();
    size_t start = 0;
    while (true) {
      size_t dot = n.find('.', start);
      std::string label = (dot == std::string::npos)
                              ? n.substr(start)
                              : n.substr(start, dot - start);
      buf.push_back((uint8_t)label.size());
      for (char c : label) buf.push_back((uint8_t)c);
      if (dot == std::string::npos) break;
      start = dot + 1;
    }
    buf.push_back(0);
  }

  static void appendU16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back((v >> 8) & 0xFF);
    buf.push_back(v & 0xFF);
  }

  static void appendU32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back((v >> 24) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
    buf.push_back(v & 0xFF);
  }

  std::vector<uint8_t> buildPacket() {
    std::string ptrName = "_oscjson._tcp.local";
    std::string instanceName = serviceName_ + "._oscjson._tcp.local";
    std::string hostName = hostname_ + ".local";
    uint32_t ttlLong = 4500;  // 75 min - for service records
    uint32_t ttlShort = 120;  // 2 min  - for A record (matches VRChat)

    std::vector<uint8_t> buf;

    // Header: QR=1, AA=1 (authoritative), 1 answer, 3 additional
    // Per RFC 6762/6763: PTR in answer, SRV+TXT+A in additional records
    appendU16(buf, 0);       // ID
    appendU16(buf, 0x8400);  // Flags: QR=1, AA=1
    appendU16(buf, 0);       // QDCOUNT
    appendU16(buf, 1);       // ANCOUNT  - PTR only
    appendU16(buf, 0);       // NSCOUNT
    appendU16(buf, 3);       // ARCOUNT  - SRV, TXT, A

    // === ANSWER SECTION ===
    // PTR: _oscjson._tcp.local -> instanceName (no cache flush on PTR)
    appendName(buf, ptrName);
    appendU16(buf, 12);      // TYPE PTR
    appendU16(buf, 0x0001);  // CLASS IN, cache flush = false
    appendU32(buf, ttlLong);
    std::vector<uint8_t> ptrRd;
    appendName(ptrRd, instanceName);
    appendU16(buf, (uint16_t)ptrRd.size());
    buf.insert(buf.end(), ptrRd.begin(), ptrRd.end());

    // === ADDITIONAL SECTION ===
    // SRV: instanceName -> 0 0 httpPort_ hostName
    appendName(buf, instanceName);
    appendU16(buf, 33);      // TYPE SRV
    appendU16(buf, 0x0001);  // CLASS IN, no cache flush
    appendU32(buf, ttlLong);
    std::vector<uint8_t> srvRd;
    appendU16(srvRd, 0);  // priority
    appendU16(srvRd, 0);  // weight
    appendU16(srvRd, (uint16_t)httpPort_);
    appendName(srvRd, hostName);
    appendU16(buf, (uint16_t)srvRd.size());
    buf.insert(buf.end(), srvRd.begin(), srvRd.end());

    // TXT: instanceName -> "txtvers=1"
    // DNS-SD RFC 6763 §6.7 requires txtvers=1
    appendName(buf, instanceName);
    appendU16(buf, 16);      // TYPE TXT
    appendU16(buf, 0x0001);  // CLASS IN, no cache flush
    appendU32(buf, ttlLong);
    std::string txtVal = "txtvers=1";
    appendU16(buf, (uint16_t)(1 + txtVal.size()));  // RDLENGTH
    buf.push_back((uint8_t)txtVal.size());
    for (char c : txtVal) buf.push_back((uint8_t)c);

    // A: hostName -> 127.0.0.1
    appendName(buf, hostName);
    appendU16(buf, 1);       // TYPE A
    appendU16(buf, 0x0001);  // CLASS IN, no cache flush
    appendU32(buf, ttlShort);
    appendU16(buf, 4);
    buf.push_back(127);
    buf.push_back(0);
    buf.push_back(0);
    buf.push_back(1);

    return buf;
  }

  void sendAnnouncement() {
    auto pkt = buildPacket();
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(5353);
    inet_pton(AF_INET, "224.0.0.251", &dest.sin_addr);
    sendto(sock_, (char*)pkt.data(), (int)pkt.size(), 0, (sockaddr*)&dest,
           sizeof(dest));
  }

  static std::string parseName(const uint8_t* data, size_t len,
                               size_t& offset) {
    std::string name;
    int jumps = 0;
    size_t pos = offset;
    bool jumped = false;
    while (pos < len) {
      uint8_t labelLen = data[pos];
      if ((labelLen & 0xC0) == 0xC0) {
        if (pos + 1 >= len) break;
        size_t ptr = ((labelLen & 0x3F) << 8) | data[pos + 1];
        if (!jumped) offset = pos + 2;
        jumped = true;
        pos = ptr;
        if (++jumps > 10) break;
        continue;
      }
      pos++;
      if (labelLen == 0) break;
      if (!name.empty()) name += '.';
      for (int i = 0; i < labelLen && pos < len; i++) name += (char)data[pos++];
    }
    if (!jumped) offset = pos;
    return name;
  }

  void listenerLoop() {
    uint8_t buf[4096];
    sockaddr_in src{};
    int srcLen = sizeof(src);

    while (running_) {
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(sock_, &fds);
      timeval tv{0, 200000};
      if (select(0, &fds, nullptr, nullptr, &tv) <= 0) continue;

      int n =
          recvfrom(sock_, (char*)buf, sizeof(buf), 0, (sockaddr*)&src, &srcLen);
      if (n < 12) continue;

      uint16_t flags = (buf[2] << 8) | buf[3];
      if (flags & 0x8000) continue;  // skip responses

      uint16_t qdcount = (buf[4] << 8) | buf[5];
      size_t offset = 12;
      bool shouldRespond = false;

      for (int q = 0; q < qdcount && offset < (size_t)n; q++) {
        std::string qname = parseName(buf, n, offset);
        if (offset + 4 > (size_t)n) break;
        uint16_t qtype = (buf[offset] << 8) | buf[offset + 1];
        offset += 4;
        if ((qname == "_oscjson._tcp.local" || qname == "_oscjson._tcp") &&
            (qtype == 12 || qtype == 255)) {
          shouldRespond = true;
        }
      }

      if (shouldRespond) sendAnnouncement();
    }
  }
};
#else
// Linux

#include <string>
class MdnsAdvertiser {
 public:
  MdnsAdvertiser(const std::string&, int) {}
  bool start() { return true; }
  void stop() {}
};
#endif