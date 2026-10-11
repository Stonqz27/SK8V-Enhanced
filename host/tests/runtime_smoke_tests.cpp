// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
// Integration with the real release DLL and the owner's converted Skate data.
// The small synthetic phLevel is process-local: no GTA process is touched.
#include "runtime_bridge.h"
#include "hud_renderer.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace {
void Check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void Log(const char* s) { std::puts(s); }
struct Mem {
    void* p; explicit Mem(size_t n):p(VirtualAlloc(nullptr,n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)){Check(p!=nullptr,"allocation");}
    ~Mem(){VirtualFree(p,0,MEM_RELEASE);}
    uintptr_t at() const {return reinterpret_cast<uintptr_t>(p);}
    template<class T> void put(size_t o,const T& v){std::memcpy(static_cast<char*>(p)+o,&v,sizeof v);}
};
// Same Enhanced BVH/instance contract as live.rs' geometry regression. A 240 m
// flat test surface is deliberately NOT evidence of GTA's complete map.
struct Floor {
    Mem owner{0x60},table{0x30},inst{0x60},arch{0x30},bound{0x130},verts{24},polys{32},mats{8},pmi{8};
    Floor(){
        const int16_t v[4][3]={{-120,-120,0},{120,-120,0},{120,120,0},{-120,120,0}};
        std::memcpy(verts.p,v,sizeof v);
        const uint16_t ids[2][3]={{0,1,2},{0,2,3}};
        for(int t=0;t<2;++t)for(int i=0;i<3;++i)polys.put(16*t+4+2*i,ids[t][i]);
        bound.put(0x10,uint8_t(8));
        const float hi[3]={120,120,1},lo[3]={-120,-120,-1},scale[3]={1,1,1};
        std::memcpy(static_cast<char*>(bound.p)+0x20,hi,sizeof hi);
        std::memcpy(static_cast<char*>(bound.p)+0x30,lo,sizeof lo);
        std::memcpy(static_cast<char*>(bound.p)+0x90,scale,sizeof scale);
        bound.put(0x88,polys.at());bound.put(0xB0,verts.at());bound.put(0xD0,uint32_t(4));bound.put(0xD4,uint32_t(2));
        mats.put(0,uint8_t(4));bound.put(0xF0,mats.at());bound.put(0x118,pmi.at());bound.put(0x120,uint8_t(1));
        arch.put(0x20,bound.at());arch.put(0x28,uint32_t(0x3e));arch.put(0x2C,uint32_t(0x07f3bec0));
        inst.put(0,uintptr_t(0x2000));inst.put(0x10,arch.at());inst.put(0x18,uint16_t(0));
        const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        std::memcpy(static_cast<char*>(inst.p)+0x20,identity,sizeof identity);
        table.put(0x20,inst.at()|2);owner.put(0x44,uint32_t(1));owner.put(0x50,table.at());
    }
};
template<class T> T Export(HMODULE m,const char* n){auto f=reinterpret_cast<T>(GetProcAddress(m,n));Check(f!=nullptr,n);return f;}
float Distance(SvVec3 a,SvVec3 b){return std::sqrt((a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y)+(a.z-b.z)*(a.z-b.z));}
struct Hud {
    SvGetHudFrameFn frame;SvGetHudTextureFn texture;SvSetHudViewportFn viewport;void* rt;
    std::shared_ptr<hudrender::TextureSet> textures;
    Hud(HMODULE m,void* r):frame(Export<SvGetHudFrameFn>(m,"sv_get_hud_frame")),texture(Export<SvGetHudTextureFn>(m,"sv_get_hud_texture")),
        viewport(Export<SvSetHudViewportFn>(m,"sv_set_hud_viewport")),rt(r){
        SvHudViewport v{sizeof v,1280,720,1.f,2.4f,1};Check(viewport(rt,&v)!=0,"HUD viewport");
    }
    hudrender::Frame read(){
        SvHudFrame f{sizeof f};frame(rt,&f,nullptr,0,nullptr,0);
        std::vector<SvHudDraw>d(f.draw_count);std::vector<SvHudVertex>v(f.vertex_count);
        Check(frame(rt,&f,d.data(),uint32_t(d.size()),v.data(),uint32_t(v.size()))!=0,"HUD read");
        if(!textures || textures->size()!=f.texture_count){
            textures=std::make_shared<hudrender::TextureSet>();
            for(uint32_t i=0;i<f.texture_count;++i){SvHudTexture t{sizeof t};Check(texture(rt,i,&t,nullptr,0)!=0,"HUD texture header");
                hudrender::Texture dst{t.width,t.height};dst.rgba.resize(size_t(t.width)*t.height*4);
                Check(texture(rt,i,&t,dst.rgba.data(),uint32_t(dst.rgba.size()))!=0,"HUD texture pixels");textures->push_back(std::move(dst));}
        }
        hudrender::Frame out;out.visible=f.visible!=0;out.serial=f.serial;out.width=f.width;out.height=f.height;
        out.textures=textures;out.textureGeneration=f.texture_generation;
        for(auto x:d){Check(x.first_vertex+x.vertex_count<=v.size(),"HUD vertex range");out.draws.push_back({x.texture,uint32_t(out.vertices.size()),x.vertex_count});
            for(uint32_t j=0;j<x.vertex_count;++j){auto a=v[x.first_vertex+j];hudrender::Vertex b{a.x,a.y,a.u,a.v};
                std::memcpy(b.mul,x.multiply,sizeof b.mul);std::memcpy(b.add,x.add,sizeof b.add);out.vertices.push_back(b);}}
        return out;
    }
};
void Screenshot(const hudrender::Frame& f,const std::filesystem::path& path){
    ComPtr<ID3D11Device>d;ComPtr<ID3D11DeviceContext>c;D3D_FEATURE_LEVEL level;
    Check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&d,&level,&c)),"WARP device");
    D3D11_TEXTURE2D_DESC desc{};desc.Width=f.width;desc.Height=f.height;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D>target;Check(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&target)),"HUD target");
    ComPtr<ID3D11RenderTargetView>view;Check(SUCCEEDED(d->CreateRenderTargetView(target.Get(),nullptr,&view)),"HUD RTV");
    const float bg[4]={0.02f,0.025f,0.035f,1};c->ClearRenderTargetView(view.Get(),bg);
    hudrender::SetLog(Log);Check(hudrender::RenderTexture(d.Get(),target.Get(),f)>=0,"original HUD render");
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D>staging;Check(SUCCEEDED(d->CreateTexture2D(&desc,nullptr,&staging)),"staging");c->CopyResource(staging.Get(),target.Get());
    D3D11_MAPPED_SUBRESOURCE map{};Check(SUCCEEDED(c->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)),"HUD readback");
    std::vector<uint8_t>pixels(size_t(f.width)*f.height*4);size_t changed=0;
    for(UINT y=0;y<f.height;++y)for(UINT x=0;x<f.width;++x){const auto* src=static_cast<uint8_t*>(map.pData)+y*map.RowPitch+x*4;auto* dst=&pixels[(size_t(y)*f.width+x)*4];
        dst[0]=src[2];dst[1]=src[1];dst[2]=src[0];dst[3]=255;if(src[0]>30 || src[1]>30 || src[2]>30)++changed;}
    c->Unmap(staging.Get(),0);Check(changed>100,"HUD produced no visible pixels");
    BITMAPFILEHEADER fh{0x4D42,DWORD(54+pixels.size()),0,0,54};BITMAPINFOHEADER ih{40,LONG(f.width),-LONG(f.height),1,32,BI_RGB,DWORD(pixels.size()),0,0,0,0};
    std::ofstream s(path,std::ios::binary);s.write(reinterpret_cast<char*>(&fh),sizeof fh);s.write(reinterpret_cast<char*>(&ih),sizeof ih);s.write(reinterpret_cast<char*>(pixels.data()),pixels.size());
    hudrender::Release();std::printf("Original HUD screenshot: %zu visible pixels\n",changed);
}
}
int wmain(int argc,wchar_t** argv){
    RuntimeBridge rt;
    try{
        Check(argc==2,"pass the Enhanced project root");const std::filesystem::path root(argv[1]);
        const auto evidence=root/L"port-evidence/runtime-smoke";std::filesystem::create_directories(evidence);
        Floor floor;std::string error;
        const auto data=(root/L"local/skate-data/assets").string(),cache=(evidence/L"fixture.svwc").string(),log=(evidence/L"runtime.log").string();
        Check(rt.Load((root/L"rust/target/release/SkateVRuntime.dll").c_str(),data,cache,log,3000,3,error),error.c_str());
        auto module=GetModuleHandleW(L"SkateVRuntime.dll");Hud hud(module,rt.Handle());
        Check(rt.SetPhysicsLevel(floor.owner.at(),0x1000,0x7FFF00000000)!=0,"Enhanced physics owner");
        const auto peds=(root/L"local/peds-enhanced").string();SvCharacter character{sizeof character};character.model_hash=0x9B22DBAF;character.cache_root_utf8=peds.c_str();
        Check(rt.SetCharacter(&character),"Franklin request");
        const auto audioConfigure=Export<SvAudioConfigureFn>(module,"sv_audio_configure");
        const auto audioStatus=Export<SvAudioGetStatusFn>(module,"sv_audio_get_status");
        const auto audioEmitters=Export<SvAudioEmittersFn>(module,"sv_audio_emitters");
        const auto audioPull=Export<SvAudioPullFn>(module,"sv_audio_pull");
        const auto audioCache=(root/L"local/audio").string();SvAudioConfig ac{sizeof ac,48000,audioCache.c_str(),1.f,0};
        Check(audioConfigure(rt.Handle(),&ac)!=0,"original audio configure");
        SvLifecycleTrack track{sizeof track,0,{0,0,0},0};Check(rt.LifecycleTrack(track),"background preparation");
        SvOutput o{};SvLifecycleState life{};const auto deadline=GetTickCount64()+90000;
        while(GetTickCount64()<deadline){Check(rt.Output(o),"output");Check(o.status!=SV_STATUS_ERROR,rt.StatusText().c_str());rt.LifecycleState(life);if(life.prepared)break;Sleep(25);}
        Check(life.prepared!=0,"background preparation timed out");
        SvAudioStatus as{sizeof as};const auto audioDeadline=GetTickCount64()+120000;
        while(GetTickCount64()<audioDeadline){Check(audioStatus(rt.Handle(),&as)!=0,"audio status");
            Check(as.state!=SV_AUDIO_ERROR,as.message_utf8);if(as.state==SV_AUDIO_READY)break;Sleep(25);}
        Check(as.state==SV_AUDIO_READY,as.message_utf8);
        auto activate=Export<SvRequestActivateFn>(module,"sv_request_activate");SvSpawn spawn{sizeof spawn,{0,0,0},0,1280.f/720};Check(activate(rt.Handle(),&spawn)!=0,"activation");
        const auto activeDeadline=GetTickCount64()+90000;
        while(GetTickCount64()<activeDeadline){rt.Output(o);Check(o.status!=SV_STATUS_ERROR,rt.StatusText().c_str());if(o.status==SV_STATUS_ACTIVE)break;Sleep(25);}
        Check(o.status==SV_STATUS_ACTIVE,"activation timed out");
        std::vector<float> pose(512*16);hudrender::Frame best;SvVec3 start=o.skater_position;
        std::ofstream trace(evidence/L"trace.csv");trace<<"tick,state,skater_x,skater_y,skater_z,board_x,board_y,board_z,camera_valid,bones,hud_draws\n";
        uint32_t poses=0,cameras=0,hudFrames=0;uint64_t firstTick=o.tick;float maxTravel=0,maxRidingSeparation=0;uint32_t xrayMax=0;
        double audioEnergy=0;uint64_t audioSamples=0;std::vector<float>pcm(4096);
        const auto xray=Export<SvGetXrayFn>(module,"sv_get_xray");
        for(int frame=0;frame<600;++frame){
            SvInput input{sizeof input,1.f/60,1280.f/720};input.pad.connected=1;input.pad.packet=frame+1;
            // Push, crouch/pop an ollie, then dismount using Skate's Y action.
            if(frame<220 && frame%60<30)input.pad.buttons=0x1000;
            if(frame>=120 && frame<142)input.pad.right_y=-32767;
            if(frame>=142 && frame<145)input.pad.right_y=32767;
            if(frame>=280 && frame<284)input.pad.buttons=0x8000;
            Check(rt.Step(input),"step");Sleep(17);Check(rt.Output(o),"stepped output");Check(o.status==SV_STATUS_ACTIVE,"runtime dropped active state");
            const auto bones=rt.CharacterPose(pose.data(),512);Check(bones==317,"Franklin retarget not 317 bones");
            Check(std::all_of(pose.begin(),pose.begin()+bones*16,[](float x){return std::isfinite(x);}),"non-finite pose");++poses;
            SvBoardPose board{};Check(rt.BoardPose(board)&&board.bone_count==7,"board pose contract");Check(board.tick==o.tick,"board/output publication mismatch");
            if(o.camera_valid){Check(std::isfinite(o.camera_position.x)&&std::isfinite(o.camera_position.z)&&o.camera_fov>0,"camera invalid");++cameras;}
            maxTravel=std::max(maxTravel,Distance(start,o.skater_position));
            if(std::strstr(o.state_utf8,"PhysicsGround"))maxRidingSeparation=std::max(maxRidingSeparation,Distance(o.skater_position,o.board_position));
            auto f=hud.read();if(f.visible&&!f.draws.empty()){++hudFrames;if(f.vertices.size()>best.vertices.size())best=std::move(f);}
            SvXrayFrame xf{sizeof xf};xray(rt.Handle(),&xf,nullptr,0);xrayMax=std::max(xrayMax,xf.vertex_count);
            SvAudioEmitter emitters[3]{};for(auto& e:emitters)e.size=sizeof e;
            Check(audioEmitters(rt.Handle(),emitters,3)==3,"audio emitter publication");
            for(uint32_t i=0;i<3;++i){const auto n=std::min(uint32_t(pcm.size()),emitters[i].queued_frames);if(!n)continue;
                SvAudioEmitter emitter{sizeof emitter};const auto got=audioPull(rt.Handle(),i,pcm.data(),n,&emitter);
                for(uint32_t k=0;k<got;++k){Check(std::isfinite(pcm[k]),"audio non-finite");audioEnergy+=double(pcm[k])*pcm[k];}audioSamples+=got;}
            if(frame%6==0)trace<<o.tick<<','<<o.state_utf8<<','<<o.skater_position.x<<','<<o.skater_position.y<<','<<o.skater_position.z<<','<<o.board_position.x<<','<<o.board_position.y<<','<<o.board_position.z<<','<<o.camera_valid<<','<<bones<<','<<best.draws.size()<<'\n';
        }
        Check(o.tick>firstTick+500,"simulation did not advance");Check(maxTravel>2,"pushing did not move the skater");Check(cameras>500,"camera publication missing");
        Check(hudFrames>0&&best.textures&&!best.textures->empty(),"original HUD publication missing");Check(maxRidingSeparation<2,"riding board separated from skater");
        Check(audioStatus(rt.Handle(),&as)!=0&&as.state==SV_AUDIO_READY&&audioSamples>0&&audioEnergy>0.0001,"original Skate event audio silent");
        // Change the real cached skeleton while the same session keeps running.
        for(const auto model:{std::pair<uint32_t,uint32_t>{0x0D7114C9,354},{0x9B810FA2,282}}){
            character.model_hash=model.first;Check(rt.SetCharacter(&character),"protagonist request");uint32_t bones=0;
            const auto modelDeadline=GetTickCount64()+40000;
            while(GetTickCount64()<modelDeadline){SvInput neutral{sizeof neutral,1.f/60,1280.f/720};neutral.pad.connected=1;rt.Step(neutral);Sleep(17);
                // Pose/board exports intentionally retain the frame selected by
                // Output: refresh it exactly as the real GTA host does.
                Check(rt.Output(o),"protagonist frame publication");
                bones=rt.CharacterPose(pose.data(),512);if(bones==model.second)break;}
            Check(bones==model.second,"protagonist bone count");Check(std::all_of(pose.begin(),pose.begin()+bones*16,[](float x){return std::isfinite(x);}),"protagonist pose invalid");
        }
        Screenshot(best,evidence/L"original-hud.bmp");rt.LifecycleState(life);Check(life.session_builds==1,"more than one Skate session");
        Check(rt.Deactivate(),"deactivate");Sleep(100);rt.Output(o);Check(o.status==SV_STATUS_READY,"deactivation did not restore ready");
        std::ofstream result(evidence/L"result.json");result<<"{\"schema\":1,\"passed\":true,\"synthetic_floor\":true,\"gta_process_touched\":false,\"session_builds\":"<<life.session_builds<<",\"pose_frames\":"<<poses<<",\"bones\":317,\"protagonist_bones\":[317,354,282],\"board_bones\":7,\"camera_frames\":"<<cameras<<",\"hud_frames\":"<<hudFrames<<",\"hud_textures\":"<<best.textures->size()<<",\"hud_draws\":"<<best.draws.size()<<",\"max_travel_metres\":"<<maxTravel<<",\"max_riding_separation_metres\":"<<maxRidingSeparation<<",\"xray_max_vertices\":"<<xrayMax<<",\"audio_samples\":"<<audioSamples<<",\"audio_energy\":"<<audioEnergy<<",\"audio_underruns\":"<<as.underruns<<"}\n";
        std::printf("Runtime integration PASS: %u poses, %u cameras, %u HUD frames; travel %.2f m; x-ray %u vertices\n",poses,cameras,hudFrames,maxTravel,xrayMax);
        rt.Release();Sleep(100);return 0;
    }catch(const std::exception&e){std::fprintf(stderr,"Runtime integration FAIL: %s\n",e.what());rt.Release();Sleep(100);return 1;}
}
