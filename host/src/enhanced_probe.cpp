// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#include "enhanced_probe.h"
#include "game_build.h"
#include "game_probe.h"
#include "host_util.h"
#include "pose_hook.h"
#include "animation_hooks.h"
#include "live_clip_layout_enhanced.h"
#if defined(SK8V_ENHANCED_AUDIO_PROBE)
#include "enhanced_audio_probe.h"
#endif
#include "natives.h"
#include <windows.h>
#include <main.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

namespace enhancedprobe {
namespace {
#if defined(SK8V_ENHANCED_AUDIO_PROBE)
constexpr wchar_t Evidence[] = L"port-evidence\\live-audio";
#elif defined(SK8V_ENHANCED_GRAPHICS_PROBE)
constexpr wchar_t Evidence[] = L"port-evidence\\live-graphics";
#elif defined(SK8V_ENHANCED_RESOURCE_PROBE)
constexpr wchar_t Evidence[] = L"port-evidence\\live-resources";
#else
constexpr wchar_t Evidence[] = L"port-evidence\\live-animation";
#endif
std::atomic<int> g_renderer{0}; // 0 not seen, 11 D3D11, 12 D3D12, -1 unknown
std::atomic<unsigned> g_presents{0};
bool g_registered = false;
using gta::Call;

template<class T> bool Read(std::uintptr_t at, T& value) {
    return probe::ReadLive(at, &value, sizeof value);
}
bool ImageHeader(std::uintptr_t base, IMAGE_NT_HEADERS64& nt) {
    IMAGE_DOS_HEADER dos{};
    return Read(base, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
           dos.e_lfanew > 0 && dos.e_lfanew < 0x100000 &&
           Read(base + dos.e_lfanew, nt) && nt.Signature == IMAGE_NT_SIGNATURE &&
           nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
           nt.OptionalHeader.SizeOfImage > 0x2E98C30 && nt.OptionalHeader.SizeOfImage <= 512u * 1024u * 1024u;
}
void OnPresent(void* raw) {
    ++g_presents;
    if (!raw || g_renderer.load() != 0) return;
    auto* chain = static_cast<IDXGISwapChain*>(raw);
    ID3D12Device* device12 = nullptr;
    if (SUCCEEDED(chain->GetDevice(IID_PPV_ARGS(&device12))) && device12) {
        g_renderer = 12;
        device12->Release();
        return;
    }
    ID3D11Device* device11 = nullptr;
    if (SUCCEEDED(chain->GetDevice(IID_PPV_ARGS(&device11))) && device11) {
        g_renderer = 11;
        device11->Release();
    } else g_renderer = -1;
}

std::string Quote(const std::string& value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c >= 32) out += char(c);
    }
    return out + '"';
}

// Copy only the owned executable's mapped image, not other modules or private
// heaps. It is local reverse-engineering evidence, never a distributable asset.
// Yield every 512 KiB rather than hold GTA's script thread for a whole dump.
bool DumpImage(std::uintptr_t base, std::size_t size, void (*log)(const char*)) {
    std::ofstream out(std::filesystem::path(Evidence) / L"GTA5_Enhanced.mapped.bin", std::ios::binary);
    if (!out) { log("Enhanced port: cannot create local mapped-image evidence"); return false; }
    std::vector<unsigned char> bytes(0x10000);
    std::uint64_t copied = 0, missing = 0;
    unsigned chunks = 0;
    const auto start = GetTickCount64();
    // Pre-size to make holes explicit: inaccessible pages remain zero-filled.
    out.seekp(static_cast<std::streamoff>(size - 1)); out.put(0);
    for (std::uintptr_t at = base; at < base + size;) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(at), &region, sizeof region)) return false;
        const auto end = std::min(base + size, reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize);
        if (end <= at) return false;
        const bool readable = region.State == MEM_COMMIT && !(region.Protect & (PAGE_GUARD | PAGE_NOACCESS));
        while (at < end) {
            const auto count = std::min<std::size_t>(bytes.size(), end - at);
            if (readable && probe::ReadLive(at, bytes.data(), count)) {
                out.seekp(static_cast<std::streamoff>(at - base));
                out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(count));
                copied += count;
            } else missing += count;
            if (!out) return false;
            at += count;
            if (++chunks % 8 == 0) WAIT(0);
        }
    }
    out.close();
    std::ofstream info(std::filesystem::path(Evidence) / L"mapped-image.json");
    info << "{\"image_base\":" << base << ",\"image_size\":" << size
         << ",\"copied_bytes\":" << copied << ",\"zero_filled_unreadable_bytes\":" << missing
         << ",\"elapsed_ms\":" << GetTickCount64() - start
         << ",\"scope\":\"owned GTA5_Enhanced.exe image only\",\"game_memory_written\":false}";
    log("Enhanced port: owned mapped image captured locally; no game code was patched");
    return bool(info);
}

