// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#include "hud_overlay.h"
#if defined(SK8V_ENHANCED_GRAPHICS_PROBE)
#include "enhanced_graphics_probe.h"
#endif
#include <windows.h>
// Called only by our ReShade addon on its render thread, with the actual
// presenting DXGI swapchain and D3D12 direct queue. No private game addresses.
extern "C" __declspec(dllexport) void __cdecl sv_enhanced_present(void* swap,void* queue) {
#if defined(SK8V_ENHANCED_GRAPHICS_PROBE)
    enhancedgraphics::Present(swap,queue);
#else
    hudoverlay::EnhancedPresent(swap,queue);
#endif
}
extern "C" __declspec(dllexport) void __cdecl sv_enhanced_destroy_swapchain(void* swap) {
#if defined(SK8V_ENHANCED_GRAPHICS_PROBE)
    enhancedgraphics::DestroySwapchain(swap);
#else
    hudoverlay::EnhancedDestroySwapchain(swap);
#endif
}
