// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace animationhooks {
struct Job {
    std::uintptr_t rva;
    int localsOffset;
    int countOffset;
    std::size_t coveredBytes;
    std::span<const std::uint8_t> signature;
};
// Original Legacy prologue: mov rax,rsp; push rbp; push rbx (five bytes).
inline constexpr std::uint8_t LegacyPrefix[] = {
    0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
// Enhanced LLVM prologues: four complete two-byte pushes occupy eight bytes.
// Function identity and buffer offsets come from the owned 1158.16 mapped
// image, including the callers that construct each job. Live use is still
// gated until the observation probe confirms the player's exact buffers.
inline constexpr std::uint8_t EnhancedComposePrefix[] = {
    0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x55,0x53,0x48,0x81,0xEC,0xA8,0x01,0,0};
inline constexpr std::uint8_t EnhancedLocalPrefix[] = {
    0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x53,0x48,0x81,0xEC,0,0x01,0,0};
inline constexpr std::uintptr_t LegacyPedPass = 0x7B0270;
inline constexpr std::uintptr_t EnhancedPedPass = 0x1B6F5C0;
inline constexpr std::array<Job,2> LegacyJobs{{
    {0x15B9948,0x20,0x28,5,LegacyPrefix},
    {0x15B8680,0x18,0x20,5,LegacyPrefix}}};
inline constexpr std::array<Job,2> EnhancedJobs{{
    {0x22D6C00,0x20,0x28,8,EnhancedComposePrefix},
    {0x22D5B90,0x18,0x20,8,EnhancedLocalPrefix}}};
}
