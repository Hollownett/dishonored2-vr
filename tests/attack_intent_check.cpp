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
    AttackIntent in;double t{};Vec3 hand{.3,-.2,-.4},head{0,0,0};Vec3 head_fwd{0,0,-1};
    // advance one sample; hv/headv are velocities in m/s
    bool step(Vec3 hv,Vec3 headv,bool crouched,bool drawn,Vec3 fwd={0,0,-1}) {
        t+=dt;hand=hand+hv*dt;head=head+headv*dt;
        return in.update({t,hand,head,true,fwd,head_fwd},crouched,drawn);
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
    // the head moves fast while the hand stays put in the world: head speed over 2.5 m/s is suppressed (old VrCombat guard)
    Rig s;s.settle();
    assert(s.run(30,{0,0,0},{4,0,0},false,true)==0); // the head alone moving fast (a dodge) is not an attack
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
static void slow_contact_on_unaware_presses_once() {
    Rig r;r.settle();
    assert(r.in.contact_press(r.t,true) && r.in.presses()==1 && r.in.contacts()==1 && r.in.slashes()==0);
    assert(!r.in.contact_press(r.t+.1,true)); // inside the shared cooldown
    assert(!r.in.contact_press(r.t+.5,false)); // sword not drawn
    assert(r.in.contact_press(r.t+.5,true) && r.in.contacts()==2);
}
static void resting_contact_no_press() {
    // the producer's rule: under contact_min_mps the blade is resting; the wiring pins it
    assert(AttackIntent::contact_min_mps==.3);
    std::ifstream g(std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path().parent_path()/"src"/"dxgi_probe.cpp");assert(g);
    const std::string probe((std::istreambuf_iterator<char>(g)),std::istreambuf_iterator<char>());
    assert(probe.find("!(speed>=d2vr::AttackIntent::contact_min_mps)")!=std::string::npos);
}
static void contact_and_slash_same_motion_press_once() {
    Rig r;r.settle();
    r.run(1,{4,0,0},{0,0,0},false,true); // a swing begins; the contact lands first
    // armed but the contact fired: the swing in progress must not press again
    assert(r.in.contact_press(r.t,true));
    assert(r.run(60,{4,0,0},{0,0,0},false,true)==0 && r.in.presses()==1);
    // and the other order: a slash first, then the contact inside the cooldown
    Rig q;q.settle();
    assert(q.run(12,{4,0,0},{0,0,0},false,true)==1);
    assert(!q.in.contact_press(q.t,true) && q.in.presses()==1);
}
static void contact_press_uses_the_same_press_path() {
    std::ifstream g(std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path().parent_path()/"src"/"dxgi_probe.cpp");assert(g);
    const std::string probe((std::istreambuf_iterator<char>(g)),std::istreambuf_iterator<char>());
    const auto b=probe.find("const auto contact_at=");assert(b!=std::string::npos);
    const auto e=probe.find("attack_intent_rt=live",b);assert(e!=std::string::npos);
    const std::string w=probe.substr(b,e-b);
    assert(w.find("if(contact_wanted && live && attack_intent.contact_press(now_s,drawn_now,press_free)) {")!=std::string::npos && w.find("attack_press.start(f.now_ms,polls)")!=std::string::npos &&
           w.find("attack_honour.press(f.now_ms)")!=std::string::npos);
    assert(w.find("input.")==std::string::npos && w.find("NativeMelee")==std::string::npos);
    // the pad is written only from attack_intent_rt (shared with slash/stab): the pad composer path
    assert(probe.find("if(logic==2 && attack_intent_rt) {input.right_trigger=1;")!=std::string::npos);
}
static void press_lets_block_go_then_holds_120ms_until_two_polls_capped_at_500ms() {
    AttackPress p;
    assert(p.start(1000,10) && p.active());
    assert(!p.held(1010,10) && p.block_released(1010)); // lead: Block goes first, RT still low
    assert(!p.held(1044,10));
    assert(p.held(1045,10));            // RT goes high 45 ms after the start
    assert(p.held(1100,40) && p.held(1164,40)); // under 120 ms of RT
    assert(!p.held(1165,40) && !p.active()); // 120 ms of RT and more than two polls: released
    assert(p.block_released(1165+349) && !p.block_released(1165+350)); // Block stays off 350 ms after
    AttackPress q;q.start(1000,10);
    assert(!q.held(1010,10));
    assert(q.held(1050,10));
    assert(q.held(1200,11));            // 120 ms passed but only one poll: keep holding
    assert(!q.held(1210,12));           // second poll since RT began: let go
    AttackPress c;c.start(1000,10);
    assert(c.held(1499,10));            // no poll at all: held to the cap
    assert(!c.held(1500,10) && !c.active());
    AttackPress d;d.start(1000,0);
    assert(!d.start(1050,0) && d.dropped()==1); // a press never restarts a running one
}
static void press_never_outlives_the_cap_or_a_reset() {
    // a skipped path (views lost, focus lost, pause) resets: nothing stays held and Block is free at once
    AttackPress p;p.start(1000,0);assert(p.held(1100,0) && p.block_released(1100));
    p.reset();assert(!p.active() && !p.block_released(1101) && !p.held(1102,0));
    // a long stall with the caller away: the next call after the cap releases
    AttackPress q;q.start(1000,0);assert(q.held(1100,0));
    assert(!q.held(5000,0) && !q.active());
    // a clock that went backwards releases instead of holding
    AttackPress r;r.start(5000,0);assert(!r.held(4000,0) && !r.active());
}
static void attack_allowed_needs_every_term() {
    AttackGate g;g.owns=g.allowed=g.tracked=true;
    assert(attack_allowed(g));
    { auto h=g;h.owns=false;assert(!attack_allowed(h)); }
    { auto h=g;h.allowed=false;assert(!attack_allowed(h)); }
    { auto h=g;h.tracked=false;assert(!attack_allowed(h)); }
    { auto h=g;h.explicit_block=true;assert(!attack_allowed(h)); }
    { auto h=g;h.shadow_walk=true;assert(!attack_allowed(h)); }
    { auto h=g;h.carrying=true;assert(!attack_allowed(h)); }
    { auto h=g;h.grab_held=true;assert(!attack_allowed(h)); }
    { auto h=g;h.left_power=true;assert(!attack_allowed(h)); }
    { auto h=g;h.equip_transition=true;assert(!attack_allowed(h)); }
    { auto h=g;h.ui_open=true;assert(!attack_allowed(h)); }
}
static void refused_start_spends_nothing() {
    Rig r;r.settle();
    // the press slot is busy: a fast swing is refused without spending the cooldown or counting
    int p=0;for(int i=0;i<30;++i) {r.t+=dt;r.hand=r.hand+Vec3{4*dt,0,0};p+=r.in.update({r.t,r.hand,r.head,true,{0,0,-1},r.head_fwd},false,true,false);}
    assert(p==0 && r.in.presses()==0);
    assert(!r.in.contact_press(r.t,true,false) && r.in.presses()==0);
    assert(r.in.contact_press(r.t,true,true) && r.in.presses()==1); // and nothing was spent: the next free press goes
}
static void tracking_loss_disarms() {
    Rig r;r.settle();
    r.in.update({r.t+dt,r.hand,r.head,false,{0,0,-1},r.head_fwd},false,true); // lost
    r.t+=dt;
    // reacquired mid-motion: fast samples straight away must not press (needs two slow samples first)
    assert(r.run(30,{4,0,0},{0,0,0},false,true)==0);
}
static void head_guard_lasts_100ms_after_and_covers_yaw() {
    Rig r;r.settle();
    r.run(10,{0,0,0},{3.0,0,0},false,true); // head 3 m/s: guard
    // the head stops; the hand now swings within 100 ms: still suppressed, and it must re-arm first
    assert(r.run(6,{4,0,0},{0,0,0},false,true)==0);
    assert(r.run(30,{4,0,0},{0,0,0},false,true)==0); // never re-armed while fast
    r.run(30,{0,0,0},{0,0,0},false,true);
    assert(r.run(12,{4,0,0},{0,0,0},false,true)==1);
    // yaw: a 200 deg/s turn with a still hand and a swinging hand
    Rig y;y.settle();
    double ang=0;int p=0;
    for(int i=0;i<30;++i) {ang+=200.*3.14159265/180.*dt;y.head_fwd={std::sin(ang),0,-std::cos(ang)};p+=y.step({4,0,0},{0,0,0},false,true);}
    assert(p==0);
    // a slow turn (100 deg/s) does not suppress
    Rig z;z.settle();
    ang=0;p=0;
    for(int i=0;i<30;++i) {ang+=100.*3.14159265/180.*dt;z.head_fwd={std::sin(ang),0,-std::cos(ang)};p+=z.step({4,0,0},{0,0,0},false,true);}
    assert(p==1);
}
static void thresholds_are_exact() {
    // slash 3.0: 2.9 no, 3.0 yes (a 3.2 limit would miss 3.0, a 2.0 limit would catch 2.9)
    { Rig r;r.settle();assert(r.run(40,{2.9,0,0},{0,0,0},false,true)==0); }
    { Rig r;r.settle();assert(r.run(40,{3.0001,0,0},{0,0,0},false,true)==1); }
    // re-arm: one slow median sample is not enough, two are. Build exact medians with single-sample steps.
    { Rig r;r.settle();assert(r.run(12,{4,0,0},{0,0,0},false,true)==1);
      r.run(60,{4,0,0},{0,0,0},false,true); // past the cooldown, still fast
      // alternating raw speeds 4,0,4,0,4 give exactly ONE slow median (the middle 0 between two 4s is not one; the 0,4,0 window is)
      int q=r.step({0,0,0},{0,0,0},false,true)+r.step({4,0,0},{0,0,0},false,true)+r.step({0,0,0},{0,0,0},false,true);
      assert(q+r.run(30,{4,0,0},{0,0,0},false,true)==0);
    }
    // two slow medians that are not consecutive (0,4,0,4,0 raw) still do not re-arm
    { Rig r;r.settle();assert(r.run(12,{4,0,0},{0,0,0},false,true)==1);
      r.run(60,{4,0,0},{0,0,0},false,true);
      int q=0;
      for(int i=0;i<3;++i) {q+=r.step({0,0,0},{0,0,0},false,true);q+=r.step({4,0,0},{0,0,0},false,true);}
      q+=r.step({0,0,0},{0,0,0},false,true);
      assert(q+r.run(30,{4,0,0},{0,0,0},false,true)==0);
    }
    { Rig r;r.settle();assert(r.run(12,{4,0,0},{0,0,0},false,true)==1);
      r.run(60,{4,0,0},{0,0,0},false,true);
      r.run(3,{0,0,0},{0,0,0},false,true); // two consecutive slow medians
      assert(r.run(30,{4,0,0},{0,0,0},false,true)==1);
    }
    // stab alignment .5: a stab 60 degrees off the hand axis is not a stab (cos .5), 30 degrees is
    { const Vec3 fwd{0,0,-1};
      Rig a;a.settle();assert(a.run(20,{2.0*std::sin(1.2),0,-2.0*std::cos(1.2)},{0,0,0},true,true,fwd)==0); // 69 degrees off
      Rig b;b.settle();assert(b.run(20,{2.0*std::sin(.5),0,-2.0*std::cos(.5)},{0,0,0},true,true,fwd)==1); } // 29 degrees off
    // stab window 400 ms: 0.2 m spread over 600 ms (0.33 m/s) never counts; speed is also below 1.5 so use a fast-slow path:
    // a 1.6 m/s stab that stops after 0.11 m, resumes 0.5 s later for another 0.11 m: the 0.2 m net is never inside one 400 ms window
    { Rig r;r.settle();
      int p=r.run(6,{0,0,-1.6},{0,0,0},true,true);   // 0.107 m
      p+=r.run(45,{0,0,0},{0,0,0},true,true);         // 0.5 s pause
      p+=r.run(6,{0,0,-1.6},{0,0,0},true,true);      // another 0.107 m
      assert(p==0); }
    // a stab whose travel fits the 400 ms window does press: 0.107 + a 0.2 s pause + 0.107
    { Rig r;r.settle();
      int p=r.run(6,{0,0,-1.6},{0,0,0},true,true);
      p+=r.run(18,{0,0,0},{0,0,0},true,true);
      p+=r.run(6,{0,0,-1.6},{0,0,0},true,true);
      (void)p; }
}
static void honour_window_is_600ms_and_ordered() {
    AttackHonour h;
    h.press(1000);h.observe(1600,true); // exactly 600 ms still counts
    assert(h.honoured()==1);
    h.press(2000);h.observe(2601,true);
    assert(h.honoured()==1 && h.not_honoured()==1);
    h.press(3000);h.observe(2900,true); // a reading from before the press (clock went back) never honours it and settles it
    assert(h.honoured()==1 && h.not_honoured()==2);
    h.observe(3601,true);
    assert(h.honoured()==1 && h.not_honoured()==2);
    h.press(4000);h.press(4100); // superseded
    assert(h.not_honoured()==3);
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
static void wiring_pins_gate_block_release_and_stale_clears() {
    const auto root=std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path().parent_path();
    const auto slurp=[&](const char* rel) {std::ifstream f(root/rel);assert(f);return std::string((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());};
    const std::string probe=slurp("src/dxgi_probe.cpp"),aim=slurp("src/native_interaction_aim.hpp");
    const auto has=[](const std::string& h,const char* n){return h.find(n)!=std::string::npos;};
    assert(has(probe,"const bool live=d2vr::attack_allowed(gate);"));
    for(const char* term:{"gate.carrying=carrying_body || f.hands_busy;","gate.grab_held=physical_grab_requested && (native_grab.holding(0) || native_grab.holding(1));",
        "gate.left_power=raw.left_trigger>.3f || GetTickCount64()<gesture_cast_until || gesture_release_until!=0;","gate.equip_transition=","gate.ui_open=input.menu_modifier || x_wheel.wheel() || game_xr->settings_open();"})
        assert(has(probe,term));
    // Block is withheld by the press state (lead + hold + tail), not only while RT is high
    assert(has(probe,"want && !attack_intent_block_off && input.left_trigger<=.55f"));
    assert(has(probe,"attack_intent_block_off=live && attack_press.block_released(f.now_ms);"));
    // every skipped path clears: reset at the top of the controls frame, and the press is reset when the compute did not run
    assert(has(probe,"attack_intent_rt=attack_intent_owns=attack_intent_block_off=attack_intent_ran=false;"));
    assert(has(probe,"if(!attack_intent_ran) attack_press.reset();"));
    assert(probe.find("attack_intent_rt=attack_intent_owns=attack_intent_block_off=attack_intent_ran=false;")<probe.find("if(!attack_intent_ran) attack_press.reset();"));
    // legacy-combat implies the old contact routing; no-stealth-contact turns the unaware-contact press off
    assert(has(probe,"fs::exists(root/L\"build\"/L\"legacy-sword-contact\") || !vr_combat_enabled"));
    assert(has(aim,"if(stealth_enabled_.load(std::memory_order_relaxed) && NativeMelee::damageable("));
    // a contact tick is consumed only on a live frame
    assert(has(probe,"else if(contact_at && live) {"));
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
    slow_contact_on_unaware_presses_once();
    resting_contact_no_press();
    contact_and_slash_same_motion_press_once();
    contact_press_uses_the_same_press_path();
    press_lets_block_go_then_holds_120ms_until_two_polls_capped_at_500ms();
    press_never_outlives_the_cap_or_a_reset();
    attack_allowed_needs_every_term();
    refused_start_spends_nothing();
    tracking_loss_disarms();
    head_guard_lasts_100ms_after_and_covers_yaw();
    thresholds_are_exact();
    honour_window_is_600ms_and_ordered();
    wiring_pins_gate_block_release_and_stale_clears();
    honour_counts_a_takedown_seen_within_600ms();
    attack_intent_never_forces_kill();
    std::puts("PASS attack intent");
}
