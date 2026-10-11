// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#pragma once
#include "game_probe.h"
#include <cstdint>

// Ped presentation: Skate's pose on the real GTA ped.
//
// GTA 1.0.3889.0 runs each ped's per-frame animation/IK pass from the ped's
// virtual method at vtable offset 0x208 (slot 65, image +0x7B0270); it ends
// with the bone object matrices that rendering skins from. Found with a
// guard-page write trace and static analysis of the in-memory image; see DECISIONS.
//
// The player's private vtable follows slot 65; two validated animation worker
// functions are also detoured, filtered by the exact player locals buffer.
// Enhanced bindings are edition-specific (animation_hooks.h); the diagnostic
// observation path counts calls without publishing or writing any Skate pose.
namespace posehook {

// `entity`: the player ped's address (ScriptHookV), `skeleton` its crSkeleton.
bool Install(std::uintptr_t entity, const probe::SkeletonInfo& skeleton, void (*log)(const char*));
void Uninstall(void (*log)(const char*));
struct Observation {
    std::uint64_t pedPasses=0, calls[2]{}, playerJobs[2]{}, badCounts[2]{};
    std::uintptr_t params[2]{}, locals[2]{};
    unsigned threads[2]{};
    bool jobsInstalled=false, tableRestored=true;
};
// Script thread. The same wrappers used by gameplay, with Apply disabled.
bool Observe(std::uintptr_t entity,const probe::SkeletonInfo& skeleton,void (*log)(const char*));
Observation ReadObservation();
void StopObservation(void (*log)(const char*));

// Script thread. Bone world matrices (GTA space), 16 floats per bone in RAGE
// row layout (X axis, Y axis, Z axis, translation).
void PublishPose(const float* bones, int count);
// Hand-offs between Skate and GTA (swim, climb, put away, take out): over
// `ms` the written pose fades between GTA's own animation pass and Skate's
// pose (rotations slerped, translations blended, in the ped's object space).
// FadeOut keeps Skate's last published pose, frozen relative to the ped, and
// ends with GTA's pose alone; FadeIn starts from GTA's pose. FadeOutDone()
// tells the script thread when Uninstall leaves nothing visible behind.
void FadeOut(int ms);
void FadeIn(int ms);
bool FadeOutDone();
// Gun out on the board (gun_pose.h): over `ms` the written pose takes GTA's
// own animated upper body (Spine1 and everything above it: the aim, the
// weapon grip, the head) while the hips and legs stay Skate's. GTA only
// animates the ped once it runs its own tasks, so the live clip must be off
// while this is on. Script thread.
void SetGun(bool on, int ms);
} // namespace posehook
