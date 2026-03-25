#pragma once

#include <commdlg.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windows.h>

#include <array>
#include <cmath>
#include <deque>
#include <thread>
using namespace std::chrono;

#include "curve.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "imgui_internal.h"
#include "implot.h"
#include "logger.h"
#include "settings.h"
#include "shockerhub.h"
#include "stats.h"
#include "updater.h"

static constexpr wchar_t kWindowTitle[] = L"Shocker Link";

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRTV = nullptr;
static HWND g_hwnd = nullptr;

static ShockerHub* g_hub = nullptr;

extern std::atomic<bool> shouldRestart;

static constexpr size_t kMaxUndoRedoStates = 128;

static void createRenderTargetView() {
  ID3D11Texture2D* buf = nullptr;
  g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&buf));
  g_pd3dDevice->CreateRenderTargetView(buf, nullptr, &g_mainRTV);
  buf->Release();
}
static void destroyRenderTargetView() {
  if (g_mainRTV) {
    g_mainRTV->Release();
    g_mainRTV = nullptr;
  }
}

struct AppState {
  Settings settings;
  std::array<CurvePoint, 3> curvePoints;
  float minDur = 0.f;
  float maxDur = 0.f;
  float xViewMin = 0.f;
  float xViewMax = 0.f;
  bool cooldownEnabled = false;

  std::string stgShockParam;
  std::string stgSecondParam;
  std::string stgShockerIDs;
  std::string stgSerialPort;
  std::string stgVrchatHost;
  bool stgUsePishock = false;
  bool stgRandomOrSeq = false;
  int stgBaseCooldown = 2;
  int stgMaxCooldown = 6;
  float stgCooldownFactor = 0.4f;
  int stgCooldownWindow = 30;
  bool stgNotifEnabled = false;
  bool stgNotifUseOvr = false;
  bool stgUseSerial = true;
  std::string stgPishockUser;
  std::string stgPishockKey;
  std::string stgOpenshockToken;
  std::string stgOpenshockServer;
  int stgPresetCount = 3;
  float stgTouchThreshold = 8.f;

  bool operator==(const AppState& other) const {
    return settings == other.settings && curvePoints == other.curvePoints &&
           minDur == other.minDur && maxDur == other.maxDur &&
           xViewMin == other.xViewMin && xViewMax == other.xViewMax &&
           cooldownEnabled == other.cooldownEnabled &&
           stgShockParam == other.stgShockParam &&
           stgSecondParam == other.stgSecondParam &&
           stgShockerIDs == other.stgShockerIDs &&
           stgSerialPort == other.stgSerialPort &&
           stgVrchatHost == other.stgVrchatHost &&
           stgUsePishock == other.stgUsePishock &&
           stgRandomOrSeq == other.stgRandomOrSeq &&
           stgBaseCooldown == other.stgBaseCooldown &&
           stgMaxCooldown == other.stgMaxCooldown &&
           stgCooldownFactor == other.stgCooldownFactor &&
           stgCooldownWindow == other.stgCooldownWindow &&
           stgNotifEnabled == other.stgNotifEnabled &&
           stgNotifUseOvr == other.stgNotifUseOvr &&
           stgUseSerial == other.stgUseSerial &&
           stgPishockUser == other.stgPishockUser &&
           stgPishockKey == other.stgPishockKey &&
           stgOpenshockToken == other.stgOpenshockToken &&
           stgOpenshockServer == other.stgOpenshockServer &&
           stgPresetCount == other.stgPresetCount &&
           stgTouchThreshold == other.stgTouchThreshold;
  }

  bool operator!=(const AppState& other) const { return !(*this == other); }
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
  char (&stgPishockUser)[128];
  char (&stgPishockKey)[128];
  char (&stgOpenshockToken)[256];
  char (&stgOpenshockServer)[128];
  int& stgPresetCount;
  float& stgTouchThreshold;
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

  strncpy_s(ui.stgShockParam, st.stgShockParam.c_str(), _TRUNCATE);
  strncpy_s(ui.stgSecondParam, st.stgSecondParam.c_str(), _TRUNCATE);
  strncpy_s(ui.stgShockerIDs, st.stgShockerIDs.c_str(), _TRUNCATE);
  strncpy_s(ui.stgSerialPort, st.stgSerialPort.c_str(), _TRUNCATE);
  strncpy_s(ui.stgVrchatHost, st.stgVrchatHost.c_str(), _TRUNCATE);

