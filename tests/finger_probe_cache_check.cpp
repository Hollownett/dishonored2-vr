#include "../src/finger_probe_cache.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
namespace r=d2vr::rig;
namespace {
r::HeldShape table(double z=0) {r::HeldShape s{};s.frame.position={0,0,z};s.frame.basis={{{1,0,0},{0,1,0},{0,0,1}}};s.half={.12,.12,.02};return s;}
unsigned live(const r::FingerProbeCache& c,std::uint64_t now) {std::array<r::HeldShape,8> out{};return c.shapes(now,out);}
// A frame clock: `frames` frames of `ms` each from `t`, the probe of `slot` answering on every `cadence`-th frame with hit(i) (nullopt = a miss);
// returns false if any frame between the first hit and `until` (exclusive) found no shape.
template<class Hit> bool present_every_frame(r::FingerProbeCache& c,unsigned slot,unsigned frames,double ms,unsigned cadence,Hit&& hit,double hitch_at=-1,double hitch_ms=0) {
    double t=1000;bool seen=false;
    for(unsigned i=0;i<frames;++i) {
        t+=(static_cast<double>(i)==hitch_at?hitch_ms:ms);
        const auto now=static_cast<std::uint64_t>(t);
        if(i%cadence==0) c.probed(slot,hit(i/cadence),now);
        const unsigned n=live(c,now);
        if(n) seen=true;else if(seen) return false;
    }
    return seen;
}
}
int main() {
    {   // hit_lives_150ms: an answer that hit lives 150 ms with no other answer; gone at 151
        r::FingerProbeCache c;c.probed(2,table(),1000);
        assert(live(c,1000)==1 && live(c,1150)==1 && live(c,1151)==0);
        std::array<r::HeldShape,8> out{};assert(c.shapes(1100,out)==1 && out[0].frame.position.z==0);
        assert(live(c,999)==1); // a reader a millisecond behind the answer (another thread's clock): not dropped
    }
    {   // stickiness_two_cycles: probes every 5th frame at 72 Hz; one miss keeps the shape, the second miss drops it
        r::FingerProbeCache c;std::uint64_t t=1000;c.probed(4,table(),t);
        t+=69;c.probed(4,std::nullopt,t);assert(live(c,t)==1);
        t+=69;c.probed(4,std::nullopt,t);assert(live(c,t)==0);
        t+=69;c.probed(4,table(.01),t);assert(live(c,t)==1); // a new hit brings it back at once
    }
    {   // no_flicker_on_cadence_5: hit, miss, hit, miss ... answered every 5th frame: present on every frame
        r::FingerProbeCache c;
        assert(present_every_frame(c,1,200,1000/72.,5,[](unsigned k) {return k%2?std::optional<r::HeldShape>{}:std::optional<r::HeldShape>{table()};}));
        r::FingerProbeCache d; // and at 90 Hz
        assert(present_every_frame(d,1,200,1000/90.,5,[](unsigned k) {return k%2?std::optional<r::HeldShape>{}:std::optional<r::HeldShape>{table()};}));
    }
    {   // stickiness_survives_hitch: a 100 ms frame between two probes of the slot: still present on every frame
        for(double at:{1.,2.,3.,4.}) {
            r::FingerProbeCache c;
            assert(present_every_frame(c,0,60,1000/72.,5,[](unsigned) {return std::optional<r::HeldShape>{table()};},at,100.));
        }
    }
    {   // lift_off_drops_within_two_cycles: the hand lifts off (every probe misses from then): the shape is gone by the second missed probe
        r::FingerProbeCache c;std::uint64_t t=1000;
        for(unsigned k=0;k<5;++k) {c.probed(3,table(),t);t+=69;}
        c.probed(3,std::nullopt,t);assert(live(c,t)==1);
        for(std::uint64_t u=t;u<t+69;u+=14) assert(live(c,u)==1);
        t+=69;c.probed(3,std::nullopt,t);assert(live(c,t)==0);
        t+=1000;assert(live(c,t)==0); // and it stays gone: no stuck open hand
    }
    {   // several slots at once, each its own life; a bad slot is ignored; clear empties it
        r::FingerProbeCache c;
        for(unsigned s=0;s<8;++s) c.probed(s,table(s*.01),1000);
        c.probed(8,table(),1000);c.probed(99,table(),1000);
        std::array<r::HeldShape,8> out{};assert(c.shapes(1000,out)==8);
        c.probed(5,std::nullopt,1010);c.probed(5,std::nullopt,1020);assert(c.shapes(1020,out)==7);
        c.clear();assert(c.shapes(1020,out)==0);
    }
    {   // palm_slot_time_based: the palm (slot 0) is probed every frame; two missed frames (~28 ms) keep it, it drops once the last hit is 140 ms old
        r::FingerProbeCache c;std::uint64_t t=1000;c.probed(0,table(),t);
        for(unsigned i=0;i<2;++i) {t+=14;c.probed(0,std::nullopt,t);assert(live(c,t)==1);}
        for(unsigned i=0;i<6;++i) {t+=14;c.probed(0,std::nullopt,t);assert(live(c,t)==1);} // 112 ms after the hit
        t=1000+140;c.probed(0,std::nullopt,t);assert(live(c,t)==0);
        c.probed(0,table(),t+14);assert(live(c,t+14)==1);
        // a fingertip slot keeps its two-miss rule
        r::FingerProbeCache d;d.probed(4,table(),1000);d.probed(4,std::nullopt,1014);d.probed(4,std::nullopt,1028);assert(live(d,1028)==0);
    }
    {   // finger_probe_priority (G5)
        unsigned hits[5]{};
        for(unsigned fr=0;fr<10;++fr) ++hits[r::pick_probe_finger(0b00110,fr,0,1)];
        assert(hits[1]+hits[2]>=7);for(unsigned f=0;f<5;++f) assert(hits[f]>=1);
        for(unsigned casts:{1u,2u}) for(unsigned fr=0;fr<100;++fr) for(unsigned k=0;k<casts;++k) assert(r::pick_probe_finger(0,fr,k,casts)==(fr*casts+k)%5);
        for(unsigned bits:{0u,0x20u,0xe0u}) assert(r::pick_probe_finger(bits,7,0,1)==7%5);
        unsigned seen[5]{};
        for(unsigned fr=0;fr<10;++fr) for(unsigned k=0;k<2;++k) ++seen[r::pick_probe_finger(0b10000,fr,k,2)];
        for(unsigned f=0;f<5;++f) assert(seen[f]>=1);
    }
    {   // finger_slab_on_palm_plane (G6)
        const double d=3.14159265358979323846/180;
        const auto slab=[&](double x,double tilt_deg,double above,double half) { // face on z=above at x, tilted about y
            const d2vr::Vec3 n{std::sin(tilt_deg*d),0,std::cos(tilt_deg*d)};
            auto h=r::slab_shape({x,0,above},n,half);return h;};
        const auto run=[&](r::HeldShape tip,bool surface,bool palm_live,r::HeldShape& got) {
            r::FingerProbeCache c;c.probed(0,slab(0,0,0,.07),1000,true);c.probed(1,tip,1000,surface);
            const std::uint64_t now=palm_live?1000:1000+400;
            if(!palm_live) c.probed(1,tip,now,surface); // the tip stays live, the palm expired
            std::array<r::HeldShape,8> out{};const unsigned n=c.shapes_on_palm_plane(now,out);assert(n==(palm_live?2u:1u));got=out[n-1];};
        for(double tilt:{1.5,2.5}) {
            const auto in=slab(.08,tilt,.003,.05);r::HeldShape got{};run(in,true,true,got);
            assert(dot(got.frame.basis[2],d2vr::Vec3{0,0,1})>1-1e-9);
            const d2vr::Vec3 face=got.frame.position+got.frame.basis[2]*got.half.z;
            assert(std::abs(face.z)<1e-6 && std::abs(face.x-(.08-.003*0))<.01);
            assert(got.half.x==.05 && got.half.y==.05 && got.half.z==in.half.z);
        }
        for(const auto& in:{slab(.08,10,.003,.05),slab(.08,1.5,.008,.05)}) {r::HeldShape got{};run(in,true,true,got);assert(std::memcmp(&got,&in,sizeof got)==0);}
        {const auto in=slab(.08,1.5,.003,.05);r::HeldShape got{};run(in,false,true,got);assert(std::memcmp(&got,&in,sizeof got)==0);}
        {const auto in=slab(.08,1.5,.003,.05);r::HeldShape got{};run(in,true,false,got);assert(std::memcmp(&got,&in,sizeof got)==0);}
    }
    std::cout<<"finger_probe_cache_check passed"<<std::endl;
    return 0;
}
