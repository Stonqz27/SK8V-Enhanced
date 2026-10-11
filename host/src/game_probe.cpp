// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#include "game_probe.h"

#include <windows.h>
#include <main.h>
#include <cstdint>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>

namespace probe {
namespace {

template <class T>
bool Read(std::uintptr_t address, T& out) { return ReadLive(address, &out, sizeof(T)); }

// GetFragInst virtual slot: its call site (pattern as used by
// ScriptHookVDotNet) carries the vtable slot byte at +0x1D. Scanned once;
// -2 when the pattern is not on this build.
int FragSlot() {
    static int slot = [] {
        auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
        std::uint8_t* const end = base + nt->OptionalHeader.SizeOfImage;
#if defined(SK8V_ENHANCED)
        // SHVDotNet Enhanced's Enhanced-specific GetFragInst call site.
        // Primary reference saved in port-evidence/shvdne-NativeMemory.cs.
        static const std::uint8_t kSite[] = {0x77, 0, 0x48, 0x8B, 0x03, 0x48, 0x89, 0xD9, 0xFF, 0x50, 0, 0x48, 0x85, 0xC0};
        int found = -2;
#else
        static const std::uint8_t kSite[] = {0x0F, 0x84, 0x8F, 0x00, 0x00, 0x00, 0x8A, 0x48, 0x28, 0x80, 0xE9, 0x02, 0x80, 0xF9, 0x03,
                                             0x0F, 0x87, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x10, 0x48, 0x8B, 0xC8, 0xFF, 0x52};
#endif
        for (std::uint8_t* p = base; p < end;) {
            MEMORY_BASIC_INFORMATION mbi{};
            if (!VirtualQuery(p, &mbi, sizeof(mbi))) break;
            auto* region = static_cast<std::uint8_t*>(mbi.BaseAddress);
            auto* regionEnd = region + mbi.RegionSize;
            const DWORD prot = mbi.Protect;
            const bool exec = (prot & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) &&
                              !(prot & (PAGE_GUARD | PAGE_NOACCESS));
            if (mbi.State == MEM_COMMIT && exec) {
                for (std::uint8_t* q = std::max(region, base); q + sizeof(kSite) <= regionEnd && q + sizeof(kSite) <= end; ++q)
#if defined(SK8V_ENHANCED)
                {
                    bool match = true;
                    for (std::size_t i = 0; i < sizeof(kSite); ++i)
                        if (i != 1 && i != 10 && q[i] != kSite[i]) { match = false; break; }
                    if (!match) continue;
                    const int candidate = static_cast<std::int8_t>(q[10]);
                    if (candidate < 8 || candidate > 120 || (candidate & 7) ||
                        (found >= 0 && found != candidate)) return -2;
                    found = candidate;
                }
#else
                    if (!std::memcmp(q, kSite, sizeof(kSite))) return static_cast<int>(static_cast<std::int8_t>(q[0x1D]));
#endif
            }
            p = regionEnd;
        }
#if defined(SK8V_ENHANCED)
        return found;
#else
        return -2;
#endif
    }();
    return slot;
}

} // namespace

int FragSlotOffset() { return FragSlot(); }

bool ReadLive(std::uintptr_t address, void* out, std::size_t size) {
    SIZE_T got = 0;
    return address && size <= 0x10000 &&
           ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address), out, size, &got) && got == size;
}

