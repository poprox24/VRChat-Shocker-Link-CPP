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
  MdnsAdvertiser(const std::string& sn, int port, const std::string& hostIp)
      : serviceName_(sn), httpPort_(port), hostIp_(hostIp) {}

  bool start() {
    mdns_sock_init();
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == SOCK_INVAL) return false;

    int reuse = 1;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
#if defined(SO_REUSEPORT) && !defined(_WIN32)
    setsockopt(sock_, SOL_SOCKET, SO_REUSEPORT, (char*)&reuse, sizeof(reuse));
#endif

    char hostname[256] = {};
    gethostname(hostname, sizeof(hostname));
    hostname_ = hostname;

    int loop = 1;
    setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_LOOP, (char*)&loop,
               sizeof(loop));
    int ttl = 255;
    setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_TTL, (char*)&ttl, sizeof(ttl));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(5353);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock_, (sockaddr*)&addr, sizeof(addr)) != 0) {
      // Avahi/systemd-resolved probably holds port 5353 - non-fatal, just no
      // auto-discovery
      logMsg("[mDNS] Bind to 5353 failed - mDNS disabled (avahi running?)");
      sock_close(sock_);
      sock_ = SOCK_INVAL;
      return true;
    }

    ip_mreq mreq{};
    inet_pton(AF_INET, "224.0.0.251", &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = INADDR_ANY;
    setsockopt(sock_, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq,
               sizeof(mreq));

    for (int i = 0; i < 3; i++) {
      sendAnnouncement();
      if (i < 2) std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    running_ = true;
    listenerThread_ = std::thread(&MdnsAdvertiser::listenerLoop, this);
    logMsg("[mDNS] Advertising {} on port {}", serviceName_, httpPort_);
    return true;
  }

  void stop() {
    running_ = false;
    if (sock_ != SOCK_INVAL) sock_close(sock_);
    if (listenerThread_.joinable()) listenerThread_.join();
  }

 private:
  std::string serviceName_, hostname_, hostIp_;
  int httpPort_;
  sock_t sock_ = SOCK_INVAL;
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
  static void appendU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(v >> 8);
    b.push_back(v & 0xFF);
  }
  static void appendU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(v >> 24);
    b.push_back((v >> 16) & 0xFF);
    b.push_back((v >> 8) & 0xFF);
    b.push_back(v & 0xFF);
  }

  std::vector<uint8_t> buildPacket() {
    std::string ptrName = "_oscjson._tcp.local";
    std::string instanceName = serviceName_ + "._oscjson._tcp.local";
    std::string hostName = hostname_ + ".local";
    std::vector<uint8_t> buf;
    appendU16(buf, 0);
    appendU16(buf, 0x8400);
    appendU16(buf, 0);
    appendU16(buf, 1);
    appendU16(buf, 0);
    appendU16(buf, 3);

    appendName(buf, ptrName);
    appendU16(buf, 12);
    appendU16(buf, 0x0001);
    appendU32(buf, 4500);
    std::vector<uint8_t> ptrRd;
    appendName(ptrRd, instanceName);
    appendU16(buf, (uint16_t)ptrRd.size());
    buf.insert(buf.end(), ptrRd.begin(), ptrRd.end());

    appendName(buf, instanceName);
    appendU16(buf, 33);
    appendU16(buf, 0x0001);
    appendU32(buf, 4500);
    std::vector<uint8_t> srvRd;
    appendU16(srvRd, 0);
    appendU16(srvRd, 0);
    appendU16(srvRd, (uint16_t)httpPort_);
    appendName(srvRd, hostName);
    appendU16(buf, (uint16_t)srvRd.size());
    buf.insert(buf.end(), srvRd.begin(), srvRd.end());

    appendName(buf, instanceName);
    appendU16(buf, 16);
    appendU16(buf, 0x0001);
    appendU32(buf, 4500);
    std::string txtVal = "txtvers=1";
    appendU16(buf, (uint16_t)(1 + txtVal.size()));
    buf.push_back((uint8_t)txtVal.size());
    for (char c : txtVal) buf.push_back((uint8_t)c);

    appendName(buf, hostName);
    appendU16(buf, 1);
    appendU16(buf, 0x0001);
    appendU32(buf, 120);
    appendU16(buf, 4);
    in_addr addr;
    inet_pton(AF_INET, hostIp_.c_str(), &addr);
    uint32_t ip = ntohl(addr.s_addr);
    buf.push_back((ip >> 24) & 0xFF);
    buf.push_back((ip >> 16) & 0xFF);
    buf.push_back((ip >> 8) & 0xFF);
    buf.push_back(ip & 0xFF);
    return buf;
  }

  void sendAnnouncement() {
    if (sock_ == SOCK_INVAL) return;
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
      if (select(SOCK_NFDS(sock_), &fds, nullptr, nullptr, &tv) <= 0) continue;
      int n = recvfrom(sock_, (char*)buf, sizeof(buf), 0, (sockaddr*)&src,
                       (socklen_t*)&srcLen);
      if (n < 12) continue;
      uint16_t flags = (buf[2] << 8) | buf[3];
      if (flags & 0x8000) continue;
      uint16_t qdcount = (buf[4] << 8) | buf[5];
      size_t offset = 12;
      bool shouldRespond = false;
      for (int q = 0; q < qdcount && offset < (size_t)n; q++) {
        std::string qname = parseName(buf, n, offset);
        if (offset + 4 > (size_t)n) break;
        uint16_t qtype = (buf[offset] << 8) | buf[offset + 1];
        offset += 4;
        if ((qname == "_oscjson._tcp.local" || qname == "_oscjson._tcp") &&
            (qtype == 12 || qtype == 255))
          shouldRespond = true;
      }
      if (shouldRespond) sendAnnouncement();
    }
  }
};