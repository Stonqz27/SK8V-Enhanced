// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#include "ped_bodies.h"

#include "game_probe.h"
#include <windows.h>
#if !defined(SK8V_PED_BODIES_READONLY_TEST)
#include <main.h>
#include "natives.h"
#endif
#include "ped_collider_builder.h"

#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <utility>

namespace pedbodies {
namespace {

struct Shape {
    std::uint8_t kind = 0;
    double relative[16]{}; // child bind @ inverse bone bind (row vectors)
    double centre[3]{};    // shape centre in the child frame
    float half[3]{};       // capsule: radius, half segment, radius; box: outer half extents
    float radius = 0.0f;   // capsule radius / box margin
    float mass = 0.0f;
    std::uint16_t boneTag = 0;
};
struct Compound {
    bool ok = false;
    std::vector<Shape> shapes;
};

float F(const std::array<std::uint8_t, 128>& raw, std::size_t at) {
    float v;
    std::memcpy(&v, raw.data() + at, 4);
    return v;
}

// Fragment type -> its compound (or the failure, remembered).
std::map<std::uintptr_t, Compound> g_types;
// (fragment type, live skeleton bones) -> bone index per shape.
std::map<std::pair<std::uintptr_t, std::uintptr_t>, std::vector<int>> g_bones;

Compound Decode(const pedcollision::builder::Memory& memory,std::uintptr_t type) {
    Compound c;
    std::vector<pedcollision::builder::Child> children;
    std::string why;
    std::uintptr_t group = 0, lod = 0, fragments = 0;
    if (pedcollision::builder::ReadCompound(memory, type, children, why) &&
        memory.Read(type + 0xf0, &group, 8) && group && memory.Read(group + 0x10, &lod, 8) && lod &&
        memory.Read(lod + 0xd0, &fragments, 8) && fragments) {
        c.ok = true;
        for (std::size_t i = 0; i < children.size() && c.ok; ++i) {
            const auto& child = children[i];
            Shape s;
            s.kind = child.kind;
            std::memcpy(s.relative, child.relative, sizeof(s.relative));
            s.boneTag = child.boneTag;
            // phBound: +0x14 bounding sphere radius, +0x20 box max, +0x2c
            // margin, +0x30 box min, +0x50 sphere centre (capsule centre).
            if (child.kind == pedcollision::builder::kCapsule) {
                s.radius = F(child.raw, 0x2c);
                const float segment = F(child.raw, 0x14) - s.radius;
                s.half[0] = s.half[2] = s.radius;
                s.half[1] = segment > 0.0f ? segment : 0.0f;
                for (int k = 0; k < 3; ++k) s.centre[k] = F(child.raw, 0x50 + 4 * k);
            } else {
                s.radius = F(child.raw, 0x2c);
                for (int k = 0; k < 3; ++k) {
                    const float lo = F(child.raw, 0x30 + 4 * k), hi = F(child.raw, 0x20 + 4 * k);
                    s.half[k] = (hi - lo) * 0.5f;
                    s.centre[k] = (hi + lo) * 0.5;
                }
            }
            // Authored pristine mass of the fragment child (+0x08).
            std::uintptr_t fragment = 0;
            c.ok = memory.Read(fragments + 8 * i, &fragment, 8) && fragment &&
                   memory.Read(fragment + 0x08, &s.mass, 4) && std::isfinite(s.mass) && s.mass > 0.0f &&
                   std::isfinite(s.radius) && s.radius > 0.0f;
            c.shapes.push_back(s);
        }
    }
    return c;
}
#if !defined(SK8V_PED_BODIES_READONLY_TEST)
const Compound& Load(std::uintptr_t type) {
    auto it = g_types.find(type);
    if (it != g_types.end()) return it->second;
    probe::LiveMemory memory;
    if (g_types.size() > 256) g_types.clear(), g_bones.clear();
    return g_types[type] = Decode(memory,type);
}
#endif

// Row-vector 4x4: out = a * b.
void Multiply(const double* a, const double* b, double* out) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            double s = 0.0;
            for (int k = 0; k < 4; ++k) s += a[r * 4 + k] * b[k * 4 + c];
            out[r * 4 + c] = s;
        }
}

bool ReadMatrix(const pedcollision::builder::Memory& memory,std::uintptr_t at, double* m) {
    float f[16];
    if (!memory.Read(at, f, sizeof(f))) return false;
    for (int i = 0; i < 16; ++i) m[i] = f[i];
    // RAGE packs metadata in column 3 of transform arrays.
    m[3] = m[7] = m[11] = 0.0;
    m[15] = 1.0;
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(m[i])) return false;
    return true;
}

