// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string_view>
#include "game_build.h"

// The ScriptHookV SDK's eGameVersion enum stops at 1.0.617; read the host
// executable's file version instead. SkateV targets GTA V Legacy 1.0.3889.0 only.
#if defined(SK8V_ENHANCED)
inline constexpr GameVersion kRequiredVersion = gamebuild::Enhanced;
#else
inline constexpr GameVersion kRequiredVersion = gamebuild::Legacy;
#endif

inline bool IsSupportedLegacy(const GameVersion& v) {
    return gamebuild::Same(v, gamebuild::Legacy);
}

inline bool IsSupportedHost(const GameVersion& v) {
    wchar_t path[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    const std::wstring_view full(path, n);
    const auto slash = full.find_last_of(L"\\/");
    const auto name = slash == std::wstring_view::npos ? full : full.substr(slash + 1);
#if defined(SK8V_ENHANCED)
    return gamebuild::SupportsEnhanced(v, name);
#else
    return gamebuild::SupportsLegacy(v, name);
#endif
}

inline GameVersion ReadHostVersion() {
    GameVersion out{};
    wchar_t path[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return out;
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &handle);
    if (size == 0) return out;
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(path, 0, size, data.data())) return out;
    VS_FIXEDFILEINFO* info = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &len) || !info) return out;
    out.major = HIWORD(info->dwFileVersionMS);
    out.minor = LOWORD(info->dwFileVersionMS);
    out.build = HIWORD(info->dwFileVersionLS);
    out.revision = LOWORD(info->dwFileVersionLS);
    return out;
}
