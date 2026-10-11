// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#pragma once
#include "hud_renderer.h"
#include "xray_renderer.h"
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

namespace hud12 {
struct Result { bool ready=false, xray=false; int hudMode=-1; };
// Render-thread-only. Reuses the stock HUD/x-ray rasterisers on the actual
// D3D12 presentation queue. No depth/world layer or second simulation.
class Bridge {
public:
    Result Present(IDXGISwapChain* swap,ID3D12CommandQueue* queue,
                   const hudrender::Frame& hud,const xrayrender::Frame& xray);
    void Release();
private:
    bool Ensure(ID3D12CommandQueue* queue);
    Microsoft::WRL::ComPtr<ID3D12Device> device12_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<ID3D11Device> device11_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11On12Device> interop_;
};
}