bool Valid(const PoseView& pose) {
    return pose.bones && pose.parent && pose.objects && pose.count>0 && pose.count<=kMaxBones;
}
std::vector<int> BoneIndices(const pedcollision::builder::Memory& memory,const Compound& compound,const PoseView& pose) {
    std::vector<int> indices;
    for(const auto& s:compound.shapes) {
        int index=-1;
        for(int b=0;b<pose.count && index<0;++b) {
            std::uint16_t tag=0;
            if(!memory.Read(pose.bones+80ull*b+0x44,&tag,sizeof(tag)))return {};
            if(tag==s.boneTag)index=b;
        }
        indices.push_back(index);
    }
    return indices;
}
bool Append(const pedcollision::builder::Memory& memory,const Compound& compound,const PoseView& pose,
            const std::vector<int>& bones,std::uint32_t tag,SvVec3 velocity,SvVec3 angular,std::vector<SvPedPart>& out) {
    if(!compound.ok || compound.shapes.empty() || bones.size()!=compound.shapes.size() || !Valid(pose))return false;
    // The skeleton's object matrices are relative to its parent (entity) matrix.
    static thread_local std::vector<float> objects;
    objects.resize(static_cast<std::size_t>(pose.count) * 16);
    double parent[16];
    if (!ReadMatrix(memory,pose.parent, parent) ||
        !memory.Read(pose.objects, objects.data(), objects.size() * sizeof(float)))
        return false;
    const std::size_t start = out.size();
    for (std::size_t i = 0; i < compound.shapes.size(); ++i) {
        const Shape& s = compound.shapes[i];
        const int bone = bones[i];
        if (bone < 0 || bone >= pose.count) {
            out.resize(start);
            return false;
        }
        double object[16], boneWorld[16], world[16];
        for (int k = 0; k < 16; ++k) object[k] = objects[static_cast<std::size_t>(bone) * 16 + k];
        object[3] = object[7] = object[11] = 0.0;
        object[15] = 1.0;
        Multiply(object, parent, boneWorld);
        Multiply(s.relative, boneWorld, world);
        SvPedPart p{};
        p.size = sizeof(p);
        p.tag = tag;
        p.kind = s.kind;
        p.component = static_cast<std::uint32_t>(i);
        SvVec3* axes[3] = {&p.right, &p.forward, &p.up};
        for (int r = 0; r < 3; ++r) {
            const double l = std::sqrt(world[r * 4] * world[r * 4] + world[r * 4 + 1] * world[r * 4 + 1] +
                                       world[r * 4 + 2] * world[r * 4 + 2]);
            if (!(l > 1e-6) || !std::isfinite(l)) {
                out.resize(start);
                return false;
            }
            *axes[r] = {static_cast<float>(world[r * 4] / l), static_cast<float>(world[r * 4 + 1] / l),
                        static_cast<float>(world[r * 4 + 2] / l)};
        }
        double c[3];
        for (int k = 0; k < 3; ++k)
            c[k] = s.centre[0] * world[k] + s.centre[1] * world[4 + k] + s.centre[2] * world[8 + k] + world[12 + k];
        p.centre = {static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2])};
        p.half_extents = {s.half[0], s.half[1], s.half[2]};
        p.radius = s.radius;
        p.mass_kg = s.mass;
        p.linear_velocity = velocity;
        p.angular_velocity = angular;
        out.push_back(p);
    }
    return true;
}
} // namespace

bool CollectFromMemory(const pedcollision::builder::Memory& memory,std::uintptr_t type,const PoseView& pose,
                       std::uint32_t tag,SvVec3 velocity,SvVec3 angular,std::vector<SvPedPart>& out) {
    if(!Valid(pose))return false;
    const auto compound=Decode(memory,type);
    if(!compound.ok)return false;
    return Append(memory,compound,pose,BoneIndices(memory,compound,pose),tag,velocity,angular,out);
}
#if !defined(SK8V_PED_BODIES_READONLY_TEST)
bool Collect(int ped, std::uint32_t tag, std::vector<SvPedPart>& out) {
    std::uintptr_t type=0;const char* why="";
    if(!probe::PedFragmentType(ped,type,why))return false;
    probe::SkeletonInfo skeleton;
    if(!probe::ResolvePedSkeleton(ped,skeleton,why))return false;
    const PoseView pose{skeleton.bones,skeleton.parentMtx,skeleton.globalMtx,skeleton.count};
    if(!Valid(pose))return false;
    const auto& compound=Load(type);probe::LiveMemory memory;
    auto& indices=g_bones[{type,pose.bones}];
    if(indices.size()!=compound.shapes.size())indices=BoneIndices(memory,compound,pose);
    const auto v=gta::Call<Vector3>(gta::GET_ENTITY_VELOCITY,ped);
    const auto w=gta::Call<Vector3>(gta::GET_ENTITY_ROTATION_VELOCITY,ped);
    return Append(memory,compound,pose,indices,tag,{v.x,v.y,v.z},{w.x,w.y,w.z},out);
}
#endif

int NearestComponent(const std::vector<SvPedPart>& parts, std::uint32_t tag, SvVec3 point) {
    int best = -1;
    float bestD2 = 0.0f;
    for (const auto& p : parts) {
        if (p.tag != tag) continue;
        const float dx = p.centre.x - point.x, dy = p.centre.y - point.y, dz = p.centre.z - point.z;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (best < 0 || d2 < bestD2) best = static_cast<int>(p.component), bestD2 = d2;
    }
    return best;
}

} // namespace pedbodies
