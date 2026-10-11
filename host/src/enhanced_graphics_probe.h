// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#pragma once
namespace enhancedgraphics {
void Run(void (*log)(const char*));
void Present(void* swap,void* queue);
void DestroySwapchain(void* swap);
}
