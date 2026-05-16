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
#include "icon_png.h"

static bool setWindowIcon(GLFWwindow* window) {
  png_image image;
  memset(&image, 0, sizeof(image));
  image.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_memory(&image, kIconPngData, kIconPngSize))
    return false;
  image.format = PNG_FORMAT_RGBA;
  std::vector<uint8_t> pixels(PNG_IMAGE_SIZE(image));
  if (!png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr))
    return false;
  GLFWimage icon{(int)image.width, (int)image.height, pixels.data()};
  glfwSetWindowIcon(window, 1, &icon);
  return true;
}
#endif

static GLFWwindow* g_window = nullptr;
static ShockerHub* g_hub = nullptr;
static Settings* g_settingsForHotkey = nullptr;
static std::atomic<bool> g_pendingClose{false};

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
       chatboxCooldownEnabled = true, stgManualScaling = false;
  int stgBaseCooldown = 2, stgMaxCooldown = 6, stgCooldownWindow = 30,
      stgPresetCount = 3;
  float stgCooldownFactor = 0.4f, stgTouchThreshold = 8.f,
        stgManualUiScale = 1.00f;
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
           stgParameters == o.stgParameters &&
           stgManualScaling == o.stgManualScaling &&
           stgManualUiScale == o.stgManualUiScale;
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
  bool& stgManualScaling;
  float& stgManualUiScale;
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
  st.stgManualScaling = ui.stgManualScaling;
  st.stgManualUiScale = ui.stgManualUiScale;
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
  ui.stgManualScaling = st.stgManualScaling;
  ui.stgManualUiScale = st.stgManualUiScale;
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

// Floppy disk save icon button
static bool drawSaveIconButton(const char* id) {
  ImVec2 size(18, 18);
  ImGui::InvisibleButton(id, size);
  bool clicked = ImGui::IsItemClicked();
  bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetItemRectMin();
  // Slightly larger hover area glow
  if (hovered)
    dl->AddRectFilled({p.x - 2, p.y - 2}, {p.x + 20, p.y + 20},
                      IM_COL32(255, 255, 255, 18), 4.f);
  ImU32 col = hovered ? IM_COL32(255, 255, 255, 255)
                      : ImGui::ColorConvertFloat4ToU32(
                            ImGui::GetStyle().Colors[ImGuiCol_Text]);
  ImU32 bg = ImGui::ColorConvertFloat4ToU32(
      ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);
  dl->AddRectFilled(p, {p.x + 15, p.y + 15}, col, 2.f);
  dl->AddRectFilled({p.x + 2, p.y + 7}, {p.x + 13, p.y + 14}, bg);
  dl->AddRectFilled({p.x + 4, p.y + 1}, {p.x + 11, p.y + 6}, bg);
  dl->AddRectFilled({p.x + 6, p.y + 2}, {p.x + 9, p.y + 6}, col);
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
  dl->AddRectFilled({trackX0, trackY - 3.5f}, {trackX1, trackY + 3.5f},
                    trackCol, 3);
  dl->AddRectFilled({hMinPos.x, trackY - 3}, {hMaxPos.x, trackY + 3}, fillCol,
                    3);
  dl->AddCircleFilled(hMinPos, hRadius,
                      (dragging == 1 || hovMin) ? grabActCol : grabCol, 16);
  dl->AddCircleFilled(hMaxPos, hRadius,
                      (dragging == 2 || hovMax) ? grabActCol : grabCol, 16);
  return changed;
}

// Theme
inline void applyUiTheme(Settings& settings, float uiScale = 1.f) {
  ImGuiStyle& style = ImGui::GetStyle();

  style.TabBarBorderSize = 0.f;
  style.WindowBorderSize = 1.f;
  style.ChildBorderSize = 1.f;
  style.FrameBorderSize = 0.f;
  style.PopupBorderSize = 1.f;

  style.WindowRounding = 10.f * uiScale;
  style.ChildRounding = 8.f * uiScale;
  style.FrameRounding = 6.f * uiScale;
  style.PopupRounding = 8.f * uiScale;
  style.ScrollbarRounding = 8.f * uiScale;
  style.GrabRounding = 6.f * uiScale;
  style.TabRounding = 6.f * uiScale;
  style.WindowPadding = {12.f * uiScale, 12.f * uiScale};
  style.FramePadding = {10.f * uiScale, 6.f * uiScale};
  style.CellPadding = {8.f * uiScale, 5.f * uiScale};
  style.ItemSpacing = {8.f * uiScale, 8.f * uiScale};
  style.ItemInnerSpacing = {6.f * uiScale, 4.f * uiScale};
  style.ScrollbarSize = 8.f * uiScale;
  style.GrabMinSize = 10.f * uiScale;
  style.IndentSpacing = 18.f * uiScale;
  style.SeparatorTextBorderSize = 2.f * uiScale;
  style.SeparatorTextPadding = {8.f * uiScale, 3.f * uiScale};

  // Color helpers
  auto mul = [](ImVec4 c, float s) -> ImVec4 {
    return {std::min(c.x * s, 1.f), std::min(c.y * s, 1.f),
            std::min(c.z * s, 1.f), c.w};
  };
  auto withA = [](ImVec4 c, float a) -> ImVec4 { return {c.x, c.y, c.z, a}; };
  auto lerp4 = [](ImVec4 a, ImVec4 b, float t) -> ImVec4 {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t};
  };

  ImVec4 bg = settings.backgroundColor;
  ImVec4 txt = settings.labelColor;
  ImVec4 a = settings.accentColor;

  // Ensure accent is usable
  float lum = a.x * 0.299f + a.y * 0.587f + a.z * 0.114f;
  if (lum < 0.15f) {
    float sc = 0.15f / (lum + 1e-5f);
    a = {std::min(a.x * sc, 1.f), std::min(a.y * sc, 1.f),
         std::min(a.z * sc, 1.f), a.w};
  }

  ImVec4 aH = mul(a, 1.25f);   // hover
  ImVec4 aA = mul(a, 0.80f);   // active / pressed
  ImVec4 aD = mul(a, 0.38f);   // dim (idle state)
  ImVec4 aDH = mul(a, 0.58f);  // dim hover

  // Border: subtle blue-grey tint
  ImVec4 border = {std::min(bg.x + 0.12f, 1.f), std::min(bg.y + 0.12f, 1.f),
                   std::min(bg.z + 0.18f, 1.f), 0.50f};

  // Derived backgrounds
  ImVec4 bgD = mul(bg, 0.72f);  // title bars, menu bar
  ImVec4 bgP =
      lerp4(bg, ImVec4(bg.x, bg.y, std::min(bg.z + 0.05f, 1.f), 1.f), 0.3f);
  bgP.w = 0.97f;  // popups

  // Frame bg: ensure always readable
  ImVec4 frameBgBase = lerp4(bg, a, 0.30f);
  float fbLum =
      frameBgBase.x * 0.299f + frameBgBase.y * 0.587f + frameBgBase.z * 0.114f;
  if (fbLum < 0.12f) {
    float sc = 0.12f / (fbLum + 1e-5f);
    frameBgBase = {std::min(frameBgBase.x * sc, 1.f),
                   std::min(frameBgBase.y * sc, 1.f),
                   std::min(frameBgBase.z * sc, 1.f), frameBgBase.w};
  }

  auto& c = style.Colors;

  c[ImGuiCol_WindowBg] = bg;
  c[ImGuiCol_ChildBg] = withA(bg, 0.f);  // transparent children by default
  c[ImGuiCol_PopupBg] = bgP;
  c[ImGuiCol_Border] = border;
  c[ImGuiCol_BorderShadow] = {0, 0, 0, 0};

  c[ImGuiCol_Text] = txt;
  c[ImGuiCol_TextDisabled] = withA(txt, 0.42f);
  c[ImGuiCol_TextSelectedBg] = withA(a, 0.38f);

  c[ImGuiCol_TitleBg] = bgD;
  c[ImGuiCol_TitleBgActive] = mul(bgD, 0.88f);
  c[ImGuiCol_TitleBgCollapsed] = withA(bgD, 0.80f);
  c[ImGuiCol_MenuBarBg] = bgD;

  c[ImGuiCol_FrameBg] = withA(frameBgBase, 0.75f);
  c[ImGuiCol_FrameBgHovered] = withA(mul(frameBgBase, 1.35f), 0.85f);
  c[ImGuiCol_FrameBgActive] = withA(aA, 0.65f);

  c[ImGuiCol_Button] = withA(aD, 0.90f);
  c[ImGuiCol_ButtonHovered] = withA(a, 0.92f);
  c[ImGuiCol_ButtonActive] = aA;

  c[ImGuiCol_SliderGrab] = a;
  c[ImGuiCol_SliderGrabActive] = aH;
  c[ImGuiCol_CheckMark] = aH;

  c[ImGuiCol_Header] = withA(aA, 0.55f);
  c[ImGuiCol_HeaderHovered] = withA(a, 0.78f);
  c[ImGuiCol_HeaderActive] = a;

  c[ImGuiCol_Separator] = border;
  c[ImGuiCol_SeparatorHovered] = withA(a, 0.70f);
  c[ImGuiCol_SeparatorActive] = a;

  c[ImGuiCol_ResizeGrip] = withA(aD, 0.25f);
  c[ImGuiCol_ResizeGripHovered] = withA(a, 0.50f);
  c[ImGuiCol_ResizeGripActive] = a;

  // Tabs - active tab is clearly distinguished
  c[ImGuiCol_Tab] = withA(lerp4(bgD, aD, 0.5f), 0.95f);
  c[ImGuiCol_TabHovered] = withA(a, 0.85f);
  c[ImGuiCol_TabActive] = withA(lerp4(aD, a, 0.6f), 1.f);
  c[ImGuiCol_TabUnfocused] = withA(bgD, 0.95f);
  c[ImGuiCol_TabUnfocusedActive] = withA(aD, 0.95f);

  c[ImGuiCol_ScrollbarBg] = withA(bgD, 0.40f);
  c[ImGuiCol_ScrollbarGrab] = withA(aD, 0.80f);
  c[ImGuiCol_ScrollbarGrabHovered] = withA(a, 0.70f);
  c[ImGuiCol_ScrollbarGrabActive] = a;

  c[ImGuiCol_PlotLines] = txt;
  c[ImGuiCol_PlotHistogram] = a;

  c[ImGuiCol_TableHeaderBg] = withA(aD, 0.55f);
  c[ImGuiCol_TableBorderStrong] = border;
  c[ImGuiCol_TableBorderLight] = withA(border, 0.40f);
  c[ImGuiCol_TableRowBg] = withA(bg, 0.f);
  c[ImGuiCol_TableRowBgAlt] = withA(frameBgBase, 0.15f);

  c[ImGuiCol_ModalWindowDimBg] = {0.f, 0.f, 0.f, 0.60f};
  c[ImGuiCol_NavHighlight] = a;
  c[ImGuiCol_DragDropTarget] = a;

  // ImPlot
  auto& ip = ImPlot::GetStyle().Colors;
  ImVec4& bgColor = settings.outsideCurveBg;
  ip[ImPlotCol_FrameBg] = bgColor;
  ip[ImPlotCol_PlotBg] = bgColor;
  ip[ImPlotCol_AxisText] = settings.labelColor;
  ip[ImPlotCol_LegendText] = settings.labelColor;
  ip[ImPlotCol_LegendBg] = withA(bgColor, 0.90f);
  ip[ImPlotCol_LegendBorder] = border;
  ip[ImPlotCol_PlotBorder] = border;
  ImPlot::GetStyle().LegendPadding = ImVec2(10, 8);
  ImPlot::GetStyle().LegendInnerPadding = ImVec2(6, 4);
  ImPlot::GetStyle().LegendSpacing = ImVec2(6, 4);
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

