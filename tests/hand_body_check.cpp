// Plan B task B11: the hand-body creation spike against a fake engine. B13: the production HandBody manager and its release gate against a
// fake engine and a fake game that stops dispatching while paused (B11 run 3).
#define D2VR_TESTING
#include <thread>
#include <chrono>
#include "hand_body_spike.hpp"
#include "hand_body.hpp"
#include "menu_tap.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
// A world with a floor at z = 0 and gravity; one physics step per tick (world() is called once per tick, first).
struct FakeWorld {
    std::uintptr_t world_id=0x1000;std::uint32_t next=0x01000042;
    bool alive=false;std::uint32_t handle=0;Vec3 pos{},vel{};double radius=.05;
    Vec3 player{0,0,0};
    int tick=0,busy_until=-1,world_change_at=-1,player_push_at=-1,fault_create=0,fault_push_at=-1,fault_destroy=0,refuse_destroys=0,unreadable_from=-1,unreadable_to=-1;
    bool create_fault_leaves_body=false;
    bool sleeps=false,asleep=false;   // a body created asleep does not move until a velocity write wakes it (createBody activation forced to 0)
    bool busy_now=false;
    int creates=0,destroys=0,pushes=0,reads=0,creates_while_busy=0,destroys_while_busy=0,calls_after_change=0;
    bool changed=false;
    SpikeEngine engine() {
        SpikeEngine e;
        e.world=[this] {
            ++tick;busy_now=tick<busy_until;
            if(world_change_at>=0 && tick>=world_change_at && !changed) {changed=true;world_id=0x2000;alive=false;}
            if(player_push_at>=0 && tick==player_push_at) player.x+=.003;
            if(alive && !asleep) {   // integrate: gravity, floor contact, the pushed velocity
                const double dt=1./60;vel.z-=9.81*dt;pos=pos+vel*dt;
                if(pos.z<=radius) {pos.z=radius;vel.z=0;}
            }
            return world_id;
        };
        e.busy=[this] {return busy_now;};
        e.create=[this](SpikeRoute,Vec3 at,double r,std::uint32_t& h) {
            ++creates;if(busy_now) ++creates_while_busy;if(changed) ++calls_after_change;
            if(fault_create) {
                if(create_fault_leaves_body) {alive=true;handle=next;pos=at;vel={};radius=r;throw SpikeFault("fake create fault after insert",handle);}
                throw SpikeFault("fake create fault");
            }
            alive=true;handle=next;pos=at;vel={};radius=r;h=handle;asleep=sleeps;return true;
        };
        e.destroy=[this](std::uint32_t h) {
            ++destroys;if(busy_now) ++destroys_while_busy;if(changed) ++calls_after_change;
            if(fault_destroy) throw SpikeFault("fake destroy fault");
            if(refuse_destroys>0) {--refuse_destroys;return false;}
            if(!alive || h!=handle) return false;
            alive=false;return true;
        };
        e.position=[this](std::uint32_t h)->std::optional<Vec3> {if(tick>=unreadable_from && tick<unreadable_to) throw SpikeUnreadable{};++reads;if(changed) ++calls_after_change;if(!alive || h!=handle) return {};return pos;};
        e.flags=[this](std::uint32_t h)->std::optional<std::uint32_t> {if(changed) ++calls_after_change;if(!alive || h!=handle) return {};return 2u;};
        e.player=[this]()->std::optional<Vec3> {return player;};
        e.push=[this](std::uint32_t h,Vec3 v) {
            ++pushes;if(changed) ++calls_after_change;
            if(fault_push_at>=0 && tick>=fault_push_at) throw SpikeFault("fake push fault");
            if(!alive || h!=handle) return false;
            vel=v;asleep=false;return true;
        };
        return e;
    }
};
const Vec3 kAt{.45,0,.40};
G1Stats good_g1(double observed=10) {
    G1Stats s;s.observed_s=observed;s.frames=600;s.steps=600;s.dispatches=50000;s.dispatch_outside_step=40000;s.dispatch_step_drain=10000;
    s.frame_threads=1;s.frame_thread=376;s.dispatch_thread=376;s.dispatch_busy_samples=700;return s;
}
OwnerEvidence good_owner() {OwnerEvidence o;o.a_resolved=o.a_userdata_is_player=o.a_entity_is_player=o.a_type_decoded=true;o.a_type_in_range=false;return o;}
SessionInput session_input(double t,G1Stats g1,OwnerEvidence owner,G2Counts g2={}) {
    SessionInput in;in.now_s=t;in.g1=g1;in.owner=owner;in.g2=g2;in.player_root=Vec3{0,0,.9};in.floor_z=0;in.forward=Vec3{1,0,0};return in;
}
}
// ======================= B13: the production HandBody =======================
namespace {
// Two hand bodies in a fake hknp world. Every engine call is counted and logged in order; forget() is mod-side bookkeeping, not an engine call.
struct FakeHands {
    std::uintptr_t world_id=0x1000;bool busy=false;bool world_unreadable=false;
    struct Body {bool alive{};std::uint32_t handle{};Vec3 pos{},vel{};bool foreign{};};   // foreign: not ours (a world rebuilt at the same address)
    int foreign_touched=0;
    std::array<Body,2> b{};std::uint32_t next=0x02000888;
    int creates=0,destroys=0,velocities=0,applies=0,positions=0,forgets=0,creates_while_busy=0,destroys_while_busy=0,calls_on_dead_world=0,create_world_mismatch=0;
    bool at_point=true;std::function<void()> on_position;   // on_position: runs inside service() (an engine callback), e.g. a re-entrant write
    bool fault_create=false,create_leaves_body=false,fault_wake=false,fault_apply=false,refuse_create=false,destroy_still_present=false,fault_destroy=false,destroy_unknown=false;int refuse_destroys=0,refuse_wakes=0;
    std::vector<std::string> log;
    std::uintptr_t dead_world=0;   // a world that changed: any engine call naming it is counted
    int engine_calls() const {return creates+destroys+velocities+applies+positions;}
    void change_world() {dead_world=world_id;world_id+=0x1000;for(auto& x:b) x.alive=false;}   // the bodies died with their world
    HandBodyEngine engine() {
        HandBodyEngine e;
        e.world=[this]()->std::uintptr_t {return world_unreadable?0:world_id;};
        e.busy=[this] {return busy;};
        e.create=[this](unsigned side,std::uintptr_t w,Vec3 at,std::uint32_t& h) {
            ++creates;if(busy) ++creates_while_busy;if(w!=world_id) ++create_world_mismatch;log.push_back("create:"+std::to_string(side));
            if(refuse_create) return false;
            if(fault_create) {
                if(create_leaves_body) {b[side]={true,next,at,{}};throw HandBodyEngineFault("fake create fault after insert",next++);}
                throw HandBodyEngineFault("fake create fault");
            }
            b[side]={true,next++,at,{}};h=b[side].handle;return true;
        };
        e.velocity=[this](unsigned side,std::uint32_t h,Vec3 v) {
            ++velocities;log.push_back("velocity:"+std::to_string(side)+":"+std::to_string(v.z));
            if(b[side].foreign && b[side].handle==h) ++foreign_touched;
            if(fault_wake) throw HandBodyEngineFault("fake wake fault");
            if(refuse_wakes>0) {--refuse_wakes;return false;}
            if(!b[side].alive || b[side].handle!=h) return false;
            b[side].vel=v;return true;
        };
        e.destroy=[this](unsigned side,std::uint32_t h) {
            ++destroys;if(busy) ++destroys_while_busy;log.push_back("destroy:"+std::to_string(side));
            if(b[side].alive && b[side].foreign && b[side].handle==h) {++foreign_touched;b[side].alive=false;return DestroyOutcome::gone;}
            if(fault_destroy) throw HandBodyEngineFault("fake destroy fault");
            if(refuse_destroys>0) {--refuse_destroys;return DestroyOutcome::refused;}
            if(!b[side].alive || b[side].handle!=h) return DestroyOutcome::absent;
            if(destroy_still_present) return DestroyOutcome::still_present;
            if(destroy_unknown) {b[side].alive=false;return DestroyOutcome::unknown;}
            b[side].alive=false;return DestroyOutcome::gone;
        };
        e.position=[this](unsigned side,std::uint32_t h)->std::optional<Vec3> {
            ++positions;if(on_position) on_position();if(b[side].foreign && b[side].handle==h) ++foreign_touched;if(!b[side].alive || b[side].handle!=h) return std::nullopt;return b[side].pos;
        };
        e.apply=[this](unsigned side,const hand_body::CommandRecord&) {
            ++applies;log.push_back("apply:"+std::to_string(side));
            if(fault_apply) throw HandBodyEngineFault("fake apply fault");
            return b[side].alive;
        };
        e.forget=[this](unsigned side) {++forgets;log.push_back("forget:"+std::to_string(side));};
        e.at_g1_point=[this] {return at_point;};
        return e;
    }
};
std::array<LifecycleInput,2> gameplay_in(std::uint64_t now_ms,bool gameplay=true,std::uint32_t gen=0) {
    std::array<LifecycleInput,2> in{};
    for(auto& li:in) {li.gameplay=gameplay;li.focused=li.tracked=true;li.now_ms=now_ms;li.world_generation=gen;}
    return in;
}
const std::array<std::optional<Vec3>,2> kPalms{Vec3{.3,.2,1.2},Vec3{.3,-.2,1.2}};
// A velocity record for the side's current body.
hand_body::CommandRecord velocity_record(std::uint32_t handle) {return hand_body::encode_set_velocity(handle,Vec3{0,0,.1},Vec3{});}

// The frame-by-frame model of the real order on the game thread (B11: frame, drain and present share one thread): the game frame consumes
// the input published at the previous Present (Start pauses it), then the 0x89fb0 dispatcher ticks (never while paused), then Present
// composes the next input. The release gate runs at Present; the composer's MenuTap turns the published menu button into the Start pulse.
struct FakeGame {
    FakeHands world;HandBodyEngine e=world.engine();HandBody body{e};HandBodyReleaseGate gate;MenuTap tap;
    bool paused=false,start_published=false,release_pub=true;std::uint32_t gen=0;unsigned paused_logic=4;
    int frame=0,last_dispatch=-1,pause_frame=-1,destroy_frame=-1,busy_from=-1,busy_to=-1;
    bool authored=false;bool pause_request=false;bool loading=false;
    std::uint64_t now() const {return static_cast<std::uint64_t>(frame)*16;}
    // menu: the raw menu button this frame (the user's hand).
    void step(bool menu) {
        ++frame;
        if(!paused && start_published) {paused=true;pause_frame=frame;}   // the game frame reads the pad: Start pauses
        if(!paused) {   // the dispatcher (outside the step); it does not run while paused
            world.busy=frame>=busy_from && frame<busy_to;
            const int before=world.destroys;
            const auto in=gameplay_in(now(),!release_pub && !authored,gen);
            body.service(in,kPalms);
            last_dispatch=frame;
            if(world.destroys>before && destroy_frame<0) destroy_frame=frame;
        }
        ReleaseSignals sig;sig.logic=paused?paused_logic:loading?(1u<<15):2;sig.menu=menu;sig.pause_request=pause_request;sig.authored=authored;
        const auto out=gate.update(sig,body.any_alive(),now());
        release_pub=out.release || authored;
        start_published=tap.update(out.menu,true,now()) || out.pause_request;
    }
};
}

