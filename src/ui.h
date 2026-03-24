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
#include "updater.h"

static constexpr wchar_t kWindowTitle[] = L"Shocker Link v" APP_VERSION_W;

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRTV = nullptr;
static HWND g_hwnd = nullptr;

static ShockerHub* g_hub = nullptr;

extern std::atomic<bool> shouldRestart;

static void CreateRTV() {
  ID3D11Texture2D* buf = nullptr;
  g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&buf));
  g_pd3dDevice->CreateRenderTargetView(buf, nullptr, &g_mainRTV);
  buf->Release();
}
static void CleanupRTV() {
  if (g_mainRTV) {
    g_mainRTV->Release();
    g_mainRTV = nullptr;
  }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM,
                                                             LPARAM);

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_HOTKEY && wp == 1) {
    if (g_hub) {
      g_hub->shocksDisabled = true;
      logMsg("[Hotkey] Shocks disabled until restart.");
    }
  }
  if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
  if (msg == WM_SIZE && g_pSwapChain) {
    CleanupRTV();
    g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lp), (UINT)HIWORD(lp),
                                DXGI_FORMAT_UNKNOWN, 0);
    CreateRTV();
  }
  if (msg == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool InitD3D(HWND hwnd) {
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
  CreateRTV();
  return true;
}

// Save button icon
static bool SaveButton(const char* id) {
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
inline bool importPythonConfig(Settings& settings, ShockerHub& hub,
                               float& minDur, float& maxDur, float& xViewMin,
                               float& xViewMax,
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

inline bool RangeSliderFloat(const char* id, float* vMin, float* vMax,
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

inline void applyTheme(Settings& settings) {
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

static void RegisterAppHotkey(const Settings& settings) {
  UnregisterHotKey(g_hwnd, 1);
  if (settings.hotkeyVk != 0)
    RegisterHotKey(g_hwnd, 1, settings.hotkeyMods | MOD_NOREPEAT,
                   settings.hotkeyVk);
}

inline std::string keyNameFromVk(int vk, int mods) {
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
inline void ui_run(Settings& settings, ShockerHub& hub,
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

  if (!InitD3D(g_hwnd)) return;

  ShowWindow(g_hwnd, SW_SHOWDEFAULT);
  UpdateWindow(g_hwnd);
  RegisterAppHotkey(settings);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  ImGuiIO& io = ImGui::GetIO();

  io.IniFilename = nullptr;

  io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 18.0f);
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
  applyTheme(settings);

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
  bool stgXsoverlay = false;
  bool stgOvrToolkit = false;
  int stgPresetCount = 3;
  float stgTouchThreshold = 8.f;
  float stgMarkerSize = 140.f;
  float stgLineWidth = 3.f;

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
    stgXsoverlay = settings.xsoverlayNotifications;
    stgOvrToolkit = settings.ovrToolkitNotifications;
    stgPresetCount = settings.presetCount;
    stgTouchThreshold = settings.touchSelectThreshold;
    stgMarkerSize = settings.touchMarkerSize;
    stgLineWidth = settings.lineWidth;
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
    bool needsAnimation =
        cooldownActive || !hub.isConnected || settingsAnim > 0.0f || forceFrame;

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
      settings.windowX = wr.left;
      settings.windowY = wr.top;
      if (settingsAnim == 0.f) settings.windowW = wr.right - wr.left;
      settings.windowH = wr.bottom - wr.top;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    applyTheme(settings);
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Left panel
    ImGui::BeginChild("##controls", {180, -90}, true);
    // Connected Icon
    ImGui::SetCursorPosY(ImGui::GetStyle().WindowPadding.y * 0.5f);

    {
      ImVec2 p = ImGui::GetCursorScreenPos();
      float r = 5.f;
      ImU32 col = hub.isConnected ? IM_COL32(60, 220, 80, 255)
                                  : IM_COL32(220, 60, 60, 255);
      ImGui::GetWindowDrawList()->AddCircleFilled(
          {p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f}, r, col);
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + r * 2 + 6);
      if (!hub.shocksDisabled) {
        ImGui::Text(hub.isConnected ? "Connected" : "Disconnected");
      } else {
        ImGui::TextColored({1.f, 0.3f, 0.3f, 1.f}, "SHOCKS DISABLED");
      }
      if (!hub.isConnected) {
        if (ImGui::Button("Retry Connection", {-1, 0})) hub.tryReconnect();
      }
    }

    // Cooldown Bar
    if (settings.cooldownEnabled) {
      double remaining =
          std::max(0.0, hub.cooldownUntil.load() - hub.getCurrentTime());
      double maxCd = settings.maxCooldown;
      float fraction = (float)(remaining / maxCd);

      ImVec2 p = ImGui::GetCursorScreenPos();
      float w = ImGui::GetContentRegionAvail().x;
      float h = 3.f;
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(p, {p.x + w, p.y + h},
                        ImGui::ColorConvertFloat4ToU32(
                            ImGui::GetStyle().Colors[ImGuiCol_FrameBg]));
      if (fraction > 0.f)
        dl->AddRectFilled(p, {p.x + w * fraction, p.y + h},
                          IM_COL32(220, 80, 80, 255));
      ImGui::Dummy({w, h});
    }

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

      ImGui::Button(label.c_str(), {130, 0});

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
      if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
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
      if (SaveButton(("##save" + std::to_string(i)).c_str())) {
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

    if (ImGui::Button(
            "Test vibrate",
            {!settings.secondShockParameter.empty() ? 77.5f : -1.f, 0}))
      hub.queueShock(-1, true);
    ImGui::SetItemTooltip("Test first parameter\nSends a vibration command");

    if (!settings.secondShockParameter.empty()) {
      ImGui::SameLine();
      if (ImGui::Button("Test 2nd", {-1, 0})) hub.queueShockUpperHalf(-1, true);
      ImGui::SetItemTooltip("Test second parameter\nSends a vibration command");
    }

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() -
                         ImGui::GetFrameHeightWithSpacing() -
                         ImGui::GetStyle().WindowPadding.y);

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
    float plotW = settings.windowW - 220.f;
    ImGui::BeginChild("##plot", {plotW, -90}, false);

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
    RangeSliderFloat("##xrange", &xViewMin, &xViewMax, 0.f, 100.f,
                     savedPlotSize.x);
    ImGui::SetCursorScreenPos(
        {plotFramePos.x + plotFrameWidth, sc.y + sliderRowH});

    ImGui::EndChild();

    // Log bar
    ImGui::Separator();
    ImGui::BeginChild("##log", {0, 80}, false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    {
      std::lock_guard<std::mutex> lock(gLog.mtx);
      for (auto& line : gLog.lines) ImGui::TextUnformatted(line.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
      ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

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
      settings.xsoverlayNotifications = stgXsoverlay;
      settings.ovrToolkitNotifications = stgOvrToolkit;
      settings.presetCount = stgPresetCount;
      settings.touchSelectThreshold = stgTouchThreshold;
      settings.touchMarkerSize = stgMarkerSize;
      settings.lineWidth = stgLineWidth;
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

    {
      float target = showSettings ? 1.f : 0.f;
      float prevAnim = settingsAnim;
      settingsAnim +=
          (target - settingsAnim) * std::min(1.f, io.DeltaTime * 12.f);
      if (settingsAnim < 0.001f) settingsAnim = 0.f;
      SetWindowPos(g_hwnd, nullptr, settings.windowX, settings.windowY,
                   settings.windowW + (int)(settingsAnim * 550),
                   settings.windowH, SWP_NOZORDER | SWP_NOMOVE);
      if (settingsAnim == 0.f && prevAnim > 0.f) forceFrame = true;
    }

    if (showSettings) {
      float panelW = settingsAnim * 542.f;
      ImGui::SetNextWindowSize({panelW, (float)settings.windowH - 40},
                               ImGuiCond_Always);
      ImGui::SetNextWindowPos({(float)settings.windowW, 0.f}, ImGuiCond_Always);
      ImGui::Begin("Settings", &showSettings,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoTitleBar);

      ImGui::BeginChild("##settingsscroll", {0, -40}, false);
      ImGui::TextDisabled("(?) Hover over settings for details");
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
      // Pishock/OpenShock Toggle
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
      if (ImGui::Button("PiShock", {100, 0})) stgUsePishock = true;
      ImGui::PopStyleColor();
      ImGui::SameLine();
      ImGui::Text("Backend/Hub");

      ImGui::InputText("Shocker IDs (#, #, ...)##s", stgShockerIDs,
                       sizeof(stgShockerIDs));
      ImGui::SetItemTooltip(
          "Shocker IDs as found on the PiShock or OpenShock website.\nSeparate "
          "with comma");
      ImGui::Checkbox("Sequential shocker order (vs Random)", &stgRandomOrSeq);
      ImGui::SetItemTooltip(
          "If using multiple shockers, this option chooses between randomizing "
          "or using them sequentially\nNo for random // Yes for sequential");
      ImGui::InputTextWithHint("Serial Port##s", "(blank = auto)",
                               stgSerialPort, sizeof(stgSerialPort));
      ImGui::SetItemTooltip("Leave blank to auto-detect");
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
      ImGui::Checkbox("XSOverlay##s", &stgXsoverlay);
      ImGui::SetItemTooltip(
          "Sends a notification about the strength and duration as a "
          "notification to your headset");
      ImGui::Checkbox("OVRToolkit##s", &stgOvrToolkit);
      ImGui::SetItemTooltip(
          "Sends a notification about the strength and duration as a "
          "notification to your headset");

      ImGui::Spacing();

      // Panic button
      ImGui::SeparatorText("Hotkey");
      ImGui::TextDisabled("Panic button:");
      ImGui::SameLine();

      std::string keyLabel =
          capturingHotkey
              ? "Press any key..."
              : (settings.hotkeyVk
                     ? keyNameFromVk(settings.hotkeyVk, settings.hotkeyMods)
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
            RegisterAppHotkey(settings);
            break;
          }
        }
        for (int vk = 0x30; vk <= 0x5A; vk++) {
          if (GetAsyncKeyState(vk) & 0x8000) {
            settings.hotkeyVk = vk;
            settings.hotkeyMods = mods;
            capturingHotkey = false;
            RegisterAppHotkey(settings);
            break;
          }
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) capturingHotkey = false;
      }

      // Style
      ImGui::SeparatorText("Style (live preview)");
      ImGui::SliderInt("Preset Count*##s", &stgPresetCount, 1, 8);
      ImGui::SetItemTooltip("Amount of presets");
      ImGui::SliderFloat("Marker Size*##s", &stgMarkerSize, 50.f, 300.f,
                         "%.0f");
      ImGui::SetItemTooltip("Size of points in the curve");
      ImGui::SliderFloat("Curve Line Width*##s", &stgLineWidth, 1.f, 6.f,
                         "%.1f");
      ImGui::SetItemTooltip("Width of the curve line");

      // Color pickers -- edit settings directly for live preview
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
          "Plot background gradient — left/low intensity side.");
      ImGui::ColorEdit4("Gradient Right##s",
                        (float*)&settings.gradientRightColor,
                        ImGuiColorEditFlags_NoInputs);
      ImGui::SetItemTooltip(
          "Plot background gradient — right/high intensity side.");

      ImGui::TextDisabled("* - Restart required");

      ImGui::Spacing();

      // Network
      ImGui::SeparatorText("Network");
      ImGui::InputText("VRChat Host##s", stgVrchatHost, sizeof(stgVrchatHost));
      ImGui::SetItemTooltip("Usually doesn't need a change.");

      ImGui::Spacing();
      ImGui::Separator();

      ImGui::Spacing();
      if (ImGui::Button("Import Python cfg", {ImGui::CalcItemWidth(), 0}))
        importPythonConfig(settings, hub, minDur, maxDur, xViewMin, xViewMax,
                           settingsPath);
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
                          currentIDs != stgShockerIDs;

      if (needsRestart) {
        if (ImGui::Button("Save & Restart", {150, 0})) {
          commitAll();
          RegisterAppHotkey(settings);
          shouldRestart = true;
          PostMessage(g_hwnd, WM_CLOSE, 0, 0);
        }
      } else {
        if (ImGui::Button("Save", {80, 0})) {
          commitAll();
          RegisterAppHotkey(settings);
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
  CleanupRTV();
  if (g_pSwapChain) g_pSwapChain->Release();
  if (g_pd3dContext) g_pd3dContext->Release();
  if (g_pd3dDevice) g_pd3dDevice->Release();
  DestroyWindow(g_hwnd);
  UnregisterClassW(wc.lpszClassName, wc.hInstance);
}