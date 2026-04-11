#pragma once

#include <string>

namespace Notifications {

void sendXSOverlay(const std::string& title, const std::string& content,
                   float timeout = 3.f);
void sendOVRToolkit(const std::string& title, const std::string& content);

}  // namespace Notifications