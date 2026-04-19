#include "oscclient.h"

OscListener::OscListener(int port, Callback cb) : port_(port), callback_(cb) {}

bool OscListener::start() {
  sock_init();
  sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock_ == SOCK_INVAL) return false;
  int reuse = 1;
  setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  addr.sin_addr.s_addr = INADDR_ANY;
  if (bind(sock_, (sockaddr*)&addr, sizeof(addr)) != 0) {
    sock_close(sock_);
    return false;
  }
  running_ = true;
  thread_ = std::thread([this] { loop(); });
  logMsg("[OSC] Listening on UDP port {}", port_);
  return true;
}

void OscListener::stop() {
  running_ = false;
  sock_close(sock_);
  if (thread_.joinable()) thread_.join();
}

std::string OscListener::readOscString(const uint8_t* d, size_t len,
                                       size_t& off) {
  std::string s;
  size_t start = off;
  while (off < len && d[off] != 0) s += (char)d[off++];
  off = start + (((off - start) + 1 + 3) & ~3);
  return s;
}

void OscListener::parseMessage(const uint8_t* d, size_t len) {
  if (len < 8) return;
  size_t off = 0;
  std::string path = readOscString(d, len, off);
  if (off >= len || d[off] != ',') return;
  std::string tags = readOscString(d, len, off);
  for (int i = 1; i < (int)tags.size(); i++) {
    char t = tags[i];
    if (t == 'f') {
      if (off + 4 > len) return;
      uint32_t r = ((uint32_t)d[off] << 24) | ((uint32_t)d[off + 1] << 16) |
                   ((uint32_t)d[off + 2] << 8) | (uint32_t)d[off + 3];
      float v;
      memcpy(&v, &r, 4);
      off += 4;
      callback_(path, v);
    } else if (t == 'T') {
      callback_(path, 1.f);
    } else if (t == 'F') {
      callback_(path, 0.f);
    } else if (t == 'i') {
      if (off + 4 > len) return;
      uint32_t r = ((uint32_t)d[off] << 24) | ((uint32_t)d[off + 1] << 16) |
                   ((uint32_t)d[off + 2] << 8) | (uint32_t)d[off + 3];
      off += 4;
      callback_(path, (float)(int32_t)r);
    } else if (t == 's') {
      readOscString(d, len, off);
    }
  }
}

void OscListener::parseBundle(const uint8_t* d, size_t len) {
  if (len < 16) return;
  size_t off = 16;
  while (off + 4 <= len) {
    uint32_t msz = ((uint32_t)d[off] << 24) | ((uint32_t)d[off + 1] << 16) |
                   ((uint32_t)d[off + 2] << 8) | (uint32_t)d[off + 3];
    off += 4;
    if (off + msz > len) break;
    if (msz > 0)
      (d[off] == '#') ? parseBundle(d + off, msz) : parseMessage(d + off, msz);
    off += msz;
  }
}

void OscListener::loop() {
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
    if (n <= 0) continue;
    (n >= 8 && memcmp(buf, "#bundle", 7) == 0) ? parseBundle(buf, n)
                                               : parseMessage(buf, n);
  }
}

OscQueryServer::OscQueryServer(int oscPort, const std::string& sn,
                               const std::string& hostIp)
    : oscPort_(oscPort),
      serviceName_(sn),
      hostIp_(hostIp),
      oscListener_(oscPort, [this](const std::string& p, float v) {
        onOscMessage(p, v);
      }) {}

bool OscQueryServer::start() {
  httpServer_.set_read_timeout(1, 0);
  httpServer_.set_write_timeout(1, 0);
  httpServer_.set_idle_interval(0, 500000);
  httpServer_.Get(".*",
                  [this](const httplib::Request& req, httplib::Response& res) {
                    handleHttpRequest(req, res);
                  });
  httpPort_ = httpServer_.bind_to_any_port(hostIp_);
  if (httpPort_ < 0) {
    logMsg("[OSCQuery] Failed to bind HTTP server");
    return false;
  }
  logMsg("[OSCQuery] HTTP server on port {}", httpPort_);
  httpThread_ = std::thread([this] { httpServer_.listen_after_bind(); });
  if (!oscListener_.start()) {
    logMsg("[OSCQuery] Failed to start OSC listener");
    return false;
  }
  mdns_ = std::make_unique<MdnsAdvertiser>(serviceName_, httpPort_, hostIp_);
  return mdns_->start();
}

void OscQueryServer::stop() {
  httpServer_.stop();
  if (httpThread_.joinable()) httpThread_.join();
  oscListener_.stop();
  if (mdns_) mdns_->stop();
}

void OscQueryServer::setParameterPaths(const std::vector<std::string>& paths) {
  std::lock_guard<std::mutex> lock(shockMutex);
  parameterPaths_ = paths;
  pathToParameterIndex_.clear();
  for (int i = 0; i < (int)parameterPaths_.size(); ++i)
    pathToParameterIndex_[parameterPaths_[i]] = i;
}

void OscQueryServer::handleHttpRequest(const httplib::Request& req,
                                       httplib::Response& res) {
  bool isHostInfo = req.params.find("HOST_INFO") != req.params.end() ||
                    req.target.find("HOST_INFO") != std::string::npos;
  if (isHostInfo) {
    nlohmann::json r = {{"NAME", serviceName_},
                        {"OSC_PORT", oscPort_},
                        {"OSC_IP", hostIp_},
                        {"OSC_TRANSPORT", "UDP"},
                        {"EXTENSIONS",
                         {{"ACCESS", true},
                          {"CLIPMODE", false},
                          {"RANGE", true},
                          {"TYPE", true},
                          {"VALUE", true}}}};
    res.set_content(r.dump(), "application/json");
  } else {
    nlohmann::json contents = nlohmann::json::object();
    for (auto& path : parameterPaths_) {
      std::string name = path.substr(path.rfind('/') + 1);
      contents[name] = {{"FULL_PATH", path}, {"ACCESS", 2}, {"TYPE", "T"}};
    }
    nlohmann::json r = {{"FULL_PATH", "/"},
                        {"ACCESS", 0},
                        {"DESCRIPTION", "root note"},
                        {"CONTENTS",
                         {{"avatar",
                           {{"FULL_PATH", "/avatar"},
                            {"ACCESS", 0},
                            {"CONTENTS",
                             {{"parameters",
                               {{"FULL_PATH", "/avatar/parameters"},
                                {"ACCESS", 0},
                                {"CONTENTS", contents}}}}}}}}}};
    res.set_content(r.dump(), "application/json");
  }
}

void OscQueryServer::onOscMessage(const std::string& path, float value) {
  {
    std::lock_guard<std::mutex> lock(lastValuesMutex_);
    auto it = lastValues_.find(path);
    if (it != lastValues_.end() && it->second == value) return;
    lastValues_[path] = value;
  }
  if (value > 0.f) {
    auto it = pathToParameterIndex_.find(path);
    if (it != pathToParameterIndex_.end()) {
      std::lock_guard<std::mutex> lock(shockMutex);
      pendingParameterIndexes.push_back(it->second);
      shockCV.notify_one();
    }
  }
}