// Helper: small colored pill/badge
static void drawBadge(ImDrawList* dl, ImVec2 pos, const char* text, ImU32 bgCol,
                      ImU32 textCol) {
  ImVec2 ts = ImGui::CalcTextSize(text);
  float padX = 6.f, padY = 2.f;
  ImVec2 bMin = {pos.x, pos.y};
  ImVec2 bMax = {pos.x + ts.x + padX * 2, pos.y + ts.y + padY * 2};
  dl->AddRectFilled(bMin, bMax, bgCol, 4.f);
  dl->AddText({bMin.x + padX, bMin.y + padY}, textCol, text);
}

// UI entry point
inline void runUI(Settings& settings, ShockerHub& hub,
                  const std::string& settingsPath) {
  extern std::atomic<bool> running;

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

#ifdef _WIN32
  {
    HWND hwnd = glfwGetWin32Window(g_window);
    HICON hIcon = LoadIconA(GetModuleHandle(nullptr), MAKEINTRESOURCEA(1));
    if (hIcon) {
      SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
      SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }
  }
#else
  if (!setWindowIcon(g_window)) {
    logMsg("[UI] Failed to load window icon");
  }
#endif

#ifdef _WIN32
  glfwSetWindowPos(g_window, settings.windowX, settings.windowY);
#endif
  glfwMakeContextCurrent(g_window);
  glfwSwapInterval(1);

  g_hub = &hub;
  g_settingsForHotkey = &settings;
  registerPanicHotkey(settings);
  g_wakeUiFunc = [] { glfwPostEmptyEvent(); };

  glfwSetWindowCloseCallback(g_window, [](GLFWwindow* win) {
    glfwSetWindowShouldClose(win, GLFW_FALSE);
    g_pendingClose = true;
    if (g_wakeUiFunc) g_wakeUiFunc();
  });

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;

#ifdef _WIN32
  ImFont* boldFont = nullptr;
  {
    const char* regularPaths[] = {"C:\\Windows\\Fonts\\segoeui.ttf",
                                  "C:\\Windows\\Fonts\\arial.ttf",
                                  "C:\\Windows\\Fonts\\calibri.ttf", nullptr};
    const char* boldPaths[] = {"C:\\Windows\\Fonts\\segoeuib.ttf",
                               "C:\\Windows\\Fonts\\arialbd.ttf",
                               "C:\\Windows\\Fonts\\calibrib.ttf", nullptr};
    const char* symPaths[] = {"C:\\Windows\\Fonts\\seguiemj.ttf",
                              "C:\\Windows\\Fonts\\seguisym.ttf", nullptr};
    for (auto p = regularPaths; *p; ++p) {
      if (std::filesystem::exists(*p)) {
        io.Fonts->AddFontFromFileTTF(*p, 18.0f);
        break;
      }
    }
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();
    for (auto p = symPaths; *p; ++p) {
      if (std::filesystem::exists(*p)) {
        ImFontConfig cfg;
        cfg.MergeMode = true;
        static const ImWchar ranges[] = {0x2600, 0x27FF, 0};
        cfg.GlyphOffset = {0, 2.f};
        cfg.GlyphMinAdvanceX = 18.f;
        io.Fonts->AddFontFromFileTTF(*p, 14.0f, &cfg, ranges);
        break;
      }
    }
    for (auto p = boldPaths; *p; ++p) {
      if (std::filesystem::exists(*p)) {
        boldFont = io.Fonts->AddFontFromFileTTF(*p, 18.0f);
        break;
      }
    }
    if (!boldFont) boldFont = io.Fonts->Fonts.back();
  }
#else
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
    for (auto p = symPaths; *p; ++p) {
      if (std::filesystem::exists(*p)) {
        ImFontConfig cfg;
        cfg.MergeMode = true;
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
#endif

  ImGui::StyleColorsDark();
  applyUiTheme(settings);

  ImGui_ImplGlfw_InitForOpenGL(g_window, true);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  float minDur = settings.minShockDuration;
  float maxDur = settings.maxShockDuration;
  bool cooldownEnabled = settings.cooldownEnabled;
  float xViewMin = settings.xViewMin;
  float xViewMax = settings.xViewMax;

  int currentCurveIndex = 0;
  int loadedPresetIndex = -1;
  std::vector<Preset> loadedPresetSnapshot;
  int pendingLoadPresetIndex = -1;

  auto flushCurrentCurve = [&]() {
    if (currentCurveIndex >= 0 &&
        currentCurveIndex < (int)settings.curves.size()) {
      settings.curves[currentCurveIndex].curvePoints = hub.curvePoints;
      settings.curves[currentCurveIndex].xViewMin = xViewMin;
      settings.curves[currentCurveIndex].xViewMax = xViewMax;
      settings.curves[currentCurveIndex].minShockDuration = minDur;
      settings.curves[currentCurveIndex].maxShockDuration = maxDur;
    }
  };

  auto isLoadedPresetDirty = [&]() -> bool {
    if (loadedPresetIndex < 0 || loadedPresetSnapshot.empty()) return false;
    flushCurrentCurve();
    if (settings.curves.size() != loadedPresetSnapshot.size()) return true;
    for (int i = 0; i < (int)settings.curves.size(); i++)
      if (!(settings.curves[i] == loadedPresetSnapshot[i])) return true;
    return false;
  };

  auto commitLoadedPresetSnapshot = [&]() {
    flushCurrentCurve();
    loadedPresetSnapshot = settings.curves;
  };

  std::deque<AppState> undoStack, redoStack;
  bool isPerformingUndoRedo = false;
  bool ctrlZPrev = false, ctrlYPrev = false, ctrlSPrev = false,
       ctrlPlusPrev = false, ctrlMinusPrev = false;
  bool stateChangedPreviousFrame = false;

  // Load default preset
  if (settings.defaultPreset >= 0 &&
      settings.defaultPreset < (int)settings.presets.size() &&
      settings.presets[settings.defaultPreset].has_value()) {
    auto& dp = settings.presets[settings.defaultPreset];
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
    loadedPresetIndex = settings.defaultPreset;
    commitLoadedPresetSnapshot();
  }

  bool showSettings = false;
  float settingsAnim = 0.f, statsAnim = 0.f;
  if (settings.showStats) statsAnim = 1.f;

  // Initial window positionin
  {
    float kSM_ =
        std::max(180.f, std::min(280.f, (float)settings.windowW * 0.32f));
    float kSetM_ =
        std::max(320.f, std::min(556.f, (float)settings.windowW * 0.63f));
    int sw = (int)roundf(statsAnim * kSM_);
    int settW = (int)roundf(settingsAnim * kSetM_);
    glfwSetWindowPos(g_window, settings.windowX - sw, settings.windowY);
    glfwSetWindowSize(g_window, settings.windowW + sw + settW,
                      settings.windowH);
  }

  char stgShockParam[64] = {}, stgSecondParam[64] = {}, stgShockerIDs[256] = {},
       stgSerialPort[64] = {}, stgVrchatHost[64] = {};
  bool stgUsePishock = false, stgRandomOrSeq = false, stgNotifEnabled = false,
       stgNotifUseOvr = false, stgUseSerial = true,
       stgChatboxShockEnabled = true, stgChatboxCooldownEnabled = true,
       stgManualScaling = false, origManualScaling = false;
  int stgBaseCooldown = 2, stgMaxCooldown = 6, stgCooldownWindow = 30,
      stgPresetCount = 3;
  float stgCooldownFactor = 0.4f, stgTouchThreshold = 8.f,
        stgManualUiScale = 1.00f, origManualUiScale = 1.00f;
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
               stgParameters,
               stgManualScaling,
               stgManualUiScale};

  AppState lastCommittedState = snapshotAppState(ui);

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
    stgManualScaling = settings.manualScaling;
    stgManualUiScale = settings.manualUiScale;
    origManualScaling = settings.manualScaling;
    origManualUiScale = settings.manualUiScale;
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
    settings.manualScaling = origManualScaling;
    settings.manualUiScale = origManualUiScale;
    stgManualScaling = origManualScaling;
    stgManualUiScale = origManualUiScale;
    showSettings = false;
  };

  bool capturingHotkey = false;
  bool panicWasPressedLastFrame = false;

  std::array<CurvePoint, 3>& pts = hub.curvePoints;
  struct CurveCache {
    std::array<CurvePoint, 3> lastPts{};
    std::vector<double> cx, cy;
  };
  std::vector<CurveCache> curveCache(settings.curves.size());

  ImVec4& clear = settings.backgroundColor;
  bool forceFrame = true;
  auto lastAnimTime = steady_clock::now();

  // Main loop
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

    if (forceFrame)
      glfwPollEvents();
    else if (!needsAnimation)
      glfwWaitEvents();
    else {
      double targetInterval = focused ? (1.0 / 60.0) : (1.0 / 16.0);
      glfwWaitEventsTimeout(targetInterval);
    }

    // Window position tracking
    {
      int ww, wh, wx, wy;
      glfwGetWindowPos(g_window, &wx, &wy);
      glfwGetWindowSize(g_window, &ww, &wh);
      if (!statsAnimating && !settAnimating) {
        float W = (float)settings.windowW;
        for (int iter = 0; iter < 3; iter++) {
          float sm = statsAnim * std::max(180.f, std::min(280.f, W * 0.32f));
          float stm =
              settingsAnim * std::max(320.f, std::min(556.f, W * 0.63f));
          W = (float)ww - sm - stm;
        }
        int sw_i = (int)roundf(statsAnim *
                               std::max(180.f, std::min(280.f, W * 0.32f)));
        int settW_i = (int)roundf(settingsAnim *
                                  std::max(320.f, std::min(556.f, W * 0.63f)));
        settings.windowX = wx + sw_i;
        settings.windowW = ww - sw_i - settW_i;
      }
      settings.windowY = wy;
      settings.windowH = wh;
    }
    // Per-frame dynamic panel sizing — derived from the actual content width.
    // Baseline: windowW=903 → stats=280 (0.32×903≈289, capped 280), sett=556
    // (0.63×903≈569, capped 556). Shrinks proportionally for smaller windows,
    // with sensible minimums.
    const float kStatsMaxW =
        std::max(180.f, std::min(280.f, (float)settings.windowW * 0.32f));
    const float kSettMaxW =
        std::max(320.f, std::min(556.f, (float)settings.windowW * 0.63f));

    int lastSetWindowX = INT_MIN, lastSetWindowW = INT_MIN,
        lastSetWindowH = INT_MIN;

    // Panel slide animations
    {
      auto now = steady_clock::now();
      float dt =
          std::min(duration<float>(now - lastAnimTime).count(), 1.f / 30.f);
      lastAnimTime = now;
      float settTarget = showSettings ? 1.f : 0.f;
      float statsTarget = settings.showStats ? 1.f : 0.f;
      float prevSett = settingsAnim, prevStats = statsAnim;
      settingsAnim += (settTarget - settingsAnim) * std::min(1.f, dt * 14.f);
      statsAnim += (statsTarget - statsAnim) * std::min(1.f, dt * 14.f);
      if (settingsAnim < 0.001f) settingsAnim = 0.f;
      if (settingsAnim > 0.999f) settingsAnim = 1.f;
      if (statsAnim < 0.043f) statsAnim = 0.f;
      if (statsAnim > 0.957f) statsAnim = 1.f;
      if (fabs(statsAnim - prevStats) > 0.001f ||
          fabs(settingsAnim - prevSett) > 0.001f) {
        int sw = (int)roundf(statsAnim * kStatsMaxW);
        int settW = (int)roundf(settingsAnim * kSettMaxW);
        int newX = settings.windowX - sw;
        int newW = settings.windowW + sw + settW;
        if (newX != lastSetWindowX || newW != lastSetWindowW ||
            settings.windowH != lastSetWindowH) {
          glfwSetWindowPos(g_window, newX, settings.windowY);
          glfwSetWindowSize(g_window, newW, settings.windowH);
          lastSetWindowX = newX;
          lastSetWindowW = newW;
          lastSetWindowH = settings.windowH;
        }
      }
      if ((settingsAnim == 0.f && prevSett > 0.f) ||
          (statsAnim == 0.f && prevStats > 0.f))
        forceFrame = true;
    }

    // Panic hotkey polling
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

    bool effectiveManual =
        showSettings ? stgManualScaling : settings.manualScaling;
    float effectiveUiScale =
        showSettings ? stgManualUiScale : settings.manualUiScale;
    float uiScale = effectiveManual
                        ? std::clamp(effectiveUiScale, 0.50f, 2.00f)
                        : std::clamp(std::min((float)settings.windowW / 900.f,
                                              (float)settings.windowH / 600.f),
                                     0.78f, 1.22f);
    io.FontGlobalScale = uiScale;

    applyUiTheme(settings, uiScale);

    float statsW = statsAnim * kStatsMaxW;
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
    // Scale with window width: ~24% of content area, clamped between 9–14 em.
    // At baseline 903px: 903×0.24≈217 ≈ fontSize(18)×12. Shrinks on small
    // windows.
    float leftPanelWidth = std::clamp((float)settings.windowW * 0.24f,
                                      fontSize * 9.f, fontSize * 14.f);

    float lineH = ImGui::GetTextLineHeightWithSpacing();
    // Use 2 log lines on short windows to free vertical space for content
    int logLines = std::clamp((settings.windowH - 200) / 130, 1, 6);
    float logH =
        lineH * (float)logLines + ImGui::GetStyle().WindowPadding.y * 2.f;
    float rowH = ImGui::GetTextLineHeightWithSpacing() + 3.f;
    float sepH = 1.f + ImGui::GetStyle().ItemSpacing.y * 2.f;
    float bottomH = rowH + logH + sepH;

    // Left panel has a subtle background tint
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(settings.backgroundColor.x * 0.92f,
                   settings.backgroundColor.y * 0.92f,
                   settings.backgroundColor.z * 1.05f,
                   1.f))
            ? settings.backgroundColor  // Just use as-is, color is via
                                        // alpha/blend below
            : settings.backgroundColor);
    ImGui::PopStyleColor();

    ImGui::BeginChild("##controls", ImVec2(leftPanelWidth, -bottomH), true);

    // Presets section
    ImGui::SeparatorText("Presets");

    for (int i = 0; i < (int)settings.presets.size(); i++) {
      bool hasData = settings.presets[i].has_value();
      bool isLoaded = (loadedPresetIndex == i);
      bool isDirty = isLoaded && isLoadedPresetDirty();
      bool isDefault = (settings.defaultPreset == i);

      std::string label = hasData ? settings.presets[i]->name
                                  : ("Preset " + std::to_string(i + 1));
      if (isDirty) label += "  *";

      // Button color: dirty=amber, default=green, loaded=blue, else normal
      ImVec4 btnCol;
      if (isDirty)
        btnCol = {0.52f, 0.36f, 0.06f, 1.f};
      else if (isDefault)
        btnCol = {0.13f, 0.48f, 0.30f, 1.f};
      else if (isLoaded)
        btnCol = {0.13f, 0.34f, 0.52f, 1.f};
      else
        btnCol = ImGui::GetStyle().Colors[ImGuiCol_Button];

      ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
      ImGui::PushStyleColor(
          ImGuiCol_ButtonHovered,
          {std::min(btnCol.x + 0.10f, 1.f), std::min(btnCol.y + 0.10f, 1.f),
           std::min(btnCol.z + 0.10f, 1.f), 1.f});

      float saveIconW = 22.f + ImGui::GetStyle().ItemSpacing.x;
      float btnW = ImGui::GetContentRegionAvail().x - saveIconW;

      if (ImGui::Button(label.c_str(), ImVec2(btnW - 2.f, 0))) {
        if (hasData) {
          if (isLoadedPresetDirty()) {
            pendingLoadPresetIndex = i;
            ImGui::OpenPopup("##confirmload");
          } else {
            flushCurrentCurve();
            auto& preset = *settings.presets[i];
            settings.curves = preset.curves;
            if (settings.curves.empty()) {
              settings.curves.push_back(Preset());
              settings.curves[0].name = "Default";
            }
            if (currentCurveIndex >= (int)settings.curves.size())
              currentCurveIndex = 0;
            hub.curvePoints = settings.curves[currentCurveIndex].curvePoints;
            xViewMin = settings.curves[currentCurveIndex].xViewMin;
            xViewMax = settings.curves[currentCurveIndex].xViewMax;
            minDur = settings.curves[currentCurveIndex].minShockDuration;
            maxDur = settings.curves[currentCurveIndex].maxShockDuration;
            settings.minShockDuration = minDur;
            settings.maxShockDuration = maxDur;
            for (auto& param : settings.parameters)
              if (param.curveIndex >= (int)settings.curves.size())
                param.curveIndex = 0;
            loadedPresetIndex = i;
            commitLoadedPresetSnapshot();
          }
        }
      }

      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        if (boldFont) ImGui::PushFont(boldFont);
        ImGui::Text("%s", hasData
                              ? settings.presets[i]->name.c_str()
                              : ("Preset " + std::to_string(i + 1)).c_str());
        if (boldFont) ImGui::PopFont();
        ImGui::Separator();
        ImGui::TextDisabled("LClick  Load");
        ImGui::TextDisabled("MClick  Set as default");
        ImGui::TextDisabled("RClick  Rename");
        if (isDefault)
          ImGui::TextColored({0.4f, 1.f, 0.7f, 1.f}, "\xe2\x98\x86 Default");
        if (isDirty)
          ImGui::TextColored({1.f, 0.75f, 0.2f, 1.f}, "* Unsaved changes");
        if (isLoaded && !isDirty)
          ImGui::TextColored({0.4f, 0.8f, 1.f, 1.f}, "Loaded");
        ImGui::EndTooltip();
      }

      ImGui::PopStyleColor(2);

      if (ImGui::IsItemClicked(ImGuiMouseButton_Middle))
        settings.defaultPreset = i, settings.save(settingsPath);

      if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && hasData)
        ImGui::OpenPopup(("##rename" + std::to_string(i)).c_str());

      if (ImGui::BeginPopup(("##rename" + std::to_string(i)).c_str())) {
        static char nameBuf[64] = {};
        if (ImGui::IsWindowAppearing())
          snprintf(nameBuf, sizeof(nameBuf), "%s", label.c_str());
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("Name##rn", nameBuf, sizeof(nameBuf),
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
      if (drawSaveIconButton(("##save" + std::to_string(i)).c_str())) {
        if (currentCurveIndex >= 0 &&
            currentCurveIndex < (int)settings.curves.size()) {
          settings.curves[currentCurveIndex].curvePoints = hub.curvePoints;
          settings.curves[currentCurveIndex].xViewMin = xViewMin;
          settings.curves[currentCurveIndex].xViewMax = xViewMax;
          settings.curves[currentCurveIndex].minShockDuration = minDur;
          settings.curves[currentCurveIndex].maxShockDuration = maxDur;
        }
        SavedPreset sp;
        sp.curves = settings.curves;
        sp.activeCurveIndex = currentCurveIndex;
        sp.name = settings.presets[i].has_value()
                      ? settings.presets[i]->name
                      : ("Preset " + std::to_string(i + 1));
        settings.presets[i] = sp;
        settings.save(settingsPath);
        if (loadedPresetIndex == i) commitLoadedPresetSnapshot();
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save to slot");
    }

    // Confirm load popup
    if (ImGui::BeginPopupModal("##confirmload", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextColored({1.f, 0.75f, 0.2f, 1.f}, "Unsaved changes");
      ImGui::Spacing();
      ImGui::Text("The loaded preset has unsaved changes.\nLoad anyway?");
      ImGui::TextDisabled("Tip: CTRL+S to save quickly.");
      ImGui::Spacing();
      if (ImGui::Button("Load anyway", {110, 0})) {
        if (pendingLoadPresetIndex >= 0 &&
            settings.presets[pendingLoadPresetIndex].has_value()) {
          int i = pendingLoadPresetIndex;
          flushCurrentCurve();
          auto& preset = *settings.presets[i];
          settings.curves = preset.curves;
          if (settings.curves.empty()) {
            settings.curves.push_back(Preset());
            settings.curves[0].name = "Default";
          }
          if (currentCurveIndex >= (int)settings.curves.size())
            currentCurveIndex = 0;
          hub.curvePoints = settings.curves[currentCurveIndex].curvePoints;
          xViewMin = settings.curves[currentCurveIndex].xViewMin;
          xViewMax = settings.curves[currentCurveIndex].xViewMax;
          minDur = settings.curves[currentCurveIndex].minShockDuration;
          maxDur = settings.curves[currentCurveIndex].maxShockDuration;
          settings.minShockDuration = minDur;
          settings.maxShockDuration = maxDur;
          for (auto& param : settings.parameters)
            if (param.curveIndex >= (int)settings.curves.size())
              param.curveIndex = 0;
          loadedPresetIndex = i;
          commitLoadedPresetSnapshot();
        }
        pendingLoadPresetIndex = -1;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", {80, 0})) {
        pendingLoadPresetIndex = -1;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    // Curve parameters
    ImGui::Spacing();
    ImGui::SeparatorText("Duration");

    // Duration sliders side-by-side in compact form

    float halfW =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) *
        0.5f;

    // Labels on the same line
    ImGui::TextDisabled("Min (s)");
    ImGui::SameLine(halfW + ImGui::GetStyle().ItemSpacing.x);
    ImGui::TextDisabled("Max (s)");

    // Sliders on the same line
    ImGui::SetNextItemWidth(halfW);
    ImGui::SliderFloat("##mind", &minDur, 0.1f, 10.f, "%.1f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      minDur = std::min(minDur, maxDur - 0.1f);
      settings.minShockDuration = minDur;
      if (currentCurveIndex >= 0 &&
          currentCurveIndex < (int)settings.curves.size())
        settings.curves[currentCurveIndex].minShockDuration = minDur;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(halfW);
    ImGui::SliderFloat("##maxd", &maxDur, 0.1f, 10.f, "%.1f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      maxDur = std::max(maxDur, minDur + 0.1f);
      settings.maxShockDuration = maxDur;
      if (currentCurveIndex >= 0 &&
          currentCurveIndex < (int)settings.curves.size())
        settings.curves[currentCurveIndex].maxShockDuration = maxDur;
    }

    ImGui::Spacing();

    // Cooldown toggle - more prominent
    {
      ImVec4 cdCol = cooldownEnabled
                         ? ImVec4(0.13f, 0.48f, 0.30f, 1.f)
                         : ImGui::GetStyle().Colors[ImGuiCol_Button];
      ImGui::PushStyleColor(ImGuiCol_Button, cdCol);
      ImGui::PushStyleColor(
          ImGuiCol_ButtonHovered,
          {cdCol.x + 0.1f, cdCol.y + 0.1f, cdCol.z + 0.1f, 1.f});
      if (ImGui::Button(cooldownEnabled ? "Cooldown  ON " : "Cooldown  OFF",
                        {-1, 0})) {
        cooldownEnabled = !cooldownEnabled;
        settings.cooldownEnabled = cooldownEnabled;
      }
      ImGui::PopStyleColor(2);
    }

    // Test buttons
    ImGui::Spacing();
    ImGui::SeparatorText("Test");

    // Two equal-width test buttons with icons
    {
      float bw =
          (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) *
          0.5f;
      // Vibrate — softer blue-purple
      ImGui::PushStyleColor(ImGuiCol_Button, {0.20f, 0.18f, 0.45f, 1.f});
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.28f, 0.25f, 0.60f, 1.f});
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.15f, 0.13f, 0.35f, 1.f});
      if (ImGui::Button("~  Vibrate", {bw, 0})) hub.queueShock(-1, true);
      ImGui::SetItemTooltip("Send a test vibration");
      ImGui::PopStyleColor(3);

      ImGui::SameLine();

      // Shock — warm red-orange
      ImGui::PushStyleColor(ImGuiCol_Button, {0.45f, 0.18f, 0.18f, 1.f});
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.62f, 0.24f, 0.24f, 1.f});
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.35f, 0.12f, 0.12f, 1.f});
      if (ImGui::Button("\xe2\x9a\xa1  Shock", {-1, 0}))
        hub.queueShock(-1, false);
      ImGui::SetItemTooltip("Send a test shock");
      ImGui::PopStyleColor(3);
    }

    // Bottom action buttons
    // Compute total height needed for the bottom buttons section
    float totalBtnsH;
    {
      int extraRows = (!hub.isConnected ? 1 : 0) + (hub.shocksDisabled ? 1 : 0);
      totalBtnsH = (0.8f + extraRows) * ImGui::GetFrameHeightWithSpacing() +
                   ImGui::GetStyle().WindowPadding.y;
    }
    // Only jump to the bottom if there is actually space — prevents overlapping
    // content above when the window is short or there are many presets.
    {
      float actionY = ImGui::GetWindowHeight() - totalBtnsH;
      if (actionY > ImGui::GetCursorPosY() + ImGui::GetStyle().ItemSpacing.y)
        ImGui::SetCursorPosY(actionY);
      else
        ImGui::Spacing();
    }

    if (!hub.isConnected) {
      ImGui::PushStyleColor(ImGuiCol_Button, {0.45f, 0.28f, 0.05f, 1.f});
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.60f, 0.36f, 0.08f, 1.f});
      if (ImGui::Button("Retry Connection", {-1, 0})) hub.tryReconnect();
      ImGui::PopStyleColor(2);
    }

    if (hub.shocksDisabled) {
      ImGui::PushStyleColor(ImGuiCol_Button, {0.55f, 0.12f, 0.12f, 1.f});
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.70f, 0.18f, 0.18f, 1.f});
      if (ImGui::Button("Enable Shocks", {-1, 0})) hub.enableShocks();
      ImGui::PopStyleColor(2);
    }

    // Stats / Settings buttons - equal width, at very bottom
    float halfBtn =
        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) *
        0.5f;

    bool settingsDirty =
        showSettings &&
        (settings.shockParameter != stgShockParam ||
         settings.secondShockParameter != stgSecondParam ||
         settings.serialPort != stgSerialPort ||
         settings.vrchatHost != stgVrchatHost ||
         settings.usePishock != stgUsePishock ||
         settings.useSerial != stgUseSerial ||
         settings.randomOrSeq != stgRandomOrSeq ||
         settings.baseCooldown != stgBaseCooldown ||
         settings.maxCooldown != stgMaxCooldown ||
         settings.cooldownFactor != stgCooldownFactor ||
         settings.cooldownWindow != stgCooldownWindow ||
         settings.notificationsEnabled != stgNotifEnabled ||
         settings.notifUseOvrToolkit != stgNotifUseOvr ||
         settings.pishockUsername != stgPishockUser ||
         settings.pishockApiKey != stgPishockKey ||
         settings.openshockApiToken != stgOpenshockToken ||
         settings.openshockServerUrl != stgOpenshockServer ||
         settings.parameters != stgParameters ||
         settings.chatboxShockEnabled != stgChatboxShockEnabled ||
         settings.chatboxCooldownEnabled != stgChatboxCooldownEnabled ||
         origManualScaling != stgManualScaling ||
         origManualUiScale != stgManualUiScale);

    // Stats button
    ImVec4 statsCol = settings.showStats
                          ? ImVec4(0.13f, 0.34f, 0.52f, 1.f)
                          : ImGui::GetStyle().Colors[ImGuiCol_Button];
    ImGui::PushStyleColor(ImGuiCol_Button, statsCol);
    if (ImGui::Button("Stats", {halfBtn, 0}))
      settings.showStats = !settings.showStats;
    ImGui::PopStyleColor();
    ImGui::SameLine();

    // Settings button
    if (settingsDirty)
      ImGui::PushStyleColor(ImGuiCol_Button, {0.52f, 0.36f, 0.06f, 1.f});
    else if (showSettings)
      ImGui::PushStyleColor(ImGuiCol_Button, {0.13f, 0.34f, 0.52f, 1.f});
    else
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyle().Colors[ImGuiCol_Button]);

    if (ImGui::Button(settingsDirty ? "Settings *" : "Settings", {-1, 0})) {
      if (!showSettings) {
        openSettingsModal();
        showSettings = true;
      } else
        closeSettingsModal();
    }
    ImGui::PopStyleColor();
    if (showSettings) ImGui::SetItemTooltip("Close without saving");

    ImGui::EndChild();

    // Curve editor
    ImGui::SameLine();
    // Clamp plot width to always be positive regardless of window size
    float plotW = std::max(150.f, (float)settings.windowW - leftPanelWidth -
                                      ImGui::GetStyle().ItemSpacing.x -
                                      ImGui::GetStyle().WindowPadding.x * 2.f);
    ImGui::BeginChild("##plot", {plotW, -bottomH}, false);

    // Curve tab bar
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

      float tabButtonWidth = fontSize * 4.2f;
      static int wantsRenameCurve = -1;
      static char renameCurveBuf[256] = {};

      for (int i = 0; i < (int)settings.curves.size(); i++) {
        bool isCurrent = (currentCurveIndex == i);

        // Active tab: accent color; inactive: subtle
        if (isCurrent) {
          ImGui::PushStyleColor(ImGuiCol_Button,
                                ImGui::GetStyle().Colors[ImGuiCol_TabActive]);
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                ImGui::GetStyle().Colors[ImGuiCol_TabHovered]);
        } else {
          ImGui::PushStyleColor(ImGuiCol_Button,
                                ImGui::GetStyle().Colors[ImGuiCol_Tab]);
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                ImGui::GetStyle().Colors[ImGuiCol_TabHovered]);
        }

        std::string label = settings.curves[i].name.empty()
                                ? ("Curve " + std::to_string(i + 1))
                                : settings.curves[i].name;

        if (ImGui::Button(label.c_str(), {tabButtonWidth, 0})) {
          saveCurrentCurve();
          currentCurveIndex = i;
          hub.curvePoints = settings.curves[i].curvePoints;
          xViewMin = settings.curves[i].xViewMin;
          xViewMax = settings.curves[i].xViewMax;
          minDur = settings.curves[i].minShockDuration;
          maxDur = settings.curves[i].maxShockDuration;
          settings.minShockDuration = minDur;
          settings.maxShockDuration = maxDur;
        }

        ImGui::PopStyleColor(2);

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
          if (ImGui::BeginMenu("Copy from Preset")) {
            bool anyPreset = false;
            for (int pi = 0; pi < (int)settings.presets.size(); pi++) {
              if (!settings.presets[pi].has_value()) continue;
              auto& sp = *settings.presets[pi];
              if (sp.curves.empty()) continue;
              anyPreset = true;
              if (ImGui::BeginMenu(sp.name.c_str())) {
                for (int ci = 0; ci < (int)sp.curves.size(); ci++) {
                  const auto& srcCurve = sp.curves[ci];
                  std::string curveLabel =
                      srcCurve.name.empty()
                          ? ("Curve " + std::to_string(ci + 1))
                          : srcCurve.name;
                  if (ImGui::MenuItem(curveLabel.c_str())) {
                    saveCurrentCurve();
                    Preset& dst = settings.curves[i];
                    dst.curvePoints = srcCurve.curvePoints;
                    dst.xViewMin = srcCurve.xViewMin;
                    dst.xViewMax = srcCurve.xViewMax;
                    dst.minShockDuration = srcCurve.minShockDuration;
                    dst.maxShockDuration = srcCurve.maxShockDuration;
                    if (i == currentCurveIndex) {
                      hub.curvePoints = dst.curvePoints;
                      xViewMin = dst.xViewMin;
                      xViewMax = dst.xViewMax;
                      minDur = dst.minShockDuration;
                      maxDur = dst.maxShockDuration;
                      settings.minShockDuration = minDur;
                      settings.maxShockDuration = maxDur;
                    }
                  }
                }
                ImGui::EndMenu();
              }
            }
            if (!anyPreset) ImGui::TextDisabled("No saved presets");
            ImGui::EndMenu();
          }
          if (ImGui::MenuItem("Delete")) {
            saveCurrentCurve();
            std::string deletedName = settings.curves[i].name;
            settings.curves.erase(settings.curves.begin() + i);
            if (settings.curves.empty()) {
              settings.curves.push_back(Preset());
              settings.curves[0].name = "Default";
            }
            if (currentCurveIndex >= (int)settings.curves.size())
              currentCurveIndex = (int)settings.curves.size() - 1;
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
            for (auto& param : settings.parameters)
              if (param.curveIndex >= (int)settings.curves.size())
                param.curveIndex = 0;
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

        ImGui::SameLine();
      }

      // Add curve button
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyle().Colors[ImGuiCol_Tab]);
      if (ImGui::Button("  +  ", {0, 0})) {
        saveCurrentCurve();
        Preset newCurve;
        newCurve.name = "Curve " + std::to_string(settings.curves.size() + 1);
        settings.curves.push_back(newCurve);
        for (auto& p : settings.presets)
          if (p.has_value()) p->curves.push_back(newCurve);
        currentCurveIndex = (int)settings.curves.size() - 1;
        hub.curvePoints = newCurve.curvePoints;
        xViewMin = newCurve.xViewMin;
        xViewMax = newCurve.xViewMax;
        minDur = newCurve.minShockDuration;
        maxDur = newCurve.maxShockDuration;
        settings.save(settingsPath);
      }
      ImGui::SetItemTooltip("Add new curve");
      ImGui::PopStyleColor();

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
            for (auto& p : settings.presets) {
              if (!p.has_value()) continue;
              for (auto& pc : p->curves)
                if (pc.name == oldName) pc.name = renameCurveBuf;
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

    if (curveCache.size() != settings.curves.size())
      curveCache.resize(settings.curves.size());

    if (ImPlot::BeginPlot(" ", {-1, -sliderH})) {
      ImPlot::SetupAxes("Intensity (%)", "Weight", ImPlotAxisFlags_NoGridLines,
                        ImPlotAxisFlags_NoGridLines);
      ImPlot::SetupAxisLimits(ImAxis_X1, xViewMin - 1.0, xViewMax + 1.0,
                              ImPlotCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, -0.02, 1.02, ImPlotCond_Always);
      ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_None);
      ImPlot::SetupFinish();

      ImDrawList* dl = ImPlot::GetPlotDrawList();

      // Centered bold title
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
      // Gradient background
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
        auto sorted = pts;
        std::sort(
            sorted.begin(), sorted.end(),
            [](const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
        auto& cache = curveCache[currentCurveIndex];
        if (sorted != cache.lastPts) {
          cache.lastPts = sorted;
          auto curve = bezierInterpolate(sorted[0], sorted[1], sorted[2]);
          cache.cx.resize(curve.size());
          cache.cy.resize(curve.size());
          for (size_t i = 0; i < curve.size(); i++) {
            cache.cx[i] = curve[i].x;
            cache.cy[i] = curve[i].y;
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

        // Subtle grid
        ImU32 gridCol = IM_COL32(255, 255, 255, 22);
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

        // Dashed min/max lines
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
        ImPlot::PlotLine("##curve", cache.cx.data(), cache.cy.data(),
                         (int)cache.cx.size());

        for (int i = 0; i < 3; i++) {
          ImPlot::DragPoint(i, &pts[i].x, &pts[i].y, settings.markerColor,
                            settings.touchMarkerSize / 15.f,
                            ImPlotDragToolFlags_None);
          pts[i].x = std::clamp(pts[i].x, 0.0, 100.0);
          pts[i].y = std::clamp(pts[i].y, 0.0, 1.0);
        }
        if (currentCurveIndex >= 0 &&
            currentCurveIndex < (int)settings.curves.size())
          settings.curves[currentCurveIndex].curvePoints = hub.curvePoints;

        ImPlot::PopPlotClipRect();
      }
      savedPlotPos = ImPlot::GetPlotPos();
      savedPlotSize = ImPlot::GetPlotSize();
      ImPlot::EndPlot();
    }

    // X Scale range slider
    {
      ImVec2 sc = ImGui::GetCursorScreenPos();
      float gap = ImGui::GetStyle().ItemSpacing.y;
      float sliderRowH = ImGui::GetFrameHeight() + 4;
      ImGui::GetWindowDrawList()->AddRectFilled(
          {plotFramePos.x, sc.y - gap},
          {plotFramePos.x + plotFrameWidth - 1.f, sc.y + sliderRowH},
          ImGui::ColorConvertFloat4ToU32(settings.outsideCurveBg));
      ImGui::GetWindowDrawList()->AddText(
          {plotFramePos.x + 8,
           sc.y +
               (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f +
               2},
          ImGui::ColorConvertFloat4ToU32(
              ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]),
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

      ImDrawList* dl2 = ImGui::GetWindowDrawList();
      float cy2 = ImGui::GetCursorScreenPos().y +
                  ImGui::GetTextLineHeight() * 0.5f + 2.f;
      float cx2 = ImGui::GetCursorScreenPos().x;

      // Connection dot with glow
      if (hub.isConnected) {
        dl2->AddCircleFilled({cx2 + 7.f, cy2}, 10.f, IM_COL32(60, 220, 80, 30));
        dl2->AddCircleFilled({cx2 + 7.f, cy2}, 5.5f,
                             IM_COL32(60, 220, 80, 255));
      } else {
        dl2->AddCircleFilled({cx2 + 7.f, cy2}, 10.f, IM_COL32(220, 60, 60, 30));
        dl2->AddCircleFilled({cx2 + 7.f, cy2}, 5.5f,
                             IM_COL32(220, 60, 60, 255));
      }
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.f);

      if (hub.shocksDisabled)
        ImGui::TextColored({1.f, 0.28f, 0.28f, 1.f}, "SHOCKS DISABLED");
      else
        ImGui::Text(hub.isConnected ? "Connected" : "Disconnected");

      ImGui::SameLine(0, 14);
      ImGui::TextDisabled("|");
      ImGui::SameLine(0, 14);

      // Session shock count with lightning icon
      ImGui::TextColored(settings.curveLineColor, "\xe2\x9a\xa1 %d",
                         gStats.sessionShocks);

      ImGui::SameLine(0, 14);
      ImGui::TextDisabled("|");
      ImGui::SameLine(0, 14);

      // Cooldown
      if (settings.cooldownEnabled) {
        double rem =
            std::max(0.0, hub.cooldownUntil.load() - hub.getCurrentTime());
        if (rem > 0.0)
          ImGui::TextColored({1.f, 0.45f, 0.45f, 1.f}, "CD: %.1fs", rem);
        else
          ImGui::TextDisabled("CD: 0.0s");

        ImGui::SameLine(0, 8);

        // Thin cooldown bar
        float textH = ImGui::GetTextLineHeight();
        ImVec2 p = ImGui::GetCursorScreenPos();
        p.y += (textH - 4.f) * 0.75f;
        float fraction = (float)(rem / std::max(1, settings.maxCooldown));
        float barLen = ImGui::GetContentRegionAvail().x * 0.26f;
        float rnd = 2.f;
        dl2->AddRectFilled(p, {p.x + barLen, p.y + 4.f},
                           ImGui::ColorConvertFloat4ToU32(
                               ImGui::GetStyle().Colors[ImGuiCol_FrameBg]),
                           rnd);
        if (fraction > 0.f) {
          // Gradient bar: yellow → red
          ImU32 barLeft = IM_COL32(230, 190, 50, 230);
          ImU32 barRight = IM_COL32(220, 60, 60, 230);
          dl2->AddRectFilledMultiColor(p, {p.x + barLen * fraction, p.y + 4.f},
                                       barLeft, barRight, barRight, barLeft);
        }
        ImGui::Dummy({barLen + 4.f, 4.f});
      }

      // Version (right-aligned)
      float verW = ImGui::CalcTextSize("v" APP_VERSION).x + 4.f;
      ImGui::SameLine(ImGui::GetContentRegionMax().x - verW);
      ImGui::TextDisabled("v" APP_VERSION);
    }

    ImGui::End();

    // Auto-update
    if (updateReady.exchange(false)) {
#ifdef _WIN32
      Updater::applyAndRestart(nullptr);
#else
      Updater::applyAndRestart();
#endif
      glfwSetWindowShouldClose(g_window, 1);
    }

    // commitAll lambda
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
      settings.manualScaling = stgManualScaling;
      settings.manualUiScale = stgManualUiScale;
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
      float sPanelW = settingsAnim * kSettMaxW;
      ImGui::SetNextWindowSize({sPanelW, (float)settings.windowH - 40},
                               ImGuiCond_Always);
      ImGui::SetNextWindowPos({statsW + (float)settings.windowW, 0.f},
                              ImGuiCond_Always);
      ImGui::Begin("Settings", &showSettings,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoTitleBar);

      float settFooterH = ImGui::GetFrameHeightWithSpacing() +
                          ImGui::GetTextLineHeightWithSpacing() +
                          ImGui::GetStyle().ItemSpacing.y * 2.f + 6.f;
      ImGui::BeginChild("##settingsscroll", {0, -settFooterH}, false);

      // Top hint row
      ImGui::TextDisabled(
          "\xe2\x84\xb9  Hover items for details   \xe2\x86\xba Ctrl+Z/Y "
          "undo/redo");
      ImGui::Spacing();

      // OSC / Avatar
      ImGui::SeparatorText("OSC / Avatar");

      std::vector<std::string> curveNames;
      for (int ci = 0; ci < (int)settings.curves.size(); ++ci)
        curveNames.push_back(settings.curves[ci].name.empty()
                                 ? ("Curve " + std::to_string(ci + 1))
                                 : settings.curves[ci].name);
      std::vector<const char*> curveNamePtrs;
      for (auto& name : curveNames) curveNamePtrs.push_back(name.c_str());

      for (int i = 0; i < (int)stgParameters.size(); ++i) {
        auto& param = stgParameters[i];
        ImGui::PushID(i);

        // Parameter header
        float paramH = ImGui::GetFrameHeightWithSpacing() * 3.8f +
                       ImGui::GetStyle().WindowPadding.y * 2.f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
                              ImVec4(settings.accentColor.x * 0.08f,
                                     settings.accentColor.y * 0.08f,
                                     settings.accentColor.z * 0.12f, 1.f));
        ImGui::BeginChild(("##param" + std::to_string(i)).c_str(), {-1, paramH},
                          true, ImGuiWindowFlags_None);

        // Inline name + delete button
        {
          char paramNameBuf[128] = {};
          snprintf(paramNameBuf, sizeof(paramNameBuf), "%s",
                   param.name.c_str());
          ImGui::SetNextItemWidth(-60.f);
          if (ImGui::InputText("##pname", paramNameBuf, sizeof(paramNameBuf)))
            param.name = paramNameBuf;
          ImGui::SameLine();
          ImGui::PushStyleColor(ImGuiCol_Button, {0.45f, 0.10f, 0.10f, 1.f});
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                {0.65f, 0.15f, 0.15f, 1.f});
          bool del = ImGui::Button("Del##p");
          ImGui::PopStyleColor(2);
          if (del) {
            stgParameters.erase(stgParameters.begin() + i);
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopID();
            break;
          }
        }

        // Curve + Range side by side
        if (!curveNamePtrs.empty()) {
          float halfw = (ImGui::GetContentRegionAvail().x -
                         ImGui::GetStyle().ItemSpacing.x) *
                        0.5f;
          ImGui::SetNextItemWidth(halfw);
          if (param.curveIndex < 0) param.curveIndex = 0;
          if (param.curveIndex >= (int)curveNamePtrs.size())
            param.curveIndex = std::max(0, (int)curveNamePtrs.size() - 1);
          ImGui::Combo("##pcurve", &param.curveIndex, curveNamePtrs.data(),
                       curveNamePtrs.size());
          ImGui::SetItemTooltip("Intensity curve for this parameter");
          ImGui::SameLine();
          const char* rangeNames[] = {"Full", "Low Half", "High Half"};
          int rangeIndex = (int)param.range;
          ImGui::SetNextItemWidth(-1);
          ImGui::Combo("##prange", &rangeIndex, rangeNames,
                       IM_ARRAYSIZE(rangeNames));
          ImGui::SetItemTooltip("Intensity range to sample from");
          param.range = static_cast<CurveRange>(rangeIndex);
        } else {
          ImGui::TextDisabled("No curves - create a preset first.");
        }

        // Shocker IDs for this parameter
        {
          std::string shockerStr;
          for (int j = 0; j < (int)param.shockerIDs.size(); j++)
            shockerStr += (j ? ", " : "") + param.shockerIDs[j];
          char shockerBuf[512] = {};
          snprintf(shockerBuf, sizeof(shockerBuf), "%s", shockerStr.c_str());
          ImGui::SetNextItemWidth(-1);
          if (ImGui::InputTextWithHint("##pids", "Shocker IDs (blank = global)",
                                       shockerBuf, sizeof(shockerBuf))) {
            param.shockerIDs.clear();
            std::istringstream ss(shockerBuf);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
              auto s = tok.find_first_not_of(" \t");
              if (s != std::string::npos)
                param.shockerIDs.push_back(tok.substr(s));
            }
          }
          ImGui::SetItemTooltip(
              "Leave empty to use the global shocker list.\n"
              "Separate multiple IDs with commas.");
          ImGui::Checkbox("Sequential##pseq", &param.randomOrSeq);
          ImGui::SetItemTooltip(
              "Cycle shockers sequentially instead of randomly.");
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
        ImGui::Spacing();
      }

      if (ImGui::Button("  + Add Parameter  ")) stgParameters.emplace_back();
      ImGui::SameLine();
      ImGui::TextDisabled("Each maps an OSC param to a curve + shockers.");

      if (stgParameters.empty())
        ImGui::TextColored({1.f, 0.6f, 0.2f, 1.f},
                           "No parameters configured. Add one above.");
      ImGui::TextDisabled("Changes require restart.");
      ImGui::Spacing();

      // Hardware
      ImGui::SeparatorText("Hardware");

      // Backend selector
      ImGui::TextDisabled("Backend:");
      ImGui::SameLine();
      auto toggleBtn = [&](const char* label, bool active,
                           float w = 110.f) -> bool {
        ImVec4 col = active ? ImVec4(0.13f, 0.48f, 0.30f, 1.f)
                            : ImGui::GetStyle().Colors[ImGuiCol_Button];
        ImGui::PushStyleColor(ImGuiCol_Button, col);
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            {col.x + 0.09f, col.y + 0.09f, col.z + 0.09f, 1.f});
        bool clicked = ImGui::Button(label, {w, 0});
        ImGui::PopStyleColor(2);
        return clicked;
      };

      if (toggleBtn("OpenShock", !stgUsePishock)) stgUsePishock = false;
      ImGui::SameLine(0, 2);
      if (toggleBtn("PiShock##hw", stgUsePishock)) stgUsePishock = true;

      ImGui::Spacing();
      ImGui::TextDisabled("Mode:");
      ImGui::SameLine();
      if (toggleBtn("Serial##cm", stgUseSerial)) stgUseSerial = true;
      ImGui::SameLine(0, 2);
      if (toggleBtn("API##cm", !stgUseSerial)) stgUseSerial = false;
      ImGui::Spacing();

      if (stgUseSerial) {
        ImGui::InputTextWithHint("Serial Port", "(blank = auto-detect)",
                                 stgSerialPort, sizeof(stgSerialPort));
        ImGui::InputTextWithHint("Shocker IDs", "ID, ID, ...", stgShockerIDs,
                                 sizeof(stgShockerIDs));
        ImGui::SetItemTooltip("Comma-separated shocker IDs.");
        ImGui::Checkbox("Sequential order (vs Random)##seq", &stgRandomOrSeq);
      } else {
        if (stgUsePishock) {
          ImGui::InputText("Username##psa", stgPishockUser,
                           sizeof(stgPishockUser));
          ImGui::SetNextItemWidth(-36.f);
          ImGui::InputText("API Key##psk", stgPishockKey, sizeof(stgPishockKey),
                           ImGuiInputTextFlags_Password);
          ImGui::SameLine();
          ImGui::TextLinkOpenURL("?", "https://login.pishock.com/Account");
          ImGui::InputTextWithHint("Shocker IDs##ps",
                                   "ID, ID, ... (blank = auto)", stgShockerIDs,
                                   sizeof(stgShockerIDs));
        } else {
          ImGui::SetNextItemWidth(-36.f);
          ImGui::InputText("API Token##ost", stgOpenshockToken,
                           sizeof(stgOpenshockToken),
                           ImGuiInputTextFlags_Password);
          ImGui::SameLine();
          ImGui::TextLinkOpenURL("?",
                                 "https://openshock.app/#/dashboard/tokens");
          ImGui::InputTextWithHint("Server URL##oss", "api.openshock.app",
                                   stgOpenshockServer,
                                   sizeof(stgOpenshockServer));
          ImGui::InputTextWithHint("Shocker IDs##os",
                                   "UUID, UUID, ... (blank = auto)",
                                   stgShockerIDs, sizeof(stgShockerIDs));
          ImGui::SetItemTooltip(
              "UUID from OpenShock dashboard — the long string with dashes.\n"
              "Leave empty to find automatically.");
        }
        ImGui::Checkbox("Sequential order (vs Random)##seqapi",
                        &stgRandomOrSeq);
      }
      ImGui::TextDisabled("Changes require restart.");
      ImGui::Spacing();

      // VR Notifications
      ImGui::SeparatorText("Notifications");