bool Snapshot(int ped, std::uintptr_t base, std::size_t imageSize, void (*log)(const char*)) {
    const auto entity = reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(ped));
    std::uintptr_t table = 0;
    if (!entity || !Read(entity, table)) return false;
    probe::SkeletonInfo skeleton;
    const char* why = "unresolved";
    const bool resolved = probe::ResolvePedSkeleton(ped, skeleton, why);
    std::string skeletonJson;
    const bool boneFormatValid = resolved && probe::SkeletonJson(skeleton, skeletonJson);
    if (boneFormatValid) {
        std::ofstream bones(std::filesystem::path(Evidence) / L"player-skeleton.json");
        bones << skeletonJson;
    }
    std::uintptr_t level = 0, records = 0;
    std::uint32_t capacity = 0;
    bool physics = Read(base + gamebuild::EnhancedPhysics::levelRva, level) && level &&
        Read(level + gamebuild::EnhancedPhysics::tableOffset, records) && records &&
        Read(level + gamebuild::EnhancedPhysics::capacityOffset, capacity) &&
        capacity > 0 && capacity <= gamebuild::EnhancedPhysics::maxCapacity;
    unsigned instances = 0, indicesMatch = 0;
    if (physics) {
        for (std::uint32_t i = 0; i < capacity; ++i) {
            std::uint64_t tagged = 0, vt = 0;
            if (!Read(records + i * gamebuild::EnhancedPhysics::recordStride +
                      gamebuild::EnhancedPhysics::instanceOffset, tagged)) { physics = false; break; }
            const auto instance = std::uintptr_t(tagged & ~std::uint64_t{15});
            if (instance < 0x10000 || !Read(instance, vt) || vt < base || vt >= base + imageSize) continue;
            ++instances;
            std::uint16_t ownIndex = 0;
            if (Read(instance + 0x18, ownIndex) && ownIndex == i) ++indicesMatch;
        }
        std::uintptr_t checkLevel = 0, checkRecords = 0;
        std::uint32_t checkCapacity = 0;
        physics = physics && Read(base + gamebuild::EnhancedPhysics::levelRva, checkLevel) &&
            checkLevel == level && Read(level + gamebuild::EnhancedPhysics::tableOffset, checkRecords) &&
            checkRecords == records && Read(level + gamebuild::EnhancedPhysics::capacityOffset, checkCapacity) &&
            checkCapacity == capacity;
    }
    std::ofstream out(std::filesystem::path(Evidence) / L"enhanced-probe.json");
    out << "{\n\"schema\":1,\"stage\":\"enhanced_bindings_probe\",\"full_gameplay_port_ready\":false,"
        << "\"game_memory_written\":false,\"legacy_hooks_entered\":false,\"runtime_started\":false,"
        << "\"version\":\"1.0.1158.16\",\"process_id\":" << GetCurrentProcessId()
        << ",\"image_base\":" << base << ",\"image_size\":" << imageSize
        << ",\"renderer\":" << g_renderer.load() << ",\"present_callbacks\":" << g_presents.load()
        << ",\"ped\":" << ped << ",\"model\":" << Call<unsigned>(gta::GET_ENTITY_MODEL, ped)
        << ",\"entity\":" << entity << ",\"vtable\":" << table
        << ",\"frag_slot_offset\":" << probe::FragSlotOffset()
        << ",\"skeleton_resolved\":" << (resolved ? "true" : "false")
        << ",\"bone_format_valid\":" << (boneFormatValid ? "true" : "false")
        << ",\"skeleton_reason\":" << Quote(resolved ? "resolved" : why)
        << ",\"skeleton\":" << skeleton.skeleton << ",\"bone_count\":" << skeleton.count
        << ",\"locals\":" << skeleton.objectMtx << ",\"objects\":" << skeleton.globalMtx
        << ",\"physics_level\":" << level << ",\"physics_records\":" << records
        << ",\"physics_capacity\":" << capacity << ",\"physics_table_coherent\":" << (physics ? "true" : "false")
        << ",\"validated_instances\":" << instances << ",\"own_indices_match\":" << indicesMatch
        << ",\"vtable_rvas\":[";
    for (unsigned i = 0; i < 128; ++i) {
        std::uintptr_t fn = 0;
        const bool inImage = Read(table + 8 * i, fn) && fn >= base && fn < base + imageSize;
        if (i) out << ',';
        out << (inImage ? fn - base : 0);
    }
    out << "]}\n";
    if (!out) return false;
    char line[256];
    std::snprintf(line, sizeof line, "Enhanced port: player %d: skeleton %s (%d bones), renderer D3D%d, physics %u/%u instances/index matches",
                  ped, resolved ? "resolved" : "unresolved", skeleton.count, g_renderer.load(), instances, indicesMatch);
    log(line);
    return true;
}

