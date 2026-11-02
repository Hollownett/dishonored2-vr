// Row A: a gun thrown into the world flies like the dropped sword (SwordFlight), the API contracts of NativeWeaponHands' gun copies, and grip offsets.
#define NOMINMAX
#include "../src/native_weapon_hands.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
namespace d2vr {
struct GunCopiesFixture {
    static void run() {
        using NWH=NativeWeaponHands;
        // flag, intern and request validation
        assert(!NWH::dual_wield());NWH::dual_wield(true);assert(NWH::dual_wield());
        {
            char name[]="models/weapons/gun_elite/elite_gun.def";
            NWH::Request r{};r.op=NWH::Op::gun_drop;r.decl=name;r.vel={1,2,3};
            assert(NWH::request(r));
            name[0]='X'; // the caller's string may die: the queued name is the interned copy
            const auto q=NWH::queue_.back();
            assert(q.decl && !std::strcmp(q.decl,"models/weapons/gun_elite/elite_gun.def") && q.decl!=name);
            NWH::Request r2=r;r2.decl="models/weapons/gun_elite/elite_gun.def";assert(NWH::request(r2) && NWH::queue_.back().decl==q.decl); // same string, same pointer
            NWH::Request nan{};nan.op=NWH::Op::gun_drop;nan.vel={NAN,0,0};assert(!NWH::request(nan));
            NWH::queue_.clear();
        }
        // held_copy wants: interned, nullptr clears
        NWH::held_copy(1,"models/weapons/gun_elite/elite_gun.def");
        assert(NWH::held_want_[1].load() && !std::strcmp(NWH::held_want_[1].load(),"models/weapons/gun_elite/elite_gun.def") && !NWH::held_want_[0].load());
        NWH::held_copy(1,nullptr);assert(!NWH::held_want_[1].load());
        NWH::held_copy(0,"");assert(!NWH::held_want_[0].load());
        assert(!NWH::held_copy_pose(0) && !NWH::gun_copy().present && !NWH::gun_copy_native().present);
        // grip offsets: a native twin's measured offset (slot 1, measured on the left hand = side 0), mirrored at the right hand; unknown defs get the constant
        {
            const char* gun=NWH::mirror_def_names[1];held_copy::Pose off{{.04,-.02,.05},{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}}};
            held_copy::Pose got{};bool mirrored{};
            const double pistol_x=held_copy::grip_constant(held_copy::Kind::pistol).offset.position.x;
            assert(!NWH::grip_offset(gun,0,got,mirrored) && !mirrored && got.position.x==pistol_x && pistol_x!=0); // nothing measured yet: the pistol constant (measured on the left)
            assert(!NWH::grip_offset(gun,1,got,mirrored) && mirrored);                                            // ... mirrored at the right hand
            NWH::meas_[1].locked=true;NWH::meas_[1].def=1;NWH::meas_[1].offset.value=off;
            assert(NWH::grip_offset(gun,0,got,mirrored) && !mirrored && got.position.y==-.02);
            assert(NWH::grip_offset(gun,1,got,mirrored) && mirrored);
            assert(!NWH::grip_offset("models/weapons/gun_elite/elite_gun.def",0,got,mirrored) && got.position.x==pistol_x); // another def: its kind's constant
            NWH::meas_[1]=HeldSlot{};
            NWH::mirror_slots_[0].locked=true;NWH::mirror_slots_[0].def=0;NWH::mirror_slots_[0].side=1;NWH::mirror_slots_[0].offset.value=off; // the sword, measured by the mirror on the right hand
            assert(NWH::grip_offset(NWH::mirror_def_names[0],1,got,mirrored) && !mirrored && NWH::grip_offset(NWH::mirror_def_names[0],0,got,mirrored) && mirrored);
            NWH::mirror_slots_[0]=HeldSlot{};
            NWH::meas_[1].locked=true;NWH::meas_[1].def=1;NWH::meas_[1].side=1;NWH::meas_[1].offset.value=off; // v758: the gun crossed to the right hand, measured there
            assert(NWH::grip_offset(gun,1,got,mirrored) && !mirrored && NWH::grip_offset(gun,0,got,mirrored) && mirrored);
            NWH::meas_[1]=HeldSlot{};
        }
        // status is JSON-shaped and carries no raw quotes from a def name
        NWH::held_failed=1;std::snprintf(NWH::held_last_failed_.data(),NWH::held_last_failed_.size(),"bad\"name\\x");
        const auto st=NWH::gun_status();
        assert(st.find("\"lying\":[null,null]")!=std::string::npos && st.find("bad?name?x")!=std::string::npos && st.front()=='{' && st.back()=='}');
        assert(st.find("\"stuns\":[0,0,0]}")!=std::string::npos); // v783: thrown gun, gun bash, thrown gadget stuns
        {int fault{};std::vector<std::uintptr_t> struck;auto no_read=[](std::uintptr_t,void*,std::size_t){assert(!"a slow throw reads nothing");return false;};
         assert(!NWH::thrown_stun(0x140000000ull,no_read,{},{1,0,0},{1,0,0},.1,2.9,0,0,struck,fault) && struck.empty());} // under 3 m/s: no stun
        // a lying gun is published with its pose and def, and the hip is told the gun is out
        {
            auto& g=NWH::lying_[1];g.present=true;g.native=true;g.kind=1;g.def=NWH::mirror_def_names[1];g.since=5;g.flight.pos={1,2,3};g.flight.axis={Vec3{0,1,0},Vec3{-1,0,0},Vec3{0,0,1}};
            NWH::gun_native_lying_=true;
            NWH::publish_guns();
            const auto c=NWH::gun_copy(1);
            assert(c.present && c.native && c.kind==1 && c.pos[2]==3 && c.axis[0]==0 && c.axis[1]==1 && c.since==5 && !NWH::gun_copy(0).present);
            assert(NWH::gun_copy_native().present && NWH::gun_status().find("\"lying\":[null,{")!=std::string::npos);
            NWH::guns_forget();NWH::publish_guns();assert(!NWH::gun_copy(1).present && !NWH::gun_native_lying_);
        }
        // flag off and nothing live: the service does nothing, even with no game at all
        NWH::dual_wield(false);
        auto read=[](std::uintptr_t,void*,std::size_t){return false;};
        NWH::service_guns(0x140000000ull,read);assert(!NWH::guns_live_);
        NWH::dual_wield(true);NWH::service_guns(0x140000000ull,read); // no game: forgets, calls nothing
        NWH::dual_wield(false);NWH::clear_copies();NWH::service_guns(0x140000000ull,read);
        assert(!NWH::clear_guns_req_.load());
    }
};
}
int main() {
    using namespace d2vr;
    // A floor at z=0 and a wall at x=3, as the native sphere sweep reports them.
    auto world=[](Vec3 a,Vec3 b)->std::optional<WorldTraceHit> {
        const double r=SwordFlight::radius;
        if(b.x>3-r && a.x<=3-r) {const double k=(3-r-a.x)/(b.x-a.x);WorldTraceHit h{};h.centre=a+(b-a)*k;h.normal={-1,0,0};h.fraction=k;return h;}
        if(b.z<r && a.z>=r) {const double k=(a.z-r)/(a.z-b.z);WorldTraceHit h{};h.centre=a+(b-a)*k;h.normal={0,0,1};h.fraction=k;return h;}
        return {};
    };
    {   // a pistol let go of at 1.1 m, barrel forward, lands on the floor within a second, upright (up axis = the floor normal), barrel in the floor plane
        SwordFlight f{};f.pos={0,0,1.1};f.axis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};f.flying=true;
        double t=0;while(f.step(.011,world) && t<3) t+=.011;
        assert(!f.flying && t>.4 && t<.7 && std::abs(f.axis[0].z)<1e-9 && f.axis[2].z==1 && std::abs(f.pos.z-(SwordFlight::radius+.01))<1e-6);
    }
    {   // a throw at 6 m/s lands further than a drop, tumbling; it stays orthonormal all the way
        SwordFlight f{};f.pos={0,0,1.4};f.vel={6,0,0};f.axis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};
        f.spin_axis={0,-1,0};f.spin=-9.6;f.flying=true;bool ok=true;double t=0;
        while(f.step(.011,world) && t<5) {t+=.011;for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) ok=ok && std::abs(dot(f.axis[i],f.axis[j])-(i==j?1.:0.))<1e-6;}
        assert(ok && !f.flying && f.pos.x>2.5 && f.pos.x<3 && f.axis[2].z==1);
    }
    {   // the lying gun is within reach of a hand next to it, and picking the nearer of two chooses the one in the hand
        SwordFlight a{};a.pos={0,0,1.1};a.axis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};a.flying=true;
        double t=0;while(a.step(.011,world) && t<3) t+=.011;
        const Vec3 pos[2]{a.pos,{2,0,0.05}};const bool present[2]{true,true};
        assert(held_copy::in_reach({.2,.1,.4},a.pos) && !held_copy::in_reach({1,0,1.3},a.pos));
        assert(held_copy::nearest(pos,present,2,{.1,0,.3})==0 && held_copy::nearest(pos,present,2,{1.9,0,.3})==1);
    }
    GunCopiesFixture::run();
    std::cout<<"gun_flight_check ok\n";
}
