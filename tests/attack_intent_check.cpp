#include "../src/attack_intent.hpp"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace d2vr;
static constexpr double dt=1./90.;
struct Rig {
    AttackIntent in;double t{};Vec3 hand{.3,-.2,-.4},head{0,0,0};
    // advance one sample; hv/headv are velocities in m/s
    bool step(Vec3 hv,Vec3 headv,bool crouched,bool drawn,Vec3 fwd={0,0,-1}) {
        t+=dt;hand=hand+hv*dt;head=head+headv*dt;
        return in.update({t,hand,head,true,fwd},crouched,drawn);
    }
    int run(int n,Vec3 hv,Vec3 headv,bool crouched,bool drawn,Vec3 fwd={0,0,-1}) {int p=0;for(int i=0;i<n;++i) p+=step(hv,headv,crouched,drawn,fwd);return p;}
    void settle() {run(10,{0,0,0},{0,0,0},false,true);}
};
static void slow_hand_no_press() {
    Rig r;r.settle();
    assert(r.run(90,{0,0,-2.5},{0,0,0},false,true)==0); // 2.5 m/s is under 3.0
    r.settle();
    assert(r.run(90,{0,0,-1.4},{0,0,0},true,true)==0);   // crouched, under the stab start speed
    assert(r.in.presses()==0);
}
static void slash_at_3mps_presses_once() {
    Rig r;r.settle();
    const int p=r.run(30,{3.4,0,0},{0,0,0},false,true);
    assert(p==1 && r.in.presses()==1 && r.in.slashes()==1 && r.in.stabs()==0);
    assert(r.run(10,{3.4,0,0},{0,0,0},false,true)==0); // still fast: no second press
}
static void slash_needs_weapon_drawn() {
    Rig r;r.settle();
    assert(r.run(30,{4,0,0},{0,0,0},false,false)==0);
}
static void rearm_needs_two_slow_samples_and_300ms() {
    Rig r;r.settle();
    assert(r.run(12,{4,0,0},{0,0,0},false,true)==1);
    // a single slow sample is not enough to re-arm (median of 3 stays fast)
    r.run(1,{0,0,0},{0,0,0},false,true);
    assert(r.run(60,{4,0,0},{0,0,0},false,true)==0);
    // genuinely slow for a while, then fast again after the cooldown
    r.run(40,{0,0,0},{0,0,0},false,true);
    assert(r.run(12,{4,0,0},{0,0,0},false,true)==1);
    // a fast swing that never slows below 1.0 m/s never re-arms, however long it lasts
    Rig q;q.settle();
    assert(q.run(12,{4,0,0},{0,0,0},false,true)==1);
    assert(q.run(180,{0.9+0.0,0,0},{0,0,0},false,true)==0); // 0.9 m/s is slow: arms
    Rig w;w.settle();
    assert(w.run(12,{4,0,0},{0,0,0},false,true)==1);
    assert(w.run(180,{1.2,0,0},{0,0,0},false,true)==0 && w.run(12,{4,0,0},{0,0,0},false,true)==0); // never below 1.0: stays disarmed
}
static void cooldown_is_300ms_after_slow() {
    Rig r;r.settle();
    assert(r.run(12,{4,0,0},{0,0,0},false,true)==1);
    // slow for 60 ms (< 300 ms) then fast: cooldown blocks
    r.run(6,{0,0,0},{0,0,0},false,true);
    assert(r.run(6,{4,0,0},{0,0,0},false,true)==0); // armed, fast, but ~200 ms after the press
    assert(r.run(12,{4,0,0},{0,0,0},false,true)==1); // the cooldown ends and the still-fast hand presses
}
static void head_motion_subtracted() {
    Rig r;r.settle();
    // walking forward at 4 m/s with a still hand (the hand moves with the head in the world: it is carried, so hv equals head velocity)
    assert(r.run(180,{0,0,-4},{0,0,-4},false,true)==0);
    // the head moves fast while the hand lags behind at rest in the world: the relative speed is high, and that is a real relative motion
    Rig s;s.settle();
    assert(s.run(30,{0,0,0},{4,0,0},false,true)==1); // hand left behind by 4 m/s reads as a fast relative motion
    // a hand swing at 4 m/s while the head also walks 4 m/s the same way: relative speed is 0
    Rig w;w.settle();
    assert(w.run(60,{4,0,0},{4,0,0},false,true)==0);
}
static void tracking_jump_ignored() {
    Rig r;r.settle();
    r.t+=dt;r.hand=r.hand+Vec3{1.0,0,0}; // 90 m/s in one sample
    assert(!r.in.update({r.t,r.hand,r.head,true,{0,0,-1}},false,true));
    assert(r.run(20,{0,0,0},{0,0,0},false,true)==0 && r.in.presses()==0);
    // a jump in the middle of a real swing is dropped, not counted, and does not block the swing from reading afterwards
    Rig q;q.settle();
    q.run(1,{0,0,0},{0,0,0},false,true);
    q.t+=dt;q.hand=q.hand+Vec3{0.6,0,0};
    assert(!q.in.update({q.t,q.hand,q.head,true,{0,0,-1}},false,true));
    assert(q.run(12,{4,0,0},{0,0,0},false,true)==1);
    // two jumps in a row would pass a median of 3 as a fast swing; each is a glitch and is dropped
    Rig j;j.settle();
    for(int i=0;i<2;++i) {j.t+=dt;j.hand=j.hand+Vec3{.3,0,0};assert(!j.in.update({j.t,j.hand,j.head,true,{0,0,-1}},false,true));}
    assert(j.run(3,{0,0,0},{0,0,0},false,true)==0 && j.in.presses()==0);
}
static void untracked_samples_reset() {
    Rig r;r.settle();
    r.run(3,{4,0,0},{0,0,0},false,true);
    assert(!r.in.update({r.t+dt,r.hand,r.head,false,{0,0,-1}},false,true));
    // after losing tracking it needs a fresh median again; a still hand does nothing
    assert(r.run(30,{0,0,0},{0,0,0},false,true)==0);
}
static void stab_only_when_crouched() {
    Rig r;r.settle();
    // a straight 2.0 m/s thrust along -z with the hand pointing -z
    assert(r.run(20,{0,0,-2.0},{0,0,0},false,true)==0); // standing: no stab
    Rig c;c.settle();
    assert(c.run(20,{0,0,-2.0},{0,0,0},true,true)==1 && c.in.stabs()==1 && c.in.slashes()==0);
}
static void stab_needs_travel_and_straightness() {
    // too short: 1.6 m/s for 0.1 s is only 0.16 m of travel
    Rig s;s.settle();
    assert(s.run(9,{0,0,-1.6},{0,0,0},true,true)==0);
    assert(s.run(60,{0,0,0},{0,0,0},true,true)==0);
    // curved path (a sweep): fast enough and far enough, but not straight; keep it under the 3 m/s slash speed
    Rig w;w.settle();
    int p=0;
    for(int i=0;i<40;++i) p+=w.step({2.0*std::cos(i*0.55),0,2.0*std::sin(i*0.55)},{0,0,0},true,true);
    assert(p==0);
    // a detour: two perpendicular 16 cm legs. The chord is long enough and aligned with the hand, but the path is not straight (0.71)
    Rig d;d.settle();
    const Vec3 diag{.7071,0,-.7071};
    assert(d.run(8,{0,0,-1.8},{0,0,0},true,true,diag)+d.run(8,{1.8,0,0},{0,0,0},true,true,diag)==0);
    // the same chord walked straight does press
    Rig e;e.settle();
    assert(e.run(16,{1.8*.7071,0,-1.8*.7071},{0,0,0},true,true,diag)==1);
    // misaligned: a straight sideways motion while the hand points forward
    Rig m;m.settle();
    assert(m.run(30,{2.0,0,0},{0,0,0},true,true)==0);
    // unknown hand axis can never be a stab
    Rig u;u.settle();
    assert(u.run(30,{0,0,-2},{0,0,0},true,true,{0,0,0})==0);
}
static void a_slash_speed_counts_as_slash_even_crouched() {
    Rig r;r.settle();
    assert(r.run(30,{3.6,0,0},{0,0,0},true,true)==1 && r.in.slashes()==1);
}
static void press_holds_120ms_then_until_two_polls_capped_at_500ms() {
    AttackPress p;
    assert(p.start(1000,10));
    assert(p.held(1060,13));            // inside 120 ms
    assert(p.held(1119,40));
    assert(!p.held(1120,40) && !p.active()); // 120 ms elapsed and more than two polls: released
    AttackPress q;q.start(1000,10);
    assert(q.held(1200,11));            // 200 ms but only one poll: keep holding
    assert(!q.held(1210,12));           // second poll: let go
    AttackPress c;c.start(1000,10);
    assert(c.held(1499,10));            // no poll at all: held to the cap
    assert(!c.held(1500,10));
    AttackPress d;d.start(1000,0);
    assert(!d.start(1050,0) && d.dropped()==1); // a press never restarts a running one
}
static void honour_counts_a_takedown_seen_within_600ms() {
    AttackHonour h;
    h.press(1000);h.observe(1300,true);
    assert(h.honoured()==1 && h.not_honoured()==0);
    h.press(2000);h.observe(2300,false);h.observe(2601,false);
    assert(h.honoured()==1 && h.not_honoured()==1);
    h.press(3000);h.observe(3700,true); // too late
    assert(h.honoured()==1 && h.not_honoured()==2);
}
// The routing guard: the new path only presses the native attack. It must not reach a kill, damage, or takedown function.
static void attack_intent_never_forces_kill() {
    const std::string root=std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path().parent_path().string();
    std::ifstream f(root+"/src/attack_intent.hpp");assert(f);
    const std::string header((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    for(const char* banned:{"NativeMelee","apply(","unaware_amount","TakeDamage","takedown(","kill(","request_melee_hit","stealth_hold","write_self","WriteProcessMemory"})
        assert(header.find(banned)==std::string::npos && banned);
    std::ifstream g(root+"/src/dxgi_probe.cpp");assert(g);
    const std::string probe((std::istreambuf_iterator<char>(g)),std::istreambuf_iterator<char>());
    const auto section=[&](const char* begin,const char* end) {
        const auto b=probe.find(begin);assert(b!=std::string::npos);
        const auto e=probe.find(end,b);assert(e!=std::string::npos);
        return probe.substr(b,e-b);
    };
    // the wiring may only set the right trigger (the pad composer's native attack), never call into damage or takedown functions
    const std::string compute=section("attack_intent_compute_begin","attack_intent_compute_end");
    const std::string press=section("attack_intent_press_tick","attack_intent_wiring_end");
    for(const char* banned:{"NativeMelee","request_melee_hit","request_sword_contact","unaware_amount","takedown_pulse","stealth_kill","input.b=","input.y=","contact_attack_tick"}) {
        assert(compute.find(banned)==std::string::npos);
        assert(press.find(banned)==std::string::npos);
    }
    assert(press.find("input.right_trigger=1")!=std::string::npos && press.find("attack_intent_rt")!=std::string::npos);
    // and the old contact_attack press is armed only behind the knob
    assert(probe.find("if(legacy_sword_contact && d2vr::arms_native_attack(")!=std::string::npos);
}
int main() {
    slow_hand_no_press();
    slash_at_3mps_presses_once();
    slash_needs_weapon_drawn();
    rearm_needs_two_slow_samples_and_300ms();
    cooldown_is_300ms_after_slow();
    head_motion_subtracted();
    tracking_jump_ignored();
    untracked_samples_reset();
    stab_only_when_crouched();
    stab_needs_travel_and_straightness();
    a_slash_speed_counts_as_slash_even_crouched();
    press_holds_120ms_then_until_two_polls_capped_at_500ms();
    honour_counts_a_takedown_seen_within_600ms();
    attack_intent_never_forces_kill();
    std::puts("PASS attack intent");
}
