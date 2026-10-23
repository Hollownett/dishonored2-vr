#include "hand_body_lifecycle.hpp"
#include <cstdio>
#include <cstdlib>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
LifecycleInput good(std::uintptr_t w=0x1000,std::uint32_t g=1,std::uint64_t t=0) {
    LifecycleInput i;i.world=w;i.world_generation=g;i.gameplay=i.focused=i.tracked=true;i.now_ms=t;return i;
}
// bring side to ready with world w, handle h
void make_ready(HandBodyLifecycle& l,unsigned side,std::uintptr_t w=0x1000,std::uint32_t h=0x01000005) {
    CHECK(l.update(side,good(w))==BodyAction::create);l.created(side,w,h);CHECK(l.state(side)==BodyState::ready);
}
}
int main() {
    {   // create_waits_for_idle_physics
        HandBodyLifecycle l;auto i=good();i.physics_busy=true;
        CHECK(l.update(0,i)==BodyAction::none);i.physics_busy=false;CHECK(l.update(0,i)==BodyAction::create);
    }
    {   // create needs every precondition
        HandBodyLifecycle l;
        auto i=good();i.gameplay=false;CHECK(l.update(0,i)==BodyAction::none);
        i=good();i.focused=false;CHECK(l.update(0,i)==BodyAction::none);
        i=good();i.tracked=false;CHECK(l.update(0,i)==BodyAction::none);
        i=good();i.shutdown=true;CHECK(l.update(0,i)==BodyAction::none);
        i=good(0);CHECK(l.update(0,i)==BodyAction::none);
        CHECK(l.update(0,good())==BodyAction::create);
    }
    {   // world_change_destroys (and generation change)
        HandBodyLifecycle l;make_ready(l,0);
        CHECK(l.update(0,good(0x1000))==BodyAction::none);
        CHECK(l.update(0,good(0x2000))==BodyAction::destroy);CHECK(l.state(0)==BodyState::destroying);
        HandBodyLifecycle m;make_ready(m,0);
        CHECK(m.update(0,good(0x1000,2))==BodyAction::destroy);
    }
    {   // stale_world_never_writes: false the instant update sees the change, before destroyed()
        HandBodyLifecycle l;make_ready(l,0,0x1000,7);
        CHECK(l.can_write(0,0x1000,7));
        l.update(0,good(0x2000));
        CHECK(!l.can_write(0,0x1000,7));CHECK(!l.can_write(0,0x2000,7));
        HandBodyLifecycle m;make_ready(m,0,0x1000,7);
        m.update(0,good(0x1000,2));CHECK(!m.can_write(0,0x1000,7));
        HandBodyLifecycle n;make_ready(n,0,0x1000,7);
        CHECK(!n.can_write(0,0x2000,7));CHECK(!n.can_write(0,0,0));
    }
    {   // recycled_handle_refused
        HandBodyLifecycle l;make_ready(l,0,0x1000,0x01000005);
        CHECK(!l.can_write(0,0x1000,0x02000005));   // same index, new generation
        CHECK(!l.can_write(0,0x1000,0x01000006));
        CHECK(!l.can_write(0,0x3000,0x01000005));   // same handle number, other world
        l.update(0,good(0x3000,1));l.destroyed(0);
        CHECK(l.update(0,good(0x3000,1))==BodyAction::create);l.created(0,0x3000,0x01000005);
        CHECK(!l.can_write(0,0x1000,0x01000005));CHECK(l.can_write(0,0x3000,0x01000005));
        // new generation, same world, same handle: refused the instant update sees it
        l.update(0,good(0x3000,2));CHECK(!l.can_write(0,0x3000,0x01000005));
    }
    {   // can_write false in every state but ready
        HandBodyLifecycle l;CHECK(!l.can_write(0,0x1000,5));
        make_ready(l,0,0x1000,5);CHECK(l.can_write(0,0x1000,5));
        auto i=good();i.gameplay=false;l.update(0,i);CHECK(l.state(0)==BodyState::destroying);CHECK(!l.can_write(0,0x1000,5));
        l.destroyed(0);CHECK(l.state(0)==BodyState::absent);CHECK(!l.can_write(0,0x1000,5));
    }
    {   // load_screen_destroys_then_recreates_once_gameplay
        HandBodyLifecycle l;make_ready(l,0);
        auto i=good();i.gameplay=false;
        CHECK(l.update(0,i)==BodyAction::destroy);l.destroyed(0);
        CHECK(l.update(0,i)==BodyAction::none);
        i=good(0x5000,2);i.focused=false;CHECK(l.update(0,i)==BodyAction::none);
        i=good(0x5000,2);i.physics_busy=true;CHECK(l.update(0,i)==BodyAction::none);
        i.physics_busy=false;CHECK(l.update(0,i)==BodyAction::create);
        l.created(0,0x5000,9);CHECK(l.can_write(0,0x5000,9));CHECK(l.update(0,i)==BodyAction::none);
    }
    {   // focus_loss_grace_1s
        HandBodyLifecycle l;make_ready(l,0);
        auto i=good(0x1000,1,10000);i.focused=false;CHECK(l.update(0,i)==BodyAction::none);
        i.now_ms=10500;CHECK(l.update(0,i)==BodyAction::none);CHECK(l.can_write(0,0x1000,0x01000005));
        i=good(0x1000,1,10600);CHECK(l.update(0,i)==BodyAction::none);   // blink over
        i.focused=false;i.now_ms=11000;CHECK(l.update(0,i)==BodyAction::none);   // timer restarted at 11000
        i.now_ms=12100;CHECK(l.update(0,i)==BodyAction::destroy);
        HandBodyLifecycle m;make_ready(m,0);
        i=good(0x1000,1,0);i.focused=false;m.update(0,i);i.now_ms=1100;CHECK(m.update(0,i)==BodyAction::destroy);
    }
    {   // tracking_loss_same
        HandBodyLifecycle l;make_ready(l,0);
        auto i=good(0x1000,1,5000);i.tracked=false;CHECK(l.update(0,i)==BodyAction::none);
        i.now_ms=5500;CHECK(l.update(0,i)==BodyAction::none);
        i.now_ms=6100;CHECK(l.update(0,i)==BodyAction::destroy);
        HandBodyLifecycle m;make_ready(m,0);
        i=good(0x1000,1,100);i.tracked=false;m.update(0,i);
        i.tracked=true;i.now_ms=700;m.update(0,i);CHECK(m.state(0)==BodyState::ready);
    }
    {   // fault_latches_off_for_session
        HandBodyLifecycle l;make_ready(l,0);
        auto i=good();i.fault=true;
        CHECK(l.update(0,i)==BodyAction::destroy);CHECK(l.state(0)==BodyState::destroying);CHECK(!l.can_write(0,0x1000,0x01000005));
        l.destroyed(0);CHECK(l.state(0)==BodyState::faulted);
        i=good();CHECK(l.update(0,i)==BodyAction::none);
        i=good(0x9000,7);CHECK(l.update(0,i)==BodyAction::none);   // not even after a world change
        CHECK(l.state(0)==BodyState::faulted);
        // fault while absent latches too; fault bit need not persist in the input
        HandBodyLifecycle m;i=good();i.fault=true;CHECK(m.update(0,i)==BodyAction::none);CHECK(m.state(0)==BodyState::faulted);
        CHECK(m.update(0,good())==BodyAction::none);
    }
    {   // shutdown_destroys_even_when_busy: destroy_queued tells the caller to use the queue
        HandBodyLifecycle l;make_ready(l,0);
        auto i=good();i.shutdown=true;i.physics_busy=true;
        CHECK(l.update(0,i)==BodyAction::destroy_queued);CHECK(!l.can_write(0,0x1000,0x01000005));
        HandBodyLifecycle m;make_ready(m,0);i.physics_busy=false;CHECK(m.update(0,i)==BodyAction::destroy);
        HandBodyLifecycle n;make_ready(n,0);i=good(0x2000);i.physics_busy=true;CHECK(n.update(0,i)==BodyAction::destroy_queued);
    }
    {   // pending destroy never returns create until destroyed(); not re-issued either
        HandBodyLifecycle l;make_ready(l,0);
        auto i=good(0x2000);i.physics_busy=true;CHECK(l.update(0,i)==BodyAction::destroy_queued);
        i.physics_busy=false;
        for(int k=0;k<3;++k) CHECK(l.update(0,i)==BodyAction::none);
        CHECK(l.state(0)==BodyState::destroying);
        l.destroyed(0);CHECK(l.update(0,i)==BodyAction::create);
    }
    {   // after a world change, create returns once gameplay, focus, tracking and idle are back
        HandBodyLifecycle l;make_ready(l,0);
        CHECK(l.update(0,good(0x2000))==BodyAction::destroy);l.destroyed(0);
        auto i=good(0x2000);i.gameplay=false;CHECK(l.update(0,i)==BodyAction::none);
        i=good(0x2000);i.tracked=false;CHECK(l.update(0,i)==BodyAction::none);
        CHECK(l.update(0,good(0x2000))==BodyAction::create);
    }
    {   // two_sides_independent
        HandBodyLifecycle l;make_ready(l,0,0x1000,5);make_ready(l,1,0x1000,6);
        auto i=good();i.fault=true;l.update(0,i);l.destroyed(0);
        CHECK(l.state(0)==BodyState::faulted);CHECK(l.state(1)==BodyState::ready);CHECK(l.can_write(1,0x1000,6));
        CHECK(!l.can_write(1,0x1000,5));CHECK(l.update(1,good())==BodyAction::none);
        CHECK(l.update(2,good())==BodyAction::none);CHECK(!l.can_write(2,0x1000,6));
    }
    {   // disabled_never_creates (and destroys a live body)
        HandBodyLifecycle l;auto i=good();i.enabled=false;
        for(int k=0;k<3;++k) CHECK(l.update(0,i)==BodyAction::none);
        make_ready(l,1);CHECK(l.update(1,i)==BodyAction::destroy);
    }
    std::puts("hand_body_lifecycle_check ok");
    return 0;
}
