#pragma once

#define GLFW_INCLUDE_NONE
#include <GL/gl.h>
#include <GLFW/glfw3.h>

#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using namespace std::chrono;

#ifndef _WIN32
#include <png.h>
#endif

#include <yaml-cpp/yaml.h>

#include <nlohmann/json.hpp>

#include "curve.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include "implot.h"
#include "logger.h"
#include "settings.h"
#include "shockerhub.h"
#include "stats.h"
#include "updater.h"

static constexpr const char* kWindowTitle = "Shocker Link";

#ifndef _WIN32
static bool loadPngRgbaFromFile(const std::filesystem::path& pngPath,
                                int& width, int& height,
                                std::vector<uint8_t>& pixels) {
  std::ifstream file(pngPath, std::ios::binary);
  if (!file) return false;
  std::vector<uint8_t> pngData((std::istreambuf_iterator<char>(file)), {});
  if (pngData.empty()) return false;

  png_image image;
  memset(&image, 0, sizeof(image));
  image.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_memory(&image, pngData.data(), pngData.size()))
    return false;

  image.format = PNG_FORMAT_RGBA;
  size_t size = PNG_IMAGE_SIZE(image);
  pixels.assign(size, 0);
  if (!png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr))
    return false;

  width = static_cast<int>(image.width);
  height = static_cast<int>(image.height);
  return true;
}

static bool setWindowIcon(GLFWwindow* window) {
  std::filesystem::path sourcePath =
      std::filesystem::path(__FILE__).parent_path() / "icon.png";
  std::filesystem::path buildPath =
      std::filesystem::current_path() / "src" / "icon.png";
  std::filesystem::path rootBuildPath =
      std::filesystem::current_path().parent_path() / "src" / "icon.png";

  std::vector<std::filesystem::path> candidates = {sourcePath, buildPath,
                                                   rootBuildPath};
  std::filesystem::path pngPath;
  for (auto& path : candidates) {
    if (std::filesystem::exists(path)) {
      pngPath = path;
      break;
    }
  }
  if (pngPath.empty()) return false;

  std::vector<uint8_t> pixels;
  int width = 0, height = 0;
  if (!loadPngRgbaFromFile(pngPath, width, height, pixels)) return false;

  GLFWimage icon;
  icon.width = width;
  icon.height = height;
  icon.pixels = pixels.data();
  glfwSetWindowIcon(window, 1, &icon);
  return true;
}
#endif

static GLFWwindow* g_window = nullptr;
static ShockerHub* g_hub = nullptr;
static Settings* g_settingsForHotkey = nullptr;

// Hotkey Manager
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

static WNDPROC g_origWndProc = nullptr;

static LRESULT CALLBACK hotkeyWndProc(HWND hwnd, UINT msg, WPARAM wp,
                                      LPARAM lp) {
  if (msg == WM_HOTKEY && g_hub) {
    g_hub->shocksDisabled = true;
    logMsg("[Hotkey] Shocks disabled.");
    if (g_wakeUiFunc) g_wakeUiFunc();
  }
  return CallWindowProc(g_origWndProc, hwnd, msg, wp, lp);
}

static void registerGlobalHotkey(int glfwKey, int mods) {
  if (!g_window || !glfwKey) return;
  HWND hwnd = glfwGetWin32Window(g_window);
  UINT winMods = MOD_NOREPEAT;
  if (mods & 1) winMods |= MOD_ALT;
  if (mods & 2) winMods |= MOD_CONTROL;
  if (mods & 4) winMods |= MOD_SHIFT;
  UnregisterHotKey(hwnd, 1);
  if (glfwKey) RegisterHotKey(hwnd, 1, winMods, glfwKey);
  if (!g_origWndProc)
    g_origWndProc =
        (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)hotkeyWndProc);
}

#else
#include <X11/Xlib.h>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>

static Display* g_x11HotkeyDisplay = nullptr;
static Window g_x11Root = 0;
static KeyCode g_x11GrabbedKey = 0;
static unsigned int g_x11GrabbedMods = 0;
static std::thread g_hotkeyThread;
static std::atomic<bool> g_hotkeyThreadRunning{false};

static int glfwKeyToKeysym(int glfwKey) {
  if (glfwKey >= GLFW_KEY_F1 && glfwKey <= GLFW_KEY_F25)
    return XK_F1 + (glfwKey - GLFW_KEY_F1);
  if (glfwKey >= GLFW_KEY_A && glfwKey <= GLFW_KEY_Z)
    return XK_a + (glfwKey - GLFW_KEY_A);
  if (glfwKey >= GLFW_KEY_0 && glfwKey <= GLFW_KEY_9)
    return XK_0 + (glfwKey - GLFW_KEY_0);
  return 0;
}

static void unregisterGlobalHotkeyLinux() {
  if (!g_x11HotkeyDisplay || !g_x11GrabbedKey) return;
  unsigned int ignoreMasks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
  for (auto ig : ignoreMasks)
    XUngrabKey(g_x11HotkeyDisplay, g_x11GrabbedKey, g_x11GrabbedMods | ig,
               g_x11Root);
  XFlush(g_x11HotkeyDisplay);
  g_x11GrabbedKey = 0;
}

static void registerGlobalHotkey(int glfwKey, int mods) {
  if (!g_x11HotkeyDisplay) {
    g_x11HotkeyDisplay = XOpenDisplay(nullptr);
    if (!g_x11HotkeyDisplay) {
      logMsg("[Hotkey] XOpenDisplay failed");
      return;
    }
    g_x11Root = DefaultRootWindow(g_x11HotkeyDisplay);
  }

  if (!glfwKey) {
    unregisterGlobalHotkeyLinux();
    return;
  }

  int keysym = glfwKeyToKeysym(glfwKey);
  if (!keysym) return;
  KeyCode kc = XKeysymToKeycode(g_x11HotkeyDisplay, keysym);
  if (!kc) return;

  unregisterGlobalHotkeyLinux();

  unsigned int x11Mods = 0;
  if (mods & 1) x11Mods |= Mod1Mask;
  if (mods & 2) x11Mods |= ControlMask;
  if (mods & 4) x11Mods |= ShiftMask;

  unsigned int ignoreMasks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
  XSetErrorHandler([](Display*, XErrorEvent*) -> int { return 0; });
  for (auto ig : ignoreMasks)
    XGrabKey(g_x11HotkeyDisplay, kc, x11Mods | ig, g_x11Root, True,
             GrabModeAsync, GrabModeAsync);
  XFlush(g_x11HotkeyDisplay);

  g_x11GrabbedKey = kc;
  g_x11GrabbedMods = x11Mods;

  if (g_hotkeyThreadRunning) return;
  g_hotkeyThreadRunning = true;
  g_hotkeyThread = std::thread([] {
    while (g_hotkeyThreadRunning) {
      if (!g_x11HotkeyDisplay) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
      int fd = ConnectionNumber(g_x11HotkeyDisplay);
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(fd, &fds);
      struct timeval tv{0, 100000};
      if (select(fd + 1, &fds, nullptr, nullptr, &tv) <= 0) continue;
      while (XPending(g_x11HotkeyDisplay)) {
        XEvent ev;
        XNextEvent(g_x11HotkeyDisplay, &ev);
        if (ev.type == KeyPress && g_hub) {
          g_hub->shocksDisabled = true;
          logMsg("[Hotkey] Shocks disabled.");
          if (g_wakeUiFunc) g_wakeUiFunc();
        }
      }
    }
  });
}
#endif

extern std::atomic<bool> shouldRestart;

static constexpr size_t kMaxUndoRedoStates = 128;

struct AppState {
  Settings settings;
  std::array<CurvePoint, 3> curvePoints;
  float minDur = 0.f, maxDur = 0.f, xViewMin = 0.f, xViewMax = 0.f;
  bool cooldownEnabled = false;

  std::string stgShockParam, stgSecondParam, stgShockerIDs, stgSerialPort,
      stgVrchatHost, stgPishockUser, stgPishockKey, stgOpenshockToken,
      stgOpenshockServer;
  bool stgUsePishock = false, stgRandomOrSeq = false, stgNotifEnabled = false,
       stgNotifUseOvr = false, stgUseSerial = true, chatboxShockEnabled = true,
       chatboxCooldownEnabled = true;
  int stgBaseCooldown = 2, stgMaxCooldown = 6, stgCooldownWindow = 30,
      stgPresetCount = 3;
  float stgCooldownFactor = 0.4f, stgTouchThreshold = 8.f;
  std::vector<Parameter> stgParameters;

  bool operator==(const AppState& o) const {
    return settings == o.settings && curvePoints == o.curvePoints &&
           minDur == o.minDur && maxDur == o.maxDur && xViewMin == o.xViewMin &&
           xViewMax == o.xViewMax && cooldownEnabled == o.cooldownEnabled &&
           stgShockParam == o.stgShockParam &&
           stgSecondParam == o.stgSecondParam &&
           stgShockerIDs == o.stgShockerIDs &&
           stgSerialPort == o.stgSerialPort &&
           stgVrchatHost == o.stgVrchatHost &&
           chatboxShockEnabled == o.chatboxShockEnabled &&
           chatboxCooldownEnabled == o.chatboxCooldownEnabled &&
           stgUsePishock == o.stgUsePishock &&
           stgRandomOrSeq == o.stgRandomOrSeq &&
           stgBaseCooldown == o.stgBaseCooldown &&
           stgMaxCooldown == o.stgMaxCooldown &&
           stgCooldownFactor == o.stgCooldownFactor &&
           stgCooldownWindow == o.stgCooldownWindow &&
           stgNotifEnabled == o.stgNotifEnabled &&
           stgNotifUseOvr == o.stgNotifUseOvr &&
           stgUseSerial == o.stgUseSerial &&
           stgPishockUser == o.stgPishockUser &&
           stgPishockKey == o.stgPishockKey &&
           stgOpenshockToken == o.stgOpenshockToken &&
           stgOpenshockServer == o.stgOpenshockServer &&
           stgPresetCount == o.stgPresetCount &&
           stgTouchThreshold == o.stgTouchThreshold &&
           stgParameters == o.stgParameters;
  }
  bool operator!=(const AppState& o) const { return !(*this == o); }
};

struct UiContext {
  Settings& settings;
  ShockerHub& hub;
  float& minDur;
  float& maxDur;
  float& xViewMin;
  float& xViewMax;
  bool& cooldownEnabled;
  char (&stgShockParam)[64];
  char (&stgSecondParam)[64];
  char (&stgShockerIDs)[256];
  char (&stgSerialPort)[64];
  char (&stgVrchatHost)[64];
  bool& stgUsePishock;
  bool& stgRandomOrSeq;
  int& stgBaseCooldown;
  int& stgMaxCooldown;
  float& stgCooldownFactor;
  int& stgCooldownWindow;
  bool& stgNotifEnabled;
  bool& stgNotifUseOvr;
  bool& stgUseSerial;
  bool& chatboxShockEnabled;
  bool& chatboxCooldownEnabled;
  char (&stgPishockUser)[128];
  char (&stgPishockKey)[128];
  char (&stgOpenshockToken)[256];
  char (&stgOpenshockServer)[128];
  int& stgPresetCount;
  float& stgTouchThreshold;
  std::vector<Parameter>& stgParameters;
};

static AppState snapshotAppState(const UiContext& ui) {
  AppState st;
  st.settings = ui.settings;
  st.curvePoints = ui.hub.curvePoints;
  st.minDur = ui.minDur;
  st.maxDur = ui.maxDur;
  st.xViewMin = ui.xViewMin;
  st.xViewMax = ui.xViewMax;
  st.cooldownEnabled = ui.cooldownEnabled;
  st.stgShockParam = ui.stgShockParam;
  st.stgSecondParam = ui.stgSecondParam;
  st.stgShockerIDs = ui.stgShockerIDs;
  st.stgSerialPort = ui.stgSerialPort;
  st.stgVrchatHost = ui.stgVrchatHost;
  st.chatboxShockEnabled = ui.chatboxShockEnabled;
  st.chatboxCooldownEnabled = ui.chatboxCooldownEnabled;
  st.stgUsePishock = ui.stgUsePishock;
  st.stgRandomOrSeq = ui.stgRandomOrSeq;
  st.stgBaseCooldown = ui.stgBaseCooldown;
  st.stgMaxCooldown = ui.stgMaxCooldown;
  st.stgCooldownFactor = ui.stgCooldownFactor;
  st.stgCooldownWindow = ui.stgCooldownWindow;
  st.stgNotifEnabled = ui.stgNotifEnabled;
  st.stgNotifUseOvr = ui.stgNotifUseOvr;
  st.stgUseSerial = ui.stgUseSerial;
  st.stgPishockUser = ui.stgPishockUser;
  st.stgPishockKey = ui.stgPishockKey;
  st.stgOpenshockToken = ui.stgOpenshockToken;
  st.stgOpenshockServer = ui.stgOpenshockServer;
  st.stgPresetCount = ui.stgPresetCount;
  st.stgTouchThreshold = ui.stgTouchThreshold;
  st.stgParameters = ui.stgParameters;
  return st;
}

