// Lab: arkOcean's GPU point evaluation as decompiled (src/native_ocean_eval.hpp, artifacts/boat-re/), driven like the engine:
// the game frame registers the boat's point and reads the handle it registered 3 frames earlier (consumer 0x4e3f70); the backend frame
// advances the slot, reads staging[slot] back while dispatched[slot] > 0 (0x304c40), then every view runs Dispatch (0x3030f0).
// One view = the flat game / theater; two = the paired native eyes. Prints the height the boat would ride at.
#include "../src/native_ocean_eval.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>

static int failures=0;
static void expect(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}

static float wave(float x) {return -6.4f+.1f*std::sin(x);} // the ocean height under the boat (Addermire water ~6 m below z 0)

struct Ocean {
    struct Handle {int offset=-1,slot=-1;};
    int slot=0,queued=0;
    std::array<float,1024> points{};
    std::array<int,4> dispatched{};
    std::array<std::array<float,1024>,4> staging{},results{}; // GPU copy, CPU copy (+0x2d20); zero = never read back
    Handle reg(float x) {Handle h{queued,(slot+1)&3};points[static_cast<std::size_t>(queued++)]=x;return h;}
    void dispatch() {
        for(int i=0;i<queued;++i) staging[static_cast<std::size_t>(slot)][static_cast<std::size_t>(i)]=wave(points[static_cast<std::size_t>(i)]);
        dispatched[static_cast<std::size_t>(slot)]=queued;queued=0; // stored even when nothing was queued: the second eye's zero
    }
    void readback() {for(int i=0;i<dispatched[static_cast<std::size_t>(slot)];++i) results[static_cast<std::size_t>(slot)][static_cast<std::size_t>(i)]=staging[static_cast<std::size_t>(slot)][static_cast<std::size_t>(i)];}
    float get(Handle h) const {return results[static_cast<std::size_t>(h.slot)][static_cast<std::size_t>(h.offset)];}
};

// Runs 200 frames; returns the worst |seen - expected| after warm-up, where expected = the boat's height 7 frames earlier
// (the engine's own latency: 3 frames in the handle ring + 4 frame slots).
static double ride(int views,const std::function<bool(int)>& pass,float* last_seen) {
    Ocean o;std::array<Ocean::Handle,3> ring{};double worst=0;
    for(int f=0;f<200;++f) {
        const float x=.02f*f;
        auto& h=ring[static_cast<std::size_t>(f%3)];
        if(h.slot>=0) {
            const float seen=o.get(h);*last_seen=seen;
            if(f>=12) worst=std::fmax(worst,std::fabs(seen-wave(.02f*(f-7))));
        }
        h=o.reg(x);
        o.slot=(o.slot+1)&3;o.readback();
        for(int v=0;v<views;++v) if(pass(o.slot)) o.dispatch();
    }
    return worst;
}

int main() {
    float mono_seen{},stereo_seen{},fixed_seen{};
    const double mono=ride(1,[](int) {return true;},&mono_seen);
    const double stereo=ride(2,[](int) {return true;},&stereo_seen);
    d2vr::OceanEvalGate gate;
    const double fixed=ride(2,[&](int s) {return gate.take(s);},&fixed_seen);
    std::printf("boat height: flat %.3f (err %.4f)  paired eyes %.3f (err %.3f)  paired eyes + gate %.3f (err %.4f)\n",
        mono_seen,mono,stereo_seen,stereo,fixed_seen,fixed);
    expect(mono<1e-4,"one view: the boat rides the evaluated water");
    expect(stereo>5 && stereo_seen==0.f,"two views: dispatched[slot] is zeroed, nothing is read back, the boat targets height 0");
    expect(fixed<1e-4,"two views with the gate: the same water as the flat game");

    d2vr::OceanEvalGate g;
    expect(g.take(1) && !g.take(1) && !g.take(1),"one dispatch per slot, whatever the view count");
    expect(g.take(2) && g.take(3) && g.take(0) && g.take(1),"each new frame slot dispatches");
    expect(g.take(3) && !g.take(3),"a frame without the pass (slot jumps) still dispatches the next");
    g.reset();expect(g.take(3),"reset re-arms the current slot");
    std::printf(failures?"native_ocean_eval_check FAILED\n":"native_ocean_eval_check ok\n");
    return failures?1:0;
}
