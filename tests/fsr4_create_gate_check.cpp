// Plan 1.3 (docs/PERF-PLAN-90HZ-2026-11-19.md): FSR 4 contexts are created only where FSR 4 can run. Pure checks of
// src/fsr4_create_gate.hpp, plus a model of FsrTaa::prepare's per-eye create loop (two eyes per rebuild) that counts the
// creates, failures and skips the rebuild log and status report. No D3D, no provider, no game.
#include "../src/fsr4_create_gate.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#define CHECK(x) do{if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using d2vr::Fsr4CreateGate;
using Skip=Fsr4CreateGate::Skip;

namespace {
constexpr unsigned amd=0x1002,nvidia=0x10de,intel=0x8086;
struct Counts {unsigned attempts=0,failures=0,skips=0;Skip last=Skip::none;};
// FsrTaa::prepare: per eye, skip() decides; an attempted create reports to note_create(); a skip other than "no provider" is counted.
template<class Create> void rebuild(Fsr4CreateGate& gate,bool provider,unsigned vendor,Counts& c,Create&& create) {
    for(unsigned eye=0;eye<2;++eye) {
        const auto skip=gate.skip(provider,vendor);
        if(skip==Skip::none) {++c.attempts;const bool ok=create(eye);gate.note_create(ok);if(!ok) ++c.failures;}
        else if(skip!=Skip::no_provider) {++c.skips;c.last=skip;}
    }
}
}

int main() {
    CHECK(Fsr4CreateGate::amd_vendor==0x1002);
    CHECK(Fsr4CreateGate::vendor_may_run(amd) && Fsr4CreateGate::vendor_may_run(0));
    CHECK(!Fsr4CreateGate::vendor_may_run(nvidia) && !Fsr4CreateGate::vendor_may_run(intel) && !Fsr4CreateGate::vendor_may_run(1));
    CHECK(std::strcmp(d2vr::fsr4_skip_name(Skip::vendor),"vendor")==0 && std::strcmp(d2vr::fsr4_skip_name(Skip::failed),"failed")==0 &&
          std::strcmp(d2vr::fsr4_skip_name(Skip::none),"none")==0 && std::strcmp(d2vr::fsr4_skip_name(Skip::no_provider),"no_provider")==0);
    {   // The 2026-11-19 session (RTX 5070, provider shipped): 4 rebuilds used to try 8 creates and fail all of them. Now: none.
        Fsr4CreateGate gate;Counts c;
        for(unsigned r=0;r<4;++r) rebuild(gate,true,nvidia,c,[](unsigned) {return false;});
        CHECK(c.attempts==0 && c.failures==0 && c.skips==8 && c.last==Skip::vendor);
        CHECK(gate.skip(true,intel)==Skip::vendor);
    }
    {   // No provider (not shipped, or build/fsr4.off): nothing is attempted and nothing counts as a skip.
        Fsr4CreateGate gate;Counts c;
        for(const unsigned vendor:{amd,nvidia,0u}) rebuild(gate,false,vendor,c,[](unsigned) {return false;});
        CHECK(c.attempts==0 && c.skips==0 && gate.skip(false,amd)==Skip::no_provider);
    }
    {   // A working AMD adapter (RX 9070 XT): every rebuild creates both eyes, as before.
        Fsr4CreateGate gate;Counts c;
        for(unsigned r=0;r<5;++r) rebuild(gate,true,amd,c,[](unsigned) {return true;});
        CHECK(c.attempts==10 && c.failures==0 && c.skips==0 && gate.created() && !gate.latched());
        // A later failure (resize, memory pressure, retiring contexts) stays retryable once FSR 4 has worked this session.
        rebuild(gate,true,amd,c,[](unsigned eye) {return eye==1;});
        CHECK(c.attempts==12 && c.failures==1 && c.skips==0 && !gate.latched());
        rebuild(gate,true,amd,c,[](unsigned) {return false;});
        CHECK(c.attempts==14 && c.failures==3 && !gate.latched() && gate.should_create(true,amd));
        rebuild(gate,true,amd,c,[](unsigned) {return true;});
        CHECK(c.attempts==16 && c.failures==3);
    }
    {   // An AMD adapter that cannot run FSR 4 (pre-RDNA 4, old driver): one failed create, then the other eye and every later rebuild skip it.
        Fsr4CreateGate gate;Counts c;
        for(unsigned r=0;r<4;++r) rebuild(gate,true,amd,c,[](unsigned) {return false;});
        CHECK(c.attempts==1 && c.failures==1 && c.skips==7 && c.last==Skip::failed && gate.latched() && !gate.created());
        CHECK(gate.skip(true,amd)==Skip::failed && gate.skip(true,0)==Skip::failed && gate.skip(true,nvidia)==Skip::vendor && gate.skip(false,amd)==Skip::no_provider);
        // The session ends (FsrTaa::clear): the next session tries once again.
        gate.session_end();
        CHECK(!gate.latched() && !gate.created() && gate.should_create(true,amd));
        Counts next;rebuild(gate,true,amd,next,[](unsigned) {return true;});
        CHECK(next.attempts==2 && next.failures==0 && gate.created());
    }
    {   // Unknown vendor (the adapter query failed): one create decides, like an AMD adapter.
        Fsr4CreateGate gate;Counts c;
        rebuild(gate,true,0,c,[](unsigned) {return false;});rebuild(gate,true,0,c,[](unsigned) {return false;});
        CHECK(c.attempts==1 && c.failures==1 && c.skips==3);
        Fsr4CreateGate works;Counts w;rebuild(works,true,0,w,[](unsigned) {return true;});
        CHECK(w.attempts==2 && w.failures==0 && works.created());
    }
    std::puts("PASS: FSR 4 creates only on AMD (or unknown) adapters, never again after a failure before any success this session, retried after a success, reset per session");
    return 0;
}
