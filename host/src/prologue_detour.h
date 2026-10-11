// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace prologuedetour {
struct Bytes {
    std::array<std::uint8_t,8> patch{};
    std::array<std::uint8_t,32> trampoline{};
    std::array<std::uint8_t,14> relay{};
};
// Only audited relocation-free prologues are accepted. The caller supplies
// a whole-instruction boundary, not a generic five-byte disassembly guess.
inline bool Plan(std::span<const std::uint8_t> original,
                 std::span<const std::uint8_t> signature, std::size_t covered,
                 std::uintptr_t target, std::uintptr_t relay, std::uintptr_t wrapper,
                 Bytes& out) {
    if ((target & 7) || (covered != 5 && covered != 8) || signature.size() < 8 ||
        original.size() < signature.size() ||
        std::memcmp(original.data(),signature.data(),signature.size())) return false;
    const auto delta = static_cast<std::int64_t>(relay) - static_cast<std::int64_t>(target + 5);
    if (delta < std::numeric_limits<std::int32_t>::min() ||
        delta > std::numeric_limits<std::int32_t>::max()) return false;
    out = {};
    std::memcpy(out.patch.data(),original.data(),8);
    out.patch[0] = 0xE9;
    const auto rel = static_cast<std::int32_t>(delta);
    std::memcpy(out.patch.data()+1,&rel,4);
    for (std::size_t i=5;i<covered;++i) out.patch[i]=0x90;
    std::memcpy(out.trampoline.data(),original.data(),covered);
    out.trampoline[covered]=0xFF; out.trampoline[covered+1]=0x25;
    const auto back=target+covered;
    std::memcpy(out.trampoline.data()+covered+6,&back,8);
    out.relay[0]=0xFF; out.relay[1]=0x25;
    std::memcpy(out.relay.data()+6,&wrapper,8);
    return true;
}
}
