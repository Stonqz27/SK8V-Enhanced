// Enhanced does not use GTA Legacy's D3D11 deferred snow pass.
#include "board_stencil.h"
#if defined(SK8V_ENHANCED)
namespace boardstencil {
void Start(void (*log)(const char*)) {
    if (log) log("SkateV Enhanced: Legacy D3D11 board snow stencil is disabled (D3D12)");
}
void Stop() {}
void SetActive(bool) {}
Stats Take() { return {}; }
unsigned TakeAllDraws() { return 0; }
} // namespace boardstencil
#else
#include "board_stencil.h"

#include <windows.h>
#include <d3d11.h>
#include <main.h>

#include "host_util.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

namespace boardstencil {

namespace {

// ID3D11DeviceContext vtable slots (d3d11.h declaration order).
constexpr int kDrawIndexed = 12, kDrawIndexedInstanced = 20;
constexpr UINT kMaterialMask = 7, kInterior = 8, kPed = 1;

using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
using DrawIndexedInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);

std::atomic<bool> g_active{false}, g_installed{false};
std::atomic<unsigned> g_all{0}, g_immediate{0}, g_deferred{0}, g_seen{0}, g_gbuffer{0}, g_marked{0}, g_logged{0};
void (*g_log)(const char*) = nullptr;

bool Matches(ID3D11ShaderResourceView* srv) {
    ID3D11Resource* res = nullptr;
    srv->GetResource(&res);
    if (!res) return false;
    D3D11_RESOURCE_DIMENSION dim{};
    res->GetType(&dim);
    bool match = false;
    if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        D3D11_TEXTURE2D_DESC d{};
        static_cast<ID3D11Texture2D*>(res)->GetDesc(&d);
        const bool rgba = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
                          d.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS || d.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS;
        match = rgba && d.ArraySize == 1 && d.Width == d.Height &&
                ((d.Width == 512 && d.MipLevels == 10) || (d.Width == 128 && d.MipLevels == 8));
        if (match && g_log && g_logged.fetch_add(1) < 8) {
            char line[128];
            std::snprintf(line, sizeof(line), "board stencil: drawing with a board texture %ux%u (format %d, %u mips)",
                          d.Width, d.Height, static_cast<int>(d.Format), d.MipLevels);
            g_log(line);
        }
    }
    res->Release();
    return match;
}

// Per thread (a deferred context records on its own thread): SRV -> board?
struct CacheEntry {
    const void* srv;
    bool board;
};
thread_local CacheEntry t_cache[4096];

// Read at draw time, so it holds however GTA bound the texture.
bool BoardBound(ID3D11DeviceContext* c) {
    ID3D11ShaderResourceView* srv = nullptr;
    c->PSGetShaderResources(0, 1, &srv);
    if (!srv) return false;
    CacheEntry& e = t_cache[(reinterpret_cast<std::uintptr_t>(srv) >> 4) & 4095];
    if (e.srv != srv) e = {srv, Matches(srv)};
    const bool board = e.board;
    srv->Release();
    return board;
}

// A G-buffer draw (3+ colour targets) of the board with a default material:
// re-marked for the draw, then the state is put back.
template <typename Draw>
void Marked(ID3D11DeviceContext* c, Draw draw) {
    g_all.fetch_add(1, std::memory_order_relaxed);
    if (!g_active.load(std::memory_order_relaxed)) return draw();
    (c->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED ? g_deferred : g_immediate).fetch_add(1, std::memory_order_relaxed);
    if (!BoardBound(c)) return draw();
    g_seen.fetch_add(1, std::memory_order_relaxed);
    ID3D11RenderTargetView* rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, nullptr);
    int targets = 0;
    for (auto* r : rtv)
        if (r) ++targets, r->Release();
    ID3D11DepthStencilState* dss = nullptr;
    UINT ref = 0;
    if (targets >= 3) c->OMGetDepthStencilState(&dss, &ref), g_gbuffer.fetch_add(1, std::memory_order_relaxed);
    if (!dss || (ref & (kMaterialMask | kInterior))) {
        if (dss) dss->Release();
        return draw();
    }
    c->OMSetDepthStencilState(dss, (ref & ~kMaterialMask) | kPed);
    draw();
    c->OMSetDepthStencilState(dss, ref);
    dss->Release();
    g_marked.fetch_add(1, std::memory_order_relaxed);
}

