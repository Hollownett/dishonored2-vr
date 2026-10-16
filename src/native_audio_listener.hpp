#pragma once
#include "audio_listener_pose.hpp"
#include "../third_party/minhook/include/MinHook.h"
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <mutex>
#include <stdexcept>

namespace d2vr {
inline constexpr std::uintptr_t audio_listener_set_rva=0xc1be0,audio_listener_call_rva=0x70ab5f;
inline constexpr std::uintptr_t audio_listener_component_vtable=0x1c4e808;
inline constexpr std::uintptr_t audio_listener_declaration_vtable=0x1c4e8e8,audio_main_pointer_rva=0x2286080;
inline constexpr std::array<unsigned char,32> audio_listener_set_code{
    0x8b,0x02,0x89,0x41,0x34,0x8b,0x42,0x04,0x89,0x41,0x38,0x8b,0x42,0x08,0x89,0x41,
    0x3c,0x41,0x8b,0x00,0x89,0x41,0x40,0x41,0x8b,0x40,0x04,0x89,0x41,0x44,0x41,0x8b};
struct AudioListenerOwner {
    std::uintptr_t player{},camera{},component{},listener{};
    rig::Identity camera_identity{},component_identity{};
    std::uintptr_t declaration{},main{},world{},components{};
    std::uint32_t listener_index{},component_count{},component_slot{};
};
template<class Read> bool current_audio_listener(Read& read,std::uintptr_t base,const AudioListenerOwner& owner) {
    using namespace rig;
    const auto game=value<std::uintptr_t>(read,base+0x39a5290);
    const auto component_id=started(read,owner.component),camera_id=started(read,owner.camera);
    const auto components=list(read,owner.player+0x268,4096);
    const auto flags=value<std::uint8_t>(read,owner.main);
    const auto acquired=value<std::uint32_t>(read,owner.world);
    const auto listener_flags=value<std::uint8_t>(read,owner.listener+0x88);
    if(!pointer(owner.main) || owner.main>0x00007fffffffffffULL-0x488 || owner.world!=owner.main+8 ||
       owner.listener_index>=8 || owner.listener!=owner.world+8+owner.listener_index*std::uintptr_t{0x90} ||
       value<std::uintptr_t>(read,base+audio_main_pointer_rva)!=owner.main || !flags || (*flags&0x24)!=0x24 ||
       !acquired || !(*acquired&(1u<<owner.listener_index)) || !listener_flags || !(*listener_flags&1) ||
       value<std::uint32_t>(read,owner.listener)!=owner.listener_index ||
       !components || components->data!=owner.components || components->packed_count!=owner.component_count ||
       owner.component_slot>=components->packed_count ||
       value<std::uintptr_t>(read,owner.components+owner.component_slot*8)!=owner.component ||
       !typed(read,base,owner.declaration,audio_listener_declaration_vtable) ||
       value<std::uint32_t>(read,owner.declaration+0x68)!=owner.listener_index) return false;
    if(!(game && typed(read,base,*game,0x1c940a8) && value<std::uintptr_t>(read,*game+0x3130b0)==owner.player &&
        typed(read,base,owner.player,0x1c5e428) &&
        typed(read,base,owner.camera,camera_vtable,camera_secondary_vtable) &&
        value<std::uintptr_t>(read,owner.camera+0x10)==owner.player && camera_id &&
        camera_id->generation==owner.camera_identity.generation &&
        typed(read,base,owner.component,audio_listener_component_vtable) &&
        value<std::uintptr_t>(read,owner.component+0x10)==owner.player && component_id &&
        component_id->generation==owner.component_identity.generation && pointer(owner.listener) &&
        value<std::uintptr_t>(read,owner.component+8)==owner.declaration &&
        value<std::uintptr_t>(read,owner.component+0x40)==owner.listener)) return false;
    // Re-read selected membership and acquisition anchors after the identity
    // checks. An unchanged list header alone does not prove its slot survived.
    const auto after=list(read,owner.player+0x268,4096);
    const auto component_after=started(read,owner.component),camera_after=started(read,owner.camera);
    const auto main_flags_after=value<std::uint8_t>(read,owner.main);
    const auto acquired_after=value<std::uint32_t>(read,owner.world);
    const auto listener_flags_after=value<std::uint8_t>(read,owner.listener+0x88);
    return after && after->data==owner.components && after->packed_count==owner.component_count &&
        value<std::uintptr_t>(read,owner.components+owner.component_slot*8)==owner.component &&
        value<std::uintptr_t>(read,base+audio_main_pointer_rva)==owner.main &&
        main_flags_after && (*main_flags_after&0x24)==0x24 && acquired_after && (*acquired_after&(1u<<owner.listener_index)) &&
        listener_flags_after && (*listener_flags_after&1) && value<std::uint32_t>(read,owner.listener)==owner.listener_index &&
        value<std::uintptr_t>(read,base+0x39a5290)==game && value<std::uintptr_t>(read,*game+0x3130b0)==owner.player &&
        component_after && component_after->generation==owner.component_identity.generation &&
        camera_after && camera_after->generation==owner.camera_identity.generation &&
        value<std::uintptr_t>(read,owner.component+0x10)==owner.player &&
        value<std::uintptr_t>(read,owner.camera+0x10)==owner.player &&
        value<std::uintptr_t>(read,owner.component+8)==owner.declaration &&
        typed(read,base,owner.declaration,audio_listener_declaration_vtable) &&
        value<std::uint32_t>(read,owner.declaration+0x68)==owner.listener_index &&
        value<std::uintptr_t>(read,owner.component+0x40)==owner.listener;
}
template<class Read> std::optional<AudioListenerOwner> find_audio_listener(Read& read,std::uintptr_t base,
    const NativePlayerCamera& camera) {
    using namespace rig;
    const auto components=list(read,camera.player+0x268,4096);
    if(!components) return {};
    AudioListenerOwner owner{};
    for(std::uint32_t i=0;i<components->packed_count;++i) {
        const auto at=value<std::uintptr_t>(read,components->data+i*8);
        if(!at || !typed(read,base,*at,audio_listener_component_vtable) ||
           value<std::uintptr_t>(read,*at+0x10)!=camera.player) continue;
        const auto identity=started(read,*at);const auto listener=value<std::uintptr_t>(read,*at+0x40);
        const auto declaration=value<std::uintptr_t>(read,*at+8),main=value<std::uintptr_t>(read,base+audio_main_pointer_rva);
        if(!identity || !listener || !pointer(*listener) || owner.component || !declaration || !main || !pointer(*main)) return {};
        const auto index=value<std::uint32_t>(read,*declaration+0x68);
        if(!index || *index>=8) return {};
        owner={camera.player,camera.camera,*at,*listener,camera.camera_identity,*identity};
        owner.declaration=*declaration;owner.main=*main;owner.world=*main+8;owner.listener_index=*index;
        owner.components=components->data;owner.component_count=components->packed_count;owner.component_slot=i;
    }
    const auto after=list(read,camera.player+0x268,4096);
    if(!owner.component || !after || after->data!=components->data || after->packed_count!=components->packed_count ||
       !current_audio_listener(read,base,owner)) return {};
    return owner;
}

// Replace only the current player's listener arguments at its native component
// update. The engine still owns interpolation, Wwise conversion, queueing and
// listener lifetime. Emitters and the caller's vectors are never modified.
class NativeAudioListener {
    friend struct NativeAudioListenerFixture;
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Logic=UINT (*)();
    using Set=void (__fastcall*)(void*,const float*,const float*,const float*);
    struct Publication {AudioListenerOwner owner{};AudioListenerPose pose{};std::uint64_t epoch{},tick{};};
    inline static NativeAudioListener* instance_{};
    Read read_{};Logic logic_{};Set original_{};std::uintptr_t base_{};
    bool enabled_{};
    std::atomic<bool> active_{};
    std::atomic<unsigned> depth_{};
    std::mutex mutex_;Publication publication_{};
    std::atomic<std::uint64_t> applied_{},fallback_{};
    bool plan(std::uintptr_t listener,std::uintptr_t caller,AudioListenerPose& pose) noexcept {
        try {
            if(!active_.load(std::memory_order_acquire) || caller!=base_+audio_listener_call_rva+5 || !logic_ || logic_()!=2) return false;
            Publication sample;{std::lock_guard<std::mutex> lock(mutex_);sample=publication_;}
            const auto now=GetTickCount64();
            if(!sample.epoch || !sample.tick || now<sample.tick || now-sample.tick>100 ||
               sample.owner.listener!=listener || !current_audio_listener(read_,base_,sample.owner)) return false;
            pose=sample.pose;return true;
        } catch(...) {return false;}
    }
    static void __fastcall hook(void* listener,const float* position,const float* top,const float* front) {
        auto& self=*instance_;self.depth_.fetch_add(1,std::memory_order_acq_rel);
        AudioListenerPose pose{};
        __try {
            if(self.plan(reinterpret_cast<std::uintptr_t>(listener),reinterpret_cast<std::uintptr_t>(_ReturnAddress()),pose)) {
                self.original_(listener,pose.position.data(),pose.top.data(),pose.front.data());++self.applied_;
            } else {self.original_(listener,position,top,front);++self.fallback_;}
        } __finally {self.depth_.fetch_sub(1,std::memory_order_acq_rel);}
    }
public:
    void clear() noexcept {std::lock_guard<std::mutex> lock(mutex_);publication_={};}
    void publish(const NativePlayerCamera& camera,const AudioListenerPose& pose,std::uint64_t epoch,std::uint64_t tick) {
        if(!active_.load(std::memory_order_acquire)) return;
        AudioListenerOwner owner;{std::lock_guard<std::mutex> lock(mutex_);owner=publication_.owner;}
        if(owner.player!=camera.player || owner.camera!=camera.camera ||
           owner.camera_identity.generation!=camera.camera_identity.generation || !current_audio_listener(read_,base_,owner)) {
            const auto found=find_audio_listener(read_,base_,camera);
            if(!found) {clear();return;}owner=*found;
        }
        std::lock_guard<std::mutex> lock(mutex_);publication_={owner,pose,epoch,tick};
    }
    void install(std::uintptr_t base,Read read,Logic logic) {
        if(!base || !read || !logic || enabled_ || depth_ || (instance_ && instance_!=this) || (original_ && base_!=base))
            throw std::runtime_error("Invalid native audio listener ownership");
        base_=base;read_=read;logic_=logic;
        std::array<unsigned char,32> code{};std::array<unsigned char,5> call{};std::int32_t displacement{};
        if(!read(base+audio_listener_set_rva,code.data(),code.size()) || code!=audio_listener_set_code ||
           !read(base+audio_listener_call_rva,call.data(),call.size()) || call[0]!=0xe8)
            throw std::runtime_error("Native audio listener code differs");
        std::memcpy(&displacement,call.data()+1,4);
        if(static_cast<std::intptr_t>(audio_listener_call_rva+5)+displacement!=static_cast<std::intptr_t>(audio_listener_set_rva))
            throw std::runtime_error("Native audio listener caller differs");
        auto* target=reinterpret_cast<void*>(base+audio_listener_set_rva);instance_=this;
        if(!original_) {
            void* trampoline{};
            if(MH_CreateHook(target,reinterpret_cast<void*>(&hook),&trampoline)!=MH_OK)
                throw std::runtime_error("Cannot create native audio listener hook");
            original_=reinterpret_cast<Set>(trampoline);
        }
        clear();applied_=0;fallback_=0;
        if(MH_EnableHook(target)!=MH_OK) throw std::runtime_error("Cannot enable native audio listener hook");
        enabled_=true;active_.store(true,std::memory_order_release);
    }
    bool disable() noexcept {
        active_.store(false,std::memory_order_release);clear();
        bool ok=true;
        if(enabled_) {
            const auto status=MH_DisableHook(reinterpret_cast<void*>(base_+audio_listener_set_rva));
            if(status==MH_OK || status==MH_ERROR_DISABLED) enabled_=false;else ok=false;
        }
        const auto deadline=GetTickCount64()+5000;
        while(depth_.load(std::memory_order_acquire) && GetTickCount64()<deadline) Sleep(1);
        if(depth_) return false;
        if(original_) {
            std::array<unsigned char,32> code{};
            if(!read_(base_+audio_listener_set_rva,code.data(),code.size()) || code!=audio_listener_set_code) ok=false;
        }
        return ok;
    }
    std::uint64_t applied() const noexcept {return applied_.load();}
    std::uint64_t fallback() const noexcept {return fallback_.load();}
    bool active() const noexcept {return active_.load();}
};
} // namespace d2vr
