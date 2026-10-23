// Plan B task B11: the hand-body creation spike against a fake engine (the seam is grown in B13).
#define D2VR_TESTING
#include "hand_body_spike.hpp"
#include <cstdio>
#include <cstdlib>
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
    bool busy_now=false;
    int creates=0,destroys=0,pushes=0,reads=0,creates_while_busy=0,destroys_while_busy=0,calls_after_change=0;
    bool changed=false;
    SpikeEngine engine() {
        SpikeEngine e;
        e.world=[this] {
            ++tick;busy_now=tick<busy_until;
            if(world_change_at>=0 && tick>=world_change_at && !changed) {changed=true;world_id=0x2000;alive=false;}
            if(player_push_at>=0 && tick==player_push_at) player.x+=.003;
            if(alive) {   // integrate: gravity, floor contact, the pushed horizontal velocity
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
            alive=true;handle=next;pos=at;vel={};radius=r;h=handle;return true;
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
            vel.x=v.x;vel.y=v.y;return true;
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
    std::printf("hand_body_check: all passed\n");
    return 0;
}
