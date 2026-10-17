#define NOMINMAX
#include "../src/native_weapon_hands.hpp"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
int main() {
    using namespace d2vr;
    // A floor at z=0 and a wall at x=3, as the native sphere sweep reports them.
    auto world=[](Vec3 a,Vec3 b)->std::optional<WorldTraceHit> {
        const double r=SwordFlight::radius;
        if(b.x>3-r && a.x<=3-r) {const double k=(3-r-a.x)/(b.x-a.x);WorldTraceHit h{};h.centre=a+(b-a)*k;h.normal={-1,0,0};h.fraction=k;return h;}
        if(b.z<r && a.z>=r) {const double k=(a.z-r)/(a.z-b.z);WorldTraceHit h{};h.centre=a+(b-a)*k;h.normal={0,0,1};h.fraction=k;return h;}
        return {};
    };
    auto orthonormal=[](const SwordFlight& f) {
        for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) if(std::abs(dot(f.axis[i],f.axis[j])-(i==j?1.:0.))>1e-6) return false;
        return true;
    };
    {   // let go at rest: falls and lies flat on the floor
        SwordFlight f{};f.pos={0,0,1};f.axis={Vec3{0,0,-1},Vec3{0,1,0},Vec3{1,0,0}};f.flying=true; // blade pointing down
        double t=0;while(f.step(.011,world) && t<3) t+=.011;
        assert(!f.flying && t>.35 && t<.55);
        assert(std::abs(f.pos.z-(SwordFlight::radius+.01))<1e-6 && std::abs(f.axis[0].z)<1e-9 && f.axis[2].z==1 && orthonormal(f));
    }
    {   // thrown at the wall: tumbles, glances off it, falls back and lands short of it
        SwordFlight f{};f.pos={0,0,1.4};f.vel={10,0,0};f.axis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};
        f.spin_axis={0,-1,0};f.spin=-16;f.flying=true;
        double t=0;bool tumbling=true;
        while(f.step(.011,world) && t<5) {t+=.011;tumbling=tumbling && orthonormal(f);}
        assert(tumbling && f.bounces==1 && !f.flying && f.pos.x<3 && std::abs(f.pos.z-(SwordFlight::radius+.01))<1e-6 && orthonormal(f));
    }
    {
        SwordFlight sword{};sword.pos={0,0,1.2};sword.vel={.2,0,0};
        sword.axis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};sword.flying=true;
        sword.step(.011,[](Vec3 from,Vec3)->std::optional<WorldTraceHit> {
            WorldTraceHit hit{};hit.centre=from;hit.normal={-1,0,0};return hit;
        });
        assert(sword.flying);
        double elapsed{};while(sword.step(.011,world) && elapsed<3) elapsed+=.011;
        assert(!sword.flying && std::abs(sword.pos.z-(SwordFlight::radius+.01))<1e-6);
    }
    {
        SwordFlight sword{};sword.pos={0,0,1.2};sword.axis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};sword.flying=true;
        sword.step(.011,[](Vec3 from,Vec3)->std::optional<WorldTraceHit> {
            WorldTraceHit hit{};hit.centre=from;hit.normal={0,0,1};hit.initial_overlap=true;hit.normal_valid=false;return hit;
        });
        assert(sword.flying && sword.pos.z<1.2);
        double elapsed{};while(sword.step(.011,world) && elapsed<3) elapsed+=.011;
        assert(!sword.flying && std::abs(sword.pos.z-(SwordFlight::radius+.01))<1e-6);
    }
    {
        SwordFlight sword{};sword.pos={0,0,.02};sword.axis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};sword.flying=true;
        assert(!sword.step(.011,[](Vec3,Vec3)->std::optional<WorldTraceHit> {
            WorldTraceHit hit{};hit.normal={0,0,1};hit.initial_overlap=true;return hit;
        }));
        assert(std::abs(sword.pos.z-(SwordFlight::radius+.01))<1e-6);
    }
    {
        SwordFlight sword{};sword.pos={0,0,1.2};sword.axis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};sword.flying=true;
        sword.step(.011,[](Vec3 from,Vec3)->std::optional<WorldTraceHit> {
            WorldTraceHit hit{};hit.point=from;hit.normal={0,0,1};hit.initial_overlap=true;hit.static_support=false;return hit;
        });
        assert(sword.flying && sword.pos.z<1.2);
        double elapsed{};while(sword.step(.011,world) && elapsed<3) elapsed+=.011;
        assert(!sword.flying && std::abs(sword.pos.z-(SwordFlight::radius+.01))<1e-6);
    }
    {   // flight_times_out: over an empty world the flight still ends after max_flight_s, at a finite position
        SwordFlight f{};f.pos={0,0,1000};f.vel={5,0,0};f.axis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};f.flying=true;
        const auto empty=[](Vec3,Vec3)->std::optional<WorldTraceHit> {return {};};
        unsigned steps=0;while(steps<400 && f.step(.02,empty)) ++steps;
        assert(!f.flying && steps<=301 && f.age>=SwordFlight::max_flight_s && SwordFlight::finite(f));
        assert(!f.step(.02,empty));
    }
    {   // nan_velocity_stops: a non-finite state ends the flight at once, position untouched
        SwordFlight f{};f.pos={1,2,3};f.vel={std::nan(""),0,0};f.axis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};f.flying=true;
        assert(!SwordFlight::finite(f));
        assert(!f.step(.02,[](Vec3,Vec3)->std::optional<WorldTraceHit> {return {};}));
        assert(!f.flying && f.pos.x==1 && f.pos.y==2 && f.pos.z==3);
    }
    {   // request() rejects non-finite pos/vel/axis and still accepts a finite throw
        NativeWeaponHands::Request r{};r.op=NativeWeaponHands::Op::drop;
        assert(NativeWeaponHands::request(r));
        auto bad=r;bad.pos[1]=INFINITY;assert(!NativeWeaponHands::request(bad));
        bad=r;bad.vel[2]=std::nanf("");assert(!NativeWeaponHands::request(bad));
        bad=r;bad.axis[8]=std::nanf("");assert(!NativeWeaponHands::request(bad));
    }
    {   // the isUnaware pin is the pinned executable's own prologue, its RVA mapped through the file's section table
        std::ifstream in("artifacts/re-toolchain/direct-ghidra/Dishonored2-steam17576867.exe",std::ios::binary);
        if(!in) std::cout<<"note: pinned executable absent, isUnaware pin not checked\n";
        else {
            IMAGE_DOS_HEADER dos{};in.read(reinterpret_cast<char*>(&dos),static_cast<std::streamsize>(sizeof dos));in.seekg(static_cast<std::streamoff>(dos.e_lfanew));
            IMAGE_NT_HEADERS64 nt{};in.read(reinterpret_cast<char*>(&nt),static_cast<std::streamsize>(sizeof nt));
            std::streamoff at=0;
            for(WORD i=0;i<nt.FileHeader.NumberOfSections && in;++i) {
                IMAGE_SECTION_HEADER s{};in.read(reinterpret_cast<char*>(&s),static_cast<std::streamsize>(sizeof s));
                if(NativeMelee::unaware_rva>=s.VirtualAddress && NativeMelee::unaware_rva+32<=s.VirtualAddress+s.SizeOfRawData)
                    at=static_cast<std::streamoff>(s.PointerToRawData+(NativeMelee::unaware_rva-s.VirtualAddress));
            }
            std::array<unsigned char,32> got{};in.seekg(at);in.read(reinterpret_cast<char*>(got.data()),static_cast<std::streamsize>(got.size()));
            assert(at>0 && in && got==NativeMelee::unaware_code);
        }
    }
    std::cout<<"PASS: mod-flown sword drops flat, tumbles orthonormal, glances off a wall and lands; isUnaware pin matches\n";
}
