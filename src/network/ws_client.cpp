#include "ws_client.h"

#include <curl/curl.h>

#include <chrono>
#include <thread>

#include "logger.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <poll.h>
#endif

WsClient::~WsClient() { close(); }

bool WsClient::connect(const std::string& url) {
  close();  // make sure any previous connection is gone

  CURL* c = curl_easy_init();
  if (!c) return false;

  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_CONNECT_ONLY, 2L);  // 2L => WebSocket mode
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
  curl_easy_setopt(c, CURLOPT_MAXCONNECTS, 0L);
  // TLS verification uses the system CA store by default. If your platform has
  // no CA bundle you can point CURLOPT_CAINFO at one here.

  // Performs the HTTP GET + Upgrade handshake and leaves the socket connected.
  CURLcode res = curl_easy_perform(c);
  if (res != CURLE_OK) {
    logMsg("[WS] connect failed: {}", curl_easy_strerror(res));
    curl_easy_cleanup(c);
    if (onState) onState(false, curl_easy_strerror(res));
    return false;
  }

  curl_ = c;
  connected_ = true;
  stop_ = false;
  rxBuffer_.clear();
  thread_ = std::thread([this] { runLoop(); });
  if (onState) onState(true, "");
  return true;
}

void WsClient::send(const std::string& text) {
  std::lock_guard<std::mutex> lk(outMutex_);
  outQueue_.push(text);
}

void WsClient::close() {
  stop_ = true;
  if (thread_.joinable()) thread_.join();

  if (curl_) {
    // Best-effort close frame so the server sees a clean disconnect.
    size_t sent = 0;
    curl_ws_send((CURL*)curl_, "", 0, &sent, 0, CURLWS_CLOSE);
    curl_easy_cleanup((CURL*)curl_);
    curl_ = nullptr;
  }

  bool was = connected_.exchange(false);
  if (was && onState) onState(false, "Closed");

  std::lock_guard<std::mutex> lk(outMutex_);
  std::queue<std::string> empty;
  std::swap(outQueue_, empty);
}

void WsClient::runLoop() {
  CURL* c = (CURL*)curl_;

  curl_socket_t sock = CURL_SOCKET_BAD;
  CURLcode gi = curl_easy_getinfo(c, CURLINFO_ACTIVESOCKET, &sock);
  bool havePoll = (gi == CURLE_OK &&
                   sock != CURL_SOCKET_BAD);  // CHANGED: also check the value

  while (!stop_.load()) {
    // Poll only as a ~100ms wait, NOT as a gate. With CONNECT_ONLY, curl can
    // hold received frames in its own buffer, so the fd never signals readable
    // even when a message is waiting — we must call curl_ws_recv regardless.
#ifdef _WIN32
    WSAPOLLFD pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (havePoll)
      WSAPoll(&pfd, 1, 100);
    else
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
#else
    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (havePoll)
      poll(&pfd, 1, 100);
    else
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
#endif

    if (!pumpRecv()) break;
    if (!pumpSend()) break;
  }

  connected_ = false;
  if (onState) onState(false, "Disconnected");
}

bool WsClient::pumpRecv() {
  CURL* c = (CURL*)curl_;
  char buf[4096];

  for (;;) {
    size_t rlen = 0;
    const struct curl_ws_frame* meta = nullptr;
    CURLcode r = curl_ws_recv(c, buf, sizeof(buf), &rlen, &meta);

    if (r == CURLE_AGAIN) return true;  // nothing more right now
    if (r != CURLE_OK) return false;    // closed / error

    // Control frames: handle without touching the message buffer.
    if (meta && (meta->flags & CURLWS_CLOSE)) return false;
    if (meta && (meta->flags & CURLWS_PING)) {
      size_t sent = 0;
      curl_ws_send(c, buf, rlen, &sent, 0, CURLWS_PONG);
      continue;
    }
    if (meta && (meta->flags & CURLWS_PONG)) continue;

    rxBuffer_.append(buf, rlen);

    // bytesleft == 0 means the current frame is fully received. Our server only
    // sends unfragmented text frames, so that's also the end of the message.
    if (meta && meta->bytesleft == 0) {
      if (!rxBuffer_.empty() && onMessage) onMessage(rxBuffer_);
      rxBuffer_.clear();
    }
  }
}

bool WsClient::pumpSend() {
  CURL* c = (CURL*)curl_;

  for (;;) {
    std::string msg;
    {
      std::lock_guard<std::mutex> lk(outMutex_);
      if (outQueue_.empty()) return true;
      msg = std::move(outQueue_.front());
      outQueue_.pop();
    }

    size_t sent = 0;
    CURLcode r = curl_ws_send(c, msg.data(), msg.size(), &sent, 0, CURLWS_TEXT);

    if (r == CURLE_AGAIN) {
      // Socket not writable yet, put it back at the front and retry next tick.
      std::lock_guard<std::mutex> lk(outMutex_);
      std::queue<std::string> tmp;
      tmp.push(std::move(msg));
      while (!outQueue_.empty()) {
        tmp.push(std::move(outQueue_.front()));
        outQueue_.pop();
      }
      std::swap(outQueue_, tmp);
      return true;
    }
    if (r != CURLE_OK) return false;
  }
}