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

void explicit_block_swing() {
    Sim simulator;simulator.f.pos={.55f,1.0f,-.2f};simulator.f.trigger=1;simulator.f.explicit_block=true;
    simulator.run(20);CHECK(simulator.any_rb && !simulator.any_rt);
    std::uint64_t fire_tick{},press_tick{},release_tick{},block_tick{};
    for(unsigned step=0;step<700;++step) {
        ++simulator.t;simulator.f.now_ms=simulator.t;
        simulator.f.vel=fire_tick?GV3{0,0,0}:GV3{4,0,0};
        if(!fire_tick)simulator.f.pos.x+=.004f;
        const auto decision=simulator.c.update(simulator.f);
        CHECK(!(decision.rt && decision.rb));
        if(decision.swing!=SwingKind::none) {CHECK(!fire_tick);fire_tick=simulator.t;CHECK(!decision.rt && !decision.rb);}
        if(fire_tick && decision.rt && !press_tick)press_tick=simulator.t;
        if(press_tick && !decision.rt && !release_tick)release_tick=simulator.t;
        if(fire_tick && simulator.t<fire_tick+45+350)CHECK(!decision.rb);
        if(fire_tick && decision.rb && !block_tick)block_tick=simulator.t;
    }
    CHECK(fire_tick && press_tick==fire_tick+45 && release_tick==press_tick+45 && block_tick==press_tick+350);
    CHECK(simulator.f.trigger==1 && simulator.f.explicit_block);
    simulator.f.explicit_block=false;simulator.f.now_ms=++simulator.t;
    CHECK(!simulator.c.update(simulator.f).rb);
}

void explicit_block_gates() {
    for(unsigned gate=0;gate<4;++gate) {
        Sim simulator;simulator.f.pos={.55f,1.0f,-.2f};simulator.f.trigger=1;simulator.f.explicit_block=true;
        if(gate==0)simulator.f.allowed=false;
        if(gate==1)simulator.f.tracked=false;
        if(gate==2)simulator.f.equipped=false;
        if(gate==3) {simulator.f.equipped=false;simulator.f.shadow_walk=true;}
        simulator.run(20);CHECK(!simulator.any_rb && !simulator.any_rt);
    }
    Sim simulator;simulator.f.pos={.55f,1.0f,-.2f};simulator.f.trigger=1;simulator.f.explicit_block=true;
    simulator.run(20);
    while(simulator.swing==SwingKind::none)simulator.run(1,{4,0,0});
    simulator.f.allowed=false;simulator.clear();simulator.run(1);CHECK(!simulator.any_rb && !simulator.any_rt);
    simulator.f.allowed=true;simulator.clear();simulator.run(50);CHECK(!simulator.any_rt && simulator.any_rb);
    simulator.f.choke=true;simulator.f.grip=0;simulator.clear();simulator.run(1);CHECK(!simulator.any_rb && !simulator.any_rt && !simulator.choke);
}

struct AttackSim {
    VrCombat combat;CombatFrame frame{};CombatDecision decision{};std::uint64_t now=1000;
    CombatTarget target{CombatTarget::Kind::ground,0x12340001};
    explicit AttackSim(const CombatTuning& tuning={}):combat(tuning) {
        frame.head_pos={0,1.7f,0};frame.head_fwd={0,0,-1};frame.head_right={1,0,0};
        frame.pos={.55f,1.0f,-.2f};frame.aim={0,0,-1};frame.tracked=frame.allowed=frame.equipped=true;
        frame.explicit_block=true;frame.trigger=1;
        for(unsigned tick=0;tick<200;++tick)step();
    }
    CombatDecision step(float speed=0) {
        frame.now_ms=++now;frame.pos.x+=speed*.001f;frame.vel={speed,0,0};decision=combat.update(frame);
        CHECK(!(decision.rt && decision.rb));return decision;
    }
    std::uint64_t recognize() {
        for(unsigned tick=0;tick<300;++tick)if(step(4).swing!=SwingKind::none)return now;
        std::printf("recognize failed shadow=%d now=%llu recognized=%llu\n",frame.shadow_walk,static_cast<unsigned long long>(now),static_cast<unsigned long long>(decision.attacks.recognized));
        CHECK(false);return 0;
    }
    std::uint64_t wait() {
        frame.remembered_target=target;const auto fire=recognize();
        CHECK(decision.attack_transition==AttackTransition::waiting && decision.attack_pending && !decision.rt && !decision.rb);
        CHECK(decision.attack_target==target && decision.attack_deadline==fire+500);return fire;
    }
};