#ifdef _WIN32
      ImGui::Checkbox("Enable VR notifications##notif", &stgNotifEnabled);
      ImGui::SetItemTooltip("Shows shock strength + duration in VR overlay");
      if (stgNotifEnabled) {
        ImGui::SameLine(0, 12);
        ImGui::TextDisabled("via:");
        ImGui::SameLine(0, 8);
        if (toggleBtn("XSOverlay##np", !stgNotifUseOvr, 90.f))
          stgNotifUseOvr = false;
        ImGui::SameLine(0, 2);
        if (toggleBtn("OVRToolkit##np", stgNotifUseOvr, 90.f))
          stgNotifUseOvr = true;
      }
#else
      ImGui::Checkbox("Enable WayVR notifications##notif", &stgNotifEnabled);
      ImGui::SetItemTooltip("Shows shock strength + duration in WayVR overlay");
      stgNotifUseOvr = false;
#endif
      ImGui::Spacing();

      // Cooldown
      ImGui::SeparatorText("Cooldown");
      ImGui::SetNextItemWidth(-1);
      ImGui::SliderInt("Base (s)##cd", &stgBaseCooldown, 1, 15);
      ImGui::SetItemTooltip(
          "Starting cooldown after each shock.\nFormula: base + factor * "
          "recent_shocks");
      ImGui::SetNextItemWidth(-1);
      ImGui::SliderInt("Max (s)##cdm", &stgMaxCooldown, 1, 30);
      ImGui::SetItemTooltip("Cooldown is capped at this value.");
      ImGui::SetNextItemWidth(-1);
      ImGui::SliderFloat("Factor##cdf", &stgCooldownFactor, 0.f, 2.f, "%.2f");
      ImGui::SetItemTooltip(
          "Added to cooldown per shock in the window.\nHigher = longer "
          "cooldown after bursts.");
      ImGui::SetNextItemWidth(-1);
      ImGui::SliderInt("Window (s)##cdw", &stgCooldownWindow, 5, 120);
      ImGui::SetItemTooltip(
          "How far back to count shocks. Older shocks are ignored.");
      ImGui::Spacing();

      // Panic Hotkey
      ImGui::SeparatorText("Panic Hotkey");
      ImGui::TextDisabled("Disables shocks from anywhere, even unfocused:");
      ImGui::Spacing();
      {
        std::string keyLabel =
            capturingHotkey
                ? "Press any key..."
                : (settings.hotkeyVk ? formatKeyNameFromVk(settings.hotkeyVk,
                                                           settings.hotkeyMods)
                                     : "None");
        ImGui::PushStyleColor(ImGuiCol_Button,
                              capturingHotkey
                                  ? ImVec4(0.45f, 0.28f, 0.05f, 1.f)
                                  : ImGui::GetStyle().Colors[ImGuiCol_Button]);
        if (ImGui::Button(keyLabel.c_str(), {-54.f, 0})) {
          capturingHotkey = true;
#ifndef _WIN32
          unregisterGlobalHotkeyLinux();
#endif
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Clear##hk", {-1, 0})) {
          settings.hotkeyVk = 0;
          settings.hotkeyMods = 0;
        }
        ImGui::SetItemTooltip("Remove hotkey");
      }
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
      ImGui::SeparatorText("Style");
      ImGui::SetNextItemWidth(-1);
      ImGui::SliderInt("Preset Slots*##s", &stgPresetCount, 1, 8);
      ImGui::SetNextItemWidth(-1);
      ImGui::SliderFloat("Marker Size##s", &settings.touchMarkerSize, 50.f,
                         300.f, "%.0f");
      ImGui::SetNextItemWidth(-1);
      ImGui::SliderFloat("Curve Width##s", &settings.lineWidth, 1.f, 6.f,
                         "%.1f");
      ImGui::Spacing();

      // Color pickers in 2-column layout
      ImGui::TextDisabled("Colors (live preview):");
      ImGui::Spacing();

      if (ImGui::BeginTable("##colors", 2, ImGuiTableFlags_SizingStretchSame)) {
        auto colorRow = [&](const char* label, ImVec4& col, const char* tip) {
          ImGui::TableNextColumn();
          ImGui::ColorEdit4(
              label, (float*)&col,
              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
          if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        };
        colorRow("Background##s", settings.backgroundColor,
                 "Main window background");
        colorRow("Curve BG##s", settings.outsideCurveBg,
                 "Area outside plot bounds");
        colorRow("Accent##s", settings.accentColor, "Buttons, sliders, inputs");
        colorRow("Curve Line##s", settings.curveLineColor, "Bezier curve line");
        colorRow("Markers##s", settings.markerColor,
                 "Draggable control points");
        colorRow("Labels##s", settings.labelColor, "All text and axis labels");
        colorRow("Gradient L##s", settings.gradientLeftColor,
                 "Plot gradient — low intensity");
        colorRow("Gradient R##s", settings.gradientRightColor,
                 "Plot gradient — high intensity");
        ImGui::EndTable();
      }
      ImGui::Spacing();

      // UI Scaling
      ImGui::TextDisabled("UI Scaling:");
      ImGui::SameLine();
      if (toggleBtn("Automatic", !stgManualScaling)) stgManualScaling = false;
      ImGui::SameLine(0, 2);
      if (toggleBtn("Manual##uis", stgManualScaling)) stgManualScaling = true;
      if (stgManualScaling) {
        ImGui::SameLine(0, 14);
        if (ImGui::Button("-##uisdn"))
          stgManualUiScale = std::max(0.50f, stgManualUiScale - 0.05f);
        ImGui::SetItemTooltip("Decrease 5%%  (Ctrl -)");
        ImGui::SameLine(0, 6);
        ImGui::Text("%.0f%%", stgManualUiScale * 100.f);
        ImGui::SameLine(0, 6);
        if (ImGui::Button("+##uisup"))
          stgManualUiScale = std::min(2.00f, stgManualUiScale + 0.05f);
        ImGui::SetItemTooltip("Increase 5%%  (Ctrl +)");
      }
      // Always live-apply so switching back to Automatic takes effect
      // immediately
      settings.manualScaling = stgManualScaling;
      settings.manualUiScale = stgManualUiScale;

      ImGui::TextDisabled("* Requires restart");
      ImGui::Spacing();

      // VRChat
      ImGui::SeparatorText("VRChat");
      ImGui::Checkbox("Send shocks to ChatBox##cbx", &stgChatboxShockEnabled);
      ImGui::Checkbox("Send cooldown msgs to ChatBox##cbcd",
                      &stgChatboxCooldownEnabled);
      ImGui::Spacing();
      ImGui::InputTextWithHint("VRChat Host##s", "127.0.0.1", stgVrchatHost,
                               sizeof(stgVrchatHost));
      ImGui::SetItemTooltip(
          "Usually 127.0.0.1. Change only if VRChat is on another machine.");
      ImGui::Spacing();

      ImGui::EndChild();  // settingsscroll

      // Settings footer
      ImGui::Separator();
      if (settingsDirty)
        ImGui::TextColored({1.f, 0.75f, 0.2f, 1.f}, "  * Unsaved changes");
      else
        ImGui::TextDisabled("  No unsaved changes");
      ImGui::Separator();

      bool needsRestart = settings.parameters != stgParameters ||
                          settings.serialPort != stgSerialPort ||
                          settings.usePishock != stgUsePishock ||
                          settings.randomOrSeq != stgRandomOrSeq ||
                          settings.vrchatHost != stgVrchatHost ||
                          settings.presetCount != stgPresetCount ||
                          settings.useSerial != stgUseSerial ||
                          settings.pishockUsername != stgPishockUser ||
                          settings.pishockApiKey != stgPishockKey ||
                          settings.openshockApiToken != stgOpenshockToken ||
                          settings.openshockServerUrl != stgOpenshockServer;

      {
        float btnW = 130.f;
        if (needsRestart) {
          ImGui::PushStyleColor(ImGuiCol_Button, {0.45f, 0.28f, 0.05f, 1.f});
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                {0.60f, 0.36f, 0.08f, 1.f});
          if (ImGui::Button("Save & Restart", {btnW, 0})) {
            commitAll();
            registerPanicHotkey(settings);
            shouldRestart = true;
            glfwSetWindowShouldClose(g_window, 1);
          }
          ImGui::PopStyleColor(2);
        } else {
          ImGui::PushStyleColor(ImGuiCol_Button, {0.13f, 0.48f, 0.30f, 1.f});
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                {0.18f, 0.62f, 0.38f, 1.f});
          if (ImGui::Button("Save##sett", {btnW, 0})) {
            commitAll();
            registerPanicHotkey(settings);
            showSettings = false;
          }
          ImGui::PopStyleColor(2);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##sett", {90, 0})) closeSettingsModal();
        ImGui::SetItemTooltip(
            "Close without saving. Theme changes will revert.");
      }

      ImGui::End();
    }

    // Stats panel
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
        row("Peak", fmt::format("{}%", gStats.highestIntensity));
        row("Longest", fmt::format("{:.1f}s", gStats.longestShockMs / 1000.0));
      }
      row("CD blocks", std::to_string(gStats.totalCooldownHits));

      ImGui::Spacing();
      ImGui::SeparatorText("Session");
      row("Shocks", std::to_string(gStats.sessionShocks));
      row("Vibrations", std::to_string(gStats.sessionVibrations));
      row("Shock time", fmtMs(gStats.sessionShockDurationMs));
      row("CD blocks", std::to_string(gStats.sessionCooldownHits));

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
        int tdm = gStats.todayMaxIntensity();
        if (tdm > 0) {
          ImGui::SameLine(0, 8);
          ImGui::TextDisabled("| Peak:");
          ImGui::SameLine(0, 4);
          ImGui::Text("%d%%", tdm);
        }
      }

      ImGui::Spacing();
      ImGui::SeparatorText("Last 7 Days");
      {
        auto days = gStats.lastNDays(7);
        double vals[7] = {};
        float statsChartH = std::max(60.f, settings.windowH * 0.18f);
        if (ImPlot::BeginPlot("##7d", {-1, statsChartH},
                              ImPlotFlags_NoTitle | ImPlotFlags_NoLegend |
                                  ImPlotFlags_NoMouseText)) {
          ImPlot::SetupAxes(
              nullptr, nullptr,
              ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoLabel,
              ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoLabel);
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
          if (!yticks.empty() && yticks.back() < max_i)
            yticks.push_back((double)max_i);
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
      ImGui::PopStyleVar();

      ImGui::End();
    }

    // Ctrl+Z / Y / S
    const bool editingText = io.WantTextInput;
    bool ctrlDown = glfwGetKey(g_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                    glfwGetKey(g_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    bool zDown = glfwGetKey(g_window, GLFW_KEY_Z) == GLFW_PRESS;
    bool yDown = glfwGetKey(g_window, GLFW_KEY_Y) == GLFW_PRESS;
    bool sDown = glfwGetKey(g_window, GLFW_KEY_S) == GLFW_PRESS;
    bool plusDown = glfwGetKey(g_window, GLFW_KEY_EQUAL) == GLFW_PRESS;
    bool minusDown = glfwGetKey(g_window, GLFW_KEY_MINUS) == GLFW_PRESS;

    bool didUndoRedo = false;

    if (!editingText) {
      if (ctrlDown && zDown && !ctrlZPrev) {
        performUndoRedo(true, undoStack, redoStack, ui, isPerformingUndoRedo);
        didUndoRedo = true;
      } else if (ctrlDown && yDown && !ctrlYPrev) {
        performUndoRedo(false, undoStack, redoStack, ui, isPerformingUndoRedo);
        didUndoRedo = true;
      } else if (ctrlDown && sDown && !ctrlSPrev && loadedPresetIndex >= 0) {
        if (currentCurveIndex >= 0 &&
            currentCurveIndex < (int)settings.curves.size()) {
          settings.curves[currentCurveIndex].curvePoints = hub.curvePoints;
          settings.curves[currentCurveIndex].xViewMin = xViewMin;
          settings.curves[currentCurveIndex].xViewMax = xViewMax;
          settings.curves[currentCurveIndex].minShockDuration = minDur;
          settings.curves[currentCurveIndex].maxShockDuration = maxDur;
        }
        SavedPreset sp;
        sp.curves = settings.curves;
        sp.activeCurveIndex = currentCurveIndex;
        sp.name = settings.presets[loadedPresetIndex].has_value()
                      ? settings.presets[loadedPresetIndex]->name
                      : ("Preset " + std::to_string(loadedPresetIndex + 1));
        settings.presets[loadedPresetIndex] = sp;
        settings.save(settingsPath);
        commitLoadedPresetSnapshot();
      }
      if (ctrlDown && plusDown && !ctrlPlusPrev) {
        settings.manualScaling = true;
        settings.manualUiScale =
            std::clamp(settings.manualUiScale + 0.05f, 0.50f, 2.00f);
        stgManualScaling = true;
        stgManualUiScale = settings.manualUiScale;
      }
      if (ctrlDown && minusDown && !ctrlMinusPrev) {
        settings.manualScaling = true;
        settings.manualUiScale =
            std::clamp(settings.manualUiScale - 0.05f, 0.50f, 2.00f);
        stgManualScaling = true;
        stgManualUiScale = settings.manualUiScale;
      }
    }
    ctrlZPrev = ctrlDown && zDown;
    ctrlYPrev = ctrlDown && yDown;
    ctrlSPrev = ctrlDown && sDown;
    ctrlPlusPrev = ctrlDown && plusDown;
    ctrlMinusPrev = ctrlDown && minusDown;

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

    // Close warning modal
    if (g_pendingClose.exchange(false)) {
      if (settingsDirty || isLoadedPresetDirty())
        ImGui::OpenPopup("##closewarn");
      else {
        running = false;
        glfwSetWindowShouldClose(g_window, GLFW_TRUE);
        g_wakeUiFunc = nullptr;
        if (g_hub) g_hub->queueCV.notify_all();
      }
    }
    if (ImGui::BeginPopupModal("##closewarn", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextColored({1.f, 0.75f, 0.2f, 1.f}, "Unsaved changes");

      ImGui::Spacing();

      if (settingsDirty) ImGui::BulletText("Settings");
      if (isLoadedPresetDirty()) ImGui::BulletText("Current preset");

      ImGui::Spacing();

      if (ImGui::Button("Save & Quit", {120, 0})) {
        if (settingsDirty) commitAll();
        if (isLoadedPresetDirty() && loadedPresetIndex >= 0) {
          flushCurrentCurve();
          SavedPreset sp;
          sp.curves = settings.curves;
          sp.activeCurveIndex = currentCurveIndex;
          sp.name = settings.presets[loadedPresetIndex].has_value()
                        ? settings.presets[loadedPresetIndex]->name
                        : ("Preset " + std::to_string(loadedPresetIndex + 1));
          settings.presets[loadedPresetIndex] = sp;
          settings.save(settingsPath);
        }

        ImGui::CloseCurrentPopup();
        running = false;
        glfwSetWindowShouldClose(g_window, GLFW_TRUE);
        g_wakeUiFunc = nullptr;
        if (g_hub) g_hub->queueCV.notify_all();
      }

      ImGui::SameLine();
      if (ImGui::Button("Discard & Quit", {130, 0})) {
        ImGui::CloseCurrentPopup();
        running = false;
        glfwSetWindowShouldClose(g_window, GLFW_TRUE);
        g_wakeUiFunc = nullptr;
        if (g_hub) g_hub->queueCV.notify_all();
      }

      ImGui::SameLine();
      if (ImGui::Button("Cancel", {80, 0})) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }

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