  ui.stgUsePishock = st.stgUsePishock;
  ui.stgRandomOrSeq = st.stgRandomOrSeq;
  ui.stgBaseCooldown = st.stgBaseCooldown;
  ui.stgMaxCooldown = st.stgMaxCooldown;
  ui.stgCooldownFactor = st.stgCooldownFactor;
  ui.stgCooldownWindow = st.stgCooldownWindow;
  ui.stgNotifEnabled = st.stgNotifEnabled;
  ui.stgNotifUseOvr = st.stgNotifUseOvr;
  ui.stgUseSerial = st.stgUseSerial;
  strncpy_s(ui.stgPishockUser, st.stgPishockUser.c_str(), _TRUNCATE);
  strncpy_s(ui.stgPishockKey, st.stgPishockKey.c_str(), _TRUNCATE);
  strncpy_s(ui.stgOpenshockToken, st.stgOpenshockToken.c_str(), _TRUNCATE);
  strncpy_s(ui.stgOpenshockServer, st.stgOpenshockServer.c_str(), _TRUNCATE);
  ui.stgPresetCount = st.stgPresetCount;
  ui.stgTouchThreshold = st.stgTouchThreshold;
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

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM,
                                                             LPARAM);

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_HOTKEY && wp == 1) {
    if (g_hub) {
      g_hub->shocksDisabled = true;
      logMsg("[Hotkey] Shocks disabled.");
    }
  }
  if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
  if (msg == WM_SIZE && g_pSwapChain) {
    destroyRenderTargetView();
    g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lp), (UINT)HIWORD(lp),
                                DXGI_FORMAT_UNKNOWN, 0);
    createRenderTargetView();
  }
  if (msg == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool initializeD3D11(HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 2;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  D3D_FEATURE_LEVEL fl;
  HRESULT hr = D3D11CreateDeviceAndSwapChain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
      D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fl,
      &g_pd3dContext);
  if (FAILED(hr)) return false;
  createRenderTargetView();
  return true;
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

// Button to import old config from
inline bool importLegacyPythonConfig(Settings& settings, ShockerHub& hub,
                                     float& minDur, float& maxDur,
                                     float& xViewMin, float& xViewMax,
                                     const std::string& settingsPath) {
  // Folder picker
  char folderPath[MAX_PATH] = {};
  BROWSEINFOA bi{};
  bi.hwndOwner = g_hwnd;
  bi.pszDisplayName = folderPath;
  bi.lpszTitle = "Select your Python ShockerLink folder";
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
  if (!pidl) return false;
  SHGetPathFromIDListA(pidl, folderPath);
  CoTaskMemFree(pidl);

  std::string folder = std::string(folderPath) + "\\";

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
        settings.presets[i] = p;
      }

      settings.defaultPreset = j.value("default_preset", -1);
      if (settings.defaultPreset >= 0 &&
          settings.defaultPreset < (int)settings.presets.size() &&
          settings.presets[settings.defaultPreset].has_value()) {
        auto& dp = settings.presets[settings.defaultPreset];
        minDur = settings.minShockDuration = dp->minShockDuration;
        maxDur = settings.maxShockDuration = dp->maxShockDuration;
        hub.curvePoints = dp->curvePoints;
        xViewMin = settings.xViewMin = dp->xViewMin;
        xViewMax = settings.xViewMax = dp->xViewMax;
      }
      logMsg("Imported curve_settings.json");
    } else {
      logMsg("curve_settings.json not found in selected folder, skipping");
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
        sscanf_s(h.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
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

      // Handle both old single-ID keys and new array key
      settings.shockerIDs.clear();
      if (c["SHOCKER_IDS"]) {
        for (auto id : c["SHOCKER_IDS"])
          settings.shockerIDs.push_back(std::to_string(id.as<int>()));
      } else {
        // Old Python format: pishock wins if present
        std::string id;
        if (c["PISHOCK_SHOCKER_ID"])
          id = c["PISHOCK_SHOCKER_ID"].as<std::string>("");
        else if (c["OPENSHOCK_SHOCKER_ID"])
          id = c["OPENSHOCK_SHOCKER_ID"].as<std::string>("");
        if (!id.empty()) settings.shockerIDs.push_back(id);
      }
      if (settings.shockerIDs.empty()) settings.shockerIDs = {"41838"};

      // Migrate old notification booleans
      bool oldXs = c["XSOVERLAY_NOTIFICATIONS"].as<bool>(false);
      bool oldOvr = c["OVRTOOLKIT_NOTIFICATIONS"].as<bool>(false);
      settings.notificationsEnabled = oldXs || oldOvr;
      settings.notifUseOvrToolkit = oldOvr;

      logMsg("Imported config.yml");
    } else {
      logMsg("config.yml not found in selected folder, skipping");
    }
  } catch (std::exception& e) {
    logMsg("config.yml import failed: {}", e.what());
  }

  settings.save(settingsPath);
  return true;
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

  // Accent color variants
  ImVec4 a = settings.accentColor;
  ImVec4 aH = {a.x * 1.2f, a.y * 1.2f, a.z * 1.2f, a.w};  // Hovered
  ImVec4 aA = {a.x * 0.8f, a.y * 0.8f, a.z * 0.8f, a.w};  // Active/pressed
  ImVec4 aD = {a.x * 0.5f, a.y * 0.5f, a.z * 0.5f, a.w};  // Dim (frame bg)

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
  ImPlot::GetStyle().Colors[ImPlotCol_LegendBg] =
      ImVec4(bgColor.x, bgColor.y, bgColor.z, 0.84f);
}

static void registerPanicHotkey(const Settings& settings) {
  UnregisterHotKey(g_hwnd, 1);
  if (settings.hotkeyVk != 0)
    RegisterHotKey(g_hwnd, 1, settings.hotkeyMods | MOD_NOREPEAT,
                   settings.hotkeyVk);
}