void ObserveAnimation(int ped,void (*log)(const char*)) {
    probe::SkeletonInfo skeleton;
    const char* why="unresolved";
    const auto entity=reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(ped));
    if(!probe::ResolvePedSkeleton(ped,skeleton,why) || !entity) {
        log("Enhanced animation probe: player skeleton unavailable; hooks not entered");return;
    }
    bool installed=false;
    // A worker can be inside the eight-byte prologue at the first attempt.
    // Do not force an unsafe patch; try again on subsequent script frames.
    for(unsigned attempt=0;attempt<30 && !installed;++attempt) {
        installed=posehook::Observe(entity,skeleton,log);
        if(!installed) WAIT(0);
    }
    if(!installed) {log("Enhanced animation probe: bindings rejected; normal animation preserved");return;}
    const auto start=GetTickCount64();
    bool playerChanged=false;
    log("Enhanced animation probe: observing 30 seconds; original jobs run, zero pose writes, no skate controls");
    while(GetTickCount64()-start<30000) {
        probe::SkeletonInfo current;
        if(Call<int>(gta::PLAYER_PED_ID)!=ped || !Call<bool>(gta::DOES_ENTITY_EXIST,ped) ||
           reinterpret_cast<std::uintptr_t>(getScriptHandleBaseAddress(ped))!=entity ||
           !probe::ResolvePedSkeleton(ped,current,why) || current.skeleton!=skeleton.skeleton ||
           current.objectMtx!=skeleton.objectMtx || current.globalMtx!=skeleton.globalMtx) {
            playerChanged=true;break;
        }
        WAIT(0);
    }
    posehook::StopObservation(log);
    // Retry restoration if a worker occupied the changing bytes. The retained
    // wrappers are already inactive (no skeleton) and continue the original.
    for(unsigned attempt=0;attempt<30 && posehook::ReadObservation().jobsInstalled;++attempt) {
        WAIT(0);posehook::StopObservation(log);
    }
    const auto s=posehook::ReadObservation();
    const bool verified=!playerChanged && s.pedPasses>0 && s.playerJobs[0]>0 && s.playerJobs[1]>0 &&
        !s.badCounts[0] && !s.badCounts[1] && s.locals[0]==skeleton.objectMtx &&
        s.locals[1]==skeleton.objectMtx && !s.jobsInstalled && s.tableRestored;
    std::ofstream out(std::filesystem::path(Evidence)/L"animation-probe.json");
    out<<"{\n\"schema\":1,\"stage\":\"enhanced_animation_job_observation\","
       <<"\"full_gameplay_port_ready\":false,\"runtime_started\":false,\"pose_memory_written\":false,"
       <<"\"temporary_game_code_patched\":true,\"legacy_hooks_entered\":false,"
       <<"\"version\":\"1.0.1158.16\",\"process_id\":"<<GetCurrentProcessId()
       <<",\"elapsed_ms\":"<<GetTickCount64()-start<<",\"player_changed\":"<<(playerChanged?"true":"false")
       <<",\"ped_pass_rva\":"<<animationhooks::EnhancedPedPass<<",\"ped_passes\":"<<s.pedPasses
       <<",\"script_thread\":"<<GetCurrentThreadId()<<",\"entity\":"<<entity
       <<",\"skeleton\":"<<skeleton.skeleton<<",\"locals\":"<<skeleton.objectMtx
       <<",\"objects\":"<<skeleton.globalMtx<<",\"bone_count\":"<<skeleton.count
       <<",\"hooks_restored\":"<<(!s.jobsInstalled?"true":"false")
       <<",\"ped_table_restored\":"<<(s.tableRestored?"true":"false")
       <<",\"player_job_bindings_verified\":"<<(verified?"true":"false")<<",\"jobs\":[";
    for(int i=0;i<2;++i) {
        if(i) out<<',';
        out<<"{\"rva\":"<<animationhooks::EnhancedJobs[i].rva
           <<",\"locals_offset\":"<<animationhooks::EnhancedJobs[i].localsOffset
           <<",\"count_offset\":"<<animationhooks::EnhancedJobs[i].countOffset
           <<",\"covered_prologue_bytes\":"<<animationhooks::EnhancedJobs[i].coveredBytes
           <<",\"all_calls\":"<<s.calls[i]<<",\"player_calls\":"<<s.playerJobs[i]
           <<",\"bad_player_counts\":"<<s.badCounts[i]<<",\"last_params\":"<<s.params[i]
           <<",\"last_locals\":"<<s.locals[i]<<",\"last_thread\":"<<s.threads[i]<<'}';
    }
    out<<"]}\n";
    char line[256];
    std::snprintf(line,sizeof line,"Enhanced animation probe: pass=%llu, player jobs=%llu/%llu, counts mismatched=%llu/%llu, bindings %s, restored=%s",
        static_cast<unsigned long long>(s.pedPasses),static_cast<unsigned long long>(s.playerJobs[0]),
        static_cast<unsigned long long>(s.playerJobs[1]),static_cast<unsigned long long>(s.badCounts[0]),
        static_cast<unsigned long long>(s.badCounts[1]),verified?"VERIFIED":"not yet verified",!s.jobsInstalled?"yes":"no");
    log(line);
}