static void b13_checks() {
    {   // creates_once_per_world: one body per hand, created once, woken once, alive for the whole stay in one world
        HandBodyFault::reset_for_tests();FakeHands f;HandBody hb(f.engine());
        for(int i=0;i<600;++i) hb.service(gameplay_in(1000+i*16),kPalms);
        CHECK(f.creates==2);CHECK(f.create_world_mismatch==0);   // review P3-1: created in the tick's world snapshot
        CHECK(hb.status(0).creates==1);CHECK(hb.status(1).creates==1);CHECK(hb.ready(0));CHECK(hb.ready(1));
        CHECK(hb.status(0).wake_writes==1);CHECK(hb.status(1).wake_writes==1);CHECK(f.destroys==0);CHECK(hb.any_alive());
        CHECK(hb.pose(0) && std::abs(hb.pose(0)->position.x-.3)<1e-9);CHECK(hb.status(0).available);
        // no spawn (untracked palm): no create
        FakeHands g;HandBody hb2(g.engine());for(int i=0;i<100;++i) hb2.service(gameplay_in(1000+i*16));
        CHECK(g.creates==0);
    }
    {   // wake_write_after_create: the one wake write right after each create, straight down at the recipe speed, nothing in between
        HandBodyFault::reset_for_tests();FakeHands f;HandBody hb(f.engine());
        hb.service(gameplay_in(1000),kPalms);
        CHECK(f.log.size()>=4);
        CHECK(f.log[0]=="create:0");CHECK(f.log[1].rfind("velocity:0:",0)==0);CHECK(std::stod(f.log[1].substr(11))==-R1Recipe::wake_speed);
        CHECK(f.log[2]=="create:1");CHECK(f.log[3].rfind("velocity:1:",0)==0);
        for(int i=0;i<300;++i) hb.service(gameplay_in(1016+i*16),kPalms);
        CHECK(f.velocities==2);CHECK(hb.status(0).wake_writes==1);
        // a refused create gets no wake write, and waits create_retry_ms before the next attempt
        FakeHands g;g.refuse_create=true;HandBody hb2(g.engine());
        for(int i=0;i<60;++i) hb2.service(gameplay_in(1000+i*16),kPalms);
        CHECK(g.velocities==0);CHECK(g.creates==2);CHECK(hb2.status(0).create_refusals==1);
        // a refused wake (busy at the re-read) is retried on the next idle tick, then never again (DeepSeek P3: no body left asleep)
        FakeHands r;r.refuse_wakes=2;HandBody hb3(r.engine());
        for(int i=0;i<60;++i) hb3.service(gameplay_in(1000+i*16),kPalms);
        CHECK(hb3.status(0).wake_writes==1);CHECK(hb3.status(1).wake_writes==1);CHECK(r.velocities==4);
        FakeHands n;n.refuse_wakes=1000;HandBody hb4(n.engine());
        for(int i=0;i<200;++i) hb4.service(gameplay_in(1000+i*16),kPalms);
        CHECK(n.velocities==2*static_cast<int>(HandBody::max_wake_tries));CHECK(hb4.status(0).wake_writes==0);
    }
    {   // never a create while physics is busy (the lifecycle and the manager both refuse)
        HandBodyFault::reset_for_tests();FakeHands f;f.busy=true;HandBody hb(f.engine());
        for(int i=0;i<100;++i) hb.service(gameplay_in(1000+i*16),kPalms);
        CHECK(f.creates==0);CHECK(f.creates_while_busy==0);
        f.busy=false;hb.service(gameplay_in(3000),kPalms);CHECK(f.creates==2);
    }
    {   // destroys_on_world_change_then_recreates: the load is seen first as a loading flag in the same world (release): destroyed there;
        // the world then changes with no body alive (no abandon) and the bodies are re-created in the new world
        HandBodyFault::reset_for_tests();FakeHands f;HandBody hb(f.engine());std::uint64_t t=1000;
        for(int i=0;i<60;++i,t+=16) hb.service(gameplay_in(t),kPalms);
        CHECK(f.creates==2);
        hb.service(gameplay_in(t,false),kPalms);t+=16;   // logic 1<<15 at a dispatch, same world
        CHECK(f.destroys==2);CHECK(hb.status(0).destroys==1);CHECK(hb.status(0).releases==1);CHECK(!hb.any_alive());
        f.change_world();
        for(int i=0;i<30;++i,t+=16) hb.service(gameplay_in(t,false,1),kPalms);
        for(int i=0;i<30;++i,t+=16) hb.service(gameplay_in(t,true,1),kPalms);
        CHECK(f.creates==4);CHECK(hb.status(0).abandons==0);CHECK(hb.status(1).abandons==0);CHECK(f.calls_on_dead_world==0);CHECK(hb.ready(0));
    }
    {   // abandon_counted_never_calls_engine: a world change with the bodies alive (the last resort): counted, forget() only, no engine call
        HandBodyFault::reset_for_tests();FakeHands f;HandBody hb(f.engine());std::uint64_t t=1000;
        for(int i=0;i<30;++i,t+=16) hb.service(gameplay_in(t),kPalms);
        CHECK(hb.any_alive());
        f.change_world();
        const int calls=f.engine_calls();
        hb.service(gameplay_in(t,false),kPalms);t+=16;   // not gameplay in the new world: no create either
        CHECK(f.engine_calls()==calls);CHECK(f.forgets==2);CHECK(hb.status(0).abandons==1);CHECK(hb.status(1).abandons==1);
        CHECK(hb.status(0).destroys==0);CHECK(!hb.any_alive());CHECK(!hb.pose(0));CHECK(hb.status(0).available);
        // a load-generation change at the same world address abandons too (review round 2: the old handle may name another body)
        FakeHands g;HandBody hb2(g.engine());
        for(int i=0;i<30;++i,t+=16) hb2.service(gameplay_in(t),kPalms);
        const int calls2=g.engine_calls();
        hb2.service(gameplay_in(t,false,7),kPalms);
        CHECK(g.engine_calls()==calls2);CHECK(hb2.status(0).abandons==1);CHECK(g.forgets==2);
        // an unreadable world is not a change: nothing decided, nothing abandoned
        FakeHands u;HandBody hb3(u.engine());
        for(int i=0;i<30;++i,t+=16) hb3.service(gameplay_in(t),kPalms);
        u.world_unreadable=true;for(int i=0;i<30;++i,t+=16) hb3.service(gameplay_in(t),kPalms);
        CHECK(hb3.status(0).abandons==0);CHECK(u.forgets==0);CHECK(hb3.unreadable_ticks()==30);
        u.world_unreadable=false;hb3.service(gameplay_in(t),kPalms);CHECK(hb3.ready(0));
    }
    {   // write_after_world_change_refused (Review Focus 1)
        HandBodyFault::reset_for_tests();FakeHands f;HandBody hb(f.engine());std::uint64_t t=1000;
        for(int i=0;i<10;++i,t+=16) hb.service(gameplay_in(t),kPalms);
        const auto h0=hb.status(0).handle;
        CHECK(hb.write(0,velocity_record(h0)));CHECK(f.applies==1);
        CHECK(!hb.write(0,velocity_record(h0+1)));CHECK(!hb.write(0,velocity_record(hb.status(1).handle)));CHECK(f.applies==1);   // not our handle
        f.busy=true;CHECK(!hb.write(0,velocity_record(h0)));f.busy=false;                                                           // busy: refused
        f.change_world();
        CHECK(!hb.write(0,velocity_record(h0)));CHECK(f.applies==1);   // before the manager has even seen the change
        hb.service(gameplay_in(t,false),kPalms);t+=16;
        CHECK(!hb.write(0,velocity_record(h0)));CHECK(!hb.write(1,velocity_record(h0)));CHECK(f.applies==1);
        CHECK(hb.status(0).refused_writes>=5);
        // a generation change at the same world address: refused once the manager has ticked
        FakeHands g;HandBody hb2(g.engine());
        for(int i=0;i<10;++i,t+=16) hb2.service(gameplay_in(t),kPalms);
        const auto g0=hb2.status(0).handle;
        hb2.service(gameplay_in(t,true,3),kPalms);t+=16;
        CHECK(!hb2.write(0,velocity_record(g0)));CHECK(g.applies==0);
    }
    {   // fault_latches_off_and_reports_unavailable
        HandBodyFault::reset_for_tests();FakeHands f;HandBody hb(f.engine());std::uint64_t t=1000;
        for(int i=0;i<10;++i,t+=16) hb.service(gameplay_in(t),kPalms);
        f.fault_apply=true;CHECK(!hb.write(0,velocity_record(hb.status(0).handle)));
        CHECK(HandBodyFault::tripped());CHECK(!hb.status(0).available);CHECK(!hb.status(1).available);CHECK(hb.status(0).faults==1);CHECK(!hb.ready(0));
        hb.service(gameplay_in(t),kPalms);t+=16;   // the latch destroys both bodies once
        CHECK(f.destroys==2);CHECK(!hb.any_alive());
        for(int i=0;i<200;++i,t+=16) hb.service(gameplay_in(t),kPalms);
        CHECK(f.creates==2);CHECK(f.destroys==2);CHECK(hb.status(0).state==BodyState::faulted);
        f.fault_apply=false;CHECK(!hb.write(0,velocity_record(0x02000888)));
        // a faulting wake write latches too (the body it created is destroyed once at the next tick)
        HandBodyFault::reset_for_tests();FakeHands g;g.fault_wake=true;HandBody hb2(g.engine());
        for(int i=0;i<20;++i,t+=16) hb2.service(gameplay_in(t),kPalms);
        CHECK(g.creates==1);CHECK(g.destroys==1);CHECK(!hb2.status(0).available);CHECK(!hb2.any_alive());
        // a create fault that left a record: it is tracked and gets exactly one destroy
        HandBodyFault::reset_for_tests();FakeHands c;c.fault_create=c.create_leaves_body=true;HandBody hb3(c.engine());
        for(int i=0;i<20;++i,t+=16) hb3.service(gameplay_in(t),kPalms);
        CHECK(c.creates==1);CHECK(c.destroys==1);CHECK(!c.b[0].alive);CHECK(hb3.status(0).faults==1);
        // a destroy that returns with the body still there: latched, never retried
        HandBodyFault::reset_for_tests();FakeHands d;HandBody hb4(d.engine());
        for(int i=0;i<10;++i,t+=16) hb4.service(gameplay_in(t),kPalms);
        d.destroy_still_present=true;for(int i=0;i<20;++i,t+=16) hb4.service(gameplay_in(t,false),kPalms);
        CHECK(d.destroys==2);CHECK(HandBodyFault::tripped());CHECK(!hb4.status(0).available);   // one attempt per side, never retried
        // a faulted destroy: latched, one attempt per side, never retried
        HandBodyFault::reset_for_tests();FakeHands fd;HandBody hb8(fd.engine());
        for(int i=0;i<10;++i,t+=16) hb8.service(gameplay_in(t),kPalms);
        fd.fault_destroy=true;for(int i=0;i<60;++i,t+=16) hb8.service(gameplay_in(t,false),kPalms);
        CHECK(fd.destroys==2);CHECK(HandBodyFault::tripped());CHECK(hb8.status(0).faults==1);CHECK(hb8.status(0).state==BodyState::faulted);
        CHECK(hb8.status(0).destroys==0);   // never reported destroyed
        // HandBodyFault tripped elsewhere: no create at all
        FakeHands e2;HandBody hb5(e2.engine());for(int i=0;i<20;++i,t+=16) hb5.service(gameplay_in(t),kPalms);
        CHECK(e2.creates==0);
        // a gate violation (G1 after the pass, G2) latches: destroy once, then off
        HandBodyFault::reset_for_tests();FakeHands v;HandBody hb6(v.engine());
        for(int i=0;i<10;++i,t+=16) hb6.service(gameplay_in(t),kPalms);
        HandBodyGate bad;bad.violated=true;bad.why="G2 rejected owner (a) at run time";
        for(int i=0;i<10;++i,t+=16) hb6.service(gameplay_in(t),kPalms,bad);
        CHECK(v.destroys==2);CHECK(v.creates==2);CHECK(hb6.why().find("G2")!=std::string::npos);
        // G1 not passed yet: no create
        HandBodyFault::reset_for_tests();FakeHands w;HandBody hb7(w.engine());HandBodyGate observing;observing.g1_pass=false;
        for(int i=0;i<100;++i,t+=16) hb7.service(gameplay_in(t),kPalms,observing);
        CHECK(w.creates==0);
        HandBodyFault::reset_for_tests();
    }
    {   // busy_destroy_goes_through_queue: busy at the destroy -> a queued record (no direct call); drained by one direct call when idle
        HandBodyFault::reset_for_tests();FakeHands f;HandBody hb(f.engine());std::uint64_t t=1000;
        for(int i=0;i<10;++i,t+=16) hb.service(gameplay_in(t),kPalms);
        f.busy=true;
        hb.service(gameplay_in(t,false),kPalms);t+=16;
        CHECK(hb.status(0).queued_destroys==1);CHECK(hb.status(1).queued_destroys==1);CHECK(f.destroys==0);
        for(int i=0;i<5;++i,t+=16) hb.service(gameplay_in(t,false),kPalms);
        CHECK(f.destroys==0);CHECK(hb.status(0).busy_waits==5);CHECK(hb.any_alive());   // destroying: the render gate keeps Start held
        f.busy=false;hb.service(gameplay_in(t,false),kPalms);t+=16;
        CHECK(f.destroys==2);CHECK(f.destroys_while_busy==0);CHECK(hb.status(0).destroys==1);CHECK(hb.status(0).direct_destroys==1);CHECK(!hb.any_alive());
        // a world change while the queued destroy waits: the record is cancelled (abandon), no engine call
        FakeHands g;HandBody hb2(g.engine());
        for(int i=0;i<10;++i,t+=16) hb2.service(gameplay_in(t),kPalms);
        g.busy=true;hb2.service(gameplay_in(t,false),kPalms);t+=16;
        g.change_world();g.busy=false;const int calls=g.engine_calls();
        hb2.service(gameplay_in(t,false),kPalms);t+=16;
        CHECK(g.engine_calls()==calls);CHECK(hb2.status(0).abandons==1);
        // a refused direct destroy is retried at the next idle tick
        FakeHands r;r.refuse_destroys=1;HandBody hb3(r.engine());
        for(int i=0;i<10;++i,t+=16) hb3.service(gameplay_in(t),kPalms);
        hb3.service(gameplay_in(t,false),kPalms);t+=16;hb3.service(gameplay_in(t,false),kPalms);
        CHECK(r.destroys==3);CHECK(!hb3.any_alive());
    }
    {   // disabled_by_knob_never_creates
        CHECK(hand_body_knob_enabled(true,false));CHECK(!hand_body_knob_enabled(false,false));CHECK(!hand_body_knob_enabled(true,true));
        CHECK(!hand_body_knob_enabled(false,true));
        HandBodyFault::reset_for_tests();FakeHands f;HandBody hb(f.engine(),hand_body_knob_enabled(true,true));
        for(int i=0;i<300;++i) hb.service(gameplay_in(1000+i*16),kPalms);
        CHECK(f.engine_calls()==0);CHECK(!hb.status(0).available);CHECK(!hb.write(0,velocity_record(0x02000888)));
    }
    {   // a destroy whose slot cannot be read afterwards: counted unverified, no longer tracked, never retried
        HandBodyFault::reset_for_tests();FakeHands f;f.destroy_unknown=true;HandBody hb(f.engine());std::uint64_t t=1000;
        for(int i=0;i<10;++i,t+=16) hb.service(gameplay_in(t),kPalms);
        for(int i=0;i<10;++i,t+=16) hb.service(gameplay_in(t,false),kPalms);
        CHECK(f.destroys==2);CHECK(hb.status(0).unverified==1);CHECK(!hb.any_alive());CHECK(hb.status(0).available);
        // shutdown requested before the tick: no create in it (the stop drains the tick and re-checks)
        FakeHands g;HandBody hb2(g.engine());hb2.shutdown();
        for(int i=0;i<10;++i,t+=16) hb2.service(gameplay_in(t),kPalms);
        CHECK(g.creates==0);
    }
    {   // shutdown: the next tick destroys both bodies; nothing is created afterwards
        HandBodyFault::reset_for_tests();FakeHands f;HandBody hb(f.engine());std::uint64_t t=1000;
        for(int i=0;i<10;++i,t+=16) hb.service(gameplay_in(t),kPalms);
        hb.shutdown();hb.service(gameplay_in(t),kPalms);t+=16;
        CHECK(f.destroys==2);CHECK(!hb.any_alive());
        for(int i=0;i<100;++i,t+=16) hb.service(gameplay_in(t),kPalms);
        CHECK(f.creates==2);
    }
    {   // the release gate alone
        HandBodyReleaseGate g;
        auto o=g.update({},false,1000);CHECK(!o.release);
        ReleaseSignals menu;menu.menu=true;o=g.update(menu,true,1016);CHECK(o.release);CHECK(o.menu);
        o=g.update({},true,1032);CHECK(o.menu);CHECK(o.release);CHECK(g.holds()==1);        // released with a body alive: held
        o=g.update({},false,1048);CHECK(!o.menu);CHECK(o.release);                          // gone: the release reaches the composer
        o=g.update({},false,1048+HandBodyReleaseGate::settle_ms);CHECK(!o.release);         // settled
        o=g.update(menu,true,3000);o=g.update({},true,3016);
        o=g.update({},true,3016+HandBodyReleaseGate::hold_cap_ms);CHECK(!o.menu);CHECK(g.timeouts()==1);   // capped
        HandBodyReleaseGate p;ReleaseSignals pr;pr.pause_request=true;
        o=p.update(pr,true,1000);CHECK(!o.pause_request);CHECK(o.release);                  // deferred while a body exists
        o=p.update({},true,1016);CHECK(!o.pause_request);
        o=p.update({},false,1032);CHECK(o.pause_request);                                   // emitted once the body is gone
        o=p.update({},false,1048);CHECK(!o.pause_request);CHECK(o.release);
        HandBodyReleaseGate q;ReleaseSignals settings;settings.settings_open=true;CHECK(q.update(settings,false,1000).release);
        ReleaseSignals menus;menus.logic=4;CHECK(q.update(menus,false,1016).release);ReleaseSignals death;death.authored=true;CHECK(q.update(death,false,1032).release);
        // a body not alive: the menu press is never held, Start is not delayed
        HandBodyReleaseGate n;n.update(menu,false,1000);CHECK(!n.update({},false,1016).menu);CHECK(n.holds()==0);
    }
    {   // pause_or_menu_destroys_before_world_change: the dispatcher stops when the game pauses; the destroy is on the last tick before it
        HandBodyFault::reset_for_tests();FakeGame g;
        for(int i=0;i<100;++i) g.step(false);
        CHECK(g.world.creates==2);CHECK(g.body.any_alive());
        g.step(true);    // frame 101: the menu button goes down (Present): release published
        g.step(false);   // frame 102: the dispatcher destroys; Present: released, no body -> Start
        for(int i=0;i<10;++i) g.step(false);
        CHECK(g.paused);CHECK(g.destroy_frame>0);CHECK(g.destroy_frame==g.last_dispatch);CHECK(g.pause_frame==g.last_dispatch+1);
        CHECK(g.world.destroys==2);CHECK(!g.body.any_alive());
        // while paused a load replaces the world; resume in the new world: no abandon, no call on the dead world, fresh bodies
        g.world.change_world();++g.gen;g.paused_logic=1u<<15;
        for(int i=0;i<30;++i) g.step(false);
        g.paused=false;g.start_published=false;
        for(int i=0;i<120;++i) g.step(false);
        CHECK(g.body.status(0).abandons==0);CHECK(g.body.status(1).abandons==0);CHECK(g.world.creates==4);CHECK(g.body.ready(0));
        CHECK(g.body.status(0).releases==1);
    }
    {   // the same with the dispatcher late (busy: queued destroy) and a held menu button: Start waits for the destroy
        HandBodyFault::reset_for_tests();FakeGame g;
        for(int i=0;i<100;++i) g.step(false);
        g.busy_from=102;g.busy_to=106;
        g.step(true);g.step(false);   // pressed at 101, released at 102 while the queued destroy waits
        for(int i=0;i<10;++i) g.step(false);
        CHECK(g.paused);CHECK(g.destroy_frame==106);CHECK(g.destroy_frame==g.last_dispatch);CHECK(g.world.destroys_while_busy==0);
        CHECK(g.body.status(0).queued_destroys==1);CHECK(g.gate.holds()==1);CHECK(g.gate.timeouts()==0);
        g.world.change_world();++g.gen;for(int i=0;i<5;++i) g.step(false);
        CHECK(g.body.status(0).abandons==0);
    }
    {   // the hold-Y pause event: deferred until the bodies are gone
        HandBodyFault::reset_for_tests();FakeGame g;
        for(int i=0;i<100;++i) g.step(false);
        g.pause_request=true;g.step(false);g.pause_request=false;
        for(int i=0;i<10;++i) g.step(false);
        CHECK(g.paused);CHECK(g.destroy_frame==g.last_dispatch);CHECK(g.world.destroys==2);
    }
    {   // death_destroys_same_world: the death clip (an authored camera) releases the bodies in the same world; the reload then finds none
        HandBodyFault::reset_for_tests();FakeGame g;
        for(int i=0;i<100;++i) g.step(false);
        const auto world_before=g.world.world_id;
        g.authored=true;g.step(false);g.step(false);
        CHECK(g.world.destroys==2);CHECK(g.world.world_id==world_before);CHECK(!g.body.any_alive());CHECK(g.body.status(0).releases==1);
        for(int i=0;i<60;++i) g.step(false);   // the death clip plays on (dispatcher running): no re-create
        CHECK(g.world.creates==2);
        g.paused=true;g.paused_logic=1u<<15;g.world.change_world();++g.gen;for(int i=0;i<20;++i) g.step(false);   // the death reload
        g.paused=false;g.start_published=false;g.authored=false;for(int i=0;i<120;++i) g.step(false);
        CHECK(g.body.status(0).abandons==0);CHECK(g.world.creates==4);
    }
    {   // without the gate the same pause abandons (what the B12 ruling avoids): the model is not vacuous
        HandBodyFault::reset_for_tests();FakeGame g;
        for(int i=0;i<100;++i) g.step(false);
        g.start_published=true;g.step(false);   // Start reaches the game with no release first (keyboard Esc)
        CHECK(g.paused);CHECK(g.body.any_alive());
        g.world.change_world();++g.gen;g.paused=false;g.start_published=false;
        for(int i=0;i<5;++i) g.step(false);
        CHECK(g.body.status(0).abandons==1);
    }
    {   // load_start_destroys_not_abandons: loading starts in the same world (logic 1<<15, generation unchanged) while the dispatcher still
        // ticks: a same-world loss of gameplay, destroyed directly (or queued when busy) BEFORE the generation bumps at loading exit
        HandBodyFault::reset_for_tests();FakeGame g;
        for(int i=0;i<100;++i) g.step(false);
        CHECK(g.body.any_alive());
        g.loading=true;g.step(false);g.step(false);   // Present publishes the release; the next dispatcher tick destroys
        CHECK(g.world.destroys==2);CHECK(g.destroy_frame==g.last_dispatch);CHECK(g.body.status(0).abandons==0);CHECK(g.body.status(1).abandons==0);
        CHECK(g.world.forgets==0);CHECK(!g.body.any_alive());CHECK(g.body.status(0).releases==1);
        g.paused=true;g.paused_logic=1u<<15;g.world.change_world();for(int i=0;i<20;++i) g.step(false);   // the load: no dispatcher
        ++g.gen;g.paused=false;g.loading=false;g.start_published=false;for(int i=0;i<120;++i) g.step(false);   // loading exit: the bump
        CHECK(g.world.creates==4);CHECK(g.body.status(0).abandons==0);CHECK(g.world.calls_on_dead_world==0);
        // busy at loading start: queued, drained at the first idle tick, then the bump finds no body: still no abandon
        HandBodyFault::reset_for_tests();FakeHands f;HandBody hb(f.engine());std::uint64_t t=1000;
        for(int i=0;i<10;++i,t+=16) hb.service(gameplay_in(t),kPalms);
        f.busy=true;for(int i=0;i<4;++i,t+=16) hb.service(gameplay_in(t,false),kPalms);
        CHECK(f.destroys==0);CHECK(hb.status(0).queued_destroys==1);CHECK(hb.any_alive());
        f.busy=false;hb.service(gameplay_in(t,false),kPalms);t+=16;
        CHECK(f.destroys==2);CHECK(!hb.any_alive());
        for(int i=0;i<10;++i,t+=16) hb.service(gameplay_in(t,true,5),kPalms);   // after the bump: fresh bodies, no abandon
        CHECK(hb.status(0).abandons==0);CHECK(f.creates==4);CHECK(hb.ready(0));
        // a generation change with a body alive: abandoned, and writes refused from then on
        const auto h=hb.status(0).handle;CHECK(hb.write(0,velocity_record(h)));
        hb.service(gameplay_in(t,true,6),kPalms);t+=16;CHECK(!hb.write(0,velocity_record(h)));CHECK(hb.status(0).abandons==1);
    }
    {   // body_alive_at_loading_exit_abandons (review round 2): the dispatcher does not tick during the load, the world is rebuilt at the
        // same address and a DIFFERENT body now holds the same full handle: no engine call at all, abandon counted, the other body untouched
        HandBodyFault::reset_for_tests();FakeGame g;
        for(int i=0;i<100;++i) g.step(false);
        CHECK(g.body.any_alive());
        const auto h0=g.body.status(0).handle,h1=g.body.status(1).handle;
        g.paused=true;g.paused_logic=1u<<15;g.loading=true;   // loading begins with no dispatcher tick (B11: it stops while paused)
        for(int i=0;i<20;++i) g.step(false);
        CHECK(g.body.any_alive());
        for(auto& x:g.world.b) {x.alive=true;x.foreign=true;}   // the rebuilt world at the same address: foreign bodies, same handles
        CHECK(g.world.b[0].handle==h0);CHECK(g.world.b[1].handle==h1);
        const int calls=g.world.engine_calls();
        ++g.gen;g.paused=false;g.loading=false;g.start_published=false;g.step(false);   // loading exit: the generation bumps
        CHECK(g.world.engine_calls()==calls);CHECK(g.world.foreign_touched==0);CHECK(g.world.b[0].alive && g.world.b[1].alive);
        CHECK(g.body.status(0).abandons==1);CHECK(g.body.status(1).abandons==1);CHECK(g.world.forgets==2);CHECK(g.body.status(0).destroys==0);
        for(int i=0;i<120;++i) g.step(false);
        CHECK(g.world.foreign_touched==0);
    }
    {   // real_world_change_still_abandons: the world pointer itself changed with the bodies alive: abandon, no engine call
        HandBodyFault::reset_for_tests();FakeGame g;
        for(int i=0;i<100;++i) g.step(false);
        const int calls=g.world.engine_calls();
        g.world.change_world();g.loading=true;++g.gen;g.step(false);
        CHECK(g.body.status(0).abandons==1);CHECK(g.body.status(1).abandons==1);CHECK(g.world.engine_calls()==calls);CHECK(g.world.forgets==2);
    }
    {   // write() only at the G1 point and never from inside service() (review P3-2): refused and counted, no UB
        HandBodyFault::reset_for_tests();FakeHands f;HandBody hb(f.engine());std::uint64_t t=1000;
        for(int i=0;i<10;++i,t+=16) hb.service(gameplay_in(t),kPalms);
        const auto h=hb.status(0).handle;
        f.at_point=false;CHECK(!hb.write(0,velocity_record(h)));CHECK(hb.status(0).misplaced_writes==1);CHECK(f.applies==0);
        f.at_point=true;
        bool inner=true;f.on_position=[&] {inner=hb.write(0,velocity_record(h));};
        hb.service(gameplay_in(t),kPalms);t+=16;
        CHECK(!inner);CHECK(hb.status(0).reentrant_writes>=1);CHECK(f.applies==0);
        f.on_position=nullptr;CHECK(hb.write(0,velocity_record(h)));CHECK(f.applies==1);
        CHECK(hb.status(0).refused_writes==hb.status(0).misplaced_writes+hb.status(0).reentrant_writes);
    }
    HandBodyFault::reset_for_tests();
}

