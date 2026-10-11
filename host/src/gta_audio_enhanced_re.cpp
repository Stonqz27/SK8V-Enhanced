// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#include "gta_audio_enhanced_re.h"
#include "enhanced_audio_bindings.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace gtare {
namespace {
unsigned Nibble(char c) { return c <= '9' ? unsigned(c-'0') : unsigned(c-'a'+10); }
bool Match(const std::uint8_t* base, std::size_t size, const enhanced::Guard& g) {
    const auto bytes = std::strlen(g.hex)/2;
    if (g.rva > size || bytes > size-g.rva) return false;
    for (std::size_t i=0;i<bytes;++i)
        if (base[g.rva+i] != (Nibble(g.hex[i*2])*16+Nibble(g.hex[i*2+1]))) return false;
    return true;
}
}

void InitEnhancedParams(void* params) {
    // The compiler inlines this constructor. Defaults were independently
    // reconstructed from its stores, not copied from Legacy's constructor.
    std::memcpy(params, enhanced::ParamDefaults, sizeof(enhanced::ParamDefaults));
    static_cast<std::uint8_t*>(params)[0xA6] = 0xFF;
}

bool ResolveEnhancedImage(Audio& out, void* image, void (*log)(const char*)) {
    out = {};
    const auto* base = static_cast<const std::uint8_t*>(image);
    if (!base) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < sizeof(IMAGE_DOS_HEADER) || dos->e_lfanew > 4096)
        return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || nt->OptionalHeader.SizeOfImage != 95703552)
        return false;
    bool ok=true;
    char line[200];
    for (const auto& guard : enhanced::Guards) {
        const bool same=Match(base,nt->OptionalHeader.SizeOfImage,guard);
        std::snprintf(line,sizeof(line),"Enhanced audio: %s %s +0x%llx",same?"OK":"FAIL",guard.name,
                      static_cast<unsigned long long>(guard.rva));
        if(log)log(line);
        ok &= same;
    }
    if(!ok) { if(log)log("Enhanced audio: exact-build guards failed; no Legacy fallback"); return false; }
    const auto ptr=[&](std::uintptr_t rva) { return const_cast<std::uint8_t*>(base+rva); };
    out.initParamsCtor=InitEnhancedParams;
    out.createSoundByName=reinterpret_cast<decltype(out.createSoundByName)>(ptr(0x20E8330));
    out.createSoundByHash=reinterpret_cast<decltype(out.createSoundByHash)>(ptr(0x20E8370));
    out.initStreamPlayer=reinterpret_cast<decltype(out.initStreamPlayer)>(ptr(0x1FA18B0));
    out.prepareAndPlay=reinterpret_cast<decltype(out.prepareAndPlay)>(ptr(0x1FAB960));
    out.stopAndForget=reinterpret_cast<decltype(out.stopAndForget)>(ptr(0x1FABCC0));
    out.envCreate=reinterpret_cast<decltype(out.envCreate)>(ptr(0x1DA2F30));
    out.envInit=reinterpret_cast<decltype(out.envInit)>(ptr(0x1DC1B70));
    out.envSetPosition=reinterpret_cast<decltype(out.envSetPosition)>(ptr(0x1DC1CD0));
    out.envSetInteriorFromEntity=reinterpret_cast<decltype(out.envSetInteriorFromEntity)>(ptr(0x1DC1CE0));
    out.requestedSetPosition=reinterpret_cast<decltype(out.requestedSetPosition)>(ptr(0xA9C290));
    out.entityTracker=reinterpret_cast<decltype(out.entityTracker)>(ptr(0xC1AD30));
    out.frontendEntity=ptr(0x499DF20);
    out.initParamsBucket=ptr(0x289C0B8);
    out.settingsPoolBase=reinterpret_cast<const std::uintptr_t*>(ptr(0x4F1C920));
    out.settingsStride=reinterpret_cast<const std::uint32_t*>(ptr(0x4F1C8FC));
    out.settingsWriteIndex=reinterpret_cast<const std::uint32_t*>(ptr(0x3FA37C0));
    // Categories, room-send resets, Legacy's vtable observer and mixer flag
    // remain disabled until their own Enhanced semantics are verified.
    if(log)log("Enhanced audio: native stream bindings resolved; optional Legacy hooks remain disabled");
    return true;
}
}
