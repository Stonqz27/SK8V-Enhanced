// Modified for Enhanced compatibility (2026), Stonqz27; see NOTICE.
#include "gta_audio_dsp.h"
#if defined(SK8V_ENHANCED)
#include "gta_audio_enhanced_re.h"
#include "enhanced_audio_bindings.h"
#endif
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)

using gtaaudio::GtaRingBuffer;
// Native consumer semantics, independently written from the decoded reader:
// +0x38 read cursor/+0x40 available in Enhanced; +0x10/+0x18 in Legacy.
std::vector<std::uint8_t> Read(GtaRingBuffer* r,unsigned bytes) {
    EnterCriticalSection(&r->lock);
    // +0xA6C051..0xA6C063 clamps a native read to the queued sample count.
    bytes=std::min(bytes,static_cast<unsigned>(std::max(r->available,0)));
    std::vector<std::uint8_t> out(bytes);
    for(unsigned i=0;i<bytes;++i)out[i]=r->data[(r->readOffset+i)%r->size];
    r->readOffset=(r->readOffset+bytes)%r->size;
    r->available-=static_cast<int>(bytes);
    LeaveCriticalSection(&r->lock);
    return out;
}
void ForceUnderrun(GtaRingBuffer* r,unsigned bytes) {
    // Inject a hostile cursor advance for the host's defensive recovery test;
    // unlike Read(), this is not a claim about normal Enhanced consumption.
    EnterCriticalSection(&r->lock);
    r->readOffset=(r->readOffset+bytes)%r->size;r->available-=static_cast<int>(bytes);
    LeaveCriticalSection(&r->lock);
}
void DestroyTestRing(GtaRingBuffer* r) {
    // Tests never attach to GTA; product allocations stay alive for the
    // session, since the asynchronous engine can retain a reader reference.
    DeleteCriticalSection(&r->lock); VirtualFree(r,0,MEM_RELEASE);
}
void RingTests() {
    CHECK(!gtaaudio::NewRing(63,0));
    auto* r=gtaaudio::NewStreamRing(512,32);CHECK(r);
    CHECK(gtaaudio::RingAvailable(r)==256 && r->references==1 && r->useLock==1);
#if defined(SK8V_ENHANCED)
    CHECK(r->segmentSize==r->size && r->readerCount==1 && r->segmentOffset==0);
    CHECK(std::all_of(std::begin(r->otherReaders),std::end(r->otherReaders),[](auto b){return b==0;}));
#endif
    CHECK(std::all_of(r->data,r->data+r->size,[](auto b){return b==0;}));
    std::int16_t pcm[256];for(int i=0;i<256;++i)pcm[i]=static_cast<std::int16_t>(i+100);
    auto pushed=gtaaudio::RingPush(r,pcm,512);CHECK(pushed.written==256 && !pushed.resynced);
    auto silence=Read(r,256);CHECK(std::all_of(silence.begin(),silence.end(),[](auto b){return b==0;}));
    auto samples=Read(r,256);CHECK(!std::memcmp(samples.data(),pcm,256));
    CHECK(gtaaudio::RingAvailable(r)==0);
    // Cross the buffer boundary; the native reader cannot replay stale data
    // after the queue becomes empty. Stress underrun recovery separately.
    pushed=gtaaudio::RingPush(r,pcm,400);CHECK(pushed.written==400);
    samples=Read(r,400);CHECK(!std::memcmp(samples.data(),pcm,400));
    CHECK(Read(r,128).empty() && gtaaudio::RingAvailable(r)==0);
    CHECK(std::all_of(r->data+400,r->data+r->size,[](auto b){return b==0;}));
    ForceUnderrun(r,128);
    CHECK(gtaaudio::RingAvailable(r)==-128);
    pushed=gtaaudio::RingPush(r,pcm,80);CHECK(pushed.written==80 && pushed.resynced==128);
    samples=Read(r,80);CHECK(!std::memcmp(samples.data(),pcm,80));
    // A stall exceeding a whole ring must resynchronize to the live reader.
    ForceUnderrun(r,1024);CHECK(gtaaudio::RingAvailable(r)==-1024);
    pushed=gtaaudio::RingPush(r,pcm,64);CHECK(pushed.written==64 && pushed.resynced==0);
    samples=Read(r,64);CHECK(!std::memcmp(samples.data(),pcm,64));
    gtaaudio::RingPush(r,pcm,128);const auto readOffset=r->readOffset;
    gtaaudio::RingSilence(r);CHECK(r->readOffset==readOffset && gtaaudio::RingAvailable(r)==128);
    CHECK(std::all_of(r->data,r->data+r->size,[](auto b){return b==0;}));
    CHECK(InterlockedIncrement(&r->references)==2 && InterlockedDecrement(&r->references)==1);
    DestroyTestRing(r);
    CHECK(gtaaudio::AudioShouldPause(2000,1000,false));
    CHECK(!gtaaudio::AudioShouldPause(1000,2000,false));
    CHECK(gtaaudio::AudioShouldPause(1000,1000,true));
}
#if defined(SK8V_ENHANCED)
void BindingsTests(const char* path) {
    std::ifstream f(path,std::ios::binary);CHECK(f);
    std::vector<std::uint8_t> image((std::istreambuf_iterator<char>(f)),{});
    CHECK(image.size()==95703552);
    gtare::Audio a;CHECK(gtare::ResolveEnhancedImage(a,image.data(),nullptr));
    CHECK(reinterpret_cast<std::uint8_t*>(a.frontendEntity)==image.data()+0x499DF20);
    CHECK(!a.categoriesOk && !a.envRoomLayoutOk);
    // Each required code/data guard must reject damage, clearing all pointers.
    unsigned rejected=0;
    for(const auto& g:gtare::enhanced::Guards) {
        image[g.rva]^=1;
        CHECK(!gtare::ResolveEnhancedImage(a,image.data(),nullptr));
        CHECK(!a.createSoundByName && !a.initStreamPlayer && !a.frontendEntity);
        image[g.rva]^=1;++rejected;
    }
    CHECK(gtare::ResolveEnhancedImage(a,image.data(),nullptr));
    alignas(16) std::uint8_t params[0xC0];std::memset(params,0xCD,sizeof(params));
    a.initParamsCtor(params);
    auto expected=std::vector<std::uint8_t>(std::begin(gtare::enhanced::ParamDefaults),std::end(gtare::enhanced::ParamDefaults));
    expected[0xA6]=0xFF;CHECK(!std::memcmp(params,expected.data(),sizeof(params)));
    CHECK(*reinterpret_cast<std::int16_t*>(params+gtare::kParamsPan)==-1);
    CHECK(*reinterpret_cast<float*>(params+0x40)==-1.0f && *reinterpret_cast<float*>(params+0x48)==1.0f);
    // Synthetic sound pools prove the new bucket stride and unchanged slots.
    std::vector<std::uint8_t> pool(3*gtare::kSettingsBucketStride),block(4*0x200),sound(0xC0);
    auto poolAddress=reinterpret_cast<std::uintptr_t>(pool.data());std::uint32_t stride=0x200;
    a.settingsPoolBase=&poolAddress;a.settingsStride=&stride;
    *reinterpret_cast<std::uintptr_t*>(pool.data()+2*gtare::kSettingsBucketStride+gtare::kSettingsBlockOffset)=reinterpret_cast<std::uintptr_t>(block.data());
    sound[0x62]=2;sound[0x80]=3;
    CHECK(gtare::RequestedSettings(a,sound.data())==block.data()+3*stride);
    sound[0x80]=0xFF;CHECK(!gtare::RequestedSettings(a,sound.data()));
    CHECK(!gtare::MixerWaitTimedOutFlag(nullptr));
    std::printf("Enhanced native audio: %u corrupted guards rejected; constructor, pool and disabled Legacy fallback passed\n",rejected);
}
#endif
int main(int argc,char** argv) {
    RingTests();
#if defined(SK8V_ENHANCED)
    CHECK(argc==1 || argc==2);
    if(argc==2) BindingsTests(argv[1]);
    else std::puts("Retail-image binding audit skipped: provide your own local mapped image to enable it");
#endif
    std::puts("Audio ring: prefill, wrap, underrun recovery, silence, ownership and pause checks passed");
}
