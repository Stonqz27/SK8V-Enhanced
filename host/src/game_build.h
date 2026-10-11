// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#pragma once
#include <cstdint>
#include <cstddef>
#include <string_view>

struct GameVersion {
    std::uint16_t major, minor, build, revision;
};

namespace gamebuild {
inline constexpr GameVersion Legacy{1, 0, 3889, 0};
inline constexpr GameVersion Enhanced{1, 0, 1158, 16};
constexpr bool Same(const GameVersion& a, const GameVersion& b) {
    return a.major == b.major && a.minor == b.minor &&
           a.build == b.build && a.revision == b.revision;
}
constexpr bool SupportsEnhanced(const GameVersion& version, std::wstring_view executable) {
    return Same(version, Enhanced) && executable == L"GTA5_Enhanced.exe";
}
constexpr bool SupportsLegacy(const GameVersion& version, std::wstring_view executable) {
    return Same(version, Legacy) && executable == L"GTA5.exe";
}

// Proven in the existing Enhanced 1158.16 collision reader and its captures.
// These are not Legacy's record-zero layout or its heap-scanning locator.
struct EnhancedPhysics {
    static constexpr std::uintptr_t levelRva = 0x2E98C28;
    static constexpr std::size_t capacityOffset = 0x44;
    static constexpr std::size_t tableOffset = 0x50;
    static constexpr std::size_t recordStride = 0x30;
    static constexpr std::size_t instanceOffset = 0x20;
    static constexpr std::uint32_t maxCapacity = 8192;
};
}
