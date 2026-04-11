#include "core/shock/cooldown.h"

#include <algorithm>

// Keep behavior equivalent to the legacy formula in ShockerHub.
double calculateDynamicCooldown(int baseCooldown, int maxCooldown,
                                double cooldownFactor,
                                std::size_t recentShockCount) {
  int extraShockCount = std::max(0, static_cast<int>(recentShockCount) - 1);
  return std::min(static_cast<double>(baseCooldown) +
                      cooldownFactor * static_cast<double>(extraShockCount),
                  static_cast<double>(maxCooldown));
}