void stealth_timeout_and_resume() {
    for(const auto kind:{CombatTarget::Kind::ground,CombatTarget::Kind::drop}) {
        AttackSim simulator;simulator.target.kind=kind;const auto fire=simulator.wait();
        unsigned rt_samples{};
        for(unsigned elapsed=1;elapsed<=1000;++elapsed) {
            const auto decision=simulator.step();
            if(elapsed<500)CHECK(!decision.rt && !decision.rb && decision.attack_deadline==fire+500);
            if(elapsed==500)CHECK(decision.attack_transition==AttackTransition::timeout_ordinary && decision.scheduled_press==fire+545);
            if(elapsed<545)CHECK(!decision.rt);
            if(decision.rt) {++rt_samples;CHECK(simulator.now>=fire+545 && simulator.now<fire+590);}
            if(decision.attack_transition==AttackTransition::dispatched)CHECK(decision.attack_target==simulator.target);
            if(elapsed<895)CHECK(!decision.rb);
            else CHECK(decision.rb);
        }
        CHECK(rt_samples==45 && simulator.decision.attacks.timeout_ordinary==1 && simulator.decision.attacks.dispatched==1);
    }
    for(const auto kind:{CombatTarget::Kind::ground,CombatTarget::Kind::drop}) {
        AttackSim simulator;simulator.target.kind=kind;simulator.wait();simulator.frame.current_target=simulator.target;
        while(!simulator.step().rt) {}
        CHECK(simulator.decision.attack_transition==AttackTransition::dispatched && simulator.decision.attack_target==simulator.target);
        CHECK(!simulator.decision.attack_pending && !simulator.decision.attack_deadline);
        CHECK(simulator.step().rt && !simulator.decision.attack_target);
    }
    for(const unsigned delay:{1u,499u}) {
        AttackSim simulator;const auto fire=simulator.wait();unsigned rt_samples{};
        for(unsigned elapsed=1;elapsed<delay+500;++elapsed) {
            if(elapsed==delay)simulator.frame.current_target=simulator.target;
            const auto decision=simulator.step();
            if(elapsed==delay)CHECK(decision.attack_transition==AttackTransition::resumed && decision.scheduled_press==fire+delay+45);
            if(elapsed<delay+45)CHECK(!decision.rt);
            if(decision.rt) {++rt_samples;CHECK(elapsed>=delay+45 && elapsed<delay+95);}
            if(elapsed<delay+395)CHECK(!decision.rb);
        }
        CHECK(rt_samples==50 && simulator.decision.attacks.resumed==1 && simulator.decision.attacks.timeout_ordinary==0 && simulator.decision.attacks.dispatched==1);
    }
}

void stealth_target_cancellation() {
    for(unsigned change=0;change<4;++change) {
        AttackSim simulator;const auto fire=simulator.wait();
        if(change==0) {auto replacement=simulator.target;++replacement.spawn;simulator.frame.current_target=replacement;}
        else {
            simulator.frame.current_target=simulator.target;simulator.step();
            while(simulator.now<fire+45)simulator.step();
            if(change==1)simulator.frame.current_target={};
            if(change==2) {simulator.frame.current_target=simulator.target;simulator.frame.current_target.kind=CombatTarget::Kind::drop;}
            if(change==3) {simulator.frame.current_target=simulator.target;++simulator.frame.current_target.spawn;}
        }
        const auto cancelled=simulator.step();CHECK(cancelled.attack_transition==AttackTransition::cancelled_target && !cancelled.rt && !cancelled.attack_pending && cancelled.rb);
        simulator.frame.current_target={};
        for(unsigned tick=0;tick<1000;++tick)CHECK(!simulator.step().rt);
        CHECK(simulator.decision.attacks.dispatched==0 && simulator.decision.attacks.cancelled_target==1);
    }
    AttackSim simulator;simulator.wait();auto replacement=simulator.target;replacement.kind=CombatTarget::Kind::drop;
    simulator.frame.remembered_target=replacement;CHECK(simulator.step().attack_transition==AttackTransition::cancelled_target);
}

void stealth_deadline_and_lifecycle() {
    AttackSim simulator;const auto first=simulator.wait();
    while(simulator.now<first+350)simulator.step();
    for(unsigned tick=0;tick<140 && simulator.decision.attacks.recognized<2;++tick)simulator.step(-4);
    CHECK(simulator.decision.attacks.recognized==2 && simulator.decision.attacks.coalesced==1 && simulator.now<first+500);
    CHECK(simulator.decision.attack_deadline==first+500);
    while(simulator.now<first+600)simulator.step();
    CHECK(simulator.decision.attacks.waiting==1 && simulator.decision.attacks.timeout_ordinary==1 && simulator.decision.attacks.dispatched==1);
    for(unsigned loss=0;loss<8;++loss) {
        AttackSim reset;reset.wait();
        if(loss==0 || loss==1)reset.frame.allowed=false;
        if(loss==2)reset.frame.tracked=false;
        if(loss==3)reset.frame.equipped=false;
        if(loss==4 || loss==5)reset.combat.reset();
        if(loss==6)reset.frame.shadow_walk=true;
        if(loss==7) {reset.frame.choke=true;reset.frame.grip=1;reset.frame.pos={.1f,1.55f,-.45f};}
        const auto cancelled=reset.step();CHECK(!cancelled.rt && !cancelled.attack_pending);
        CHECK(cancelled.attacks.cancelled_context==1);
        reset.frame.allowed=reset.frame.tracked=reset.frame.equipped=true;reset.frame.choke=false;reset.frame.grip=0;
        for(unsigned tick=0;tick<1000;++tick)CHECK(!reset.step().rt);
        CHECK(reset.decision.attacks.dispatched==0);
    }
    AttackSim shadow;shadow.frame.shadow_walk=true;shadow.frame.remembered_target=shadow.target;
    for(unsigned tick=0;tick<200;++tick)shadow.step();
    const auto fire=shadow.recognize();CHECK(fire && shadow.decision.attacks.waiting==0);
}