static void restoreAppState(const AppState& st, UiContext& ui) {
  ui.settings = st.settings;
  ui.hub.curvePoints = st.curvePoints;
  ui.minDur = st.minDur;
  ui.maxDur = st.maxDur;
  ui.xViewMin = st.xViewMin;
  ui.xViewMax = st.xViewMax;
  ui.cooldownEnabled = st.cooldownEnabled;
  ui.settings.minShockDuration = ui.minDur;
  ui.settings.maxShockDuration = ui.maxDur;
  ui.settings.xViewMin = ui.xViewMin;
  ui.settings.xViewMax = ui.xViewMax;
  ui.settings.cooldownEnabled = ui.cooldownEnabled;

  auto cp = [](auto& dst, const std::string& src) {
    snprintf(dst, sizeof(dst), "%s", src.c_str());
  };
  cp(ui.stgShockParam, st.stgShockParam);
  cp(ui.stgSecondParam, st.stgSecondParam);
  cp(ui.stgShockerIDs, st.stgShockerIDs);
  cp(ui.stgSerialPort, st.stgSerialPort);
  cp(ui.stgVrchatHost, st.stgVrchatHost);
  cp(ui.stgPishockUser, st.stgPishockUser);
  cp(ui.stgPishockKey, st.stgPishockKey);
  cp(ui.stgOpenshockToken, st.stgOpenshockToken);
  cp(ui.stgOpenshockServer, st.stgOpenshockServer);

  ui.stgUsePishock = st.stgUsePishock;
  ui.stgRandomOrSeq = st.stgRandomOrSeq;
  ui.stgBaseCooldown = st.stgBaseCooldown;
  ui.stgMaxCooldown = st.stgMaxCooldown;
  ui.stgCooldownFactor = st.stgCooldownFactor;
  ui.stgCooldownWindow = st.stgCooldownWindow;
  ui.stgNotifEnabled = st.stgNotifEnabled;
  ui.stgNotifUseOvr = st.stgNotifUseOvr;
  ui.stgUseSerial = st.stgUseSerial;
  ui.stgPresetCount = st.stgPresetCount;
  ui.stgTouchThreshold = st.stgTouchThreshold;
  ui.stgParameters = st.stgParameters;
  ui.chatboxShockEnabled = st.chatboxShockEnabled;
  ui.chatboxCooldownEnabled = st.chatboxCooldownEnabled;
}

static void pushUndoSnapshot(std::deque<AppState>& undoStack,
                             std::deque<AppState>& redoStack,
                             const AppState& state, bool isPerformingUndoRedo) {
  if (isPerformingUndoRedo) return;
  if (!undoStack.empty() && undoStack.back() == state) return;
  undoStack.push_back(state);
  if (undoStack.size() > kMaxUndoRedoStates) undoStack.pop_front();
  redoStack.clear();
}

static void performUndoRedo(bool is_undo, std::deque<AppState>& undoStack,
                            std::deque<AppState>& redoStack, UiContext& ui,
                            bool& isPerformingUndoRedo) {
  if (is_undo && undoStack.empty()) return;
  if (!is_undo && redoStack.empty()) return;
  isPerformingUndoRedo = true;
  AppState current = snapshotAppState(ui);
  if (is_undo) {
    redoStack.push_back(current);
    if (redoStack.size() > kMaxUndoRedoStates) redoStack.pop_front();
    AppState previous = undoStack.back();
    undoStack.pop_back();
    restoreAppState(previous, ui);
  } else {
    undoStack.push_back(current);
    if (undoStack.size() > kMaxUndoRedoStates) undoStack.pop_front();
    AppState next = redoStack.back();
    redoStack.pop_back();
    restoreAppState(next, ui);
  }
  isPerformingUndoRedo = false;
}

// Save button icon
static bool drawSaveIconButton(const char* id) {
  ImVec2 size(16, 16);
  ImGui::InvisibleButton(id, size);
  bool clicked = ImGui::IsItemClicked();
  bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetItemRectMin();
  ImU32 col = hovered
                  ? ImGui::ColorConvertFloat4ToU32(ImVec4(1.f, 1.f, 1.f, 1.f))
                  : ImGui::ColorConvertFloat4ToU32(
                        ImGui::GetStyle().Colors[ImGuiCol_Text]);
  // Floppy disk body
  dl->AddRectFilled(p, {p.x + 14, p.y + 14}, col, 1.f);
  // Inner label area (cutout)
  dl->AddRectFilled({p.x + 2, p.y + 6}, {p.x + 12, p.y + 13},
                    ImGui::ColorConvertFloat4ToU32(
                        ImGui::GetStyle().Colors[ImGuiCol_WindowBg]));
  // Shutter slot
  dl->AddRectFilled({p.x + 4, p.y + 1}, {p.x + 10, p.y + 5},
                    ImGui::ColorConvertFloat4ToU32(
                        ImGui::GetStyle().Colors[ImGuiCol_WindowBg]));
  return clicked;
}

inline bool drawRangeSliderFloat(const char* id, float* vMin, float* vMax,
                                 float min, float max, float width = -1.f) {
  ImGuiIO& io = ImGui::GetIO();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImGuiStyle& style = ImGui::GetStyle();
  if (width < 0) width = ImGui::GetContentRegionAvail().x;
  float height = ImGui::GetFrameHeight();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::Dummy({width, height});
  bool changed = false;
  float trackY = pos.y + height * 0.5f;
  float trackX0 = pos.x + height * 0.5f;
  float trackX1 = pos.x + width - height * 0.5f;
  float trackW = trackX1 - trackX0;
  float hRadius = height * 0.5f;
  auto valToX = [&](float v) {
    return trackX0 + (v - min) / (max - min) * trackW;
  };
  auto xToVal = [&](float x) {
    return std::clamp(min + (x - trackX0) / trackW * (max - min), min, max);
  };
  ImVec2 hMinPos = {valToX(*vMin), trackY};
  ImVec2 hMaxPos = {valToX(*vMax), trackY};
  static int dragging = 0;
  ImVec2 mouse = io.MousePos;
  auto inCircle = [&](ImVec2 c) {
    float dx = mouse.x - c.x, dy = mouse.y - c.y;
    return dx * dx + dy * dy <= hRadius * hRadius;
  };
  if (ImGui::IsMouseClicked(0) && dragging == 0) {
    bool onMin = inCircle(hMinPos), onMax = inCircle(hMaxPos);
    if (onMin && onMax)
      dragging = (mouse.x < (hMinPos.x + hMaxPos.x) * 0.5f) ? 1 : 2;
    else if (onMin)
      dragging = 1;
    else if (onMax)
      dragging = 2;
  }
  if (!ImGui::IsMouseDown(0)) dragging = 0;
  if (dragging == 1) {
    *vMin = std::min(xToVal(mouse.x), *vMax - 1.f);
    changed = true;
  } else if (dragging == 2) {
    *vMax = std::max(xToVal(mouse.x), *vMin + 1.f);
    changed = true;
  }
  hMinPos = {valToX(*vMin), trackY};
  hMaxPos = {valToX(*vMax), trackY};
  bool hovMin = inCircle(hMinPos), hovMax = inCircle(hMaxPos);
  ImU32 trackCol =
      ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_FrameBg]);
  ImU32 fillCol =
      ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_SliderGrab]);
  ImU32 grabCol =
      ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_SliderGrab]);
  ImU32 grabActCol =
      ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_SliderGrabActive]);
  dl->AddRectFilled({trackX0, trackY - 3}, {trackX1, trackY + 3}, trackCol, 3);
  dl->AddRectFilled({hMinPos.x, trackY - 3}, {hMaxPos.x, trackY + 3}, fillCol,
                    3);
  dl->AddCircleFilled(hMinPos, hRadius,
                      (dragging == 1 || hovMin) ? grabActCol : grabCol, 12);
  dl->AddCircleFilled(hMaxPos, hRadius,
                      (dragging == 2 || hovMax) ? grabActCol : grabCol, 12);
  return changed;
}

inline void applyUiTheme(Settings& settings) {
  ImGuiStyle& style = ImGui::GetStyle();
  style.Colors[ImGuiCol_WindowBg] = settings.backgroundColor;
  style.Colors[ImGuiCol_ChildBg] = settings.backgroundColor;
  style.Colors[ImGuiCol_Text] = settings.labelColor;
  ImVec4 a = settings.accentColor;
  ImVec4 aH = {a.x * 1.2f, a.y * 1.2f, a.z * 1.2f, a.w};
  ImVec4 aA = {a.x * 0.8f, a.y * 0.8f, a.z * 0.8f, a.w};
  ImVec4 aD = {a.x * 0.5f, a.y * 0.5f, a.z * 0.5f, a.w};
  style.Colors[ImGuiCol_Button] = aA;
  style.Colors[ImGuiCol_ButtonHovered] = a;
  style.Colors[ImGuiCol_ButtonActive] = aH;
  style.Colors[ImGuiCol_FrameBg] = aD;
  style.Colors[ImGuiCol_FrameBgHovered] = {aD.x * 1.3f, aD.y * 1.3f,
                                           aD.z * 1.3f, aD.w};
  style.Colors[ImGuiCol_FrameBgActive] = aA;
  style.Colors[ImGuiCol_SliderGrab] = a;
  style.Colors[ImGuiCol_SliderGrabActive] = aH;
  style.Colors[ImGuiCol_CheckMark] = aH;
  style.Colors[ImGuiCol_Header] = aA;
  style.Colors[ImGuiCol_HeaderHovered] = a;
  style.Colors[ImGuiCol_HeaderActive] = aH;
  style.Colors[ImGuiCol_SeparatorHovered] = a;
  style.Colors[ImGuiCol_SeparatorActive] = aH;
  style.Colors[ImGuiCol_TitleBgActive] = aD;
  ImPlot::GetStyle().Colors[ImPlotCol_FrameBg] = settings.outsideCurveBg;
  ImPlot::GetStyle().Colors[ImPlotCol_PlotBg] = settings.outsideCurveBg;
  ImPlot::GetStyle().Colors[ImPlotCol_AxisText] = settings.labelColor;
  ImPlot::GetStyle().Colors[ImPlotCol_LegendText] = settings.labelColor;
  ImVec4& bgColor = settings.outsideCurveBg;
  ImPlot::GetStyle().Colors[ImPlotCol_LegendBg] = {bgColor.x, bgColor.y,
                                                   bgColor.z, 0.84f};
}

static void registerPanicHotkey(const Settings& settings) {
  registerGlobalHotkey(settings.hotkeyVk, settings.hotkeyMods);
}

inline std::string formatKeyNameFromVk(int glfwKey, int mods) {
  std::string s;
  if (mods & 2) s += "Ctrl+";
  if (mods & 1) s += "Alt+";
  if (mods & 4) s += "Shift+";
  if (glfwKey >= GLFW_KEY_F1 && glfwKey <= GLFW_KEY_F25) {
    s += "F" + std::to_string(glfwKey - GLFW_KEY_F1 + 1);
  } else if (glfwKey != 0) {
    const char* name = glfwGetKeyName(glfwKey, 0);
    if (name) {
      std::string n = name;
      if (!n.empty()) n[0] = (char)toupper((unsigned char)n[0]);
      s += n;
    } else {
      s += "Key(" + std::to_string(glfwKey) + ")";
    }
  } else {
    s += "None";
  }
  return s;
}

