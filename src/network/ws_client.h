#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

class WsClient {
 public:
  using MessageCallback = std::function<void(const std::string&)>;
  // connected == true on successful handshake, false when the socket drops.
  using StateCallback =
      std::function<void(bool connected, const std::string& reason)>;

  WsClient() = default;
  ~WsClient();

  // url like "wss://host/path?query". Blocks until the TLS + WS handshake
  // finishes (or fails). Returns true on success and starts the recv thread.
  bool connect(const std::string& url);

  // Thread-safe; queues a text message to be sent from the worker thread.
  void send(const std::string& text);

  // Stops the thread and closes the connection. Safe to call multiple times.
  void close();

  bool isConnected() const { return connected_.load(); }

  MessageCallback onMessage;
  StateCallback onState;

 private:
  void runLoop();
  bool pumpRecv();  // returns false on fatal error / server close
  bool pumpSend();  // drains the outbound queue; false on fatal error

  void* curl_ = nullptr;  // CURL*
  std::thread thread_;
  std::atomic<bool> connected_{false};
  std::atomic<bool> stop_{false};

  std::mutex outMutex_;
  std::queue<std::string> outQueue_;

  std::string rxBuffer_;  // accumulates partial/multi-chunk frames
};