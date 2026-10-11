// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#include "../src/animation_hooks.h"
#include "../src/prologue_detour.h"
#include "../src/thread_patch.h"
#include <iostream>

namespace {
using Fn=std::uintptr_t(__fastcall*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);
Fn original=nullptr;
unsigned calls=0;
std::uintptr_t __fastcall Wrap(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d) {
    ++calls; return original(a,b,c,d)+100;
}
bool Execute(bool enhanced) {
    auto* mem=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!mem) return false;
    // Both functions really execute their original prologue, add all four
    // Win64 integer arguments, restore the pushed registers and return.
    // This catches instruction cuts, a wrong continuation and ABI damage.
    const std::uint8_t legacy[]={0x48,0x8B,0xC4,0x55,0x53,0x48,0x8D,0x04,0x11,0x4C,0x01,0xC0,0x4C,0x01,0xC8,0x5B,0x5D,0xC3};
    const std::uint8_t modern[]={0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x48,0x8D,0x04,0x11,0x4C,0x01,0xC0,0x4C,0x01,0xC8,0x41,0x5C,0x41,0x5D,0x41,0x5E,0x41,0x5F,0xC3};
    const std::span<const std::uint8_t> code=enhanced?std::span<const std::uint8_t>(modern):std::span<const std::uint8_t>(legacy);
    std::memcpy(mem,code.data(),code.size());
    prologuedetour::Bytes plan;
    const auto target=reinterpret_cast<std::uintptr_t>(mem);
    if(!prologuedetour::Plan({mem,code.size()},{mem,8},enhanced?8:5,target,target+64,
                           reinterpret_cast<std::uintptr_t>(&Wrap),plan)) return false;
    std::memcpy(mem+64,plan.relay.data(),plan.relay.size());
    std::memcpy(mem+96,plan.trampoline.data(),plan.trampoline.size());
    original=reinterpret_cast<Fn>(mem+96); const auto fn=reinterpret_cast<Fn>(mem);
    std::uint64_t saved=0,patched=0;std::memcpy(&saved,mem,8);std::memcpy(&patched,plan.patch.data(),8);
    FlushInstructionCache(GetCurrentProcess(),mem,4096);
    const bool baseline=fn(3,7,11,19)==40 && original(3,7,11,19)==40;
    const threadpatch::Write install{target,saved,patched};
    calls=0;
    const bool hooked=threadpatch::Apply({&install,1}) && fn(3,7,11,19)==140 && calls==1;
    const bool changedRejected=!threadpatch::Apply({&install,1});
    const threadpatch::Write restore{target,patched,saved};
    const bool restored=threadpatch::Apply({&restore,1}) && fn(3,7,11,19)==40 && calls==1;
    VirtualFree(mem,0,MEM_RELEASE);
    return baseline && hooked && changedRejected && restored;
}
}
int main() {
    if(!Execute(false) || !Execute(true)) return 1;
    prologuedetour::Bytes bytes;
    const auto& job=animationhooks::EnhancedJobs[0];
    auto changed=std::array<std::uint8_t,19>{};std::memcpy(changed.data(),job.signature.data(),19);changed[8]^=1;
    if(prologuedetour::Plan(changed,job.signature,8,0x1000,0x2000,0x3000,bytes) ||
       prologuedetour::Plan(job.signature,job.signature,8,0x1001,0x2000,0x3000,bytes) ||
       prologuedetour::Plan(job.signature,job.signature,6,0x1000,0x2000,0x3000,bytes) ||
       prologuedetour::Plan(job.signature,job.signature,8,0x1000,0x80002000,0x3000,bytes)) return 2;
    std::cout<<"Executed Legacy/Enhanced detours: four arguments, original continuation, return value, byte guard and restoration passed\n";
}