void ObserveResources(int ped,void (*log)(const char*)) {
    constexpr auto model=util::Joaat("skatev_board");
    constexpr auto weapon=util::Joaat("WEAPON_SKATEBOARD");
    constexpr const char* dictionary="skatev_live";
    const bool modelValid=Call<bool>(gta::IS_MODEL_VALID,model);
    const bool inImage=Call<bool>(gta::IS_MODEL_IN_CDIMAGE,model);
    const bool weaponValid=Call<bool>(gta::IS_WEAPON_VALID,weapon);
    if(modelValid && inImage)Call<void>(gta::REQUEST_MODEL,model);
    Call<void>(gta::REQUEST_ANIM_DICT,dictionary);
    const auto start=GetTickCount64();
    bool modelLoaded=false,clipLoaded=false;
    while(GetTickCount64()-start<12000 && Call<int>(gta::PLAYER_PED_ID)==ped && Call<bool>(gta::DOES_ENTITY_EXIST,ped)) {
        modelLoaded=Call<bool>(gta::HAS_MODEL_LOADED,model);
        clipLoaded=Call<bool>(gta::HAS_ANIM_DICT_LOADED,dictionary);
        if((modelLoaded || !modelValid || !inImage) && clipLoaded)break;
        WAIT(0);
    }
    int object=0,bones=0;bool contract=false,deleted=true;
    if(modelLoaded) {
        const auto p=Call<Vector3>(gta::GET_ENTITY_COORDS,ped,true);
        // Isolated stationary presentation object, beside the player. No changes
        // to player tasks, collision, position, input or the animation workers.
        object=Call<int>(gta::CREATE_OBJECT_NO_OFFSET,model,p.x+0.8f,p.y,p.z-0.9f,false,false,false,0);
        if(object && Call<bool>(gta::DOES_ENTITY_EXIST,object)) {
            Call<void>(gta::SET_ENTITY_AS_MISSION_ENTITY,object,true,false);
            Call<void>(gta::FREEZE_ENTITY_POSITION,object,true);
            Call<void>(gta::SET_ENTITY_HAS_GRAVITY,object,false);
            Call<void>(gta::SET_ENTITY_COLLISION,object,false,false);
            probe::SkeletonInfo skeleton;const char* why="unresolved";
            for(unsigned attempt=0;attempt<60;++attempt) {
                if(probe::ResolvePedSkeleton(object,skeleton,why) && skeleton.count==7)break;
                WAIT(0);
            }
            bones=skeleton.count;
            std::uintptr_t data=0,parents=0;
            contract=bones==7 && Read(skeleton.skeleton,data) && data && Read(data+0x38,parents) && parents;
            int indices[7]{};constexpr int expected[7]={-1,0,0,1,2,1,2};
            if(contract)for(int i=0;i<7;++i) {
                indices[i]=probe::BoneIndexByTag(skeleton,static_cast<std::uint16_t>(i));
                if(indices[i]<0 || indices[i]>=7)contract=false;
            }
            if(contract)for(int i=0;i<7;++i) {
                std::int16_t parent=0;
                if(!Read(parents+sizeof(parent)*indices[i],parent) || parent!=(expected[i]<0?-1:indices[expected[i]]))contract=false;
            }
            log("Enhanced resource probe: native board shown beside player for eight seconds; normal GTA controls retained");
            const auto show=GetTickCount64();
            while(GetTickCount64()-show<8000 && Call<bool>(gta::DOES_ENTITY_EXIST,object))WAIT(0);
            const int handle=object;
            Call<void>(gta::DELETE_OBJECT,&object);
            deleted=!Call<bool>(gta::DOES_ENTITY_EXIST,handle);
        }
    }
    const bool verified=modelValid && inImage && modelLoaded && clipLoaded && weaponValid && contract && deleted;
    std::ofstream out(std::filesystem::path(Evidence)/L"resource-probe.json");
    out<<"{\"schema\":1,\"stage\":\"enhanced_resource_loading\",\"version\":\"1.0.1158.16\","
       <<"\"process_id\":"<<GetCurrentProcessId()<<",\"runtime_started\":false,\"full_gameplay_port_ready\":false,"
       <<"\"legacy_private_mount_hooks_entered\":false,\"player_tasks_changed\":false,\"pose_memory_written\":false,"
       <<"\"model_valid\":"<<(modelValid?"true":"false")<<",\"model_in_cdimage\":"<<(inImage?"true":"false")
       <<",\"model_loaded\":"<<(modelLoaded?"true":"false")<<",\"clip_loaded\":"<<(clipLoaded?"true":"false")
       <<",\"weapon_valid\":"<<(weaponValid?"true":"false")<<",\"board_bones\":"<<bones
       <<",\"board_skeleton_contract_verified\":"<<(contract?"true":"false")<<",\"board_deleted\":"<<(deleted?"true":"false")
       <<",\"clip_bone_tags\":"<<std::size(liveclip::kTracks)<<",\"clip_channels\":"<<liveclip::kChannels
       <<",\"resources_verified\":"<<(verified?"true":"false")<<"}\n";
    log(verified?"Enhanced resource probe: board, clip and weapon loading VERIFIED; full skating remains gated":"Enhanced resource probe: resource gate incomplete; detailed results saved; normal GTA controls retained");
}
}

