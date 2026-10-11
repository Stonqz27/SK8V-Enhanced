// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#pragma once
#if defined(SK8V_ENHANCED_GRAPHICS_PROBE)
#include "enhanced_graphics_probe.h"
#endif
namespace enhancedprobe {
// Edition-specific compatibility diagnostics. The animation stage temporarily
// wraps the original ped pass/jobs, counts exact player-buffer matches, then
// restores them. No pose writes, player controls or Skate runtime activation.
void Attach();
void Detach();
void Run(void (*log)(const char*));
}
