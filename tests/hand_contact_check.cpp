#include "../src/native_hand_contact.hpp"
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <limits>
#include <thread>
#include <vector>
using namespace d2vr;
namespace d2vr {
struct NativeHandContactPublicationFixture {
    struct Published {bool active{};HandProxies proxies{};Vec3 velocity{},lag{};std::uintptr_t held_by{};std::uint64_t serial{};};
    static Published snapshot(NativeHandContact& contact,unsigned side) {
        std::array<NativeHandContact::Hand,2> hands{};contact.snapshot(hands,NativeHandContact::seconds(),false);
        const auto& hand=hands[side];return {hand.active,hand.proxies,hand.velocity,hand.lag,hand.held_by,hand.serial};
    }
    static unsigned kind(const NativeHandContact& contact,std::uintptr_t entity) {
        for(const auto& n:contact.near_) if(n.entity==entity) return n.kind;
        return 99;
    }
    template<class Read> static unsigned prop_class(Read& read,std::uintptr_t base,std::uintptr_t entity) {return NativeHandContact::prop_class(read,base,entity);}
    // Is this entity a candidate the service is skipping at now_ms (refused as a prop, until ignore_until)?
    static bool ignored(const NativeHandContact& contact,std::uintptr_t entity,std::uint64_t now_ms) {
        for(const auto& n:contact.near_) if(n.entity==entity) return n.ignore_until>now_ms;
        return false;
    }
    static bool tracked(const NativeHandContact& contact,std::uintptr_t entity) {
        for(const auto& n:contact.near_) if(n.entity==entity) return true;
        return false;
    }
};
}
namespace {
// The synthetic hand of tests/finger_ik_check.cpp: palm frame origin at the interior palm centre, fingers along +Y, palm normal -Z
// (a positive turn about axis (-1,0,0) closes a finger toward the palm).
rig::HandGeometry synthetic_geometry() {
    rig::HandGeometry g{};g.palm_in={0,0,-1};
    for(unsigned f=0;f<5;++f) {
        g.finger[f].pivot={.04-.02*f,f?.08:.03,0};
        g.finger[f].bone={Vec3{0,.03,0},Vec3{0,.02,0},Vec3{0,.02,0}};
        g.finger[f].axis={-1,0,0};
        g.finger[f].hinge={Vec3{-1,0,0},Vec3{-1,0,0},Vec3{-1,0,0}};
    }
    return g;
}
// Palm-down hand whose fingers point along world +X.
WorldTrackedPose palm_at(Vec3 p) {return {p,{{{0,-1,0},{1,0,0},{0,0,1}}}};}
HandProxies lone(Vec3 centre,double radius) {
    HandProxies p{};for(auto& s:p) s={{0,0,50},.01};p[0]={centre,radius};return p;
}
// A bottle standing at x (bounds .07 x .07 x .28).
NativeRigidBody bottle(double x,double y=0) {
    NativeRigidBody b{};b.pose={{x,y,.14},{{{1,0,0},{0,1,0},{0,0,1}}}};
    b.bounds=NativeGrabBounds{{x-.035,y-.035,0},{x+.035,y+.035,.28}};
    return b;
}
// A contact that has gone on for a while (steady state): the ramp of a fresh one is tested on its own.
HandContact hand_contact_s(const HandProxies& proxies,const rig::HeldShape& shape,Vec3 hand_velocity,Vec3 body_velocity,double mass,
        const HandContactConfig& cfg={},ContactInput in={}) {in.age=1;return hand_contact(proxies,shape,hand_velocity,body_velocity,mass,cfg,in);}
// Flat synthetic memory for cost checks (the byte map of the service check costs microseconds a byte): a few contiguous regions.
struct Arena {
    struct Region {std::uintptr_t base;std::vector<unsigned char> bytes;};
    std::vector<Region> regions;unsigned reads=0;
    void add(std::uintptr_t base,std::size_t size) {regions.push_back({base,std::vector<unsigned char>(size)});}
    unsigned char* span(std::uintptr_t at,std::size_t size) {
        for(auto& r:regions) if(at>=r.base && at+size<=r.base+r.bytes.size()) return r.bytes.data()+(at-r.base);
        return nullptr;
    }
    template<class T> void put(std::uintptr_t at,const T& value) {std::memcpy(span(at,sizeof value),&value,sizeof value);}
    bool operator()(std::uintptr_t at,void* out,std::size_t size) {++reads;const auto* p=span(at,size);if(!p) return false;std::memcpy(out,p,size);return true;}
};
}
int main() {
    std::cout<<std::unitbuf; // the numbers below survive a failed assert
    {
        const auto geometry=synthetic_geometry();const auto drawn=palm_at({1,2,3}),raw=palm_at({1.02,2,3});const Vec3 offset{0,.01,0};
        const std::array<float,5> curl{};HandJointTurns turns{};
        for(unsigned finger=0;finger<5;++finger)turns[finger]=rig::finger_turn_at(geometry.finger[finger],finger,1.);
        NativeHandContact contact;contact.set_geometry(1,geometry);
        contact.publish(1,true,drawn,raw,curl,123,offset,&turns);
        auto published=NativeHandContactPublicationFixture::snapshot(contact,1);
        auto palm=drawn;palm.position=palm.position+offset;
        const auto solved=hand_proxies(geometry,palm,turns),scalar=hand_proxies(geometry,palm,curl);
        assert(published.active && contact.wanted() && published.held_by==123 && length(published.lag-Vec3{.02,0,0})<1e-12);
        for(unsigned proxy=0;proxy<hand_proxy_count;++proxy)assert(length(published.proxies[proxy].centre-solved[proxy].centre)<1e-12 && published.proxies[proxy].radius==solved[proxy].radius);
        const auto tip=hand_palm_spheres+2*4+3;assert(length(published.proxies[tip].centre-scalar[tip].centre)>.03);
        const auto serial=published.serial;
        turns[2][1]=std::numeric_limits<double>::quiet_NaN();contact.publish(1,true,drawn,raw,curl,123,offset,&turns);
        published=NativeHandContactPublicationFixture::snapshot(contact,1);
        assert(!published.active && !contact.wanted() && published.serial==serial+1);
        contact.publish(1,true,drawn,raw,curl,123,offset);
        published=NativeHandContactPublicationFixture::snapshot(contact,1);
        assert(published.active && length(published.velocity)==0 && published.serial==serial+2 && published.held_by==123);
        for(unsigned proxy=0;proxy<hand_proxy_count;++proxy)assert(length(published.proxies[proxy].centre-scalar[proxy].centre)<1e-12);
        turns[2][1]=std::numeric_limits<double>::infinity();contact.publish(1,true,drawn,raw,curl,123,offset,&turns);assert(!contact.wanted());
        contact.publish(1,false,drawn,raw,curl,123,offset);assert(!contact.wanted());
    }
    // --- proxies
    {
        const auto g=synthetic_geometry();const auto palm=palm_at({1,2,3});
        const auto open=hand_proxies(g,palm,{0,0,0,0,0});
        for(const auto& s:open) assert(finite(s.centre) && s.radius>0 && s.radius<.1);
        assert(length(open[0].centre-Vec3{1+.0143,2+.0144,3+.0009})<1e-9 && open[0].radius==.0254);
        const auto tip=open[hand_palm_spheres+4*2+3].centre;
        assert(std::abs(tip.x-(1+.08+.07-(g.thickness[2]+hand_joint_add)*hand_tip_pull))<1e-9 && std::abs(tip.z-3)<1e-9 && std::abs(tip.y-(2-(.04-.04)))<1e-9);
        const auto fist=hand_proxies(g,palm,{1,1,1,1,1});
        const auto curled=fist[hand_palm_spheres+4*2+3].centre;
        assert(curled.z<3-.01 && curled.x<tip.x-.03); // the fingertip folds toward the palm side (down) and back
        const auto bad=hand_proxies(g,palm,{std::nanf(""),-3,9,0,0});
        for(const auto& s:bad) assert(finite(s.centre)); // a bad curl is clamped, not propagated
    }
    {
        auto geometry=synthetic_geometry();auto& thumb=geometry.finger[0];
        thumb.pivot={};thumb.hinge={Vec3{0,0,1},Vec3{1,0,0},Vec3{1,0,0}};
        thumb.existing={-3.14159265358979323846/2,-3.14159265358979323846/2,0};
        const WorldTrackedPose palm{{1,2,3},{{{1,0,0},{0,1,0},{0,0,1}}}};
        const auto bent=hand_proxies(geometry,palm,{0,0,0,0,0});
        assert(length(bent[hand_palm_spheres+1].centre-Vec3{.97,2,3})<1e-9);
        assert(length(bent[hand_palm_spheres+2].centre-Vec3{.97,2,3.02})<1e-9);
        assert(length(bent[hand_palm_spheres+3].centre-Vec3{.97,2,3.04-(geometry.thickness[0]+hand_thumb_joint_add)*hand_tip_pull})<1e-9);
        thumb.hinge={Vec3{1,0,0},Vec3{1,0,0},Vec3{1,0,0}};
        thumb.existing={.2,0,0};thumb.lowest={-.2,0,0};
        const auto opened=hand_proxies(geometry,palm,{0,0,0,0,0});
        assert(length(opened[hand_palm_spheres+1].centre-Vec3{1,2+.03*std::cos(.2),3-.03*std::sin(.2)})<1e-9);
    }
    {
        const auto right=synthetic_geometry();auto left=right;
        const auto mirror=[](Vec3 point) {return Vec3{-point.x,point.y,point.z};};
        left.palm_in=mirror(right.palm_in);
        for(auto& finger:left.finger) {
            finger.pivot=mirror(finger.pivot);
            for(auto& bone:finger.bone) bone=mirror(bone);
            for(auto& hinge:finger.hinge) hinge=mirror(hinge)*-1.;
        }
        const WorldTrackedPose palm{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
        for(const float curl:{0.f,.5f,1.f}) {
            const auto right_proxies=hand_proxies(right,palm,{curl,curl,curl,curl,curl});
            const auto left_proxies=hand_proxies(left,palm,{curl,curl,curl,curl,curl});
            for(std::size_t proxy=0;proxy<hand_proxy_count;++proxy) {
                assert(length(left_proxies[proxy].centre-mirror(right_proxies[proxy].centre))<1e-9);
                assert(right_proxies[proxy].radius==left_proxies[proxy].radius && right_proxies[proxy].radius<.035);
            }
            for(unsigned finger=0;finger<5;++finger) {
                const auto points=rig::finger_points(right.finger[finger],rig::finger_turn_at(right.finger[finger],finger,curl));
                const auto& tip=right_proxies[hand_palm_spheres+finger*4+3];
                assert(dot(tip.centre-points[3],unit(points[3]-points[2]))+tip.radius<0);
            }
        }
    }
    // --- hand motion
    {
        HandMotion m;assert(length(m.velocity())==0);
        double t=100;Vec3 p{};
        for(int i=0;i<30;++i,t+=.011) {p.x=.5*(t-100);m.sample(p,t);}
        assert(std::abs(m.velocity().x-.5)<.02 && std::abs(m.velocity().y)<1e-9);
        // 30 ms steps of 0.5 cm (a scripted 0.16 m/s hand): the estimate stays within 25% of the true speed.
        HandMotion stepped;double lo=1e9,hi=0;Vec3 q{};
        for(int i=0;i<120;++i) {const double ts=i*.0111;q.x=.16*(std::floor(ts/.03)*.03);stepped.sample(q,ts);if(i>40) {lo=std::min(lo,stepped.velocity().x);hi=std::max(hi,stepped.velocity().x);}}
        assert(lo>.16*.75 && hi<.16*1.25);
        // v506 live sideways: a hand stepped every 30 ms and sampled at irregular instants (a gap of 4 ms turned a 0.4 cm step into 1 m/s and
        // the fast attack believed it): the estimate stays near the true 0.146 m/s whatever the timing
        {
            HandMotion jitter;unsigned lcg=12345;double ts=0,lo2=1e9,hi2=0;
            for(int i=0;i<400;++i) {
                lcg=lcg*1664525u+1013904223u;const double wander=((lcg>>8)&0xffff)/65535.*.008-.004; // +-4 ms around the 11 ms frame
                ts=std::max(ts+.0042,i*.0111+wander);
                const double held=std::floor(ts/.03)*.03;jitter.sample({.146*held,0,0},ts);
                if(i>60) {lo2=std::min(lo2,jitter.velocity().x);hi2=std::max(hi2,jitter.velocity().x);}
            }
            assert(lo2>.09 && hi2<.23);
            // two spikes in a row are a swat, not a step
            HandMotion two;two.sample({0,0,0},0);two.sample({0,0,0},.0111);two.sample({.028,0,0},.0222);assert(two.velocity().x<.6);
            two.sample({.056,0,0},.0333);assert(two.velocity().x>1.5);
        }
        // a swat arrives: the estimate follows within two samples; a hand that stops lets go within a few more
        HandMotion swat;swat.sample({0,0,0},0);for(int i=1;i<=18;++i) swat.sample({0,0,0},.0111*i);
        swat.sample({.0278,0,0},.0111*19);swat.sample({.0556,0,0},.0111*20); // 2.5 m/s
        assert(swat.velocity().x>1.7 && swat.velocity().x<2.6);
        double at=.0111*20,x=.0556;for(int i=0;i<12;++i) {at+=.0111;swat.sample({x,0,0},at);}
        assert(swat.velocity().x<.15);
        m.sample(p+Vec3{2,0,0},t+.011); // teleport: restart, no 180 m/s spike
        assert(length(m.velocity())==0);
        m.sample(p+Vec3{2,0,0},t+.5); // stale gap: restart
        assert(length(m.velocity())==0);
        HandMotion swing;swing.sample({0,0,0},0);
        for(int i=1;i<20;++i) swing.sample({.1*i,0,0},.011*i); // 9 m/s: a hard swat is a real hand speed
        assert(swing.velocity().x>8 && swing.velocity().x<9.5);
        HandMotion snap;snap.sample({0,0,0},0);snap.sample({.001,0,0},.011);snap.sample({.27,0,0},.022); // a 27 cm snap turn in one frame
        assert(length(snap.velocity())==0);
        HandMotion dup;dup.sample({0,0,0},1);dup.sample({0,0,0},1.001);dup.sample({.011,0,0},1.011);
        assert(dup.velocity().x>0 && dup.velocity().x<1.1); // a 1 ms duplicate keeps the baseline
        dup.sample({0,0,std::nan("")},2);assert(length(dup.velocity())==0);
    }
    // --- shapes and mass
    {
        const auto s=prop_shape(bottle(0));
        assert(std::abs(s.radius-.035)<1e-6 && std::abs(s.half.x-.105)<1e-6);
        assert(rig::shape_distance(s,{0,0,.30})>.019 && rig::shape_distance(s,{0,0,.30})<.021); // 2 cm above the top
        assert(std::abs(length(shape_normal(s,{.1,0,.14}))-1)<1e-6 && shape_normal(s,{.1,0,.14}).x>.99);
        assert(prop_mass(0x4000,*bottle(0).bounds)==.5); // inverse mass 2 -> 0.5 kg, about what the volume says
        const auto estimate=prop_mass(0,*bottle(0).bounds);assert(estimate>.3 && estimate<.7);
        assert(prop_mass(0x3c00,*bottle(0).bounds)==estimate); // 0x3c00 read as a float is 0.0078: a ton, not credible
        const NativeGrabBounds crate{{0,0,0},{1,1,1}};assert(prop_mass(0x4000,crate)>100); // 0.5 kg crate is not credible either
    }
    // --- contact
    {
        const auto shape=prop_shape(bottle(0));const HandContactConfig cfg;
        const Vec3 rest{};
        auto distant=hand_contact_s(lone({-.3,0,.1},.06),shape,{.3,0,0},rest,.5);
        assert(!distant.touching && !distant.push && length(distant.velocity)==0);
        // Palm 1.5 cm into the bottle's side, moving at it at 0.3 m/s: it is driven at the hand's speed plus the spring that eases
        // the overlap down to the deadband.
        const double spring=(.015-cfg.depth_deadband)*cfg.depth_gain;
        auto slow=hand_contact_s(lone({-.08,0,.1},.06),shape,{.3,0,0},rest,.5);
        assert(slow.touching && slow.driving && slow.push && std::abs(slow.depth-.015)<1e-6);
        assert(std::abs(slow.velocity.x-(.3+spring))<1e-9 && std::abs(slow.velocity.y)<1e-9 && std::abs(slow.velocity.z)<1e-9);
        assert(slow.normal.x<-.99);
        // A resting hand: only the ease out of the overlap, at most depth_speed; a finger deep inside is left alone.
        auto still=hand_contact_s(lone({-.08,0,.1},.06),shape,{},rest,.5);
        assert(still.push && std::abs(still.velocity.x-(.015-cfg.depth_deadband)*cfg.depth_gain)<1e-6);
        auto deep=hand_contact_s(lone({-.05,0,.1},.06),shape,{},rest,.5); // 4.5 cm: deeper than `shallow`
        assert(deep.touching && !deep.push);
        // a resting hand through the prop (one finger 6 cm inside, another just touching the far side) pushes it nowhere
        HandProxies through_it=lone({-.0505,0,.1},.06);through_it[1]={{.05,0,.1},.017};
        auto buried=hand_contact_s(through_it,shape,{},rest,.5);assert(buried.touching && !buried.push);
        auto firm=hand_contact_s(lone({-.07,0,.1},.06),shape,{},rest,.5); // 2.5 cm: eases out, capped
        assert(firm.push && firm.velocity.x<=cfg.depth_speed+1e-9);
        // A touch inside the deadband with a still hand moves nothing.
        auto graze=hand_contact_s(lone({-.093,0,.1},.06),shape,{},rest,.5);
        assert(graze.touching && !graze.push);
        // Speed: slow contact is capped at 4 m/s; a swat (hand faster than 1.2 m/s) may reach 6; both along the push direction only.
        auto swat=hand_contact_s(lone({-.08,0,.1},.06),shape,{8,0,0},rest,.5);
        assert(std::abs(swat.velocity.x-cfg.swat_speed)<1e-9);
        auto brisk=hand_contact_s(lone({-.08,0,.1},.06),shape,{1.1,0,0},rest,.5);
        assert(std::abs(brisk.velocity.x-(1.1+spring))<1e-9);
        auto glance=hand_contact_s(lone({-.08,0,.1},.06),shape,{0,2,0},rest,.5); // sliding past: nothing to transfer
        assert(glance.touching && !glance.push);
        auto diagonal=hand_contact_s(lone({-.08,0,.1},.06),shape,{.3,.3,0},rest,.5); // 45 degrees onto the side: carried along the hand's motion
        assert(diagonal.push && std::abs(diagonal.velocity.x-diagonal.velocity.y)<1e-9 && std::hypot(diagonal.velocity.x,diagonal.velocity.y)>=.3*std::sqrt(2.)-1e-9);
        // Mass: 4 kg moves at half the speed, 12 kg and up not at all (touching still reported).
        auto heavy=hand_contact_s(lone({-.08,0,.1},.06),shape,{.3,0,0},rest,4.);
        assert(std::abs(heavy.velocity.x-(.3+spring)*.5)<1e-9);
        auto barrel=hand_contact_s(lone({-.08,0,.1},.06),shape,{.3,0,0},rest,12.);
        assert(barrel.touching && !barrel.push && length(barrel.velocity)==0);
        assert(!hand_contact_s(lone({-.08,0,.1},.06),shape,{.3,0,0},rest,0.).push);
        // Never lowered; sideways motion is kept.
        auto fast=hand_contact_s(lone({-.08,0,.1},.06),shape,{.3,0,0},{2,0,0},.5);
        assert(!fast.push && fast.velocity.x==2);
        auto mover=hand_contact_s(lone({-.08,0,.1},.06),shape,{.3,0,0},{.1,.5,0},.5);
        assert(mover.push && std::abs(mover.velocity.y-.5*(1-cfg.lateral_damp))<1e-9 && std::abs(mover.velocity.x-(.3+spring))<1e-9); // drift across the hand is damped
        // A retreating hand does not pull, resting or not.
        auto back=hand_contact_s(lone({-.093,0,.1},.06),shape,{-.5,0,0},rest,.5);
        assert(back.touching && !back.push);
        // Table: pressing down on the top pushes nothing; a hand above a shoulder pushes sideways only; below, it lifts.
        auto press=hand_contact_s(lone({0,0,.30},.06),shape,{0,0,-1},rest,.5);
        assert(press.touching && !press.push);
        auto shoulder=hand_contact_s(lone({.05,0,.31},.06),shape,{-.5,0,-1},rest,.5);
        assert(shoulder.touching && shoulder.push && shoulder.velocity.x<-.5 && std::abs(shoulder.velocity.z)<1e-12 && std::abs(shoulder.velocity.y)<1e-12);
        auto lift=hand_contact_s(lone({0,0,-.03},.06),shape,{0,0,.5},rest,.5);
        assert(lift.push && lift.velocity.z>.5 && lift.velocity.z<.5+cfg.depth_speed+1e-9);
        // Never up unless the hand is under the prop: the same hand beside its lower flank, rising, lifts nothing.
        auto flank=hand_contact_s(lone({-.08,0,.02},.06),shape,{.05,0,.4},rest,.5);
        assert(!flank.push || std::abs(flank.velocity.z)<1e-12);
        auto flank_push=hand_contact_s(lone({-.08,0,.02},.06),shape,{.3,0,.4},rest,.5);
        assert(flank_push.push && std::abs(flank_push.velocity.z)<1e-12 && flank_push.velocity.x>.3);
        // Non-finite inputs refuse.
        assert(!hand_contact_s(lone({-.08,0,.1},.06),shape,{std::nan(""),0,0},rest,.5).push);
        // A fingertip the grip-only world cast left past the prop's far side does not push it back: only the near side pushes.
        HandProxies skewer=lone({-.08,0,.1},.06);skewer[1]={{.05,0,.1},.017};
        auto through=hand_contact_s(skewer,shape,{.3,0,0},rest,.5);
        assert(through.push && std::abs(through.velocity.x-(.3+spring))<1e-9);
        // Sticky contact: a prop the hand drove within engage_ms is still driven 2 cm off the palm (no overlap), a fresh one is not;
        // a retreating hand lets go either way.
        auto fresh=hand_contact_s(lone({-.115,0,.1},.06),shape,{.3,0,0},rest,.5);
        auto sticky=hand_contact_s(lone({-.115,0,.1},.06),shape,{.3,0,0},rest,.5,cfg,ContactInput{true});
        assert(!fresh.touching && !fresh.push && !sticky.touching && sticky.driving && sticky.push && std::abs(sticky.velocity.x-.3)<1e-9);
        assert(!hand_contact_s(lone({-.115,0,.1},.06),shape,{-.3,0,0},rest,.5,cfg,ContactInput{true}).push);
        assert(!hand_contact_s(lone({-.2,0,.1},.06),shape,{.3,0,0},rest,.5,cfg,ContactInput{true}).push); // out of the slack
    }
    // --- a hand sliding a bottle along a table (1-D, friction like a table, 90 Hz): it moves with the hand, never launches or
    // passes through the hand, and stops when the hand does.
    {
        const double dt=1./90,mu_g=.4*9.81;double bx=0,bv=0,hx=-.2,peak=0,max_step=0,worst_overlap=0;const double hand_v=.3;
        for(int i=0;i<300;++i) {
            const bool moving=i<150;const Vec3 hv=moving?Vec3{hand_v,0,0}:Vec3{};
            if(moving) hx+=hand_v*dt;
            const auto shape=prop_shape(bottle(bx));
            const auto r=hand_contact_s(lone({hx,0,.1},.06),shape,hv,{bv,0,0},.5);
            if(r.push) {max_step=std::max(max_step,std::abs(r.velocity.x-bv));bv=r.velocity.x;}
            peak=std::max(peak,bv);
            bx+=bv*dt;bv=std::max(0.,bv-mu_g*dt);
            worst_overlap=std::max(worst_overlap,(hx+.06)-(bx-.035)); // palm sphere past the bottle's back face
        }
        assert(peak<.45 && peak>.25 && max_step<.45);
        assert(worst_overlap<.03); // never more than 3 cm into the prop
        assert(bx>.25 && bx<.5 && bv==0); // it travelled about as far as the hand did and came to rest
    }
    // --- a hand resting against a bottle: it eases out of the overlap and stays there
    {
        const double dt=1./90,mu_g=.4*9.81;double bx=0,bv=0,peak=0;
        for(int i=0;i<270;++i) {
            const auto r=hand_contact_s(lone({-.08,0,.1},.06),prop_shape(bottle(bx)),{},{bv,0,0},.5);
            if(r.push) bv=r.velocity.x;
            peak=std::max(peak,bv);bx+=bv*dt;bv=std::max(0.,bv-mu_g*dt);
        }
        assert(peak<=.3 && bx<.03 && bx>0 && bv==0); // pushed a couple of cm clear of the palm, then at rest
    }
    // --- friction the engine takes between writes, and the gap control that gives it back
    {
        PropDrag drag;
        assert(drag.decel()==0 && drag.compensation()==0);
        // written at .2 m/s, found at .12 m/s 16 ms later, again and again: 5 m/s^2 -> 1/60 s of it
        double t=1;for(int i=0;i<30;++i) {drag.wrote({.2,0,0},t);drag.observe({.12,0,0},t+.016);t+=.016;}
        assert(std::abs(drag.decel()-5)<.01 && std::abs(drag.compensation()-5./60)<.001);
        // a write with no step in between (the velocity found is the velocity written) lowers the estimate only by averaging in
        drag.wrote({.2,0,0},t);drag.observe({.2,0,0},t+.011);
        assert(drag.decel()>3 && drag.decel()<5);
        // blocked by a wall: it stopped; that is not friction
        PropDrag weak;for(int i=0;i<10;++i) {weak.wrote({.2,0,0},i*.016);weak.observe({.0,0,0},i*.016+.016);}
        assert(weak.decel()>10 && !weak.blocked(.17)); // a weak write friction ate whole: friction, ask for more next time
        PropDrag wall;for(int i=0;i<10;++i) {wall.wrote({.6,0,0},i*.016);wall.observe({.0,0,0},i*.016+.016);}
        assert(wall.blocked(.17) && wall.decel()==0); // a strong write that moved nothing: a wall
        // a speed-up (somebody else pushed it) is not negative friction; compensation is capped
        PropDrag other;other.wrote({.1,0,0},1);other.observe({.4,0,0},1.016);assert(other.decel()==0);
        // a resting hand's ease that friction keeps eating: after three stalls nothing is written for 0.25 s
        PropDrag held;for(int i=0;i<3;++i) {held.wrote({.05,0,0},i*.016);held.observe({0,0,0},i*.016+.016);}
        assert(held.stalled(.06) && !held.stalled(.4) && !held.blocked(.06));
        // pinned by a wall: after two writes it did not follow, the hand stops writing for 0.3 s, then tries once more
        PropDrag pinned;for(int i=0;i<2;++i) {pinned.wrote({.6,0,0},i*.016);pinned.observe({0,0,0},i*.016+.016);}
        assert(pinned.blocked(.04) && !pinned.blocked(.4));
        pinned.wrote({.6,0,0},.4);pinned.observe({.5,0,0},.416);assert(!pinned.blocked(.42));
        PropDrag rough;for(int i=0;i<30;++i) {rough.wrote({.5,0,0},i*.016);rough.observe({.2,0,0},i*.016+.016);}
        assert(rough.compensation()<=PropDrag::limit+1e-12);
    }
    // --- the engine's order of events: the solver takes friction off the velocity before it moves the prop (all of it, or only part: the
    // v505 sim bottle ran 1.1-1.2x ahead of the hand), in 1/62.5 s steps, while the hand writes at 90 Hz. A prop written at the hand's speed
    // lags (v503: 0.6-0.7x); with the friction given back and the gap trimmed it keeps its place on the hand, whichever way the engine is.
    {
        struct Ride {double carried{},range{},ratio{},first{},peak{};};
        const auto ride=[](double mu,double phase,double start_gap) {
            const double render=1./90,step=1./62.5,mu_g=mu*9.81,hand_v=.16;const auto g=synthetic_geometry();const HandContactConfig cfg;
            const double tip_reach=rig::finger_points(g.finger[2],rig::finger_turn_at(g.finger[2],2,0))[3].y;
            double hx=-.035-start_gap-tip_reach,bx=0,bvx=0,next=step,engage_start=0,hx0=0,bx0=0,lo=1e9,hi=-1e9;std::uint64_t engaged_until=0;PropDrag drag;
            Ride out{};out.first=-1;bool started=false;double stop=0,bx_stop=0,hx_stop=0;
            for(double now=0;now<4.2;now+=render) {
                const bool moving=now<.2+.35/hand_v;
                if(moving && now>=.2) hx+=hand_v*render;
                if(moving && now>=.2) {stop=now;hx_stop=hx;bx_stop=bx;}
                const auto proxies=hand_proxies(g,palm_at({hx,0,.1}),{0,0,0,0,0});
                const std::uint64_t ms=1000+static_cast<std::uint64_t>(now*1000);
                drag.observe({bvx,0,0},now);
                ContactInput in;in.engaged=engaged_until>ms;in.friction=drag.compensation();in.trim=drag.trim();in.age=in.engaged?now-engage_start:0.;
                in.blocked=drag.blocked(now);in.stalled=drag.stalled(now);
                const auto r=hand_contact(proxies,prop_shape(bottle(bx)),moving&&now>=.2?Vec3{hand_v,0,0}:Vec3{},{bvx,0,0},.5,cfg,in);
                drag.steer(r,now);
                if(r.driving) {if(!in.engaged) engage_start=now;engaged_until=ms+cfg.engage_ms;if(!started) {started=true;hx0=hx;bx0=bx;}}
                if(r.push) {if(out.first<0) out.first=r.velocity.x;bvx=r.velocity.x;drag.wrote(r.velocity,now);}
                out.peak=std::max(out.peak,bvx);
                if(started && moving && now>.2) {if(now>=engage_start+.1) {lo=std::min(lo,bx-hx);hi=std::max(hi,bx-hx);}}
                while(next<=now+render) { // the physics steps of this frame: the position sees `phase` of the step's friction, the velocity all of it
                    const double loss=std::min(bvx,mu_g*step);
                    bx+=(bvx>mu_g*step?bvx-phase*loss:0.)*step;bvx-=loss;next+=step;
                }
            }
            out.carried=bx-.035-(hx+tip_reach);out.range=hi-lo;out.ratio=(bx_stop-bx0)/(hx_stop-hx0);
            return out;
        };
        for(const auto& [mu,phase]:{std::pair{.4,1.},std::pair{.4,.5},std::pair{.4,.3},std::pair{2.,1.},std::pair{1.,.6}}) {
            const auto r=ride(mu,phase,.06);
            std::cout<<"ride mu "<<mu<<" phase "<<phase<<": ends "<<r.carried*100<<" cm from the fingertips, travel ratio "<<r.ratio<<", gap range "<<r.range*100<<" cm, first write "<<r.first<<" m/s\n";
            assert(std::abs(r.carried)<.02 && std::abs(r.ratio-1)<.06 && r.range<.015 && r.first<=HandContactConfig{}.write_step+1e-9);
        }
        // starting 12 cm from the bottle, the fingers already 5 cm into its envelope: the first write is still a ride, then the starting overlap is eased out
        const auto close=ride(.4,.5,-.05); // palm 12 cm from the bottle's face
        std::cout<<"ride from 12 cm: first write "<<close.first<<" m/s, peak "<<close.peak<<" m/s, ends "<<close.carried*100<<" cm from the fingertips\n";
        assert(close.first<=HandContactConfig{}.write_step+1e-9 && close.peak<.16+HandContactConfig{}.depth_speed+.05 && std::abs(close.carried)<.02);
    }
    // --- a prop pinned against a wall by a hand that keeps pushing: nothing written while blocked, and no sideways squirt on the way
    {
        const HandContactConfig cfg;const auto shape=prop_shape(bottle(0));
        ContactInput pinned;pinned.blocked=true;
        auto held=hand_contact(lone({-.08,0,.1},.06),shape,{.3,0,0},{},.5,cfg,pinned);
        ContactInput stalled;stalled.stalled=true;
        assert(!hand_contact(lone({-.08,0,.1},.06),shape,{},{},.5,cfg,stalled).push && hand_contact(lone({-.08,0,.1},.06),shape,{.3,0,0},{},.5,cfg,stalled).push); // only a resting hand's ease
        assert(held.touching && held.driving && !held.push && length(held.velocity)==0);
        // the hand sinks 3 cm into it while it cannot move: whatever is written stays on the hand's line
        for(const double deep:{.015,.03,.045}) {
            auto sink=hand_contact(lone({-.035-.06+deep,.02,.1},.06),shape,{.3,0,0},{},.5,cfg);
            assert(sink.push && std::abs(sink.velocity.y)<1e-12 && std::abs(sink.velocity.z)<1e-12 && sink.velocity.x<=.3+cfg.depth_speed+1e-9);
        }
    }
    // --- a prop is kept upright, and only driven along the hand's motion
    {
        const HandContactConfig cfg;const auto shape=prop_shape(bottle(0));
        // tilted 5 degrees toward +x (its axis leans from the vertical), pushed at its middle: the spin turns it back
        const double tilt=5*3.14159265358979323846/180;
        ContactInput in;in.basis={{{std::cos(tilt),0,-std::sin(tilt)},{0,1,0},{std::sin(tilt),0,std::cos(tilt)}}};
        auto upright=hand_contact(lone({-.08,0,.1},.06),shape,{.3,0,0},{},.5,cfg,in);
        assert(upright.push && upright.spin.y<-1.5 && upright.spin.y>-cfg.upright_max-1e-9 && std::abs(upright.spin.x)<1e-9 && std::abs(upright.spin.z)<1e-9);
        // the written spin rotates its axis back: d(axis)/dt = spin x axis points toward the vertical
        const Vec3 axis{std::sin(tilt),0,std::cos(tilt)};const Vec3 rate=cross(upright.spin,axis);assert(rate.x<0);
        // a perfectly upright prop: nothing to correct
        auto flat=hand_contact(lone({-.08,0,.1},.06),shape,{.3,0,0},{},.5,cfg);assert(flat.push && length(flat.spin)<1e-9);
        // pushed high (above its middle by 8 cm): the hand means to tip it, the spin is left alone
        auto high=hand_contact(lone({-.08,0,.27},.06),shape,{.3,0,0},{},.5,cfg,in);assert(high.push && length(high.spin)==0 && !high.turn); // 13 cm above the middle: past the fade
        auto upper=hand_contact(lone({-.08,0,.21},.06),shape,{.3,0,0},{},.5,cfg,in);assert(length(upper.spin)>0 && length(upper.spin)<length(upright.spin)); // 7 cm above: fading
        // the spin is worth a write on its own: the prop already moves at the hand's speed, only the tilt needs the write
        auto spin_only=hand_contact_s(lone({-.08,0,.1},.06),shape,{.3,0,0},{.45,0,0},.5,cfg,in);assert(!spin_only.push && spin_only.turn && spin_only.spin.y<0);
        // lying down (axis horizontal): no vertical axis near enough, a lying bottle is not stood up
        ContactInput lying;lying.basis={{{1,0,0},{0,std::cos(.7),std::sin(.7)},{0,-std::sin(.7),std::cos(.7)}}};
        auto rolled=hand_contact(lone({-.08,0,.1},.06),shape,{.3,0,0},{},.5,cfg,lying);assert(length(rolled.spin)==0);
        // 8 cm off the hand's line: no squirt and no hop, whatever the contact normal says
        const auto g=synthetic_geometry();
        for(const auto& [offset,palm_x]:{std::pair{.08,-.15},std::pair{-.05,-.15}}) {
            const auto proxies=hand_proxies(g,palm_at({palm_x,0,.1}),{0,0,0,0,0});
            auto off=hand_contact_s(proxies,prop_shape(bottle(0,offset)),{.2,0,0},{},.5,cfg);
            std::cout<<"off "<<offset<<" touching "<<off.touching<<" push "<<off.push<<" v "<<off.velocity.x<<" "<<off.velocity.y<<" "<<off.velocity.z<<" gap "<<off.gap<<std::endl;
            assert(off.touching && off.push && std::abs(off.velocity.y)<1e-12 && std::abs(off.velocity.z)<1e-12 && off.velocity.x>=.2);
        }
    }
    // --- a 0.35 m slow push (0.16 m/s) with the whole hand, friction like a table, 90 Hz, sticky contact as the service keeps it:
    // the bottle ends against the fingertips whatever side of the hand it was met on, and does not squirt off sideways.
    for(const double offset:{0.,.03,.05,.08,-.05}) {
        const double dt=1./90,mu_g=.4*9.81,hand_v=.16;const auto g=synthetic_geometry();const HandContactConfig cfg;
        double hx=-.30,bx=0,by=offset,bvx=0,bvy=0,peak=0;std::uint64_t t=1000,engaged_until=0;unsigned pushes=0;
        for(int i=0;i<330;++i) {
            const bool moving=i<219; // 0.35 m, then 1.5 s of rest
            if(moving) hx+=hand_v*dt;
            t+=11;
            const auto proxies=hand_proxies(g,palm_at({hx,0,.1}),{0,0,0,0,0});
            const auto r=hand_contact(proxies,prop_shape(bottle(bx,by)),moving?Vec3{hand_v,0,0}:Vec3{},{bvx,bvy,0},.5,cfg,engaged_until>t);
            if(r.driving) engaged_until=t+cfg.engage_ms;
            if(r.push) {bvx=r.velocity.x;bvy=r.velocity.y;++pushes;}
            peak=std::max(peak,std::hypot(bvx,bvy));
            bx+=bvx*dt;by+=bvy*dt;
            const double speed=std::hypot(bvx,bvy),slow=mu_g*dt;
            if(speed<=slow) bvx=bvy=0;else {bvx*=(speed-slow)/speed;bvy*=(speed-slow)/speed;}
        }
        const double front=hx+.15;
        const auto final_shape=prop_shape(bottle(bx,by));double tip_gap=1e9;
        const auto final_palm=palm_at({hx,0,.1});
        for(unsigned finger=1;finger<5;++finger) {
            const auto tip=rig::finger_points(g.finger[finger],rig::finger_turn_at(g.finger[finger],finger,0))[3];
            const auto world_tip=final_palm.position+final_palm.basis[0]*tip.x+final_palm.basis[1]*tip.y+final_palm.basis[2]*tip.z;
            tip_gap=std::min(tip_gap,std::abs(rig::shape_distance(final_shape,world_tip)));
        }
        std::cout<<"carry: bottle "<<offset*100<<" cm off the hand's line ends "<<(bx-.035-front)*100<<" cm from the fingertips, "<<(by-offset)*100<<" cm sideways, peak "<<peak<<" m/s, "<<pushes<<" pushes\n";
        assert(tip_gap<.02);
        assert(std::abs(by-offset)<.02);             // no squirt
        assert(peak<.16+cfg.depth_speed+1e-9);       // never launched
        assert(pushes>60);                           // continuous: a push most frames of the contact (219 frames, the first ~55 before it)
    }
    // --- mass-aware pushes (C5): one write raises the prop's speed by at most push_force_n * dt / mass; a first write uses write_dt (1/90 s), so it is capped too
    {   // push_force_cap: an 8 kg box swatted at 3 m/s (200 N: 0.278 m/s a write at 90 Hz)
        const HandContactConfig cfg;NativeRigidBody box{};box.pose={{0,0,.2},{{{1,0,0},{0,1,0},{0,0,1}}}};box.bounds=NativeGrabBounds{{-.2,-.2,0},{.2,.2,.4}};
        const auto shape=prop_shape(box);const auto palm=lone({-.25,0,.2},.06);
        ContactInput in;in.dt=1./90;
        const auto capped=hand_contact_s(palm,shape,{3,0,0},{},8,cfg,in);
        const auto today=hand_contact_s(palm,shape,{3,0,0},{},8,cfg);
        in.dt=0;const auto uncapped=hand_contact_s(palm,shape,{3,0,0},{},8,cfg,in);
        std::cout<<"push_force_cap: 8 kg box, 3 m/s swat: "<<length(capped.velocity)<<" m/s capped (limit "<<200./90/8<<"), "<<length(today.velocity)<<" m/s uncapped\n";
        assert(cfg.push_force_n==200.);
        assert(capped.push && capped.capped && length(capped.velocity)<=200./90/8+1e-9 && length(capped.velocity)>=200./90/8-1e-9);
        assert(uncapped.push && !uncapped.capped && !today.capped && length(uncapped.velocity-today.velocity)==0 && length(today.velocity)>.5);
        // the cap counts from what the prop already does along the push: a box moving at 0.1 m/s may reach 0.1 + 0.278
        in.dt=1./90;const auto moving=hand_contact_s(palm,shape,{3,0,0},{.1,0,0},8,cfg,in);
        assert(moving.push && moving.capped && std::abs(moving.velocity.x-(.1+200./90/8))<1e-9);
        // a prop's first write is capped too (review I2): the service's dt for a prop it never wrote is one nominal frame, never 0
        const PropDrag fresh;assert(NativeHandContact::write_dt(fresh,123.)==1./90);
        PropDrag used;used.wrote({},10.);
        assert(std::abs(NativeHandContact::write_dt(used,10.+1./90)-1./90)<1e-12 && NativeHandContact::write_dt(used,10.001)==1./240 && NativeHandContact::write_dt(used,15.)==1./30);
        for(const double kg:{5.,11.}) { // a 3 m/s swat's first write onto a resting 5 kg / 11 kg prop: 200/90/5 = 0.444, 200/90/11 = 0.202 m/s
            ContactInput first;first.dt=NativeHandContact::write_dt(fresh,0.);
            const auto r=hand_contact_s(palm,shape,{3,0,0},{},kg,cfg,first);
            std::cout<<"first write: "<<kg<<" kg prop, 3 m/s swat: "<<length(r.velocity)<<" m/s (cap "<<200./90/kg<<")\n";
            assert(r.push && r.capped && length(r.velocity)<=200./90/kg+1e-9);
        }
        // the dv cap is in game time too (review M3): at time factor 0.5 one write adds half as much
        ContactInput half_time;half_time.dt=1./90;half_time.time_factor=.5;
        const auto slow=hand_contact_s(palm,shape,{3,0,0},{},8,cfg,half_time);assert(slow.push && slow.capped && std::abs(length(slow.velocity)-.5*200./90/8)<1e-9);
    }
    // bottle_unchanged: the carry loop's 0.5 kg slow ride writes exactly the same velocities with dt = 1/90 as with dt = 0
    {
        const auto ride=[](double write_dt) {
            const double dt=1./90,mu_g=.4*9.81,hand_v=.16;const auto g=synthetic_geometry();const HandContactConfig cfg;
            double hx=-.30,bx=0,bvx=0,bvy=0;std::uint64_t t=1000,engaged_until=0;std::vector<Vec3> written;bool capped=false;
            for(int i=0;i<330;++i) {
                const bool moving=i<219;if(moving) hx+=hand_v*dt;t+=11;
                ContactInput in;in.engaged=engaged_until>t;in.dt=write_dt;
                const auto r=hand_contact(hand_proxies(g,palm_at({hx,0,.1}),{0,0,0,0,0}),prop_shape(bottle(bx,0)),moving?Vec3{hand_v,0,0}:Vec3{},{bvx,bvy,0},.5,cfg,in);
                if(r.driving) engaged_until=t+cfg.engage_ms;
                if(r.push) {bvx=r.velocity.x;bvy=r.velocity.y;written.push_back(r.velocity);}
                capped=capped || r.capped;
                bx+=bvx*dt;
                const double speed=std::hypot(bvx,bvy),slow=mu_g*dt;
                if(speed<=slow) bvx=bvy=0;else {bvx*=(speed-slow)/speed;bvy*=(speed-slow)/speed;}
            }
            return std::make_pair(written,capped);
        };
        const auto [capped,any_capped]=ride(1./90);const auto [free,none]=ride(0);
        std::cout<<"bottle_unchanged: "<<capped.size()<<" writes with dt 1/90, "<<free.size()<<" with dt 0\n";
        assert(!any_capped && !none && capped.size()==free.size() && capped.size()>60);
        for(std::size_t i=0;i<capped.size();++i) assert(capped[i].x==free[i].x && capped[i].y==free[i].y && capped[i].z==free[i].z);
    }
    // time_factor_scales: the engine's time factor multiplies the push (phase 1 always passes 1, C13)
    {
        const HandContactConfig cfg;const auto shape=prop_shape(bottle(0));const auto palm=lone({-.085,0,.1},.06);
        ContactInput in;const auto full=hand_contact_s(palm,shape,{.3,0,0},{},.5,cfg,in);
        in.time_factor=.5;const auto half=hand_contact_s(palm,shape,{.3,0,0},{},.5,cfg,in);
        std::cout<<"time_factor_scales: "<<length(full.velocity)<<" m/s at 1, "<<length(half.velocity)<<" m/s at 0.5\n";
        assert(ContactInput{}.time_factor==1. && ContactInput{}.dt==0.);
        assert(full.push && half.push && std::abs(length(half.velocity)-.5*length(full.velocity))<1e-9 && length(full.velocity)>.3);
        // a time factor above 1 counts as 1 (review M3): a push never gets stronger than today
        in.time_factor=2.;const auto fast=hand_contact_s(palm,shape,{.3,0,0},{},.5,cfg,in);assert(fast.push && length(fast.velocity-full.velocity)==0);
    }
    // bad_time_factor_writes_nothing: a non-finite or non-positive time factor, a non-finite or negative dt: no write, no spin
    {
        const HandContactConfig cfg;const auto shape=prop_shape(bottle(0));const auto palm=lone({-.085,0,.1},.06);const double nan=std::nan("");
        for(const double factor:{nan,0.,-1.,std::numeric_limits<double>::infinity()}) {
            ContactInput in;in.time_factor=factor;const auto r=hand_contact_s(palm,shape,{.3,0,0},{.01,0,0},.5,cfg,in);
            assert(!r.push && !r.turn && !r.capped && r.velocity.x==.01 && r.velocity.y==0);
        }
        for(const double dt:{nan,-1.,std::numeric_limits<double>::infinity()}) {
            ContactInput in;in.dt=dt;const auto r=hand_contact_s(palm,shape,{.3,0,0},{.01,0,0},.5,cfg,in);
            assert(!r.push && !r.turn && !r.capped && r.velocity.x==.01 && r.velocity.y==0);
        }
    }
    // launch_cap (controller rulings, Task 6): a push never leaves the prop faster along the push than the hand moves along it + launch_margin (0.3,
    // times the time factor); a ride may add the friction it gives back. A dv cap per write limits the acceleration only; this caps the final speed:
    // the hand cannot throw by pushing. The default margin equals depth_speed, so the spring alone never reaches it: the cases below raise the spring.
    {
        const HandContactConfig base;const auto shape=prop_shape(bottle(0));
        assert(base.launch_margin==.3 && base.launch_margin>=base.depth_speed);
        auto cfg=base;cfg.depth_speed=.6;   // a spring that could throw: 5 cm into the bottle it adds 0.47 m/s
        auto loose=cfg;loose.launch_margin=1e9;
        const auto deep=lone({-.045,0,.1},.06);
        // a 2 m/s swat: the write stops at 2.3
        const auto swat=hand_contact_s(deep,shape,{2,0,0},{},1.,cfg),unbound=hand_contact_s(deep,shape,{2,0,0},{},1.,loose);
        std::cout<<"launch_cap: 2 m/s swat writes "<<length(swat.velocity)<<" m/s ("<<length(unbound.velocity)<<" without the cap)\n";
        assert(swat.push && std::abs(length(swat.velocity)-2.3)<1e-9 && length(unbound.velocity)>2.4);
        // a slow hand pressing into a prop (a ride, not a swat): the write stops at hand + margin + the friction it gives back (0.05: the engine takes that
        // off before the prop moves)
        ContactInput slow;slow.friction=.05;
        const auto press=hand_contact_s(deep,shape,{.146,0,0},{},1.,cfg,slow),press_unbound=hand_contact_s(deep,shape,{.146,0,0},{},1.,loose,slow);
        assert(press.push && std::abs(length(press.velocity)-(.146+.3+.05))<1e-9 && length(press_unbound.velocity)>.146+.35+.05);
        // a swat gives no friction back: the same friction estimate does not raise the cap
        const auto fast=hand_contact_s(deep,shape,{2,0,0},{},1.,cfg,slow);assert(fast.push && std::abs(length(fast.velocity)-2.3)<1e-9);
        // the hand's speed ALONG the push counts: a hand descending at 0.6 m/s while it moves 1.5 m/s sideways swats at 1.5 (+ the margin)
        const auto descending=hand_contact_s(deep,shape,{1.5,0,-.6},{},1.,cfg);
        assert(descending.push && std::abs(descending.velocity.x-1.8)<1e-9 && std::abs(descending.velocity.z)<1e-12);
        // a hand rising at 75 degrees at 3 m/s moves 0.776 m/s along the (horizontal) push: the write stops at 0.776 + 0.3, not at its 3 m/s + 0.3 (review M2)
        const double a75=75*3.14159265358979323846/180;
        const auto rising=hand_contact_s(deep,shape,{3*std::cos(a75),0,3*std::sin(a75)},{},1.,cfg),rising_loose=hand_contact_s(deep,shape,{3*std::cos(a75),0,3*std::sin(a75)},{},1.,loose);
        std::cout<<"launch_cap: hand rising at 75 degrees, 3 m/s: writes "<<length(rising.velocity)<<" m/s ("<<length(rising_loose.velocity)<<" without the cap)\n";
        assert(rising.push && std::abs(length(rising.velocity)-(3*std::cos(a75)+.3))<1e-9 && length(rising_loose.velocity)>3*std::cos(a75)+.35);
        // the cap is in game time: the time factor scales it with the push
        ContactInput slowmo;slowmo.time_factor=.5;
        const auto mo=hand_contact_s(deep,shape,{2,0,0},{},1.,cfg,slowmo);assert(mo.push && std::abs(length(mo.velocity)-1.15)<1e-9);
        // a prop already faster than the cap along the push is never slowed: nothing is written
        const auto ahead=hand_contact_s(deep,shape,{2,0,0},{2.5,0,0},1.,cfg);assert(!ahead.push && ahead.velocity.x==2.5);
        // the ease of a resting hand (no hand speed; it eases out of overlaps up to `shallow`, 3 cm) stays within the margin
        const auto rest=hand_contact_s(lone({-.067,0,.1},.06),shape,{},{},.5,cfg);assert(rest.push && length(rest.velocity)<=cfg.launch_margin+1e-9);
        // with the default spring the cap does not bind on a swat: the write is the hand + the spring, as before the cap
        const auto plain=hand_contact_s(deep,shape,{2,0,0},{},1.,base),plain_loose=hand_contact_s(deep,shape,{2,0,0},{},1.,[&]{auto c=base;c.launch_margin=1e9;return c;}());
        assert(plain.push && length(plain.velocity-plain_loose.velocity)==0);
    }
    // --- the world cast holds the drawn hand back from the prop (sim v525: 4-12 cm, whichever way its sweep crosses the prop): the tracked
    // hand pushes the prop that cast hit, and only that one; a shift past lag_limit is not believed
    {
        const HandContactConfig cfg;const auto shape=prop_shape(bottle(0));
        const Vec3 lag{.045,0,0};                                  // the tracked hand is 4.5 cm further along +x than the drawn one
        const auto raw=lone({-.08,0,.1},.06);                      // its palm sphere is 1.5 cm into the bottle (face at x=-.035)
        HandProxies drawn=raw;for(auto& p:drawn) p.centre=p.centre-lag; // the drawn one stops 3 cm short of touching it
        const auto straight=hand_contact_s(raw,shape,{.3,0,0},{},.5,cfg);
        ContactInput held;held.lag=lag;held.held=true;
        const auto cast=hand_contact_s(drawn,shape,{.3,0,0},{},.5,cfg,held);
        assert(straight.push && cast.push && std::abs(cast.velocity.x-straight.velocity.x)<1e-9 && std::abs(cast.depth-straight.depth)<1e-9 && std::abs(cast.error-straight.error)<1e-9);
        ContactInput elsewhere=held;elsewhere.held=false;       // held back by something else: its lag is nothing this prop knows about
        const auto behind_wall=hand_contact_s(drawn,shape,{.3,0,0},{},.5,cfg,elsewhere);
        assert(!behind_wall.touching && !behind_wall.push);
        ContactInput far;far.lag=Vec3{.4,0,0};far.held=true;    // 40 cm: more than the cast ever moves a hand
        HandProxies drawn_far=raw;for(auto& p:drawn_far) p.centre=p.centre-far.lag;
        assert(!hand_contact_s(drawn_far,shape,{.3,0,0},{},.5,cfg,far).touching);
        std::cout<<"cast: the tracked hand pushes the prop the drawn hand was held back from (depth "<<cast.depth*1000<<" mm), not another, not a 40 cm shift\n";
    }
    // --- a sideways pass with the cast's hold-off on (sim v525 hand-push-side2: the lag fed the gap spring, the writes ramped to 0.56 m/s for a hand
    // at 0.146 and the bottle ran 6 cm ahead). The hold-off appears whole as the hand nears the bottle and stays: 5 cm back along the fingers, 4.5 cm
    // back across the motion. The bottle must ride the hand as it does without the cast.
    {
        const double dt=1./90,mu_g=.4*9.81,hand_v=.146;const auto g=synthetic_geometry();const HandContactConfig cfg;
        double result[2]{};double top[2]{};double spread[2]{};
        for(int cast=0;cast<2;++cast) {
            double hy=-.30,by=0,bx=0,bvx=0,bvy=0,lo=1e9,hi=-1e9;std::uint64_t t=1000,engaged_until=0;double engaged_at=0;PropDrag drag;
            for(int i=0;i<420;++i) {
                const bool moving=i<250;const double now=i*dt;
                if(moving) hy+=hand_v*dt;
                t+=11;
                const Vec3 tracked{-.10,hy,.1},shift=cast && hy>-.20?Vec3{-.05,-.045,0}:Vec3{};
                const auto proxies=hand_proxies(g,palm_at(tracked+shift),{0,0,0,0,0});
                ContactInput in;in.engaged=engaged_until>t;in.age=in.engaged?now-engaged_at:0.;in.lag=shift*-1.;in.held=cast!=0;
                in.friction=drag.compensation();in.trim=drag.trim();in.basis=bottle(bx,by).pose.basis;in.blocked=drag.blocked(now);in.stalled=drag.stalled(now);
                drag.observe({bvx,bvy,0},now);
                const auto r=hand_contact(proxies,prop_shape(bottle(bx,by)),moving?Vec3{0,hand_v,0}:Vec3{},{bvx,bvy,0},.5,cfg,in);
                if(r.driving) {if(!in.engaged) engaged_at=now;engaged_until=t+cfg.engage_ms;}
                drag.steer(r,now);
                if(r.push) {bvx=r.velocity.x;bvy=r.velocity.y;drag.wrote({bvx,bvy,0},now);}
                top[cast]=std::max(top[cast],std::hypot(bvx,bvy));
                bx+=bvx*dt;by+=bvy*dt;
                const double speed=std::hypot(bvx,bvy),slow=mu_g*dt;
                if(speed<=slow) bvx=bvy=0;else {bvx*=(speed-slow)/speed;bvy*=(speed-slow)/speed;}
                if(moving && i>150) {lo=std::min(lo,by-hy);hi=std::max(hi,by-hy);}
            }
            result[cast]=by;spread[cast]=hi-lo;
        }
        std::cout<<"cast sideways: bottle ends at y "<<result[0]*100<<" cm without the cast, "<<result[1]*100<<" cm with it; peak "<<top[0]<<" / "<<top[1]<<" m/s; hand-to-bottle spread "<<spread[0]*100<<" / "<<spread[1]*100<<" cm\n";
        assert(std::abs(result[1]-result[0])<.02);            // the same ride
        assert(top[1]<hand_v+.15);                            // the hand's speed and the friction given back: never the 0.56 m/s of the lag-fed spring
        assert(spread[1]<spread[0]+.015);                     // the bottle keeps its place on the hand
    }
    // --- the way out: the drawn hand meets what it pushes (HandContact::outward is how the tracked hand leaves the prop; set whether or not anything is pushed)
    {
        const HandContactConfig cfg;const auto shape=prop_shape(bottle(0));
        const auto pushing=hand_contact_s(lone({-.08,0,.1},.06),shape,{.3,0,0},{},.5,cfg);       // the palm sphere is 1.5 cm into the face at x=-.035
        assert(pushing.push && std::abs(pushing.outward.x+.015)<.002 && std::abs(pushing.outward.y)<1e-9 && std::abs(pushing.outward.z)<1e-9);
        const auto resting=hand_contact_s(lone({-.08,0,.1},.06),shape,{},{},.5,cfg);              // a hand at rest in it: nothing to push, the same way out
        assert(std::abs(resting.outward.x+.015)<.002);
        const auto buried=hand_contact_s(lone({-.03,0,.1},.06),shape,{},{},.5,cfg);               // palm centre 5 mm into the prop: pushes nothing, the way out is still told
        assert(!buried.push && length(buried.outward)>.06);
        assert(length(hand_contact_s(lone({-.2,0,.1},.06),shape,{.3,0,0},{},.5,cfg).outward)==0); // clear of it
        const auto crate=hand_contact_s(lone({-.08,0,.1},.06),shape,{.3,0,0},{},50.,cfg);          // heavy: nothing pushed, the way out still told (meet() leaves heavy props to the cast)
        assert(!crate.push && std::abs(crate.outward.x+.015)<.002);
        std::cout<<"outward: "<<pushing.outward.x*100<<" cm for a palm 1.5 cm in, "<<length(buried.outward)*100<<" cm for a buried one\n";
    }
    // --- a drawn hand moved sideways by something that is not a prop of ours says nothing about the prop (sim v509 s2b/s2c: one onset frame, the drawn proxies 6 cm
    // inside the bottle, a write of 0.5 m/s for a hand at 0.15): nothing is read from it; the table's vertical lift and a lag that is a prop's stay trusted
    {
        const HandContactConfig cfg;const auto shape=prop_shape(bottle(0));
        const auto deep=lone({-.03,0,.1},.06);                      // the drawn palm 5 mm into the prop's axis: 6.5 cm of overlap
        ContactInput stranger;stranger.lag=Vec3{.06,0,0};          // a clamp of something else moved it 6 cm sideways, held false
        const auto glitch=hand_contact_s(deep,shape,{.15,0,0},{},.5,cfg,stranger);
        assert(!glitch.touching && !glitch.push && length(glitch.outward)==0);
        ContactInput table;table.lag=Vec3{0,0,.05};                // the table lifted the hand: vertical, trusted
        assert(hand_contact_s(lone({-.08,0,.1},.06),shape,{.3,0,0},{},.5,cfg,table).push);
        ContactInput known=stranger;known.lag_known=true;          // another prop's lag (the cast holds the hand at it, or the hand meets it): the drawn hand is read as drawn
        assert(hand_contact_s(lone({-.08,0,.1},.06),shape,{.3,0,0},{},.5,cfg,known).touching);
        ContactInput small;small.lag=Vec3{.008,0,0};               // under a centimetre: no matter
        assert(hand_contact_s(lone({-.08,0,.1},.06),shape,{.3,0,0},{},.5,cfg,small).push);
        std::cout<<"stranger's clamp: a 6 cm sideways offset (not held, not a prop's) reads nothing; the table's lift, a prop's lag and 8 mm still do\n";
    }
    // --- cost of the math: 8 bodies x 2 hands x 21 proxies per frame
    {
        const auto shape=prop_shape(bottle(0));const auto proxies=hand_proxies(synthetic_geometry(),palm_at({-.1,0,.1}),{0,0,0,0,0});
        const int frames=2000;volatile double sink=0;
        const auto t0=std::chrono::steady_clock::now();
        for(int f=0;f<frames;++f) for(int b=0;b<16;++b) sink=sink+hand_contact(proxies,shape,{.3,0,0},{},.5).depth;
        const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t0).count()/frames;
        std::cout<<"hand_contact math: "<<us<<" us per frame (16 hand-body pairs)\n";
        assert(us<200);
    }
    // --- the physics-thread service against synthetic engine memory
    {
        const std::uintptr_t base=0x140000000,game=0x1000000,world=0x5000000,pool=0x6000000,motions=0x9000000;
        std::map<std::uintptr_t,unsigned char> bytes;unsigned reads=0;
        std::map<unsigned,unsigned> entity_reads,position_reads; // per prop index: reads anywhere in its block, and of its +0x300 position
        const auto put=[&](std::uintptr_t at,const auto& value) {const auto* p=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(value);++i) bytes[at+i]=p[i];};
        const auto read=[&](std::uintptr_t at,void* out,std::size_t size) {
            ++reads;auto* p=static_cast<unsigned char*>(out);
            if(at>=0x2000000 && at<0x3000000) {++entity_reads[unsigned((at-0x2000000)>>20)];if(((at-0x2000000)&0xfffff)==0x300) ++position_reads[unsigned((at-0x2000000)>>20)];}
            for(std::size_t i=0;i<size;++i) {const auto it=bytes.find(at+i);if(it==bytes.end()) return false;p[i]=it->second;}return true;};
        std::array<std::uintptr_t,0x3fff> registry{};registry[0]=0x7000000;
        put(base+0x39a5290,game);put(game,base+0x1c940a8);put(game+0x60410,world);put(world+0x28,pool);put(world+0x30,std::uint32_t{16});put(world+0x120,motions);
        put(game+0x3130b0,registry);
        // index, handle, centre, bounds half extents, vtable, carryable, inverse-mass half
        struct Prop {unsigned index;double x,y,hx,hy,hz;std::uintptr_t vtable;bool carryable;std::uint16_t inverse_mass;};
        // Writes (or rewrites, to move or retype it) a prop's entity, components, model, body record and registry slot.
        const auto build=[&](const Prop& p) {
            const std::uintptr_t entity=0x2000000+p.index*0x100000,component=entity+0x10000,entries=entity+0x20000,model=entity+0x30000,pickable=entity+0x40000;
            const std::uint32_t id=(p.index<<16)|0x10,handle=0x1000000|p.index;const auto record=pool+p.index*0xb0;
            registry[p.index]=entity;put(game+0x3130b0+p.index*8ull,entity);
            put(entity+0xa8,id);put(entity,base+p.vtable);put(entity+0x300,std::array<float,3>{float(p.x),float(p.y),float(p.hz)});
            put(entity+0x268,rig::List{entries,p.carryable?2u:1u,2u<<8});
            put(entries,component);put(entries+8,pickable);put(pickable,base+0x1c5a3b8);
            put(component,base+(p.carryable?0x1b64b68:0x1c745c8));put(component+0x10,entity);put(component+0x18,rig::Identity{7,10});
            put(component+0xe8,base+(p.carryable?0x1b64d38:0x1c74798));put(component+0xf8,handle);put(component+0xc8,std::array<unsigned char,12>{}); // parent pointer, lease byte (+0xd2)
            put(component+0x438,std::uint32_t{0x40});
            put(entity+0xf0,model);put(model,base+0x1b0dd88);
            put(model+0x1840,std::array<float,8>{float(p.x-p.hx),float(p.y-p.hy),0,0,float(p.x+p.hx),float(p.y+p.hy),float(2*p.hz),0});
            put(model+0x1860,std::array<float,3>{float(p.x),float(p.y),float(p.hz)});
            put(record,std::array<unsigned char,0xb0>{}); // the whole 0xb0-byte record is readable
            rig::Transform t{};t.f[0]=t.f[5]=t.f[10]=t.f[15]=1;t.f[12]=float(p.x);t.f[13]=float(p.y);t.f[14]=float(p.hz);put(record,t);
            put(record+0x44,std::uint32_t{130});put(record+0x40,std::uint32_t{p.index});put(record+0x70,handle);put(record+0x74,handle);
            put(motions+p.index*0x80+0x40,std::array<float,3>{0,0,0});put(motions+p.index*0x80+0x26,p.inverse_mass);
        };
        for(const auto& p:std::array<Prop,3>{{{1,1.0,0,.035,.035,.14,0x1c5de18,true,0x4000},{2,1.0,.3,.5,.5,.5,0x1c886a8,false,0x3a00},{3,4.0,0,.035,.035,.14,0x1c886a8,false,0x4000}}}) build(p);
        // carryables are refused unless asked for; the pickable entry is fine when allowed
        assert(!native_rigid_body(read,base,0x2000000+0x100000));
        assert(native_rigid_body(read,base,0x2000000+0x100000,true));
        assert(native_rigid_body(read,base,0x2000000+0x200000));
        struct Written {std::uint32_t handle{};std::array<float,3> linear{};unsigned count{};float peak{};};
        std::map<std::uint32_t,Written> writes;
        const auto write=[&](std::uintptr_t w,std::uint32_t handle,const std::array<float,3>& linear,const std::array<float,3>& angular) {
            assert(w==world && angular[0]==0 && angular[1]==0 && angular[2]==0);
            auto& entry=writes[handle];entry.handle=handle;if(!entry.count) entry.linear=linear;++entry.count;entry.peak=std::max(entry.peak,linear[0]); /* the first push */
            put(motions+(handle&0xffffff)*0x80+0x40,linear); // the body now moves
        };
        NativeHandContact contact;contact.set_geometry(1,synthetic_geometry());
        contact.scan_limit_us=contact.service_limit_us=1<<30; // the byte map below costs microseconds a read: no time boxes
        assert(!contact.wanted());
        contact.publish(1,true,palm_at({0,0,0}),palm_at({0,0,0}),{0,0,0,0,0});assert(contact.wanted());
        contact.publish(1,false,palm_at({0,0,0}),palm_at({0,0,0}),{0,0,0,0,0});assert(!contact.wanted());
        std::array<std::uintptr_t,2> held{};
        // Palm-down right hand sweeping toward the bottle at 0.4 m/s from 0.4 m away.
        auto sweep=[&](bool active,std::uintptr_t held_entity,std::uintptr_t service_world) {
            writes.clear();held[1]=held_entity;
            for(int i=0;i<40;++i) {
                const double x=.70+.4*.011*i;const auto pose=palm_at({x,0,.1});
                contact.publish(1,active,pose,pose,{0,0,0,0,0});
                std::this_thread::sleep_for(std::chrono::milliseconds(11));
                contact.service(read,base,service_world,1000+i*11,held,write);
            }
        };
        sweep(true,0,world);
        assert(writes.count(0x1000001)); // the bottle (a native carryable) was pushed
        std::cout<<"bottle push "<<writes[0x1000001].linear[0]<<" "<<writes[0x1000001].linear[1]<<" "<<writes[0x1000001].linear[2]<<" count "<<writes[0x1000001].count<<std::endl;
        // a fresh contact is a ride: the first write adds at most write_step; the speed then builds up to the hand's
        assert(writes[0x1000001].linear[0]<=HandContactConfig{}.write_step+1e-6 && writes[0x1000001].peak>.2 && writes[0x1000001].peak<1. &&
               std::abs(writes[0x1000001].linear[1])<1e-4 && std::abs(writes[0x1000001].linear[2])<1e-4);
        assert(!writes.count(0x1000002) && !writes.count(0x1000003)); // 350 kg crate refused (mass), far prop never tested
        put(motions+1*0x80+0x40,std::array<float,3>{0,0,0});
        sweep(true,0x2000000+0x100000,world); // a prop held physically by a hand is the grab motor's
        assert(writes.empty());
        sweep(true,0,world+8); // the dispatcher's world is not the game's
        assert(writes.empty());
        sweep(false,0,world); // a closed or armed hand pushes nothing
        assert(writes.empty());
        contact.release(1);
        assert(contact.faults==0 && contact.pushes>0 && contact.touches>0 && contact.tested>0);
        // Cost of the engine reads: services in the steady state (hand resting by a bottle and a crate); the registry slice cycles
        // through the 16384 entries, so the average and the worst of 20 services are reported.
        unsigned worst=0,total=0;
        for(int i=0;i<20;++i) {
            contact.publish(1,true,palm_at({.7,0,.1}),palm_at({.7,0,.1}),{0,0,0,0,0});
            std::this_thread::sleep_for(std::chrono::milliseconds(11));
            reads=0;contact.service(read,base,world,5000+i*11,held,write);
            worst=std::max(worst,reads);total+=reads;
        }
        std::cout<<"service: "<<total/20.<<" memory reads per tick on average, "<<worst<<" at worst (a bottle and a crate by the hand)\n";
        assert(worst<400);
        // A cluttered table: eight more props in reach of the resting hand. Each costs a light refresh (8 reads) a service and a full
        // read (~45) once a second, not a full read a service.
        for(unsigned i=0;i<8;++i) build({8+i,.9+.07*(i%4),-.12+.24*(i/4),.03,.03,.1,0x1c886a8,false,0x4000});
        {
            const auto before_tested=contact.tested.load();unsigned busiest=0;double sum=0;std::uint64_t clock=20000;
            for(int i=0;i<300;++i) {
                contact.publish(1,true,palm_at({.7,0,.1}),palm_at({.7,0,.1}),{0,0,0,0,0});
                reads=0;contact.service(read,base,world,clock+=11,held,write);
                if(i>=40) {busiest=std::max(busiest,reads);sum+=reads;}
            }
            const double average=sum/260;
            std::cout<<"service: ten props in reach, "<<average<<" memory reads per tick on average ("<<average*.075<<" us at 75 ns), "<<busiest<<" at worst; "
                <<contact.tested.load()-before_tested<<" full reads in 300 ticks\n";
            assert(average<130 && busiest<300); // the worst tick: three full reads and the refreshes
            assert(contact.tested.load()-before_tested<10*(300*11/NativeHandContact::full_period_ms+2)); // a full read per prop per full_period, not per tick
        }

        // --- the whole service as the sim ran it (v502/v510: 0.35 m at 0.16 m/s moved the bottle 6 cm): the hand moves in real time, the
        // fake world integrates the bottle from the velocity the service writes (table friction), the record, position and bounds follow.
        for(unsigned i=0;i<8;++i) build({8+i,9.+i,5.,.03,.03,.1,0x1c886a8,false,0x4000}); // clear the table
        {
            double bx=1.0,bvx=0,last=0,hand_x=0;unsigned writes_seen=0;const double mu_g=.4*9.81,hx0=.67;
            auto place=[&](double x,double v) {
                build({1,x,0,.035,.035,.14,0x1c5de18,true,0x4000});put(motions+1*0x80+0x40,std::array<float,3>{float(v),0,0});};
            place(bx,0);held={};
            NativeHandContact solid;solid.set_geometry(1,synthetic_geometry());solid.scan_limit_us=solid.service_limit_us=1<<30;
            const auto start=std::chrono::steady_clock::now();
            for(;;) {
                const double now=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                if(now>3.8) break;
                hand_x=hx0+std::min(.35,.16*std::max(0.,now-.3));
                const auto pose=palm_at({hand_x,0,.1});
                solid.publish(1,true,pose,pose,{0,0,0,0,0});
                const auto before=writes.count(0x1000001)?writes[0x1000001].count:0u;
                solid.service(read,base,world,200000+static_cast<std::uint64_t>(now*1000),held,write);
                writes_seen+=(writes.count(0x1000001)?writes[0x1000001].count:0u)-before;
                // the world: velocity from memory (the service's write), friction, motion
                std::array<float,3> v{};read(motions+1*0x80+0x40,v.data(),12);bvx=v[0];
                const double dt=now-last;last=now;
                bvx=std::max(0.,bvx-mu_g*dt);bx+=bvx*dt; // the engine: friction first, then the move
                place(bx,bvx);
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }
            const double face=bx-.035,front=hand_x+.15;
            std::cout<<"service carry: hand travelled 0.35 m, the bottle's back face ends "<<(face-front)*100<<" cm from the fingertips ("<<(bx-1.0)*100<<" cm moved), "<<writes_seen<<" writes\n";
            assert(std::abs(face-front)<.03 && bx-1.0>.15); // carried the whole way, not 6 cm
            assert(writes_seen>20 && solid.faults==0);
        }
        // --- discovery off the service (the game's configuration): discover() scans from another thread's call, the service only handles what it was handed
        {
            NativeHandContact split;split.set_geometry(1,synthetic_geometry());split.scan_limit_us=split.service_limit_us=1<<30;split.inline_discovery=false;
            build({1,1.0,0,.035,.035,.14,0x1c5de18,true,0x4000});put(motions+1*0x80+0x40,std::array<float,3>{0,0,0});writes.clear();held={}; // the bottle back at x=1
            unsigned service_reads=0,discover_reads=0,service_peak=0,discover_peak=0;
            for(int i=0;i<60;++i) {
                const double x=.70+.4*.011*i;const auto pose=palm_at({x,0,.1});
                split.publish(1,true,pose,pose,{0,0,0,0,0});
                std::this_thread::sleep_for(std::chrono::milliseconds(11));
                reads=0;split.discover(read,base);discover_reads+=reads;discover_peak=std::max(discover_peak,reads);
                reads=0;split.service(read,base,world,50000+i*11,held,write);service_reads+=reads;service_peak=std::max(service_peak,reads);
            }
            std::cout<<"discovery apart: the scan "<<discover_reads/60.<<" reads a frame on average ("<<discover_peak<<" at worst), the service "<<service_reads/60.<<" ("<<service_peak<<" at worst), "
                     <<split.discoveries.load()<<" discoveries, the bottle pushed "<<(writes.count(0x1000001)?writes[0x1000001].count:0u)<<" times\n";
            assert(writes.count(0x1000001) && writes[0x1000001].count>5);   // found by discover(), pushed by the service
            assert(split.discoveries.load()>0 && split.faults==0 && split.tested>0);
            assert(service_peak<NativeHandContact::scan_budget && discover_peak<=NativeHandContact::scan_budget+120); // the service never reads the registry
        }
        // --- discovery apart, but nobody calls discover() (a miswired caller): after latch_services services the service scans for itself, the push still happens
        {
            NativeHandContact latch;latch.set_geometry(1,synthetic_geometry());latch.scan_limit_us=latch.service_limit_us=1<<30;latch.inline_discovery=false;
            build({1,1.0,0,.035,.035,.14,0x1c5de18,true,0x4000});put(motions+1*0x80+0x40,std::array<float,3>{0,0,0});writes.clear();held={};
            for(int i=0;i<230;++i) {
                const double x=i<170?.70:.70+.0044*(i-170);const auto pose=palm_at({x,0,.1});
                latch.publish(1,true,pose,pose,{0,0,0,0,0});
                std::this_thread::sleep_for(std::chrono::milliseconds(11));
                latch.service(read,base,world,70000+i*11,held,write);
            }
            std::cout<<"discovery never called: the service scanned after "<<NativeHandContact::latch_services<<" services and pushed the bottle "<<(writes.count(0x1000001)?writes[0x1000001].count:0u)<<" times\n";
            assert(latch.discoveries==0 && writes.count(0x1000001) && writes[0x1000001].count>3 && latch.faults==0);
        }
        // --- the same ride with the world cast holding the drawn hand 5 cm back along its motion (sim v525), the bottle the entity it hit: the tracked
        // hand carries it, so the face ends at the tracked fingertips (5 cm short of them if the drawn hand pushed)
        {
            double bx=1.0,bvx=0,last=0,hand_x=0;const double mu_g=.4*9.81,hx0=.67;
            auto place=[&](double x,double v) {
                build({1,x,0,.035,.035,.14,0x1c5de18,true,0x4000});put(motions+1*0x80+0x40,std::array<float,3>{float(v),0,0});};
            place(bx,0);held={};
            NativeHandContact solid;solid.set_geometry(1,synthetic_geometry());solid.scan_limit_us=solid.service_limit_us=1<<30;
            const auto start=std::chrono::steady_clock::now();
            for(;;) {
                const double now=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                if(now>3.8) break;
                hand_x=hx0+std::min(.35,.16*std::max(0.,now-.3));
                solid.publish(1,true,palm_at({hand_x-.05,0,.1}),palm_at({hand_x,0,.1}),{0,0,0,0,0},0x2000000+0x100000);
                solid.service(read,base,world,300000+static_cast<std::uint64_t>(now*1000),held,write);
                std::array<float,3> v{};read(motions+1*0x80+0x40,v.data(),12);bvx=v[0];
                const double dt=now-last;last=now;
                bvx=std::max(0.,bvx-mu_g*dt);bx+=bvx*dt;
                place(bx,bvx);
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }
            const double face=bx-.035,front=hand_x+.15;
            std::cout<<"service carry, cast holding the drawn hand 5 cm back: the face ends "<<(face-front)*100<<" cm from the tracked fingertips ("<<(bx-1.0)*100<<" cm moved), cast seen "<<solid.cast_seen.load()<<" held "<<solid.cast_held.load()<<"\n";
            assert(std::abs(face-front)<.03 && bx-1.0>.15 && solid.cast_seen>0 && solid.cast_held>0 && solid.faults==0);
        }
        // --- meet_latch_holds_while_clamped: a latched (pinned) prop stays the clamp's while the clamp holds the hand at it, though the way out shrinks
        // under meet_unlatch (the hand pushed past the prop's middle); without the hold it unlatches there as before
        {
            assert(NativeHandContact::next_meet_latch(true,.03,true));     // clamped at the prop: still latched
            assert(!NativeHandContact::next_meet_latch(true,.03,false));   // not clamped: under meet_unlatch, unlatched
            assert(NativeHandContact::next_meet_latch(false,.09,false));   // deeper than meet_latch: latches
            assert(NativeHandContact::next_meet_latch(true,.06,false));    // between meet_unlatch and meet_latch: stays latched
        }
        // --- compose_meet (fix round 2, I3): the clamp's depth latches only while its contact is the pushable prop's plane, freshly hit; a stale plane (the
        // bottle flew off) never holds the hand back. held_by and the exclusion entity follow the clamp's contact and the met prop.
        {
            const HandClamp idle;const WorldTrackedPose target{{.3,0,.1},{{{1,0,0},{0,1,0},{0,0,1}}}};
            ClampOutput out{};out.valid=true;out.state=ClampState::contact;out.drawn=target;out.drawn.position={.155,0,.1};out.shift={-.145,0,0}; // 9 cm past the shell
            out.contact=ClampHit{{.2,0,.1},{-1,0,0},5,0,false,true};
            NativeHandContact::MeetPublished pub{};pub.fresh=true;pub.entity=5;pub.pushable[0]=5;pub.pushable_count=1;
            NativeHandContact::MeetState st{};const double dt=1./90;
            out.contact_fresh=false;
            auto m=NativeHandContact::compose_meet(st,pub,idle,0,out,target,dt);
            assert(!st.latched && !m.clamp_holding && m.meeting && m.prop && *m.prop==5 && m.held_by==5 && m.exclude && *m.exclude==5); // a stale plane: no latch, the meet draws
            assert(std::abs(m.drawn.x-.3)<1e-12);
            out.contact_fresh=true;
            m=NativeHandContact::compose_meet(st,pub,idle,0,out,target,dt);
            assert(st.latched && m.new_latch && m.clamp_holding && !m.meeting && !m.prop && std::abs(m.drawn.x-.155)<1e-12);   // fresh: the clamp's depth latches
            m=NativeHandContact::compose_meet(st,pub,idle,0,out,target,dt,false);
            assert(st.latched && !m.new_latch);                             // still held at the fresh prop plane: stays latched (depth_latch off only drops the depth)
            out.contact_fresh=false;
            m=NativeHandContact::compose_meet(st,pub,idle,0,out,target,dt);
            // The plane went stale and the way out is shallow: unlatched. The meeting hand eases out of the clamp's 14.5 cm hold at release_speed (1.5 m/s),
            // it does not jump to the target in one frame (fix round 3: the 4-5 m/s swat threw it 20-40 cm forward).
            assert(!st.latched && m.meeting && std::abs(m.drawn.x-(.155+1.5*dt))<1e-9);
            for(int i=0;i<30;++i) m=NativeHandContact::compose_meet(st,pub,idle,0,out,target,dt);
            assert(std::abs(m.drawn.x-.3)<1e-12);                           // eased all the way: 14.5 cm at 1.67 cm a frame
            pub.pushable_count=0;pub.loose[0]=5;pub.loose_count=1;          // pinned: not pushable, loose
            m=NativeHandContact::compose_meet(st,pub,idle,0,out,target,dt);
            assert(!m.meeting && !m.prop && m.held_by==5 && m.exclude && *m.exclude==5 && std::abs(m.drawn.x-.155)<1e-12); // the clamp's hand; the wall behind still swept for
            out.contact=ClampHit{{.2,0,.1},{-1,0,0},0,0,false,true};pub.fresh=true;pub.outward={-.01,0,0};pub.pushable[0]=5;pub.pushable_count=1;
            m=NativeHandContact::compose_meet(st,pub,idle,0,out,target,dt);
            assert(m.meeting && !m.prop && m.held_by==0 && m.exclude && *m.exclude==5);   // held at a wall (entity 0) while the shift meets prop 5
            NativeHandContact fresh_contact;                                 // meet_clamp on a contact with no published hand: the clamp's hand as it is
            const auto none=fresh_contact.meet_clamp(1,idle,out,target);
            assert(!none.meeting && std::abs(none.drawn.x-.155)<1e-12);
            ClampInput in{};fresh_contact.fill_clamp_input(1,in,std::uintptr_t{5});
            assert(in.loose_count==0 && !in.exclude);                         // nothing served: no loose props, nothing to exclude
        }
        // --- meet(): the cast must not hold the hand off a light loose prop the contact pushes; the way out of an overlap; pinned (deep) and heavy props and a hand
        // that is not pushing keep the hold-off
        {
            const std::uintptr_t bottle_entity=0x2000000+0x100000,crate_entity=0x2000000+0x200000;
            build({1,1.0,0,.035,.035,.14,0x1c5de18,true,0x4000});put(motions+1*0x80+0x40,std::array<float,3>{0,0,0});held={};writes.clear();
            NativeHandContact m;m.set_geometry(1,synthetic_geometry());m.scan_limit_us=m.service_limit_us=1<<30;
            std::uint64_t at=80000;
            const auto tick=[&](double x) {
                const auto pose=palm_at({x,0,.1});m.publish(1,true,pose,pose,{0,0,0,0,0});
                std::this_thread::sleep_for(std::chrono::milliseconds(11));m.service(read,base,world,at+=11,held,write);};
            assert(!m.meet(1,bottle_entity).prop);                          // nothing known yet
            for(int i=0;i<150;++i) tick(.60+.15*.011*i);
            const auto meeting=m.meet(1,bottle_entity);
            assert(writes.count(0x1000001));
            assert(meeting.prop && length(meeting.shift)<.1 && meeting.shift.x<0);            // met: the cast lets go of it, the way out is back along the push (this fake world never moves the bottle: the overlap grows)
            {   // the clamp's depth latches too: the tracked grip 9 cm past the clamp's shell at the prop, though the contact's way out is shallow (a toppled bottle)
                assert(length(meeting.shift)<NativeHandContact::meet_unlatch);
                const auto latches=m.meet_latches.load();
                const auto deep=m.meet(1,bottle_entity,true,.09);
                assert(!deep.prop && length(deep.shift)==0 && m.meet_latches==latches+1);
                assert(!m.meet(1,bottle_entity,true,0).prop);                   // still held at the prop: stays latched
                assert(m.meet(1,bottle_entity,false,.09).prop);                 // the clamp's depth counts only while it holds the hand at the prop: unlatched
            }
            assert(!m.meet(1,crate_entity).prop && !m.meet(1,0).prop);      // the heavy crate and an empty cast keep the hold-off
            assert(!m.meet(0,bottle_entity).prop && length(m.meet(0,bottle_entity).shift)==0); // the other hand is not published: its cast holds it
            build({1,1.0,0,.08,.08,.14,0x1c5de18,true,0x4000});put(motions+1*0x80+0x40,std::array<float,3>{0,0,0});
            for(int i=0;i<40;++i) tick(1.0);
            const auto pinned=m.meet(1,bottle_entity);
            std::cout<<"meet: pushing, the cast lets go ("<<length(meeting.shift)*100<<" cm out of the prop); deep in it: prop "<<pinned.prop<<", latches "<<m.meet_latches.load()<<"\n";
            assert(!pinned.prop && length(pinned.shift)==0 && m.meet_latches>0);
            for(int i=0;i<40;++i) tick(.70);
            assert(m.meet(1,bottle_entity).prop);                           // backed out: met again
            std::this_thread::sleep_for(std::chrono::milliseconds(200));    // the contact stopped serving: nothing is met on old data
            assert(!m.meet(1,bottle_entity).prop && m.faults==0);
        }
        // --- the player's keep-out zone: a prop against the player's body or under his feet is never pushed, one a little way off is
        {
            NativeHandContact keep;keep.set_geometry(1,synthetic_geometry());keep.scan_limit_us=keep.service_limit_us=1<<30;
            std::uint64_t at=90000;
            const auto go=[&](Vec3 eye,int ticks) { // the hand sweeps into the bottle at 0.15 m/s, the player at `eye`
                build({1,1.0,0,.035,.035,.14,0x1c5de18,true,0x4000});put(motions+1*0x80+0x40,std::array<float,3>{0,0,0});writes.clear();held={};
                for(int i=0;i<ticks;++i) {
                    const auto pose=palm_at({.70+.15*.011*i,0,.1});keep.publish(1,true,pose,pose,{0,0,0,0,0});keep.set_player(eye);
                    std::this_thread::sleep_for(std::chrono::milliseconds(11));keep.service(read,base,world,at+=11,held,write);}};
            go({.50,0,1.4},100);                                      // 46 cm from the bottle's edge: outside the 40 cm cylinder, pushed (the big crate is kept out: he is inside it)
            assert(writes.count(0x1000001));
            const auto crate_only=keep.keepouts.load();
            go({.80,0,1.4},100);                                      // 16 cm from it, body height: against the player, never written
            assert(!writes.count(0x1000001) && keep.keepouts>=crate_only+150);
            const auto against=keep.keepouts.load();
            go({1.0,0,1.6},100);                                      // the player stands over it: never written
            std::cout<<"keep-out: a bottle 46 cm from the player is pushed; against him or under him it is skipped ("<<keep.keepouts.load()-against<<" ticks the last time, the crate counted too)\n";
            assert(!writes.count(0x1000001) && keep.keepouts>=against+150);
        }
        // --- the slot cache: props come and go, a slot is reused, the hand jumps. Hand rests at `home`; the observable is `tested`
        // (a prop was known and read in full) and the reads of a prop's block.
        std::uint64_t tick=100000;held={};
        const auto rest=[&](Vec3 at,int n) {
            for(int i=0;i<n;++i) {const auto pose=palm_at(at);contact.publish(1,true,pose,pose,{0,0,0,0,0});contact.service(read,base,world,tick+=11,held,write);}};
        const auto services_until_tested=[&](Vec3 at,int limit) { // how many services until some prop is tested again, or -1
            const auto before=contact.tested.load();
            for(int i=1;i<=limit;++i) {rest(at,1);if(contact.tested>before) return i;}
            return -1;};
        const Prop walker{4,6,6,.035,.035,.14,0x1c886a8,false,0x4000};
        const auto walker_at=[&](double x,double y) {auto p=walker;p.x=x;p.y=y;build(p);};
        const Vec3 home{-2,0,.1};
        const unsigned pass=2*((0x3fff+1023)/1024); // services for one pass over the registry by a resting hand (it scans every other service)
        walker_at(6,6);rest(home,40);
        {const auto idle=contact.tested.load();rest(home,40);assert(contact.tested==idle);} // nothing within reach of the hand
        // Entering: the prop steps up beside the hand and is known within one pass.
        walker_at(-1.7,0);
        const int entered=services_until_tested(home,pass+4);std::cout<<"cache: a prop entering range was tested after "<<entered<<" services\n";assert(entered>0);
        // Leaving: it goes; the next service drops it and passes read its position about once each, not every service.
        walker_at(6,6);rest(home,3);
        {const auto tested=contact.tested.load();const auto before=position_reads[4];rest(home,2*pass);assert(contact.tested==tested && position_reads[4]-before<=4);}
        // A removed entity id: the registry slot empties (the memory stays readable); once its slot is passed nothing reads it.
        walker_at(-1.7,0);assert(services_until_tested(home,pass+4)>0);
        put(game+0x3130b0+4*8ull,std::uintptr_t{0});rest(home,pass+2);
        {const auto tested=contact.tested.load();const auto before=entity_reads[4];rest(home,2*pass);assert(contact.tested==tested && entity_reads[4]==before);}
        walker_at(-1.7,0);assert(services_until_tested(home,pass+4)>0); // the slot is reused by an entity at the same address
        // A hand jump (snap turn, teleport): the props at the old spot are dropped by the first service, those at the new one found within a pass.
        const Prop faraway{6,30.3,0,.035,.035,.14,0x1c886a8,false,0x4000};build(faraway);
        {const auto before=entity_reads[4];
         const int jumped=services_until_tested({30,0,.1},pass+4);std::cout<<"cache: a prop at the hand's new spot was tested after "<<jumped<<" services\n";assert(jumped>0);
         rest({30,0,.1},2*pass);assert(entity_reads[4]-before<=8);}
        // The cache keys on the entity pointer, not the class forever: an address reused by a prop is found within recheck_cycles passes.
        walker_at(6,6);build({7,-2,.3,.035,.035,.14,0x1c5e258,false,0x4000}); // unverified pickup class beside the hand (arkNPC is a candidate since Task 7)
        {const auto tested=contact.tested.load();rest(home,60);assert(contact.tested==tested && position_reads[7]==0);} // never a candidate, never position-read
        put(0x2000000+7*0x100000ull,base+0x1c886a8);
        int recheck=0;{const auto before=contact.tested.load();while(contact.tested==before && recheck<(int(NativeHandContact::recheck_cycles)+2)*int(pass)) {rest(home,1);++recheck;}
            std::cout<<"cache: an entity retyped in place was picked up after "<<recheck<<" services\n";assert(contact.tested>before);}
        contact.release(1);assert(contact.faults==0);
    }
    // --- cost of the registry scan on flat synthetic memory: a full registry (16382 entities, one in sixteen a prop), a hand resting
    // among them and then swinging. read_self costs about 75 ns a call (v502 sim: 75 us for the ~1030 reads of one slice).
    {
        const std::uintptr_t base=0x140000000,game=0x1000000,world=0x5000000,entities=0x20000000;constexpr unsigned stride=0x400;
        Arena mem;mem.add(base+0x39a5290,16);mem.add(game,0x340000);mem.add(entities,std::size_t(0x4000)*stride);
        mem.put(base+0x39a5290,game);mem.put(game,base+0x1c940a8);mem.put(game+0x60410,world);
        unsigned props=0;
        for(unsigned slot=1;slot<0x3fff;++slot) {
            const std::uintptr_t entity=entities+slot*stride;const bool prop=slot%16==0;props+=prop;
            mem.put(game+0x3130b0+slot*8ull,entity);mem.put(entity,base+(prop?0x1c886a8:0x1c5e258)); // the rest: a class that is never a candidate
            mem.put(entity+0x300,std::array<float,3>{prop?30.f+slot*.01f:50.f,0,.14f});
        }
        for(const unsigned slot:{5008u,9008u,14000u}) mem.put(entities+slot*stride+0x300,std::array<float,3>{.3f,0,.14f}); // three props at the hand, bodiless: refused
        NativeHandContact contact;contact.set_geometry(1,synthetic_geometry());
        const std::array<std::uintptr_t,2> held{};const auto write=[](std::uintptr_t,std::uint32_t,const std::array<float,3>&,const std::array<float,3>&) {};
        unsigned cold=0,worst_reads=0;double total_reads=0,total_us=0,worst_us=0;int warm=0;std::uint64_t tick=1000;std::uintptr_t live=world;
        const auto serve=[&](Vec3 at) {
            const auto pose=palm_at(at);contact.publish(1,true,pose,pose,{0,0,0,0,0});
            mem.reads=0;const auto t0=std::chrono::steady_clock::now();contact.service(mem,base,live,tick+=11,held,write);
            return std::make_pair(mem.reads,std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t0).count());
        };
        for(int i=0;i<600;++i) { // the first pass classifies every entity, a budget's worth a service
            const auto [reads,us]=serve({0,0,.1});
            if(reads>200) cold=i+1;
            if(i>=200) {worst_reads=std::max(worst_reads,reads);total_reads+=reads;total_us+=us;worst_us=std::max(worst_us,us);++warm;}
            assert(reads<=NativeHandContact::scan_budget+120);
        }
        std::cout<<"scan: cold pass "<<cold<<" services ("<<props<<" props); warm "<<total_reads/warm<<" reads a service on average, "<<worst_reads<<" at worst; "
            <<total_us/warm<<" us average, "<<worst_us<<" us worst on flat memory (~"<<total_reads/warm*.075<<" us average, ~"<<worst_reads*.075<<" us worst at 75 ns a read)\n";
        assert(cold<=(16382+props)/NativeHandContact::scan_budget+2*((0x3fff+1023)/1024)); // a few dozen services, once per level
        assert(total_reads/warm<130 && worst_reads<NativeHandContact::scan_budget+120 && total_us/warm<20);
        // A swinging hand scans four slices a service; the budget holds all the same.
        double swing_reads=0;unsigned swing_worst=0;Vec3 at{0,0,.1};
        for(int i=0;i<100;++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));at.x+=.03;
            const auto [reads,us]=serve(at);(void)us;swing_worst=std::max(swing_worst,reads);swing_reads+=reads;
        }
        std::cout<<"scan: swinging hand "<<swing_reads/100<<" reads a service on average, "<<swing_worst<<" at worst\n";
        assert(swing_worst<=NativeHandContact::scan_budget+120 && contact.faults==0);
        // A new level (different world) is a new registry: the classes are read again from the start.
        live=world+0x1000;mem.put(game+0x60410,live);
        {const auto first=serve(at);assert(first.first>200 && first.first<=NativeHandContact::scan_budget+120);}
    }

    // --- Task 7 (C2): knocked-out and dead guards are pushable, awake and yanked ones are not
    {
        const std::uintptr_t base=0x140000000,game=0x1000000,world=0x5000000,pool=0x6000000,motions=0x9000000;
        const std::uintptr_t npc=0x2200000,list=0x2300000,physics=0x2400000,handles=0x2500000,model=0x2600000;
        const std::uint32_t npc_id=0x20002;
        std::map<std::uintptr_t,unsigned char> bytes;
        const auto put=[&](std::uintptr_t at,const auto& value) {const auto* p=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(value);++i) bytes[at+i]=p[i];};
        const auto read=[&](std::uintptr_t at,void* out,std::size_t size) {
            auto* p=static_cast<unsigned char*>(out);
            for(std::size_t i=0;i<size;++i) {const auto it=bytes.find(at+i);if(it==bytes.end()) return false;p[i]=it->second;}return true;};
        std::array<std::uintptr_t,0x3fff> registry{};registry[2]=npc;
        put(base+0x39a5290,game);put(game,base+0x1c940a8);put(game+0x60410,world);put(world+0x28,pool);put(world+0x30,std::uint32_t{20});put(world+0x120,motions);
        put(game+0x3130b0,registry);
        put(npc,base+0x1c5e108);put(npc+0xa8,npc_id);put(npc+0xf0,model);put(npc+0x300,std::array<float,3>{10.f,0.f,0.f});
        put(npc+0x268,rig::List{list,1,1<<8});put(list,physics);put(physics,base+0x1c54008);put(physics+0x18,rig::Identity{3,10});put(physics+0x100,handles);
        put(handles+0x28,handles+0x120);put(handles+0x30,std::uint32_t{17});
        put(physics+0x10,npc);put(physics+0xc8,std::array<unsigned char,12>{}); // the component's entity, native carry parent (+0xc8) and lease (+0xd2)
        const std::uintptr_t userdata=0x2800000,stranger=0x2900000,stranger_component=0x2a00000;
        put(userdata+8,physics);put(stranger+8,stranger_component);put(stranger_component+0x10,std::uintptr_t{0x2b00000});
        const auto bone_flags=[&](std::uint32_t flags) {for(std::uint32_t b=0;b<17;++b) put(pool+b*0xb0+0x44,flags);};
        for(std::uint32_t b=0;b<17;++b) { // bone b lies at x = b, so bone 10 (the right hand) is the one at x = 10
            const std::uint32_t h=0x1000000+b;put(handles+0x120+b*4,h);const auto rec=pool+b*0xb0;
            rig::Transform t{};t.f[0]=t.f[5]=t.f[10]=t.f[15]=1;t.f[12]=static_cast<float>(b);t.f[14]=.1f;
            put(rec,t);put(rec+0x40,std::uint32_t{5+b});put(rec+0x70,h);put(rec+0x74,h);put(rec+0x90,userdata);
            put(motions+(5+b)*0x80+0x40,std::array<float,3>{0,0,0});put(motions+(5+b)*0x80+0x26,std::uint16_t{0x4000});
        }
        bone_flags(0x82);
        struct Written {unsigned count{};std::array<float,3> first{};};
        std::map<std::uint32_t,Written> writes;
        const auto write=[&](std::uintptr_t w,std::uint32_t handle,const std::array<float,3>& linear,const std::array<float,3>&) {
            assert(w==world && std::isfinite(linear[0]) && std::isfinite(linear[1]) && std::isfinite(linear[2]));
            auto& e=writes[handle];if(!e.count) e.first=linear;++e.count;put(motions+(5+(handle&0xffffff))*0x80+0x40,linear);};
        const auto make=[&]() {auto c=std::make_unique<NativeHandContact>();c->set_geometry(1,synthetic_geometry());c->scan_limit_us=c->service_limit_us=1<<30;return c;};
        std::array<std::uintptr_t,2> held{};
        // The right hand sweeps toward bone 10 at 0.4 m/s; every tick is 11 ms. now_ms keeps running across scenarios.
        std::uint64_t now_ms=200000;bool carrying_body=false;
        const auto sweep=[&](NativeHandContact& c,int ticks,double from) {
            if(from<9.71) for(std::uint32_t b=0;b<17;++b) put(motions+(5+b)*0x80+0x40,std::array<float,3>{0,0,0}); // a fresh approach: the bodies at rest
            for(int i=0;i<ticks;++i) {
                const auto pose=palm_at({from+.4*.011*i,0,.1});c.publish(1,true,pose,pose,{0,0,0,0,0});
                c.set_carrying_body(carrying_body);
                std::this_thread::sleep_for(std::chrono::milliseconds(11));c.service(read,base,world,now_ms+=11,held,write);}
        };
        // push_unconscious_guard: dynamic bodies, unconscious. Only bone 10 (the body the palm is by) is written.
        {
            put(npc+0xd1,static_cast<unsigned char>(0x20));writes.clear();
            auto c=make();sweep(*c,100,9.70);
            assert(writes.size()==1 && writes.count(0x100000a) && writes[0x100000a].count>0);
            assert(writes[0x100000a].first[0]>0 && std::abs(writes[0x100000a].first[1])<1e-4 && std::abs(writes[0x100000a].first[2])<1e-4);
            assert(c->pushes>0 && c->faults==0);
            std::cout<<"push_unconscious_guard: "<<writes[0x100000a].count<<" writes to bone 10, first "<<writes[0x100000a].first[0]<<" m/s\n";
            // ragdoll_meetable: the guard is a pushable of the meet, like a prop, so the clamp exempts his planes
            assert(c->meet(1,npc).prop);
            // dead (0x08) is pushable too
            put(npc+0xd1,static_cast<unsigned char>(0x08));writes.clear();
            auto d=make();sweep(*d,100,9.70);assert(writes.count(0x100000a) && writes.size()==1 && d->meet(1,npc).prop);
        }
        // awake_guard_not_pushed: bodies follow the animation (keyframed), not ragdolling.
        {
            bone_flags(0xce);put(npc+0xd1,static_cast<unsigned char>(0));writes.clear();
            auto c=make();sweep(*c,30,9.70);
            assert(writes.empty() && c->pushes==0 && !c->meet(1,npc).prop);
            assert(NativeHandContactPublicationFixture::tracked(*c,npc) && NativeHandContactPublicationFixture::ignored(*c,npc,now_ms)); // skipped until ignore_until
            // unconscious state with keyframed bodies is refused too: the body class decides as well
            put(npc+0xd1,static_cast<unsigned char>(0x20));writes.clear();
            auto d=make();sweep(*d,30,9.70);assert(writes.empty());
            // dynamic bodies but neither dead nor unconscious: refused
            bone_flags(0x82);put(npc+0xd1,static_cast<unsigned char>(0));writes.clear();
            auto e=make();sweep(*e,30,9.70);assert(writes.empty() && e->pushes==0);
        }
        // yanked_guard_not_pushed: ragdolling alive (0x80), dynamic bodies. native_ragdoll_body takes him; the hands must not.
        {
            bone_flags(0x82);put(npc+0xd1,static_cast<unsigned char>(0x80));writes.clear();
            assert(native_ragdoll_body(read,base,npc,10));
            assert(!pushable_ragdoll_body(read,base,npc,Vec3{9.8,0,.1}));
            auto c=make();sweep(*c,40,9.70);
            assert(writes.empty() && c->pushes==0 && !c->meet(1,npc).prop);
        }
        // pushable_ragdoll_body: the nearest of all 17 bones, dynamic ones only, dead or unconscious only
        {
            bone_flags(0x82);put(npc+0xd1,static_cast<unsigned char>(0x20));
            auto b=pushable_ragdoll_body(read,base,npc,Vec3{10.05,0,.1});assert(b && b->bone==10 && b->handle==0x100000a);
            b=pushable_ragdoll_body(read,base,npc,Vec3{2.1,0,.1});assert(b && b->bone==2); // the chest: not a drag bone, but a push bone
            b=pushable_ragdoll_body(read,base,npc,Vec3{-3,0,.1});assert(b && b->bone==0);
            put(pool+10*0xb0+0x44,std::uint32_t{0xce});   // that one body keyframed: another bone answers
            b=pushable_ragdoll_body(read,base,npc,Vec3{10.05,0,.1});assert(b && b->bone!=10);
            bone_flags(0x82);
            put(npc+0xd1,static_cast<unsigned char>(0xa8));assert(pushable_ragdoll_body(read,base,npc,Vec3{10,0,.1}));    // dead, unconscious and ragdolling
            put(npc+0xd1,static_cast<unsigned char>(0x10));assert(!pushable_ragdoll_body(read,base,npc,Vec3{10,0,.1}));   // other bits only
            put(npc+0xd1,static_cast<unsigned char>(0));assert(!pushable_ragdoll_body(read,base,npc,Vec3{10,0,.1}));
            put(npc+0xd1,static_cast<unsigned char>(0x20));put(npc,base+0x1c886a8);
            assert(!pushable_ragdoll_body(read,base,npc,Vec3{10,0,.1}));                                                  // not a guard
            put(npc,base+0x1c5e108);
            assert(NativeHandContactPublicationFixture::prop_class(read,base,npc)==2);
            put(0x2700000,base+0x1c886a8);assert(NativeHandContactPublicationFixture::prop_class(read,base,0x2700000)==1);
            put(0x2700000,base+0x1c1111);assert(NativeHandContactPublicationFixture::prop_class(read,base,0x2700000)==0);
        }
        // the guard wakes (or is revived) while a hand is on him: the writes stop
        {
            bone_flags(0x82);put(npc+0xd1,static_cast<unsigned char>(0x20));writes.clear();
            auto c=make();sweep(*c,100,9.70);assert(writes.count(0x100000a));
            put(npc+0xd1,static_cast<unsigned char>(0));writes.clear();
            sweep(*c,60,9.70+.4*.011*100);
            assert(writes.empty());
        }
        // the keep-out: a guard lying against the player is never shoved
        {
            bone_flags(0x82);put(npc+0xd1,static_cast<unsigned char>(0x20));writes.clear();
            auto c=make();
            for(int i=0;i<60;++i) {
                const auto pose=palm_at({9.70+.4*.011*i,0,.1});c->publish(1,true,pose,pose,{0,0,0,0,0});c->set_player({9.9,0,1.4});
                std::this_thread::sleep_for(std::chrono::milliseconds(11));c->service(read,base,world,now_ms+=11,held,write);}
            assert(writes.empty() && c->keepouts>0);
        }
        // a hand's own grab (held) keeps the grab motor's body
        {
            bone_flags(0x82);put(npc+0xd1,static_cast<unsigned char>(0x20));writes.clear();
            auto c=make();held[1]=npc;sweep(*c,60,9.70);held={};assert(writes.empty());
        }

        // Fix round 1 --------------------------------------------------------------------------------------------------------------
        const auto reset_guard=[&]() {bone_flags(0x82);put(npc+0xd1,static_cast<unsigned char>(0x20));put(physics+0xc8,std::array<unsigned char,12>{});
            put(npc+0x300,std::array<float,3>{10.f,0.f,0.f});carrying_body=false;writes.clear();held={};
            for(std::uint32_t b=0;b<17;++b) {put(pool+b*0xb0+0x90,userdata);put(motions+(5+b)*0x80+0x26,std::uint16_t{0x4000});}};
        // (1) never push a carried or leased body: the player's shoulder carry, a parent on the component, a lease, an NPC dragging him
        {
            reset_guard();carrying_body=true;
            {auto c=make();sweep(*c,60,9.70);assert(writes.empty() && c->pushes==0);}      // the player carries a body natively: no guard is pushed
            reset_guard();put(physics+0xc8,std::uintptr_t{0x3000000});
            {auto c=make();sweep(*c,60,9.70);assert(writes.empty() && !pushable_ragdoll_body(read,base,npc,Vec3{10,0,.1}));}   // native carry parent
            reset_guard();put(physics+0xd2,static_cast<unsigned char>(1));
            {auto c=make();sweep(*c,60,9.70);assert(writes.empty() && !pushable_ragdoll_body(read,base,npc,Vec3{10,0,.1}));}   // a lease
            reset_guard();
            {auto c=make();sweep(*c,100,9.70);assert(writes.count(0x100000a));                                       // control: free, pushed
             put(physics+0xc8,std::uintptr_t{0x3000000});writes.clear();sweep(*c,40,9.70+.4*.011*100);assert(writes.empty());}  // carried mid-push: stops
            reset_guard();
        }
        // (2) per-candidate heavy limit: a 25 kg torso is pushed (slowly, F*dt/m), a 50 kg one refused, props keep 12 kg
        {
            const auto heavy=[&](std::uint32_t bone,double kg) { // the bone's inverse mass (hkHalf: the upper 16 bits of the float)
                const float inverse=static_cast<float>(1./kg);std::uint32_t bits;std::memcpy(&bits,&inverse,4);
                put(motions+(5+bone)*0x80+0x26,static_cast<std::uint16_t>(bits>>16));};
            reset_guard();put(npc+0x300,std::array<float,3>{2.f,0.f,0.f});heavy(2,25);     // the chest: 0.32 m cube, estimate 11.5 kg
            {auto c=make();sweep(*c,100,1.70);
             assert(writes.size()==1 && writes.count(0x1000002) && writes[0x1000002].count>0);
             assert(writes[0x1000002].first[0]>0 && writes[0x1000002].first[0]<=200./90/25+1e-3);
             std::cout<<"heavy torso: 25 kg chest, first write "<<writes[0x1000002].first[0]<<" m/s (cap "<<200./90/25<<")\n";
             assert(c->meet(1,npc).prop);}
            reset_guard();put(npc+0x300,std::array<float,3>{0.f,0.f,0.f});heavy(0,50);       // the pelvis: 50 kg is past heavy_kg_ragdoll
            {auto c=make();sweep(*c,100,-.30);assert(writes.empty() && !c->meet(1,npc).prop);}
            reset_guard();put(npc+0x300,std::array<float,3>{0.f,0.f,0.f});heavy(0,39);
            {auto c=make();sweep(*c,100,-.30);assert(writes.count(0x1000000));}
            assert(NativeHandContact::heavy_kg_ragdoll==40. && HandContactConfig{}.heavy_kg==12.);
            reset_guard();
        }
        // (3) ownership like inspect_hit_body: record+0x90 userdata -> component -> entity; a recycled handle of another entity is refused
        {
            reset_guard();
            auto b=pushable_ragdoll_body(read,base,npc,Vec3{10.05,0,.1});assert(b && b->bone==10);
            put(pool+10*0xb0+0x90,stranger);                                                  // bone 10's record now belongs to another entity
            b=pushable_ragdoll_body(read,base,npc,Vec3{10.05,0,.1});assert(b && b->bone!=10);
            {auto c=make();sweep(*c,60,9.70);assert(!writes.count(0x100000a));}
            put(pool+10*0xb0+0x90,std::uintptr_t{0});b=pushable_ragdoll_body(read,base,npc,Vec3{10.05,0,.1});assert(b && b->bone!=10);   // no userdata at all
            reset_guard();put(physics+0x10,std::uintptr_t{0x1234000});put(userdata+8,physics);
            assert(!pushable_ragdoll_body(read,base,npc,Vec3{10.05,0,.1}) || pushable_ragdoll_body(read,base,npc,Vec3{10.05,0,.1})->bone!=10);
            put(physics+0x10,npc);reset_guard();
        }
        // (4) a scan that reclassifies an address re-derives the candidate's kind
        {
            const unsigned pass=2*((0x3fff+1023)/1024);
            const auto settle=[&](NativeHandContact& c,double x) {for(int i=0;i<(int(NativeHandContact::recheck_cycles)+2)*int(pass);++i) {
                const auto pose=palm_at({x,0,.1});c.publish(1,true,pose,pose,{0,0,0,0,0});c.service(read,base,world,now_ms+=11,held,write);}};
            reset_guard();auto c=make();settle(*c,9.70);assert(NativeHandContactPublicationFixture::kind(*c,npc)==2);
            put(npc,base+0x1c886a8);settle(*c,9.70);assert(NativeHandContactPublicationFixture::kind(*c,npc)==1);   // guard -> prop
            put(npc,base+0x1c5e108);settle(*c,9.70);assert(NativeHandContactPublicationFixture::kind(*c,npc)==2);   // prop -> guard
            reset_guard();
        }
        // (5) a failing choose is throttled (100 ms), and a knocked-out guard is looked at again within 0.25 s
        {
            reset_guard();for(std::uint32_t b=0;b<17;++b) put(motions+(5+b)*0x80+0x26,std::uint16_t{0x4000});
            const auto world_array=world+0x120;put(world_array,std::uintptr_t{0x7f00000});    // no motion array: every body read fails after the choose
            auto c=make();sweep(*c,40,9.70);                                                  // 440 ms
            std::cout<<"throttle: "<<c->ragdoll_chooses.load()<<" chooses in 40 services\n";
            assert(c->ragdoll_chooses>0 && c->ragdoll_chooses<=6 && writes.empty());
            put(world_array,motions);reset_guard();
            // awake, then knocked out: pushable again within 0.25 s
            put(npc+0xd1,static_cast<unsigned char>(0));
            auto d=make();sweep(*d,30,9.70);assert(writes.empty());
            put(npc+0xd1,static_cast<unsigned char>(0x20));writes.clear();
            sweep(*d,40,9.70+.4*.011*30);                                                     // 440 ms of pushing: well past the 0.25 s window
            assert(writes.count(0x100000a));
            reset_guard();
        }
    }
    std::cout<<"hand_contact_check ok\n";
}
