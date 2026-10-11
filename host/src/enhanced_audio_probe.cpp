// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#include "enhanced_audio_probe.h"
#include "gta_audio_enhanced_re.h"
#include "gta_audio_dsp.h"
#include "game_probe.h"
#include "natives.h"
#include <main.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace enhancedaudio {
namespace {
template<class T> bool Read(std::uintptr_t at,T& value) {
    return at>=0x10000 && probe::ReadLive(at,&value,sizeof(value));
}
struct Trial {
    // GTA retains the address of the sound reference and may clear it from
    // another thread. These slots and host-owned rings live for the session.
    void* volatile reference=nullptr;
    void* group=nullptr;
    gtaaudio::GtaRingBuffer* ring=nullptr;
    bool positional=false,stream=false,played=false,stopped=false,released=false;
    unsigned fault=0,step=0,readerChanges=0,bytesWritten=0,underruns=0;
    LONG maxReferences=1,finalReferences=1;
    std::uint64_t elapsed=0;
};
Trial trials[2];
gtare::Audio audio;
constexpr int Rate=48000;
constexpr unsigned RingBytes=32768;

bool Settings(void* sound,void*& result) {
    result=nullptr;
    std::uint8_t slot=0xFF,bucket=0xFF;
    std::uintptr_t pool=0,block=0;std::uint32_t stride=0;
    const auto ptr=reinterpret_cast<std::uintptr_t>(sound);
    if(!Read(ptr+0x80,slot) || slot==0xFF || !Read(ptr+0x62,bucket) || bucket>63 ||
       !Read(reinterpret_cast<std::uintptr_t>(audio.settingsPoolBase),pool) ||
       !Read(reinterpret_cast<std::uintptr_t>(audio.settingsStride),stride) || stride<0x100 || stride>8192 ||
       !Read(pool+bucket*gtare::kSettingsBucketStride+gtare::kSettingsBlockOffset,block))return false;
    std::uint8_t readable[0x100];
    const auto address=block+slot*static_cast<std::uintptr_t>(stride);
    if(block<0x10000 || !probe::ReadLive(address,readable,sizeof(readable)))return false;
    result=reinterpret_cast<void*>(address);return true;
}
bool StreamSlot(void* sound) {
    // InitStreamPlayer uses the separate dword slot at +0xB4, not the
    // requested-settings byte at +0x80. Verify both before passing the ring.
    const auto ptr=reinterpret_cast<std::uintptr_t>(sound);
    std::uint8_t bucket=0xFF;std::uint32_t slot=0xFFFFFFFF,stride=0;
    std::uintptr_t pool=0,block=0;
    if(!Read(ptr+0xB4,slot) || slot>4095 || !Read(ptr+0x62,bucket) || bucket>63 ||
       !Read(reinterpret_cast<std::uintptr_t>(audio.settingsPoolBase),pool) || pool<0x10000 ||
       !Read(reinterpret_cast<std::uintptr_t>(audio.settingsStride),stride) || stride<0x100 || stride>8192 ||
       !Read(pool+bucket*gtare::kSettingsBucketStride+gtare::kSettingsBlockOffset,block) || block<0x10000)return false;
    std::uint8_t readable[16];
    return probe::ReadLive(block+slot*static_cast<std::uintptr_t>(stride),readable,sizeof(readable));
}
bool Globals(std::uintptr_t image) {
    std::uintptr_t vtable=0,pool=0;std::uint16_t id=0xFFFF;
    std::uint8_t bucket=0xFF;std::uint32_t stride=0,index=99;
    const auto entity=reinterpret_cast<std::uintptr_t>(audio.frontendEntity);
    return Read(entity,vtable) && vtable>=image && vtable<image+95703552 &&
        Read(entity+gtare::kEntityAudioId,id) && id!=0xFFFF &&
        Read(reinterpret_cast<std::uintptr_t>(audio.initParamsBucket),bucket) && bucket<=63 &&
        Read(reinterpret_cast<std::uintptr_t>(audio.settingsPoolBase),pool) && pool>=0x10000 &&
        Read(reinterpret_cast<std::uintptr_t>(audio.settingsStride),stride) && stride>=0x100 && stride<=8192 &&
        Read(reinterpret_cast<std::uintptr_t>(audio.settingsWriteIndex),index) && index<4;
}

// Keep private ABI calls inside functions with no C++ unwinding objects.
bool Start(Trial* trial,void* ped,const float* position) {
    __try {
        alignas(16) std::uint8_t params[gtare::kParamsSize];
        trial->step=1;audio.initParamsCtor(params);
        params[gtare::kParamsBucket]=*audio.initParamsBucket;
        if(trial->positional) {
            std::memcpy(params,position,16);
            trial->step=2;trial->group=audio.envCreate("SK8VEnhancedProbe");
            if(!trial->group)return false;
            trial->step=3;audio.envInit(trial->group,nullptr,audio.envArgDistance,0,4000,audio.envArgScale,1000);
            trial->step=4;audio.envSetPosition(trial->group,position);
            trial->step=5;audio.envSetInteriorFromEntity(trial->group,ped);
            *reinterpret_cast<void**>(params+gtare::kParamsEnvGroup)=trial->group;
        }else *reinterpret_cast<std::int16_t*>(params+gtare::kParamsPan)=0;
        trial->step=6;audio.createSoundByName(audio.frontendEntity,"BINK_MONO_SOUND",
                                            const_cast<void**>(&trial->reference),params);
        auto* sound=trial->reference;void* settings=nullptr;
        if(!sound || !Settings(sound,settings) || !StreamSlot(sound))return false;
        trial->step=7;trial->stream=audio.initStreamPlayer(sound,trial->ring,1,Rate);
        if(!trial->stream)return false;
        if(trial->positional) {trial->step=8;audio.requestedSetPosition(settings,position);}
        trial->step=9;audio.prepareAndPlay(sound,nullptr,false,0,0);trial->played=true;
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER) {trial->fault=GetExceptionCode();return false;}
}
bool Stop(Trial* trial) {
    __try {
        auto* sound=trial->reference;void* settings=nullptr;
        // A game-dropped reference is already stopped. Do not call the private
        // destructor with a freed sound or the native invalid settings slot.
        if(!sound) {trial->stopped=true;return true;}
        if(!Settings(sound,settings))return false;
        trial->step=10;audio.stopAndForget(sound,false);
        trial->stopped=trial->reference==nullptr;return trial->stopped;
    }__except(EXCEPTION_EXECUTE_HANDLER) {trial->fault=GetExceptionCode();return false;}
}
void Reader(Trial& trial,unsigned& cursor,std::int32_t& available,LONG& refs) {
    EnterCriticalSection(&trial.ring->lock);
    cursor=trial.ring->readOffset;available=trial.ring->available;
    refs=InterlockedCompareExchange(&trial.ring->references,0,0);
    LeaveCriticalSection(&trial.ring->lock);
    trial.maxReferences=std::max(trial.maxReferences,refs);
}
void RunTrial(Trial& trial,int ped,void (*log)(const char*)) {
    trial.ring=gtaaudio::NewRing(RingBytes,RingBytes);if(!trial.ring)return;
    const auto p=gta::Call<Vector3>(gta::GET_ENTITY_COORDS,ped,true);
    alignas(16) const float pos[4]={p.x,p.y,p.z,0};
    auto* entity=getScriptHandleBaseAddress(ped);
    if(!entity || !Start(&trial,entity,pos)) {Stop(&trial);return;}
    log(trial.positional?"Enhanced audio probe: short positional test tone; player untouched":
                         "Enhanced audio probe: short listener test tone; player untouched");
    const auto start=GetTickCount64();unsigned previous=0;std::int32_t queued=0;LONG refs=1;
    Reader(trial,previous,queued,refs);
    float samples[4096];std::int16_t pcm[4096];double phase=0;
    while(GetTickCount64()-start<3000 && trial.reference &&
          gta::Call<int>(gta::PLAYER_PED_ID)==ped && !gta::FrontendActive()) {
        unsigned cursor=0;Reader(trial,cursor,queued,refs);
        if(cursor!=previous)++trial.readerChanges;previous=cursor;
        if(queued<0)++trial.underruns;
        const int count=std::min(4096,(9600-std::max(queued,0))/2);
        if(count>=64) {
            gtaaudio::ProbeTone(samples,count,phase,Rate,0.2f);
            gtaaudio::FloatToPcm16(samples,pcm,count,1,1);
            trial.bytesWritten+=gtaaudio::RingPush(trial.ring,pcm,count*2).written;
        }
        WAIT(0);
    }
    trial.elapsed=GetTickCount64()-start;
    gtaaudio::RingSilence(trial.ring);Stop(&trial);
    // Native stop releases asynchronously. Never free a native-reader ring.
    const auto stop=GetTickCount64();
    do {
        unsigned cursor=0;Reader(trial,cursor,queued,refs);trial.finalReferences=refs;
        trial.released=trial.stopped && refs==1;
        if(trial.released)break;WAIT(0);
    }while(GetTickCount64()-stop<2000);
}
bool Passed(const Trial& t) {
    return t.stream && t.played && t.elapsed>=2800 && t.readerChanges>10 && t.bytesWritten>Rate &&
           t.maxReferences>=2 && t.finalReferences==1 && t.stopped && t.released && !t.fault;
}
}
void Run(int ped,void (*log)(const char*)) {
    WAIT(5000);
    const auto image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const bool bindings=gtare::ResolveEnhancedImage(audio,reinterpret_cast<void*>(image),log);
    const bool globals=bindings && Globals(image);
    if(globals && gta::Call<int>(gta::PLAYER_PED_ID)==ped && !gta::FrontendActive()) {
        RunTrial(trials[0],ped,log);
        // Fail closed after any native ABI fault, stuck sound or ownership loss.
        if(Passed(trials[0])) {trials[1].positional=true;RunTrial(trials[1],ped,log);}
    }
    const bool verified=Passed(trials[0]) && Passed(trials[1]);
    const std::filesystem::path folder=L"port-evidence\\live-audio";
    std::filesystem::create_directories(folder);
    std::ofstream out(folder/L"audio-probe.json");
    out<<"{\"schema\":1,\"stage\":\"enhanced_native_audio\",\"version\":\"1.0.1158.16\",\"process_id\":"<<GetCurrentProcessId()
       <<",\"exact_bindings_verified\":"<<(bindings?"true":"false")<<",\"live_globals_verified\":"<<(globals?"true":"false")
       <<",\"runtime_started\":false,\"player_tasks_changed\":false,\"pose_memory_written\":false,"
         "\"legacy_private_hooks_entered\":false,\"mixer_flag_changed\":false,\"categories_enabled\":false,"
         "\"room_send_reset_enabled\":false,\"full_gameplay_port_ready\":false,\"audio_stream_verified\":"<<(verified?"true":"false")
       <<",\"trials\":[";
    for(unsigned i=0;i<2;++i) {const auto& t=trials[i];if(i)out<<',';
        out<<"{\"positional\":"<<(t.positional?"true":"false")<<",\"stream_initialized\":"<<(t.stream?"true":"false")
           <<",\"played\":"<<(t.played?"true":"false")<<",\"elapsed_ms\":"<<t.elapsed<<",\"native_reader_changes\":"<<t.readerChanges
           <<",\"pcm_bytes_written\":"<<t.bytesWritten<<",\"underrun_frames\":"<<t.underruns<<",\"max_references\":"<<t.maxReferences
           <<",\"final_references\":"<<t.finalReferences<<",\"sound_stopped\":"<<(t.stopped?"true":"false")
           <<",\"reader_released\":"<<(t.released?"true":"false")<<",\"fault_code\":"<<t.fault<<",\"last_step\":"<<t.step
           <<",\"passed\":"<<(Passed(t)?"true":"false")<<'}';
    }
    out<<"]}\n";
    log(verified?"Enhanced audio probe: listener, positional stream and native reader ownership VERIFIED":
                 "Enhanced audio probe: playback gate incomplete; inspect audio-probe.json; gameplay remains gated");
}
}