// ======================= B14: the drive =======================
namespace {
// A 90 Hz fake world for the drive: one physics step per tick (gravity, then the written velocity integrates), an optional wall at x = wall_x
// (the body never passes it; its velocity into the wall is removed), a microsecond clock that each engine call advances (cost), and
// garbage injection for the read-back.
struct FakeDrive {
    static constexpr double dt=1./90;
    std::uintptr_t world_id=0x1000;bool busy=false;
    struct Body {bool alive{};std::uint32_t handle{};Vec3 pos{},vel{},spin{};};
    std::array<Body,2> b{};std::uint32_t next=0x02000888;
    double wall_x=1e9;double gravity=9.81;bool step_gravity=true;double vel_scale=1;   // vel_scale < 1: a slowed world / a push by something else (the achieved velocity falls short with no wall)
    int creates=0,destroys=0,velocities=0,applies=0,motions=0,steps=0;
    std::array<int,2> applies_side{};std::array<Vec3,2> last_written{};std::array<int,2> creates_side{},destroys_side{};
    std::uint64_t clock=0;std::uint64_t call_us=7;   // every engine call costs call_us microseconds of the fake clock
    std::uint64_t cpu=0;bool cpu_ok=true;std::uint64_t preempt_us=0;   // fix round 1: the thread's CPU clock; preempt_us: wall time with no CPU (in apply)
    void spend() {clock+=call_us;cpu+=call_us;}
    bool garbage_nan=false,garbage_huge=false,garbage_pos=false,position_garbage=false,motion_unreadable=false;
    std::vector<std::string> log;
    void step() {   // the physics step between two dispatcher ticks
        ++steps;
        for(auto& x:b) if(x.alive) {
            if(step_gravity) x.vel.z-=gravity*dt;
            if(vel_scale!=1) x.vel=x.vel*vel_scale;
            const double before=x.pos.x;
            x.pos=x.pos+x.vel*dt;
            if(before<=wall_x-.05+1e-12 && x.pos.x>wall_x-.05) {x.pos.x=wall_x-.05;if(x.vel.x>0) x.vel.x=0;}   // a slab: stops what comes from the near side
        }
    }
    HandBodyEngine engine() {
        HandBodyEngine e;
        e.world=[this]()->std::uintptr_t {return world_id;};
        e.busy=[this] {return busy;};
        e.create=[this](unsigned side,std::uintptr_t,Vec3 at,std::uint32_t& h) {spend();++creates;++creates_side[side];log.push_back("create:"+std::to_string(side));b[side]={true,next++,at,{},{}};h=b[side].handle;return true;};
        e.velocity=[this](unsigned side,std::uint32_t h,Vec3 v) {spend();++velocities;log.push_back("wake:"+std::to_string(side));if(!b[side].alive || b[side].handle!=h) return false;b[side].vel=v;return true;};
        e.destroy=[this](unsigned side,std::uint32_t h) {spend();++destroys;++destroys_side[side];log.push_back("destroy:"+std::to_string(side));if(!b[side].alive || b[side].handle!=h) return DestroyOutcome::absent;b[side].alive=false;return DestroyOutcome::gone;};
        e.position=[this](unsigned side,std::uint32_t h)->std::optional<Vec3> {
            spend();if(!b[side].alive || b[side].handle!=h) return std::nullopt;
            if(position_garbage) return Vec3{b[side].pos.x,std::numeric_limits<double>::quiet_NaN(),b[side].pos.z};
            return b[side].pos;
        };
        e.motion=[this](unsigned side,std::uint32_t h)->std::optional<BodyMotion> {
            spend();++motions;
            if(motion_unreadable) throw HandBodyUnreadable{};
            if(!b[side].alive || b[side].handle!=h) return std::nullopt;
            BodyMotion m{b[side].pos,b[side].vel,b[side].spin};
            if(garbage_nan) m.velocity.x=std::numeric_limits<double>::quiet_NaN();
            if(garbage_huge) m.velocity.x=500;
            if(garbage_pos) m.position.y=std::numeric_limits<double>::infinity();
            return m;
        };
        e.apply=[this](unsigned side,const hand_body::CommandRecord& r) {
            clock+=preempt_us;spend();++applies;++applies_side[side];log.push_back("apply:"+std::to_string(side));
            std::uint32_t h{};std::memcpy(&h,r.bytes.data()+8,4);float f[3]{};std::memcpy(f,r.bytes.data()+0x10,12);
            if(!b[side].alive || b[side].handle!=h) return false;
            b[side].vel={f[0],f[1],f[2]};last_written[side]=b[side].vel;return true;
        };
        e.forget=[](unsigned) {};
        e.at_g1_point=[] {return true;};
        e.now_us=[this] {return clock;};
        e.cpu_cycles=[this]()->std::optional<std::uint64_t> {if(!cpu_ok) return std::nullopt;return cpu*3;};e.cycles_per_us=3;   // 3 cycles per us
        return e;
    }
};
WorldTrackedPose pose_at(Vec3 p) {WorldTrackedPose w;w.position=p;w.basis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};return w;}
// One dispatcher tick: service (spawn at the targets when free), then the drive for both sides, then the physics step.
struct DriveRig {
    FakeDrive w;HandBody hb{w.engine()};double t=10;std::uint64_t ms=1000;
    std::array<Vec3,2> target{Vec3{.4,.2,1.2},Vec3{.4,-.2,1.2}};std::array<bool,2> free{true,true},yield{false,false};
    std::optional<Vec3> root{};bool locomotion=false;bool gameplay=true;
    void tick(int drives_per_side=1) {
        std::array<std::optional<Vec3>,2> spawn{};
        for(unsigned side=0;side<2;++side) if(free[side]) spawn[side]=target[side];
        hb.service(gameplay_in(ms,gameplay),spawn);
        for(int k=0;k<drives_per_side;++k) for(unsigned side=0;side<2;++side) {
            DriveInput d;d.side=side;d.target=pose_at(target[side]);d.now=t;d.yield=yield[side];d.target_free=free[side];d.player_root=root;d.locomotion=locomotion;
            hb.drive(d);
        }
        w.step();t+=FakeDrive::dt;ms+=11;
    }
};
}

