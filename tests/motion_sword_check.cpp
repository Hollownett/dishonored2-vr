#include "../src/play_controls.hpp"
#include <cassert>
#include <limits>
#include <iostream>
using d2vr::MotionSwordSwing;
int main() {
    MotionSwordSwing swing;
    auto update=[&](float speed,unsigned now,bool allowed=true,bool valid=true,unsigned epoch=1,unsigned source=0) {
        return swing.update(speed,valid,allowed,epoch,source?source:now,now);
    };
    assert(!update(3,100).active); // Returning under a moving hand cannot attack.
    assert(!update(.2f,110).active);
    auto first=update(3,120);assert(first.started && first.active && first.until==370);
    assert(!update(3,120).started); // Duplicate publication.
    unsigned pulses=1;
    for(unsigned tick:{210u,320u,420u,520u,620u}) pulses+=update(3,tick).started;
    assert(pulses==1 && !update(3,630).active); // Old time-only predicate attacks again at 420.
    assert(!update(.3f,640).active);assert(update(3,650).started);
    assert(!update(3,660,false).active);assert(!update(3,960).started);
    update(.3f,970);assert(update(3,980).started);
    assert(!update(3,990,true,false).active); // Missing runtime velocity cancels.
    assert(!update(3,1300).started);update(.3f,1310);assert(update(3,1320).started);
    assert(!update(3,1330,true,true,2).active); // Epoch change cancels and consumes high speed.
    update(.3f,1340,true,true,2);assert(update(3,1350,true,true,2).started);
    assert(!update(3,1700,true,true,2,1400).active); // A 300-ms-old source cannot extend the pulse.
    assert(!update(3,1710,true,true,2).started);
    update(.3f,1720,true,true,2);assert(update(3,1730,true,true,2).started);
    assert(!update(3,1725,true,true,2).active); // Backward wall clock.
    assert(!update(3,1735,true,true,2).started);
    update(.3f,1740,true,true,2);assert(update(3,1750,true,true,2).started);
    assert(!update(3,1760,true,true,2,1749).active); // Backward source.
    assert(!update(3,1770,true,true,2).started);
    update(.3f,1780,true,true,2);assert(update(3,1790,true,true,2).started);
    assert(!update(std::numeric_limits<float>::quiet_NaN(),1800,true,true,2).active);
    assert(!update(3,1810,true,true,2).started);
    update(.3f,2220,true,true,2);
    assert(!update(3,2221,true,true,2,2220).started); // A changed value cannot reuse a source to create an edge.
    assert(update(3,2230,true,true,2).started);
    // A fast edge inside the refractory period is spent, never queued for expiry.
    update(.3f,2240,true,true,2);assert(!update(3,2250,true,true,2).started);
    assert(!update(3,2531,true,true,2).started);
    update(.3f,2540,true,true,2);assert(update(3,2550,true,true,2).started);
    // 110-250-ms cadence retains the exact pulse length without repeating motion.
    MotionSwordSwing slow;
    assert(!slow.update(.1f,true,true,1,100,100).active);
    assert(slow.update(3,true,true,1,300,300).started);
    assert(slow.update(3,true,true,1,410,410).active);
    assert(!slow.update(3,true,true,1,610,610).active);
    assert(!slow.update(3,true,true,1,810,810).started);
    std::cout<<"PASS: one sword pulse per motion; exact 250-ms pulse; focus/menu/grab, velocity, epoch/source/clock and slow-frame transitions\n";
}
