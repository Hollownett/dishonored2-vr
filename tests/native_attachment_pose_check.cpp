#define NOMINMAX
#include "../src/native_attachment_pose.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <memory>
#include <cstdlib>
std::atomic<std::uint64_t> fixture_allocations{};
void* operator new(std::size_t size) {
    if(auto* memory=std::malloc(size?size:1)) {fixture_allocations.fetch_add(1,std::memory_order_relaxed);return memory;}
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {return ::operator new(size);}
void operator delete(void* memory) noexcept {std::free(memory);}
void operator delete[](void* memory) noexcept {std::free(memory);}
void operator delete(void* memory,std::size_t) noexcept {std::free(memory);}
void operator delete[](void* memory,std::size_t) noexcept {std::free(memory);}
namespace {
alignas(16) std::array<std::byte,0x300> fixture_animation{};
alignas(16) std::array<std::byte,64> fixture_states{};
alignas(16) std::array<std::byte,0x1900> fixture_model{};
alignas(16) std::array<d2vr::rig::Transform,69> fixture_native{};
alignas(16) std::array<d2vr::rig::Transform,69> fixture_moved{}; // v613: the same state-3 block after the animation moved it
std::array<short,69> fixture_remap{};
bool read(std::uintptr_t address,void* out,std::size_t size){SIZE_T n{};return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,size,&n) && n==size;}
template<class T>void put(std::byte* address,const T& value){std::memcpy(address,&value,sizeof(value));}
constexpr DWORD fixture_exception=0xe0421826u;
bool faulting_read(std::uintptr_t,void*,std::size_t) {RaiseException(fixture_exception,0,0,nullptr);return false;}
bool catch_fixture_seh(void(*call)(void*),void* context) {
    __try {call(context);}
    __except(GetExceptionCode()==fixture_exception?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return true;}
    return false;
}
struct CallOwnership {
    unsigned* destroyed;
    explicit CallOwnership(unsigned* value):destroyed(value) {}
    ~CallOwnership(){++*destroyed;}
};
}
namespace d2vr {
struct NativeAttachmentPoseCheck {
    inline static unsigned native_calls{},hook_mode{};
    struct AdmissionContext {
        NativeAttachmentPose* pose{};std::uint64_t receipt{41};bool valid{true};unsigned acquires{},releases{},active{};
        HANDLE entered{},resume{};
    };
    inline static AdmissionContext* expected_admission{};
    inline static unsigned socket_observations{},socket_stage{};
    inline static bool socket_substituted{},socket_returned{};
    static void observe_socket(const NativeAttachmentSource& source,std::uintptr_t animation,short joint,unsigned stage,
            bool substituted,bool returned,const rig::Transform* local) noexcept {
        assert(expected_admission && !expected_admission->active);
        assert(expected_admission->pose->mutex_.try_lock());expected_admission->pose->mutex_.unlock();
        assert(source.diagnostic_socket_token==41 && source.diagnostic_side==1 && joint==59);
        assert(animation==reinterpret_cast<std::uintptr_t>(fixture_animation.data()));
        assert(source.epoch && source.reference_epoch && source.mapping_revision && source.revision);
        assert(substituted==(local!=nullptr));if(local)assert(local->f[12]==.4f);
        ++socket_observations;socket_stage=stage;socket_substituted=substituted;socket_returned=returned;
    }
    inline static bool expect_admitted{};
    inline static float expected_joint=.4f;
    static bool acquire(void* raw,std::uint64_t receipt) noexcept {
        auto& context=*static_cast<AdmissionContext*>(raw);++context.acquires;
        assert(context.pose->mutex_.try_lock());context.pose->mutex_.unlock();
        if(!context.valid || receipt!=context.receipt)return false;
        ++context.active;
        if(context.entered){assert(SetEvent(context.entered));assert(WaitForSingleObject(context.resume,5000)==WAIT_OBJECT_0);}
        return true;
    }
    static void release(void* raw,std::uint64_t receipt) noexcept {
        auto& context=*static_cast<AdmissionContext*>(raw);assert(receipt==context.receipt && context.active==1);
        assert(fixture_native[59].f[12]==(hook_mode==3?.7f:0.f));
        assert(context.pose->mutex_.try_lock());context.pose->mutex_.unlock();
        --context.active;++context.releases;
    }
    static void check_admission(){if(expected_admission)assert(expected_admission->active==(expect_admitted?1u:0u));}
    static std::uint64_t __fastcall query_original(std::uintptr_t anim,int state,short joint,void* a,void* b,void* c,void* d) {
        ++native_calls;assert(anim==reinterpret_cast<std::uintptr_t>(fixture_animation.data()) && state==3 && joint==59);
        assert(a==reinterpret_cast<void*>(1) && b==reinterpret_cast<void*>(2) && c==reinterpret_cast<void*>(3) && d==reinterpret_cast<void*>(4));
        assert(fixture_native[59].f[12]==expected_joint);check_admission();
        if(hook_mode==3)fixture_native[59].f[12]=.7f;
        if(hook_mode==1 || hook_mode==3)RaiseException(fixture_exception,0,0,nullptr);
        if(hook_mode==2)throw std::runtime_error("query failure");
        return 71;
    }
    static std::uint64_t __fastcall reader_original(std::uintptr_t anim,int state,short joint,void* a,void* b) {
        ++native_calls;assert(anim==reinterpret_cast<std::uintptr_t>(fixture_animation.data()) && state==3 && joint==59);
        assert(a==reinterpret_cast<void*>(1) && b==reinterpret_cast<void*>(2));
        assert(fixture_native[59].f[12]==expected_joint);check_admission();
        if(hook_mode==1)RaiseException(fixture_exception,0,0,nullptr);
        if(hook_mode==2)throw std::runtime_error("reader failure");
        return 73;
    }
    static void invoke_query(void*) {
        assert(NativeAttachmentPose::query_hook(reinterpret_cast<std::uintptr_t>(fixture_animation.data()),3,59,
            reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),reinterpret_cast<void*>(4))==71);
    }
    static void invoke_reader(void*) {
        assert(NativeAttachmentPose::reader_hook(reinterpret_cast<std::uintptr_t>(fixture_animation.data()),3,59,
            reinterpret_cast<void*>(1),reinterpret_cast<void*>(2))==73);
    }
    struct ConcurrentCall {void(*invoke)(void*);};
    static DWORD WINAPI invoke_concurrent(void* raw){static_cast<ConcurrentCall*>(raw)->invoke(nullptr);return 0;}
    struct OwnedContext {NativeAttachmentPose* pose;unsigned* destroyed;bool bypass;};
    static void invoke_owned(void* raw) {
        auto& context=*static_cast<OwnedContext*>(raw);
        auto ownership=std::make_shared<CallOwnership>(context.destroyed);
        context.pose->around(reinterpret_cast<std::uintptr_t>(fixture_animation.data()),context.bypass?0:3,59,
            [ownership=std::move(ownership)]()->std::uint64_t {RaiseException(fixture_exception,0,0,nullptr);return 0;});
    }
    static void run(){
        NativeAttachmentPose pose;pose.read_=read;pose.active_=true;
        const auto animation=reinterpret_cast<std::uintptr_t>(fixture_animation.data());
        const auto states=reinterpret_cast<std::uintptr_t>(fixture_states.data()),native=reinterpret_cast<std::uintptr_t>(fixture_native.data()),remap=reinterpret_cast<std::uintptr_t>(fixture_remap.data());
        put(fixture_animation.data()+0x2d8,native);put(fixture_animation.data()+0x2e8,states);
        put(fixture_states.data()+48,remap);put(fixture_states.data()+56,std::uint32_t{69});
        for(unsigned i=0;i<69;++i)fixture_remap[i]=static_cast<short>(i);
        const auto model=reinterpret_cast<std::uintptr_t>(fixture_model.data());
        const rig::Identity identity{1,10};
        put(fixture_animation.data()+0x18,identity);put(fixture_animation.data()+0x38,model);
        for(auto& joint:fixture_native) joint.f[0]=joint.f[5]=joint.f[10]=joint.f[15]=1;
        auto tracked=fixture_native;tracked[59].f[12]=.4f;unsigned originals{};
        const std::array<float,15> model_frame{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
        put(fixture_model.data()+0x1860,model_frame);
        NativeAttachmentSource source{GetTickCount64()-2,7,1,2,3,model,identity,model_frame};
        const auto policy=[&](unsigned mask=3u) {pose.set_allowed(mask,source.epoch,source.tick,source.revision,source.reference_epoch,source.mapping_revision);};
        const auto publish=[&] {return pose.publish(1,animation,native,tracked,source);};
        const auto call=[&](bool changed){return pose.around(animation,3,59,[&]{++originals;assert(fixture_native[59].f[12]==(changed?.4f:0.f));return UINT64{17};});};
        // Execution requests native item ownership: partial solver blends must
        // fail attachment admission, so the caller retains its original arm.
        // After the state ends, the exact return blend is admitted for both.
        for(const float native_weight:{.25f,.75f,1.f}) {
            auto blended=tracked;blended[59].f[12]=.4f*(1-native_weight);
            policy(native_attachment_allowed_mask(3,2,true,true,true));
            assert(!pose.publish(1,animation,native,blended,source));
            assert(pose.around(animation,3,59,[&]{assert(fixture_native[59].f[12]==0);return UINT64{17};})==17);
        }
        for(const float native_weight:{.75f,.25f,0.f}) {
            auto blended=tracked;blended[59].f[12]=.4f*(1-native_weight);
            policy(native_attachment_allowed_mask(3,0,true,true,true));
            assert(pose.publish(1,animation,native,blended,source));
            assert(pose.around(animation,3,59,[&]{assert(fixture_native[59].f[12]==blended[59].f[12]);return UINT64{17};})==17);
            assert(fixture_native[59].f[12]==0); // Original authored slot restored.
        }
        {   // A note may skin tracked hands while weapon readers keep the native animation.
            const auto visual=[&](const NativeAttachmentSource& candidate) {return pose.validate_visual(1,animation,native,tracked,candidate);};
            assert(!visual(source));assert(call(false)==17); // An enabled weapon policy is never a visual bypass; old palette cleared.
            policy(0);assert(!publish());assert(visual(source));
            assert(!pose.sides_[1].animation && call(false)==17);
            policy();assert(call(false)==17); // Re-enabling weapons cannot resurrect a visual-only palette.
            policy(0);
            auto behind=source;behind.tick=source.tick-20;--behind.revision;
            assert(visual(behind) && call(false)==17); // Same bounded one-sample publication grace.
            for(unsigned invalid=0;invalid<6;++invalid) {
                auto bad=source;
                if(invalid==0)bad.tick=GetTickCount64()-251;
                if(invalid==1)++bad.epoch;
                if(invalid==2)++bad.reference_epoch;
                if(invalid==3)++bad.mapping_revision;
                if(invalid==4)--bad.revision;
                if(invalid==5)bad.source_tick=GetTickCount64()-251;
                assert(!visual(bad) && !pose.sides_[1].animation && call(false)==17);
            }
            auto bad=source;bad.admission.context=&pose;assert(!visual(bad));
            bad=source;bad.admission.receipt=41;assert(!visual(bad));
            bad=source;bad.admission.acquire=&acquire;assert(!visual(bad));
            bad=source;bad.admission.release=&release;assert(!visual(bad));
            AdmissionContext visual_admission{&pose};bad=source;
            bad.admission={&visual_admission,41,&acquire,&release};assert(!visual(bad) && !visual_admission.acquires);
            bad=source;bad.diagnostic_socket_token=41;assert(!visual(bad));
            bad=source;bad.observe_socket=&observe_socket;assert(!visual(bad));
            bad=source;bad.receipt_frame_policy=NativeAttachmentReceiptFramePolicy::current_parent;assert(!visual(bad));
            auto broken=tracked;broken[2].f[0]=1.4f;
            assert(!pose.validate_visual(1,animation,native,broken,source));
            put(fixture_animation.data()+0x18,rig::Identity{2,10});assert(!visual(source));
            put(fixture_animation.data()+0x18,identity);
            fixture_remap[59]=60;assert(!visual(source));fixture_remap[59]=59;
            auto moved=model_frame;moved[0]=1;put(fixture_model.data()+0x1860,moved);assert(!visual(source));
            put(fixture_model.data()+0x1860,model_frame);
            assert(visual(source) && call(false)==17);
            pose.set_allowed(0,0,0);assert(!visual(source));
        }
        pose.set_allowed(0,0,0); // Preserve existing initial-policy rejection case.
        assert(!publish());assert(call(false)==17); // no policy yet
        policy();assert(publish());assert(call(true)==17 && fixture_native[59].f[12]==0);
        policy(native_attachment_allowed_mask(3,3,true,true,true));publish();assert(call(false)==17); // DropCorpse: both arms native
        policy();assert(call(false)==17); // revoked palette does not revive
        publish();assert(call(true)==17);
        ++source.epoch;policy();assert(call(false)==17);publish();assert(call(true)==17);
        ++source.reference_epoch;policy();assert(call(false)==17);publish();assert(call(true)==17);
        // Query-before-upload keeps the last accepted rendered source as tracking advances.
        const auto rendered=source;++source.revision;++source.tick;
        policy();assert(call(true)==17);
        assert(pose.sides_[1].source.revision==rendered.revision);
        assert(publish());assert(call(true)==17);
        assert(pose.sides_[1].source.revision==source.revision);
        --source.revision;policy();assert(call(false)==17); // Backward source sequence revokes.
        assert(publish());
        ++source.mapping_revision;policy();assert(call(false)==17);publish();assert(call(true)==17);
        // An accepted slow frame uses the original tracking tick, not the callback tick.
        source.tick=GetTickCount64()-200;policy();publish();assert(call(true)==17);
        assert(pose.sides_[1].source.tick==source.tick);
        source.tick=GetTickCount64()-251;policy();publish();assert(call(false)==17 && !pose.sides_[1].animation);
        source.tick=GetTickCount64();policy();publish();assert(call(true)==17);
        // single_missed_sample_keeps_tracked_palette and weapon_stays_visible_through_one_missed_frame: the head-view thread moved the policy on one sample before this publication.
        {
            const auto base=source;auto behind=source;behind.tick=GetTickCount64()-20;behind.revision=base.revision-1;
            const auto before=pose.counters();
            source.tick=GetTickCount64()-5;source.revision=base.revision+1;policy();   // policy is now one sample ahead of `behind`
            assert(pose.publish(1,animation,native,tracked,behind));assert(call(true)==17);
            assert(pose.counters().grace_accepts==before.grace_accepts+1);
            // fallback_after_grace_window: a pose older than the grace window is refused (and the stale palette dropped)
            auto old=behind;old.tick=GetTickCount64()-(NativeAttachmentPose::publish_grace_ms+20);
            assert(!pose.publish(1,animation,native,tracked,old));assert(call(false)==17 && !pose.sides_[1].animation);
            assert(pose.counters().rejects_source>before.rejects_source);
            // epoch_change_still_falls_back_immediately: another epoch, reference or mapping never rides the grace
            for(unsigned which=0;which<3;++which) {
                auto changed=behind;if(which==0)++changed.epoch;else if(which==1)++changed.reference_epoch;else ++changed.mapping_revision;
                assert(!pose.publish(1,animation,native,tracked,changed));assert(call(false)==17);
            }
            // a backward revision at the same tick (the replayed sample) is still refused
            auto replay=source;--replay.revision;assert(!pose.publish(1,animation,native,tracked,replay));
            source=base;policy();publish();assert(call(true)==17);
        }
        auto rejected=source;--rejected.revision;
        assert(!pose.publish(1,animation,native,tracked,rejected));assert(call(false)==17 && !pose.sides_[1].animation);
        publish();assert(call(true)==17);
        // Refreshing virtual-world publication cannot freshen an old XR pose.
        source.tick=GetTickCount64();source.source_tick=source.tick-251;policy();
        assert(!publish());assert(call(false)==17);
        source.source_tick=source.tick-200;assert(publish());assert(call(true)==17);
        source.source_tick=0; // Legacy fixtures derive their age from publication tick.
        // Slow moving-model query-before-upload retains exact model-local joints.
        source.tick=GetTickCount64()-200;policy();assert(publish());
        auto moved=model_frame;moved[0]=1;moved[1]=-.5f;
        moved[3]=0;moved[4]=1;moved[6]=-1;moved[7]=0;
        moved[12]=moved[13]=moved[14]=1.2f;
        put(fixture_model.data()+0x1860,moved);assert(call(true)==17);
        assert(pose.sides_[1].source.model_frame==model_frame);
        assert(pose.sides_[1].joints[59].f==tracked[59].f); // No inferred world rebase.
        assert(!publish());assert(call(false)==17); // New upload requires exact admission frame.
        source.model_frame=moved;assert(publish());assert(call(true)==17);
        moved[3]=2;put(fixture_model.data()+0x1860,moved);assert(call(false)==17); // Malformed current frame revokes.
        moved[3]=0;put(fixture_model.data()+0x1860,moved);assert(call(false)==17);
        assert(publish());assert(call(true)==17);
        put(fixture_animation.data()+0x18,rig::Identity{2,10});assert(call(false)==17);
        put(fixture_animation.data()+0x18,identity);publish();
        // Other native pose states may alias these slots, but state-3 publication
        // must never substitute them or revoke an otherwise current state-3 pose.
        put(fixture_states.data(),remap);put(fixture_states.data()+8,std::uint32_t{69});
        assert(pose.around(animation,0,59,[&]{++originals;assert(fixture_native[59].f[12]==0);return UINT64{23};})==23);
        assert(call(true)==17);
        tracked[60].f[12]=.8f;assert(publish());fixture_remap[59]=60;
        assert(pose.around(animation,3,59,[&]{++originals;assert(fixture_native[60].f[12]==0);return UINT64{29};})==29);
        assert(!pose.sides_[1].animation && !publish()); // Same animation generation/allocation, changed map.
        fixture_remap[59]=59;assert(call(false)==17);assert(publish());assert(call(true)==17);
        {   // v613 (headset: the sword out of the hand for a frame after a catch; the pistol on holster -> draw): the animation moves its whole
            // contiguous state-3 block about 10 frames into an equip clip. The model-space palette still holds per joint: the substitution
            // follows the moved block (left arm and right arm alike) instead of revoking and letting the reader take the native joint.
            const auto moved_block=reinterpret_cast<std::uintptr_t>(fixture_moved.data());
            for(auto& joint:fixture_moved) joint=fixture_native[0];
            for(const unsigned arm:{0u,1u}) {
                put(fixture_animation.data()+0x2d8,native);pose.clear(1-arm); // the fixture's arms share one animation: one side at a time
                assert(pose.publish(arm,animation,native,tracked,source));
                const auto rebased=pose.rebases();
                put(fixture_animation.data()+0x2d8,moved_block);
                for(int frame=0;frame<2;++frame) // the first call follows the move, later calls stay on it
                    assert(pose.around(animation,3,59,[&]{assert(fixture_moved[59].f[12]==.4f && fixture_native[59].f[12]==0);return UINT64{31};})==31);
                assert(fixture_moved[59].f[12]==0 && pose.rebases()==rebased+1 && pose.sides_[arm].animation); // restored, palette kept
                // A moved block that is not the contiguous 69-joint map still revokes.
                fixture_remap[59]=60;
                assert(pose.around(animation,3,59,[&]{assert(fixture_moved[60].f[12]==0 && fixture_moved[59].f[12]==0);return UINT64{37};})==37);
                assert(!pose.sides_[arm].animation);
                fixture_remap[59]=59;put(fixture_animation.data()+0x2d8,native);
            }
            assert(publish());assert(call(true)==17);
        }
        // Only cuff/origin joints admit intentional bounded positive affine fit.
        const auto ordinary=tracked;
        for(const float width:{.6f,1.4f,2.f}) {
            tracked=ordinary;
            const float q=static_cast<float>(std::sqrt(.5));
            for(unsigned joint=0;joint<2;++joint) {
                tracked[joint].f[0]=q;tracked[joint].f[1]=q*width;
                tracked[joint].f[4]=-q;tracked[joint].f[5]=q*width;
            }
            assert(publish());assert(call(true)==17);
        }
        for(const float invalid:{0.f,-1.f,.59f,2.1f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
            tracked=ordinary;tracked[0].f[0]=invalid;
            assert(!publish());assert(call(false)==17);
        }
        tracked=ordinary;tracked[2].f[0]=1.4f;assert(!publish()); // Arm/wrist/finger transforms remain rigid.
        tracked=ordinary;assert(publish());
        // Concurrent native authored writes retain ownership on restoration.
        assert(pose.around(animation,3,59,[&]{fixture_native[59].f[12]=.7f;return UINT64{19};})==19);
        assert(fixture_native[59].f[12]==.7f);fixture_native[59].f[12]=0;
        source.tick=GetTickCount64();policy();assert(publish());
        pose.query_=&query_original;pose.reader_=&reader_original;NativeAttachmentPose::instance_=&pose;
        hook_mode=0;
        const auto allocations_before=fixture_allocations.load(std::memory_order_relaxed);
        const auto calls_before=native_calls;
        for(unsigned iteration=0;iteration<256;++iteration) {invoke_query(nullptr);invoke_reader(nullptr);}
        assert(native_calls==calls_before+512 && fixture_allocations.load(std::memory_order_relaxed)==allocations_before);
        assert(pose.depth_==0 && fixture_native[59].f[12]==0);
        for(const auto invoke:{&invoke_query,&invoke_reader}) {
            for(const auto mode:{1u,2u,0u}) {
                hook_mode=mode;const auto before=native_calls;
                if(mode==1)assert(catch_fixture_seh(invoke,nullptr));
                else if(mode==2) {
                    bool caught=false;try {invoke(nullptr);}catch(const std::runtime_error&){caught=true;}assert(caught);
                } else invoke(nullptr);
                assert(native_calls==before+1 && pose.depth_==0 && fixture_native[59].f[12]==0);
                assert(pose.mutex_.try_lock());pose.mutex_.unlock();
                assert(publish());
            }
        }
        hook_mode=3;const auto before=native_calls;
        assert(catch_fixture_seh(&invoke_query,nullptr));
        assert(native_calls==before+1 && pose.depth_==0 && fixture_native[59].f[12]==.7f);
        assert(pose.mutex_.try_lock());pose.mutex_.unlock();fixture_native[59].f[12]=0;
        unsigned destroyed{};
        for(const bool bypass:{false,true}) {
            OwnedContext context{&pose,&destroyed,bypass};const auto previous=destroyed;
            assert(catch_fixture_seh(&invoke_owned,&context));assert(destroyed==previous+1);
            assert(fixture_native[59].f[12]==0);assert(pose.mutex_.try_lock());pose.mutex_.unlock();
        }
        {
            OwnedContext context{&pose,&destroyed,false};const auto previous=destroyed;
            pose.read_=&faulting_read;
            assert(catch_fixture_seh(&invoke_owned,&context));assert(destroyed==previous+1);
            assert(fixture_native[59].f[12]==0);assert(pose.mutex_.try_lock());pose.mutex_.unlock();
            pose.read_=&read;
        }
        {
            auto ownership=std::make_shared<CallOwnership>(&destroyed);const auto previous=destroyed;
            bool caught=false;
            try {pose.around(animation,3,59,[ownership=std::move(ownership)]()->std::uint64_t {throw std::runtime_error("owned callback");});}
            catch(const std::runtime_error&){caught=true;}
            assert(caught && destroyed==previous+1 && fixture_native[59].f[12]==0);
            assert(pose.mutex_.try_lock());pose.mutex_.unlock();
        }
        AdmissionContext admission{&pose};expected_admission=&admission;
        source.admission={&admission,admission.receipt,&acquire,&release};
        source.diagnostic_socket_token=41;source.diagnostic_side=1;source.observe_socket=&observe_socket;
        hook_mode=0;
        const auto contact_policy=[&]{source.tick=GetTickCount64();policy();};
        for(const auto invoke:{&invoke_query,&invoke_reader}) {
            contact_policy();assert(publish());expected_joint=.4f;expect_admitted=true;
            const auto first_acquires=admission.acquires,first_releases=admission.releases,first_native=native_calls;
            const auto before_allocations=fixture_allocations.load(std::memory_order_relaxed);
            invoke(nullptr);
            assert(native_calls==first_native+1 && admission.acquires==first_acquires+1 && admission.releases==first_releases+1 && !admission.active);
            assert(socket_observations && socket_stage==6 && socket_substituted && socket_returned);
            assert(fixture_allocations.load(std::memory_order_relaxed)==before_allocations);
            admission.valid=false;expected_joint=0;expect_admitted=false;
            const auto rejected_acquires=admission.acquires,rejected_releases=admission.releases,rejected_native=native_calls;
            invoke(nullptr);
            assert(native_calls==rejected_native+1 && admission.acquires==rejected_acquires+1 && admission.releases==rejected_releases && !admission.active);
            assert(socket_stage==2 && !socket_substituted && socket_returned);
            admission.valid=true;auto stale=source;stale.admission.receipt=admission.receipt+1;assert(pose.publish(1,animation,native,tracked,stale));
            invoke(nullptr);assert(!admission.active && admission.releases==rejected_releases);
            contact_policy();assert(publish());
            auto contact_moved=source.model_frame;contact_moved[0]+=.25f;put(fixture_model.data()+0x1860,contact_moved);
            const auto before_move=admission.acquires;invoke(nullptr);assert(admission.acquires==before_move && !pose.sides_[1].animation);
            put(fixture_model.data()+0x1860,source.model_frame);invoke(nullptr);assert(admission.acquires==before_move);
            for(unsigned partial=1;partial<15;++partial) {
                auto malformed=source;malformed.admission={};
                if(partial&1)malformed.admission.context=&admission;
                if(partial&2)malformed.admission.receipt=admission.receipt;
                if(partial&4)malformed.admission.acquire=&acquire;
                if(partial&8)malformed.admission.release=&release;
                assert(!pose.publish(1,animation,native,tracked,malformed));
                const auto before_partial=native_calls;invoke(nullptr);assert(native_calls==before_partial+1 && !admission.active);
            }
            expected_joint=.4f;expect_admitted=true;
            for(const auto mode:{1u,2u,0u}) {
                contact_policy();assert(publish());hook_mode=mode;
                const auto before_native=native_calls,before_acquire=admission.acquires,before_release=admission.releases;
                if(mode==1)assert(catch_fixture_seh(invoke,nullptr));
                else if(mode==2){bool caught=false;try{invoke(nullptr);}catch(const std::runtime_error&){caught=true;}assert(caught);}
                else invoke(nullptr);
                assert(native_calls==before_native+1 && admission.acquires==before_acquire+1 && admission.releases==before_release+1 && !admission.active);
                assert(socket_stage==6 && socket_substituted && socket_returned==(mode==0));
                assert(fixture_native[59].f[12]==0 && pose.depth_==0);
            }
            for(const bool change_model:{false,true}) {
                contact_policy();assert(publish());expected_joint=0;expect_admitted=true;
                admission.entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);admission.resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
                assert(admission.entered && admission.resume);ConcurrentCall concurrent{invoke};
                const auto before_native=native_calls,before_acquire=admission.acquires,before_release=admission.releases;
                const auto thread=CreateThread(nullptr,0,&invoke_concurrent,&concurrent,0,nullptr);assert(thread);
                assert(WaitForSingleObject(admission.entered,5000)==WAIT_OBJECT_0);
                if(change_model){auto concurrent_model=source.model_frame;concurrent_model[0]+=.5f;put(fixture_model.data()+0x1860,concurrent_model);}
                else pose.clear(1);
                assert(SetEvent(admission.resume));assert(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);
                assert(native_calls==before_native+1 && admission.acquires==before_acquire+1 && admission.releases==before_release+1 && !admission.active);
                assert(socket_stage==3 && !socket_substituted && socket_returned);
                CloseHandle(thread);CloseHandle(admission.entered);CloseHandle(admission.resume);admission.entered=admission.resume=nullptr;
                put(fixture_model.data()+0x1860,source.model_frame);
            }
            expected_joint=.4f;
        }
        source.receipt_frame_policy=NativeAttachmentReceiptFramePolicy::current_parent;
        for(const auto invoke:{&invoke_query,&invoke_reader}) {
            contact_policy();assert(publish());
            const std::array<float,15> moved_parent{10,20,30,0,1,0,-1,0,0,0,0,1,1,1,1};
            put(fixture_model.data()+0x1860,moved_parent);
            expected_joint=.4f;expect_admitted=true;
            const auto before_motion_native=native_calls,before_motion_acquire=admission.acquires,before_motion_release=admission.releases;
            invoke(nullptr);
            assert(native_calls==before_motion_native+1 && admission.acquires==before_motion_acquire+1 && admission.releases==before_motion_release+1);
            assert(fixture_native[59].f[12]==0 && !admission.active && pose.sides_[1].animation==animation);
            assert(!publish());
            put(fixture_model.data()+0x1860,source.model_frame);
            auto malformed=source;malformed.receipt_frame_policy=static_cast<NativeAttachmentReceiptFramePolicy>(99);
            assert(!pose.publish(1,animation,native,tracked,malformed));
            for(unsigned partial=0;partial<15;++partial) {
                malformed=source;malformed.admission={};
                if(partial&1)malformed.admission.context=&admission;
                if(partial&2)malformed.admission.receipt=admission.receipt;
                if(partial&4)malformed.admission.acquire=&acquire;
                if(partial&8)malformed.admission.release=&release;
                assert(!pose.publish(1,animation,native,tracked,malformed));
            }
            for(unsigned rejection=0;rejection<6;++rejection) {
                contact_policy();assert(publish());expected_joint=0;expect_admitted=rejection==2;
                if(rejection==0)put(fixture_animation.data()+0x38,std::uintptr_t{});
                if(rejection==1)put(fixture_animation.data()+0x18,rig::Identity{identity.generation+1,identity.flags});
                if(rejection==2)fixture_remap[59]=58;
                if(rejection==3)pose.set_allowed(3,source.epoch+1,source.tick,source.revision,source.reference_epoch,source.mapping_revision);
                if(rejection==4)pose.sides_[1].source.tick=GetTickCount64()-1000;
                if(rejection==5)admission.valid=false;
                const auto before_reject_native=native_calls;invoke(nullptr);
                assert(native_calls==before_reject_native+1 && !admission.active && fixture_native[59].f[12]==0);
                put(fixture_animation.data()+0x38,model);put(fixture_animation.data()+0x18,identity);
                fixture_remap[59]=59;admission.valid=true;
            }
            for(unsigned changed=0;changed<3;++changed) {
                contact_policy();assert(publish());expected_joint=changed==0?.4f:0.f;expect_admitted=true;
                admission.entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);admission.resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);
                assert(admission.entered && admission.resume);ConcurrentCall concurrent{invoke};
                const auto before_race_native=native_calls,before_race_acquire=admission.acquires,before_race_release=admission.releases;
                const auto thread=CreateThread(nullptr,0,&invoke_concurrent,&concurrent,0,nullptr);assert(thread);
                assert(WaitForSingleObject(admission.entered,5000)==WAIT_OBJECT_0);
                if(changed==0)put(fixture_model.data()+0x1860,moved_parent);
                if(changed==1)put(fixture_animation.data()+0x38,std::uintptr_t{});
                if(changed==2){std::lock_guard<std::mutex> lock(pose.mutex_);pose.sides_[1].source.receipt_frame_policy=NativeAttachmentReceiptFramePolicy::exact;}
                assert(SetEvent(admission.resume));assert(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);
                assert(native_calls==before_race_native+1 && admission.acquires==before_race_acquire+1 && admission.releases==before_race_release+1 && !admission.active);
                assert(socket_substituted==(changed==0) && fixture_native[59].f[12]==0);
                CloseHandle(thread);CloseHandle(admission.entered);CloseHandle(admission.resume);admission.entered=admission.resume=nullptr;
                put(fixture_model.data()+0x1860,source.model_frame);put(fixture_animation.data()+0x38,model);
            }
        }
        expected_joint=.4f;expect_admitted=true;
        source.receipt_frame_policy=NativeAttachmentReceiptFramePolicy::exact;
        contact_policy();assert(publish());hook_mode=3;
        const auto before_contact_native=native_calls,before_contact_release=admission.releases;
        assert(catch_fixture_seh(&invoke_query,nullptr));
        assert(native_calls==before_contact_native+1 && admission.releases==before_contact_release+1 && !admission.active && fixture_native[59].f[12]==.7f);
        fixture_native[59].f[12]=0;hook_mode=0;
        source.observe_socket=nullptr;contact_policy();assert(publish());const auto observed_before_disabled=socket_observations;
        expected_joint=.4f;expect_admitted=true;invoke_query(nullptr);assert(socket_observations==observed_before_disabled);
        expected_admission=nullptr;source.admission={};
        NativeAttachmentPose::instance_=nullptr;
        pose.clear(1);assert(call(false)==17);
        assert(native_attachment_allowed_mask(3,2,true,true,true)==1); // CarryCorpse: the right arm is the game's, the left stays tracked
        assert(native_attachment_allowed_mask(3,0,true,false,true)==0);
        assert(native_attachment_allowed_mask(3,0,true,true,false)==0);
        assert(native_attachment_allowed_mask(3,3,true,true,true)==0);
        assert(originals>20 && pose.substitutions()>10);

    }
};
}
int main(){d2vr::NativeAttachmentPoseCheck::run();std::cout<<"PASS: immediate carry/authored/focus revocation, blocked publication, source revision/reference/mapping/model identity and slow-frame freshness, native-once and restored attachment slot, C++/SEH query/reader/callable cleanup, 512 native-hook calls with zero allocations\n";}
