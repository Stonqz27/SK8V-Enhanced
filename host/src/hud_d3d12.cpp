// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#include "hud_d3d12.h"

namespace hud12 {
using Microsoft::WRL::ComPtr;
namespace {
// ReShade v6.8.0 com_utils.hpp: query the original COM object. Resource
// GetDevice is hooked after a world texture is created and can return a
// proxy even when the addon supplied an unwrapped native queue.
constexpr GUID UnwrappedObject={0x7f2c9a11,0x3b4e,0x4d6a,{0x81,0x2f,0x5e,0x9c,0xd3,0x7a,0x1b,0x42}};
ComPtr<ID3D12Device> NativeDevice(ID3D12Device* device) {
    ComPtr<ID3D12Device> result=device;
    ComPtr<IUnknown> original;
    if(SUCCEEDED(device->QueryInterface(UnwrappedObject,reinterpret_cast<void**>(original.GetAddressOf())))) {
        ComPtr<ID3D12Device> native;
        if(SUCCEEDED(original.As(&native)))result=native;
    }
    return result;
}
bool SameDevice(ID3D12Device* first,ID3D12Device* second) {
    const auto native=NativeDevice(first);
    ComPtr<IUnknown> left,right;
    return SUCCEEDED(native.As(&left))&&SUCCEEDED(second->QueryInterface(IID_PPV_ARGS(&right)))&&left.Get()==right.Get();
}
}
bool Bridge::Ensure(ID3D12CommandQueue* queue) {
    if(!queue||queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT)return false;
    if(queue_.Get()==queue&&interop_)return true;
    ComPtr<ID3D12Device> device;
    if(FAILED(queue->GetDevice(IID_PPV_ARGS(&device))))return false;
    device=NativeDevice(device.Get());
    Release();
    IUnknown* queues[]={queue};
    const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};
    if(FAILED(D3D11On12CreateDevice(device.Get(),D3D11_CREATE_DEVICE_BGRA_SUPPORT,
       levels,1,queues,1,0,&device11_,&context_,nullptr))||FAILED(device11_.As(&interop_))) {
        Release();return false;
    }
    device12_=device;queue_=queue;return true;
}
Result Bridge::Present(IDXGISwapChain* swap,ID3D12CommandQueue* queue,
                      const hudrender::Frame& hud,const xrayrender::Frame& xray) {
    Result result;
    if(!swap||!Ensure(queue))return result;
    ComPtr<IDXGISwapChain3> swap3;
    if(FAILED(swap->QueryInterface(IID_PPV_ARGS(&swap3))))return result;
    ComPtr<ID3D12Resource> buffer;
    if(FAILED(swap3->GetBuffer(swap3->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&buffer))))return result;
    ComPtr<ID3D12Device> owner;
    if(FAILED(buffer->GetDevice(IID_PPV_ARGS(&owner)))||!SameDevice(owner.Get(),device12_.Get()))return result;
    result.ready=true;
    if(!hud.visible&&!xray.visible)return result;
    const D3D11_RESOURCE_FLAGS flags={D3D11_BIND_RENDER_TARGET,0,0,0};
    ComPtr<ID3D11Texture2D> wrapped;
    // Called at presentation: GTA has transitioned its backbuffer to PRESENT.
    // 11-on-12 owns transitions during acquisition and returns it to PRESENT.
    if(FAILED(interop_->CreateWrappedResource(buffer.Get(),&flags,D3D12_RESOURCE_STATE_PRESENT,
       D3D12_RESOURCE_STATE_PRESENT,IID_PPV_ARGS(&wrapped)))){result.ready=false;return result;}
    ID3D11Resource* resources[]={wrapped.Get()};
    interop_->AcquireWrappedResources(resources,1);
    result.xray=xrayrender::RenderTexture(device11_.Get(),wrapped.Get(),xray);
    result.hudMode=hudrender::RenderTexture(device11_.Get(),wrapped.Get(),hud);
    interop_->ReleaseWrappedResources(resources,1);
    context_->Flush();
    // The rasterisers restore their state; no backbuffer/wrapped view is
    // retained across this call, so ResizeBuffers remains legal.
    return result;
}
void Bridge::Release() {
    if(context_)context_->Flush();
    interop_.Reset();context_.Reset();device11_.Reset();queue_.Reset();device12_.Reset();
}
}
