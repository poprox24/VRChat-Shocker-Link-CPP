#pragma once

#include <cstring>
#include <string>

#include "imgui.h"
#include "session.h"

inline void drawSessionButton(SessionManager& session, bool& showSessionPanel) {
  bool active = session.isActive();
  bool pending = session.status() == SessionStatus::Pending;

  ImVec4 col;
  if (active)
    col = {0.13f, 0.48f, 0.30f, 1.f};  // Green when in a room
  else if (pending)
    col = {0.45f, 0.28f, 0.05f, 1.f};  // Amber while waiting
  else if (showSessionPanel)
    col = {0.13f, 0.34f, 0.52f, 1.f};  // Blue when panel open
  else
    col = ImGui::GetStyle().Colors[ImGuiCol_Button];

  ImGui::PushStyleColor(ImGuiCol_Button, col);
  const char* label = active ? "Session \xe2\x97\x8f" : "Session";
  if (ImGui::Button(label, {-1, 0})) showSessionPanel = !showSessionPanel;
  ImGui::PopStyleColor();
}

inline void drawSessionWindow(SessionManager& session, bool& showSessionPanel) {
  // Persistent input buffers (single SessionManager instance, so static is ok).
  static char nameBuf[64] = {};
  static char codeBuf[8] = {};

  // Seed the name field once from whatever the session already knows.
  static bool seeded = false;
  if (!seeded) {
    std::string n = session.myName();
    if (!n.empty()) std::snprintf(nameBuf, sizeof(nameBuf), "%s", n.c_str());
    seeded = true;
  }

  // Approval modal
  auto pend = session.pending();
  if (!pend.empty()) {
    if (!ImGui::IsPopupOpen("##approve")) ImGui::OpenPopup("##approve");
  }
  if (ImGui::BeginPopupModal("##approve", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextColored({1.f, 0.75f, 0.2f, 1.f}, "Join requests");
    ImGui::Separator();
    if (pend.empty()) {
      ImGui::CloseCurrentPopup();
    } else {
      for (auto& p : pend) {
        ImGui::PushID(p.sid.c_str());
        ImGui::Text("%s", p.name.c_str());
        ImGui::SameLine(160.f);
        ImGui::PushStyleColor(ImGuiCol_Button, {0.13f, 0.48f, 0.30f, 1.f});
        if (ImGui::Button("Approve")) session.approve(p.sid);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, {0.55f, 0.12f, 0.12f, 1.f});
        if (ImGui::Button("Deny")) session.deny(p.sid);
        ImGui::PopStyleColor();
        ImGui::PopID();
      }
      ImGui::Spacing();
      if (ImGui::Button("Dismiss")) ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (!showSessionPanel) return;

  // Main session window
  ImGui::SetNextWindowSize({340, 320}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints({280, 160}, {900, 900});
  if (!ImGui::Begin("Session", &showSessionPanel)) {
    ImGui::End();
    return;
  }

  SessionStatus st = session.status();

  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##sname", "Your name", nameBuf, sizeof(nameBuf));
  ImGui::Spacing();

  if (st == SessionStatus::Idle) {
    ImGui::PushStyleColor(ImGuiCol_Button, {0.13f, 0.48f, 0.30f, 1.f});
    if (ImGui::Button("Create session", {-1, 0}))
      session.createSession(nameBuf[0] ? nameBuf : "host");
    ImGui::PopStyleColor();

    ImGui::SeparatorText("Join a session");
    ImGui::SetNextItemWidth(-1);
    // Force uppercase as they type.
    if (ImGui::InputTextWithHint("##scode", "CODE", codeBuf, sizeof(codeBuf),
                                 ImGuiInputTextFlags_CharsUppercase)) {
    }
    if (ImGui::Button("Join session", {-1, 0}) && std::strlen(codeBuf) == 6)
      session.joinSession(codeBuf, nameBuf[0] ? nameBuf : "guest");

    std::string err = session.lastError();
    if (!err.empty())
      ImGui::TextColored({0.9f, 0.35f, 0.35f, 1.f}, "%s", err.c_str());

  } else if (st == SessionStatus::Connecting) {
    ImGui::Text("Connecting...");

  } else if (st == SessionStatus::Pending) {
    ImGui::Text("Waiting for a member to approve you...");
    if (ImGui::Button("Cancel", {-1, 0})) session.leave();

  } else if (st == SessionStatus::Active) {
    std::string code = session.code();
    ImGui::Text("Room code:");
    ImGui::SameLine();
    ImGui::TextColored({0.4f, 1.f, 0.6f, 1.f}, "%s", code.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(code.c_str());
    if (session.isHost()) {
      ImGui::SameLine();
      ImGui::TextDisabled("(host)");
    }

    ImGui::SeparatorText("Members");
    std::string mySid = session.mySid();
    for (auto& m : session.members()) {
      ImGui::BulletText("%s", m.name.c_str());
      if (m.sid == mySid) {
        ImGui::SameLine();
        ImGui::TextDisabled("(you)");
      }
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, {0.55f, 0.12f, 0.12f, 1.f});
    if (ImGui::Button("Leave session", {-1, 0})) session.leave();
    ImGui::PopStyleColor();
  }

  ImGui::End();
}