// ======================= B16: the filter record (type 0x1a) =======================
static void b16_checks() {
    {   // write()'s allow-list: a velocity record (9, 0x30) and a filter record (0x1a, 0x20) carrying an allowed hand word; nothing else
        HandBodyFault::reset_for_tests();FakeHands f;HandBody hb(f.engine());std::uint64_t t=1000;
        for(int i=0;i<10;++i,t+=16) hb.service(gameplay_in(t),kPalms);
        const auto h=hb.status(0).handle;CHECK(hb.ready(0));CHECK(hb.filter_word(0)==R1Recipe::filter);CHECK(hb.status(0).filter==R1Recipe::filter);
        const std::uint32_t held=hand_body::filter_word(hand_body::layer_debris,hand_body::word_mask(R1Recipe::filter)&~hand_body::layer_bit(hand_body::layer_npc_body_ragdoll));
        CHECK(hand_filter_word_allowed(held));
        int applies=f.applies;
        CHECK(hb.write(0,hand_body::encode_set_filter(h,held)));CHECK(f.applies==applies+1);CHECK(hb.filter_word(0)==held);CHECK(hb.status(0).filter_writes==1);
        // the filter write did not take the tick's velocity slot: a velocity write in the same tick still goes; a second filter write does not
        CHECK(hb.write(0,velocity_record(h)));CHECK(!hb.write(0,hand_body::encode_set_filter(h,R1Recipe::filter)));CHECK(hb.filter_word(0)==held);
        hb.service(gameplay_in(t),kPalms);t+=16;
        // refused words: DEBRIS in the mask, a player layer, a world layer missing, another layer than DEBRIS; refused sizes and types
        applies=f.applies;const auto refused=hb.status(0).filter_refused;
        for(const std::uint32_t bad:{hand_body::hand_filter_word(true),hand_body::filter_word(hand_body::layer_debris,hand_body::word_mask(R1Recipe::filter)|hand_body::layer_bit(hand_body::layer_player)),
                                     hand_body::filter_word(hand_body::layer_debris,hand_body::word_mask(R1Recipe::filter)&~hand_body::layer_bit(hand_body::layer_world_high)),
                                     hand_body::filter_word(hand_body::layer_rigid_body,hand_body::word_mask(R1Recipe::filter)),0u,0xffffffffu})
            CHECK(!hb.write(0,hand_body::encode_set_filter(h,bad)));
        CHECK(hb.status(0).filter_refused==refused+6);
        auto wrong_size=hand_body::encode_set_filter(h,R1Recipe::filter);wrong_size.size=0x30;CHECK(!hb.write(0,wrong_size));
        auto other=velocity_record(h);std::uint16_t six=6;std::memcpy(other.bytes.data()+4,&six,2);CHECK(!hb.write(0,other));   // setBodyTransform: never
        CHECK(f.applies==applies);CHECK(hb.filter_word(0)==held);
        // the lifecycle guards hold for the filter record too: busy, outside the G1 point, not our handle
        f.busy=true;CHECK(!hb.write_filter(0,R1Recipe::filter));f.busy=false;
        f.at_point=false;CHECK(!hb.write_filter(0,R1Recipe::filter));f.at_point=true;
        CHECK(!hb.write(0,hand_body::encode_set_filter(h+1,R1Recipe::filter)));CHECK(f.applies==applies);
        // write_filter: no call when the body already carries the word
        CHECK(!hb.write_filter(0,held));CHECK(f.applies==applies);
        CHECK(hb.write_filter(0,R1Recipe::filter));CHECK(hb.filter_word(0)==R1Recipe::filter);
        // a recreated body always starts with the default word (destroy on a lost gameplay frame, then a new create)
        hb.service(gameplay_in(t),kPalms);t+=16;CHECK(hb.write_filter(0,held));CHECK(hb.filter_word(0)==held);
        hb.service(gameplay_in(t,false),kPalms);t+=16;CHECK(!hb.ready(0));CHECK(hb.filter_word(0)==R1Recipe::filter);
        for(int i=0;i<60;++i,t+=16) hb.service(gameplay_in(t),kPalms);
        CHECK(hb.ready(0));CHECK(hb.status(0).creates==2);CHECK(hb.filter_word(0)==R1Recipe::filter);
        // an abandoned body's word is forgotten with it
        hb.service(gameplay_in(t),kPalms);t+=16;CHECK(hb.write_filter(1,held));f.change_world();hb.service(gameplay_in(t,false),kPalms);
        CHECK(hb.status(1).abandons==1);CHECK(hb.filter_word(1)==R1Recipe::filter);
    }
}
static void b14_checks() {
    {   // drive_writes_once_per_tick: the create tick's wake write is its one write; then exactly one drive write per side per tick, even when the
        // dispatcher calls drive several times in a tick (it runs 7-11k/s); a second drive in one tick is counted and writes nothing
        HandBodyFault::reset_for_tests();DriveRig r;
        r.tick(3);
        CHECK(r.w.creates==2);CHECK(r.w.velocities==2);CHECK(r.w.applies==0);   // the wake writes only
        for(int i=0;i<50;++i) {const int before0=r.w.applies_side[0],before1=r.w.applies_side[1];r.tick(4);CHECK(r.w.applies_side[0]-before0<=1);CHECK(r.w.applies_side[1]-before1<=1);}
        CHECK(r.w.applies_side[0]==50);CHECK(r.w.applies_side[1]==50);   // the create tick's drive only starts the dt clock (the wake was its write)
        CHECK(r.hb.drive_status(0).second_drives==50*3+2);   // every further call in a tick, the create tick included
        CHECK(r.hb.drive_status(0).drive_writes==50);
        CHECK(r.w.velocities==2);   // never another wake write once driven
    }
    {   // drive_follows_target_within_1cm (no obstacles, 90 Hz, gravity on, gravity compensated): a 0.5 m reach, then hold
        HandBodyFault::reset_for_tests();DriveRig r;
        r.tick();   // created at the targets
        const Vec3 start=r.target[0];
        for(int i=0;i<90;++i) {r.target[0]=start+Vec3{.5*(i+1)/90.,0,-.3*(i+1)/90.};r.tick();}   // 0.58 m in 1 s
        for(int i=0;i<45;++i) r.tick();   // 0.5 s hold
        const double gap=length(r.w.b[0].pos-r.target[0]);
        CHECK(gap<.01);CHECK(r.hb.drive_status(0).gap_m<.01);CHECK(length(r.w.b[1].pos-r.target[1])<.01);   // the idle hand hovers (gravity compensated)
        CHECK(r.hb.drive_status(0).snaps==0);CHECK(r.hb.status(0).faults==0);CHECK(r.hb.drive_status(0).contact_frames==0);
        CHECK(!r.hb.drive_status(0).contact_now);   // B15: no contact signal in free air at rest
        {const auto before=r.hb.pose_stamped(0);r.tick();const auto after=r.hb.pose_stamped(0);CHECK(before.second<after.second && after.first);}   // B15: every tick stamps a new pose
    }
    {   // B15 (review P2-3): contact_now in free air. A hard unobstructed sweep (the command is cap-limited) never reads as contact; a slowed
        // world is a shortfall with no wall, so the drive may report it, which is why the visible hand also needs a world hit near the body
        HandBodyFault::reset_for_tests();DriveRig r;r.tick();
        const Vec3 start=r.target[0];bool ever=false;
        for(int i=0;i<90;++i) {r.target[0]=start+Vec3{6.*(i+1)/90.,0,0};r.tick();ever=ever || r.hb.drive_status(0).contact_now;}   // 6 m/s for 1 s
        CHECK(!ever);
        HandBodyFault::reset_for_tests();DriveRig d;d.tick();d.w.vel_scale=.5;
        const Vec3 begin=d.target[0];bool dilated=false;
        for(int i=0;i<60;++i) {d.target[0]=begin+Vec3{1.*(i+1)/90.,0,0};d.tick();dilated=dilated || d.hb.drive_status(0).contact_now;}
        CHECK(dilated);   // known limitation, documented: a shortfall alone is not contact
    }
    {   // B15: a gentle press into a wall (5 cm past the stop, the command is not cap-limited) reads as contact; a hard 10 cm press is cap-limited
        // every tick, which is not judged (the visible hand then relies on the clamp's own contact and the body-at-the-clamp-pose rule)
        HandBodyFault::reset_for_tests();DriveRig r;r.w.wall_x=.6;r.tick();
        r.target[0]=Vec3{.60,.2,1.2};r.free[0]=false;
        int contact_ticks=0;for(int i=0;i<90;++i) {r.tick();if(r.hb.drive_status(0).contact_now) ++contact_ticks;}
        CHECK(contact_ticks>=60);CHECK(r.hb.drive_status(0).contact_frames>=60);CHECK(!r.hb.drive_status(1).contact_now);
        CHECK(r.hb.drive_status(0).contact_now);std::this_thread::sleep_for(std::chrono::milliseconds(80));
        CHECK(!r.hb.drive_status(0).contact_now);   // the drive stopped judging (paused, skipped): the signal does not latch
        HandBodyFault::reset_for_tests();DriveRig h;h.w.wall_x=.6;h.tick();
        h.target[0]=Vec3{.65,.2,1.2};h.free[0]=false;
        for(int i=0;i<90;++i) h.tick();
        CHECK(h.hb.drive_status(0).capped && !h.hb.drive_status(0).contact_now);
    }
    {   // drive_stops_at_wall_and_reports_contact: the target 0.35 m beyond a wall; the body never passes it, contact frames rise, no snap
        // while the target is blocked (the clamp's cast does not show it free)
        HandBodyFault::reset_for_tests();DriveRig r;r.w.wall_x=.6;
        r.tick();
        r.target[0]=Vec3{.95,.2,1.2};r.free[0]=false;   // 0.4 m past the wall: past the servo's snap gap
        double max_x=0;
        for(int i=0;i<180;++i) {r.tick();max_x=std::max(max_x,r.w.b[0].pos.x);}
        CHECK(max_x<=.6-.05+1e-9);CHECK(r.hb.drive_status(0).snaps==0);
        CHECK(r.w.destroys==0);CHECK(r.hb.drive_status(0).snap_requests==0);CHECK(r.hb.drive_status(1).contact_frames==0);
        CHECK(!r.hb.drive_status(1).contact_now);   // B15: short of the command for two ticks in a row
        CHECK(r.hb.drive_status(0).gap_m>.2);   // lags behind the controller (Bonelab §2.2), still driven
        CHECK(r.hb.drive_status(0).snap_blocked==0);   // the servo is told the target is blocked: it never even asks for a snap
        // a blocked far gap (the target 1.4 m away inside geometry): no snap request at all, counted as blocked, never a destroy
        r.target[0]=Vec3{2.0,.2,1.2};for(int i=0;i<10;++i) r.tick();
        CHECK(r.hb.drive_status(0).snap_requests==0);CHECK(r.hb.drive_status(0).snap_blocked>=9);CHECK(r.w.destroys==0);
    }
    {   // snap = destroy + recreate at the controller pose, only when the target is free (ruling): the body is stuck behind a wall while the
        // target (on the far side, per the clamp's cast) is free -> after 0.25 s stuck the next idle tick destroys and re-creates it there
        HandBodyFault::reset_for_tests();DriveRig r;r.w.wall_x=.6;
        r.tick();
        r.target[0]=Vec3{1.0,.2,1.2};r.free[0]=true;
        int ticks=0;while(r.hb.drive_status(0).snaps==0 && ticks<120) {r.tick();++ticks;}
        CHECK(r.hb.drive_status(0).snaps==1);CHECK(ticks>=22 && ticks<=40);   // 0.25 s stuck (+ the approach)
        CHECK(r.w.destroys_side[0]==1);CHECK(r.w.creates_side[0]==2);CHECK(length(r.w.b[0].pos-r.target[0])<.02);
        CHECK(r.hb.status(0).destroys==1);CHECK(r.hb.ready(0));CHECK(r.w.destroys_side[1]==0);
        // order in the log: the destroy, then the create, then the wake (re-wake after the recreate), in one tick
        std::size_t d=0;for(std::size_t i=0;i<r.w.log.size();++i) if(r.w.log[i]=="destroy:0") d=i;
        CHECK(d>0 && r.w.log[d+1]=="create:0" && r.w.log[d+2]=="wake:0");
        // blocked: the same stuck body with the target not free never snaps; it keeps servoing (writes continue)
        HandBodyFault::reset_for_tests();DriveRig b;b.w.wall_x=.6;b.tick();
        b.target[0]=Vec3{1.0,.2,1.2};b.free[0]=false;
        const int writes0=b.w.applies_side[0];
        for(int i=0;i<180;++i) b.tick();
        CHECK(b.hb.drive_status(0).snaps==0);CHECK(b.w.destroys==0);CHECK(b.w.applies_side[0]-writes0>=170);
        // free when the request is made, blocked at the next tick: dropped (counted), no destroy
        HandBodyFault::reset_for_tests();DriveRig c;c.w.wall_x=.6;c.tick();
        c.target[0]=Vec3{1.0,.2,1.2};
        while(c.hb.drive_status(0).snap_requests==0) {
            std::array<std::optional<Vec3>,2> spawn{};   // drive says free, the next service sees it blocked
            c.hb.service(gameplay_in(c.ms),spawn);
            for(unsigned side=0;side<2;++side) {DriveInput dd;dd.side=side;dd.target=pose_at(c.target[side]);dd.now=c.t;dd.target_free=true;c.hb.drive(dd);}
            c.w.step();c.t+=FakeDrive::dt;c.ms+=11;
        }
        c.free[0]=false;c.tick();
        CHECK(c.w.destroys==0);CHECK(c.hb.drive_status(0).snaps==0);CHECK(c.hb.drive_status(0).snap_blocked>=1);
        // busy at the snap tick: waits for an idle tick, never a direct destroy while busy
        HandBodyFault::reset_for_tests();DriveRig q;q.w.wall_x=.6;q.tick();q.target[0]=Vec3{1.0,.2,1.2};
        while(q.hb.drive_status(0).snap_requests==0) q.tick();
        q.w.busy=true;for(int i=0;i<5;++i) q.tick();
        CHECK(q.w.destroys==0);q.w.busy=false;q.tick();CHECK(q.hb.drive_status(0).snaps==1);
        // a far gap (Blink, a teleport) snaps at once when free: never flies the body 5 m through the world
        HandBodyFault::reset_for_tests();DriveRig f;f.tick();f.tick();
        f.target[0]=f.target[0]+Vec3{5,0,0};f.tick();f.tick();
        CHECK(f.hb.drive_status(0).snaps==1);CHECK(length(f.w.b[0].pos-f.target[0])<.02);
    }
    {   // create_waits_while_palm_blocked (ruling): no create while the clamp's cast does not show the palm free; created once it does
        HandBodyFault::reset_for_tests();DriveRig r;r.free={false,true};
        for(int i=0;i<60;++i) r.tick();
        CHECK(r.w.creates_side[0]==0);CHECK(r.w.creates_side[1]==1);CHECK(!r.hb.ready(0));
        r.free[0]=true;r.tick();CHECK(r.w.creates_side[0]==1);CHECK(r.hb.ready(0));
    }
    {   // drive_dt_is_real_interval: dt is the time since this side's last drive, not the dispatcher's interval; a clock that does not
        // advance writes nothing; a long gap (a hitch) is clamped by the servo, not refused
        HandBodyFault::reset_for_tests();DriveRig r;r.tick();r.tick();
        const double t0=r.t;
        auto drive_at=[&](double now) {r.hb.service(gameplay_in(r.ms+=11),{r.target[0],r.target[1]});DriveInput d;d.side=0;d.target=pose_at(r.target[0]);d.now=now;d.target_free=true;r.hb.drive(d);r.w.step();};
        drive_at(t0+.007);CHECK(std::abs(r.hb.drive_status(0).dt_s-(t0+.007-(r.t-FakeDrive::dt)))<1e-9);
        drive_at(t0+.007+.019);CHECK(std::abs(r.hb.drive_status(0).dt_s-.019)<1e-9);
        const auto writes=r.hb.drive_status(0).drive_writes;
        drive_at(t0+.007+.019);CHECK(r.hb.drive_status(0).drive_writes==writes);   // no time passed: no write
        drive_at(t0+.5);CHECK(std::abs(r.hb.drive_status(0).dt_s-(.5-.026))<1e-9);CHECK(r.hb.drive_status(0).drive_writes==writes+1);
        CHECK(length(r.w.last_written[0])<=HandDriveConfig{}.max_write_speed+HandDriveConfig{}.gravity/30+1e-9);   // the gravity term uses dt_max
    }
    {   // readback_garbage_latches_body_off (Review Focus 3): NaN velocity, huge velocity, non-finite position: HandBodyFault, no further
        // write, available false; the body is then destroyed once
        for(int kind=0;kind<4;++kind) {   // 3: the service read-back's position only (DeepSeek: that path was not exercised)
            HandBodyFault::reset_for_tests();DriveRig r;r.tick();r.tick();r.tick();
            const int applies=r.w.applies;
            if(kind==0) r.w.garbage_nan=true;else if(kind==1) r.w.garbage_huge=true;else if(kind==2) r.w.garbage_pos=true;else r.w.position_garbage=true;
            r.tick();
            CHECK(HandBodyFault::tripped());CHECK(!r.hb.status(0).available);CHECK(r.hb.status(0).faults+r.hb.status(1).faults>=1);
            CHECK(r.hb.drive_status(0).garbage+r.hb.drive_status(1).garbage>=1);
            const int after=r.w.applies;CHECK(after==applies);   // the garbage tick wrote nothing
            for(int i=0;i<30;++i) r.tick();
            CHECK(r.w.applies==after);CHECK(r.w.destroys==2);CHECK(!r.hb.any_alive());CHECK(r.w.creates==2);
        }
    }
    {   // yield_writes_zero_velocity_and_holds: an authored arm / a lost track: zero velocity plus the gravity term, the body holds still
        HandBodyFault::reset_for_tests();DriveRig r;r.tick();r.tick();
        r.yield[0]=true;const Vec3 at=r.w.b[0].pos;r.target[0]=r.target[0]+Vec3{.3,0,0};
        for(int i=0;i<90;++i) r.tick();
        CHECK(length(r.w.b[0].pos-at)<.001);CHECK(std::abs(r.w.last_written[0].x)<1e-9 && std::abs(r.w.last_written[0].y)<1e-9);
        CHECK(std::abs(r.w.last_written[0].z-9.81*FakeDrive::dt)<1e-6);CHECK(r.hb.drive_status(0).yields==90);CHECK(r.hb.drive_status(0).snaps==0);
        r.yield[0]=false;for(int i=0;i<90;++i) r.tick();CHECK(length(r.w.b[0].pos-r.target[0])<.01);
    }
    {   // cost_budget_recorded: the fake clock advances 7 us per engine call; one drive = motion + apply = 14 us; summed over both hands 28
        HandBodyFault::reset_for_tests();DriveRig r;r.tick();r.tick();r.tick();
        CHECK(r.hb.drive_status(0).cost_us==14);CHECK(r.hb.drive_status(1).cost_us==14);CHECK(r.hb.drive_cost_us()==28);
        r.w.call_us=150;r.tick();CHECK(r.hb.drive_status(0).cost_max_us==300);CHECK(r.hb.drive_cost_max_us()==600);
        r.w.call_us=7;r.tick();CHECK(r.hb.drive_cost_max_us()==600);CHECK(r.hb.drive_cost_us()==28);
    }
    {   // player_drift_reported_not_clamped: the root moves 2 mm while the hand is pressed into the capsule and no locomotion input: reported
        // as 2000 um (above the 1000 um proof bound: the metric is reported, never clamped); with locomotion input it is explained (not counted)
        HandBodyFault::reset_for_tests();DriveRig r;r.root=Vec3{0,0,0};r.target[0]=Vec3{.1,.1,1.0};r.target[1]=Vec3{.6,-.2,1.2};r.tick();
        for(int i=0;i<30;++i) r.tick();
        CHECK(r.hb.drive_status(0).player_drift_um==0);
        for(int i=0;i<20;++i) {r.root=Vec3{.0001*(i+1),0,0};r.tick();}
        CHECK(r.hb.drive_status(0).player_drift_um==2000);CHECK(r.hb.drive_status(1).player_drift_um==0);   // hand 1 is outside the capsule
        CHECK(r.hb.drive_status(0).pressed_ticks>=45);CHECK(r.hb.drive_status(1).pressed_ticks==0);
        CHECK(length(r.w.b[0].pos-r.target[0])<.01);   // the drive was not altered by the metric
        DriveRig l;l.root=Vec3{0,0,0};l.target[0]=Vec3{.1,.1,1.0};l.locomotion=true;l.tick();
        for(int i=0;i<20;++i) {l.root=Vec3{.01*(i+1),0,0};l.tick();}
        CHECK(l.hb.drive_status(0).player_drift_um==0);CHECK(l.hb.drive_status(0).pressed_ticks==0);   // locomotion: not a pressed tick
        // a root jump (Blink, a teleport: 1 m in one tick, no stick) opens a new window, it is not drift (DeepSeek P2); counted
        DriveRig j;j.root=Vec3{0,0,0};j.target[0]=Vec3{.1,.1,1.0};j.tick();for(int i=0;i<5;++i) j.tick();
        j.root=Vec3{.3,0,0};j.tick();for(int i=0;i<5;++i) j.tick();   // 0.3 m in one tick, the body still inside the capsule
        CHECK(j.hb.drive_status(0).player_drift_um==0);CHECK(j.hb.drive_status(0).root_jumps==1);
    }
    {   // fix round 1 (proof d instrumentation): CPU time and wall time per drive, a tick histogram, over-budget counts and the slowest tick's
        // phase split. A preemption (wall time with no CPU time) inside the write shows as wall over budget, CPU under it, and lands in "write".
        HandBodyFault::reset_for_tests();DriveRig r;r.tick();r.tick();r.tick();
        CHECK(r.hb.drive_status(0).cpu_us==14);CHECK(r.hb.drive_status(1).cpu_us==14);
        for(int i=0;i<20;++i) r.tick();
        r.w.preempt_us=300;r.tick();r.w.preempt_us=0;r.tick();r.hb.file_drive_tick();
        const auto t=r.hb.drive_timing();
        CHECK(t.cpu_available);CHECK(t.cycles_per_us==3);
        CHECK(t.over_wall==1);CHECK(t.over_cpu==0);CHECK(t.cpu_max_us==28);CHECK(t.wall_max_us==628);   // both sides preempted in that tick
        CHECK(t.slow_wall_us==628);CHECK(t.slow_cpu_us==28);CHECK(t.slow_read_us==14);CHECK(t.slow_write_us==614);CHECK(t.slow_servo_us==0);CHECK(t.slow_other_us==0);
        CHECK(t.hist[2]>=20);CHECK(t.hist[8]==1);   // 28 us ticks in [20,50); the preempted 628 us tick in [500,1000)
        CHECK(t.ticks==t.hist[0]+t.hist[1]+t.hist[2]+t.hist[3]+t.hist[4]+t.hist[5]+t.hist[6]+t.hist[7]+t.hist[8]+t.hist[9]);
        CHECK(r.hb.drive_status(0).cpu_max_us==14);CHECK(r.hb.drive_status(0).cost_max_us==314);
        // real CPU work over budget counts as CPU over budget
        r.w.call_us=120;r.tick();r.w.call_us=7;r.tick();r.hb.file_drive_tick();
        CHECK(r.hb.drive_timing().over_cpu==1);CHECK(r.hb.drive_timing().cpu_max_us==480);
        // CPU time unavailable (QueryThreadCycleTime fails under Wine): reported as such, never counted as zero-cost CPU over budget
        HandBodyFault::reset_for_tests();DriveRig u;u.w.cpu_ok=false;u.tick();u.tick();u.w.call_us=150;u.tick();u.hb.file_drive_tick();
        CHECK(!u.hb.drive_timing().cpu_available);CHECK(u.hb.drive_timing().over_cpu==0);CHECK(u.hb.drive_status(0).cpu_us==0);CHECK(u.hb.drive_timing().over_wall==1);
    }
    {   // review P2-2: the drift proof's positive control: the root moving under locomotion input is counted (and its travel), a root that
        // never changes under locomotion is not (a stale root fails the soak checker's root_moved_ticks > 0)
        HandBodyFault::reset_for_tests();DriveRig r;r.root=Vec3{0,0,0};r.tick();r.tick();
        r.locomotion=true;for(int i=0;i<30;++i) {r.root=Vec3{.01*(i+1),0,0};r.tick();}
        CHECK(r.hb.drive_status(0).root_moved_ticks>=29);CHECK(r.hb.drive_status(0).root_travel_um>=290000);
        DriveRig z;z.root=Vec3{0,0,0};z.tick();z.locomotion=true;for(int i=0;i<30;++i) z.tick();
        CHECK(z.hb.drive_status(0).root_moved_ticks==0);CHECK(z.hb.drive_status(0).root_travel_um==0);
        DriveRig j;j.root=Vec3{0,0,0};j.tick();j.locomotion=true;j.tick();j.root=Vec3{5,0,0};j.tick();   // a Blink under locomotion: not "moved"
        CHECK(j.hb.drive_status(0).root_moved_ticks==0);
    }
    {   // write() shares the one-write-per-tick rule with the drive (DeepSeek P3): a write after this tick's drive write is refused
        HandBodyFault::reset_for_tests();DriveRig r;r.tick();r.tick();
        const auto h=r.hb.status(0).handle;const int applies=r.w.applies;
        r.hb.service(gameplay_in(r.ms),{r.target[0],r.target[1]});
        DriveInput d;d.side=0;d.target=pose_at(r.target[0]);d.now=r.t;d.target_free=true;r.hb.drive(d);
        CHECK(r.w.applies==applies+1);
        CHECK(!r.hb.write(0,velocity_record(h)));CHECK(r.w.applies==applies+1);
        r.hb.service(gameplay_in(r.ms+11),{r.target[0],r.target[1]});CHECK(r.hb.write(0,velocity_record(h)));   // a new tick
        d.now=r.t+.011;r.hb.drive(d);CHECK(r.w.applies==applies+2);   // ... and the drive in that tick then writes nothing
    }
    {   // a motion that never reads (a bad motion index) latches after max_unreadable_motion drives; a short gap does not
        HandBodyFault::reset_for_tests();DriveRig r;r.tick();r.tick();
        r.w.motion_unreadable=true;for(int i=0;i<10;++i) r.tick();
        CHECK(!HandBodyFault::tripped());r.w.motion_unreadable=false;r.tick();CHECK(r.hb.status(0).available);
        r.w.motion_unreadable=true;for(int i=0;i<static_cast<int>(HandDriveConfig{}.max_unreadable_motion);++i) r.tick();
        CHECK(HandBodyFault::tripped());CHECK(!r.hb.status(0).available);
    }
    {   // knob off: a disabled manager never drives
        HandBodyFault::reset_for_tests();FakeDrive w;HandBody hb(w.engine(),false);
        for(int i=0;i<10;++i) {hb.service(gameplay_in(1000+i*11),{Vec3{.4,.2,1.2},Vec3{.4,-.2,1.2}});DriveInput d;d.target=pose_at({.4,.2,1.2});d.now=1+i*.011;hb.drive(d);}
        CHECK(w.creates==0);CHECK(w.applies==0);CHECK(w.motions==0);
    }
    {   // the drive refuses outside the G1 point and from inside service() (re-entry), like write()
        HandBodyFault::reset_for_tests();DriveRig r;r.tick();r.tick();
        bool point=false;auto e=r.w.engine();e.at_g1_point=[&] {return point;};HandBody hb(e);
        hb.service(gameplay_in(1000),{Vec3{.4,.2,1.2},Vec3{.4,-.2,1.2}});
        DriveInput d;d.target=pose_at({.4,.2,1.2});d.now=1;hb.drive(d);d.now=1.1;hb.drive(d);
        CHECK(hb.drive_status(0).drives==0);CHECK(hb.status(0).misplaced_writes==2);
    }
    HandBodyFault::reset_for_tests();
}

