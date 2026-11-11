// v1166 pure check of the rig palette history slots (src/rig_mask.hpp rig_history_slots): one eye's renders every other engine frame read
// the slot of its own previous render (frame - 2) and write the other; a gap, a reset or the first render reads nothing (zero motion);
// a second rig draw of the same model in the same render finds the copy done and still reads the previous render. No device.
#include <cstdio>
#include <cstdint>
#include "../src/rig_mask.hpp"
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
int main() {
    std::uint64_t key[2]{};const std::uint32_t epoch=1;
    const auto render=[&](std::uint32_t frame) { // first draw of the model in this render: write, then read
        const auto s=d2vr::rig_history_slots(key[0],key[1],epoch,frame,2);
        if(!s.done) key[s.write]=d2vr::rig_history_key(epoch,frame);
        return s;
    };
    auto s=render(10);CHECK(s.read==-1 && !s.done);                 // first render: no own previous
    s=d2vr::rig_history_slots(key[0],key[1],epoch,10,2);CHECK(s.done && s.read==-1); // second draw, same render
    s=render(12);CHECK(s.read>=0 && key[s.read]==d2vr::rig_history_key(epoch,10) && key[s.write]==d2vr::rig_history_key(epoch,12)); // reads frame 10, writes 12 into the other slot
    s=d2vr::rig_history_slots(key[0],key[1],epoch,12,2);CHECK(s.done && key[s.read]==d2vr::rig_history_key(epoch,10));
    s=render(14);CHECK(s.read>=0 && key[s.read]==d2vr::rig_history_key(epoch,12));
    s=render(20);CHECK(s.read==-1);                                  // a gap (reset, paired frames): zero motion
    s=render(22);CHECK(s.read>=0 && key[s.read]==d2vr::rig_history_key(epoch,20));
    s=d2vr::rig_history_slots(0,0,1,0,2);CHECK(s.read==-1);          // frame 0 never matches an empty slot
    s=d2vr::rig_history_slots(key[0],key[1],2,24,2);CHECK(s.read==-1); // a new epoch (mode restart, frame counter reset): nothing from before
    if(failures) {std::printf("rig_history_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS: rig_history_check (own previous slot, same-render reuse, gaps read nothing)\n");
    return 0;
}
