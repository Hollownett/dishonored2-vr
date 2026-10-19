#include "../src/physical_grab.hpp"
#include <cassert>
#include <iostream>
#include <limits>
int main() {
    using namespace d2vr;
    {
        std::array<PhysicalGrab,2> grips;
        for(auto& grip:grips) {
            WorldTrackedPose pose{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
            assert(!grip.sample(pose,0,true,1,1000));
            assert(grip.sample(pose,std::nextafter(.45f,0.f),true,1,1010));assert(!grip.pressed());
            assert(grip.sample(pose,.45f,true,1,1020));assert(grip.pressed() && grip.bind(pose));
            pose.position.x=.02;assert(grip.sample(pose,.35f,true,1,1030));assert(grip.pressed());
            const auto retained=grip.motor(pose,true);assert(retained && !retained->release);
            pose.position.x=.04;assert(grip.sample(pose,std::nextafter(.35f,0.f),true,1,1040));assert(!grip.pressed());
            const auto released=grip.motor(pose,true);assert(released && released->release && released->linear.x>0 && !grip.held());
            assert(grip.sample(pose,.45f,true,1,1050));assert(grip.bind(pose));
        }
    }
    WorldTrackedPose hand{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}},object=hand;object.position={.1,0,0};
    PhysicalGrab grab;
    assert(!grab.sample(hand,.9f,true,1,100));assert(!grab.bind(object));
    assert(grab.sample(hand,.9f,true,1,110));assert(!grab.pressed()); // Held grip cannot acquire on focus return.
    grab.sample(hand,0,true,1,120);grab.sample(hand,.9f,true,1,130);assert(grab.bind(object));
    assert(length(grab.target().position-object.position)<1e-9); // No pickup teleport.
    hand.position.x=.2;grab.sample(hand,.9f,true,1,180);const auto moving=grab.motor(object,true);
    assert(moving && moving->linear.x>0 && length(moving->linear)<=10.000001);
    hand.basis={{{0,1,0},{-1,0,0},{0,0,1}}};grab.sample(hand,.9f,true,1,190);
    const auto turned=grab.target();assert(length(turned.position-Vec3{.2,.1,0})<1e-9);
    assert(grab.motor(object,true)->angular.z>0);
    grab.sample(hand,0,true,1,200);const auto thrown=grab.motor(object,true);assert(thrown && thrown->release && thrown->linear.x>0 && length(thrown->linear)<=12.000001);
    assert(!grab.held());object=hand;grab.sample(hand,.9f,true,1,210);assert(grab.bind(object));
    assert(!grab.sample(hand,.9f,true,2,220));assert(grab.motor(object,false)->release);assert(!grab.pressed());
    grab.sample(hand,0,true,2,230);grab.sample(hand,.9f,true,2,240);object.position=hand.position+Vec3{.4,0,0};assert(!grab.bind(object));
    hand.position.x+=.15;grab.sample(hand,.9f,true,2,250);assert(grab.bind(object));
    hand.position.x+=.1;grab.sample(hand,.9f,true,2,260);
    object.position.x-=2;assert(grab.motor(object,true)->release); // Blocked carry releases.
    grab.sample(hand,0,true,2,270);grab.sample(hand,.9f,true,2,280);object=hand;assert(grab.bind(object));
    assert(!grab.sample(hand,.9f,true,2,600));assert(grab.motor(object,false)->linear.x==0);
    // Low submission cadence must not drop a continuously tracked physical hold.
    for(const unsigned interval:{11u,33u,100u,200u}) {
        PhysicalGrab rate;WorldTrackedPose stable{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
        rate.sample(stable,0,true,1,1000);rate.sample(stable,0,true,1,1000+interval);
        rate.sample(stable,1,true,1,1000+2*interval);assert(rate.bind(stable));
        for(unsigned frame=3;frame<9;++frame) {
            assert(rate.sample(stable,1,true,1,1000+frame*interval));
            const auto held=rate.motor(stable,true);assert(held && !held->release);
        }
        rate.sample(stable,0,true,1,1000+9*interval);assert(rate.motor(stable,true)->release);
        rate.sample(stable,1,true,1,1000+10*interval);assert(rate.bind(stable));
        assert(!rate.sample(stable,1,false,1,1001+10*interval));assert(rate.motor(stable,false)->release);
    }
    {   // A press that waits ~400 ms for its owner (a corpse's native carry query) still presses when kept fresh, not otherwise.
        WorldTrackedPose still{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
        PhysicalGrab waited;waited.sample(still,0,true,1,1000);waited.sample(still,0,true,1,1010);
        for(std::uint64_t t=1030;t<=1410;t+=20) waited.keep_fresh(still,1,t);
        assert(waited.sample(still,1,true,1,1420) && waited.pressed() && waited.bind(still));
        PhysicalGrab stale;stale.sample(still,0,true,1,1000);stale.sample(still,0,true,1,1010);
        stale.sample(still,1,true,1,1420);assert(!stale.pressed()); // the gap reset the latch with the grip held
    }
    // Reproduce fast movement followed by stationary 110-250ms samples;
    // slow valid samples must decay the existing throw estimate before release.
    for(const unsigned interval:{110u,200u,250u}) {
        PhysicalGrab rate;WorldTrackedPose stable{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
        rate.sample(stable,0,true,1,1000);rate.sample(stable,0,true,1,1010);
        rate.sample(stable,1,true,1,1020);assert(rate.bind(stable));
        stable.position.x=.12;assert(rate.sample(stable,1,true,1,1040));
        assert(rate.velocity().x>2);
        auto slow_object=stable; // Object caught up to hand, isolating throw velocity.
        assert(rate.sample(stable,1,true,1,1040+interval));
        assert(length(rate.velocity())<.2);
        assert(rate.motor(slow_object,true) && !rate.motor(slow_object,true)->release);
        assert(rate.sample(stable,0,true,1,1040+2*interval));
        const auto release=rate.motor(slow_object,true);
        assert(release && release->release && length(release->linear)<.01);
        // Fresh rearm followed by a slow-interval teleport on the release frame
        // must release without throwing even if caller still says usable.
        rate.sample(stable,1,true,1,1050+2*interval);assert(rate.bind(slow_object));
        stable.position.x+=.6;
        assert(!rate.sample(stable,0,true,1,1050+3*interval));
        const auto teleport=rate.motor(slow_object,true);
        assert(teleport && teleport->release && length(teleport->linear)==0);
    }
    {
        PhysicalGrab duplicate;WorldTrackedPose stable{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
        duplicate.sample(stable,0,true,1,1000);duplicate.sample(stable,0,true,1,1010);
        duplicate.sample(stable,1,true,1,1020);assert(duplicate.bind(stable));
        auto stationary_object=stable;stable.position.x=.6;
        assert(!duplicate.sample(stable,0,true,1,1020));
        const auto release=duplicate.motor(stationary_object,true);
        assert(release && release->release && length(release->linear)==0);
    }
    // Native pending-release lane samples the release publication before throwing.
    for(const unsigned interval:{110u,200u,250u}) {
        PhysicalGrab pending;WorldTrackedPose stable{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
        pending.sample(stable,0,true,1,1000);pending.sample(stable,0,true,1,1010);
        pending.sample(stable,1,true,1,1020);assert(pending.bind(stable));
        stable.position.x=.12;pending.sample(stable,1,true,1,1040);assert(pending.velocity().x>2);
        const auto released=pending.release_sample(stable,0,true,1,1040+interval,true);
        assert(released.release && !pending.held() && length(released.linear)<.2);
        pending.sample(stable,1,true,1,1050+interval);assert(pending.bind(stable));stable.position.x+=.6;
        assert(length(pending.release_sample(stable,0,true,1,1050+2*interval,true).linear)==0);
    }
    const WorldTrackedPose identity{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
    auto rotated=identity;
    for(unsigned axis=0;axis<3;++axis) {
        rotated=identity;for(unsigned i=0;i<3;++i) if(i!=axis) rotated.basis[i]=rotated.basis[i]*-1;
        const auto error=grab_rotation_error(identity,rotated);
        assert(finite(error) && std::abs(length(error)-std::acos(-1.))<1e-9);
        PhysicalGrab half_turn;half_turn.sample(identity,0,true,1,100);half_turn.sample(identity,0,true,1,110);
        half_turn.sample(identity,.9f,true,1,120);assert(half_turn.bind(identity));half_turn.sample(rotated,.9f,true,1,130);
        const auto motor=half_turn.motor(identity,true);assert(motor && !motor->release && std::abs(length(motor->angular)-5)<1e-9);
    }
    for(const double angle:{1e-7,.2,1.57,3.1415,-.2,-3.1415}) {
        rotated=identity;rotated.basis={{{std::cos(angle),std::sin(angle),0},{-std::sin(angle),std::cos(angle),0},{0,0,1}}};
        assert(length(grab_rotation_error(identity,rotated)-Vec3{0,0,angle})<1e-8);
        assert(length(grab_rotation_error(rotated,identity)+Vec3{0,0,angle})<1e-8);
    }
    assert(length(grab_rotation_error(identity,identity))==0);
    // Explicit forgiving acquisition preserves the native object's pose. The
    // ordinary default remains .3; callers cannot request arbitrary remote grabs.
    PhysicalGrab forgiving;
    forgiving.sample(identity,0,true,1,100);forgiving.sample(identity,0,true,1,110);
    forgiving.sample(identity,.9f,true,1,120);object=identity;object.position={.4,0,0};
    assert(!forgiving.bind(object));
    assert(!forgiving.bind(object,object.position,.6));
    assert(!forgiving.bind(object,object.position,std::numeric_limits<double>::quiet_NaN()));
    assert(forgiving.bind(object,object.position,.45));
    assert(length(forgiving.target().position-object.position)<1e-9);
    assert(length(forgiving.motor(object,true)->linear)<1e-9);
    // An occupied native hand (weapon/power/carried body) leaves the lane
    // unusable: the squeeze never acquires and a held prop is released.
    PhysicalGrab occupied;
    occupied.sample(identity,.9f,false,1,100);assert(!occupied.pressed() && !occupied.bind(identity));
    occupied.sample(identity,0,true,1,110);occupied.sample(identity,.9f,true,1,120);assert(occupied.bind(identity));
    occupied.sample(identity,.9f,true,1,130);
    const auto occupied_release=occupied.motor(identity,false);
    assert(occupied_release && occupied_release->release && !occupied.held() && length(occupied_release->linear)==0);
    // Two frames share each 16 ms tick quantum: the in-quantum motion must stay in the baseline, not vanish.
    {
        PhysicalGrab dup;auto pose=identity;
        for(unsigned i=0;i<20;++i) {pose.position.x+=.011;dup.sample(pose,0,true,1,1000+16*(i/2));}
        assert(std::abs(dup.velocity().x-1.375)<1.375*.15);
    }
    // A held object follows the newest pose even when two samples share a clock quantum.
    {
        PhysicalGrab latest;auto pose=identity;auto held=identity;
        latest.sample(pose,0,true,1,100);latest.sample(pose,0,true,1,116);latest.sample(pose,.9f,true,1,132);
        assert(latest.bind(held));
        pose.position.x+=.011;latest.sample(pose,.9f,true,1,148);
        const auto before=latest.target().position.x;
        pose.position.x+=.011;latest.sample(pose,.9f,true,1,148);
        assert(std::abs(latest.target().position.x-before-.011)<1e-4);
    }
    // One non-finite pose sample mid-carry must not poison the velocity baseline for later samples.
    {
        PhysicalGrab nan_hand;auto pose=identity;auto held=identity;
        nan_hand.sample(pose,0,true,1,100);nan_hand.sample(pose,0,true,1,116);nan_hand.sample(pose,.9f,true,1,132);
        assert(nan_hand.bind(held));
        pose.position.x+=.011;nan_hand.sample(pose,.9f,true,1,148);
        auto bad=pose;bad.position.x=std::numeric_limits<double>::quiet_NaN();
        nan_hand.sample(bad,.9f,true,1,164);
        for(unsigned i=0;i<60;++i) {pose.position.x+=.011;nan_hand.sample(pose,0,true,1,180+16*i);}
        assert(std::isfinite(nan_hand.velocity().x) && std::isfinite(nan_hand.velocity().y) && std::isfinite(nan_hand.velocity().z));
        assert(nan_hand.velocity().x>.3);
    }
    std::cout<<"PASS: grip hysteresis/rearm, relative pose, bounded dynamic motor/throw, focus/epoch/stale/blocked release, occupied-hand refusal and release\n";
}
