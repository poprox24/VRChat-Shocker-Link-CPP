#include "session.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <random>

#include "logger.h"

using json = nlohmann::json;

SessionManager::SessionManager(std::string serverBaseUrl,
                               RemoteShockCallback onRemoteShock)
    : serverBase_(std::move(serverBaseUrl)),
      onRemoteShock_(std::move(onRemoteShock)) {
  ws_.onMessage = [this](const std::string& raw) { handleMessage(raw); };
  ws_.onState = [this](bool c, const std::string& r) { handleState(c, r); };
}

SessionManager::~SessionManager() { ws_.close(); }

void SessionManager::setServerUrl(const std::string& url) {
  std::lock_guard<std::mutex> lk(mutex_);
  serverBase_ = url;
}

std::string SessionManager::makeCode() {
  // Unambiguous alphabet (no 0/O, 1/I).
  static const char* kAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  static std::mt19937 rng{std::random_device{}()};
  std::uniform_int_distribution<int> dist(0, 31);
  std::string out;
  for (int i = 0; i < 6; ++i) out += kAlphabet[dist(rng)];
  return out;
}

std::string SessionManager::urlEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char ch : s) {
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      out += (char)ch;
    } else {
      out += '%';
      out += hex[ch >> 4];
      out += hex[ch & 0xF];
    }
  }
  return out;
}

void SessionManager::createSession(const std::string& displayName) {
  std::string code = makeCode();
  {
    std::lock_guard<std::mutex> lk(mutex_);
    code_ = code;
    myName_ = displayName;
    members_.clear();
    pending_.clear();
    lastError_.clear();
  }
  connect(code, displayName);
}

void SessionManager::joinSession(const std::string& code,
                                 const std::string& displayName) {
  std::string upper = code;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
  {
    std::lock_guard<std::mutex> lk(mutex_);
    code_ = upper;
    myName_ = displayName;
    members_.clear();
    pending_.clear();
    lastError_.clear();
  }
  connect(upper, displayName);
}

void SessionManager::connect(const std::string& code, const std::string& name) {
  std::string base;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    base = serverBase_;
  }
  status_ = SessionStatus::Connecting;
  notify();

  std::string url = base + "/room/" + code + "?name=" + urlEncode(name);
  if (!ws_.connect(url)) {
    status_ = SessionStatus::Idle;
    {
      std::lock_guard<std::mutex> lk(mutex_);
      if (lastError_.empty()) lastError_ = "Connection failed";
    }
    notify();
  }
  // On success we stay in Connecting until the server sends
  // "approved"/"pending".
}

void SessionManager::approve(const std::string& sid) {
  if (!isActive()) return;
  json j = {{"type", "approve"}, {"sid", sid}};
  ws_.send(j.dump());
}

void SessionManager::deny(const std::string& sid) {
  if (!isActive()) return;
  json j = {{"type", "deny"}, {"sid", sid}};
  ws_.send(j.dump());
  // Drop them from our local pending list immediately for snappy UI.
  {
    std::lock_guard<std::mutex> lk(mutex_);
    pending_.erase(
        std::remove_if(pending_.begin(), pending_.end(),
                       [&](const PendingJoin& p) { return p.sid == sid; }),
        pending_.end());
  }
  notify();
}

void SessionManager::leave() {
  ws_.close();
  {
    std::lock_guard<std::mutex> lk(mutex_);
    members_.clear();
    pending_.clear();
    code_.clear();
    mySid_.clear();
  }
  isHost_ = false;
  status_ = SessionStatus::Idle;
  notify();
}

void SessionManager::broadcastShock(int strength, int durationMs,
                                    bool vibrate) {
  if (!isActive()) return;
  json j = {{"type", "shock"},
            {"strength", strength},
            {"duration_ms", durationMs},
            {"vibrate", vibrate}};
  ws_.send(j.dump());
}

void SessionManager::handleMessage(const std::string& raw) {
  json j;
  try {
    j = json::parse(raw);
  } catch (...) {
    return;
  }
  const std::string type = j.value("type", "");

  // Incoming shock: fire locally. Call the callback WITHOUT holding our mutex.
  if (type == "shock") {
    int strength = j.value("strength", 0);
    int durMs = j.value("duration_ms", 0);
    bool vib = j.value("vibrate", false);
    if (onRemoteShock_) onRemoteShock_(strength, durMs, vib);
    return;
  }

  bool changed = true;
  {
    std::lock_guard<std::mutex> lk(mutex_);

    if (type == "approved") {
      isHost_ = j.value("host", false);
      mySid_ = j.value("sid", mySid_);
      members_.clear();
      if (j.contains("members") && j["members"].is_array()) {
        for (auto& m : j["members"])
          members_.push_back({m.value("name", ""), m.value("sid", "")});
      }
      pending_.clear();
      status_ = SessionStatus::Active;
      logMsg("[Session] Joined room {} ({} member(s))", code_,
             (int)members_.size());

    } else if (type == "pending") {
      mySid_ = j.value("sid", mySid_);
      status_ = SessionStatus::Pending;
      logMsg("[Session] Waiting for approval...");

    } else if (type == "join_request") {
      pending_.push_back({j.value("name", ""), j.value("sid", "")});
      logMsg("[Session] {} wants to join", j.value("name", ""));

    } else if (type == "joined") {
      std::string sid = j.value("sid", "");
      pending_.erase(
          std::remove_if(pending_.begin(), pending_.end(),
                         [&](const PendingJoin& p) { return p.sid == sid; }),
          pending_.end());
      bool exists = false;
      for (auto& m : members_)
        if (m.sid == sid) exists = true;
      if (!exists) members_.push_back({j.value("name", ""), sid});

    } else if (type == "left") {
      std::string sid = j.value("sid", "");
      members_.erase(
          std::remove_if(members_.begin(), members_.end(),
                         [&](const SessionMember& m) { return m.sid == sid; }),
          members_.end());

    } else if (type == "denied") {
      lastError_ = "Join request was denied";
      status_ = SessionStatus::Idle;
      logMsg("[Session] Join denied");

    } else {
      changed = false;
    }
  }

  if (changed) notify();
}

void SessionManager::handleState(bool connected, const std::string& reason) {
  if (!connected) {
    SessionStatus s = status_.load();
    if (s == SessionStatus::Active || s == SessionStatus::Pending ||
        s == SessionStatus::Connecting) {
      status_ = SessionStatus::Idle;
      {
        std::lock_guard<std::mutex> lk(mutex_);
        if (lastError_.empty())
          lastError_ = reason.empty() ? "Disconnected" : reason;
      }
      notify();
    }
  }
}

void SessionManager::notify() {
  if (onChange) onChange();
}

std::string SessionManager::code() {
  std::lock_guard<std::mutex> lk(mutex_);
  return code_;
}
std::string SessionManager::myName() {
  std::lock_guard<std::mutex> lk(mutex_);
  return myName_;
}
std::string SessionManager::mySid() {
  std::lock_guard<std::mutex> lk(mutex_);
  return mySid_;
}
std::vector<SessionMember> SessionManager::members() {
  std::lock_guard<std::mutex> lk(mutex_);
  return members_;
}
std::vector<PendingJoin> SessionManager::pending() {
  std::lock_guard<std::mutex> lk(mutex_);
  return pending_;
}
std::string SessionManager::lastError() {
  std::lock_guard<std::mutex> lk(mutex_);
  return lastError_;
}