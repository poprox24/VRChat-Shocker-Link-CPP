#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include <array>
#include <cmath>

#include "config.h"
#include "curve.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "implot.h"
#include "logger.h"
#include "settings.h"
#include "shockerhub.h"
#include "ui.h"

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRTV = nullptr;
static HWND g_hwnd = nullptr;

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

// UI entry point
inline void ui_run(Config& config, Settings& settings, ShockerHub& hub,
                   const std::string& settingsPath) {
  WNDCLASSEXW wc{sizeof(wc),
                 CS_CLASSDC,
                 WndProc,
                 0,
                 0,
                 GetModuleHandle(nullptr),
                 nullptr,
                 nullptr,
                 nullptr,
                 nullptr,
                 L"ShockerLink",
                 nullptr};
  RegisterClassExW(&wc);
  g_hwnd =
      CreateWindowW(wc.lpszClassName, L"ShockerLink", WS_OVERLAPPEDWINDOW, 100,
                    100, 900, 600, nullptr, nullptr, wc.hInstance, nullptr);

  if (!InitD3D(g_hwnd)) return;

  ShowWindow(g_hwnd, SW_SHOWDEFAULT);
  UpdateWindow(g_hwnd);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  ImGui::StyleColorsDark();

  // Apply background colors
  ImGuiStyle& style = ImGui::GetStyle();
  style.Colors[ImGuiCol_WindowBg] = config.backgroundColor;
  style.Colors[ImGuiCol_ChildBg] = config.backgroundColor;
  style.Colors[ImGuiCol_Text] = config.labelColor;

  ImPlot::GetStyle().Colors[ImPlotCol_FrameBg] = config.insideCurveBg;
  ImPlot::GetStyle().Colors[ImPlotCol_PlotBg] = config.insideCurveBg;
  ImPlot::GetStyle().Colors[ImPlotCol_AxisText] = config.labelColor;

  ImGui_ImplWin32_Init(g_hwnd);
  ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dContext);

  // Dynamic UI state
  float minDur = (float)settings.minShockDuration;
  float maxDur = (float)settings.maxShockDuration;
  bool cooldownEnabled = config.cooldownEnabled;

  // Apply default preset if set
  if (settings.defaultPreset >= 0 &&
      settings.defaultPreset < (int)settings.presets.size() &&
      settings.presets[settings.defaultPreset].has_value()) {
    auto& p = settings.presets[settings.defaultPreset];
    minDur = p->minShockDuration;
    maxDur = p->maxShockDuration;
    hub.curvePoints = p->curvePoints;
  }

  std::array<CurvePoint, 3>& pts = hub.curvePoints;

  // Curve cache
  std::array<CurvePoint, 3> lastPts = {};
  std::vector<double> cx, cy;

  ImVec4& clear = config.backgroundColor;

  MSG msg{};
  while (msg.message != WM_QUIT) {
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
    ImGui::BeginChild("##controls", {220, -90}, true);

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
      std::string saveLabel = "S##" + std::to_string(i);
      if (ImGui::SmallButton(saveLabel.c_str())) {
        Preset p;
        p.name = label;
        p.minShockDuration = minDur;
        p.maxShockDuration = maxDur;
        p.curvePoints = hub.curvePoints;
        settings.presets[i] = p;
        settings.save(settingsPath);
      }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Test Shock", {-1, 0}))
      hub.queueShock(config.shockStrength);

    ImGui::EndChild();

    // Curve editor
    ImGui::SameLine();
    ImGui::BeginChild("##plot", {0, -90}, false);

    if (ImPlot::BeginPlot("Intensity Curve", {-1, -1})) {
      ImPlot::SetupAxes("Intensity (%)", "Weight");
      ImPlot::SetupAxisLimits(ImAxis_X1, 0, 100, ImPlotCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1, ImPlotCond_Always);

      ImPlot::PushPlotClipRect();
      ImDrawList* dl = ImPlot::GetPlotDrawList();
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
        double lx[3] = {pts[0].x, pts[1].x, pts[2].x};
        double ly[3] = {pts[0].y, pts[1].y, pts[2].y};
        ImPlot::SetNextLineStyle(config.curveLineColor, config.lineWidth);
        ImPlot::PlotLine("Curve", cx.data(), cy.data(), (int)cx.size());
      }

      {
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
        ImPlot::SetNextLineStyle({0, 0.76f, 1, 1}, 2.5f);
        ImPlot::PlotLine("Curve", cx.data(), cy.data(), (int)cx.size());
      }

      ImPlot::EndPlot();
    }
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

    // Render
    ImGui::Render();
    g_pd3dContext->OMSetRenderTargets(1, &g_mainRTV, nullptr);
    g_pd3dContext->ClearRenderTargetView(g_mainRTV, (float*)&clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pSwapChain->Present(1, 0);
  }

  // Cleanup
  settings.minShockDuration = minDur;
  settings.maxShockDuration = maxDur;

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