// Button to import old config from
inline bool importLegacyPythonConfig(Settings& settings, ShockerHub& hub,
                                     float& minDur, float& maxDur,
                                     float& xViewMin, float& xViewMax,
                                     const std::string& settingsPath,
                                     const char* folderPathIn) {
  if (!folderPathIn || folderPathIn[0] == '\0') return false;
  std::string folder = folderPathIn;
  if (!folder.empty() && folder.back() != '/') folder += '/';

  // --- Import curve_settings.json ---
  try {
    std::ifstream f(folder + "curve_settings.json");
    if (f.is_open()) {
      nlohmann::json j = nlohmann::json::parse(f);
      const nlohmann::json names =
          j.contains("preset_names") ? j["preset_names"] : nlohmann::json{};
      if (j.contains("curve_points") && j["curve_points"].size() == 3)
        for (int i = 0; i < 3; i++)
          hub.curvePoints[i] = {j["curve_points"][i][0].get<double>(),
                                j["curve_points"][i][1].get<double>()};
      if (j.contains("min_duration"))
        minDur = settings.minShockDuration = j["min_duration"].get<float>();
      if (j.contains("max_duration"))
        maxDur = settings.maxShockDuration = j["max_duration"].get<float>();
      if (j.contains("ui_min_x"))
        xViewMin = settings.xViewMin = j["ui_min_x"].get<float>();
      if (j.contains("ui_max_x"))
        xViewMax = settings.xViewMax = j["ui_max_x"].get<float>();
      auto& rawPresets = j["presets"];
      for (int i = 0;
           i < (int)settings.presets.size() && i < (int)rawPresets.size();
           i++) {
        auto& rp = rawPresets[i];
        if (rp.is_null()) {
          settings.presets[i] = std::nullopt;
          continue;
        }
        Preset p;
        p.name = (names.is_array() && i < (int)names.size())
                     ? names[i].get<std::string>()
                     : ("Preset " + std::to_string(i + 1));
        p.minShockDuration = rp.value("min_duration", 1.0f);
        p.maxShockDuration = rp.value("max_duration", 2.0f);
        p.xViewMin = rp.value("ui_min_x", 0.f);
        p.xViewMax = rp.value("ui_max_x", 100.f);
        if (rp.contains("curve_points") && rp["curve_points"].size() == 3)
          for (int k = 0; k < 3; k++)
            p.curvePoints[k] = {rp["curve_points"][k][0].get<double>(),
                                rp["curve_points"][k][1].get<double>()};
        SavedPreset sp2;
        sp2.name = p.name;
        sp2.curves.push_back(p);
        sp2.activeCurveIndex = 0;
        settings.presets[i] = sp2;
      }
      settings.defaultPreset = j.value("default_preset", -1);

      // Apply default preset if set
      if (settings.defaultPreset >= 0 &&
          settings.defaultPreset < (int)settings.presets.size() &&
          settings.presets[settings.defaultPreset].has_value()) {
        auto& dp = settings.presets[settings.defaultPreset];
        int ai = dp->activeCurveIndex;
        if (!dp->curves.empty()) {
          if (ai >= (int)dp->curves.size()) ai = 0;
          auto& ac = dp->curves[ai];
          minDur = settings.minShockDuration = ac.minShockDuration;
          maxDur = settings.maxShockDuration = ac.maxShockDuration;
          hub.curvePoints = ac.curvePoints;
          xViewMin = settings.xViewMin = ac.xViewMin;
          xViewMax = settings.xViewMax = ac.xViewMax;
        }
      }
      logMsg("Imported curve_settings.json");
    } else {
      logMsg("curve_settings.json not found in folder, skipping");
    }
  } catch (std::exception& e) {
    logMsg("curve_settings.json import failed: {}", e.what());
  }

  // --- Import config.yml ---
  try {
    std::string srcYml = folder + "config.yml";
    if (std::ifstream(srcYml).is_open()) {
      YAML::Node c = YAML::LoadFile(srcYml);
      settings.shockParameter = c["SHOCK_PARAMETER"].as<std::string>("Shock");
      settings.secondShockParameter =
          c["SECOND_SHOCK_PARAMETER"].as<std::string>("");
      settings.usePishock = c["USE_PISHOCK"].as<bool>(false);
      settings.randomOrSeq = c["RANDOM_OR_SEQUENTIAL"].as<bool>(false);
      settings.serialPort = c["SERIAL_PORT"].as<std::string>("");
      settings.baseCooldown = c["BASE_COOLDOWN_S"].as<int>(2);
      settings.maxCooldown = c["MAX_COOLDOWN_S"].as<int>(6);
      settings.cooldownFactor = c["COOLDOWN_FACTOR_S"].as<float>(0.4f);
      settings.cooldownWindow = c["COOLDOWN_WINDOW_S"].as<int>(30);
      settings.cooldownEnabled = c["COOLDOWN_ENABLED"].as<bool>(true);
      settings.vrchatHost = c["VRCHAT_HOST"].as<std::string>("127.0.0.1");
      settings.presetCount = c["PRESET_COUNT"].as<int>(3);
      settings.touchSelectThreshold =
          c["TOUCH_SELECT_THRESHOLD"].as<float>(8.f);
      settings.touchMarkerSize = c["TOUCH_MARKER_SIZE"].as<float>(140.f);
      settings.lineWidth = c["LINE_WIDTH"].as<float>(3.f);
      auto hex = [](const std::string& h) {
        unsigned r = 0, g = 0, b = 0;
        sscanf(h.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
        return ImVec4{r / 255.f, g / 255.f, b / 255.f, 1.f};
      };
      settings.outsideCurveBg =
          hex(c["outside_CURVE_BG"].as<std::string>("#2C3749"));
      settings.backgroundColor =
          hex(c["BACKGROUND_COLOR"].as<std::string>("#202630"));
      settings.curveLineColor =
          hex(c["CURVE_LINE_COLOR"].as<std::string>("#00C2FF"));
      settings.markerColor = hex(c["MARKER_COLOR"].as<std::string>("#D88A91"));
      settings.labelColor = hex(c["LABEL_COLOR"].as<std::string>("#E6EEF6"));
      settings.gradientLeftColor =
          hex(c["GRADIENT_LEFT_COLOR"].as<std::string>("#42953b"));
      settings.gradientRightColor =
          hex(c["GRADIENT_RIGHT_COLOR"].as<std::string>("#6e173b"));
      settings.shockerIDs.clear();
      if (c["SHOCKER_IDS"]) {
        for (auto id : c["SHOCKER_IDS"])
          settings.shockerIDs.push_back(std::to_string(id.as<int>()));
      } else {
        std::string id;
        if (c["PISHOCK_SHOCKER_ID"])
          id = c["PISHOCK_SHOCKER_ID"].as<std::string>("");
        else if (c["OPENSHOCK_SHOCKER_ID"])
          id = c["OPENSHOCK_SHOCKER_ID"].as<std::string>("");
        if (!id.empty()) settings.shockerIDs.push_back(id);
      }
      if (settings.shockerIDs.empty()) settings.shockerIDs = {"41838"};
      bool oldXs = c["XSOVERLAY_NOTIFICATIONS"].as<bool>(false);
      bool oldOvr = c["OVRTOOLKIT_NOTIFICATIONS"].as<bool>(false);
      settings.notificationsEnabled = oldXs || oldOvr;
      settings.parameters.clear();
      if (!settings.shockParameter.empty())
        settings.parameters.push_back(
            {settings.shockParameter, 0, CurveRange::Full});
      if (!settings.secondShockParameter.empty())
        settings.parameters.push_back(
            {settings.secondShockParameter, 0, CurveRange::SecondHalf});
      if (settings.parameters.empty()) settings.parameters.push_back({});
      settings.notifUseOvrToolkit = oldOvr;
      logMsg("Imported config.yml");
    } else {
      logMsg("config.yml not found in folder, skipping");
    }
  } catch (std::exception& e) {
    logMsg("config.yml import failed: {}", e.what());
  }

  settings.save(settingsPath);
  return true;
}

// UI entry point
inline void runUI(Settings& settings, ShockerHub& hub,
                  const std::string& settingsPath) {
  extern std::atomic<bool> running;

  static bool g_canSetWindowPos = true;
  glfwSetErrorCallback([](int err, const char* desc) {
    if (err == 65548) return;
    if (err == 65540) return;
    logMsg("[GLFW] Error {}: {}", err, desc);
  });
#ifndef _WIN32
  glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
  if (!glfwInit()) {
    logMsg("[UI] glfwInit failed");
    return;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  g_window = glfwCreateWindow(settings.windowW, settings.windowH, kWindowTitle,
                              nullptr, nullptr);
  if (!g_window) {
    logMsg("[UI] glfwCreateWindow failed");
    glfwTerminate();
    return;
  }

#ifndef _WIN32
  if (!setWindowIcon(g_window)) {
    logMsg("[UI] Failed to load window icon");
  }
#endif

#ifdef _WIN32
  glfwSetWindowPos(g_window, settings.windowX, settings.windowY);
#endif
  glfwMakeContextCurrent(g_window);
  glfwSwapInterval(1);  // vsync

  g_hub = &hub;
  g_settingsForHotkey = &settings;
  registerPanicHotkey(settings);
  g_wakeUiFunc = [] { glfwPostEmptyEvent(); };

  glfwSetWindowCloseCallback(g_window, [](GLFWwindow* win) {
    glfwSetWindowShouldClose(win, GLFW_TRUE);
    running = false;

    g_wakeUiFunc = nullptr;

    if (g_hub) {
      g_hub->queueCV.notify_all();
    }
  });

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;

  ImFont* boldFont = nullptr;
  {
    const char* regularPaths[] = {
        "/usr/share/fonts/truetype/cantarell/Cantarell-Regular.otf",
        "/usr/share/fonts/cantarell/Cantarell-Regular.otf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        nullptr};
    const char* boldPaths[] = {
        "/usr/share/fonts/truetype/cantarell/Cantarell-Bold.otf",
        "/usr/share/fonts/cantarell/Cantarell-Bold.otf",
        "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        nullptr};
    const char* symPaths[] = {
        "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf",
        "/usr/share/fonts/noto/NotoSansSymbols2-Regular.ttf",
        "/usr/share/fonts/noto-fonts/NotoSansSymbols2-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansSymbols-Regular.ttf",
        "/usr/share/fonts/noto/NotoSansSymbols-Regular.ttf",
        nullptr};

    for (auto p = regularPaths; *p; ++p) {
      if (std::filesystem::exists(*p)) {
        io.Fonts->AddFontFromFileTTF(*p, 18.0f);
        break;
      }
    }
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();

    // Merge symbol font for ⚡ (U+26A1)
    for (auto p = symPaths; *p; ++p) {
      if (std::filesystem::exists(*p)) {
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.GlyphOffset = {0, 0.f};
        static const ImWchar ranges[] = {0x2600, 0x27FF, 0};
        io.Fonts->AddFontFromFileTTF(*p, 18.0f, &cfg, ranges);
        break;
      }
    }

    for (auto p = boldPaths; *p; ++p) {
      if (std::filesystem::exists(*p)) {
        boldFont = io.Fonts->AddFontFromFileTTF(*p, 18.0f);
        break;
      }
    }
  }

  ImVec4& bgColor = settings.outsideCurveBg;
  ImPlot::GetStyle().Colors[ImPlotCol_LegendBg] = {bgColor.x, bgColor.y,
                                                   bgColor.z, 0.84f};
  ImPlot::GetStyle().Colors[ImPlotCol_LegendBorder] = {0.4f, 0.4f, 0.5f, 0.8f};
  ImPlot::GetStyle().LegendPadding = ImVec2(10, 8);
  ImPlot::GetStyle().LegendInnerPadding = ImVec2(6, 4);
  ImPlot::GetStyle().LegendSpacing = ImVec2(6, 4);

  ImGui::StyleColorsDark();
  applyUiTheme(settings);

  ImGui_ImplGlfw_InitForOpenGL(g_window, true);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  // Dynamic UI state
  float minDur = settings.minShockDuration;
  float maxDur = settings.maxShockDuration;
  bool cooldownEnabled = settings.cooldownEnabled;
  float xViewMin = settings.xViewMin;
  float xViewMax = settings.xViewMax;

  int currentCurveIndex = 0;

  std::deque<AppState> undoStack, redoStack;
  bool isPerformingUndoRedo = false;
  bool ctrlZPrev = false, ctrlYPrev = false;
  bool stateChangedPreviousFrame = false;

  if (settings.defaultPreset >= 0 &&
      settings.defaultPreset < (int)settings.presets.size() &&
      settings.presets[settings.defaultPreset].has_value()) {
    auto& dp = settings.presets[settings.defaultPreset];
    // Preserve names across preset load
    std::vector<std::string> savedNames;
    for (auto& c : settings.curves) savedNames.push_back(c.name);

    settings.curves = dp->curves;
    currentCurveIndex = dp->activeCurveIndex;

    for (int k = 0;
         k < (int)settings.curves.size() && k < (int)savedNames.size(); k++)
      settings.curves[k].name = savedNames[k];

    if (currentCurveIndex >= (int)settings.curves.size()) currentCurveIndex = 0;
    for (auto& param : settings.parameters)
      if (param.curveIndex >= (int)settings.curves.size()) param.curveIndex = 0;

    if (!settings.curves.empty()) {
      hub.curvePoints = settings.curves[currentCurveIndex].curvePoints;
      xViewMin = settings.curves[currentCurveIndex].xViewMin;
      xViewMax = settings.curves[currentCurveIndex].xViewMax;
      minDur = settings.curves[currentCurveIndex].minShockDuration;
      maxDur = settings.curves[currentCurveIndex].maxShockDuration;
    }
  }

  // Settings/Stats modal state
  bool showSettings = false;
  float settingsAnim = 0.f, statsAnim = 0.f;
  if (settings.showStats) statsAnim = 1.f;

  // Fixes launching the app when stats is saved open
  {
    int sw = (int)roundf(statsAnim * 280.f);
    int settW = (int)roundf(settingsAnim * 550.f);
    glfwSetWindowPos(g_window, settings.windowX - sw, settings.windowY);
    glfwSetWindowSize(g_window, settings.windowW + sw + settW,
                      settings.windowH);
  }

  // Editable staging copies (only written back on save)
  char stgShockParam[64] = {}, stgSecondParam[64] = {}, stgShockerIDs[256] = {},
       stgSerialPort[64] = {}, stgVrchatHost[64] = {};
  bool stgUsePishock = false, stgRandomOrSeq = false, stgNotifEnabled = false,
       stgNotifUseOvr = false, stgUseSerial = true,
       stgChatboxShockEnabled = true, stgChatboxCooldownEnabled = true;
  int stgBaseCooldown = 2, stgMaxCooldown = 6, stgCooldownWindow = 30,
      stgPresetCount = 3;
  float stgCooldownFactor = 0.4f, stgTouchThreshold = 8.f;
  char stgPishockUser[128] = {}, stgPishockKey[128] = {},
       stgOpenshockToken[256] = {}, stgOpenshockServer[128] = {};
  std::vector<Parameter> stgParameters;

  UiContext ui{settings,
               hub,
               minDur,
               maxDur,
               xViewMin,
               xViewMax,
               cooldownEnabled,
               stgShockParam,
               stgSecondParam,
               stgShockerIDs,
               stgSerialPort,
               stgVrchatHost,
               stgUsePishock,
               stgRandomOrSeq,
               stgBaseCooldown,
               stgMaxCooldown,
               stgCooldownFactor,
               stgCooldownWindow,
               stgNotifEnabled,
               stgNotifUseOvr,
               stgUseSerial,
               stgChatboxShockEnabled,
               stgChatboxCooldownEnabled,
               stgPishockUser,
               stgPishockKey,
               stgOpenshockToken,
               stgOpenshockServer,
               stgPresetCount,
               stgTouchThreshold,
               stgParameters};

  AppState lastCommittedState = snapshotAppState(ui);

  // Style copies apply live on edit, so point directly at settings field
  auto openSettingsModal = [&]() {
    snprintf(stgShockParam, sizeof(stgShockParam), "%s",
             settings.shockParameter.c_str());
    snprintf(stgSecondParam, sizeof(stgSecondParam), "%s",
             settings.secondShockParameter.c_str());
    snprintf(stgSerialPort, sizeof(stgSerialPort), "%s",
             settings.serialPort.c_str());
    snprintf(stgVrchatHost, sizeof(stgVrchatHost), "%s",
             settings.vrchatHost.c_str());
    snprintf(stgPishockUser, sizeof(stgPishockUser), "%s",
             settings.pishockUsername.c_str());
    snprintf(stgPishockKey, sizeof(stgPishockKey), "%s",
             settings.pishockApiKey.c_str());
    snprintf(stgOpenshockToken, sizeof(stgOpenshockToken), "%s",
             settings.openshockApiToken.c_str());
    snprintf(stgOpenshockServer, sizeof(stgOpenshockServer), "%s",
             settings.openshockServerUrl.c_str());
    std::string ids;
    for (int i = 0; i < (int)settings.shockerIDs.size(); i++)
      ids += (i ? ", " : "") + settings.shockerIDs[i];
    snprintf(stgShockerIDs, sizeof(stgShockerIDs), "%s", ids.c_str());
    stgUsePishock = settings.usePishock;
    stgRandomOrSeq = settings.randomOrSeq;
    stgBaseCooldown = settings.baseCooldown;
    stgMaxCooldown = settings.maxCooldown;
    stgCooldownFactor = settings.cooldownFactor;
    stgCooldownWindow = settings.cooldownWindow;
    stgNotifEnabled = settings.notificationsEnabled;
    stgNotifUseOvr = settings.notifUseOvrToolkit;
    stgUseSerial = settings.useSerial;
    stgPresetCount = settings.presetCount;
    stgTouchThreshold = settings.touchSelectThreshold;
    stgChatboxShockEnabled = settings.chatboxShockEnabled;
    stgChatboxCooldownEnabled = settings.chatboxCooldownEnabled;
    stgParameters = settings.parameters;
  };

  auto closeSettingsModal = [&]() {
    Settings reverted(settingsPath);
    settings.backgroundColor = reverted.backgroundColor;
    settings.outsideCurveBg = reverted.outsideCurveBg;
    settings.accentColor = reverted.accentColor;
    settings.curveLineColor = reverted.curveLineColor;
    settings.markerColor = reverted.markerColor;
    settings.labelColor = reverted.labelColor;
    settings.gradientLeftColor = reverted.gradientLeftColor;
    settings.gradientRightColor = reverted.gradientRightColor;
    showSettings = false;
  };

  bool capturingHotkey = false;
  bool panicWasPressedLastFrame = false;

  // Curve cache
  std::array<CurvePoint, 3>& pts = hub.curvePoints;
  std::array<CurvePoint, 3> lastPts = {};
  std::vector<double> cx, cy;

  ImVec4& clear = settings.backgroundColor;
  bool forceFrame = true;
  auto lastAnimTime = steady_clock::now();

  while (!glfwWindowShouldClose(g_window) && running.load()) {
    bool minimized = glfwGetWindowAttrib(g_window, GLFW_ICONIFIED);
    bool focused = glfwGetWindowAttrib(g_window, GLFW_FOCUSED) || showSettings;

    bool cooldownActive = settings.cooldownEnabled &&
                          hub.cooldownUntil.load() > hub.getCurrentTime();
    bool statsAnimating =
        fabs(statsAnim - (settings.showStats ? 1.f : 0.f)) > 0.001f;
    bool settAnimating =
        fabs(settingsAnim - (showSettings ? 1.f : 0.f)) > 0.001f;
    bool needsAnimation = cooldownActive || !hub.isConnected ||
                          statsAnimating || settAnimating || forceFrame;

    if (minimized) {
      glfwWaitEvents();
      continue;
    }

    if (forceFrame) {
      glfwPollEvents();
    } else if (!needsAnimation) {
      glfwWaitEvents();
    } else {
      double targetInterval = focused ? (1.0 / 60.0) : (1.0 / 16.0);
      glfwWaitEventsTimeout(targetInterval);
    }

    // Window position tracking
    {
      int ww, wh;
      int wx, wy;
      glfwGetWindowPos(g_window, &wx, &wy);
      glfwGetWindowSize(g_window, &ww, &wh);
      bool fullyIdle = (statsAnim == 0.f && settingsAnim == 0.f);
      if (fullyIdle) {
        settings.windowX = wx;
        settings.windowW = ww;
      }
      settings.windowY = wy;
      settings.windowH = wh;
    }

    // Panel slide animations
    {
      auto now = steady_clock::now();
      float dt =
          std::min(duration<float>(now - lastAnimTime).count(), 1.f / 30.f);
      lastAnimTime = now;
      float settTarget = showSettings ? 1.f : 0.f;
      float statsTarget = settings.showStats ? 1.f : 0.f;
      float prevSett = settingsAnim, prevStats = statsAnim;
      settingsAnim += (settTarget - settingsAnim) * std::min(1.f, dt * 12.f);
      statsAnim += (statsTarget - statsAnim) * std::min(1.f, dt * 12.f);
      if (settingsAnim < 0.001f) settingsAnim = 0.f;
      if (statsAnim < 0.043f) statsAnim = 0.f;
      if (fabs(statsAnim - prevStats) > 0.001f ||
          fabs(settingsAnim - prevSett) > 0.001f) {
        int sw = (int)roundf(statsAnim * 280.f);
        int settW = (int)roundf(settingsAnim * 550.f);
        glfwSetWindowPos(g_window, settings.windowX - sw, settings.windowY);
        glfwSetWindowSize(g_window, settings.windowW + sw + settW,
                          settings.windowH);
      }
      if ((settingsAnim == 0.f && prevSett > 0.f) ||
          (statsAnim == 0.f && prevStats > 0.f))
        forceFrame = true;
    }

    // Panic hotkey polling (window-focused only on Linux)
    if (settings.hotkeyVk != 0) {
      bool pressed = glfwGetKey(g_window, settings.hotkeyVk) == GLFW_PRESS;
      if (pressed && !panicWasPressedLastFrame) {
        hub.shocksDisabled = true;
        logMsg("[Hotkey] Shocks disabled.");
      }
      panicWasPressedLastFrame = pressed;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    applyUiTheme(settings);

    float statsW = statsAnim * 280.f;
    ImGui::SetNextWindowPos({statsW, 0});
    ImGui::SetNextWindowSize(
        {ImGui::GetIO().DisplaySize.x - statsW, ImGui::GetIO().DisplaySize.y});
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    float fontSize = ImGui::GetFontSize();

    // Left panel
    float leftPanelWidth = std::max(180.f, fontSize * 10.0f);

    float lineH = ImGui::GetTextLineHeightWithSpacing();
    float logH = lineH * 3.f + ImGui::GetStyle().WindowPadding.y * 2.f;
    float rowH = ImGui::GetTextLineHeightWithSpacing() + 3.f;
    float sepH = 1.f + ImGui::GetStyle().ItemSpacing.y * 2.f;
    float bottomH = rowH + logH + sepH;

    ImGui::BeginChild("##controls", ImVec2(leftPanelWidth, -bottomH), true);
    ImGui::SetCursorPosY(ImGui::GetStyle().WindowPadding.y * 0.5f);

    ImGui::Spacing();
    ImGui::Text("Min Duration (s)");
    ImGui::SliderFloat("##mind", &minDur, 0.1f, 5.f, "%.1f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      minDur = std::min(minDur, maxDur - 0.1f);
      settings.minShockDuration = minDur;
      // Write back to current curve immediately
      if (currentCurveIndex >= 0 &&
          currentCurveIndex < (int)settings.curves.size())
        settings.curves[currentCurveIndex].minShockDuration = minDur;
    }

    ImGui::SliderFloat("##maxd", &maxDur, 0.1f, 5.f, "%.1f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      maxDur = std::max(maxDur, minDur + 0.1f);
      settings.maxShockDuration = maxDur;
      // Write back to current curve immediately
      if (currentCurveIndex >= 0 &&
          currentCurveIndex < (int)settings.curves.size())
        settings.curves[currentCurveIndex].maxShockDuration = maxDur;
    }

    ImGui::Spacing();
    if (ImGui::Checkbox("Enable Cooldown", &cooldownEnabled))
      settings.cooldownEnabled = cooldownEnabled;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Presets");
    for (int i = 0; i < (int)settings.presets.size(); i++) {
      bool hasData = settings.presets[i].has_value();
      std::string label = hasData ? settings.presets[i]->name
                                  : ("Preset " + std::to_string(i + 1));

      bool isDefault = (settings.defaultPreset == i);
      if (isDefault)
        ImGui::PushStyleColor(ImGuiCol_Button, {0.17f, 0.54f, 0.34f, 1.f});
      ImGui::Button(label.c_str(),
                    ImVec2(fontSize * 10.f - fontSize * 2.5f, 0));
      ImGui::SetItemTooltip(
          "LClick - Load\nMClick - Startup default\nRClick - Rename");

      if (isDefault) ImGui::PopStyleColor();

      // Left click - load preset: replace all curves with preset's full set
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && hasData) {
        // Flush current curve state before switching
        if (currentCurveIndex >= 0 &&
            currentCurveIndex < (int)settings.curves.size()) {
          settings.curves[currentCurveIndex].curvePoints = hub.curvePoints;
          settings.curves[currentCurveIndex].xViewMin = xViewMin;
          settings.curves[currentCurveIndex].xViewMax = xViewMax;
          settings.curves[currentCurveIndex].minShockDuration = minDur;
          settings.curves[currentCurveIndex].maxShockDuration = maxDur;
        }

        auto& preset = *settings.presets[i];

        // Replace the entire curve list with what the preset stored
        settings.curves = preset.curves;
        if (settings.curves.empty()) {
          settings.curves.push_back(Preset());
          settings.curves[0].name = "Default";
        }

        currentCurveIndex = preset.activeCurveIndex;
        if (currentCurveIndex >= (int)settings.curves.size())
          currentCurveIndex = 0;

        // Load the active curve's per-curve settings
        hub.curvePoints = settings.curves[currentCurveIndex].curvePoints;
        xViewMin = settings.curves[currentCurveIndex].xViewMin;
        xViewMax = settings.curves[currentCurveIndex].xViewMax;
        minDur = settings.curves[currentCurveIndex].minShockDuration;
        maxDur = settings.curves[currentCurveIndex].maxShockDuration;

        settings.minShockDuration = minDur;
        settings.maxShockDuration = maxDur;

        // Clamp parameter curve indices to the new curve list size
        for (auto& param : settings.parameters) {
          if (param.curveIndex >= (int)settings.curves.size())
            param.curveIndex = 0;
        }
      }

      // Middle click - set default
      if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) {
        settings.defaultPreset = i;
        settings.save(settingsPath);
      }
      // Right click - open rename popup
      if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && hasData)
        ImGui::OpenPopup(("##rename" + std::to_string(i)).c_str());

      // Rename popup
      if (ImGui::BeginPopup(("##rename" + std::to_string(i)).c_str())) {
        static char nameBuf[64] = {};
        if (ImGui::IsWindowAppearing())
          snprintf(nameBuf, sizeof(nameBuf), "%s", label.c_str());
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
          settings.presets[i]->name = nameBuf;
          settings.save(settingsPath);
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }

      ImGui::SameLine();
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                           (ImGui::GetFrameHeight() - 16.f) * 0.5f);
      // Save preset: snapshot ALL current curves into this slot
      if (drawSaveIconButton(("##save" + std::to_string(i)).c_str())) {
        // Flush active curve state first
        if (currentCurveIndex >= 0 &&
            currentCurveIndex < (int)settings.curves.size()) {
          settings.curves[currentCurveIndex].curvePoints = hub.curvePoints;
          settings.curves[currentCurveIndex].xViewMin = xViewMin;
          settings.curves[currentCurveIndex].xViewMax = xViewMax;
          settings.curves[currentCurveIndex].minShockDuration = minDur;
          settings.curves[currentCurveIndex].maxShockDuration = maxDur;
        }

        // Snapshot ALL curves into this preset slot
        SavedPreset sp;
        sp.curves = settings.curves;
        sp.activeCurveIndex = currentCurveIndex;
        sp.name = settings.presets[i].has_value()
                      ? settings.presets[i]->name
                      : ("Preset " + std::to_string(i + 1));

        settings.presets[i] = sp;
        settings.save(settingsPath);
      }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Test Vibrate", {80.0f, 0})) hub.queueShock(-1, true);
    ImGui::SetItemTooltip("Sends a vibration command");
    ImGui::SameLine();
    if (ImGui::Button("Test Shock", {-1, 0})) hub.queueShock(-1, false);
    ImGui::SetItemTooltip("Sends a Shock command");

    {
      int extraRows = (!hub.isConnected ? 1 : 0) + (hub.shocksDisabled ? 1 : 0);
      float totalBtnsH = (1 + extraRows) * ImGui::GetFrameHeightWithSpacing() +
                         ImGui::GetStyle().WindowPadding.y;
      ImGui::SetCursorPosY(ImGui::GetWindowHeight() - totalBtnsH);
    }
    if (!hub.isConnected)
      if (ImGui::Button("Retry Connection", {-1, 0})) hub.tryReconnect();
    if (hub.shocksDisabled)
      if (ImGui::Button("Enable Shocks", {-1, 0})) hub.enableShocks();

    float halfBtn =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) *
        0.5f;
    if (ImGui::Button("Stats", {halfBtn, 0}))
      settings.showStats = !settings.showStats;
    ImGui::SameLine();
    if (ImGui::Button("Settings", {-1, 0})) {
      if (!showSettings) {
        openSettingsModal();
        showSettings = true;
      } else
        closeSettingsModal();
    }

    if (showSettings)
      ImGui::SetItemTooltip("Will close settings without saving.");

    ImGui::EndChild();

    // Curve editor
    ImGui::SameLine();
    ImGui::BeginChild("##plot", {settings.windowW - 220.f, -bottomH}, false);

    // Curve selector bar
    {
      auto saveCurrentCurve = [&]() {
        if (currentCurveIndex >= 0 &&
            currentCurveIndex < (int)settings.curves.size()) {
          settings.curves[currentCurveIndex].curvePoints = hub.curvePoints;
          settings.curves[currentCurveIndex].xViewMin = xViewMin;
          settings.curves[currentCurveIndex].xViewMax = xViewMax;
          settings.curves[currentCurveIndex].minShockDuration = minDur;
          settings.curves[currentCurveIndex].maxShockDuration = maxDur;
        }
      };

      float tabButtonWidth = fontSize * 4.f;
      static int wantsRenameCurve = -1;
      static char renameCurveBuf[256] = {};

      for (int i = 0; i < (int)settings.curves.size(); i++) {
        bool isCurrent = (currentCurveIndex == i);
        if (isCurrent)
          ImGui::PushStyleColor(ImGuiCol_Button, {0.17f, 0.54f, 0.34f, 1.f});

        std::string label = settings.curves[i].name.empty()
                                ? ("Curve " + std::to_string(i + 1))
                                : settings.curves[i].name;

        if (ImGui::Button(label.c_str(), {tabButtonWidth, 0})) {
          saveCurrentCurve();

          currentCurveIndex = i;

          // Load the selected curve's per-curve data
          hub.curvePoints = settings.curves[currentCurveIndex].curvePoints;
          xViewMin = settings.curves[currentCurveIndex].xViewMin;
          xViewMax = settings.curves[currentCurveIndex].xViewMax;
          minDur = settings.curves[currentCurveIndex].minShockDuration;
          maxDur = settings.curves[currentCurveIndex].maxShockDuration;

          settings.minShockDuration = minDur;
          settings.maxShockDuration = maxDur;
        }

        // Right-click menu
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
          ImGui::OpenPopup(("##curve_menu" + std::to_string(i)).c_str());

        if (ImGui::BeginPopup(("##curve_menu" + std::to_string(i)).c_str())) {
          if (ImGui::MenuItem("Rename")) {
            wantsRenameCurve = i;
            snprintf(renameCurveBuf, sizeof(renameCurveBuf), "%s",
                     settings.curves[i].name.c_str());
          }
          if (ImGui::MenuItem("Clone")) {
            saveCurrentCurve();
            Preset cloned = settings.curves[i];
            cloned.name = cloned.name + " (copy)";
            settings.curves.push_back(cloned);
            // Add cloned version to all saved presets, matching by source name
            std::string srcName = settings.curves[i].name;
            for (auto& p : settings.presets) {
              if (!p.has_value()) continue;
              bool found = false;
              for (auto& pc : p->curves) {
                if (pc.name == srcName) {
                  Preset pCloned = pc;
                  pCloned.name = cloned.name;
                  p->curves.push_back(pCloned);
                  found = true;
                  break;
                }
              }
              if (!found) p->curves.push_back(cloned);
            }
            settings.save(settingsPath);
          }
          if (ImGui::MenuItem("Delete")) {
            saveCurrentCurve();
            std::string deletedName = settings.curves[i].name;
            settings.curves.erase(settings.curves.begin() + i);
            // Ensure at least one curve always exists
            if (settings.curves.empty()) {
              settings.curves.push_back(Preset());
              settings.curves[0].name = "Default";
            }
            if (currentCurveIndex >= (int)settings.curves.size())
              currentCurveIndex = (int)settings.curves.size() - 1;
            // Remove matching curve from all saved presets
            for (auto& p : settings.presets) {
              if (!p.has_value()) continue;
              auto& pc = p->curves;
              pc.erase(std::remove_if(pc.begin(), pc.end(),
                                      [&](const Preset& c) {
                                        return c.name == deletedName;
                                      }),
                       pc.end());
              if (pc.empty()) {
                pc.push_back(Preset());
                pc[0].name = "Default";
              }
              if (p->activeCurveIndex >= (int)pc.size())
                p->activeCurveIndex = std::max(0, (int)pc.size() - 1);
            }
            // Fallback any parameters that pointed at the deleted curve
            for (auto& param : settings.parameters) {
              if (param.curveIndex >= (int)settings.curves.size())
                param.curveIndex = 0;
            }
            hub.curvePoints = settings.curves[currentCurveIndex].curvePoints;
            xViewMin = settings.curves[currentCurveIndex].xViewMin;
            xViewMax = settings.curves[currentCurveIndex].xViewMax;
            minDur = settings.curves[currentCurveIndex].minShockDuration;
            maxDur = settings.curves[currentCurveIndex].maxShockDuration;
            settings.minShockDuration = minDur;
            settings.maxShockDuration = maxDur;
            settings.save(settingsPath);
          }
          ImGui::EndPopup();
        }

        if (isCurrent) ImGui::PopStyleColor();
        ImGui::SameLine();
      }

      // Add new curve
      if (ImGui::Button("+", {tabButtonWidth, 0})) {
        saveCurrentCurve();
        Preset newCurve;
        newCurve.name = "Curve " + std::to_string(settings.curves.size() + 1);
        settings.curves.push_back(newCurve);
        // Add a default version of the new curve to all saved presets
        for (auto& p : settings.presets) {
          if (p.has_value()) p->curves.push_back(newCurve);
        }
        currentCurveIndex = (int)settings.curves.size() - 1;
        hub.curvePoints = newCurve.curvePoints;
        xViewMin = newCurve.xViewMin;
        xViewMax = newCurve.xViewMax;
        minDur = newCurve.minShockDuration;
        maxDur = newCurve.maxShockDuration;
        settings.save(settingsPath);
      }
      ImGui::SetItemTooltip("Add new curve");

      // Rename popup
      if (wantsRenameCurve >= 0 && !ImGui::IsPopupOpen("##rename_curve_modal"))
        ImGui::OpenPopup("##rename_curve_modal");
      if (ImGui::BeginPopup("##rename_curve_modal")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("Rename##rcm", renameCurveBuf,
                             sizeof(renameCurveBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
          if (wantsRenameCurve >= 0 &&
              wantsRenameCurve < (int)settings.curves.size()) {
            std::string oldName = settings.curves[wantsRenameCurve].name;
            settings.curves[wantsRenameCurve].name = renameCurveBuf;
            // Sync the rename to all saved presets
            for (auto& p : settings.presets) {
              if (!p.has_value()) continue;
              for (auto& pc : p->curves) {
                if (pc.name == oldName) pc.name = renameCurveBuf;
              }
            }
          }
          settings.save(settingsPath);
          wantsRenameCurve = -1;
          ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
          wantsRenameCurve = -1;
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      } else if (!ImGui::IsPopupOpen("##rename_curve_modal")) {
        wantsRenameCurve = -1;
      }
      ImGui::NewLine();
    }

    float sliderH = ImGui::GetFrameHeightWithSpacing() + 4;
    ImVec2 savedPlotPos = {}, savedPlotSize = {};
    ImVec2 plotFramePos = ImGui::GetCursorScreenPos();
    float plotFrameWidth = ImGui::GetContentRegionAvail().x;

    if (ImPlot::BeginPlot(" ", {-1, -sliderH})) {
      ImPlot::SetupAxes("Intensity (%)", "Weight", ImPlotAxisFlags_NoGridLines,
                        ImPlotAxisFlags_NoGridLines);
      ImPlot::SetupAxisLimits(ImAxis_X1, xViewMin, xViewMax, ImPlotCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1, ImPlotCond_Always);
      ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_None);
      ImPlot::SetupFinish();

      ImDrawList* dl = ImPlot::GetPlotDrawList();
      const char* title = "Intensity Curve";
      ImVec2 plot_pos = ImPlot::GetPlotPos();
      ImVec2 plot_size = ImPlot::GetPlotSize();
      ImVec2 text_size =
          boldFont ? boldFont->CalcTextSizeA(18.0f, FLT_MAX, 0.0f, title)
                   : ImGui::CalcTextSize(title);
      ImVec2 titlePos = {plot_pos.x + (plot_size.x - text_size.x) * 0.5f,
                         plot_pos.y - ImGui::GetTextLineHeight() - 4};
      if (boldFont)
        dl->AddText(boldFont, 18.0f, titlePos,
                    ImGui::ColorConvertFloat4ToU32(
                        ImGui::GetStyle().Colors[ImGuiCol_Text]),
                    title);
      else
        dl->AddText(titlePos,
                    ImGui::ColorConvertFloat4ToU32(
                        ImGui::GetStyle().Colors[ImGuiCol_Text]),
                    title);

      ImPlot::PushPlotClipRect();
      ImVec2 pmin = ImPlot::PlotToPixels({0, 0});
      ImVec2 pmax = ImPlot::PlotToPixels({100, 1});
      dl->AddRectFilledMultiColor(
          pmin, pmax,
          ImGui::ColorConvertFloat4ToU32(settings.gradientLeftColor),
          ImGui::ColorConvertFloat4ToU32(settings.gradientRightColor),
          ImGui::ColorConvertFloat4ToU32(settings.gradientRightColor),
          ImGui::ColorConvertFloat4ToU32(settings.gradientLeftColor));
      ImPlot::PopPlotClipRect();

      {
        // Sort + cache curve
        auto sorted = pts;
        std::sort(
            sorted.begin(), sorted.end(),
            [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
        if (sorted != lastPts) {
          lastPts = sorted;
          auto curve = bezierInterpolate(sorted[0], sorted[1], sorted[2]);
          cx.resize(curve.size());
          cy.resize(curve.size());
          for (size_t i = 0; i < curve.size(); i++) {
            cx[i] = curve[i].x;
            cy[i] = curve[i].y;
          }
        }

        char minLabel[64], maxLabel[64];
        snprintf(minLabel, sizeof(minLabel), "Min: %.0f%%  W: %.2f",
                 sorted[0].x, sorted[0].y);
        snprintf(maxLabel, sizeof(maxLabel), "Max: %.0f%%  W: %.2f",
                 sorted[2].x, sorted[2].y);
        ImVec4 minCol = {0.35f, 0.9f, 0.35f, 1.f};
        ImVec4 maxCol = {0.9f, 0.35f, 0.35f, 1.f};
        ImPlot::SetNextLineStyle(minCol, 1.5f);
        ImPlot::PlotLine(minLabel, (double*)nullptr, 0);
        ImPlot::SetNextLineStyle(maxCol, 1.5f);
        ImPlot::PlotLine(maxLabel, (double*)nullptr, 0);

        ImPlot::PushPlotClipRect();
        ImDrawList* dl2 = ImPlot::GetPlotDrawList();

        // Manual grid on top of gradient
        ImU32 gridCol = ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.25f));
        for (int x = 0; x <= 100; x += 10) {
          ImVec2 p0 = ImPlot::PlotToPixels({(double)x, 0.0});
          ImVec2 p1 = ImPlot::PlotToPixels({(double)x, 1.0});
          dl2->AddLine(p0, p1, gridCol, 1.f);
        }
        for (int y = 0; y <= 10; y++) {
          double yv = y * 0.1;
          ImVec2 p0 = ImPlot::PlotToPixels({0.0, yv});
          ImVec2 p1 = ImPlot::PlotToPixels({100.0, yv});
          dl2->AddLine(p0, p1, gridCol, 1.f);
        }

        // Dashed vertical lines for min/max
        auto drawDashedV = [&](double x, ImVec4 col, float thickness) {
          ImVec2 top = ImPlot::PlotToPixels({x, 1.0});
          ImVec2 bot = ImPlot::PlotToPixels({x, 0.0});
          ImU32 c = ImGui::ColorConvertFloat4ToU32(col);
          float y = top.y, dash = 8.f, gap = 5.f;
          while (y < bot.y) {
            dl2->AddLine({top.x, y}, {top.x, std::min(y + dash, bot.y)}, c,
                         thickness);
            y += dash + gap;
          }
        };
        drawDashedV(sorted[0].x, minCol, 1.5f);
        drawDashedV(sorted[2].x, maxCol, 1.5f);

        ImPlot::SetNextLineStyle(settings.curveLineColor, settings.lineWidth);
        ImPlot::PlotLine("##curve", cx.data(), cy.data(), (int)cx.size());
        for (int i = 0; i < 3; i++) {
          ImPlot::DragPoint(i, &pts[i].x, &pts[i].y, settings.markerColor,
                            settings.touchMarkerSize / 15.f,
                            ImPlotDragToolFlags_None);
          pts[i].x = std::clamp(pts[i].x, 0.0, 100.0);
          pts[i].y = std::clamp(pts[i].y, 0.0, 1.0);
        }

        ImPlot::PopPlotClipRect();
      }
      savedPlotPos = ImPlot::GetPlotPos();
      savedPlotSize = ImPlot::GetPlotSize();

      ImPlot::EndPlot();
    }

    {
      ImVec2 sc = ImGui::GetCursorScreenPos();
      float gap = ImGui::GetStyle().ItemSpacing.y;
      float sliderRowH = ImGui::GetFrameHeight() + 4;
      ImGui::GetWindowDrawList()->AddRectFilled(
          {plotFramePos.x, sc.y - gap},
          {plotFramePos.x + plotFrameWidth - 1.f, sc.y + sliderRowH},
          ImGui::ColorConvertFloat4ToU32(settings.outsideCurveBg));
      ImGui::GetWindowDrawList()->AddText(
          {plotFramePos.x + 6,
           sc.y +
               (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f +
               2},
          ImGui::ColorConvertFloat4ToU32(
              ImGui::GetStyle().Colors[ImGuiCol_Text]),
          "X Scale");
      ImGui::SetCursorScreenPos({savedPlotPos.x, sc.y + 2});
      drawRangeSliderFloat("##xrange", &xViewMin, &xViewMax, 0.f, 100.f,
                           savedPlotSize.x);
      ImGui::SetCursorScreenPos(
          {plotFramePos.x + plotFrameWidth, sc.y + sliderRowH});
    }

    ImGui::EndChild();

    // Log bar
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - rowH - logH - sepH -
                         ImGui::GetStyle().WindowPadding.y);
    ImGui::Separator();
    ImGui::BeginChild("##log", {0, logH}, false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    {
      std::lock_guard<std::mutex> lock(gLog.mtx);
      for (auto& line : gLog.lines) ImGui::TextUnformatted(line.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
      ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    // Status bar
    ImGui::Separator();
    {
      ImGui::SetCursorPosY(ImGui::GetWindowHeight() - rowH);
      float cy2 = ImGui::GetCursorScreenPos().y +
                  ImGui::GetTextLineHeight() * 0.5f + 2.f;
      float cx2 = ImGui::GetCursorScreenPos().x;
      ImDrawList* dl2 = ImGui::GetWindowDrawList();

      // Connection circle
      ImU32 connCol = hub.isConnected ? IM_COL32(60, 220, 80, 255)
                                      : IM_COL32(220, 60, 60, 255);
      dl2->AddCircleFilled({cx2 + 7.f, cy2}, 5.f, connCol);
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 17.f);
      if (!hub.shocksDisabled)
        ImGui::Text(hub.isConnected ? "Connected" : "Disconnected");
      else
        ImGui::TextColored({1.f, 0.3f, 0.3f, 1.f}, "SHOCKS DISABLED");

      ImGui::SameLine(0, 12);
      ImGui::TextDisabled("|");
      ImGui::SameLine(0, 12);

      // \xe2\x9a\xa1 = ⚡ symbol
      ImGui::Text("\xe2\x9a\xa1 %d", gStats.sessionShocks);

      ImGui::SameLine(0, 12);
      ImGui::TextDisabled("|");
      ImGui::SameLine(0, 12);

      // Cooldown indicator
      if (settings.cooldownEnabled) {
        double rem =
            std::max(0.0, hub.cooldownUntil.load() - hub.getCurrentTime());
        if (rem > 0.0)
          ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "Cooldown: %.1fs", rem);
        else
          ImGui::TextDisabled("Cooldown: 0.0s");

        ImGui::SameLine(0, 8);

        // Cooldown bar
        double remaining =
            std::max(0.0, hub.cooldownUntil.load() - hub.getCurrentTime());
        float fraction = (float)(remaining / settings.maxCooldown);
        float height = 3.f, textH = ImGui::GetTextLineHeight();
        ImVec2 p = ImGui::GetCursorScreenPos();
        p.y += (textH - height) * 0.7f;
        float len = ImGui::GetContentRegionAvail().x * 0.28f;
        dl2->AddRectFilled(p, {p.x + len, p.y + height},
                           ImGui::ColorConvertFloat4ToU32(
                               ImGui::GetStyle().Colors[ImGuiCol_FrameBg]));
        if (fraction > 0.f)
          dl2->AddRectFilled(p, {p.x + len * fraction, p.y + height},
                             IM_COL32(220, 80, 80, 255));
        ImGui::Dummy({len, height});
      }

      // Right-align version
      float verW = ImGui::CalcTextSize("v" APP_VERSION).x;
      ImGui::SameLine(ImGui::GetContentRegionMax().x - verW);
      ImGui::TextDisabled("v" APP_VERSION);
    }

    ImGui::End();

    if (updateReady.exchange(false)) {
#ifdef _WIN32
      Updater::applyAndRestart(nullptr);
#else
      Updater::applyAndRestart();
#endif
      glfwSetWindowShouldClose(g_window, 1);
    }

    auto commitAll = [&]() {
      settings.shockParameter = stgShockParam;
      settings.secondShockParameter = stgSecondParam;
      settings.serialPort = stgSerialPort;
      settings.vrchatHost = stgVrchatHost;
      settings.usePishock = stgUsePishock;
      settings.randomOrSeq = stgRandomOrSeq;
      settings.baseCooldown = stgBaseCooldown;
      settings.maxCooldown = stgMaxCooldown;
      settings.cooldownFactor = stgCooldownFactor;
      settings.cooldownWindow = stgCooldownWindow;
      settings.notificationsEnabled = stgNotifEnabled;
      settings.notifUseOvrToolkit = stgNotifUseOvr;
      settings.useSerial = stgUseSerial;
      settings.pishockUsername = stgPishockUser;
      settings.pishockApiKey = stgPishockKey;
      settings.openshockApiToken = stgOpenshockToken;
      settings.openshockServerUrl = stgOpenshockServer;
      settings.presetCount = stgPresetCount;
      settings.touchSelectThreshold = stgTouchThreshold;
      settings.parameters = stgParameters;
      settings.chatboxShockEnabled = stgChatboxShockEnabled;
      settings.chatboxCooldownEnabled = stgChatboxCooldownEnabled;
      {
        std::lock_guard<std::mutex> lock(hub.queueMutex);
        settings.shockerIDs.clear();
        std::istringstream ss(stgShockerIDs);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
          auto s = tok.find_first_not_of(" \t");
          if (s != std::string::npos)
            settings.shockerIDs.push_back(tok.substr(s));
        }
      }
      settings.save(settingsPath);
    };

    // Settings panel
    if (showSettings) {
      float sPanelW = settingsAnim * 542.f;
      ImGui::SetNextWindowSize({sPanelW, (float)settings.windowH - 40},
                               ImGuiCond_Always);
      ImGui::SetNextWindowPos({statsW + (float)settings.windowW, 0.f},
                              ImGuiCond_Always);
      ImGui::Begin("Settings", &showSettings,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoTitleBar);

      ImGui::BeginChild("##settingsscroll", {0, -40}, false);
      ImGui::TextDisabled("(?) Hover for details | CTRL+Z/Y to Undo/Redo");
      ImGui::Spacing();

      // OSC / Avatar
      ImGui::SeparatorText("OSC / Avatar");

      std::vector<std::string> curveNames;
      curveNames.reserve(settings.curves.size());
      for (int ci = 0; ci < (int)settings.curves.size(); ++ci) {
        curveNames.push_back(settings.curves[ci].name.empty()
                                 ? ("Curve " + std::to_string(ci + 1))
                                 : settings.curves[ci].name);
      }
      std::vector<const char*> curveNamePtrs;
      curveNamePtrs.reserve(curveNames.size());
      for (auto& name : curveNames) curveNamePtrs.push_back(name.c_str());

      for (int i = 0; i < (int)stgParameters.size(); ++i) {
        auto& param = stgParameters[i];
        ImGui::PushID(i);
        char paramNameBuf[128] = {};
        snprintf(paramNameBuf, sizeof(paramNameBuf), "%s", param.name.c_str());
        if (ImGui::InputText("Parameter name", paramNameBuf,
                             sizeof(paramNameBuf)))
          param.name = paramNameBuf;
        if (param.curveIndex < 0) param.curveIndex = 0;
        if (param.curveIndex >= (int)curveNamePtrs.size())
          param.curveIndex = std::max(0, (int)curveNamePtrs.size() - 1);
        if (!curveNamePtrs.empty()) {
          ImGui::Combo("Curve", &param.curveIndex, curveNamePtrs.data(),
                       curveNamePtrs.size());
        } else {
          ImGui::TextDisabled("No curves available. Create a preset first.");
        }

        const char* rangeNames[] = {"Full Curve", "First Half", "Second Half"};
        int rangeIndex = (int)param.range;
        ImGui::Combo("Range", &rangeIndex, rangeNames,
                     IM_ARRAYSIZE(rangeNames));
        param.range = static_cast<CurveRange>(rangeIndex);

        if (ImGui::Button("Delete")) {
          stgParameters.erase(stgParameters.begin() + i);
          ImGui::PopID();
          break;
        }
        ImGui::Separator();
        ImGui::PopID();
      }

      if (ImGui::Button(" + ")) stgParameters.emplace_back();
      ImGui::SetItemTooltip("Add a new OSC parameter mapping");
      ImGui::SameLine();
      ImGui::TextDisabled("Use a unique parameter name.");

      if (stgParameters.empty())
        ImGui::TextDisabled("No parameters configured yet.");
      ImGui::TextDisabled("Changes here require a restart");

      ImGui::Spacing();

      // Hardware
      ImGui::SeparatorText("Hardware");

      // Backend toggle: Openshock | PiShock
      if (!stgUsePishock)
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
      else
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImGui::GetStyle().Colors[ImGuiCol_Button]);
      if (ImGui::Button("OpenShock", {100, 0})) stgUsePishock = false;
      ImGui::PopStyleColor();
      ImGui::SameLine(0, 0);
      if (stgUsePishock)
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
      else
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImGui::GetStyle().Colors[ImGuiCol_Button]);
      if (ImGui::Button("PiShock##hw", {100, 0})) stgUsePishock = true;
      ImGui::PopStyleColor();
      ImGui::SameLine();
      ImGui::Text("Backend/Hub");

      ImGui::Spacing();

      // Connection mode toggle: Serial | API
      if (stgUseSerial)
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
      else
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImGui::GetStyle().Colors[ImGuiCol_Button]);
      if (ImGui::Button("Serial##cm", {100, 0})) stgUseSerial = true;
      ImGui::PopStyleColor();
      ImGui::SameLine(0, 0);
      if (!stgUseSerial)
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
      else
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImGui::GetStyle().Colors[ImGuiCol_Button]);
      if (ImGui::Button("API##cm", {100, 0})) stgUseSerial = false;
      ImGui::PopStyleColor();
      ImGui::SameLine();
      ImGui::Text("Connection Mode");

      ImGui::Spacing();

      if (stgUseSerial) {
        ImGui::InputTextWithHint("Serial Port##s", "(blank = auto)",
                                 stgSerialPort, sizeof(stgSerialPort));
        ImGui::SetItemTooltip("Leave blank to auto-detect");
        ImGui::InputText("Shocker IDs (#, #, ...)##s", stgShockerIDs,
                         sizeof(stgShockerIDs));
        ImGui::SetItemTooltip(
            "Shocker IDs as found on the PiShock or OpenShock website.\n"
            "Separate with comma");
        ImGui::Checkbox("Sequential shocker order (vs Random)",
                        &stgRandomOrSeq);
        ImGui::SetItemTooltip(
            "If using multiple shockers, this option chooses between "
            "randomizing or using them sequentially\n"
            "No for random // Yes for sequential");
      } else {
        if (stgUsePishock) {
          ImGui::InputText("Username##psa", stgPishockUser,
                           sizeof(stgPishockUser));
          ImGui::SetItemTooltip("Your PiShock account username");
          ImGui::InputText("API Key##psk", stgPishockKey, sizeof(stgPishockKey),
                           ImGuiInputTextFlags_Password);
          ImGui::SetItemTooltip(
              "Your PiShock API key (from Account > API Access)");
          ImGui::InputText("Shocker IDs (ID, ID, ...)##ps", stgShockerIDs,
                           sizeof(stgShockerIDs));
          ImGui::SetItemTooltip(
              "Share ID(s) for each shocker, comma-separated.\n"
              "Found on the PiShock website under your shocker.\nIf left "
              "empty, will try to get the IDs automatically");
        } else {
          ImGui::InputText("API Token##ost", stgOpenshockToken,
                           sizeof(stgOpenshockToken),
                           ImGuiInputTextFlags_Password);
          ImGui::SetItemTooltip(
              "Your OpenShock API token.\n"
              "Create one at your OpenShock dashboard under API Tokens.");
          ImGui::InputText("Server URL##oss", stgOpenshockServer,
                           sizeof(stgOpenshockServer));
          ImGui::SetItemTooltip(
              "OpenShock server hostname\n"
              "Default: api.openshock.app");
          ImGui::InputText("Shocker IDs (uuid, uuid, ...)##os", stgShockerIDs,
                           sizeof(stgShockerIDs));
          ImGui::SetItemTooltip(
              "Shocker UUID(s) from your OpenShock dashboard, "
              "comma-separated.\nIf left empty, will find them automatically "
              "using the API\nDO NOT MISTAKE THIS FOR SHOCKER IDs\nThe UUID "
              "is the long string of text and dashes");
        }
        ImGui::Checkbox("Sequential shocker order (vs Random)",
                        &stgRandomOrSeq);
        ImGui::SetItemTooltip(
            "If using multiple shockers, this option chooses between "
            "randomizing or using them sequentially\n"
            "No for random // Yes for sequential");
      }
      ImGui::TextDisabled("Changes here require a restart");
      ImGui::Spacing();

      // Cooldown
      ImGui::SeparatorText("Cooldown");
      ImGui::SliderInt("Base Cooldown (s)##s", &stgBaseCooldown, 1, 15);
      ImGui::SetItemTooltip(
          "Starting cooldown after each shock.\nFormula: Base + Factor * "
          "shocks_in_window");
      ImGui::SliderInt("Max Cooldown (s)##s", &stgMaxCooldown, 1, 30);
      ImGui::SetItemTooltip(
          "Cooldown is capped at this value regardless of shock count.");
      ImGui::SliderFloat("Cooldown Factor##s", &stgCooldownFactor, 0.f, 2.f,
                         "%.2f");
      ImGui::SetItemTooltip(
          "Added to cooldown per shock within the window.\nHigher = longer "
          "cooldown after bursts.");
      ImGui::SliderInt("Cooldown Window (s)##s", &stgCooldownWindow, 5, 120);
      ImGui::SetItemTooltip(
          "How far back to count shocks for the factor.\nShocks older than "
          "this are ignored.");

      ImGui::Spacing();
      ImGui::SeparatorText("Notifications");

#ifdef _WIN32
      ImGui::Checkbox("Enable##notif", &stgNotifEnabled);
      ImGui::SetItemTooltip(
          "Send a VR notification showing shock strength and duration");
      if (stgNotifEnabled) {
        ImGui::SameLine();
        ImGui::TextDisabled("Provider:");
        ImGui::SameLine();

        if (!stgNotifUseOvr)
          ImGui::PushStyleColor(
              ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        else
          ImGui::PushStyleColor(ImGuiCol_Button,
                                ImGui::GetStyle().Colors[ImGuiCol_Button]);

        if (ImGui::Button("XSOverlay##np", {90, 0})) stgNotifUseOvr = false;
        ImGui::PopStyleColor();

        ImGui::SameLine(0, 0);

        if (stgNotifUseOvr)
          ImGui::PushStyleColor(
              ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        else
          ImGui::PushStyleColor(ImGuiCol_Button,
                                ImGui::GetStyle().Colors[ImGuiCol_Button]);

        if (ImGui::Button("OVRToolkit##np", {90, 0})) stgNotifUseOvr = true;
        ImGui::PopStyleColor();
      }
#else
      ImGui::Checkbox("Enable WayVR Notifications##notif", &stgNotifEnabled);
      ImGui::SetItemTooltip(
          "Send a VR notification showing shock strength and duration");

      if (stgNotifEnabled) {
        ImGui::TextDisabled("Example notification:");
        ImGui::TextDisabled("⚡ Shock");
        ImGui::TextDisabled("37%% | 1.3s");
      }

      stgNotifUseOvr = false;
#endif
      ImGui::Spacing();

      ImGui::SeparatorText("Hotkey");
      ImGui::TextDisabled("Panic button:");
      ImGui::SameLine();
      std::string keyLabel =
          capturingHotkey
              ? "Press any key..."
              : (settings.hotkeyVk ? formatKeyNameFromVk(settings.hotkeyVk,
                                                         settings.hotkeyMods)
                                   : "None");
      if (ImGui::Button(keyLabel.c_str(), {160, 0})) {
        capturingHotkey = true;
#ifndef _WIN32
        unregisterGlobalHotkeyLinux();
#endif
      }
      ImGui::SetItemTooltip("Hotkey to disable shocks\nWorks anywhere");
      ImGui::SetItemTooltip("Disables shocks.");
      ImGui::SameLine();
      if (ImGui::Button("Clear##hk")) {
        settings.hotkeyVk = 0;
        settings.hotkeyMods = 0;
      }
      ImGui::SetItemTooltip("Clear the button (disables hotkey)");

      if (capturingHotkey) {
        int mods = 0;
        if (glfwGetKey(g_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
            glfwGetKey(g_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
          mods |= 2;
        if (glfwGetKey(g_window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
            glfwGetKey(g_window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS)
          mods |= 1;
        if (glfwGetKey(g_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
            glfwGetKey(g_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
          mods |= 4;

        bool captured = false;
        auto tryKey = [&](int k) {
          if (!captured && glfwGetKey(g_window, k) == GLFW_PRESS) {
            settings.hotkeyVk = k;
            settings.hotkeyMods = mods;
            capturingHotkey = false;
            captured = true;
#ifndef _WIN32
            registerGlobalHotkey(k, mods);
#endif
          }
        };
        for (int k = GLFW_KEY_F1; k <= GLFW_KEY_F25; k++) tryKey(k);
        for (int k = GLFW_KEY_0; k <= GLFW_KEY_9; k++) tryKey(k);
        for (int k = GLFW_KEY_A; k <= GLFW_KEY_Z; k++) tryKey(k);
        if (glfwGetKey(g_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
          capturingHotkey = false;
      }
      ImGui::Spacing();

      // Style
      ImGui::SeparatorText("Style (live preview)");
      ImGui::SliderInt("Preset Count*##s", &stgPresetCount, 1, 8);
      ImGui::SetItemTooltip("Amount of presets");
      ImGui::SliderFloat("Marker Size##s", &settings.touchMarkerSize, 50.f,
                         300.f, "%.0f");
      ImGui::SetItemTooltip("Size of points in the curve");
      ImGui::SliderFloat("Curve Line Width##s", &settings.lineWidth, 1.f, 6.f,
                         "%.1f");
      ImGui::SetItemTooltip("Width of the curve line");

      auto liftMinLum = [](ImVec4 c, float minLum) {
        float lum = c.x * 0.299f + c.y * 0.587f + c.z * 0.114f;
        if (lum < minLum) {
          float sc = minLum / (lum + 1e-5f);
          c.x *= sc;
          c.y *= sc;
          c.z *= sc;
        }
        return ImVec4(std::min(c.x, 1.f), std::min(c.y, 1.f),
                      std::min(c.z, 1.f), c.w);
      };
      ImVec4 base = liftMinLum(settings.accentColor, 0.35f);
      ImGui::PushStyleColor(
          ImGuiCol_FrameBg,
          ImVec4(base.x * 1.25f, base.y * 1.25f, base.z * 1.25f, 1.0f));

      ImGui::ColorEdit4("Background##s", (float*)&settings.backgroundColor,
                        ImGuiColorEditFlags_NoInputs);
      ImGui::SetItemTooltip("Main window background color.");
      ImGui::ColorEdit4("Outside Curve BG##s", (float*)&settings.outsideCurveBg,
                        ImGuiColorEditFlags_NoInputs);
      ImGui::SetItemTooltip("Area outside of the curve/plot UI.");
      ImGui::ColorEdit4("Accent##s", (float*)&settings.accentColor,
                        ImGuiColorEditFlags_NoInputs);
      ImGui::SetItemTooltip("Buttons, sliders, checkboxes, input fields.");
      ImGui::ColorEdit4("Curve Line##s", (float*)&settings.curveLineColor,
                        ImGuiColorEditFlags_NoInputs);
      ImGui::SetItemTooltip("The bezier curve line.");
      ImGui::ColorEdit4("Markers##s", (float*)&settings.markerColor,
                        ImGuiColorEditFlags_NoInputs);
      ImGui::SetItemTooltip("The draggable curve control points.");
      ImGui::ColorEdit4("Labels##s", (float*)&settings.labelColor,
                        ImGuiColorEditFlags_NoInputs);
      ImGui::SetItemTooltip("All text labels and axis text.");
      ImGui::ColorEdit4("Gradient Left##s", (float*)&settings.gradientLeftColor,
                        ImGuiColorEditFlags_NoInputs);
      ImGui::SetItemTooltip(
          "Plot background gradient - left/low intensity side.");
      ImGui::ColorEdit4("Gradient Right##s",
                        (float*)&settings.gradientRightColor,
                        ImGuiColorEditFlags_NoInputs);
      ImGui::SetItemTooltip(
          "Plot background gradient - right/high intensity side.");

      ImGui::TextDisabled("* - Restart required");

      ImGui::PopStyleColor(1);

      ImGui::Spacing();

      // VRChat
      ImGui::SeparatorText("VRChat");
      ImGui::Checkbox("Send shocks to ChatBox", &stgChatboxShockEnabled);
      ImGui::Checkbox("Send cooldowns to ChatBox", &stgChatboxCooldownEnabled);

      ImGui::Spacing();

      ImGui::InputText("VRChat Host##s", stgVrchatHost, sizeof(stgVrchatHost));
      ImGui::SetItemTooltip("Usually doesn't need a change.");

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      static char importPathBuf[512] = {};
      ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - 135.f);
      ImGui::InputTextWithHint("##importpath", "/path/to/python/shocker-link",
                               importPathBuf, sizeof(importPathBuf));

      ImGui::SameLine();
      if (ImGui::Button("Import Python cfg"))
        importLegacyPythonConfig(settings, hub, minDur, maxDur, xViewMin,
                                 xViewMax, settingsPath, importPathBuf);
      ImGui::SetItemTooltip("Point at your old Python ShockerLink folder");
      ImGui::Spacing();

      ImGui::EndChild();

      ImGui::Separator();
      std::string currentIDs;
      for (int i = 0; i < (int)settings.shockerIDs.size(); i++)
        currentIDs += (i ? ", " : "") + settings.shockerIDs[i];
      bool needsRestart = settings.parameters != stgParameters ||
                          settings.serialPort != stgSerialPort ||
                          settings.usePishock != stgUsePishock ||
                          settings.randomOrSeq != stgRandomOrSeq ||
                          settings.vrchatHost != stgVrchatHost ||
                          settings.presetCount != stgPresetCount ||
                          currentIDs != stgShockerIDs ||
                          settings.useSerial != stgUseSerial ||
                          settings.pishockUsername != stgPishockUser ||
                          settings.pishockApiKey != stgPishockKey ||
                          settings.openshockApiToken != stgOpenshockToken ||
                          settings.openshockServerUrl != stgOpenshockServer;

      if (needsRestart) {
        if (ImGui::Button("Save & Restart", {150, 0})) {
          commitAll();
          registerPanicHotkey(settings);
          shouldRestart = true;
          glfwSetWindowShouldClose(g_window, 1);
        }
      } else {
        if (ImGui::Button("Save", {80, 0})) {
          commitAll();
          registerPanicHotkey(settings);
          showSettings = false;
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", {80, 0})) closeSettingsModal();
      ImGui::SetItemTooltip(
          "Closes settings without saving\nTheme settings will be reverted");
      ImGui::End();
    }

    // Stats panel (slides out left)
    if (statsAnim > 0.43f) {
      ImGui::SetNextWindowPos({0.f, 0.f}, ImGuiCond_Always);
      ImGui::SetNextWindowSize({statsW, (float)settings.windowH},
                               ImGuiCond_Always);
      ImGui::Begin("##statspanel", nullptr,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoTitleBar |
                       ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoScrollWithMouse);

      float alpha = std::min(1.f, std::max(0.f, (statsW - 60.f) / 160.f));
      ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

      if (boldFont) ImGui::PushFont(boldFont);
      ImGui::Text("Statistics");
      if (boldFont) ImGui::PopFont();
      ImGui::SameLine(statsW - 36.f);
      if (ImGui::SmallButton("X##sc")) settings.showStats = false;
      ImGui::Separator();

      ImGui::BeginChild("##statsscroll",
                        {0, -ImGui::GetFrameHeightWithSpacing() - 6}, false);

      auto row = [&](const char* label, const std::string& val) {
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine(112.f);
        ImGui::TextUnformatted(val.c_str());
      };

      auto fmtMs = [](double ms) -> std::string {
        int ts = (int)(ms / 1000.0);
        if (ts < 60) return fmt::format("{:.1f}s", ms / 1000.0);
        int m = ts / 60, s = ts % 60;
        if (m < 60) return fmt::format("{}m {:02d}s", m, s);
        int h = m / 60;
        m %= 60;
        return fmt::format("{}h {:02d}m", h, m);
      };

      ImGui::SeparatorText("All-Time");
      row("Shocks", std::to_string(gStats.totalShocks));
      row("Vibrations", std::to_string(gStats.totalVibrations));
      row("Shock time", fmtMs(gStats.totalShockDurationMs));
      if (gStats.totalShocks > 0) {
        row("Avg intensity", fmt::format("{:.0f}%", gStats.averageIntensity()));
        row("Peak intensity", fmt::format("{}%", gStats.highestIntensity));
        row("Longest shock",
            fmt::format("{:.1f}s", gStats.longestShockMs / 1000.0));
      }
      row("Cooldown blocks", std::to_string(gStats.totalCooldownHits));

      ImGui::Spacing();
      ImGui::SeparatorText("This Session");
      row("Shocks", std::to_string(gStats.sessionShocks));
      row("Vibrations", std::to_string(gStats.sessionVibrations));
      row("Shock time", fmtMs(gStats.sessionShockDurationMs));
      row("Cooldown blocks", std::to_string(gStats.sessionCooldownHits));

      ImGui::Spacing();
      ImGui::SeparatorText("Records");
      {
        auto [bestDay, bestCnt] = gStats.mostShockedDay();
        ImGui::TextDisabled("Best day:");
        if (bestCnt > 0) {
          ImGui::SameLine();
          ImGui::Text("%s", bestDay.c_str());
          ImGui::SameLine();
          ImGui::Text("%d shock%s", bestCnt, bestCnt == 1 ? "" : "s");
        } else {
          ImGui::SameLine();
          ImGui::TextDisabled("None yet");
        }
        int tdc = gStats.todayCount();
        ImGui::TextDisabled("Today:");
        ImGui::SameLine();
        ImGui::Text("%d shock%s", tdc, tdc == 1 ? "" : "s");
      }

      ImGui::Spacing();
      ImGui::SeparatorText("Last 7 Days");
      {
        auto days = gStats.lastNDays(7);
        double vals[7] = {};
        if (ImPlot::BeginPlot("##7d", {-1, 110},
                              ImPlotFlags_NoTitle | ImPlotFlags_NoLegend |
                                  ImPlotFlags_NoMouseText)) {
          ImPlot::SetupAxes(
              nullptr, nullptr,
              ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoLabel,
              ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_AutoFit |
                  ImPlotAxisFlags_NoLabel);
          ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, 6.5, ImPlotCond_Always);
          ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0.0, DBL_MAX);

          static std::string dayStrings[7];
          const char* dayLabels[7] = {};
          for (int i = 0; i < 7; i++) {
            vals[i] = (double)days[i].second;
            dayStrings[i] = days[i].first.size() >= 10
                                ? days[i].first.substr(8, 2)
                                : days[i].first;
            dayLabels[i] = dayStrings[i].c_str();
          }
          double dayPositions[7] = {0, 1, 2, 3, 4, 5, 6};
          ImPlot::SetupAxisTicks(ImAxis_X1, dayPositions, 7, dayLabels);

          static std::vector<double> yticks;
          yticks.clear();
          double maxv = *std::max_element(vals, vals + 7);
          int max_i = (int)std::ceil(maxv);
          int step = max_i <= 4 ? 1 : (int)std::ceil(max_i / 4.0);
          for (int i = 0; i <= max_i; i += step) yticks.push_back((double)i);
          if (yticks.back() < max_i) yticks.push_back((double)max_i);
          ImPlot::SetupAxisTicks(ImAxis_Y1, yticks.data(), yticks.size());
          ImPlot::SetNextFillStyle(settings.curveLineColor, 0.85f);
          ImPlot::PlotBars("##bars", vals, 7, 0.6);
          ImPlot::EndPlot();
          ImGui::TextDisabled("%s", days[0].first.substr(0, 7).c_str());
        }
      }

      ImGui::Separator();
      if (ImGui::Button("Reset Stats", {-1, 0}))
        ImGui::OpenPopup("Confirm Reset##sr");
      if (ImGui::BeginPopupModal("Confirm Reset##sr", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete all recorded statistics?");
        ImGui::Spacing();
        if (ImGui::Button("Reset", {80, 0})) {
          gStats.reset();
          gStats.save("stats.json");
          ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {80, 0})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }

      ImGui::EndChild();
      ImGui::PopStyleVar();  // Alpha
      ImGui::End();
    }

    // Ctrl+Z / Ctrl+Y
    const bool editingText = io.WantTextInput;
    bool ctrlDown = glfwGetKey(g_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                    glfwGetKey(g_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    bool zDown = glfwGetKey(g_window, GLFW_KEY_Z) == GLFW_PRESS;
    bool yDown = glfwGetKey(g_window, GLFW_KEY_Y) == GLFW_PRESS;
    bool didUndoRedo = false;

    if (!editingText) {
      if (ctrlDown && zDown && !ctrlZPrev) {
        performUndoRedo(true, undoStack, redoStack, ui, isPerformingUndoRedo);
        didUndoRedo = true;
      }
      if (ctrlDown && yDown && !ctrlYPrev) {
        performUndoRedo(false, undoStack, redoStack, ui, isPerformingUndoRedo);
        didUndoRedo = true;
      }
    }
    ctrlZPrev = ctrlDown && zDown;
    ctrlYPrev = ctrlDown && yDown;

    AppState currentState = snapshotAppState(ui);
    bool isEditingThisFrame = ImGui::IsAnyItemActive() || io.WantTextInput;
    if (!didUndoRedo) {
      if (isEditingThisFrame && !stateChangedPreviousFrame)
        pushUndoSnapshot(undoStack, redoStack, lastCommittedState, false);
      if (isEditingThisFrame || stateChangedPreviousFrame)
        lastCommittedState = snapshotAppState(ui);
    } else {
      lastCommittedState = snapshotAppState(ui);
    }
    stateChangedPreviousFrame = isEditingThisFrame;

    // Render
    ImGui::Render();
    int fb_w, fb_h;
    glfwGetFramebufferSize(g_window, &fb_w, &fb_h);
    glViewport(0, 0, fb_w, fb_h);
    glClearColor(clear.x * clear.w, clear.y * clear.w, clear.z * clear.w,
                 clear.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(g_window);
    forceFrame = false;
  }

  // Cleanup
  running = false;
  g_wakeUiFunc = nullptr;

  hub.shutdown();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  settings.minShockDuration = minDur;
  // Save current curve state before exiting
  if (currentCurveIndex >= 0 &&
      currentCurveIndex < (int)settings.curves.size()) {
    settings.curves[currentCurveIndex].curvePoints = hub.curvePoints;
    settings.curves[currentCurveIndex].xViewMin = xViewMin;
    settings.curves[currentCurveIndex].xViewMax = xViewMax;
    settings.curves[currentCurveIndex].minShockDuration = minDur;
    settings.curves[currentCurveIndex].maxShockDuration = maxDur;
  }

  settings.maxShockDuration = maxDur;
  settings.xViewMin = xViewMin;
  settings.xViewMax = xViewMax;

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();

#ifndef _WIN32
  g_hotkeyThreadRunning = false;
  unregisterGlobalHotkeyLinux();
  if (g_hotkeyThread.joinable()) g_hotkeyThread.join();
#endif

  glfwDestroyWindow(g_window);
  glfwTerminate();
}