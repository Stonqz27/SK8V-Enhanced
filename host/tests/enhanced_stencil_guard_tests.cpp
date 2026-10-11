// The upstream snow stencil detours D3D11; Enhanced must not install it.
#include "board_stencil.h"
#include <cstdio>
#include <cstring>
namespace {
bool explained = false;
void Log(const char* text) { explained = std::strstr(text, "disabled (D3D12)") != nullptr; }
}
int main() {
    boardstencil::Start(Log);
    boardstencil::SetActive(true);
    const auto stats = boardstencil::Take();
    if (!explained || stats.immediate || stats.deferred || stats.seen || stats.gbuffer || stats.marked || boardstencil::TakeAllDraws()) {
        std::fputs("Legacy D3D11 stencil was not safely excluded\n", stderr);
        return 1;
    }
    boardstencil::Stop();
    boardstencil::Stop(); // unload before/after startup remains safe
    return 0;
}
