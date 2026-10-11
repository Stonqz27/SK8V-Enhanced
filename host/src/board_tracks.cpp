#include "board_tracks.h"

#include <cmath>

namespace boardtracks {

int DecalFor(unsigned m, bool snow) {
    if (snow) return 3014;
    if (m >= 18 && m <= 24) return 3011;                          // SAND_*
    if (m >= 27 && m <= 30) return 3014;                          // SNOW_*
    if (m >= 36 && m <= 42) return 3012;                          // MUD_*, MARSH*
    if (m == 35 || (m >= 43 && m <= 48) || m == 53) return 3013;  // DIRT_TRACK, SOIL, CLAY, GRASS*, WOODCHIPS
    return 0;
}

int Tracks::Step(const float wheels[2][3], bool snow, const GroundFn& ground, Mark out[2]) {
    int n = 0;
    for (int w = 0; w < 2; ++w) {
        Line& line = lines_[w];
        const float* c = wheels[w];
        Ground g;
        // On the ground: the wheel centre one radius above it (along its normal).
        const bool grounded = ground(c, kWheelRadius + 0.08f, g) &&
                              std::fabs((c[2] - g.height) * g.normal[2] - kWheelRadius) < 0.035f;
        const int decal = grounded ? DecalFor(g.material, snow) : 0;
        if (!decal) {
            line.on = false;
            continue;
        }
        float p[3];
        for (int i = 0; i < 3; ++i) p[i] = c[i] - g.normal[i] * kWheelRadius;
        const float d[3] = {p[0] - line.at[0], p[1] - line.at[1], p[2] - line.at[2]};
        const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (!line.on || line.decal != decal || len > 1.5f) {
            line = {true, {p[0], p[1], p[2]}, decal};
            continue;
        }
        if (len < kStep) continue;
        Mark& m = out[n++];
        for (int i = 0; i < 3; ++i) {
            m.position[i] = (p[i] + line.at[i]) * 0.5f;
            m.direction[i] = d[i] / len;
            m.normal[i] = g.normal[i];
            line.at[i] = p[i];
        }
        m.length = len + 0.03f; // overlap: no seams between segments
        m.decal = decal;
    }
    return n;
}

} // namespace boardtracks
