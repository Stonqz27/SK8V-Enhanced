#pragma once
// Wheel tracks in sand, mud, dirt, grass and snow: GTA's own vehicle tread
// decals (decals.dat 3011 sand, 3012 mud, 3013 dirt/grass, 3014 snow) laid
// along the board's back wheels where they touch the ground. Pure logic; the
// host places each mark with ADD_DECAL.
#include <functional>

namespace boardtracks {

struct Ground {
    float height = 0.0f;
    float normal[3] = {0.0f, 0.0f, 1.0f};
    unsigned material = 0; // GTA materials.dat index
};
using GroundFn = std::function<bool(const float point[3], float depth, Ground& out)>;

struct Mark {
    float position[3]; // middle of the segment, on the ground
    float direction[3]; // unit, along travel
    float normal[3];
    float length;
    int decal; // GTA's tread decal id for that ground (3011 sand .. 3014 snow): the host's tint
};

constexpr float kWheelRadius = 0.031f; // Skate's RETAIL_WHEEL_RADIUS
constexpr float kStep = 0.25f;         // travel per mark

// decals.dat id for a wheel on `material`; 0 leaves no track. In snow every
// walkable surface takes the snow tread.
int DecalFor(unsigned material, bool snow);

class Tracks {
public:
    void Reset() { lines_[0] = lines_[1] = {}; }
    // `wheels`: the two back wheel centres (GTA space). Returns the marks
    // laid this step (0..2) in `out`.
    int Step(const float wheels[2][3], bool snow, const GroundFn& ground, Mark out[2]);

private:
    struct Line {
        bool on = false;
        float at[3] = {};
        int decal = 0;
    };
    Line lines_[2];
};

} // namespace boardtracks
