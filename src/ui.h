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
using namespace std::chrono;

#include "config.h"
#include "curve.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "imgui_internal.h"
#include "implot.h"
#include "logger.h"
#include "settings.h"
#include "shockerhub.h"
#include "ui.h"
#include "updater.h"

static constexpr wchar_t kWindowTitle[] = L"Shocker Link v1.1.0";

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRTV = nullptr;
static HWND g_hwnd = nullptr;

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

  // --- Import curve_config.json ---
  try {
    std::ifstream f(folder + "curve_config.json");
    if (f.is_open()) {
      nlohmann::json j = nlohmann::json::parse(f);

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
      const nlohmann::json names =
          j.contains("preset_names") ? j["preset_names"] : nlohmann::json{};
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
      logMsg("Imported curve_config.json");
    } else {
      logMsg("curve_config.json not found in selected folder, skipping");
    }
  } catch (std::exception& e) {
    logMsg("curve_config.json import failed: {}", e.what());
  }

  // --- Import config.yml ---
  try {
    std::string srcYml = folder + "config.yml";
    std::ifstream src(srcYml);
    if (src.is_open()) {
      static const std::vector<std::string> dropKeys = {
          "OSC_LISTEN_PORT",
          "OSC_SEND_PORT",
          "test",
          "OPENSHOCK_SHOCKER_ID",
      };

      std::string openshockId, pishockId;
      std::vector<std::string> lines;
      std::string line;

      while (std::getline(src, line)) {
        auto keyOf = [&](const std::string& key) {
          return line.find(key + ":") == 0;
        };
        if (keyOf("OPENSHOCK_SHOCKER_ID")) {
          openshockId = line.substr(line.find(':') + 1);
          continue;
        }
        if (keyOf("PISHOCK_SHOCKER_ID")) {
          pishockId = line.substr(line.find(':') + 1);
          continue;
        }
        bool drop = false;
        for (auto& k : dropKeys)
          if (keyOf(k)) {
            drop = true;
            break;
          }
        if (!drop) lines.push_back(line);
      }

      // Resolve shocker ID: pishock wins if present
      std::string resolvedId = pishockId.empty() ? openshockId : pishockId;
      // Strip leading whitespace
      auto start = resolvedId.find_first_not_of(" \t");
      if (start != std::string::npos) resolvedId = resolvedId.substr(start);
      // Strip inline comment
      auto comment = resolvedId.find('#');
      if (comment != std::string::npos)
        resolvedId = resolvedId.substr(0, comment);
      // Strip trailing whitespace
      auto end = resolvedId.find_last_not_of(" \t");
      if (end != std::string::npos) resolvedId = resolvedId.substr(0, end + 1);

      std::ofstream dst("config.yml");
      for (auto& l : lines) {
        dst << l << '\n';
        // Emit SHOCKER_IDS right after SERIAL_PORT line
        if (l.find("USE_PISHOCK:") == 0)
          dst << "SHOCKER_IDS: [" << resolvedId
              << "] # Shocker IDs, if you have multiple, split by comma (eg.: "
                 "[12345, 23456]), PiShock should find them "
                 "automatically(OpenShock doesn't save them on the hub)\n";
      }
      dst.close();
      logMsg("Imported config.yml, restarting in 5s...");

      char exePath[MAX_PATH] = {};
      GetModuleFileNameA(nullptr, exePath, MAX_PATH);
      std::thread([exePath, &hub]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        shouldRestart = true;
        PostMessage(g_hwnd, WM_CLOSE, 0, 0);
      }).detach();
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

// UI entry point
inline void ui_run(Config& config, Settings& settings, ShockerHub& hub,
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
      CreateWindowW(wc.lpszClassName, kWindowTitle, WS_OVERLAPPEDWINDOW, 100,
                    100, 750, 520, nullptr, nullptr, wc.hInstance, nullptr);

  BOOL dark = TRUE;
  DwmSetWindowAttribute(g_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                        sizeof(dark));

  SendMessage(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
  SendMessage(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

  if (!InitD3D(g_hwnd)) return;

  ShowWindow(g_hwnd, SW_SHOWDEFAULT);
  UpdateWindow(g_hwnd);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  ImGuiIO& io = ImGui::GetIO();

  // io.IniFilename = NULL;

  io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 18.0f);
  ImFont* boldFont =
      io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeuib.ttf", 18.0f);

  ImPlot::GetStyle().Colors[ImPlotCol_FrameBg] = config.insideCurveBg;
  ImPlot::GetStyle().Colors[ImPlotCol_PlotBg] = config.insideCurveBg;
  ImPlot::GetStyle().Colors[ImPlotCol_AxisText] = config.labelColor;
  ImPlot::GetStyle().Colors[ImPlotCol_AxisGrid] = ImVec4(1, 1, 1, 0.25f);
  ImPlot::GetStyle().Colors[ImPlotCol_LegendBg] =
      ImVec4(0.1f, 0.1f, 0.15f, 0.85f);
  ImPlot::GetStyle().Colors[ImPlotCol_LegendBorder] =
      ImVec4(0.4f, 0.4f, 0.5f, 0.8f);
  ImPlot::GetStyle().Colors[ImPlotCol_LegendText] = config.labelColor;
  ImPlot::GetStyle().LegendPadding = ImVec2(10, 8);
  ImPlot::GetStyle().LegendInnerPadding = ImVec2(6, 4);
  ImPlot::GetStyle().LegendSpacing = ImVec2(6, 4);

  ImGui::StyleColorsDark();

  // Apply background colors
  ImGuiStyle& style = ImGui::GetStyle();
  style.Colors[ImGuiCol_WindowBg] = config.backgroundColor;
  style.Colors[ImGuiCol_ChildBg] = config.backgroundColor;
  style.Colors[ImGuiCol_Text] = config.labelColor;

  ImGui_ImplWin32_Init(g_hwnd);
  ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dContext);

  // Dynamic UI state
  float minDur = (float)settings.minShockDuration;
  float maxDur = (float)settings.maxShockDuration;
  bool cooldownEnabled = config.cooldownEnabled;

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

  std::array<CurvePoint, 3>& pts = hub.curvePoints;

  // Curve cache
  std::array<CurvePoint, 3> lastPts = {};
  std::vector<double> cx, cy;

  ImVec4& clear = config.backgroundColor;

  MSG msg{};
  while (msg.message != WM_QUIT) {
    auto frameStart = steady_clock::now();
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
      continue;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Left panel
    ImGui::BeginChild("##controls", {180, -90}, true);

    ImGui::Spacing();
    ImGui::Text("Min Duration (s)");
    ImGui::SliderFloat("##mind", &minDur, 0.1f, 5.f, "%.1f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      settings.minShockDuration = minDur;
    }

    ImGui::Text("Max Duration (s)");
    ImGui::SliderFloat("##maxd", &maxDur, 0.1f, 5.f, "%.1f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      settings.maxShockDuration = maxDur;
    }

    ImGui::Spacing();
    if (ImGui::Checkbox("Enable Cooldown", &cooldownEnabled))
      config.cooldownEnabled = cooldownEnabled;

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

    if (ImGui::Button("Test Shock",
                      {config.hasSecondShockParameter ? 85.f : -1.f, 0}))
      hub.queueShock(config.shockStrength);

    if (config.hasSecondShockParameter) {
      ImGui::SameLine();
      if (ImGui::Button("Test 2nd", {-1, 0}))
        hub.queueShockUpperHalf(config.shockStrength);
    }

    if (!hub.isConnected()) {
      if (ImGui::Button("Retry Connection", {-1, 0})) hub.tryReconnect();
    }

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() -
                         ImGui::GetFrameHeightWithSpacing() -
                         ImGui::GetStyle().WindowPadding.y);
    if (ImGui::Button("Import Python cfg", {-1, 0}))
      importPythonConfig(settings, hub, minDur, maxDur, xViewMin, xViewMax,
                         settingsPath);

    ImGui::EndChild();

    // Curve editor
    ImGui::SameLine();
    ImGui::BeginChild("##plot", {0, -90}, false);

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
          boldFont->CalcTextSizeA(boldFont->FontSize, FLT_MAX, 0.0f, title);

      ImVec2 pos;
      pos.x = plot_pos.x + (plot_size.x - text_size.x) * 0.5f;
      pos.y = plot_pos.y - ImGui::GetTextLineHeight() - 4;
      dl->AddText(boldFont, boldFont->FontSize, pos,
                  ImGui::ColorConvertFloat4ToU32(
                      ImGui::GetStyle().Colors[ImGuiCol_Text]),
                  title);

      ImPlot::PushPlotClipRect();
      ImVec2 pmin = ImPlot::PlotToPixels({0, 0});
      ImVec2 pmax = ImPlot::PlotToPixels({100, 1});
      dl->AddRectFilledMultiColor(
          pmin, pmax, ImGui::ColorConvertFloat4ToU32(config.gradientLeftColor),
          ImGui::ColorConvertFloat4ToU32(config.gradientRightColor),
          ImGui::ColorConvertFloat4ToU32(config.gradientRightColor),
          ImGui::ColorConvertFloat4ToU32(config.gradientLeftColor));

      ImPlot::PopPlotClipRect();

      for (int i = 0; i < 3; i++) {
        ImPlot::DragPoint(i, &pts[i].x, &pts[i].y, config.markerColor,
                          config.touchMarkerSize / 15.f,
                          ImPlotDragToolFlags_None);
        pts[i].x = std::clamp(pts[i].x, 0.0, 100.0);
        pts[i].y = std::clamp(pts[i].y, 0.0, 1.0);
      }

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

        // Curve
        ImPlot::SetNextLineStyle(config.curveLineColor, config.lineWidth);
        ImPlot::PlotLine("##curve", cx.data(), cy.data(), (int)cx.size());

        // Dashed vertical lines
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
        ImGui::ColorConvertFloat4ToU32(config.insideCurveBg));
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

    if (updateReady) Updater::applyAndRestart(g_hwnd);

    // Render
    ImGui::Render();
    g_pd3dContext->OMSetRenderTargets(1, &g_mainRTV, nullptr);
    g_pd3dContext->ClearRenderTargetView(g_mainRTV, (float*)&clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pSwapChain->Present(1, 0);

    // Frame cap
    auto elapsed = steady_clock::now() - frameStart;
    auto target = microseconds(16667);  // ~60fps
    if (elapsed < target) {
      std::this_thread::sleep_for(target - elapsed);
    }
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