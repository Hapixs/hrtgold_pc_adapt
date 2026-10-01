#pragma once
#include "core/emulation/EmulationTypes.h"
inline std::uint32_t toActiveLowKeyMask(DsButtonMask pressed) noexcept { return ~pressed & 0xFFF; }
// Upstream BGRA bytes on Windows are numeric AARRGGBB, the project's format.
inline std::uint32_t toProjectRgba(std::uint32_t pixel) noexcept { return 0xFF000000U | (pixel & 0x00FFFFFFU); }
