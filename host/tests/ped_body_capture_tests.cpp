// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#include "ped_bodies.h"
#include "ped_collider_builder.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>

#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);std::exit(1);}}while(0)
class Capture:public pedcollision::builder::Memory {
    struct Block {std::uint64_t at;std::vector<std::uint8_t> data;};
    std::vector<Block> blocks;
public:
    std::map<std::uint64_t,std::uint8_t> changed;
    std::uint64_t type=0;
    explicit Capture(const char* path) {
        std::ifstream in(path,std::ios::binary);CHECK(in);
        char magic[8];in.read(magic,8);CHECK(!std::memcmp(magic,"SVPMEM01",8));
        std::uint32_t count=0;in.read(reinterpret_cast<char*>(&type),8);in.read(reinterpret_cast<char*>(&count),4);
        CHECK(type>=0x10000 && count>0 && count<1000);
        for(unsigned i=0;i<count;++i) {
            Block b;std::uint32_t size=0;
            in.read(reinterpret_cast<char*>(&b.at),8);in.read(reinterpret_cast<char*>(&size),4);
            CHECK(b.at>=0x10000 && size>0 && size<=65536);
            b.data.resize(size);in.read(reinterpret_cast<char*>(b.data.data()),size);CHECK(in);
            blocks.push_back(std::move(b));
        }
        // Prefer each complete stable buffer to its overlapping field reads.
        std::sort(blocks.begin(),blocks.end(),[](const Block& a,const Block& b){return a.data.size()>b.data.size();});
    }
    bool Read(std::uintptr_t address,void* out,std::size_t size)const override {
        for(const auto& b:blocks) {
            if(address<b.at || address-b.at>b.data.size() || size>b.data.size()-(address-b.at))continue;
            std::memcpy(out,b.data.data()+(address-b.at),size);
            for(std::size_t i=0;i<size;++i) {
                auto value=changed.find(address+i);
                if(value!=changed.end())static_cast<std::uint8_t*>(out)[i]=value->second;
            }
            return true;
        }
        return false;
    }
    template<class T>T Get(std::uint64_t at)const {T t{};CHECK(Read(at,&t,sizeof(t)));return t;}
    template<class T>void Change(std::uint64_t at,T t) {
        auto* p=reinterpret_cast<const std::uint8_t*>(&t);
        for(std::size_t i=0;i<sizeof(t);++i)changed[at+i]=p[i];
    }
};
float Len(SvVec3 p){return std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z);}
int main(int argc,char** argv) {
    CHECK(argc==5);Capture capture(argv[1]);
    const auto data=capture.Get<std::uint64_t>(capture.Get<std::uint64_t>(capture.type+0x30)+0x18);
    const pedbodies::PoseView pose{capture.Get<std::uint64_t>(data+0x20),std::strtoull(argv[2],nullptr,10),
                                  std::strtoull(argv[3],nullptr,10),std::atoi(argv[4])};
    CHECK(pose.count==317 && pose.count<=pedbodies::kMaxBones);
    std::vector<SvPedPart> parts;
    CHECK(pedbodies::CollectFromMemory(capture,capture.type,pose,42,{1,2,3},{4,5,6},parts));
    CHECK(parts.size()==21);
    std::array<float,16> parent{};CHECK(capture.Read(pose.parent,parent.data(),sizeof(parent)));
    float mass=0;unsigned capsules=0,boxes=0;
    for(const auto& p:parts) {
        CHECK(p.size==sizeof(p) && p.tag==42 && p.mass_kg>0 && std::isfinite(p.mass_kg));mass+=p.mass_kg;
        CHECK(std::fabs(Len(p.right)-1)<0.001f && std::fabs(Len(p.forward)-1)<0.001f && std::fabs(Len(p.up)-1)<0.001f);
        CHECK(Len({p.centre.x-parent[12],p.centre.y-parent[13],p.centre.z-parent[14]})<5);
        CHECK(p.radius>0 && p.radius<1 && std::isfinite(p.radius));
        CHECK(p.half_extents.x>=0 && p.half_extents.y>=0 && p.half_extents.z>=0);
        CHECK(p.linear_velocity.x==1 && p.linear_velocity.y==2 && p.linear_velocity.z==3);
        CHECK(p.angular_velocity.x==4 && p.angular_velocity.y==5 && p.angular_velocity.z==6);
        capsules+=p.kind==1;boxes+=p.kind==3;
    }
    CHECK(capsules+boxes==21 && mass>20 && mass<200);
    // Unsupported skeleton counts must leave the caller's prior output alone.
    auto invalid=pose;invalid.count=pedbodies::kMaxBones+1;
    CHECK(!pedbodies::CollectFromMemory(capture,capture.type,invalid,43,{},{},parts) && parts.size()==21);
    const auto group=capture.Get<std::uint64_t>(capture.type+0xF0);
    const auto lod=capture.Get<std::uint64_t>(group+0x10);
    const auto composite=capture.Get<std::uint64_t>(lod+0xE8);
    capture.Change<std::uint8_t>(composite+0x10,0);
    CHECK(!pedbodies::CollectFromMemory(capture,capture.type,pose,43,{},{},parts) && parts.size()==21);
    capture.changed.clear();
    // A missing child tag cannot produce a partly updated body compound.
    std::set<std::uint16_t> tags;
    for(int i=0;i<pose.count;++i)tags.insert(capture.Get<std::uint16_t>(pose.bones+80ull*i+0x44));
    std::uint16_t absent=0;while(tags.contains(absent))++absent;
    const auto children=capture.Get<std::uint64_t>(lod+0xD0);
    const auto child=capture.Get<std::uint64_t>(children);
    capture.Change(child+0x12,absent);
    CHECK(!pedbodies::CollectFromMemory(capture,capture.type,pose,43,{},{},parts) && parts.size()==21);
    std::printf("Enhanced live-capture replay passed: %zu parts (%u capsules/%u boxes), 317 bones, mass %.3f kg; invalid compound/count/tag rejected atomically\n",parts.size(),capsules,boxes,mass);
}
