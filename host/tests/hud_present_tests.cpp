// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#include "hud_renderer.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdio>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
namespace {
void Need(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
void Hr(HRESULT result,const char* message) { Need(SUCCEEDED(result),message); }
void Log(const char* line) { std::puts(line); }
hudrender::Frame Fixture() {
    hudrender::Frame f;f.visible=true;f.serial=1;f.width=64;f.height=64;f.textureGeneration=1;
    auto textures=std::make_shared<hudrender::TextureSet>();
    textures->push_back({1,1,{255,255,255,255}});f.textures=textures;
    for(const auto p: {std::pair<float,float>{0,0},{64,0},{0,64},{0,64},{64,0},{64,64}})
        f.vertices.push_back({p.first,p.second,0.5f,0.5f,{1,0.25f,0,0.5f},{0,0,0,0}});
    f.draws.push_back({0,0,6});return f;
}
void Pixels(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* buffer) {
    D3D11_TEXTURE2D_DESC d{};buffer->GetDesc(&d);
    d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
    ComPtr<ID3D11Texture2D> readback;Hr(device->CreateTexture2D(&d,nullptr,&readback),"readback allocation");
    context->CopyResource(readback.Get(),buffer);
    D3D11_MAPPED_SUBRESOURCE m{};Hr(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&m),"readback map");
    const auto* pixel=static_cast<const unsigned char*>(m.pData)+32*m.RowPitch+32*4;
    const int r=pixel[0],g=pixel[1],b=pixel[2],a=pixel[3];context->Unmap(readback.Get(),0);
    std::printf("HUD reference pixel: %d %d %d %d\n",r,g,b,a);
    // Straight-alpha HUD into an sRGB premultiplied target, then linear composite.
    Need(r>=186&&r<=190&&g>=97&&g<=101&&b<=1&&a>=254,"HUD colour/alpha regression");
}
}
int main() {
    try {
        const wchar_t* name=L"SK8VHudOffscreenTest";
        WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=name;
        Need(RegisterClassW(&wc)!=0,"test window class");
        HWND window=CreateWindowW(name,L"",WS_POPUP,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
        Need(window!=nullptr,"hidden test window"); // Never shown.
        DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=64;desc.BufferDesc.Height=64;
        desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=1;desc.OutputWindow=window;
        desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        ComPtr<IDXGISwapChain> swap;ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        Hr(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
           &desc,&swap,&device,nullptr,&context),"D3D11 WARP creation");
        hudrender::SetLog(Log);auto frame=Fixture();
        ComPtr<ID3D11Texture2D> buffer;Hr(swap->GetBuffer(0,IID_PPV_ARGS(&buffer)),"backbuffer");
        ComPtr<ID3D11RenderTargetView> target;Hr(device->CreateRenderTargetView(buffer.Get(),nullptr,&target),"clear target");
        const float black[4]={0,0,0,1};context->ClearRenderTargetView(target.Get(),black);
        Need(hudrender::Present(swap.Get(),frame)>=0,"HUD draw failed");Pixels(device.Get(),context.Get(),buffer.Get());
        frame.visible=false;Need(hudrender::Present(swap.Get(),frame)==-1,"hidden HUD rendered");
        frame.visible=true;context->ClearRenderTargetView(target.Get(),black);
        Need(hudrender::Present(swap.Get(),frame)>=0,"cached HUD draw failed");Pixels(device.Get(),context.Get(),buffer.Get());
        context->ClearState();target.Reset();buffer.Reset();
        Hr(swap->ResizeBuffers(1,96,48,DXGI_FORMAT_UNKNOWN,0),"backbuffer retained across frames");
        hudrender::Release();swap.Reset();context.Reset();device.Reset();
        DestroyWindow(window);UnregisterClassW(name,wc.hInstance);
        std::puts("HUD baseline PASS: linear colour, alpha, cached publication, hidden state, resize");return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
}
