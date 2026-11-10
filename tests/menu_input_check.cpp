#include "../src/menu_input.hpp"
#include <array>
#include <cassert>
#include <limits>
#include <iostream>
int main() {
    using C=d2vr::MenuClick;C c;d2vr::NativeMenuInput in{.5,.4,100,1,true,true};
    assert(c.update(in,100)==C::Event::none);
    assert(c.update(in,101)==C::Event::none);
    in.trigger=false;assert(c.update(in,102)==C::Event::none);
    in.trigger=true;assert(c.update(in,103)==C::Event::down);
    assert(c.update(in,104)==C::Event::none);
    in.active=false;assert(c.update(in,105)==C::Event::up);
    in.active=true;assert(c.update(in,106)==C::Event::none);
    in.trigger=false;c.update(in,107);in.trigger=true;assert(c.update(in,108)==C::Event::down);
    in.epoch=2;assert(c.update(in,109)==C::Event::up);
    assert(c.update(in,110)==C::Event::none);in.trigger=false;c.update(in,111);
    in.trigger=true;assert(c.update(in,112)==C::Event::down);
    assert(c.update(in,351)==C::Event::up); // Renderer stall.
    in.tick=352;in.u=std::numeric_limits<double>::quiet_NaN();assert(c.update(in,352)==C::Event::none);
    d2vr::MenuTriggerGate trigger;
    assert(trigger.update(.8f,false,true)==.8f);
    assert(trigger.update(.8f,true,true)==0);
    assert(trigger.update(.8f,false,true)==0); // Resume while the click remains held.
    assert(trigger.update(0,false,false)==0); // Lost focus is not a physical release.
    assert(trigger.update(.8f,false,true)==0);
    assert(trigger.update(.1f,false,true)==.1f);
    assert(trigger.update(.8f,false,true)==.8f);
    {   // Iggy input slots: own slot, then an empty one, then eviction of a slot with no click held (never one holding a click).
        struct Slot {std::uintptr_t wrapper{};C click;};std::array<Slot,3> s{};unsigned evict=0;
        assert(d2vr::menu_input_slot(s,10,evict)==0);s[0].wrapper=10;s[1].wrapper=11;
        assert(d2vr::menu_input_slot(s,11,evict)==1 && d2vr::menu_input_slot(s,12,evict)==2);s[2].wrapper=12;
        d2vr::NativeMenuInput held{.5,.5,1,1,true,false};s[0].click.update(held,1);held.trigger=true;assert(s[0].click.update(held,2)==C::Event::down);
        assert(d2vr::menu_input_slot(s,13,evict)==1);s[1].wrapper=13;          // full: slot 0 holds a click, so slot 1 goes
        assert(d2vr::menu_input_slot(s,14,evict)==2);s[2].wrapper=14;          // the next after it
        assert(d2vr::menu_input_slot(s,15,evict)==1);                          // wraps past the held slot 0
        assert(d2vr::menu_input_slot(s,10,evict)==0);                          // a known wrapper keeps its slot
        held.trigger=true;for(auto& x:s) {x.click=C{};held.trigger=false;x.click.update(held,3);held.trigger=true;x.click.update(held,4);}
        assert(d2vr::menu_input_slot(s,99,evict)==-1);                         // every slot holds a click: skip the frame
    }
    std::cout<<"PASS: native menu trigger press/release, held combat activation, focus/recenter/stale cancellation and finite pointer\n";
}