inline std::string formatKeyNameFromVk(int vk, int mods) {
  std::string s;
  if (mods & MOD_CONTROL) s += "Ctrl+";
  if (mods & MOD_ALT) s += "Alt+";
  if (mods & MOD_SHIFT) s += "Shift+";
  char buf[32] = {};
  UINT sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
  GetKeyNameTextA(sc << 16, buf, sizeof(buf));
  s += buf[0] ? buf : "?";
  return s;
}

// UI entry point
inline void runUI(Settings& settings, ShockerHub& hub,
                  const std::string& settingsPath) {
  HICON hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(1));
  WNDCLASSEXW wc{sizeof(wc),
                 CS_CLASSDC,
                 WndProc,
                 0,
                 0,
                 GetModuleHandle(nullptr),
                 hIcon,
                 nullptr,
                 nullptr,
                 nullptr,
                 L"ShockerLink",
                 hIcon};
  ImGui_ImplWin32_EnableDpiAwareness();
  RegisterClassExW(&wc);

  g_hwnd =
      CreateWindowW(wc.lpszClassName, kWindowTitle, WS_OVERLAPPEDWINDOW,
                    settings.windowX, settings.windowY, settings.windowW,
                    settings.windowH, nullptr, nullptr, wc.hInstance, nullptr);

  g_hub = &hub;

  BOOL dark = TRUE;
  DwmSetWindowAttribute(g_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                        sizeof(dark));

  SendMessage(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
  SendMessage(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

  if (!initializeD3D11(g_hwnd)) return;

  ShowWindow(g_hwnd, SW_SHOWDEFAULT);
  UpdateWindow(g_hwnd);
  registerPanicHotkey(settings);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  ImGuiIO& io = ImGui::GetIO();

  io.IniFilename = nullptr;

  io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 18.0f);
  // Merge symbol font for ⚡ (U+26A1)
  {
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.GlyphOffset = {0, 0.f};
    static const ImWchar ranges[] = {0x2600, 0x27FF, 0};
    io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/seguisym.ttf", 18.0f, &cfg,
                                 ranges);
  }
  ImFont* boldFont =
      io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeuib.ttf", 18.0f);

  ImVec4& bgColor = settings.outsideCurveBg;
  ImPlot::GetStyle().Colors[ImPlotCol_LegendBg] =
      ImVec4(bgColor.x, bgColor.y, bgColor.z, 0.84f);
  ImPlot::GetStyle().Colors[ImPlotCol_LegendBorder] =
      ImVec4(0.4f, 0.4f, 0.5f, 0.8f);
  ImPlot::GetStyle().LegendPadding = ImVec2(10, 8);
  ImPlot::GetStyle().LegendInnerPadding = ImVec2(6, 4);
  ImPlot::GetStyle().LegendSpacing = ImVec2(6, 4);

  ImGui::StyleColorsDark();
  applyUiTheme(settings);

  // Apply background colors
  ImGuiStyle& style = ImGui::GetStyle();
  style.Colors[ImGuiCol_WindowBg] = settings.backgroundColor;
  style.Colors[ImGuiCol_ChildBg] = settings.backgroundColor;
  style.Colors[ImGuiCol_Text] = settings.labelColor;

  ImGui_ImplWin32_Init(g_hwnd);
  ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dContext);

  // Dynamic UI state
  float minDur = (float)settings.minShockDuration;
  float maxDur = (float)settings.maxShockDuration;
  bool cooldownEnabled = settings.cooldownEnabled;

  float xViewMin = settings.xViewMin;
  float xViewMax = settings.xViewMax;

  std::deque<AppState> undoStack;
  std::deque<AppState> redoStack;
  bool isPerformingUndoRedo = false;

  bool ctrlZPrev = false;
  bool ctrlYPrev = false;
  bool stateChangedPreviousFrame = false;

  // Apply default preset if set
  if (settings.defaultPreset >= 0 &&
      settings.defaultPreset < (int)settings.presets.size() &&
      settings.presets[settings.defaultPreset].has_value()) {
    auto& p = settings.presets[settings.defaultPreset];
    minDur = p->minShockDuration;
    maxDur = p->maxShockDuration;
    hub.curvePoints = p->curvePoints;
    xViewMin = p->xViewMin;
    xViewMax = p->xViewMax;
  }

  // Settings modal state
  bool showSettings = false;
  float settingsAnim = 0.f;

  // Stats modal state
  float statsAnim = 0.f;
  if (settings.showStats) {
    // Start with animation fully open
    statsAnim = 1.f;
    // Make sure the window width is correct
    int sw = (int)(statsAnim * 280);
    SetWindowPos(g_hwnd, nullptr, settings.windowX - sw, settings.windowY,
                 settings.windowW + sw + (int)(settingsAnim * 550),
                 settings.windowH, SWP_NOZORDER);
  }

  // Editable staging copies (only written back on Save)
  char stgShockParam[64] = {};
  char stgSecondParam[64] = {};
  char stgShockerIDs[256] = {};
  char stgSerialPort[64] = {};
  char stgVrchatHost[64] = {};
  bool stgUsePishock = false;
  bool stgRandomOrSeq = false;
  int stgBaseCooldown = 2;
  int stgMaxCooldown = 6;
  float stgCooldownFactor = 0.4f;
  int stgCooldownWindow = 30;
  bool stgNotifEnabled = false;
  bool stgNotifUseOvr = false;
  bool stgUseSerial = true;
  char stgPishockUser[128] = {};
  char stgPishockKey[128] = {};
  char stgOpenshockToken[256] = {};
  char stgOpenshockServer[128] = {};
  int stgPresetCount = 3;
  float stgTouchThreshold = 8.f;

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
               stgPishockUser,
               stgPishockKey,
               stgOpenshockToken,
               stgOpenshockServer,
               stgPresetCount,
               stgTouchThreshold};

  AppState lastCommittedState = snapshotAppState(ui);

  int baseClientW = (int)ImGui::GetIO().DisplaySize.x;
  // Style copies apply live on edit, so point directly at settings fields
  auto openSettingsModal = [&]() {
    baseClientW = (int)ImGui::GetIO().DisplaySize.x;

    strncpy_s(stgShockParam, settings.shockParameter.c_str(),
              sizeof(stgShockParam) - 1);
    strncpy_s(stgSecondParam, settings.secondShockParameter.c_str(),
              sizeof(stgSecondParam) - 1);
    strncpy_s(stgSerialPort, settings.serialPort.c_str(),
              sizeof(stgSerialPort) - 1);
    strncpy_s(stgVrchatHost, settings.vrchatHost.c_str(),
              sizeof(stgVrchatHost) - 1);
    std::string ids;
    for (int i = 0; i < (int)settings.shockerIDs.size(); i++)
      ids += (i ? ", " : "") + settings.shockerIDs[i];
    strncpy_s(stgShockerIDs, ids.c_str(), sizeof(stgShockerIDs) - 1);
    stgUsePishock = settings.usePishock;
    stgRandomOrSeq = settings.randomOrSeq;
    stgBaseCooldown = settings.baseCooldown;
    stgMaxCooldown = settings.maxCooldown;
    stgCooldownFactor = settings.cooldownFactor;
    stgCooldownWindow = settings.cooldownWindow;
    stgNotifEnabled = settings.notificationsEnabled;
    stgNotifUseOvr = settings.notifUseOvrToolkit;
    stgUseSerial = settings.useSerial;
    strncpy_s(stgPishockUser, settings.pishockUsername.c_str(),
              sizeof(stgPishockUser) - 1);
    strncpy_s(stgPishockKey, settings.pishockApiKey.c_str(),
              sizeof(stgPishockKey) - 1);
    strncpy_s(stgOpenshockToken, settings.openshockApiToken.c_str(),
              sizeof(stgOpenshockToken) - 1);
    strncpy_s(stgOpenshockServer, settings.openshockServerUrl.c_str(),
              sizeof(stgOpenshockServer) - 1);
    stgPresetCount = settings.presetCount;
    stgTouchThreshold = settings.touchSelectThreshold;
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

  std::array<CurvePoint, 3>& pts = hub.curvePoints;

  // Curve cache
  std::array<CurvePoint, 3> lastPts = {};
  std::vector<double> cx, cy;

  ImVec4& clear = settings.backgroundColor;

  bool forceFrame = true;
  MSG msg{};
  while (msg.message != WM_QUIT) {
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
      continue;
    }

    bool minimized = IsIconic(g_hwnd);

    if (minimized) {
      // If minimized don't render
      WaitMessage();
      continue;
    }

    bool focused = GetForegroundWindow() == g_hwnd || showSettings;
    bool cooldownActive = settings.cooldownEnabled &&
                          hub.cooldownUntil.load() > hub.getCurrentTime();
    bool statsAnimating =
        fabs(statsAnim - (settings.showStats ? 1.f : 0.f)) > 0.001f;
    bool settAnimating =
        fabs(settingsAnim - (showSettings ? 1.f : 0.f)) > 0.001f;
    bool needsAnimation = cooldownActive || !hub.isConnected ||
                          statsAnimating || settAnimating || forceFrame;

    if (!forceFrame) {
      if (!needsAnimation && !focused) {
        // Fully idle -- nothing to animate and nobody watching
        MsgWaitForMultipleObjects(0, nullptr, FALSE, INFINITE, QS_ALLINPUT);
      } else if (!needsAnimation && focused) {
        // Focused but static -- render on input only
        MsgWaitForMultipleObjects(0, nullptr, FALSE, INFINITE, QS_ALLINPUT);
      } else {
        // Animation needed -- cap to target fps, wake early on input
        int frameMs = focused ? (1000 / 60) : (1000 / 16);
        MsgWaitForMultipleObjects(0, nullptr, FALSE, frameMs, QS_ALLINPUT);
      }
    }

    RECT wr;
    if (GetWindowRect(g_hwnd, &wr)) {
      // Only update the logical base when fully idle - no panels animating.
      // While animating, reading back and subtracting offsets causes a
      // rounding feedback loop that makes the window vibrate.
      bool fullyIdle = (statsAnim == 0.f && settingsAnim == 0.f);
      if (fullyIdle) {
        settings.windowX = wr.left;
        settings.windowW = wr.right - wr.left;
      }
      settings.windowY = wr.top;
      settings.windowH = wr.bottom - wr.top;
    }

    // Animate panels and reposition window before layout so they stay in sync
    {
      float settTarget = showSettings ? 1.f : 0.f;
      float statsTarget = settings.showStats ? 1.f : 0.f;

      float prevSettAnim = settingsAnim;
      float prevStatsAnim = statsAnim;

      settingsAnim +=
          (settTarget - settingsAnim) * std::min(1.f, io.DeltaTime * 12.f);
      statsAnim +=
          (statsTarget - statsAnim) * std::min(1.f, io.DeltaTime * 12.f);
      if (settingsAnim < 0.001f) settingsAnim = 0.f;
      if (statsAnim < 0.043f) statsAnim = 0.f;

      if (fabs(statsAnim - prevStatsAnim) > 0.001f ||
          fabs(settingsAnim - prevSettAnim) > 0.001f) {
        int sw = (int)roundf(statsAnim * 280.f);
        int settW = (int)roundf(settingsAnim * 550.f);
        SetWindowPos(g_hwnd, nullptr, settings.windowX - sw, settings.windowY,
                     settings.windowW + sw + settW, settings.windowH,
                     SWP_NOZORDER);
      }

      if ((settingsAnim == 0.f && prevSettAnim > 0.f) ||
          (statsAnim == 0.f && prevStatsAnim > 0.f))
        forceFrame = true;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    applyUiTheme(settings);
    // statsW is the current animated left-panel width; used throughout the
    // frame
    float statsW = statsAnim * 280.f;
    ImGui::SetNextWindowPos({statsW, 0});
    ImGui::SetNextWindowSize(
        {ImGui::GetIO().DisplaySize.x - statsW, ImGui::GetIO().DisplaySize.y});
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Compute panel width from font size so it scales with DPI
    float fontSize = ImGui::GetFontSize();
    float panelW = fontSize * 10.f;

    // Left panel
    float lineH = ImGui::GetTextLineHeightWithSpacing();
    float logH = lineH * 3.f + ImGui::GetStyle().WindowPadding.y * 2.f;
    float rowH = ImGui::GetTextLineHeightWithSpacing() + 3.f;
    float sepH = 1.f + ImGui::GetStyle().ItemSpacing.y * 2.f;
    float bottomH = rowH + logH + sepH;

    ImGui::BeginChild("##controls", {panelW, -bottomH}, true);
    // Connected Icon
    ImGui::SetCursorPosY(ImGui::GetStyle().WindowPadding.y * 0.5f);

    ImGui::Spacing();
    ImGui::Text("Min Duration (s)");
    ImGui::SliderFloat("##mind", &minDur, 0.1f, 5.f, "%.1f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      minDur = std::min(minDur, maxDur - 0.1f);
      settings.minShockDuration = minDur;
    }

    ImGui::SliderFloat("##maxd", &maxDur, 0.1f, 5.f, "%.1f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      maxDur = std::max(maxDur, minDur + 0.1f);
      settings.maxShockDuration = maxDur;
    }

    ImGui::Spacing();
    if (ImGui::Checkbox("Enable Cooldown", &cooldownEnabled)) {
      settings.cooldownEnabled = cooldownEnabled;
    }

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

      ImGui::Button(label.c_str(), {panelW - fontSize * 2.5f, 0});
      ImGui::SetItemTooltip(
          "LClick - Load\nMClick - Startup default\nRClick - Rename");

      if (isDefault) ImGui::PopStyleColor();

      // left click - load
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && hasData) {
        minDur = settings.presets[i]->minShockDuration;
        maxDur = settings.presets[i]->maxShockDuration;
        settings.minShockDuration = minDur;
        settings.maxShockDuration = maxDur;
        hub.curvePoints = settings.presets[i]->curvePoints;
        xViewMin = settings.presets[i]->xViewMin;
        xViewMax = settings.presets[i]->xViewMax;
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
          strncpy_s(nameBuf, label.c_str(), sizeof(nameBuf) - 1);
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
      if (drawSaveIconButton(("##save" + std::to_string(i)).c_str())) {
        Preset p;
        p.name = label;
        p.minShockDuration = minDur;
        p.maxShockDuration = maxDur;
        p.curvePoints = hub.curvePoints;
        p.xViewMin = xViewMin;
        p.xViewMax = xViewMax;
        settings.presets[i] = p;
        settings.save(settingsPath);
      }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Test Vibrate", {77.5f, 0})) hub.queueShock(-1, true);
    ImGui::SetItemTooltip("Sends a vibration command");
    ImGui::SameLine();
    if (ImGui::Button("Test Shock", {-1, 0})) hub.queueShock(-1, false);
    ImGui::SetItemTooltip("Sends a Shock command");

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() -
                         ImGui::GetFrameHeightWithSpacing() -
                         ImGui::GetStyle().WindowPadding.y);

    // Set cursor position based on buttons being visible or not
    // Prevents Stats/Settings buttons being pushed down
    {
      int extraRows = (!hub.isConnected ? 1 : 0) + (hub.shocksDisabled ? 1 : 0);
      float totalBtnsH = (1 + extraRows) * ImGui::GetFrameHeightWithSpacing() +
                         ImGui::GetStyle().WindowPadding.y;
      ImGui::SetCursorPosY(ImGui::GetWindowHeight() - totalBtnsH);
    }

    if (!hub.isConnected) {
      if (ImGui::Button("Retry Connection", {-1, 0})) hub.tryReconnect();
    }
    if (hub.shocksDisabled) {
      if (ImGui::Button("Enable Shocks", {-1, 0})) hub.enableShocks();
    }

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
      } else {
        closeSettingsModal();
      }
    }

    if (showSettings) {
      ImGui::SetItemTooltip("Will close settings without saving.");
    }

    ImGui::EndChild();

    // Curve editor
    ImGui::SameLine();
    // We are not using GetContentRegionAvail() otherwise the sliding animation
    // for the settings menu breaks
    ImGui::BeginChild("##plot", {settings.windowW - 220.f, -bottomH}, false);

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
      ImVec2 text_size = boldFont->CalcTextSizeA(18.0f, FLT_MAX, 0.0f, title);

      ImVec2 pos;
      pos.x = plot_pos.x + (plot_size.x - text_size.x) * 0.5f;
      pos.y = plot_pos.y - ImGui::GetTextLineHeight() - 4;
      dl->AddText(boldFont, 18.0f, pos,
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

        // Legend entries
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

        // Curve
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

    ImVec2 sc = ImGui::GetCursorScreenPos();
    float gap = ImGui::GetStyle().ItemSpacing.y;
    float sliderRowH = ImGui::GetFrameHeight() + 4;
    float border = 1.f;
    ImGui::GetWindowDrawList()->AddRectFilled(
        {plotFramePos.x, sc.y - gap},
        {plotFramePos.x + plotFrameWidth - border, sc.y + sliderRowH},
        ImGui::ColorConvertFloat4ToU32(settings.outsideCurveBg));
    ImGui::GetWindowDrawList()->AddText(
        {plotFramePos.x + 6,
         sc.y + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f +
             2},
        ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text]),
        "X Scale");
    ImGui::SetCursorScreenPos({savedPlotPos.x, sc.y + 2});
    drawRangeSliderFloat("##xrange", &xViewMin, &xViewMax, 0.f, 100.f,
                         savedPlotSize.x);
    ImGui::SetCursorScreenPos(
        {plotFramePos.x + plotFrameWidth, sc.y + sliderRowH});

    ImGui::EndChild();

    // Log bar
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - rowH - logH - sepH -
                         ImGui::GetStyle().WindowPadding.y);  // CHANGED
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
      // Align to bottom
      ImGui::SetCursorPosY(ImGui::GetWindowHeight() - rowH);

      float cy = ImGui::GetCursorScreenPos().y +
                 ImGui::GetTextLineHeight() * 0.5f + 2.f;
      float cx = ImGui::GetCursorScreenPos().x;
      ImDrawList* dl = ImGui::GetWindowDrawList();

      // Connection circle
      ImU32 connCol = hub.isConnected ? IM_COL32(60, 220, 80, 255)
                                      : IM_COL32(220, 60, 60, 255);
      dl->AddCircleFilled({cx + 7.f, cy}, 5.f, connCol);
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 17.f);
      if (!hub.shocksDisabled) {
        ImGui::Text(hub.isConnected ? "Connected" : "Disconnected");
      } else {
        ImGui::TextColored({1.f, 0.3f, 0.3f, 1.f}, "SHOCKS DISABLED");
      }

      ImGui::SameLine(0, 12);
      ImGui::TextDisabled("|");
      ImGui::SameLine(0, 12);

      // Shock counter
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
      }

      ImGui::SameLine(0, 8);

      // Cooldown Bar
      if (settings.cooldownEnabled) {
        double remaining =
            std::max(0.0, hub.cooldownUntil.load() - hub.getCurrentTime());
        double maxCd = settings.maxCooldown;
        float fraction = (float)(remaining / maxCd);

        float height = 3.f;
        float textH = ImGui::GetTextLineHeight();
        ImVec2 p = ImGui::GetCursorScreenPos();
        p.y += (textH - height) * 0.7f;
        float len = ImGui::GetContentRegionAvail().x * 0.28;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, {p.x + len, p.y + height},
                          ImGui::ColorConvertFloat4ToU32(
                              ImGui::GetStyle().Colors[ImGuiCol_FrameBg]));
        if (fraction > 0.f)
          dl->AddRectFilled(p, {p.x + len * fraction, p.y + height},
                            IM_COL32(220, 80, 80, 255));
        ImGui::Dummy({len, height});
      }

      // Right-align: version
      float verW = ImGui::CalcTextSize("v" APP_VERSION).x;
      ImGui::SameLine(ImGui::GetContentRegionMax().x - verW);
      ImGui::TextDisabled("v" APP_VERSION);
    }

    ImGui::End();

    if (updateReady.exchange(false)) Updater::applyAndRestart(g_hwnd);

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

    if (showSettings) {
      float panelW = settingsAnim * 542.f;
      ImGui::SetNextWindowSize({panelW, (float)settings.windowH - 40},
                               ImGuiCond_Always);
      ImGui::SetNextWindowPos({statsW + (float)settings.windowW, 0.f},
                              ImGuiCond_Always);
      ImGui::Begin("Settings", &showSettings,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoTitleBar);

      ImGui::BeginChild("##settingsscroll", {0, -40}, false);
      ImGui::TextDisabled(
          "(?) Hover over settings for details | CTRL+Z/CTRL+Y to Undo/Redo");
      ImGui::Spacing();

      // OSC / Avatar
      ImGui::SeparatorText("OSC / Avatar");
      ImGui::InputText("Shock Parameter##s", stgShockParam,
                       sizeof(stgShockParam));
      ImGui::SetItemTooltip(
          "Input the parameter name you want to use for the shock (for example "
          "for touches)\nThis is the parameter you set in unity");
      ImGui::InputText("Second Shock Parameter##s", stgSecondParam,
                       sizeof(stgSecondParam));
      ImGui::SetItemTooltip(
          "Optional second parameter for stronger shocks.\nTakes only the "
          "second half of the curve into account (for example for slaps)");
      ImGui::TextDisabled("Changes here require a restart");

      ImGui::Spacing();

      // Hardware
      ImGui::SeparatorText("Hardware");

      // Backend toggle: OpenShock | PiShock
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
        // Serial mode fields
        // Shocker IDs label stays the same for serial
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
        // API mode fields
        if (stgUsePishock) {
          // PiShock API
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
          // OpenShock API
          ImGui::InputText("API Token##ost", stgOpenshockToken,
                           sizeof(stgOpenshockToken),
                           ImGuiInputTextFlags_Password);
          ImGui::SetItemTooltip(
              "Your OpenShock API token.\n"
              "Create one at your OpenShock dashboard under API Tokens.");
          ImGui::InputText("Server URL##oss", stgOpenshockServer,
                           sizeof(stgOpenshockServer));
          ImGui::SetItemTooltip(
              "OpenShock server hostname (without https://).\n"
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

      // Notifications
      ImGui::SeparatorText("Notifications");
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

      ImGui::Spacing();

      // Panic button
      ImGui::SeparatorText("Hotkey");
      ImGui::TextDisabled("Panic button:");
      ImGui::SameLine();

      std::string keyLabel =
          capturingHotkey
              ? "Press any key..."
              : (settings.hotkeyVk ? formatKeyNameFromVk(settings.hotkeyVk,
                                                         settings.hotkeyMods)
                                   : "None");
      if (ImGui::Button(keyLabel.c_str(), {160, 0})) capturingHotkey = true;
      ImGui::SetItemTooltip("Hotkey to disable shocks\nWorks anywhere");
      ImGui::SameLine();
      if (ImGui::Button("Clear##hk")) {
        settings.hotkeyVk = 0;
        settings.hotkeyMods = 0;
        UnregisterHotKey(g_hwnd, 1);
      }
      ImGui::SetItemTooltip("Clear the button (disables hotkey)");

      if (capturingHotkey) {
        int mods = 0;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
        if (GetAsyncKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;

        for (int vk = VK_F1; vk <= VK_F24; vk++) {
          if (GetAsyncKeyState(vk) & 0x8000) {
            settings.hotkeyVk = vk;
            settings.hotkeyMods = mods;
            capturingHotkey = false;
            registerPanicHotkey(settings);
            break;
          }
        }
        for (int vk = 0x30; vk <= 0x5A; vk++) {
          if (GetAsyncKeyState(vk) & 0x8000) {
            settings.hotkeyVk = vk;
            settings.hotkeyMods = mods;
            capturingHotkey = false;
            registerPanicHotkey(settings);
            break;
          }
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) capturingHotkey = false;
      }

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

      // Increase luminance to make the color pickers always visible
      auto liftMinLum = [](ImVec4 c, float minLum) {
        float lum = c.x * 0.299f + c.y * 0.587f + c.z * 0.114f;
        if (lum < minLum) {
          float scale = minLum / (lum + 1e-5f);
          c.x *= scale;
          c.y *= scale;
          c.z *= scale;
        }
        return ImVec4(std::min(c.x, 1.0f), std::min(c.y, 1.0f),
                      std::min(c.z, 1.0f), c.w);
      };

      ImVec4 base = liftMinLum(settings.accentColor, 0.35f);

      ImGui::PushStyleColor(
          ImGuiCol_FrameBg,
          ImVec4(base.x * 1.25f, base.y * 1.25f, base.z * 1.25f, 1.0f));

      // Color pickers
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

      // Network
      ImGui::SeparatorText("Network");
      ImGui::InputText("VRChat Host##s", stgVrchatHost, sizeof(stgVrchatHost));
      ImGui::SetItemTooltip("Usually doesn't need a change.");

      ImGui::Spacing();
      ImGui::Separator();

      ImGui::Spacing();
      if (ImGui::Button("Import Python cfg", {ImGui::CalcItemWidth(), 0}))
        importLegacyPythonConfig(settings, hub, minDur, maxDur, xViewMin,
                                 xViewMax, settingsPath);
      ImGui::SetItemTooltip(
          "Select the folder of your python installation.\nUseless for most, "
          "imports config from the old python build.");
      ImGui::SameLine();
      ImGui::Text("Import old python config");
      ImGui::Spacing();

      ImGui::EndChild();

      ImGui::Separator();
      std::string currentIDs;
      for (int i = 0; i < (int)settings.shockerIDs.size(); i++)
        currentIDs += (i ? ", " : "") + settings.shockerIDs[i];
      bool needsRestart = settings.shockParameter != stgShockParam ||
                          settings.secondShockParameter != stgSecondParam ||
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
          PostMessage(g_hwnd, WM_CLOSE, 0, 0);
        }
      } else {
        if (ImGui::Button("Save", {80, 0})) {
          commitAll();
          registerPanicHotkey(settings);
          showSettings = false;
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", {80, 0})) {
        closeSettingsModal();
      }
      ImGui::SetItemTooltip(
          "Will close settings without saving\nTheme settings will be "
          "reverted");

      ImGui::End();
    }

    // Stats panel (slides out to the left)
    if (statsAnim > 0.43) {
      ImGui::SetNextWindowPos({0.f, 0.f}, ImGuiCond_Always);
      ImGui::SetNextWindowSize({statsW, (float)settings.windowH},
                               ImGuiCond_Always);
      ImGui::Begin("##statspanel", nullptr,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoTitleBar |
                       ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoScrollWithMouse);

      // Fade the text in so it doesn't clip weirdly during the slide
      float alpha = std::min(1.f, std::max(0.f, (statsW - 60.f) / 160.f));
      ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

      // Header row
      if (boldFont) ImGui::PushFont(boldFont);
      ImGui::Text("Statistics");
      if (boldFont) ImGui::PopFont();
      ImGui::SameLine(statsW - 36.f);
      if (ImGui::SmallButton("X##sc")) settings.showStats = false;
      ImGui::Separator();

      // Scrollable body
      ImGui::BeginChild("##statsscroll",
                        {0, -ImGui::GetFrameHeightWithSpacing() - 6}, false);

      // Two-column row helper
      auto row = [&](const char* label, const std::string& val) {
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine(112.f);
        ImGui::TextUnformatted(val.c_str());
      };

      // Duration formatter
      auto fmtMs = [](double ms) -> std::string {
        int ts = (int)(ms / 1000.0);
        if (ts < 60) return fmt::format("{:.1f}s", ms / 1000.0);
        int m = ts / 60, s = ts % 60;
        if (m < 60) return fmt::format("{}m {:02d}s", m, s);
        int h = m / 60;
        m %= 60;
        return fmt::format("{}h {:02d}m", h, m);
      };

      // All-Time
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

      // This Session
      ImGui::Spacing();
      ImGui::SeparatorText("This Session");
      row("Shocks", std::to_string(gStats.sessionShocks));
      row("Vibrations", std::to_string(gStats.sessionVibrations));
      row("Shock time", fmtMs(gStats.sessionShockDurationMs));
      row("Cooldown blocks", std::to_string(gStats.sessionCooldownHits));

      // Records
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

      // Last 7 Days bar chart
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

          // Days of the month
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

          // Smart vertical ticks
          static std::vector<double> yticks;
          yticks.clear();

          double max = *std::max_element(vals, vals + 7);
          int max_i = (int)std::ceil(max);
          // 4 vertical ticks max, will show 5 due to 0 not being counted
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

      // Footer: reset button
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

    const bool editingText = io.WantTextInput;
    bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool zDown = (GetAsyncKeyState('Z') & 0x8000) != 0;
    bool yDown = (GetAsyncKeyState('Y') & 0x8000) != 0;
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
      if (isEditingThisFrame && !stateChangedPreviousFrame) {
        pushUndoSnapshot(undoStack, redoStack, lastCommittedState, false);
      }
      lastCommittedState = currentState;
    } else {
      lastCommittedState = currentState;
    }

    stateChangedPreviousFrame = isEditingThisFrame;

    // Render
    ImGui::Render();
    g_pd3dContext->OMSetRenderTargets(1, &g_mainRTV, nullptr);
    g_pd3dContext->ClearRenderTargetView(g_mainRTV, (float*)&clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pd3dContext->OMSetRenderTargets(0, nullptr, nullptr);
    g_pSwapChain->Present(focused ? 1 : 0, 0);
    forceFrame = false;
  }

  // Cleanup
  settings.minShockDuration = minDur;
  settings.maxShockDuration = maxDur;
  settings.xViewMin = xViewMin;
  settings.xViewMax = xViewMax;

  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();
  destroyRenderTargetView();
  if (g_pSwapChain) g_pSwapChain->Release();
  if (g_pd3dContext) g_pd3dContext->Release();
  if (g_pd3dDevice) g_pd3dDevice->Release();
  DestroyWindow(g_hwnd);
  UnregisterClassW(wc.lpszClassName, wc.hInstance);
}