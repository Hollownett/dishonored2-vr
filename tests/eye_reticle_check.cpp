#include "../src/eye_reticle.hpp"
#include <cassert>
#include <limits>
#include <iostream>
int main() {
    using namespace d2vr;
    EyeReticleFrames frames;
    const EyeReticle left{.75f,-.8f,10,4,3,1000,true},right{.65f,-.79f,10,4,3,1000,true};
    frames.publish(0,left);frames.publish(1,right);
    assert(frames.find(0,10,4,3,1001)->x==left.x);
    assert(frames.find(1,10,4,3,1001)->x==right.x); // independent stereo parallax, no flat shared stage
    frames.publish(0,{.7f,-.75f,11,4,3,1010,true});
    assert(frames.find(0,11,4,3,1011));
    assert(frames.find(1,10,4,3,1011)->x==right.x); // kept eye retains the receipt of its own image
    assert(!frames.find(1,11,4,3,1011)); // never substitute latest opposite eye or previous projection
    assert(!frames.find(0,10,5,3,1001));
    assert(!frames.find(0,10,4,4,1001));
    assert(!frames.find(0,10,4,3,999));
    assert(!frames.find(0,10,4,3,1251));
    frames.publish(0,{0,0,11,4,3,1020,false});
    assert(!frames.find(0,11,4,3,1021)); // tracking loss replaces the same image's receipt
    for(float outside:{-1.f,1.f,-1.2f,1.2f,std::numeric_limits<float>::quiet_NaN()}) {
        frames.publish(0,{outside,0,12,4,3,1030,true});
        assert(!frames.find(0,12,4,3,1031)); // offscreen aims disappear; no clamped edge crosshair
        assert(!eye_reticle_rect(outside,0,2000,1600));
    }
    const auto rect=eye_reticle_rect(.8f,-.8f,2000,1600);
    assert(rect && (*rect)[0]>.7f && (*rect)[1]<-.7f); // outside the old 16:9 stage's vertical extent
    assert(std::abs(((*rect)[2]-(*rect)[0])*2000-((*rect)[1]-(*rect)[3])*1600)<.001f);
    assert(!eye_reticle_rect(0,0,0,1600));
    const auto pixels=eye_reticle_pixels();
    assert(pixels[3]==0 && pixels[4*(15*32+15)+3]==255);
    frames.clear();assert(!frames.find(1,10,4,3,1031));
    std::cout<<"PASS: per-image stereo reticle receipts, kept-eye identity, offscreen rejection and full-eye pixel geometry\n";
}
