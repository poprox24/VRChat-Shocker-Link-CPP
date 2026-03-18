
#pragma once
#include <windows.h>
#include <wininet.h>
#include <winsock2.h>

#include <atomic>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#include "logger.h"

#pragma comment(lib, "wininet.lib")

#define APP_VERSION "1.1.3"

#define WIDEN2(x) L##x
#define WIDEN(x) WIDEN2(x)
#define APP_VERSION_W WIDEN(APP_VERSION)

inline std::atomic<bool> updateReady{false};
inline std::string pendingExePath;

namespace Updater {
inline bool newerThan(const std::string& remote, const std::string& local) {
  auto parse = [](const std::string& v) {
    std::string s = (!v.empty() && v[0] == 'v') ? v.substr(1) : v;
    int a = 0, b = 0, c = 0;
    sscanf_s(s.c_str(), "%d.%d.%d", &a, &b, &c);
    return std::make_tuple(a, b, c);
  };
  return parse(remote) > parse(local);
}

inline std::string httpGet(const std::string& url) {
  HINTERNET hNet =
      InternetOpenA("ShockerLink/" APP_VERSION, 0, nullptr, nullptr, 0);
  if (!hNet) return "";
  HINTERNET hUrl =
      InternetOpenUrlA(hNet, url.c_str(), nullptr, 0,
                       INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD |
                           INTERNET_FLAG_NO_CACHE_WRITE,
                       0);
  if (!hUrl) {
    InternetCloseHandle(hNet);
    return "";
  }
  std::string result;
  char buf[4096];
  DWORD read;
  while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read > 0)
    result.append(buf, read);
  InternetCloseHandle(hUrl);
  InternetCloseHandle(hNet);
  return result;
}

inline bool download(const std::string& url, const std::string& dest) {
  HINTERNET hNet =
      InternetOpenA("ShockerLink/" APP_VERSION, 0, nullptr, nullptr, 0);
  if (!hNet) return false;
  HINTERNET hUrl =
      InternetOpenUrlA(hNet, url.c_str(), nullptr, 0,
                       INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD |
                           INTERNET_FLAG_NO_CACHE_WRITE,
                       0);
  if (!hUrl) {
    InternetCloseHandle(hNet);
    return false;
  }
  HANDLE hFile = CreateFileA(dest.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hNet);
    return false;
  }
  char buf[65536];
  DWORD read, written;
  bool ok = true;
  while (InternetReadFile(hUrl, buf, sizeof(buf), &read) && read > 0)
    if (!WriteFile(hFile, buf, read, &written, nullptr)) {
      ok = false;
      break;
    }
  CloseHandle(hFile);
  InternetCloseHandle(hUrl);
  InternetCloseHandle(hNet);
  return ok;
}

inline void checkAsync() {
  std::thread([]() {
    auto resp = httpGet(
        "https://api.github.com/repos/poprox24/VRChat-Shocker-Link-CPP/"
        "releases/latest");
    if (resp.empty()) {
      logMsg("[Update] Could not reach GitHub");
      return;
    }

    try {
      auto j = nlohmann::json::parse(resp);
      std::string tag = j["tag_name"].get<std::string>();

      if (!newerThan(tag, APP_VERSION)) {
        logMsg("[Update] Up to date ({})", APP_VERSION);
        return;
      }

      logMsg("[Update] New version {} found, downloading...", tag);

      std::string dlUrl;
      for (auto& asset : j["assets"]) {
        std::string name = asset["name"].get<std::string>();
        if (name.size() >= 4 && name.substr(name.size() - 4) == ".exe") {
          dlUrl = asset["browser_download_url"].get<std::string>();
          break;
        }
      }
      if (dlUrl.empty()) {
        logMsg("[Update] No .exe in release assets");
        return;
      }

      char exePath[MAX_PATH] = {};
      GetModuleFileNameA(nullptr, exePath, MAX_PATH);
      pendingExePath = std::string(exePath);
      std::string newPath = pendingExePath + ".new";

      if (!download(dlUrl, newPath)) {
        logMsg("[Update] Download failed");
        return;
      }

      logMsg("[Update] Ready, will restart shortly");
      updateReady = true;

    } catch (std::exception& e) {
      logMsg("[Update] Error: {}", e.what());
    }
  }).detach();
}

inline void applyAndRestart(HWND hwnd) {
  std::string newPath = pendingExePath + ".new";
  std::string cmd = "/c timeout /t 1 /nobreak && move /y \"" + newPath +
                    "\" \"" + pendingExePath + "\" && start \"\" \"" +
                    pendingExePath + "\"";
  ShellExecuteA(nullptr, "open", "cmd.exe", cmd.c_str(), nullptr, SW_HIDE);
  PostMessage(hwnd, WM_CLOSE, 0, 0);
}

}