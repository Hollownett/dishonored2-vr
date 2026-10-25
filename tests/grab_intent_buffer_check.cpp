#include "../src/grab_intent_buffer.hpp"
#include <cassert>
#include <iostream>
using d2vr::GrabIntentBuffer;using Step=GrabIntentBuffer::Step;
int main() {
    {   // catches_inside_window: 0.30 m at +100 ms with a 0.45 m gate
        GrabIntentBuffer b;b.start(1000,150,.45);assert(b.waiting());
        assert(b.step(1050,std::nullopt)==Step::waiting);
        assert(b.step(1100,.30)==Step::caught && !b.waiting() && b.buffered()==1 && b.expired()==0);
        assert(b.step(1101,.10)==Step::idle);   // caught once
    }
    {   // expires_after_window: nothing by +151 ms; the window's end is the expiry (+150 is already out)
        GrabIntentBuffer b;b.start(1000,150,.45);
        assert(b.step(1149,std::nullopt)==Step::waiting);
        assert(b.step(1151,.30)==Step::expired && b.expired()==1 && b.buffered()==0);
        GrabIntentBuffer edge;edge.start(1000,150,.45);assert(edge.step(1150,.10)==Step::expired);
    }
    {   // never_catches_beyond_gate: 0.46 m at +50 ms stays waiting, then expires
        GrabIntentBuffer b;b.start(1000,150,.45);
        assert(b.step(1050,.46)==Step::waiting);assert(b.step(1100,.451)==Step::waiting);
        assert(b.step(1160,.46)==Step::expired && b.buffered()==0);
        GrabIntentBuffer grip;grip.start(1000,150,.12);   // the grip-alone gate (ruling H1)
        assert(grip.step(1050,.13)==Step::waiting && grip.step(1060,.12)==Step::caught);
    }
    {   // zero_window_disabled: grab_buffer_ms 0 never starts
        GrabIntentBuffer b;b.start(1000,0,.45);
        assert(!b.waiting() && b.step(1001,.0)==Step::idle && b.step(2000,.0)==Step::idle && b.expired()==0 && b.buffered()==0);
        b.start(1000,150,std::nan(""));assert(!b.waiting());
    }
    {   // buffer_target_vanishes_expires: the candidate goes away, then a different id appears in the same registry slot: no catch of
        // the recycled id, expiry at 150 ms
        GrabIntentBuffer b;b.start(1000,150,.45);
        assert(b.step(1010,.50,0x10001u)==Step::waiting);
        assert(b.step(1040,std::nullopt)==Step::waiting);
        assert(b.step(1080,.30,0x10002u)==Step::waiting);   // recycled slot 1
        assert(b.step(1120,.05,0x10002u)==Step::waiting);
        assert(b.step(1150,.05,0x10002u)==Step::expired && b.buffered()==0);
        GrabIntentBuffer other;other.start(1000,150,.45);   // a different slot is a different object: caught
        assert(other.step(1010,.50,0x10001u)==Step::waiting && other.step(1020,.30,0x20001u)==Step::caught);
    }
    {   // epoch_change_clears_pending_buffer: the caller cancels on a release, an epoch change or an occupied hand
        GrabIntentBuffer b;b.start(1000,150,.45);b.cancel();
        assert(!b.waiting() && b.step(1050,.10)==Step::idle && b.buffered()==0 && b.expired()==0);
        b.start(2000,150,.45);assert(b.step(2010,.10)==Step::caught);   // a fresh press starts afresh
    }
    std::cout<<"PASS: grab intent buffer catches inside 150 ms within the gate, expires after it, never beyond the gate, 0 disables, recycled ids, cancel\n";
}
