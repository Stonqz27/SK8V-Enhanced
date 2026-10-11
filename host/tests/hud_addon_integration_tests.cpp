// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace {
constexpr GUID UnwrappedObject={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
void Hr(HRESULT hr,const char* what){if(FAILED(hr)){char msg[256];std::snprintf(msg,sizeof(msg),"%s: %08X",what,unsigned(hr));throw std::runtime_error(msg);}}
void Need(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
D3D12_RESOURCE_BARRIER Barrier(ID3D12Resource* r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};return b;
}
}
int main(){try{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    wchar_t work[MAX_PATH]{};GetCurrentDirectoryW(MAX_PATH,work);
    SetEnvironmentVariableW(L"RESHADE_BASE_PATH_OVERRIDE",work);
    SetEnvironmentVariableW(L"RESHADE_DISABLE_GRAPHICS_HOOK",nullptr);
    SetEnvironmentVariableW(L"RESHADE_DISABLE_LOADING_CHECK",L"1");
    ComPtr<ID3D12Debug> debug;Hr(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)),"debug interface");debug->EnableDebugLayer();
    HMODULE host=LoadLibraryW(L"SkateVEnhanced.asi");Need(host!=nullptr,"synthetic host load");
    HMODULE rs=LoadLibraryW(L"ReShade64.dll");Need(rs!=nullptr,"actual ReShade load");
    Sleep(300);
    HMODULE addon=LoadLibraryW(L"SK8VHudBridge.addon64");Need(addon!=nullptr,"actual addon load");
    auto calls=reinterpret_cast<unsigned(__cdecl*)()>(GetProcAddress(host,"test_hud_calls"));
    auto drawn=reinterpret_cast<unsigned(__cdecl*)()>(GetProcAddress(host,"test_hud_drawn"));Need(calls&&drawn,"test host exports");
    auto rejects=reinterpret_cast<bool(__cdecl*)(void*,void*)>(GetProcAddress(host,"test_hud_rejects_queue"));
    Need(rejects!=nullptr,"foreign queue test export");
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_POPUP,0,0,1904,1001,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Need(window!=nullptr,"hidden window");
    ComPtr<IDXGIFactory6> factory;Hr(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG,IID_PPV_ARGS(&factory)),"hooked factory");
    ComPtr<IDXGIAdapter> adapter;Hr(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)),"adapter");
    ComPtr<ID3D12Device> device;Hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"hooked device");
    ComPtr<ID3D12InfoQueue> info;Hr(device.As(&info),"info queue");
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    Hr(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");
    DXGI_SWAP_CHAIN_DESC1 sd{};sd.Width=1904;sd.Height=1001;sd.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferCount=3;sd.SampleDesc.Count=1;sd.BufferUsage=0x30;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;sd.AlphaMode=DXGI_ALPHA_MODE_IGNORE;sd.Flags=0x802;
    ComPtr<IDXGISwapChain1> swap1;Hr(factory->CreateSwapChainForHwnd(queue.Get(),window,&sd,nullptr,nullptr,&swap1),"hooked swapchain");
    ComPtr<IDXGISwapChain3> swap;Hr(swap1.As(&swap),"swap3");
    // GTA creates world textures before the addon presents. ReShade then
    // hooks resource GetDevice and returns its proxy, while get_native()
    // supplies an unwrapped queue. Exercise that real ownership boundary.
    D3D12_HEAP_PROPERTIES textureHeap{};textureHeap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC textureDesc{};textureDesc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width=64;textureDesc.Height=64;textureDesc.DepthOrArraySize=1;textureDesc.MipLevels=1;
    textureDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;textureDesc.SampleDesc.Count=1;
    textureDesc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ComPtr<ID3D12Resource> worldTexture;
    Hr(device->CreateCommittedResource(&textureHeap,D3D12_HEAP_FLAG_NONE,&textureDesc,
       D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&worldTexture)),"world texture");
    {
        ComPtr<ID3D12CommandQueue> rawQueue;
        Hr(queue->QueryInterface(UnwrappedObject,reinterpret_cast<void**>(rawQueue.GetAddressOf())),"native queue");
        ComPtr<ID3D12Resource> buffer;Hr(swap->GetBuffer(0,IID_PPV_ARGS(&buffer)),"proxy fixture backbuffer");
        ComPtr<ID3D12Device> nativeOwner,proxyOwner;
        Hr(rawQueue->GetDevice(IID_PPV_ARGS(&nativeOwner)),"native queue owner");
        Hr(buffer->GetDevice(IID_PPV_ARGS(&proxyOwner)),"hooked resource owner");
        Need(proxyOwner.Get()!=nativeOwner.Get(),"world texture did not reproduce proxy/native mismatch");
        std::puts("Verified proxy/native ownership mismatch after world texture load");
    }
    ComPtr<ID3D12Fence> fence;Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Need(event!=nullptr,"fence event");UINT64 seq=0;
    auto sync=[&]{Hr(queue->Signal(fence.Get(),++seq),"signal");Hr(fence->SetEventOnCompletion(seq,event),"set event");Need(WaitForSingleObject(event,5000)==WAIT_OBJECT_0,"GPU timeout");};
    ComPtr<ID3D12CommandAllocator> allocator;Hr(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)),"allocator");
    ComPtr<ID3D12GraphicsCommandList> list;Hr(device->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"list");Hr(list->Close(),"initial close");
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
    ComPtr<ID3D12DescriptorHeap> heap;Hr(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"heap");
    const auto rtv=heap->GetCPUDescriptorHandleForHeapStart();
    for(unsigned i=0;i<32;++i){
        if(i==16){sync();Hr(swap->ResizeBuffers(3,1280,720,DXGI_FORMAT_UNKNOWN,0x802),"live hooked resize");}
        sync();Hr(allocator->Reset(),"allocator reset");Hr(list->Reset(allocator.Get(),nullptr),"list reset");
        ComPtr<ID3D12Resource> buffer;Hr(swap->GetBuffer(swap->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&buffer)),"buffer");
        device->CreateRenderTargetView(buffer.Get(),nullptr,rtv);
        auto b=Barrier(buffer.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);list->ResourceBarrier(1,&b);
        const float black[]={0,0,0,1};list->ClearRenderTargetView(rtv,black,0,nullptr);
        b=Barrier(buffer.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);list->ResourceBarrier(1,&b);Hr(list->Close(),"close");
        ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);
        std::printf("Before intercepted Present %u\n",i);std::fflush(stdout);
        Hr(swap->Present(0,DXGI_PRESENT_ALLOW_TEARING),"intercepted present");sync();
        Hr(device->GetDeviceRemovedReason(),"device removed");
    }
    Need(calls()>=32&&drawn()>=32,"real addon presentation callback did not render");
    {
        ComPtr<IDXGIAdapter> warp;Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"foreign WARP adapter");
        ComPtr<ID3D12Device> other;Hr(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&other)),"foreign device");
        ComPtr<ID3D12CommandQueue> otherQueue;Hr(other->CreateCommandQueue(&q,IID_PPV_ARGS(&otherQueue)),"foreign queue");
        Need(rejects(swap.Get(),otherQueue.Get()),"bridge accepted a genuinely foreign device");
        std::puts("Verified rejection of genuinely foreign device queue");
    }
    bool errors=false;for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i){
        SIZE_T bytes=0;info->GetMessage(i,nullptr,&bytes);std::vector<unsigned char> storage(bytes);
        auto* m=reinterpret_cast<D3D12_MESSAGE*>(storage.data());Hr(info->GetMessage(i,m,&bytes),"debug message");
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"D3D12 ERROR: %s\n",m->pDescription);errors=true;}
    }
    Need(!errors,"live hook GPU validation errors");
    worldTexture.Reset();heap.Reset();list.Reset();allocator.Reset();swap.Reset();swap1.Reset();sync();CloseHandle(event);queue.Reset();device.Reset();
    // Factory/adapter and device-child interfaces can be ReShade proxies.
    // Release them while the module containing their vtables is still loaded.
    info.Reset();fence.Reset();adapter.Reset();factory.Reset();debug.Reset();
    DestroyWindow(window);FreeLibrary(addon);FreeLibrary(rs);FreeLibrary(host);
    std::puts("PASS: actual ReShade present hook, original HUD, 32 GPU frames including resize, no validation errors; GTA not involved");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
