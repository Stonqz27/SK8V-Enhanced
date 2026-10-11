// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#include <reshade.hpp>
using namespace reshade::api;
namespace {
using PresentFn=void(__cdecl*)(void*,void*);
using DestroyFn=void(__cdecl*)(void*);
void Present(command_queue* queue,swapchain* swap,const rect*,const rect*,uint32_t,const rect*) {
    if(!queue||!swap||queue->get_device()->get_api()!=device_api::d3d12)return;
    HMODULE host=GetModuleHandleW(L"SkateVEnhanced.asi");if(!host)return;
    auto present=reinterpret_cast<PresentFn>(GetProcAddress(host,"sv_enhanced_present"));
    if(present)present(reinterpret_cast<void*>(swap->get_native()),reinterpret_cast<void*>(queue->get_native()));
}
void Destroy(swapchain* swap,bool) {
    if(!swap||swap->get_device()->get_api()!=device_api::d3d12)return;
    HMODULE host=GetModuleHandleW(L"SkateVEnhanced.asi");if(!host)return;
    auto destroy=reinterpret_cast<DestroyFn>(GetProcAddress(host,"sv_enhanced_destroy_swapchain"));
    if(destroy)destroy(reinterpret_cast<void*>(swap->get_native()));
}
}
extern "C" __declspec(dllexport) const char* NAME="SK8V Enhanced HUD bridge";
extern "C" __declspec(dllexport) const char* DESCRIPTION="The original SK8V HUD and Hall of Meat rasterisers on GTA Enhanced's D3D12 queue.";
BOOL WINAPI DllMain(HMODULE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if(!reshade::register_addon(module))return FALSE;
        reshade::register_event<reshade::addon_event::present>(Present);
        reshade::register_event<reshade::addon_event::destroy_swapchain>(Destroy);
    } else if(reason==DLL_PROCESS_DETACH) {
        reshade::unregister_event<reshade::addon_event::present>(Present);
        reshade::unregister_event<reshade::addon_event::destroy_swapchain>(Destroy);
        reshade::unregister_addon(module);
    }
    return TRUE;
}