void Attach() {
    if (!g_registered) { presentCallbackRegister(OnPresent); g_registered = true; }
}
void Detach() {
    if (g_registered) { presentCallbackUnregister(OnPresent); g_registered = false; }
}
void Run(void (*log)(const char*)) {
    std::filesystem::create_directories(Evidence);
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    IMAGE_NT_HEADERS64 nt{};
    if (!ImageHeader(base, nt)) { log("Enhanced port: invalid image header; no probes run"); return; }
    log("Enhanced port: waiting for a live Story Mode player; no skating controls are changed");
    for (;;) {
        const int player = Call<int>(gta::PLAYER_ID);
        const int ped = Call<int>(gta::PLAYER_PED_ID);
        if (ped && Call<bool>(gta::IS_PLAYER_PLAYING, player) &&
            Call<bool>(gta::DOES_ENTITY_EXIST, ped) && !gta::FrontendActive()) {
            // Let the model's skeleton and the first render callback settle.
            WAIT(1500);
            if (!Snapshot(ped, base, nt.OptionalHeader.SizeOfImage, log)) { WAIT(1000); continue; }
            // The first-stage read-only capture is preserved in ../live. This
            // stage reuses its static analysis, avoiding another 96 MB dump.
#if defined(SK8V_ENHANCED_AUDIO_PROBE)
            enhancedaudio::Run(ped,log);
#elif defined(SK8V_ENHANCED_GRAPHICS_PROBE)
            enhancedgraphics::Run(log);
#elif defined(SK8V_ENHANCED_RESOURCE_PROBE)
            ObserveResources(ped,log);
#else
            ObserveAnimation(ped,log);
#endif
            log("Enhanced port: bounded probe complete; full upstream skating remains gated");
            return;
        }
        WAIT(500);
    }
}
}
