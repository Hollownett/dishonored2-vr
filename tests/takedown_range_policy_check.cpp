// Takedown range policy (headset 2026-10-22: takedowns trigger too far from the guard): knob parsing, the choke private-view factor,
// the kill box, and the poll limiter against a fake process memory.
#include "../src/native_choke_distance_scope.hpp"
#include "../src/takedown_range_policy.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
using namespace d2vr;
namespace {
void require(bool ok,const char* what) {if(!ok) {std::printf("FAIL: %s\n",what);std::exit(1);}}
using uptr=std::uintptr_t;
struct Mem {
    std::map<uptr,std::uint8_t> bytes;
    void put(uptr at,const void* src,std::size_t n) {const auto* p=static_cast<const std::uint8_t*>(src);for(std::size_t i=0;i<n;++i) bytes[at+i]=p[i];}
    template<class T> void put(uptr at,T v) {put(at,&v,sizeof(v));}
    bool operator()(uptr at,void* out,std::size_t n) const {
        auto* p=static_cast<std::uint8_t*>(out);
        for(std::size_t i=0;i<n;++i) {auto it=bytes.find(at+i);if(it==bytes.end()) return false;p[i]=it->second;}
        return true;
    }
};
constexpr uptr base=0x140000000;
constexpr uptr game=0x200000000,player=0x300000000,guard=0x310000000;
constexpr std::uint32_t player_spawn=0x00000001u,guard_spawn=0x00010002u;
void world(Mem& m,float guard_dx,float guard_dy,float guard_dz) {
    m.put<uptr>(base+0x39a5290,game);m.put<uptr>(game,base+0x1c940a8);
    m.put<uptr>(game+0x3130b0,player);m.put<uptr>(game+0x3130b0+8,guard);
    m.put<uptr>(player,base+0x1c5e428);m.put<std::uint32_t>(player+0xa8,player_spawn);
    m.put<uptr>(guard,base+0x1c5e428);m.put<std::uint32_t>(guard+0xa8,guard_spawn);
    const std::array<float,3> p{10.f,20.f,5.f},g{10.f+guard_dx,20.f+guard_dy,5.f+guard_dz};
    m.put(player+0x300,&p,12);m.put(guard+0x300,&g,12);
}
TakedownState offered_kill() {
    TakedownState s;s.kill=true;s.offered=true;s.kill_spawn=guard_spawn;s.spawn=guard_spawn;return s;
}
}
int main() {
    // knob
    require(takedown_range_scale_from_knob("")==.75f,"empty -> default .75");
    require(takedown_range_scale_from_knob("0.6")==.6f && takedown_range_scale_from_knob(" 0.9\r\n")==.9f,"plain numbers");
    require(takedown_range_scale_from_knob("0.5")==.5f && takedown_range_scale_from_knob("1")==1.f && takedown_range_scale_from_knob("1.0")==1.f,"bounds are inclusive");
    require(takedown_range_scale_from_knob("0.1")==.5f && takedown_range_scale_from_knob("-3")==.5f,"below range clamps to .5");
    require(takedown_range_scale_from_knob("1.5")==1.f && takedown_range_scale_from_knob("9")==1.f,"above range clamps to native: never widens");
    require(takedown_range_scale_from_knob("abc")==.75f && takedown_range_scale_from_knob("nan")==.75f && takedown_range_scale_from_knob("inf")==.75f,"junk -> default");
    require(choke_view_factor(1.5f)==1.f && choke_view_factor(.75f)==.75f && choke_view_factor(.1f)==.5f && choke_view_factor(NAN)==.75f,"choke factor is at most native");
    // choke private view: distance and height scale, the yaw windows never narrow
    require(NativeChokeDistanceScope::view_value(0,.75f)==3.f*.75f && NativeChokeDistanceScope::view_value(1,.75f)==1.7f*.75f,"choke distance/height shrink");
    for(unsigned i=2;i<6;++i) require(NativeChokeDistanceScope::view_value(i,.75f)==NativeChokeDistanceScope::shipped[i],"choke yaw windows stay native when shrinking");
    for(unsigned i=0;i<6;++i) require(NativeChokeDistanceScope::view_value(i,1.5f)==NativeChokeDistanceScope::shipped[i]*1.5f,"the old x1.5 view is unchanged");
    // kill box
    const std::array<float,3> origin{0,0,0};
    require(kill_within_scaled_range(origin,{2.f,0,0},.75f) && !kill_within_scaled_range(origin,{2.4f,0,0},.75f),"3D distance limit is 3*S");
    require(kill_within_scaled_range(origin,{2.9f,0,0},1.f) && !kill_within_scaled_range(origin,{3.1f,0,0},1.f),"S=1 is the native 3 m");
    require(kill_within_scaled_range(origin,{1.f,0,1.2f},.75f) && !kill_within_scaled_range(origin,{1.f,0,1.4f},.75f),"height limit is 1.7*S");
    require(!kill_within_scaled_range(origin,{0,0,-1.4f},.75f) && kill_within_scaled_range(origin,{0,0,-1.2f},.75f),"below the player too");
    require(!kill_within_scaled_range(origin,{2.3f,0,0},.5f) && kill_within_scaled_range(origin,{1.4f,0,0},.5f),"S=.5 is 1.5 m");
    require(kill_within_scaled_range(origin,{NAN,0,0},.75f),"unreadable origin fails open");
    // poll limiter
    {Mem m;world(m,2.f,0,0);
     auto s=limit_takedown_to_scale(m,base,offered_kill(),.75f);
     require(s.kill && s.offered && s.kill_spawn==guard_spawn && s.spawn==guard_spawn,"2 m guard stays offered at .75");}
    {Mem m;world(m,2.6f,0,0);
     auto s=limit_takedown_to_scale(m,base,offered_kill(),.75f);
     require(!s.kill && !s.offered && !s.kill_spawn && !s.spawn && !s.any(),"2.6 m guard is no longer offered at .75");
     s=limit_takedown_to_scale(m,base,offered_kill(),1.f);
     require(s.kill && s.offered,"scale 1 leaves the native verdict");
     require(limit_takedown_to_scale(m,base,offered_kill(),.9f).kill,"2.6 m is inside the .9 box (2.7 m)");
     require(!limit_takedown_to_scale(m,base,offered_kill(),.85f).kill,"2.6 m is outside the .85 box (2.55 m)");}
    {Mem m;world(m,1.f,0,1.5f);
     require(!limit_takedown_to_scale(m,base,offered_kill(),.75f).kill,"1.5 m above is outside 1.275 m");}
    {Mem m;world(m,2.6f,0,0);
     TakedownState s=offered_kill();s.choke=true;s.choke_spawn=0x00020005u;
     s=limit_takedown_to_scale(m,base,s,.75f);
     require(!s.kill && s.choke && s.spawn==0x00020005u && !s.offered,"a far kill target hands the prompt to the choke target");}
    {Mem m;world(m,2.6f,0,0);
     TakedownState s=offered_kill();s.drop=true;s.drop_spawn=0x00030006u;
     s=limit_takedown_to_scale(m,base,s,.75f);
     require(!s.kill && s.drop && s.offered && s.spawn==0x00030006u,"a drop offer survives a far kill target");}
    {Mem m;   // nothing readable: native behaviour
     const auto s=limit_takedown_to_scale(m,base,offered_kill(),.75f);
     require(s.kill && s.offered,"unreadable memory fails open");}
    {Mem m;world(m,9.f,0,0);m.put<uptr>(guard+0xa8,0u); // stale spawn id: takedown_target refuses it
     require(limit_takedown_to_scale(m,base,offered_kill(),.75f).kill,"an unresolved kill target fails open");}
    {Mem m;world(m,9.f,0,0);TakedownState none;
     require(!limit_takedown_to_scale(m,base,none,.75f).any(),"no kill: nothing to limit");}
    // wiring pins (run from the repo root)
    auto slurp=[](const char* path) {std::ifstream in(path);return std::string((std::istreambuf_iterator<char>(in)),{});};
    const auto probe=slurp("src/dxgi_probe.cpp");
    if(!probe.empty()) {
        require(probe.find("takedown-range-scale")!=std::string::npos,"dxgi_probe reads the takedown-range-scale knob");
        require(probe.find("limit_takedown_to_scale(control_reader")!=std::string::npos,"the control poll is range-limited");
        require(probe.find("private-view-x1.5")==std::string::npos,"the x1.5 status label is gone");
        require(probe.find("takedown_range_scale_from_knob(text):1.f")!=std::string::npos,"takedown-range off means scale 1 (native)");
    }
    const auto features=slurp("tools/default_features.json");
    if(!features.empty()) require(features.find("Longer takedown range")==std::string::npos && features.find("1.5 times longer")==std::string::npos,"the feature no longer promises a longer range");
    std::puts("PASS takedown range policy");
}
