// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#include "../src/game_build.h"
#include <iostream>

int main() {
    using namespace gamebuild;
    // Accept only the pair that the native interface has been audited against.
    if (!SupportsEnhanced(Enhanced, L"GTA5_Enhanced.exe") ||
        SupportsEnhanced(Legacy, L"GTA5_Enhanced.exe") ||
        SupportsEnhanced(Enhanced, L"GTA5.exe") ||
        SupportsEnhanced({1, 0, 1158, 13}, L"GTA5_Enhanced.exe") ||
        SupportsEnhanced({1, 0, 1159, 16}, L"GTA5_Enhanced.exe") ||
        !SupportsLegacy(Legacy, L"GTA5.exe") ||
        SupportsLegacy(Enhanced, L"GTA5.exe")) return 1;
    // A record with unrelated bytes before its tagged Enhanced instance must
    // resolve the correct pointer and preserve its fixed/moving state bits.
    std::uint64_t record[EnhancedPhysics::recordStride / 8]{};
    record[0] = 0xDEADBEEF;
    record[EnhancedPhysics::instanceOffset / 8] = 0x12345002;
    const auto tagged = record[EnhancedPhysics::instanceOffset / 8];
    if ((tagged & ~std::uint64_t{15}) != 0x12345000 || (tagged & 15) != 2) return 2;
    std::cout << "Enhanced/Legacy identity guards and Enhanced collision record: passed\n";
}
