// v1179 pure check of the rig palette ring's run rule (src/rig_mask.hpp rig_ring_run / rig_ring_valid): the ring rotates once per engine
// frame at the GPU skin copy (P <- B <- A <- current), so P holds the render two engine frames back only after three consecutive frames;
// a second call in the same frame changes nothing; a skipped frame or a restart (~0u) begins a new run. No device.
// (Replaces the v1166 per-eye slot check: the slots keyed by a draw's classified frame are gone.)
#include <cstdio>
#include <cstdint>
#include "../src/rig_mask.hpp"
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
int main() {
    using d2vr::rig_ring_run;using d2vr::rig_ring_valid;
    unsigned run=rig_ring_run(~0u,0,100);CHECK(run==1 && !rig_ring_valid(run));     // first rotation: only A is real
    run=rig_ring_run(100,run,100);CHECK(run==1);                                     // same frame again: no rotation, no change
    run=rig_ring_run(100,run,101);CHECK(run==2 && !rig_ring_valid(run));             // A, B real; P not yet
    run=rig_ring_run(101,run,102);CHECK(run==3 && rig_ring_valid(run));              // P = frame 100's palette: valid
    run=rig_ring_run(102,run,103);CHECK(run==4 && rig_ring_valid(run));
    run=rig_ring_run(103,run,105);CHECK(run==1 && !rig_ring_valid(run));             // frame 104 had no skin copy: start over
    run=rig_ring_run(~0u,run,106);CHECK(run==1);                                     // a restart (begin_epoch)
    run=rig_ring_run(5,250,6);CHECK(run==251);run=rig_ring_run(6,255,7);CHECK(run==255); // saturates
    run=rig_ring_run(10,5,9);CHECK(run==1);                                          // the counter went back (a load)
    if(failures) {std::printf("rig_history_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS: rig_history_check (ring run: three consecutive engine frames, same-frame calls inert, gaps restart)\n");
    return 0;
}
