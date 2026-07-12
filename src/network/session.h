#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "ws_client.h"

struct SessionMember {
  std::string name;
  std::string sid;
};

struct PendingJoin {
  std::string name;
  std::string sid;
};

enum class SessionStatus {
  Idle,        // not connected
  Connecting,  // handshake in progress
  Pending,     // connected, waiting for a member to approve us
  Active,      // approved and in the room
};

class SessionManager {
 public:
  // Fired when a remote member's shock should run on OUR hardware.
  using RemoteShockCallback =
      std::function<void(int strength, int durationMs, bool vibrate)>;

  SessionManager(std::string serverBaseUrl, RemoteShockCallback onRemoteShock);
  ~SessionManager();

  // Actions (call from the UI thread)
  void createSession(const std::string& displayName);  // become host
  void joinSession(const std::string& code, const std::string& displayName);
  void approve(const std::string& sid);
  void deny(const std::string& sid);
  void leave();

  // Relay a fully-resolved shock to everyone else in the room.
  void broadcastShock(int strength, int durationMs, bool vibrate);

  void setServerUrl(const std::string& url);

  // State (thread-safe getters, return copies)
  SessionStatus status() const { return status_.load(); }
  bool isActive() const { return status_.load() == SessionStatus::Active; }
  bool isHost() const { return isHost_.load(); }
  std::string code();
  std::string myName();
  std::string mySid();
  std::vector<SessionMember> members();
  std::vector<PendingJoin> pending();
  std::string lastError();

  // UI sets this so it can repaint when session state changes.
  std::function<void()> onChange;

 private:
  void connect(const std::string& code, const std::string& name);
  void handleMessage(const std::string& raw);
  void handleState(bool connected, const std::string& reason);
  void notify();
  static std::string makeCode();
  static std::string urlEncode(const std::string& s);

  std::string serverBase_;
  RemoteShockCallback onRemoteShock_;

  WsClient ws_;
  std::mutex mutex_;
  std::string code_;
  std::string myName_;
  std::string mySid_;
  std::vector<SessionMember> members_;
  std::vector<PendingJoin> pending_;
  std::string lastError_;

  std::atomic<SessionStatus> status_{SessionStatus::Idle};
  std::atomic<bool> isHost_{false};
};