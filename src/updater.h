#pragma once

#ifdef _WIN32
// Windows

// clang-format off
#include <windows.h>
#include <shellapi.h>
#include <wininet.h>
#include <winsock2.h>
// clang-format on

#include <atomic>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#include "logger.h"
#include "version.h"

#pragma comment(lib, "wininet.lib")

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
#if defined(_WIN32)
    sscanf_s(s.c_str(), "%d.%d.%d", &a, &b, &c);
#else
    sscanf(s.c_str(), "%d.%d.%d", &a, &b, &c);
#endif
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
      if (!j.contains("tag_name") || j["tag_name"].is_null()) {
        logMsg("[Update] GitHub response has no tag_name: {}",
               resp.substr(0, 200));
        return;
      }
      std::string tag = j["tag_name"].get<std::string>();

      if (!newerThan(tag, APP_VERSION)) {
        logMsg("[Update] Up to date ({})", APP_VERSION);
        std::string notes = APP_RELEASE_NOTES;
        std::istringstream ss(notes);
        std::string line;
        while (std::getline(ss, line)) logMsg("[Update] {}", line);
        return;
      }

      std::string relName = j.value("name", "");

      logMsg("[Update] New version {} found, patch name: {}, downloading...",
             tag, relName);

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
  std::string oldPath = pendingExePath;
  std::string newPath = pendingExePath + ".new";
  std::string bakPath = pendingExePath + ".old";

  std::string cmd = fmt::format(
      "/c timeout /t 2 /nobreak >nul && "
      "move /y \"{}\" \"{}\" && "
      "move /y \"{}\" \"{}\" && "
      "start \"\" \"{}\"",
      oldPath, bakPath, newPath, oldPath, oldPath);

  logMsg("[Updater] Replacing binary:\n  {} → {}\n  {} → {}", newPath, oldPath,
         oldPath, bakPath);

  ShellExecuteA(nullptr, "open", "cmd.exe", cmd.c_str(), nullptr, SW_HIDE);

  if (hwnd)
    PostMessage(hwnd, WM_CLOSE, 0, 0);
  else
    PostQuitMessage(0);
}

}  // namespace Updater
#else

#include <curl/curl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#include "logger.h"
#include "version.h"

inline std::atomic<bool> updateReady{false};
inline std::string pendingExePath;

namespace Updater {

inline bool newerThan(const std::string& remote, const std::string& local) {
  auto parse = [](const std::string& v) {
    std::string s = (!v.empty() && v[0] == 'v') ? v.substr(1) : v;
    int a = 0, b = 0, c = 0;
    sscanf(s.c_str(), "%d.%d.%d", &a, &b, &c);
    return std::make_tuple(a, b, c);
  };
  return parse(remote) > parse(local);
}

static size_t curlWriteUpd(void* ptr, size_t sz, size_t n, std::string* out) {
  out->append(static_cast<char*>(ptr), sz * n);
  return sz * n;
}

static size_t curlWriteFile(void* ptr, size_t sz, size_t n, FILE* f) {
  return fwrite(ptr, sz, n, f);
}

inline void checkAsync() {
  std::thread([] {
    CURL* c = curl_easy_init();
    if (!c) return;
    std::string resp;
    std::string ua = std::string("ShockerLink/") + APP_VERSION;
    curl_easy_setopt(c, CURLOPT_URL,
                     "https://api.github.com/repos/poprox24/"
                     "VRChat-Shocker-Link-CPP/releases/latest");
    curl_easy_setopt(c, CURLOPT_USERAGENT, ua.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWriteUpd);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    if (curl_easy_perform(c) != CURLE_OK) {
      logMsg("[Update] Could not reach GitHub");
      curl_easy_cleanup(c);
      return;
    }
    curl_easy_cleanup(c);

    try {
      auto j = nlohmann::json::parse(resp);
      if (!j.contains("tag_name") || j["tag_name"].is_null()) {
        logMsg("[Update] GitHub response has no tag_name: {}",
               resp.substr(0, 200));
        return;
      }
      std::string tag = j["tag_name"].get<std::string>();

      if (!newerThan(tag, APP_VERSION)) {
        logMsg("[Update] Up to date ({})", APP_VERSION);
        std::string notes = APP_RELEASE_NOTES;
        std::istringstream ss(notes);
        std::string line;
        while (std::getline(ss, line)) logMsg("[Update] {}", line);
        return;
      }
      logMsg("[Update] New version {} found, downloading...", tag);

      std::string dlUrl;
      for (auto& asset : j["assets"]) {
        std::string name = asset["name"].get<std::string>();
        if (name.size() >= 4 && name.substr(name.size() - 4) == ".exe")
          continue;  // skip .exe
        if (name.find("Shocker") != std::string::npos) {
          dlUrl = asset["browser_download_url"].get<std::string>();
          break;
        }
      }
      // fallback: grab first non-.exe asset
      if (dlUrl.empty()) {
        for (auto& asset : j["assets"]) {
          std::string name = asset["name"].get<std::string>();
          if (name.substr(name.size() - 4) != ".exe") {
            dlUrl = asset["browser_download_url"].get<std::string>();
            break;
          }
        }
      }
      if (dlUrl.empty()) {
        logMsg("[Update] No Linux binary in release assets");
        return;
      }

      // Get current exe path via /proc/self/exe
      char exePath[4096] = {};
      ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
      if (len < 0) {
        logMsg("[Update] readlink /proc/self/exe failed");
        return;
      }
      pendingExePath = std::string(exePath, len);
      std::string newPath = pendingExePath + ".new";

      FILE* f = fopen(newPath.c_str(), "wb");
      if (!f) {
        logMsg("[Update] Cannot open {} for writing", newPath);
        return;
      }
      CURL* dc = curl_easy_init();
      curl_easy_setopt(dc, CURLOPT_URL, dlUrl.c_str());
      curl_easy_setopt(dc, CURLOPT_WRITEFUNCTION, curlWriteFile);
      curl_easy_setopt(dc, CURLOPT_WRITEDATA, f);
      curl_easy_setopt(dc, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(dc, CURLOPT_TIMEOUT, 120L);
      CURLcode res = curl_easy_perform(dc);
      fclose(f);
      curl_easy_cleanup(dc);

      if (res != CURLE_OK) {
        logMsg("[Update] Download failed: {}", curl_easy_strerror(res));
        return;
      }
      logMsg("[Update] Ready, will restart shortly");
      updateReady = true;
    } catch (std::exception& e) {
      logMsg("[Update] Error: {}", e.what());
    }
  }).detach();
}

inline void applyAndRestart() {
  std::string oldPath = pendingExePath;
  std::string newPath = pendingExePath + ".new";
  std::string bakPath = pendingExePath + ".old";

  chmod(newPath.c_str(), 0755);

  logMsg("[Updater] Replacing binary:\n  {} → {}\n  {} → {}", newPath, oldPath,
         oldPath, bakPath);

  // Fork so the current process can exit cleanly
  if (fork() == 0) {
    sleep(2);
    if (rename(oldPath.c_str(), bakPath.c_str()) != 0) {
      logMsg("[Updater] rename old→bak failed");
      _exit(1);
    }
    if (rename(newPath.c_str(), oldPath.c_str()) != 0) {
      logMsg("[Updater] rename new→old failed");
      _exit(1);
    }
    execl(oldPath.c_str(), oldPath.c_str(), nullptr);
    logMsg("[Updater] execl failed");
    _exit(1);
  }

  // Parent exits immediately
  _exit(0);
}

}  // namespace Updater
#endif