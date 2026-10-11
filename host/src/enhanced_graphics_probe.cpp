// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#include "enhanced_graphics_probe.h"
#include "hud_d3d12.h"
#include <main.h>
#include <atomic>
#include <filesystem>
#include <fstream>

namespace enhancedgraphics {
namespace {
std::atomic<bool> active{false};
std::atomic<unsigned> calls{0},ready{0},drawn{0},width{0},height{0};
std::atomic<std::uintptr_t> owner{0};
hud12::Bridge bridge;
hudrender::Frame frame;
void Fixture(unsigned w,unsigned h) {
    if(frame.width==w&&frame.height==h)return;
    frame.width=w;frame.height=h;frame.visible=true;++frame.serial;frame.textureGeneration=1;
    if(!frame.textures){auto textures=std::make_shared<hudrender::TextureSet>();
        textures->push_back({1,1,{255,255,255,255}});frame.textures=textures;}
    frame.vertices.clear();frame.draws={{0,0,6}};
    for(const auto p:{std::pair<float,float>{32,32},{152,32},{32,56},{32,56},{152,32},{152,56}})
        frame.vertices.push_back({p.first,p.second,0.5f,0.5f,{0.4f,0.8f,1,0.8f},{0,0,0,0}});
}
}
void Present(void* rawSwap,void* rawQueue) {
    if(!active.load(std::memory_order_acquire)||!rawSwap||!rawQueue)return;
    auto* swap=static_cast<IDXGISwapChain*>(rawSwap);DXGI_SWAP_CHAIN_DESC desc{};
    if(FAILED(swap->GetDesc(&desc)))return;
    DWORD pid=0;GetWindowThreadProcessId(desc.OutputWindow,&pid);
    if(pid!=GetCurrentProcessId()||desc.BufferDesc.Width<640||desc.BufferDesc.Height<360)return;
    auto expected=std::uintptr_t{0};const auto candidate=reinterpret_cast<std::uintptr_t>(rawSwap);
    owner.compare_exchange_strong(expected,candidate);
    if(owner.load()!=candidate)return;
    ++calls;width=desc.BufferDesc.Width;height=desc.BufferDesc.Height;
    Fixture(desc.BufferDesc.Width,desc.BufferDesc.Height);
    xrayrender::Frame xray;
    const auto result=bridge.Present(swap,static_cast<ID3D12CommandQueue*>(rawQueue),frame,xray);
    if(result.ready)++ready;if(result.hudMode>=0)++drawn;
}
void DestroySwapchain(void* rawSwap) {
    if(owner.load()!=reinterpret_cast<std::uintptr_t>(rawSwap))return;
    hudrender::Release();xrayrender::Release();bridge.Release();owner=0;frame={};
}
void Run(void (*log)(const char*)) {
    calls=0;ready=0;drawn=0;width=0;height=0;
    log("Enhanced graphics probe: fifteen-second D3D12 HUD-layer test; player and controls untouched");
    const auto start=GetTickCount64();active.store(true,std::memory_order_release);
    while(GetTickCount64()-start<15000)WAIT(0);
    active.store(false,std::memory_order_release);
    const bool verified=calls>0&&ready>0&&drawn>0;
    const std::filesystem::path folder=L"port-evidence\\live-graphics";
    std::filesystem::create_directories(folder);
    std::ofstream out(folder/L"graphics-probe.json");
    out<<"{\"schema\":1,\"stage\":\"enhanced_graphics\",\"version\":\"1.0.1158.16\",\"process_id\":"<<GetCurrentProcessId()
       <<",\"duration_ms\":"<<GetTickCount64()-start<<",\"present_calls\":"<<calls<<",\"interop_ready_frames\":"<<ready
       <<",\"drawn_frames\":"<<drawn<<",\"width\":"<<width<<",\"height\":"<<height
       <<",\"renderer\":\"D3D11-on-D3D12\",\"test_publication\":\"small temporary diagnostic rectangle\","
         "\"runtime_started\":false,\"player_tasks_changed\":false,\"pose_memory_written\":false,"
         "\"legacy_private_hooks_entered\":false,\"full_gameplay_port_ready\":false,\"graphics_verified\":"
       <<(verified?"true":"false")<<"}\n";
    log(verified?"Enhanced graphics probe: D3D12 presentation and original HUD pipeline VERIFIED":"Enhanced graphics probe: no verified presentation; inspect addon and graphics-probe evidence");
}
}
