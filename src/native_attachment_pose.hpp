#pragma once
#include <windows.h>
#include <MinHook.h>
#include "rig_transform.hpp"
#include "tracked_pose_lifetime.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <stdexcept>

namespace d2vr {
inline unsigned native_attachment_allowed_mask(unsigned tracked_hands,unsigned native_hands,
        bool gameplay,bool focused,bool owner_verified,bool carry_or_drop) noexcept {
    return gameplay && focused && owner_verified && !carry_or_drop?(tracked_hands&~native_hands&3u):0u;
}
// Weapons, powers and hand effects follow the tracked hand.
// 0x53d3f0 (7 args) and 0x53d6a0 (5 args) read ONE arm joint from the animation's
// transform array: *(anim+0x2d8) + remap[state][joint]*64, with the state remap
// table at *(anim+0x2e8) (16-byte entries {short* remap; u32 count}), then compose an
// attachment frame through 0x9b900 (docs: artifacts/re-toolchain/direct-ghidra/
// weapon-attachment-*). Skinning uses our palette (0x25efe0), but these readers
// used the untracked animation, so the sword floated away from the hand.
// While a fresh tracked palette exists for that animation, the joint's slot holds
// the tracked transform for the duration of the original call, then is restored.
struct NativeAttachmentSource {
    std::uint64_t tick{},revision{},epoch{},reference_epoch{},mapping_revision{};
    std::uintptr_t model{};rig::Identity animation_identity{};
    std::array<float,15> model_frame{};
    std::uint64_t source_tick{};
};
class NativeAttachmentPose {
    friend struct NativeAttachmentPoseCheck;
    using Query=std::uint64_t (__fastcall*)(std::uintptr_t,int,short,void*,void*,void*,void*);
    using Reader=std::uint64_t (__fastcall*)(std::uintptr_t,int,short,void*,void*);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    static constexpr std::uintptr_t query_rva=0x53d3f0,reader_rva=0x53d6a0;
    static constexpr std::array<unsigned char,16> query_code{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x81,0xec,0xc0,0x00,0x00,0x00,0x49,0x8b,0xf9};
    static constexpr std::array<unsigned char,16> reader_code{0x48,0x83,0xec,0x28,0x4c,0x8b,0xd9,0x66,0x45,0x85,0xc0,0x78,0x5f,0x48,0x8b,0x41};
    struct Side {std::uintptr_t animation{},first_slot{};std::array<rig::Transform,69> joints{};NativeAttachmentSource source{};};
    inline static NativeAttachmentPose* instance_{};
    Query query_{};Reader reader_{};Read read_{};std::uintptr_t base_{};
    bool enabled_{};std::atomic<bool> active_{};std::atomic<unsigned> depth_{};
    std::mutex mutex_;std::array<Side,2> sides_{};
    unsigned allowed_{};std::uint64_t policy_epoch_{},policy_tick_{},policy_revision_{},policy_reference_epoch_{},policy_mapping_revision_{};
    std::atomic<std::uint64_t> substitutions_{},calls_{};
    static constexpr std::uint64_t freshness_ms=tracked_pose_freshness_ms;
    bool source_retained(const NativeAttachmentSource& source,std::uint64_t now) const {
        return source.tick && source.tick<=policy_tick_ && source.revision && source.revision<=policy_revision_ &&
            source.reference_epoch && rig::pointer(source.model) && source.epoch==policy_epoch_ &&
            source.reference_epoch==policy_reference_epoch_ && source.mapping_revision==policy_mapping_revision_ &&
            now>=source.tick && now-source.tick<=freshness_ms &&
            (!source.source_tick || (now>=source.source_tick && now-source.source_tick<=freshness_ms));
    }
    bool source_allowed(const NativeAttachmentSource& source,std::uint64_t now) const {
        return source_retained(source,now) && source.tick==policy_tick_ && source.revision==policy_revision_;
    }
    bool frame_current(std::uintptr_t animation,const NativeAttachmentSource& source,bool exact) const {
        std::array<float,15> frame{};
        if(rig::value<std::uintptr_t>(read_,animation+0x38)!=source.model ||
           rig::started(read_,animation)!=source.animation_identity ||
           !read_(source.model+0x1860,frame.data(),sizeof(frame))) return false;
        try {(void)rig::ModelFrame(frame);} catch(...) {return false;}
        // Native 0x25e790 and 0x53d800 compose these SAME model-local joints
        // through the current parent frame. Movement does not replace a palette.
        return !exact || frame==source.model_frame;
    }
    static bool valid_cuff_affine(const rig::Transform& t) {
        for(float v:t.f) if(!std::isfinite(v) || std::abs(v)>1e7f) return false;
        const auto& a=t.f;
        const double det=a[0]*(double(a[5])*a[10]-double(a[6])*a[9])-a[4]*(double(a[1])*a[10]-double(a[2])*a[9])+a[8]*(double(a[1])*a[6]-double(a[2])*a[5]);
        if(det<=0) return false;
        double gram[3][3]{};
        for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j)
            for(unsigned k=0;k<3;++k) gram[i][j]+=double(a[i*4+k])*a[j*4+k];
        // All principal minors certify .6 <= singular values <= 2, including
        // rotated lateral scaling whose columns are not orthogonal.
        for(unsigned bound=0;bound<2;++bound) {
            double m[3][3]{};
            for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j)
                m[i][j]=bound?(i==j?4.:0.)-gram[i][j]:gram[i][j]-(i==j?.36:0.);
            for(unsigned i=0;i<3;++i) {
                if(m[i][i]<-1e-5) return false;
                for(unsigned j=i+1;j<3;++j) if(m[i][i]*m[j][j]-m[i][j]*m[j][i]<-1e-5) return false;
            }
            const double d=m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
            if(d<-1e-5) return false;
        }
        return true;
    }
    // Locate the slot for (anim,state,joint) exactly as the native readers do.
    bool slot(std::uintptr_t anim,int state,short joint,std::uintptr_t& at) const noexcept {
        std::uintptr_t states{},transforms{},remap{};std::uint32_t count{};std::int16_t index{};
        if(joint<0 || state<0 || state>3 || !read_(anim+0x2e8,&states,8) || !read_(anim+0x2d8,&transforms,8) ||
           !read_(states+static_cast<std::uintptr_t>(state)*16,&remap,8) || !read_(states+static_cast<std::uintptr_t>(state)*16+8,&count,4) ||
           !(count&0xffffff) || static_cast<std::uint32_t>(joint)>=(count&0xffffff) ||
           !read_(remap+static_cast<std::uintptr_t>(joint)*2,&index,2) || index<0) return false;
        at=transforms+static_cast<std::uintptr_t>(index)*64;return true;
    }
    bool palette_slots_current(std::uintptr_t animation,std::uintptr_t first_slot) const noexcept {
        std::uintptr_t states{},transforms{},remap{};std::uint32_t count{};std::array<std::int16_t,69> indices{};
        if(!read_(animation+0x2e8,&states,8) || !read_(animation+0x2d8,&transforms,8) ||
           !read_(states+48,&remap,8) || !read_(states+56,&count,4) || (count&0xffffff)!=69 ||
           !read_(remap,indices.data(),sizeof(indices)) || indices[0]<0 ||
           transforms+static_cast<std::uintptr_t>(indices[0])*64!=first_slot) return false;
        for(unsigned joint=0;joint<indices.size();++joint) if(indices[joint]!=indices[0]+static_cast<int>(joint)) return false;
        return true;
    }
    static bool copy_slot(std::uintptr_t at,const void* value) noexcept {
        __try {std::memcpy(reinterpret_cast<void*>(at),value,64);return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    static void restore_slot(std::uintptr_t at,const void* tracked,const void* saved) noexcept {
        __try {if(!std::memcmp(reinterpret_cast<void*>(at),tracked,64)) std::memcpy(reinterpret_cast<void*>(at),saved,64);} __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    template<class Call> std::uint64_t around(std::uintptr_t anim,int state,short joint,Call call) {
        ++calls_;
        if(!active_.load(std::memory_order_acquire) || state!=3 || joint<0 || joint>=69) return call();
        std::unique_lock<std::mutex> lock(mutex_);
        const auto now=GetTickCount64();
        if(!policy_epoch_ || now<policy_tick_ || now-policy_tick_>freshness_ms) {sides_={};lock.unlock();return call();}
        Side* side=nullptr;
        for(unsigned i=0;i<2;++i) {
            auto& s=sides_[i];
            if(s.animation && (!source_retained(s.source,now) || !frame_current(s.animation,s.source,false))) s={};
            if((allowed_&(1u<<i)) && s.animation && s.animation==anim) side=&s;
        }
        std::uintptr_t at{};
        if(!side) {lock.unlock();return call();}
        if(!slot(anim,state,joint,at) || at!=side->first_slot+static_cast<std::uintptr_t>(joint)*64) {
            *side={};lock.unlock();return call(); // Changed state-3 mapping revokes the retained palette.
        }
        const auto& tracked=side->joints[static_cast<unsigned>(joint)];
        std::array<unsigned char,64> saved{};
        if(!read_(at,saved.data(),64)) {lock.unlock();return call();}
        const bool written=copy_slot(at,tracked.f.data());
        if(written) ++substitutions_;
        // Restore only if the slot still holds our value; a concurrent native
        // animation write of a new frame must win.
        struct Restore {std::uintptr_t at;const rig::Transform* tracked;const std::array<unsigned char,64>* saved;bool written;
            ~Restore() {if(written) restore_slot(at,tracked->f.data(),saved->data());}
        } restore{at,&tracked,&saved,written};
        return call();
    }
    static std::uint64_t __fastcall query_hook(std::uintptr_t anim,int state,short joint,void* a,void* b,void* c,void* d) {
        auto& self=*instance_;++self.depth_;
        struct Depth {std::atomic<unsigned>& d;~Depth(){--d;}} guard{self.depth_};
        return self.around(anim,state,joint,[&]{return self.query_(anim,state,joint,a,b,c,d);});
    }
    static std::uint64_t __fastcall reader_hook(std::uintptr_t anim,int state,short joint,void* a,void* b) {
        auto& self=*instance_;++self.depth_;
        struct Depth {std::atomic<unsigned>& d;~Depth(){--d;}} guard{self.depth_};
        return self.around(anim,state,joint,[&]{return self.reader_(anim,state,joint,a,b);});
    }
public:
    // Publish native ownership BEFORE blend weights ramp. Revocation clears old
    // palettes and blocks subsequent worker publications in that same state.
    void set_allowed(unsigned mask,std::uint64_t epoch,std::uint64_t tick,
            std::uint64_t revision=0,std::uint64_t reference_epoch=0,std::uint64_t mapping_revision=0) {
        std::lock_guard<std::mutex> lock(mutex_);
        // A newer tracking sample does not replace the arm palette already consumed.
        if(epoch!=policy_epoch_ || revision<policy_revision_ || tick<policy_tick_ ||
           reference_epoch!=policy_reference_epoch_ || mapping_revision!=policy_mapping_revision_) sides_={};
        allowed_=epoch && tick && revision && reference_epoch?(mask&3u):0u;
        policy_epoch_=epoch;policy_tick_=tick;policy_revision_=revision;
        policy_reference_epoch_=reference_epoch;policy_mapping_revision_=mapping_revision;
        for(unsigned i=0;i<2;++i)if(!(allowed_&(1u<<i)))sides_[i]={};
    }
    // Animation-worker publication of the final tracked palette (model space,
    // same order as the contiguous pose-state-3 block starting at first_slot).
    bool publish(unsigned side,std::uintptr_t animation,std::uintptr_t first_slot,const std::array<rig::Transform,69>& joints,const NativeAttachmentSource& source) {
        if(side>1) return false;
        std::lock_guard<std::mutex> lock(mutex_);const auto now=GetTickCount64();
        sides_[side]={}; // Rejected/replaced uploads must never leave an older palette alive.
        if(!(allowed_&(1u<<side)) || !policy_epoch_ || now<policy_tick_ || now-policy_tick_>freshness_ms ||
           !source_allowed(source,now) || !animation || !first_slot || !frame_current(animation,source,true) ||
           !palette_slots_current(animation,first_slot)) return false;
        for(unsigned joint=0;joint<joints.size();++joint)
            if(joint<2?!valid_cuff_affine(joints[joint]):!rig::valid(joints[joint])) return false;
        sides_[side]={animation,first_slot,joints,source};
        return true;
    }
    void clear(unsigned side) {if(side>1) return;std::lock_guard<std::mutex> lock(mutex_);sides_[side]={};}
    void install(std::uintptr_t base,Read read) {
        base_=base;read_=read;
        std::array<unsigned char,16> code{};
        if(!read(base+query_rva,code.data(),16) || code!=query_code || !read(base+reader_rva,code.data(),16) || code!=reader_code)
            throw std::runtime_error("Native attachment readers differ from the supported executable");
        instance_=this;{std::lock_guard<std::mutex> lock(mutex_);sides_={};allowed_=0;policy_epoch_=policy_tick_=policy_revision_=policy_reference_epoch_=policy_mapping_revision_=0;}substitutions_=calls_=0;
        if(!query_ && MH_CreateHook(reinterpret_cast<void*>(base+query_rva),reinterpret_cast<void*>(&query_hook),reinterpret_cast<void**>(&query_))!=MH_OK)
            throw std::runtime_error("Cannot create attachment query hook");
        if(!reader_ && MH_CreateHook(reinterpret_cast<void*>(base+reader_rva),reinterpret_cast<void*>(&reader_hook),reinterpret_cast<void**>(&reader_))!=MH_OK)
            throw std::runtime_error("Cannot create attachment reader hook");
        active_=true;
        if(MH_EnableHook(reinterpret_cast<void*>(base+query_rva))!=MH_OK || MH_EnableHook(reinterpret_cast<void*>(base+reader_rva))!=MH_OK) {
            active_=false;MH_DisableHook(reinterpret_cast<void*>(base+query_rva));MH_DisableHook(reinterpret_cast<void*>(base+reader_rva));
            throw std::runtime_error("Cannot enable attachment hooks");
        }
        enabled_=true;
    }
    bool disable() noexcept {
        active_=false;
        if(enabled_) {
            if(MH_DisableHook(reinterpret_cast<void*>(base_+query_rva))!=MH_OK || MH_DisableHook(reinterpret_cast<void*>(base_+reader_rva))!=MH_OK) return false;
            enabled_=false;
        }
        const auto deadline=GetTickCount64()+5000;while(depth_ && GetTickCount64()<deadline) Sleep(1);
        if(depth_) return false;
        std::array<unsigned char,16> code{};
        return (!query_ || (read_(base_+query_rva,code.data(),16) && code==query_code)) &&
               (!reader_ || (read_(base_+reader_rva,code.data(),16) && code==reader_code));
    }
    std::uint64_t substitutions() const {return substitutions_.load();}
    std::uint64_t calls() const {return calls_.load();}
};
} // namespace d2vr
