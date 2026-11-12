// v1192 pure check of src/sim_pose_hold.hpp: the script clock changes only on Presents whose display frame has eye 0 fresh, so the
// following frame (eye 1 fresh) renders the same pose; the phase knob and the parity flip move it by one frame; the Setup check
// verifies held frames and catches a wrong phase.
#include <cstdio>
#include "../src/alternate_eye.hpp"
#include "../src/sim_pose_hold.hpp"
using namespace d2vr;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
int main() {
    for(int flip=0;flip<2;++flip) for(unsigned phase=0;phase<2;++phase) {
        sim_pose_hold::Clock clock;sim_pose_hold::Check check;int verified=0,broken=0;
        std::uint64_t pose_of[64]{};
        for(std::uint64_t n=1;n<40;++n) { // Present n: the script is sampled at 11 ms steps; the Setup after it renders display D
            const std::uint64_t d=alternate_display_frame(n);
            const bool change=sim_pose_hold::changes(d,flip!=0,phase);
            pose_of[d%64]=clock.sample(n*11,change);check.stamp(d,change);
            const int eye=static_cast<int>((d+(flip?1u:0u))&1u); // parity_eye(D)
            CHECK(change==(eye==static_cast<int>(phase)));
            const int r=check.setup(d,eye,change);verified+=r>0;broken+=r<0;
            if(!change && n>2) CHECK(pose_of[d%64]==pose_of[(d-1)%64]); // the held eye renders the change frame's pose
            if(change && n>2) CHECK(pose_of[d%64]!=pose_of[(d-1)%64]);   // and the next change frame moves on
        }
        CHECK(verified>=17 && broken==0);
        // a provider one Present out of phase: every held Setup is caught
        sim_pose_hold::Check wrong;int caught=0;
        for(std::uint64_t n=1;n<40;++n) {const std::uint64_t d=alternate_display_frame(n);const bool change=sim_pose_hold::changes(d,flip!=0,phase);
            wrong.stamp(d,sim_pose_hold::changes(d+1,flip!=0,phase));caught+=wrong.setup(d,static_cast<int>((d+(flip?1u:0u))&1u),change)<0;}
        CHECK(caught>=17);
    }
    if(failures) {std::printf("sim_pose_hold_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS: sim_pose_hold_check (poses change on eye-0-fresh frames, held for the eye-1 frame; phase and parity flip; wrong phase caught)\n");
    return 0;
}