// The draw functions themselves are detoured: patching the context's table
// counted no draws (GTA calls D3D through pointers it took earlier), and a
// deferred context's table belongs to the context.
// Each detour is a 5-byte jump (one aligned 8-byte store) to a relay near
// the function; the trampoline runs the moved prologue and jumps back.
struct Detour {
    void* target = nullptr;
    void* trampoline = nullptr;
    std::uint64_t original = 0;
};
Detour g_detours[4];

template <int K>
void STDMETHODCALLTYPE DetourDrawIndexed(ID3D11DeviceContext* c, UINT count, UINT start, INT base) {
    const auto original = reinterpret_cast<DrawIndexedFn>(g_detours[K].trampoline);
    Marked(c, [&] { original(c, count, start, base); });
}

template <int K>
void STDMETHODCALLTYPE DetourDrawIndexedInstanced(ID3D11DeviceContext* c, UINT count, UINT instances, UINT start,
                                                  INT base, UINT startInstance) {
    const auto original = reinterpret_cast<DrawIndexedInstancedFn>(g_detours[K].trampoline);
    Marked(c, [&] { original(c, count, instances, start, base, startInstance); });
}

template <int... K>
void* Wrapper(bool instanced, int k, std::integer_sequence<int, K...>) {
    void* const plain[] = {reinterpret_cast<void*>(&DetourDrawIndexed<K>)...};
    void* const inst[] = {reinterpret_cast<void*>(&DetourDrawIndexedInstanced<K>)...};
    return instanced ? inst[k] : plain[k];
}

// Length of one x64 prologue instruction that can move as is (no
// RIP-relative operand, no branch); 0 otherwise.
int MovableLength(const std::uint8_t* p) {
    int n = 0;
    if ((p[0] & 0xF0) == 0x40) ++n; // REX
    const std::uint8_t op = p[n++];
    if (op >= 0x50 && op <= 0x57) return n; // push
    int imm = 0;
    switch (op) {
    case 0x89: case 0x8B: case 0x8D: case 0x85: case 0x33: case 0x31: case 0x2B: case 0x03: case 0x3B: break;
    case 0x83: imm = 1; break;
    case 0x81: imm = 4; break;
    default: return 0;
    }
    const std::uint8_t modrm = p[n++];
    const int mod = modrm >> 6, rm = modrm & 7;
    if (mod != 3 && rm == 4) {
        const std::uint8_t sib = p[n++];
        if (mod == 0 && (sib & 7) == 5) n += 4;
    } else if (mod == 0 && rm == 5) {
        return 0; // RIP-relative
    }
    if (mod == 1) n += 1;
    if (mod == 2) n += 4;
    return n + imm;
}

bool InstallDetour(int k, void* fn, bool instanced, char* why, std::size_t whyLen) {
    auto* t = static_cast<std::uint8_t*>(fn);
    int moved = 0;
    while (moved < 5) {
        const int n = MovableLength(t + moved);
        if (!n) break;
        moved += n;
    }
    if (moved < 5 || (reinterpret_cast<std::uintptr_t>(t) & 7) != 0) {
        std::snprintf(why, whyLen, "prologue %02x %02x %02x %02x %02x %02x %02x %02x not movable", t[0], t[1], t[2],
                      t[3], t[4], t[5], t[6], t[7]);
        return false;
    }
    auto* mem = static_cast<std::uint8_t*>(util::AllocNear(reinterpret_cast<std::uintptr_t>(t), 4096));
    if (!mem) return std::snprintf(why, whyLen, "no memory near it"), false;
    const std::intptr_t rel = reinterpret_cast<std::intptr_t>(mem) - reinterpret_cast<std::intptr_t>(t + 5);
    if (rel < INT32_MIN || rel > INT32_MAX) return std::snprintf(why, whyLen, "relay out of range"), false;
    void* wrap = Wrapper(instanced, k, std::make_integer_sequence<int, 4>{});
    std::uint8_t* relay = mem;      // jmp [rip+0]; dq wrapper
    std::uint8_t* tramp = mem + 32; // moved prologue; jmp [rip+0]; dq t + moved
    relay[0] = 0xFF, relay[1] = 0x25;
    std::memset(relay + 2, 0, 4);
    std::memcpy(relay + 6, &wrap, 8);
    std::memcpy(tramp, t, moved);
    tramp[moved] = 0xFF, tramp[moved + 1] = 0x25;
    std::memset(tramp + moved + 2, 0, 4);
    const std::uint8_t* back = t + moved;
    std::memcpy(tramp + moved + 6, &back, 8);
    FlushInstructionCache(GetCurrentProcess(), mem, 4096);
    Detour& d = g_detours[k];
    d = {fn, tramp, *reinterpret_cast<const std::uint64_t*>(t)};
    std::uint8_t patch[8];
    std::memcpy(patch, &d.original, 8);
    patch[0] = 0xE9;
    const auto rel32 = static_cast<std::int32_t>(rel);
    std::memcpy(patch + 1, &rel32, 4);
    std::uint64_t value;
    std::memcpy(&value, patch, 8);
    DWORD old = 0;
    VirtualProtect(t, 8, PAGE_EXECUTE_READWRITE, &old);
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(t), static_cast<LONG64>(value));
    VirtualProtect(t, 8, old, &old);
    FlushInstructionCache(GetCurrentProcess(), t, 8);
    return true;
}