std::uintptr_t FragInstOf(std::uintptr_t entity) {
    const int slot = FragSlot();
    std::uintptr_t vtable = 0, fn = 0;
    if (slot < 0 || !entity || !Read(entity, vtable) || !Read(vtable + slot, fn) || !fn) return 0;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(fn), &info, sizeof info) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !(info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return 0;
    using GetFragInst = std::uint8_t*(__fastcall*)(std::uint8_t*);
    __try {
        return reinterpret_cast<std::uintptr_t>(reinterpret_cast<GetFragInst>(fn)(reinterpret_cast<std::uint8_t*>(entity)));
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Route 1: fragInst -> cache entry +0x68 -> +0x178. Route 2 (ScriptHookVDotNet's
// fallback): entity +0x50 -> +0x28.
bool ResolvePedSkeleton(int ped, SkeletonInfo& out, const char*& why) {
    out = SkeletonInfo{};
    const auto entity = reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(ped));
    if (!entity) {
        why = "no entity address for the ped handle";
        return false;
    }
    why = "GetFragInst call site not found";
    if (FragSlot() >= 0) {
        const std::uintptr_t frag = FragInstOf(entity);
        std::uintptr_t cache = 0;
        why = !frag ? "ped has no fragInst" : "fragInst has no cache entry";
        if (frag && Read(frag + 0x68, cache) && cache) {
            Read(cache + 0x178, out.skeleton);
            if (!out.skeleton) why = "cache entry has no skeleton";
        }
    }
    if (!out.skeleton) {
        std::uintptr_t draw = 0;
        if (Read(entity + 0x50, draw) && draw) Read(draw + 0x28, out.skeleton);
    }
    if (!out.skeleton) return false;
    why = "skeleton fields empty";
    std::uintptr_t data = 0;
    Read(out.skeleton + 0x00, data);
    Read(out.skeleton + 0x08, out.parentMtx);
    Read(out.skeleton + 0x10, out.objectMtx);
    Read(out.skeleton + 0x18, out.globalMtx);
    Read(out.skeleton + 0x20, out.count);
    if (data) Read(data + 0x20, out.bones); // crBoneData[0], 80 bytes each
    return data && out.parentMtx && out.objectMtx && out.globalMtx && out.bones && out.count > 0 && out.count <= 512;
}

bool SkeletonJson(const SkeletonInfo& s, std::string& json) {
    const auto num = [&json](float v) {
        char b[32];
        const auto r = std::to_chars(b, b + sizeof(b), v); // locale-independent, round-trip
        json.append(b, r.ptr);
    };
    const auto vec = [&](const float* v, int n) {
        json += '[';
        for (int i = 0; i < n; ++i) { if (i) json += ','; num(v[i]); }
        json += ']';
    };
    json = "{\"source\":\"live\",\"bones\":[";
    for (int i = 0; i < s.count; ++i) {
        unsigned char bone[80];
        if (!ReadLive(s.bones + 80 * i, bone, sizeof(bone))) return false;
        float r[4], t[3], sc[3];
        std::int16_t parent;
        std::uint16_t tag;
        std::uintptr_t namePtr;
        std::memcpy(r, bone + 0x00, 16);
        std::memcpy(t, bone + 0x10, 12);
        std::memcpy(sc, bone + 0x20, 12);
        std::memcpy(&parent, bone + 0x32, 2);
        std::memcpy(&namePtr, bone + 0x38, 8);
        std::memcpy(&tag, bone + 0x44, 2);
        // Malformed bindings must never become invalid JSON or animation input.
        if (parent < -1 || parent >= s.count) return false;
        for (float v : r) if (!std::isfinite(v)) return false;
        for (float v : t) if (!std::isfinite(v)) return false;
        for (float v : sc) if (!std::isfinite(v)) return false;
        char name[64] = {};
        if (namePtr) ReadLive(namePtr, name, sizeof(name) - 1);
        std::string clean;
        for (const char* c = name; *c; ++c) if (*c >= 0x20 && *c < 0x7f && *c != '"' && *c != '\\') clean += *c;
        if (i) json += ',';
        json += "{\"name\":\"" + clean + "\",\"tag\":" + std::to_string(tag) + ",\"parent\":" + std::to_string(parent) + ",\"t\":";
        vec(t, 3);
        json += ",\"r\":";
        vec(r, 4);
        json += ",\"s\":";
        vec(sc, 3);
        json += '}';
    }
    json += "]}";
    return s.count > 0;
}

int BoneIndexByTag(const SkeletonInfo& s, std::uint16_t tag) {
    for (int i = 0; i < s.count; ++i) {
        std::uint16_t t = 0;
        if (Read(s.bones + 80 * i + 0x44, t) && t == tag) return i;
    }
    return -1;
}

bool PedFragmentType(int ped, std::uintptr_t& type, const char*& why) {
    type = 0;
    const std::uintptr_t frag = FragInstOf(reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(ped)));
    why = frag ? "live fragment type unavailable" : "live fragment resolver unavailable";
    return frag && Read(frag + 0x78, type) && type;
}

} // namespace probe