int main() {
    {   // parse_spike_route
        CHECK(parse_spike_route("R1")==SpikeRoute::R1);CHECK(parse_spike_route(" R2\r\n")==SpikeRoute::R2);CHECK(parse_spike_route("R3\n")==SpikeRoute::R3);
        CHECK(!parse_spike_route(""));CHECK(!parse_spike_route("r1"));CHECK(!parse_spike_route("R4"));CHECK(!parse_spike_route("R1 R2"));
    }
    {   // spike_creates_falls_destroys
        HandBodyFault::reset_for_tests();FakeWorld f;auto e=f.engine();
        const auto r=run_spike(e,SpikeRoute::R1,kAt,0,12);
        CHECK(r.created);CHECK(r.settled);CHECK(std::abs(r.settle_z-.05)<=.01);CHECK(r.collided_with_floor);
        CHECK(r.destroyed);CHECK(!r.faulted);CHECK(f.creates==1);CHECK(f.destroys==1);CHECK(!f.alive);CHECK(!r.flags_after_destroy);
        CHECK(r.pushes>200);CHECK(r.player_drift_m==0);CHECK(r.destroyed_at_s>=12);CHECK(!HandBodyFault::tripped());
    }
    {   // a body created asleep: the wake write makes it fall; settling is never judged on a body that has not moved
        FakeWorld f;f.sleeps=true;auto e=f.engine();
        const auto r=run_spike(e,SpikeRoute::R1,kAt,0,12);
        CHECK(r.woke);CHECK(r.moved);CHECK(r.settled);CHECK(std::abs(r.settle_z-.05)<=.01);CHECK(r.collided_with_floor);CHECK(r.destroyed);
    }
    {   // a body that never moves (no wake effect): not settled, no floor claim
        FakeWorld f;f.sleeps=true;auto e=f.engine();e.push=[](std::uint32_t,Vec3) {return true;};   // writes accepted but the body stays asleep
        SpikeConfig c;c.life_s=6;SpikeRunner run(SpikeRoute::R1,kAt,0,c);double t=0;
        for(;t<5 && !run.done();t+=1./60) run.tick(e,t);
        CHECK(run.report().created);CHECK(!run.report().moved);CHECK(!run.report().settled);CHECK(!run.report().collided_with_floor);
    }
    {   // floor not where the body rests: no collision claimed
        FakeWorld f;auto e=f.engine();const auto r=run_spike(e,SpikeRoute::R1,kAt,-.5,8);
        CHECK(r.created && r.settled && !r.collided_with_floor && r.destroyed);
    }
    {   // spike_refuses_while_busy: retries when idle, never creates while busy
        FakeWorld f;f.busy_until=90;auto e=f.engine();
        const auto r=run_spike(e,SpikeRoute::R1,kAt,0,8);
        CHECK(r.created);CHECK(f.creates==1);CHECK(f.creates_while_busy==0);CHECK(r.busy_waits>=80);CHECK(r.destroyed);
    }
    {   // busy at the moment of destroy: never destroyed while busy; destroyed once idle
        FakeWorld f;auto e=f.engine();SpikeConfig c;c.life_s=6;SpikeRunner run(SpikeRoute::R1,kAt,0,c);
        double t=0;for(;t<6.5 && !run.done();t+=1./60) {f.busy_until=f.tick+1000;if(t<5.9) f.busy_until=-1;run.tick(e,t);}
        CHECK(!run.done());CHECK(f.destroys==0);
        f.busy_until=-1;for(int i=0;i<5 && !run.done();++i) {run.tick(e,t);t+=1./60;}
        CHECK(run.done());CHECK(f.destroys==1);CHECK(f.destroys_while_busy==0);CHECK(run.report().destroyed);
    }
    {   // spike_fault_latches_and_stops: no destroy attempt on a handle never created
        HandBodyFault::reset_for_tests();FakeWorld f;f.fault_create=1;auto e=f.engine();
        const auto r=run_spike(e,SpikeRoute::R1,kAt,0,8);
        CHECK(r.faulted);CHECK(!r.created);CHECK(HandBodyFault::tripped());CHECK(f.creates==1);CHECK(f.destroys==0);CHECK(f.pushes==0);
    }
    {   // a fault after creation: writes stop, the body gets exactly one destroy (no body outlives the session)
        HandBodyFault::reset_for_tests();FakeWorld f;f.fault_push_at=300;auto e=f.engine();
        const auto r=run_spike(e,SpikeRoute::R1,kAt,0,12);
        CHECK(r.faulted);CHECK(r.created);CHECK(HandBodyFault::tripped());CHECK(f.destroys==1);CHECK(r.destroyed);
        const int pushes=f.pushes;CHECK(pushes>=1);CHECK(r.destroyed_at_s<6);   // stopped long before the timeline end
        HandBodyFault::reset_for_tests();
    }
    {   // spike_never_writes_after_world_change: abandon, no engine call afterwards, the destroy is skipped
        FakeWorld f;f.world_change_at=200;auto e=f.engine();
        const auto r=run_spike(e,SpikeRoute::R1,kAt,0,12);
        CHECK(r.created);CHECK(r.abandoned);CHECK(!r.destroyed);CHECK(!r.destroy_attempted);CHECK(f.destroys==0);CHECK(f.calls_after_change==0);
    }
    {   // the body's record vanishes while the world pointer stays: treated as gone, no engine write after it
        FakeWorld f;auto e=f.engine();SpikeRunner run(SpikeRoute::R1,kAt,0);
        double t=0;for(int i=0;i<120;++i,t+=1./60) run.tick(e,t);
        CHECK(run.report().created);f.alive=false;const int pushes=f.pushes;
        for(int i=0;i<600 && !run.done();++i,t+=1./60) run.tick(e,t);
        CHECK(run.done());CHECK(run.report().abandoned);CHECK(f.destroys==0);CHECK(f.pushes==pushes);
    }
    {   // a destroy that faults: latched, not reported destroyed, never retried
        HandBodyFault::reset_for_tests();FakeWorld f;f.fault_destroy=1;auto e=f.engine();
        const auto r=run_spike(e,SpikeRoute::R1,kAt,0,6);
        CHECK(r.created);CHECK(r.faulted);CHECK(!r.destroyed);CHECK(f.destroys==1);CHECK(HandBodyFault::tripped());
        HandBodyFault::reset_for_tests();
    }
    {   // a destroy refused without an engine call (busy race, unreadable): retried at the next idle tick, then done
        FakeWorld f;f.refuse_destroys=3;auto e=f.engine();
        const auto r=run_spike(e,SpikeRoute::R1,kAt,0,6);
        CHECK(r.destroyed);CHECK(f.destroys==4);CHECK(r.destroy_refusals==3);CHECK(!f.alive);
    }
    {   // a createBody that faults after inserting its record: the body is tracked and gets its one destroy attempt (when idle)
        HandBodyFault::reset_for_tests();FakeWorld f;f.fault_create=1;f.create_fault_leaves_body=true;auto e=f.engine();
        const auto r=run_spike(e,SpikeRoute::R1,kAt,0,6);
        CHECK(r.faulted);CHECK(r.created);CHECK(f.destroys==1);CHECK(!f.alive);CHECK(r.destroyed);CHECK(f.pushes==0);CHECK(HandBodyFault::tripped());
        HandBodyFault::reset_for_tests();
    }
    {   // an unreadable position for a while is not "gone": the tick is skipped, the body is destroyed normally later
        FakeWorld f;f.unreadable_from=100;f.unreadable_to=160;auto e=f.engine();
        const auto r=run_spike(e,SpikeRoute::R1,kAt,0,8);
        CHECK(!r.abandoned);CHECK(r.unreadable_ticks>=55);CHECK(r.destroyed);CHECK(f.destroys==1);
    }
    {   // spike_reports_player_drift: a 3 mm player move during the press is reported, not hidden
        FakeWorld f;f.player_push_at=200;auto e=f.engine();   // tick 200 (3.3 s): inside the 5 s press (settle ends near 0.8 s)
        const auto r=run_spike(e,SpikeRoute::R1,kAt,0,12);
        CHECK(r.created);CHECK(r.player_drift_m>=.0029 && r.player_drift_m<=.0031);CHECK(r.destroyed);
    }
    {   // loss of gameplay destroys at once (pause menu), with the same world
        FakeWorld f;auto e=f.engine();bool gameplay=true;e.gameplay=[&] {return gameplay;};SpikeRunner run(SpikeRoute::R1,kAt,0);
        double t=0;for(int i=0;i<120;++i,t+=1./60) run.tick(e,t);
        gameplay=false;run.tick(e,t);CHECK(run.done());CHECK(f.destroys==1);CHECK(run.report().destroyed);
    }
    {   // R2 and R3: return false with the B6 reason, no engine call at all
        for(auto route:{SpikeRoute::R2,SpikeRoute::R3}) {
            FakeWorld f;auto e=f.engine();const auto r=run_spike(e,route,kAt,0,8);
            CHECK(r.refused);CHECK(!r.created);CHECK(f.creates==0);CHECK(f.tick==0);CHECK(!r.reason.empty());CHECK(spike_route_refusal(route)!=nullptr);
        }
        CHECK(spike_route_refusal(SpikeRoute::R1)==nullptr);
    }
    {   // judge_g1
        CHECK(!judge_g1(good_g1(3),8).decided);
        CHECK(judge_g1(good_g1(),8).pass);
        auto s=good_g1();s.dispatch_off_frame_thread=1;CHECK(judge_g1(s,8).decided && !judge_g1(s,8).pass);
        s=good_g1();s.dispatch_overlap=1;CHECK(!judge_g1(s,8).pass);
        s=good_g1();s.dispatch_busy=1;CHECK(!judge_g1(s,8).pass);
        s=good_g1();s.step_busy=1;CHECK(!judge_g1(s,8).pass);
        s=good_g1();s.frame_threads=2;CHECK(!judge_g1(s,8).pass);
        s=good_g1();s.dispatch_outside_step=0;CHECK(!judge_g1(s,8).pass);
        s=good_g1();s.steps=0;CHECK(!judge_g1(s,8).pass);
        s=good_g1();s.frames=0;CHECK(!judge_g1(s,8).pass);
        s=good_g1();s.dispatch_busy_samples=0;CHECK(judge_g1(s,8).decided && !judge_g1(s,8).pass);   // an unreadable busy flag is no evidence
        auto later=good_g1(20);later.dispatches+=1000;later.dispatch_step_drain+=50;CHECK(g1_still_holds(good_g1(),later));
        later.dispatch_overlap=1;CHECK(!g1_still_holds(good_g1(),later));
    }
    {   // owner choice and fallback
        CHECK(choose_owner(good_owner()).owner==SpikeOwner::player_a);
        auto o=good_owner();o.a_type_in_range=true;CHECK(choose_owner(o).owner==SpikeOwner::none);
        o.b_available=true;CHECK(choose_owner(o).owner==SpikeOwner::prop_b);
        o=good_owner();o.a_entity_is_player=false;CHECK(choose_owner(o).owner==SpikeOwner::none);
        o=good_owner();o.a_type_decoded=false;CHECK(choose_owner(o).owner==SpikeOwner::none);
        CHECK(!g2_rejects({}));CHECK(g2_rejects({1,0}));CHECK(g2_rejects({0,1}));
        CHECK(owner_after_g2_reject(SpikeOwner::player_a,good_owner()).owner==SpikeOwner::none);
        o=good_owner();o.b_available=true;CHECK(owner_after_g2_reject(SpikeOwner::player_a,o).owner==SpikeOwner::prop_b);
        CHECK(owner_after_g2_reject(SpikeOwner::prop_b,o).owner==SpikeOwner::none);
    }
    {   // phase order: no create while G1 is still observing
        FakeWorld f;auto e=f.engine();SpikeSession s(SpikeRoute::R1,8);
        for(int i=0;i<400;++i) s.tick(e,session_input(i/60.,good_g1(i/60.),good_owner()));
        CHECK(f.creates==0);CHECK(s.phase()==SpikeSession::Phase::observe);
        for(int i=400;i<600;++i) s.tick(e,session_input(i/60.,good_g1(i/60.),good_owner()));
        CHECK(f.creates==1);CHECK(s.owner()==SpikeOwner::player_a);
    }
    {   // phase order: G1 failing means no create, ever
        FakeWorld f;auto e=f.engine();SpikeSession s(SpikeRoute::R1,8);auto g=good_g1();g.dispatch_off_frame_thread=3;
        for(int i=0;i<1200;++i) s.tick(e,session_input(10+i/60.,g,good_owner()));
        CHECK(f.creates==0);CHECK(f.tick==0);CHECK(s.phase()==SpikeSession::Phase::done);CHECK(s.outcome().find("G1 failed")!=std::string::npos);
    }
    {   // phase order: no owner means no create (NO-GO)
        FakeWorld f;auto e=f.engine();SpikeSession s(SpikeRoute::R1,8);auto o=good_owner();o.a_resolved=false;
        for(int i=0;i<600;++i) s.tick(e,session_input(10+i/60.,good_g1(),o));
        CHECK(f.creates==0);CHECK(s.outcome().find("NO-GO")!=std::string::npos);
    }
    {   // phase order: waits for the player before creating
        FakeWorld f;auto e=f.engine();SpikeSession s(SpikeRoute::R1,8);
        for(int i=0;i<60;++i) {auto in=session_input(10+i/60.,good_g1(),good_owner());in.player_root.reset();s.tick(e,in);}
        CHECK(f.creates==0);CHECK(s.phase()==SpikeSession::Phase::owner);
        s.tick(e,session_input(11.5,good_g1(),good_owner()));CHECK(f.creates==1);
        CHECK(std::abs(f.pos.x-.45)<1e-9 && std::abs(f.pos.z-.35)<.01);   // in front of the player, 0.30 m above the floor
    }
    {   // G2 rejects owner (a) at run time: the body is destroyed, (b) unavailable gives NO-GO; counts before creation do not count
        FakeWorld f;auto e=f.engine();SpikeSession s(SpikeRoute::R1,8);G2Counts g{5,0};
        double t=10;int i=0;
        for(;i<120;++i,t+=1./60) s.tick(e,session_input(t,good_g1(),good_owner(),g));
        CHECK(f.creates==1);CHECK(!s.g2_rejected());CHECK(f.alive);
        g.wake_owner=6;
        for(;i<300 && s.phase()!=SpikeSession::Phase::done;++i,t+=1./60) s.tick(e,session_input(t,good_g1(),good_owner(),g));
        CHECK(s.g2_rejected());CHECK(f.destroys==1);CHECK(!f.alive);CHECK(s.outcome().find("NO-GO")!=std::string::npos);
    }
    {   // G2 events during the create tick count (the engine's counters taken just before createBody are the base)
        FakeWorld f;auto e=f.engine();SpikeSession s(SpikeRoute::R1,8);double t=10;bool created=false;
        for(int i=0;i<60 && !created;++i,t+=1./60) {
            auto in=session_input(t,good_g1(),good_owner(),{4,0});in.g2_at_create=G2Counts{3,0};   // one wake during the create call
            s.tick(e,in);created=f.creates==1;
        }
        CHECK(created);auto in=session_input(t,good_g1(),good_owner(),{4,0});in.g2_at_create=G2Counts{3,0};
        for(int i=0;i<10 && s.phase()!=SpikeSession::Phase::done;++i,t+=1./60) {in.now_s=t;s.tick(e,in);}
        CHECK(s.g2_rejected());CHECK(f.destroys==1);
    }
    {   // G1 violated after the pass: the body is destroyed at the next allowed tick
        FakeWorld f;auto e=f.engine();SpikeSession s(SpikeRoute::R1,8);double t=10;
        for(int i=0;i<120;++i,t+=1./60) s.tick(e,session_input(t,good_g1(),good_owner()));
        CHECK(f.alive);auto g=good_g1();g.dispatch_overlap=1;
        for(int i=0;i<10 && s.phase()!=SpikeSession::Phase::done;++i,t+=1./60) s.tick(e,session_input(t,g,good_owner()));
        CHECK(!f.alive);CHECK(f.destroys==1);CHECK(s.runner()->report().destroyed);
    }
    {   // session stop before creation: nothing created
        FakeWorld f;auto e=f.engine();SpikeSession s(SpikeRoute::R1,8);
        s.tick(e,session_input(1,good_g1(1),good_owner()));s.request_stop("session end");
        for(int i=0;i<100;++i) s.tick(e,session_input(12+i/60.,good_g1(),good_owner()));
        CHECK(f.creates==0);CHECK(s.phase()==SpikeSession::Phase::done);
    }
    {   // session stop after creation: destroyed
        FakeWorld f;auto e=f.engine();SpikeSession s(SpikeRoute::R1,8);double t=10;
        for(int i=0;i<60;++i,t+=1./60) s.tick(e,session_input(t,good_g1(),good_owner()));
        CHECK(f.alive);s.request_stop("session end");s.tick(e,session_input(t,good_g1(),good_owner()));
        CHECK(!f.alive);CHECK(s.phase()==SpikeSession::Phase::done);
    }
    {   // R2/R3 sessions never observe, never call
        FakeWorld f;auto e=f.engine();SpikeSession s(SpikeRoute::R3,8);
        CHECK(!s.tick(e,session_input(10,good_g1(),good_owner())));CHECK(f.tick==0);CHECK(s.outcome().find("R3")!=std::string::npos);
    }
    {   // the filter word the real engine gives the body: hand_filter_word(false) never pairs with the live player word (B7 0x0838c566)
        CHECK(hand_body::hand_filter_word(false)==0x000885e4u);
        CHECK(!hand_body::layers_collide(hand_body::hand_filter_word(false),0x0838c566u));
    }
    b13_checks();
    b14_checks();
    b16_checks();
    std::printf("hand_body_check: all passed\n");
    return 0;
}
