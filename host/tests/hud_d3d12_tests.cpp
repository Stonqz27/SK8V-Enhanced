// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#include "hud_d3d12.h"
#include <cstdio>
#include <stdexcept>
#include <cstring>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
using Microsoft::WRL::ComPtr;
namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Hr(HRESULT result,const char* why){Need(SUCCEEDED(result),why);}
void Log(const char* line){std::puts(line);}
void Sync(ID3D12Device* device,ID3D12CommandQueue* queue) {
    ComPtr<ID3D12Fence> fence;Hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"fence");
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);Need(event!=nullptr,"fence event");
    Hr(queue->Signal(fence.Get(),1),"signal");Hr(fence->SetEventOnCompletion(1,event),"fence event registration");
    const DWORD wait=WaitForSingleObject(event,10000);CloseHandle(event);Need(wait==WAIT_OBJECT_0,"GPU timeout");
}
D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,from,to};return b;
}
hudrender::Frame Fixture(){
    hudrender::Frame f;f.visible=true;f.serial=1;f.width=64;f.height=64;f.textureGeneration=1;
    auto t=std::make_shared<hudrender::TextureSet>();t->push_back({1,1,{255,255,255,255}});f.textures=t;
    for(const auto p:{std::pair<float,float>{0,0},{64,0},{0,64},{0,64},{64,0},{64,64}})
        f.vertices.push_back({p.first,p.second,0.5f,0.5f,{1,0.25f,0,0.5f},{0,0,0,0}});
    f.draws.push_back({0,0,6});return f;
}
}
int main(int argc,char** argv){try{
    bool hardware=false,debug=false,gtaSwap=false;
    for(int i=1;i<argc;++i){hardware|=std::strcmp(argv[i],"--hardware")==0;debug|=std::strcmp(argv[i],"--debug")==0;gtaSwap|=std::strcmp(argv[i],"--gta-swap")==0;}
    if(debug){ComPtr<ID3D12Debug> layer;Hr(D3D12GetDebugInterface(IID_PPV_ARGS(&layer)),"D3D12 debug layer");layer->EnableDebugLayer();}
    const wchar_t* name=L"SK8VD3D12OffscreenTest";WNDCLASSW wc{};
    wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=name;
    Need(RegisterClassW(&wc)!=0,"window class");
    HWND window=CreateWindowW(name,L"",WS_POPUP,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
    Need(window!=nullptr,"hidden test window");
    ComPtr<IDXGIFactory4> factory;Hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");
    ComPtr<IDXGIAdapter> adapter;
    if(hardware){ComPtr<IDXGIFactory6> factory6;Hr(factory.As(&factory6),"DXGI factory 6");Hr(factory6->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)),"hardware adapter");}
    else Hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"WARP adapter");
    DXGI_ADAPTER_DESC ad{};Hr(adapter->GetDesc(&ad),"adapter description");std::printf("Adapter: %ls; debug=%d; GTA swap=%d\n",ad.Description,debug,gtaSwap);
    ComPtr<ID3D12Device> device;Hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"D3D12 device");
    ComPtr<ID3D12InfoQueue> info;if(debug)Hr(device.As(&info),"D3D12 info queue");
    D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;Hr(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"queue");
    DXGI_SWAP_CHAIN_DESC1 sd{};sd.Width=64;sd.Height=64;sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.SampleDesc.Count=1;
    sd.BufferCount=2;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if(gtaSwap){sd.Width=1904;sd.Height=1001;sd.Format=DXGI_FORMAT_B8G8R8A8_UNORM;sd.BufferCount=3;sd.BufferUsage=0x30;sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;sd.AlphaMode=DXGI_ALPHA_MODE_IGNORE;sd.Flags=0x802;}
    ComPtr<IDXGISwapChain1> swap;Hr(factory->CreateSwapChainForHwnd(queue.Get(),window,&sd,nullptr,nullptr,&swap),"swapchain");
    ComPtr<ID3D12Resource> buffer;Hr(swap->GetBuffer(0,IID_PPV_ARGS(&buffer)),"backbuffer");
    ComPtr<ID3D12CommandAllocator> allocator;Hr(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator)),"allocator");
    ComPtr<ID3D12GraphicsCommandList> list;Hr(device->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"command list");
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=1;
    ComPtr<ID3D12DescriptorHeap> heap;Hr(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"RTV heap");
    const auto rtv=heap->GetCPUDescriptorHandleForHeapStart();device->CreateRenderTargetView(buffer.Get(),nullptr,rtv);
    auto b=Transition(buffer.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);list->ResourceBarrier(1,&b);
    const float black[4]={0,0,0,1};list->ClearRenderTargetView(rtv,black,0,nullptr);
    b=Transition(buffer.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);list->ResourceBarrier(1,&b);
    Hr(list->Close(),"clear close");ID3D12CommandList* commands[]={list.Get()};queue->ExecuteCommandLists(1,commands);Sync(device.Get(),queue.Get());
    hudrender::SetLog(Log);hud12::Bridge bridge;auto frame=Fixture();frame.width=sd.Width;frame.height=sd.Height;xrayrender::Frame xray;
    const auto rendered=bridge.Present(swap.Get(),queue.Get(),frame,xray);
    Need(rendered.ready&&rendered.hudMode>=0,"11-on-12 HUD rendering");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 total=0;const auto bd=buffer->GetDesc();
    device->GetCopyableFootprints(&bd,0,1,0,&footprint,nullptr,nullptr,&total);
    D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=total;rd.Height=1;rd.DepthOrArraySize=1;
    rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;Hr(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"readback");
    Sync(device.Get(),queue.Get());Hr(allocator->Reset(),"allocator reset");Hr(list->Reset(allocator.Get(),nullptr),"list reset");
    b=Transition(buffer.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=buffer.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    b=Transition(buffer.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PRESENT);list->ResourceBarrier(1,&b);
    Hr(list->Close(),"readback close");queue->ExecuteCommandLists(1,commands);Sync(device.Get(),queue.Get());
    unsigned char* data=nullptr;D3D12_RANGE range{0,static_cast<SIZE_T>(total)};Hr(readback->Map(0,&range,reinterpret_cast<void**>(&data)),"readback map");
    const auto* pixel=data+footprint.Offset+32*footprint.Footprint.RowPitch+32*4;
    const int r=pixel[gtaSwap?2:0],g=pixel[1],blue=pixel[gtaSwap?0:2],a=pixel[3];D3D12_RANGE none{0,0};readback->Unmap(0,&none);
    std::printf("D3D12 HUD reference pixel: %d %d %d %d (mode %d)\n",r,g,blue,a,rendered.hudMode);
    Need(r>=186&&r<=190&&g>=97&&g<=101&&blue<=1&&a>=254,"D3D12 colour/alpha mismatch");
    frame.visible=false;const auto hidden=bridge.Present(swap.Get(),queue.Get(),frame,xray);
    Need(hidden.ready&&hidden.hudMode==-1,"hidden D3D12 HUD rendered");
    buffer.Reset();list.Reset();allocator.Reset();heap.Reset();readback.Reset();Sync(device.Get(),queue.Get());
    Hr(swap->ResizeBuffers(sd.BufferCount,96,48,DXGI_FORMAT_UNKNOWN,sd.Flags),"wrapped backbuffer retained across resize");
    hudrender::Release();xrayrender::Release();bridge.Release();swap.Reset();queue.Reset();device.Reset();
    DestroyWindow(window);UnregisterClassW(name,wc.hInstance);
    if(info){
        bool errors=false;
        for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i){
            SIZE_T bytes=0;info->GetMessage(i,nullptr,&bytes);std::vector<unsigned char> storage(bytes);
            auto* msg=reinterpret_cast<D3D12_MESSAGE*>(storage.data());Hr(info->GetMessage(i,msg,&bytes),"debug message");
            if(msg->Severity<=D3D12_MESSAGE_SEVERITY_WARNING)std::fprintf(stderr,"D3D12 debug [%d]: %s\n",msg->Severity,msg->pDescription);
            errors|=msg->Severity<=D3D12_MESSAGE_SEVERITY_ERROR;
        }
        Need(!errors,"D3D12 validation reported errors");
    }
    std::puts("D3D12 HUD PASS: same linear pixels, queue ordering, hidden state, resize");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
