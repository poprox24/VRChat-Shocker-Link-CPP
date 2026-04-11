#pragma once

#include <cstddef>

// Computes dynamic cooldown based on recent trigger count.
double calculateDynamicCooldown(int baseCooldown, int maxCooldown,
                                double cooldownFactor,
                                std::size_t recentShockCount);