void stealth_dispatch_boundaries() {
    AttackSim late;const auto original=late.wait();
    while(late.now<original+499)late.step();
    late.frame.current_target=late.target;
    CHECK(late.step().attack_transition==AttackTransition::timeout_ordinary);
    CHECK(late.decision.scheduled_press==original+545 && late.decision.attacks.resumed==0);
    AttackSim committed;const auto start=committed.wait();committed.frame.current_target=committed.target;
    committed.step();while(committed.now<start+46)committed.step();CHECK(committed.decision.rt && committed.decision.attacks.dispatched==1);
    committed.frame.current_target={};CHECK(committed.step().rt && committed.decision.attacks.cancelled_target==0);
    AttackSim choke;const auto choke_start=choke.wait();choke.frame.current_target=choke.target;choke.step();
    while(choke.now<choke_start+45)choke.step();
    choke.frame.choke=true;choke.frame.grip=1;choke.frame.pos={.1f,1.55f,-.45f};
    const auto reserved=choke.step();CHECK(!reserved.rt && reserved.rb && reserved.attacks.dispatched==0 && reserved.attacks.cancelled_context==1);
    CombatTuning tuning;tuning.cooldown_ms=0;
    AttackSim spacing(tuning);spacing.recognize();
    while(!spacing.decision.rt)spacing.step();const auto first_press=spacing.now;
    for(unsigned tick=0;tick<60;++tick)spacing.step();
    spacing.frame.remembered_target=spacing.target;spacing.recognize();
    CHECK(spacing.decision.attack_transition==AttackTransition::waiting);
    spacing.frame.current_target=spacing.target;spacing.step();
    CHECK(spacing.decision.scheduled_press>=first_press+340);
    while(!spacing.decision.rt)spacing.step();
    CHECK(spacing.now>=first_press+340 && spacing.decision.attacks.dispatched==2);
}

void contact_presses_attack() {
    // a stealth contact presses the attack once even without a readable arc and without a flagged kill target; nothing without the contact
    Sim hit;hit.run(40);hit.f.contact_attack=true;hit.run(1);hit.f.contact_attack=false;hit.run(20);
    CHECK(!hit.f.kill && hit.swing==SwingKind::slash && hit.any_rt && hit.c.update(hit.f).attacks.recognized==1);
    Sim kill;kill.run(40);kill.f.kill=true;kill.f.contact_attack=true;kill.run(1);kill.f.contact_attack=false;kill.run(20);
    CHECK(kill.swing==SwingKind::slash && kill.any_rt && kill.c.update(kill.f).attacks.recognized==1);
    Sim none;none.run(40);none.f.kill=true;none.run(21);CHECK(!none.any_rt && none.swing==SwingKind::none);
    Sim idle;idle.run(61);CHECK(!idle.any_rt && idle.swing==SwingKind::none);
    // the native offered verdict drives the kill flag; the flickering sensor target only stands in when GlobalAssassination is unknown
    CHECK(kill_flag(true,true,false,false) && kill_flag(false,true,false,true) && !kill_flag(false,true,true,false));
    CHECK(kill_flag(false,false,true,false) && !kill_flag(false,false,false,false));
    bool steady=true;for(int frame=0;frame<10;++frame) steady&=kill_flag(true,true,frame%2==0,false); // sensor blinks, offered steady
    CHECK(steady);
}

