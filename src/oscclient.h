#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
#define SOCK_INVAL INVALID_SOCKET
#define sock_close closesocket
#define SOCK_NFDS(s) 0
inline void sock_init() {
  WSADATA w;
  WSAStartup(MAKEWORD(2, 2), &w);
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
#define SOCK_INVAL (-1)
#define sock_close close
#define SOCK_NFDS(s) ((s) + 1)
inline void sock_init() {}
#endif

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "httplib.h"
#include "logger.h"
#include "mdns_advertiser.h"

class OscListener {
 public:
  using Callback = std::function<void(const std::string&, float)>;

  OscListener(int port, Callback cb);
  bool start();
  void stop();

 private:
  int port_;
  Callback callback_;
  sock_t sock_ = SOCK_INVAL;
  std::atomic<bool> running_{false};
  std::thread thread_;

  static std::string readOscString(const uint8_t* d, size_t len, size_t& off);
  void parseMessage(const uint8_t* d, size_t len);
  void parseBundle(const uint8_t* d, size_t len);
  void loop();
};

class OscQueryServer {
 public:
  std::mutex shockMutex;
  std::condition_variable shockCV;
  std::deque<int> pendingParameterIndexes;

  OscQueryServer(int oscPort, const std::string& sn, const std::string& hostIp);
  bool start();
  void stop();
  void setParameterPaths(const std::vector<std::string>& paths);

 private:
  int oscPort_, httpPort_ = -1;
  std::string serviceName_, hostIp_;
  std::vector<std::string> parameterPaths_;
  std::unordered_map<std::string, int> pathToParameterIndex_;
  httplib::Server httpServer_;
  std::thread httpThread_;
  OscListener oscListener_;
  std::unique_ptr<MdnsAdvertiser> mdns_;
  std::mutex lastValuesMutex_;
  std::unordered_map<std::string, float> lastValues_;

  void handleHttpRequest(const httplib::Request& req, httplib::Response& res);
  void onOscMessage(const std::string& path, float value);
};