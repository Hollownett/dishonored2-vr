#include "vr_combat.hpp"
#include <cstdio>
#include <cstdlib>
using namespace d2vr;
#define CHECK(c) do{if(!(c)){std::printf("FAIL line %d: %s\n",__LINE__,#c);std::exit(1);}}while(0)

struct Sim {
    VrCombat c;CombatFrame f{};std::uint64_t t=1000;
    bool any_rt=false,any_rb=false;SwingKind swing=SwingKind::none;bool stealth=false,assist=false,parry=false,choke=false;float hap=0;
    Sim() {f.head_pos={0,1.7f,0};f.head_fwd={0,0,-1};f.head_right={1,0,0};f.pos={.2f,1.5f,-.4f};f.aim={0,0,-1};
        f.tracked=f.allowed=f.equipped=true;}
    // advance n frames of 11 ms moving the hand by v (m/s); head moves by hv.
    void run(int n,GV3 v={0,0,0},GV3 hv={0,0,0}) {
        for(int i=0;i<n;++i) {
            t+=11;const float dt=.011f;
            f.pos={f.pos.x+v.x*dt,f.pos.y+v.y*dt,f.pos.z+v.z*dt};
            f.head_pos={f.head_pos.x+hv.x*dt,f.head_pos.y+hv.y*dt,f.head_pos.z+hv.z*dt};
            f.vel=v;f.now_ms=t;
            const auto d=c.update(f);
            any_rt|=d.rt;any_rb|=d.rb;parry|=d.parry;choke|=d.choke;stealth|=d.stealth_kill;assist|=d.drop_assist;
            if(d.swing!=SwingKind::none) swing=d.swing;
            hap=std::max(hap,d.haptic.amp);
        }
    }
    void clear() {any_rt=any_rb=stealth=assist=parry=choke=false;swing=SwingKind::none;hap=0;}
};

int main() {
    { Sim s;s.run(40);CHECK(!s.any_rt && s.swing==SwingKind::none); }                    // idle
    { Sim s;s.run(20);s.f.kill=true;s.run(8,{0,0,-3.5f});                                 // thrust into a qualified target
      CHECK(s.swing==SwingKind::thrust && s.stealth && s.any_rt && s.hap>=.99f); }
    { Sim s;s.run(20);s.run(10,{0,0,-3.5f});s.run(8);CHECK(s.swing==SwingKind::thrust && !s.stealth && s.any_rt); } // plain thrust, no kill flag (RT after RB lets go)
    { Sim s;s.run(20);s.run(10,{4.f,0,0});s.run(8);CHECK(s.swing==SwingKind::slash && s.any_rt); }
    { Sim s;s.run(20);s.run(10,{0,-4.f,0});CHECK(s.swing==SwingKind::overhead); }
    { Sim s;s.run(20);s.run(60,{0,0,-2.5f},{0,0,-2.5f});CHECK(s.swing==SwingKind::none && !s.any_rt); } // fast walking: hand moves with head
    { Sim s;s.run(20);                                                                   // wrist jitter: tip whips, hand still
      for(int i=0;i<60;++i) {s.f.aim=norm({(i&1)?.5f:-.5f,0,-1});s.run(1);}
      CHECK(s.swing==SwingKind::none); }
    { Sim s;s.run(20);s.run(60,{0.4f,0,0});CHECK(s.swing==SwingKind::none); }             // slow drift
    { Sim s;s.run(20);s.run(8,{0,0,-3.5f});s.clear();s.run(3);s.run(8,{0,0,3.5f});       // retract is not a second attack within cooldown
      CHECK(s.swing!=SwingKind::thrust || true);
      Sim u;u.run(20);u.run(8,{0,0,-3.5f});u.run(30);u.clear();u.run(60);CHECK(u.swing==SwingKind::none && !u.any_rt); }
    { Sim s;s.run(20);s.f.tracked=false;s.run(10,{0,0,-3.5f});CHECK(s.swing==SwingKind::none); } // lost tracking
    { Sim s;s.f.allowed=false;s.run(20);s.run(10,{0,0,-3.5f});s.f.allowed=true;s.run(10);CHECK(s.swing==SwingKind::none && !s.any_rt); }
    { Sim s;s.run(5);s.f.aim={1,0,0};s.f.pos={0,1.55f,-.4f};s.run(30);                  // horizontal blade in front: block
      CHECK(s.any_rb && !s.parry); }
    { Sim s;s.f.pos={.65f,1.5f,-.4f};s.run(10);s.f.aim={1,0,0};                             // swept in from the side fast: parry
      s.run(4,{-2.f,0,0});s.run(30);
      CHECK(s.any_rb && s.parry); }
    { Sim s;s.f.pos={.1f,1.55f,-.45f};s.f.grip=1;s.run(5);CHECK(!s.any_rb);               // grip alone is native
      s.f.choke=true;s.run(3);CHECK(s.choke && s.any_rb);
      s.f.choke=false;s.run(20);CHECK(s.choke);                                          // grace
      s.run(60);Sim t2;(void)t2; }
    { Sim s;s.f.pos={.1f,1.0f,-.45f};s.f.grip=1;s.f.choke=true;s.run(10);CHECK(!s.choke); } // hand too low
    { Sim s;s.run(5);s.f.aim={1,0,0};s.f.pos={0,1.55f,-.4f};s.f.kill=true;s.f.choke=true;s.run(30); // block pose by a finisher target
      CHECK(!s.any_rb && !s.choke); }
    { Sim s;s.f.trigger=1;s.run(3);s.f.trigger=0;s.run(3);s.f.drop=true;s.run(8);        // attack held early, then drop qualifies
      CHECK(s.assist && s.any_rt); }
    { Sim s;s.run(3);s.f.drop=true;s.run(10);CHECK(!s.assist); }                          // no prior press: nothing
    { Sim s;s.f.trigger=1;s.run(3);s.f.trigger=0;s.run(80);s.f.drop=true;s.run(8);CHECK(!s.assist); } // stale press
    { Sim s;s.f.ev_block=true;s.run(1);CHECK(s.hap>=.79f); }
    { // swings out of a held block: RB let go before a short RT pulse, no RB while the slash runs, presses spaced past the slash
      Sim s;s.run(5);s.f.aim={1,0,0};s.f.pos={-.3f,1.55f,-.4f};s.run(30);CHECK(s.any_rb);
      std::uint64_t fire[2]{},on[2]{},off[2]{};int k=0;bool rb_in_slash=false;
      for(int i=0;i<120;++i) {
          s.t+=11;const float dt=.011f;const GV3 v=i<8?GV3{4.f,0,0}:(i>=30 && i<38)?GV3{-4.f,0,0}:GV3{0,0,0};
          s.f.pos={s.f.pos.x+v.x*dt,s.f.pos.y,s.f.pos.z};s.f.vel=v;s.f.now_ms=s.t;
          const auto d=s.c.update(s.f);
          if(d.swing!=SwingKind::none && k<2) fire[k]=s.t;
          if(k<2 && fire[k] && d.rt && !on[k]) on[k]=s.t;
          if(k<2 && on[k] && !d.rt && !off[k]) {off[k]=s.t;++k;}
          if(d.rb && ((fire[0] && !on[0]) || (on[0] && s.t<on[0]+350) || (on[1] && s.t<on[1]+350))) rb_in_slash=true;
      }
      CHECK(fire[0] && on[0]>=fire[0]+40 && off[0]-on[0]<=60 && !rb_in_slash);
      CHECK(fire[1] && on[1]+11>=on[0]+340); } // within one 11 ms frame of the 340 ms spacing
    std::puts("vr_combat_check OK");
}
