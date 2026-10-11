// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
// Synthetic ASI for the isolated ReShade presentation-hook reproducer.
// No ScriptHookV, GTA memory, assets, player controls or simulation.
#include "hud_d3d12.h"
#include <cstdio>
namespace {
hud12::Bridge bridge;
unsigned calls=0,drawn=0;
void Log(const char* line){std::puts(line);std::fflush(stdout);}
}
extern "C" __declspec(dllexport) void __cdecl sv_enhanced_present(void* rawSwap,void* rawQueue){
    ++calls;std::printf("Hook callback %u enter\n",calls);std::fflush(stdout);
    auto* swap=static_cast<IDXGISwapChain*>(rawSwap);DXGI_SWAP_CHAIN_DESC desc{};
    if(FAILED(swap->GetDesc(&desc)))return;
    hudrender::Frame frame;frame.visible=true;frame.serial=1;frame.textureGeneration=1;
    frame.width=desc.BufferDesc.Width;frame.height=desc.BufferDesc.Height;
    auto textures=std::make_shared<hudrender::TextureSet>();textures->push_back({1,1,{255,255,255,255}});frame.textures=textures;
    for(auto p:{std::pair<float,float>{0,0},{64,0},{0,64},{0,64},{64,0},{64,64}})
        frame.vertices.push_back({p.first,p.second,.5f,.5f,{1,.25f,0,.5f},{0,0,0,0}});
    frame.draws.push_back({0,0,6});xrayrender::Frame xray;
    hudrender::SetLog(Log);
    const auto result=bridge.Present(swap,static_cast<ID3D12CommandQueue*>(rawQueue),frame,xray);
    if(result.hudMode>=0)++drawn;
    std::printf("Hook callback %u complete: ready=%d, mode=%d\n",calls,result.ready,result.hudMode);std::fflush(stdout);
}
extern "C" __declspec(dllexport) void __cdecl sv_enhanced_destroy_swapchain(void*){
    hudrender::Release();xrayrender::Release();bridge.Release();
}
extern "C" __declspec(dllexport) unsigned __cdecl test_hud_calls(){return calls;}
extern "C" __declspec(dllexport) unsigned __cdecl test_hud_drawn(){return drawn;}
extern "C" __declspec(dllexport) bool __cdecl test_hud_rejects_queue(void* swap,void* queue){
    hudrender::Frame hidden;xrayrender::Frame xray;
    return !bridge.Present(static_cast<IDXGISwapChain*>(swap),static_cast<ID3D12CommandQueue*>(queue),hidden,xray).ready;
}