void RemoveDetours() {
    for (Detour& d : g_detours) {
        if (!d.target) continue;
        DWORD old = 0;
        VirtualProtect(d.target, 8, PAGE_EXECUTE_READWRITE, &old);
        InterlockedExchange64(static_cast<volatile LONG64*>(d.target), static_cast<LONG64>(d.original));
        VirtualProtect(d.target, 8, old, &old);
        FlushInstructionCache(GetCurrentProcess(), d.target, 8);
        d.target = nullptr; // the relay and trampoline stay (a call may still be inside)
    }
}

// Inside a loaded module's image (a function pointer read from D3D).
bool InImage(void* fn) {
    HMODULE m = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              static_cast<LPCWSTR>(fn), &m) && m;
}

void Install(IDXGISwapChain* sc) {
    if (g_installed.load() || !sc) return;
    g_installed = true; // once, whatever happens below
    ID3D11Device* device = nullptr;
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) || !device) return;
    // The functions are read while each context is alive.
    void* fns[2][2] = {};
    ID3D11DeviceContext* ctx = nullptr;
    device->GetImmediateContext(&ctx);
    if (ctx) {
        void** vt = *reinterpret_cast<void***>(ctx);
        fns[0][0] = vt[kDrawIndexed], fns[0][1] = vt[kDrawIndexedInstanced];
        ctx->Release();
    }
    ctx = nullptr;
    if (SUCCEEDED(device->CreateDeferredContext(0, &ctx)) && ctx) {
        void** vt = *reinterpret_cast<void***>(ctx);
        fns[1][0] = vt[kDrawIndexed], fns[1][1] = vt[kDrawIndexedInstanced];
        ctx->Release();
    }
    device->Release();
    int detours = 0;
    for (int kind = 0; kind < 2; ++kind) {
        for (int instanced = 0; instanced < 2; ++instanced) {
            void* fn = fns[kind][instanced];
            char why[96] = "";
            bool done = false;
            for (const Detour& d : g_detours) done = done || (d.target && d.target == fn);
            if (!fn || !InImage(fn)) std::snprintf(why, sizeof(why), "function %p outside any module", fn);
            else if (!done && InstallDetour(detours, fn, instanced, why, sizeof(why))) ++detours;
            if (g_log) {
                char line[192];
                std::snprintf(line, sizeof(line), "board stencil: %s context %s %s%s%s", kind ? "deferred" : "immediate",
                              instanced ? "DrawIndexedInstanced" : "DrawIndexed",
                              *why ? "not hooked (" : done ? "shares the detour" : "detoured", why, *why ? ")" : "");
                g_log(line);
            }
        }
    }
}

void OnPresent(void* swapChain) { Install(static_cast<IDXGISwapChain*>(swapChain)); }

} // namespace

// ScriptHookV stops the game with a critical error when a callback it never registered is
// unregistered: GTA closed before ScriptMain reached Start (loading screen, no runtime).
static bool g_registered = false;

void Start(void (*log)(const char*)) {
    g_log = log;
    presentCallbackRegister(OnPresent);
    g_registered = true;
}

void Stop() {
    if (g_registered) presentCallbackUnregister(OnPresent);
    g_registered = false;
    g_active = false;
    RemoveDetours();
    g_installed = false;
}

void SetActive(bool on) { g_active.store(on, std::memory_order_relaxed); }

Stats Take() {
    return {g_immediate.exchange(0), g_deferred.exchange(0), g_seen.exchange(0), g_gbuffer.exchange(0),
            g_marked.exchange(0)};
}

unsigned TakeAllDraws() { return g_all.exchange(0); }

} // namespace boardstencil

#endif // SK8V_ENHANCED
