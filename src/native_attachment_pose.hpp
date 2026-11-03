#pragma once
#include <windows.h>
#include <MinHook.h>
#include "native_power_hand.hpp"
#include "rig_transform.hpp"
#include "tracked_pose_lifetime.hpp"
#include "frame_order_trace.hpp"
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace d2vr {
// native_hands: the arms the player state owns (body_ik native_ownership). A carried body owns only the right arm, so the
// left keeps its tracked palette (was revoked for every carry: the left arm played the carry clip, headset 2026-10-14).
inline unsigned native_attachment_allowed_mask(unsigned tracked_hands,unsigned native_hands,
        bool gameplay,bool focused,bool owner_verified) noexcept {
    return gameplay && focused && owner_verified?(tracked_hands&~native_hands&3u):0u;
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
struct NativeAttachmentAdmission {
    void* context{};
    std::uint64_t receipt{};
    bool (*acquire)(void*,std::uint64_t) noexcept{};
    void (*release)(void*,std::uint64_t) noexcept{};
};
static_assert(std::is_standard_layout_v<NativeAttachmentAdmission> && std::is_trivially_copyable_v<NativeAttachmentAdmission>);
enum class NativeAttachmentReceiptFramePolicy {exact,current_parent};
struct NativeAttachmentSource {
    std::uint64_t tick{},revision{},epoch{},reference_epoch{},mapping_revision{};
    std::uintptr_t model{};rig::Identity animation_identity{};
    std::array<float,15> model_frame{};
    std::uint64_t source_tick{};
    NativeAttachmentAdmission admission{};
    std::uint64_t diagnostic_socket_token{};
    unsigned diagnostic_side{};
    void (*observe_socket)(const NativeAttachmentSource&,std::uintptr_t,short,unsigned,bool,bool,const rig::Transform*) noexcept{};
    NativeAttachmentReceiptFramePolicy receipt_frame_policy{NativeAttachmentReceiptFramePolicy::exact};
};
class NativeAttachmentPose {
    friend struct NativeAttachmentPoseCheck;
    using Query=std::uint64_t (__fastcall*)(std::uintptr_t,int,short,void*,void*,void*,void*);
    using Reader=std::uint64_t (__fastcall*)(std::uintptr_t,int,short,void*,void*);
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    static constexpr std::uintptr_t query_rva=0x53d3f0,reader_rva=0x53d6a0;
    static constexpr std::array<unsigned char,16> query_code{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x81,0xec,0xc0,0x00,0x00,0x00,0x49,0x8b,0xf9};
    static constexpr std::array<unsigned char,16> reader_code{0x48,0x83,0xec,0x28,0x4c,0x8b,0xd9,0x66,0x45,0x85,0xc0,0x78,0x5f,0x48,0x8b,0x41};
    struct Side {std::uintptr_t animation{},first_slot{};std::array<rig::Transform,69> joints{};NativeAttachmentSource source{};std::uint64_t interval{};};
    inline static NativeAttachmentPose* instance_{};
    Query query_{};Reader reader_{};Read read_{};std::uintptr_t base_{};
    bool enabled_{};std::atomic<bool> active_{};std::atomic<unsigned> depth_{};
    std::mutex mutex_;std::array<Side,2> sides_{};
    unsigned allowed_{};std::uint64_t policy_epoch_{},policy_tick_{},policy_revision_{},policy_reference_epoch_{},policy_mapping_revision_{};
    std::atomic<std::uint64_t> substitutions_{},calls_{};
    // Dual wield, power hand (native_power_hand.hpp): bit 0 set = reads of the LEFT arm (side 0) in state 3 return the RIGHT hand's tracked
    // joint (side 1), re-expressed in the left model, so everything bound to the left arm rides the right hand; bit 1 = the same the other
    // way round (the native sword on the right arm rides the left hand). Zero (the default): not read.
    std::atomic<unsigned> cross_{};std::atomic<std::uint64_t> cross_substitutions_{},cross_skipped_{};
    // The joint-59 correction per DESTINATION arm (guarded by mutex_): [0] the left arm's slot-1 item riding the right hand (mirror_correction of that item's
    // left bind tag: y180 for guns, grenades, mines and powers, z180 for the Heart), [1] the right arm's sword riding the left hand (its own right tag: y180).
    std::array<power_hand::Rot3,2> cross_attachment_{power_hand::y180_rot,power_hand::y180_rot};
    // v776: crossed reads whose live arm model frames differed by more than a millimetre (the origin arm not yet updated for this game frame), and the largest gap.
    mutable std::atomic<std::uint64_t> cross_skewed_{},cross_skew_peak_um_{};
    std::atomic<bool> cross_live_{}; // test switch: convert with the models' live frames (the pre-v776 behaviour) instead of the published ones
    std::atomic<std::uint64_t> rebases_{}; // substitutions that followed a moved contiguous state-3 block (status arm_flicker.publish.rebases)
    static constexpr std::uint64_t freshness_ms=tracked_pose_freshness_ms;
public:
    static constexpr std::uint64_t publish_grace_ms=100;
private:
    std::atomic<std::uint64_t> clears_{},grace_accepts_{},rejects_policy_{},rejects_source_{},rejects_other_{};
    // v833 diagnostic: the Present interval (note_interval) each palette was published in, and per substitution whether the weapon reader ran in that
    // same interval (after the arm upload), the next one (before this interval's upload: the item a frame behind the hand) or later.
    std::atomic<std::uint64_t> interval_{},reads_order_[3]{},advanced_{};
    // v835: a read before this interval's upload gets its joint moved on by the hand's motion this tick (the probe's advance_attachment)
    using Advance=std::optional<rig::Transform>(*)(unsigned,const NativeAttachmentSource&,const rig::Transform&,short) noexcept;   // v861: + the joint index
    std::atomic<Advance> advance_{};
    static bool admission_valid(const NativeAttachmentAdmission& value) noexcept {
        return (!value.context && !value.receipt && !value.acquire && !value.release) ||
            (value.context && value.receipt && value.acquire && value.release);
    }
    static bool same_source(const NativeAttachmentSource& first,const NativeAttachmentSource& second) noexcept {
        return first.tick==second.tick && first.revision==second.revision && first.epoch==second.epoch &&
            first.reference_epoch==second.reference_epoch && first.mapping_revision==second.mapping_revision &&
            first.model==second.model && first.animation_identity==second.animation_identity && first.model_frame==second.model_frame &&
            first.source_tick==second.source_tick && first.admission.context==second.admission.context &&
            first.admission.receipt==second.admission.receipt && first.admission.acquire==second.admission.acquire && first.admission.release==second.admission.release &&
            first.receipt_frame_policy==second.receipt_frame_policy;
    }
    static bool receipt_policy_valid(const NativeAttachmentSource& source) noexcept {
        return source.receipt_frame_policy==NativeAttachmentReceiptFramePolicy::exact ||
            (source.receipt_frame_policy==NativeAttachmentReceiptFramePolicy::current_parent && source.admission.receipt && admission_valid(source.admission));
    }
    static bool receipt_frame_exact(const NativeAttachmentSource& source) noexcept {
        return source.admission.receipt && source.receipt_frame_policy==NativeAttachmentReceiptFramePolicy::exact;
    }
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
    // The solve a worker publishes can be one tracking sample behind the policy: the head-view thread published the next sample
    // between the solve and this publication. A strictly older tick is the common production case (the arm upload revision is
    // shared by both hands and the head-view thread advances only the tick), so the tick alone decides; an equal tick with an older
    // revision is a replayed sample and is still refused. v966 (headset arm_flicker: grace_accepts 0, rejects_policy 6326,
    // fallbacks ~3200 with reads_previous 65067).
    bool source_in_grace(const NativeAttachmentSource& source,std::uint64_t now) const {
        return source_retained(source,now) && source.tick<policy_tick_ &&
            now-source.tick<=publish_grace_ms && (!source.source_tick || now-source.source_tick<=publish_grace_ms);
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
    __declspec(noinline) static std::uint64_t invoke_cleanup(std::uint64_t(*invoke)(void*),void* frame,void(*cleanup)(void*) noexcept,bool* returned) {
        std::uint64_t result{};
        __try {result=invoke(frame);*returned=true;}
        __finally {cleanup(frame);}
        return result;
    }
    template<class Call> struct CallFrame {
        NativeAttachmentPose* self;
        std::uintptr_t animation;
        int state;
        short joint;
        std::decay_t<Call> call;
        std::uintptr_t at{};
        rig::Transform tracked{};
        std::array<unsigned char,64> saved{};
        NativeAttachmentSource admitted_source{};
        NativeAttachmentSource observed_source{};
        unsigned observed_stage{};
        bool returned{};
        bool locked{},written{},admitted{},crossed{};
        CallFrame(NativeAttachmentPose* owner,std::uintptr_t anim,int pose_state,short index,Call&& action)
            noexcept(std::is_nothrow_constructible_v<std::decay_t<Call>,Call&&>):
            self(owner),animation(anim),state(pose_state),joint(index),call(std::forward<Call>(action)) {}
        void unlock() noexcept {if(locked) {locked=false;self->mutex_.unlock();}}
    };
    // The right hand's palette joint for a left-arm read (mutex_ held). No source: the right arm is not allowed or published, either side
    // carries a receipt (the socket and contact admissions are one-arm protocols), the joint is a cuff joint, or a published model frame is invalid.
    std::optional<rig::Transform> cross_joint(const Side& destination,const Side& origin,short joint) const noexcept {
        if(joint<2 || (allowed_&3u)!=3u || !origin.animation || !origin.first_slot || origin.animation==destination.animation ||
           destination.source.admission.context || destination.source.admission.receipt || origin.source.admission.context || origin.source.admission.receipt) return {};
        std::array<float,15> from{},to{};
        const bool live=read_(origin.source.model+0x1860,from.data(),sizeof(from)) && read_(destination.source.model+0x1860,to.data(),sizeof(to));
        if(live) {   // Diagnostics (status dual_wield.cross): the arms' LIVE frames. Both hang on the same spine joint, so they agree unless one arm has not been updated yet
            const double gap=std::hypot(std::hypot(double(from[0])-to[0],double(from[1])-to[1]),double(from[2])-to[2]);
            if(gap>.001) ++cross_skewed_;
            const auto um=static_cast<std::uint64_t>(std::min(gap,1e3)*1e6);
            auto peak=cross_skew_peak_um_.load();while(um>peak && !cross_skew_peak_um_.compare_exchange_weak(peak,um)) {}
        }
        try {
            // v776 (headset v775: a crossed item trailed the hand when the player ran): the frames the two palettes were PUBLISHED with, not the models' live
            // ones. This read comes from the post-physics attach pass (0x755030 -> 0x753c10), which writes each arm's frame (model+0x1860) and then
            // evaluates the items bound to it: the item of the destination arm runs right after that arm's update, but the origin arm is its sibling and
            // may be updated later, so its live frame is still the previous game frame's while the origin arm is DRAWN in this one: the item rode a pose one
            // frame of the player's motion behind the hand (v*dt: 10 cm at 6 m/s and 60 fps). Both arms hang on center_spine03_jnt with identity tags and were
            // uploaded in the same animation stage, so their published frames agree, the conversion is the identity and the item takes the destination's
            // live frame exactly as that arm's own palette does: the frame the origin arm is drawn in. (Equal frames: nothing changes.)
            // cross_live_ (test command "crossframes live") brings the pre-v776 conversion back for an A/B in the sim.
            const bool use_live=live && cross_live_.load(std::memory_order_relaxed);
            const rig::ModelFrame source(use_live?from:origin.source.model_frame),target(use_live?to:destination.source.model_frame);
            // The correction is the item's mirror (power_hand::mirror_correction): y180 for a weapon tag. It is the exact mirror only
            // while the holding hand plays the other arm's clip mirrored (v763 FullBodyDriver mirror_native; the receipts show the left item
            // frame equals joint 59): with the player's fist instead the item sat wrong (headsets v761-v762).
            const auto* correction=joint==static_cast<short>(power_hand::attachment_joint)?&cross_attachment_[static_cast<unsigned>(&destination-sides_.data())]:nullptr;
            return power_hand::cross_joint(origin.joints[static_cast<unsigned>(joint)],source,target,destination.joints[static_cast<unsigned>(joint)],correction);
        } catch(...) {return {};}
    }
    template<class Frame> std::uint64_t around_frame(Frame& frame) {
        const auto anim=frame.animation;const auto state=frame.state;const auto joint=frame.joint;
        ++calls_;
        if(!active_.load(std::memory_order_acquire) || state!=3 || joint<0 || joint>=69) return frame.call();
        mutex_.lock();frame.locked=true;
        const auto now=GetTickCount64();
        if(!policy_epoch_ || now<policy_tick_ || now-policy_tick_>freshness_ms) {sides_={};frame.unlock();return frame.call();}
        Side* side=nullptr;
        for(unsigned i=0;i<2;++i) {
            auto& s=sides_[i];
            if(s.animation && (!source_retained(s.source,now) || !frame_current(s.animation,s.source,receipt_frame_exact(s.source)))) s={};
            if((allowed_&(1u<<i)) && s.animation && s.animation==anim) side=&s;
        }
        if(!side) {frame.unlock();return frame.call();}
        const auto read_age=interval_.load(std::memory_order_relaxed)-side->interval;++reads_order_[read_age==0?0:read_age==1?1:2];
        frame.observed_source=side->source;frame.observed_stage=1;
        if(side->source.admission.receipt) {
            const auto side_index=static_cast<std::size_t>(side-sides_.data());frame.admitted_source=side->source;
            frame.unlock();
            const auto& admission=frame.admitted_source.admission;
            if(!admission.acquire(admission.context,admission.receipt)) {frame.observed_stage=2;return frame.call();}
            frame.admitted=true;mutex_.lock();frame.locked=true;side=&sides_[side_index];
            if(!active_.load(std::memory_order_acquire) || !(allowed_&(1u<<side_index)) || side->animation!=anim ||
               !same_source(side->source,frame.admitted_source) || !source_retained(side->source,GetTickCount64()) || !frame_current(anim,side->source,receipt_frame_exact(side->source))) {
                frame.observed_stage=3;frame.unlock();return frame.call();
            }
        }
        if(!slot(anim,state,joint,frame.at)) {
            frame.observed_stage=4;*side={};frame.unlock();return frame.call(); // Changed state-3 mapping revokes the retained palette.
        }
        if(const auto offset=static_cast<std::uintptr_t>(joint)*64;frame.at!=side->first_slot+offset) {
            // v613: the animation moved its whole state-3 block between our upload and this read (sim 20261025T164524: once per equip,
            // ~10 frames after a catch and ~1 s into a draw). The palette is model-space per joint, so it still holds for the moved block
            // when that block is again the contiguous 69-joint map. Revoking here let this read and every later one in the frame take the
            // native joint: the sword drawn out of the hand for a frame after a catch (headset v612). A non-contiguous map still revokes.
            if(frame.at<offset || !palette_slots_current(anim,frame.at-offset)) {
                frame.observed_stage=4;*side={};frame.unlock();return frame.call();
            }
            side->first_slot=frame.at-offset;++rebases_;
        }
        frame.tracked=side->joints[static_cast<unsigned>(joint)];
        if(joint==59) frame_order::note('R',static_cast<std::uint8_t>(side-sides_.data()),{static_cast<double>(read_age)});
        if(read_age==1) if(const auto advance=advance_.load(std::memory_order_acquire))
            if(const auto moved=advance(static_cast<unsigned>(side-sides_.data()),side->source,frame.tracked,joint)) {frame.tracked=*moved;++advanced_;}
        if(const auto mask=cross_.load(std::memory_order_relaxed)) {
            const auto index=static_cast<unsigned>(side-sides_.data());
            if((mask>>index)&1u) {
                if(const auto crossed=cross_joint(*side,sides_[index^1u],joint)) {frame.tracked=*crossed;frame.crossed=true;}
                else ++cross_skipped_;
            }
        }
        if(!read_(frame.at,frame.saved.data(),64)) {frame.observed_stage=5;frame.unlock();return frame.call();}
        frame.written=copy_slot(frame.at,frame.tracked.f.data());
        frame.observed_stage=frame.written?6u:7u;
        if(frame.written) {++substitutions_;if(frame.crossed) ++cross_substitutions_;}
        // Restore only if the slot still holds our value; a concurrent native
        // animation write of a new frame must win.
        return frame.call();
    }
    template<class Call> std::uint64_t around(std::uintptr_t anim,int state,short joint,Call&& call) {
        using Frame=CallFrame<Call>;
        static_assert(std::is_nothrow_constructible_v<std::decay_t<Call>,Call&&>);
        alignas(Frame) unsigned char storage[sizeof(Frame)];
        auto* frame=::new(static_cast<void*>(storage)) Frame(this,anim,state,joint,std::forward<Call>(call));
        const auto invoke=[](void* raw)->std::uint64_t {
            auto& value=*static_cast<Frame*>(raw);return value.self->around_frame(value);
        };
        const auto cleanup=[](void* raw) noexcept {
            auto* value=static_cast<Frame*>(raw);
            if(value->written)restore_slot(value->at,value->tracked.f.data(),value->saved.data());
            value->unlock();
            if(value->admitted) {
                const auto& admission=value->admitted_source.admission;
                admission.release(admission.context,admission.receipt);
            }
            if(value->observed_source.diagnostic_socket_token && value->observed_source.observe_socket)
                value->observed_source.observe_socket(value->observed_source,value->animation,value->joint,value->observed_stage,
                    value->written,value->returned,value->written?&value->tracked:nullptr);
            value->~Frame();
        };
        return invoke_cleanup(invoke,frame,cleanup,&frame->returned);
    }
    static std::uint64_t __fastcall query_hook(std::uintptr_t anim,int state,short joint,void* a,void* b,void* c,void* d) {
        auto& self=*instance_;++self.depth_;
        const auto original=self.query_;
        __try {return self.around(anim,state,joint,[=]{return original(anim,state,joint,a,b,c,d);});}
        __finally {--self.depth_;}
    }
    static std::uint64_t __fastcall reader_hook(std::uintptr_t anim,int state,short joint,void* a,void* b) {
        auto& self=*instance_;++self.depth_;
        const auto original=self.reader_;
        __try {return self.around(anim,state,joint,[=]{return original(anim,state,joint,a,b);});}
        __finally {--self.depth_;}
    }
    bool validate_palette(std::uintptr_t animation,std::uintptr_t first_slot,const std::array<rig::Transform,69>& joints,
            const NativeAttachmentSource& source,std::uint64_t now,bool& exact) {
        if(!policy_epoch_ || now<policy_tick_ || now-policy_tick_>freshness_ms ||
           !admission_valid(source.admission) || !receipt_policy_valid(source)) {++rejects_policy_;return false;}
        exact=source_allowed(source,now);
        if(!exact && !source_in_grace(source,now)) {++rejects_source_;return false;}
        if(!animation || !first_slot || !frame_current(animation,source,exact) ||
           !palette_slots_current(animation,first_slot)) {++rejects_other_;return false;}
        for(unsigned joint=0;joint<joints.size();++joint)
            if(joint<2?!valid_cuff_affine(joints[joint]):!rig::valid(joints[joint])) {++rejects_other_;return false;}
        return true;
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
        if(!(allowed_&(1u<<side))) {++rejects_policy_;return false;}
        bool exact{};if(!validate_palette(animation,first_slot,joints,source,now,exact))return false;
        sides_[side]={animation,first_slot,joints,source,interval_.load(std::memory_order_relaxed)};
        if(!exact)++grace_accepts_;
        return true;
    }
    // Reading-note skinning may move the hands without granting any native item reader that palette.
    bool validate_visual(unsigned side,std::uintptr_t animation,std::uintptr_t first_slot,const std::array<rig::Transform,69>& joints,const NativeAttachmentSource& source) {
        if(side>1)return false;
        std::lock_guard<std::mutex> lock(mutex_);sides_[side]={};
        if((allowed_&(1u<<side)) || source.admission.context || source.admission.receipt || source.admission.acquire || source.admission.release ||
           source.diagnostic_socket_token || source.observe_socket || source.receipt_frame_policy!=NativeAttachmentReceiptFramePolicy::exact) {
            ++rejects_policy_;return false;
        }
        bool exact{};return validate_palette(animation,first_slot,joints,source,GetTickCount64(),exact);
    }
    void clear(unsigned side) {if(side>1) return;std::lock_guard<std::mutex> lock(mutex_);if(sides_[side].animation)++clears_;sides_[side]={};}
    // Palettes removed by clear() (a live palette only), accepted one sample behind the policy, and refused publications by cause
    // (policy/age/admission, source not current, frame/slot/joint checks): status arm_flicker.
    struct Counters {std::uint64_t clears{},grace_accepts{},rejects_policy{},rejects_source{},rejects_other{},reads_same{},reads_previous{},reads_older{},advanced{};};
    Counters counters() const {return {clears_.load(),grace_accepts_.load(),rejects_policy_.load(),rejects_source_.load(),rejects_other_.load(),reads_order_[0].load(),reads_order_[1].load(),reads_order_[2].load(),advanced_.load()};}
    void set_advance(Advance advance) noexcept {advance_.store(advance,std::memory_order_release);}
    void note_interval(std::uint64_t present) noexcept {interval_.store(present,std::memory_order_relaxed);}   // render thread, at each Present
    // Dual wield, power hand. Any thread, any time (the lead sets it every control tick from the same flag that mirrors the hands). Set it only
    // while slot 1 holds something without a mod copy (a power, grenade, mine, razor): a gun or crossbow is drawn as a copy measured from the
    // native weapon at the left hand, which this would move. Off (the default) changes nothing: the extra work is one relaxed load.
    // set_cross(true) = left-arm things ride the right hand; set_cross_mask(2) = the native sword on the right arm rides the left hand (the
    // sword copy's offset is measured from it, so use it only if that measurement is taken against the left hand).
    void set_cross(bool on) noexcept {cross_.store(on?1u:0u,std::memory_order_relaxed);}
    void set_cross_mask(unsigned mask) noexcept {cross_.store(mask&3u,std::memory_order_relaxed);}
    unsigned cross_mask() const noexcept {return cross_.load(std::memory_order_relaxed);}
    bool cross() const noexcept {return (cross_.load(std::memory_order_relaxed)&1u)!=0;}
    // The local rotation applied to a crossed joint 59 for reads of the `side` arm (default y180). side 0: the slot-1 item riding the right hand, the
    // item's mirror_correction (power_hand.hpp) of its left bind tag: the lead sets it each control tick from the equipped item (v776: the Heart's tag
    // differs from the weapon tag, and y180 turned it upside down). side 1: the sword riding the left hand (its own right tag: y180, never changes).
    bool set_cross_attachment(const power_hand::Rot3& rotation,unsigned side=0) {
        if(side>1 || !power_hand::valid_rot(rotation)) return false;
        std::lock_guard<std::mutex> lock(mutex_);cross_attachment_[side]=rotation;return true;
    }
    // Reads that returned the other hand, and cross reads that fell back to the arm's own palette (status arm_flicker / power_hand).
    std::uint64_t cross_substitutions() const {return cross_substitutions_.load();}
    std::uint64_t cross_skipped() const {return cross_skipped_.load();}
    void set_cross_live_frames(bool on) noexcept {cross_live_.store(on,std::memory_order_relaxed);}
    // Crossed reads whose live arm frames differed by more than 1 mm (the origin arm not yet updated for this game frame: the lag v776 removes), and the
    // largest live gap in millimetres. Zero while standing; about v*dt while running if the origin arm is evaluated after the item's arm.
    std::uint64_t cross_skewed() const {return cross_skewed_.load();}
    double cross_skew_peak_mm() const {return static_cast<double>(cross_skew_peak_um_.load())*1e-3;}
    // The joint axis the `side` correction half-turns about (0 X, 1 Y, 2 Z, 3 none): status dual_wield.cross[4..5]. A weapon: 1 (y180); the Heart on the right hand: 2 (z180).
    unsigned cross_correction_axis(unsigned side=0) {
        if(side>1) return 3;
        std::lock_guard<std::mutex> lock(mutex_);const auto& c=cross_attachment_[side];
        for(unsigned axis=0;axis<3;++axis) if(c[axis*4]>.99f) return axis;
        return 3;
    }
    void install(std::uintptr_t base,Read read) {
        base_=base;read_=read;
        std::array<unsigned char,16> code{};
        if(!read(base+query_rva,code.data(),16) || code!=query_code || !read(base+reader_rva,code.data(),16) || code!=reader_code)
            throw std::runtime_error("Native attachment readers differ from the supported executable");
        instance_=this;{std::lock_guard<std::mutex> lock(mutex_);sides_={};allowed_=0;policy_epoch_=policy_tick_=policy_revision_=policy_reference_epoch_=policy_mapping_revision_=0;}substitutions_=calls_=cross_substitutions_=cross_skipped_=cross_skewed_=cross_skew_peak_um_=0;clears_=grace_accepts_=rejects_policy_=rejects_source_=rejects_other_=0;
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
    std::uint64_t rebases() const {return rebases_.load();}
};
} // namespace d2vr
