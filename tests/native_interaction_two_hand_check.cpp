#define D2VR_NATIVE_INTERACTION_PURE
#include "../src/native_interaction_aim.hpp"
#include <cassert>
#include <iostream>
#include <limits>
int main() {
    using namespace d2vr;
    const std::array<bool,2> both{true,true},left{true,false},right{false,true},none{false,false};
    InteractionHandRouting routing;
    assert(routing.next(both,100)==0);
    const auto revision=routing.revision();
    for(unsigned now=101;now<220;++now) assert(routing.next(both,now)==0);
    assert(routing.revision()==revision);
    assert(routing.next(both,220)==1 && routing.revision()!=revision);
    assert(routing.next(both,339)==1);
    assert(routing.next(both,340)==0);
    routing.lock(1);const auto locked_revision=routing.revision();
    routing.lock(0);assert(routing.locked()==1 && routing.revision()==locked_revision);
    for(unsigned now=400;now<1000;now+=150) assert(routing.next(both,now)==1);
    assert(routing.next(left,1000)==-1); // No other hand steals an unavailable lock.
    assert(routing.next(right,1001)==1);
    routing.unlock();assert(routing.locked()==-1);
    routing.lock(7);assert(routing.locked()==-1);
    assert(routing.next(left,1100)==0);
    assert(routing.next(right,1101)==1);
    assert(routing.next(none,1102)==-1);
    routing.reset();assert(routing.locked()==-1 && routing.selected()==-1);
    const InteractionRay ray{{10,20,30,0,0,0,4,0,0,0,0,0}};
    const auto near=interaction_hit_point(ray,1,2.0);
    assert(near && near->x==10 && near->y==20 && near->z==32);
    const auto far=interaction_hit_point(ray,0x123401,4.0);
    assert(far && far->z==34);
    assert(!interaction_hit_point(ray,0x123400,2.0)); // Only AL is authoritative.
    assert(!interaction_hit_point(ray,1,std::nullopt));
    assert(!interaction_hit_point(ray,1,-1.0));
    assert(!interaction_hit_point(ray,1,10001.0));
    assert(!interaction_hit_point(ray,1,std::numeric_limits<double>::quiet_NaN()));
    const InteractionRay degenerate{{0,0,0,0,0,0,0,0,0,0,0,0}};
    assert(!interaction_hit_point(degenerate,1,1.0));
    const WorldTrackedPose hand{{1,2,3},{{{1,0,0},{0,1,0},{0,0,1}}}};
    // Native geometry ranges all follow the same exact tracked centre axis,
    // regardless of range start, callback order or time.
    for(const double start:{0.,.045,.095}) {
        InteractionRay native{};native.f[0]=static_cast<float>(1+start);native.f[1]=2;native.f[2]=3;native.f[4]=static_cast<float>(3-start);native.f[7]=1;
        const std::uint32_t flags=0x3f000007;std::memcpy(&native.f[11],&flags,4);
        const auto selected=controller_interaction_ray(native,{1,2,3},{1,0,0,0,1,0,0,0,1},hand);
        assert(selected);
        assert(std::abs(selected->f[0]-hand.position.x-start)<1e-6);
        assert(selected->f[1]==hand.position.y&&selected->f[2]==hand.position.z);
        assert(selected->f[5]==0&&selected->f[6]==0); // No off-axis cone substitution.
    }
    // Nearby static-item policy takes qualified native eligibility as input.
    // Direction never matters, and each hand evaluates its own surface volume.
    const std::array<NativeUseVolumeCandidate,4> items{{
        {1,{.2,-.02,-.02},{.24,.02,.02},true},
        {2,{-.3,-.02,-.02},{-.25,.02,.02},true},
        {3,{0,.1,0},{.02,.12,.02},false},
        {4,{0,.46,0},{.02,.47,.02},true}}};
    auto focus=native_use_volume_focus(items,Vec3{});assert(focus&&focus->owner==1&&focus->point.x==.2);
    focus=native_use_volume_focus(items,Vec3{-.4,0,0});assert(focus&&focus->owner==2);
    assert(!native_use_volume_focus(items,Vec3{0,0,2}));
    const std::array<NativeUseVolumeCandidate,2> retained{{
        {5,{.5,0,0},{.51,.01,.01},true},{6,{.42,0,0},{.43,.01,.01},true}}};
    assert(native_use_volume_focus(retained,Vec3{},5)->owner==5);
    assert(native_use_volume_focus(retained,Vec3{.15,0,0},5)->owner==5);
    auto challenger=retained;challenger[1].minimum={.3,0,0};challenger[1].maximum={.31,.01,.01};
    assert(native_use_volume_focus(challenger,Vec3{},5)->owner==6);
    auto invalid=items;for(auto& item:invalid)item.native_eligible=false;
    assert(!native_use_volume_focus(invalid,Vec3{}));
    invalid[0].native_eligible=true;invalid[0].minimum.x=1; // Inverted bounds cannot create eligibility.
    assert(!native_use_volume_focus(invalid,Vec3{}));
    InteractionHandRouting stable;
    assert(stable.next(both,100)==0);const auto stable_revision=stable.revision();
    for(unsigned now=101;now<2000;++now) assert(stable.next(left,now,true)==0);
    assert(stable.revision()==stable_revision); // No 120ms churn while native keeps eligibility.
    assert(stable.next(both,2119,false)==0); // Sticky: the hand that last had a target is searched first.
    assert(stable.next(both,2239,false)==0 && stable.next(both,2359,false)==0);
    assert(stable.next(both,2479,false)==1); // Sticky window (400ms) expired: bounded search resumes.
    {   // A qualified resting hand cannot starve the other: it gets a probe, and an empty probe returns.
        InteractionHandRouting fair;assert(fair.next(both,100)==0);bool probed=false;
        for(unsigned now=101;now<800;++now) if(fair.next(both,now,fair.selected()==0)==1) probed=true;
        assert(probed);
        assert(fair.next(both,900,fair.selected()==0)==0);
    }
    { // A resting other hand is never probed (a held guard/choke prompt must not blink); a moving one is.
        InteractionHandRouting rest;assert(rest.next(both,100)==0);
        for(unsigned now=101;now<2000;++now) assert(rest.next(both,now,rest.selected()==0,{false,false})==0);
        bool probed=false;for(unsigned now=2000;now<2700;++now) if(rest.next(both,now,rest.selected()==0,{false,true})==1) probed=true;
        assert(probed);
        InteractionHandRouting search;assert(search.next(both,100,false,{false,false})==0);
        for(unsigned now=101;now<600;now+=7) assert(search.next(both,now,false,{false,true})==1); // the moving hand gets every query
    }
    stable.reset();stable.next(both,3000);
    stable.lock(0);assert(stable.next(both,2120,true)==0);stable.unlock();
    stable.lock(1);assert(stable.next(both,2121,true)==1); // Opposite grip wins immediately.
    assert(stable.next(left,2122,true)==-1);
    std::cout<<"PASS: qualified-hand dwell, opposite grip override, centred native geometry ranges, native-qualified hand volumes and low-byte hit ABI\n";
}
