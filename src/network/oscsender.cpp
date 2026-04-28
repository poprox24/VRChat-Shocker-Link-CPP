#include "oscsender.h"

// Construction / destruction
OscSender::OscSender(const std::string& host, int port)
    : host_(host), port_(port) {
  chatbox_sock_init();
  sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  clearThread_ = std::thread([this] { clearLoop(); });
}

OscSender::~OscSender() {
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

// Shared OSC helper
void OscSender::appendOscString(std::vector<uint8_t>& pkt,
                                const std::string& s) {
  for (char c : s) pkt.push_back(static_cast<uint8_t>(c));
  pkt.push_back(0);
  while (pkt.size() % 4 != 0) pkt.push_back(0);
}

// Packet builders
std::vector<uint8_t> OscSender::buildChatboxPacket(const std::string& msg) {
  std::vector<uint8_t> pkt;
  appendOscString(pkt, "/chatbox/input");
  appendOscString(pkt, ",sTF");
  appendOscString(pkt, msg);
  return pkt;
}

std::vector<uint8_t> OscSender::buildFloatPacket(const std::string& path,
                                                 float value) {
  std::vector<uint8_t> pkt;
  appendOscString(pkt, path);
  appendOscString(pkt, ",f");
  uint32_t raw;
  std::memcpy(&raw, &value, 4);
  pkt.push_back((raw >> 24) & 0xFF);
  pkt.push_back((raw >> 16) & 0xFF);
  pkt.push_back((raw >> 8) & 0xFF);
  pkt.push_back(raw & 0xFF);
  return pkt;
}

std::vector<uint8_t> OscSender::buildBoolPacket(const std::string& path,
                                                bool value) {
  std::vector<uint8_t> pkt;
  appendOscString(pkt, path);
  appendOscString(pkt, value ? ",T" : ",F");
  // Boolean OSC types carry no payload bytes.
  return pkt;
}

std::vector<uint8_t> OscSender::buildIntPacket(const std::string& path,
                                               int value) {
  std::vector<uint8_t> pkt;
  appendOscString(pkt, path);
  appendOscString(pkt, ",i");
  auto raw = static_cast<uint32_t>(value);
  pkt.push_back((raw >> 24) & 0xFF);
  pkt.push_back((raw >> 16) & 0xFF);
  pkt.push_back((raw >> 8) & 0xFF);
  pkt.push_back(raw & 0xFF);
  return pkt;
}

std::vector<uint8_t> OscSender::buildStringPacket(const std::string& path,
                                                  const std::string& value) {
  std::vector<uint8_t> pkt;
  appendOscString(pkt, path);
  appendOscString(pkt, ",s");
  appendOscString(pkt, value);
  return pkt;
}

// Raw send
void OscSender::sendRawPacket(const std::vector<uint8_t>& packet) {
  if (sock_ == SOCK_INVAL) return;
  sockaddr_in dest{};
  dest.sin_family = AF_INET;
  dest.sin_port = htons(static_cast<uint16_t>(port_));
  inet_pton(AF_INET, host_.c_str(), &dest.sin_addr);
  sendto(sock_, reinterpret_cast<const char*>(packet.data()),
         static_cast<int>(packet.size()), 0, reinterpret_cast<sockaddr*>(&dest),
         sizeof(dest));
}

// Public send methods
void OscSender::send(const std::string& message, bool clearAfter) {
  std::lock_guard<std::mutex> lock(sendMutex_);

  // Shock messages (⚡) bypass the cooldown so they always get through.
  bool isShock = message.find("\xe2\x9a\xa1") != std::string::npos;
  if (!isShock) {
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - lastSendTime_).count();
    if (elapsed < MESSAGE_COOLDOWN_S) return;
  }

  lastSendTime_ = std::chrono::steady_clock::now();
  sendRawPacket(buildChatboxPacket(message));

  if (clearAfter) {
    std::lock_guard<std::mutex> l(clearMutex_);
    clearDeadline_ =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<int>(CLEAR_AFTER_S * 1000));
    clearCV_.notify_one();
  }
}

void OscSender::sendFloat(const std::string& path, float value) {
  std::lock_guard<std::mutex> lock(sendMutex_);
  sendRawPacket(buildFloatPacket(path, value));
}

void OscSender::sendBool(const std::string& path, bool value) {
  std::lock_guard<std::mutex> lock(sendMutex_);
  sendRawPacket(buildBoolPacket(path, value));
}

void OscSender::sendInt(const std::string& path, int value) {
  std::lock_guard<std::mutex> lock(sendMutex_);
  sendRawPacket(buildIntPacket(path, value));
}

void OscSender::sendString(const std::string& path, const std::string& value) {
  std::lock_guard<std::mutex> lock(sendMutex_);
  sendRawPacket(buildStringPacket(path, value));
}

// Chatbox clear loop
void OscSender::clearLoop() {
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
        sendRawPacket(buildChatboxPacket(""));
      }
    }
  }
}