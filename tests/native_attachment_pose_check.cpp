#define NOMINMAX
#include "../src/native_attachment_pose.hpp"
#include <cassert>
#include <iostream>
#include <limits>
namespace {
alignas(16) std::array<std::byte,0x300> fixture_animation{};
alignas(16) std::array<std::byte,64> fixture_states{};
alignas(16) std::array<std::byte,0x1900> fixture_model{};
alignas(16) std::array<d2vr::rig::Transform,69> fixture_native{};
std::array<short,69> fixture_remap{};
bool read(std::uintptr_t address,void* out,std::size_t size){SIZE_T n{};return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,size,&n) && n==size;}
template<class T>void put(std::byte* address,const T& value){std::memcpy(address,&value,sizeof(value));}
}
namespace d2vr {
struct NativeAttachmentPoseCheck {
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
            policy(native_attachment_allowed_mask(3,2,true,true,true,false));
            assert(!pose.publish(1,animation,native,blended,source));
            assert(pose.around(animation,3,59,[&]{assert(fixture_native[59].f[12]==0);return UINT64{17};})==17);
        }
        for(const float native_weight:{.75f,.25f,0.f}) {
            auto blended=tracked;blended[59].f[12]=.4f*(1-native_weight);
            policy(native_attachment_allowed_mask(3,0,true,true,true,false));
            assert(pose.publish(1,animation,native,blended,source));
            assert(pose.around(animation,3,59,[&]{assert(fixture_native[59].f[12]==blended[59].f[12]);return UINT64{17};})==17);
            assert(fixture_native[59].f[12]==0); // Original authored slot restored.
        }
        pose.set_allowed(0,0,0); // Preserve existing initial-policy rejection case.
        assert(!publish());assert(call(false)==17); // no policy yet
        policy();assert(publish());assert(call(true)==17 && fixture_native[59].f[12]==0);
        policy(native_attachment_allowed_mask(3,0,true,true,true,true));publish();assert(call(false)==17);
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
        pose.clear(1);assert(call(false)==17);
        assert(native_attachment_allowed_mask(3,2,true,true,true,false)==1);
        assert(native_attachment_allowed_mask(3,0,true,false,true,false)==0);
        assert(native_attachment_allowed_mask(3,0,true,true,false,false)==0);
        assert(native_attachment_allowed_mask(3,3,true,true,true,false)==0);
        assert(originals>20 && pose.substitutions()>10);

    }
};
}
int main(){d2vr::NativeAttachmentPoseCheck::run();std::cout<<"PASS: immediate carry/authored/focus revocation, blocked publication, source revision/reference/mapping/model identity and slow-frame freshness, native-once and restored attachment slot\n";}