int main() {
    contact_presses_attack();explicit_block_swing();explicit_block_gates();
    stealth_timeout_and_resume();stealth_target_cancellation();stealth_deadline_and_lifecycle();stealth_dispatch_boundaries();
    { Sim s;s.run(40);CHECK(!s.any_rt && s.swing==SwingKind::none); }                    // idle
    { Sim s;s.f.equipped=false;s.run(20);s.run(10,{4,0,0});s.run(8);CHECK(!s.any_rt && s.swing==SwingKind::none); }
    {
      Sim s;s.f.equipped=false;s.f.shadow_walk=true;s.run(30);CHECK(!s.any_rb && !s.any_rt);
      s.run(10,{4,0,0});s.run(8);CHECK(s.any_rt && s.swing==SwingKind::slash && !s.any_rb);
      s.clear();s.f.shadow_walk=false;s.run(1);CHECK(!s.any_rt && !s.any_rb); }
    { Sim s;s.f.shadow_walk=true;s.f.equipped=false;s.f.allowed=false;s.run(20);s.run(10,{4,0,0});
      CHECK(!s.any_rt && !s.any_rb); }
    { Sim s;s.f.shadow_walk=true;s.f.equipped=false;s.f.tracked=false;s.run(20);s.run(10,{4,0,0});
      CHECK(!s.any_rt && !s.any_rb); }
    for(bool entering:{false,true}) {
      Sim s;s.f.shadow_walk=!entering;s.f.equipped=entering;s.run(20);
      for(int i=0;i<15 && s.swing==SwingKind::none;++i) s.run(1,{4,0,0});
      CHECK(s.swing==SwingKind::slash);
      s.clear();s.f.shadow_walk=entering;s.f.equipped=!entering;s.run(1);
      CHECK(!s.any_rt && s.swing==SwingKind::none); }
    { Sim s;s.run(20);s.f.kill=true;s.run(8,{0,0,-3.5f});s.run(8);
      CHECK(s.swing==SwingKind::thrust && s.stealth && s.any_rt && s.hap>=.99f); }
    { Sim s;s.run(20);s.run(10,{0,0,-3.5f});s.run(8);CHECK(s.swing==SwingKind::thrust && !s.stealth && s.any_rt); } // plain thrust, no kill flag (RT after RB lets go)
    { Sim s;s.run(20);s.run(10,{4.f,0,0});s.run(8);CHECK(s.swing==SwingKind::slash && s.any_rt); }
    { Sim s;s.run(20);s.run(10,{0,-4.f,0});CHECK(s.swing==SwingKind::overhead); }
    { Sim s;s.run(20);s.run(60,{0,0,-2.5f},{0,0,-2.5f});CHECK(s.swing==SwingKind::none && !s.any_rt); } // fast walking: hand moves with head
    { Sim s;s.run(20);                                                                   // wrist jitter: tip whips, hand still
      for(int i=0;i<60;++i) {s.f.aim=norm({(i&1)?.5f:-.5f,0,-1});s.run(1);}
      CHECK(s.swing==SwingKind::none); }
    { Sim s;s.run(20);s.run(60,{0.4f,0,0});CHECK(s.swing==SwingKind::none); }             // slow drift
    { Sim s;s.run(20);s.run(10,{0,0,-1.9f});s.run(8);CHECK(s.swing==SwingKind::none && s.any_rb); } // a small push in guard keeps the block (headset v447)
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
      s.f.choke=true;s.f.kill=true;s.run(3);CHECK(s.choke && s.any_rb);
      s.f.choke=false;s.run(20);CHECK(s.choke);                                          // grace
      s.run(60);Sim t2;(void)t2; }
    { Sim s;s.f.pos={.1f,1.0f,-.45f};s.f.grip=1;s.f.choke=true;s.run(10);CHECK(!s.choke); } // hand too low
    { Sim s;s.run(5);s.f.aim={1,0,0};s.f.pos={0,1.55f,-.4f};s.f.kill=true;s.f.choke=true;s.run(30); // block pose by a finisher target
      CHECK(!s.any_rb && !s.choke); }
    { Sim s;s.run(20);s.f.kill=true;s.clear();s.run(1);CHECK(s.any_rb);s.f.kill=false;s.clear();s.run(1);CHECK(s.any_rb); }
    { Sim s;s.f.trigger=1;s.run(3);s.f.trigger=0;s.run(3);s.f.drop=true;s.run(8);        // attack held early, then drop qualifies
      CHECK(s.assist && s.any_rt); }
    { Sim s;s.run(3);s.f.drop=true;s.run(10);CHECK(!s.assist); }                          // no prior press: nothing
    { Sim s;s.f.trigger=1;s.run(3);s.f.trigger=0;s.run(80);s.f.drop=true;s.run(8);CHECK(!s.assist); } // stale press
    { Sim s;s.f.ev_block=true;s.run(1);CHECK(s.hap>=.79f); }
    for(const bool explicit_block:{false,true}) { // swings out of a held block: RB let go before a short RT pulse, no RB while the slash runs, presses spaced past the slash
      Sim s;s.run(5);s.f.aim={1,0,0};s.f.pos={-.3f,1.55f,-.4f};s.f.kill=true;s.run(30);CHECK(s.any_rb);
      s.f.explicit_block=explicit_block;s.f.trigger=explicit_block?1.f:0.f;
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
