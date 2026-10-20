#include "grab_mass.hpp"
#include "mass_probe.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
bool near(double a,double b,double tol=1e-9) {return std::fabs(a-b)<=tol;}
std::uint16_t half_of(float f) {std::uint32_t b;std::memcpy(&b,&f,4);return static_cast<std::uint16_t>(b>>16);}
}
int main() {
    {   // class_table_pinned
        const double want[17]={12,10,18,1.5,4.5,2.0,1.6,.6,2.0,1.6,.6,7,3.4,1.2,7,3.4,1.2};
        double sum=0; // the pinned values sum to 77.6; the 75 kg total is its own constant (hanging mass uses it)
        for(int i=0;i<17;++i) {CHECK(ragdoll_class_kg[static_cast<std::size_t>(i)]==want[i]);sum+=want[i];}
        CHECK(near(sum,77.6) && body_class_total_kg==75. && prop_class_kg==12.);
    }
    {   // names_pinned
        CHECK(std::string(ragdoll_bone_name(0))=="pelvis" && std::string(ragdoll_bone_name(2))=="chest" && std::string(ragdoll_bone_name(3))=="neck");
        CHECK(std::string(ragdoll_bone_name(4))=="head" && std::string(ragdoll_bone_name(16))=="r_foot" && std::string(ragdoll_bone_name(17))=="?");
        CHECK(std::string(ragdoll_bone_name(-1))=="?");
    }
    {   // garbage_mass_rejected
        const std::uint16_t garbage[]={0,0xffff,0x7fc0,0x7f80,0x0001,0x8000};
        for(auto g:garbage) for(bool measured:{false,true}) {
            const auto m=ragdoll_body_mass(6,g,measured);
            CHECK(m.kg==1.6 && m.rejected && m.source==MassSource::cls);
        }
        const NativeGrabBounds b{{0,0,0},{.2,.2,.2}};
        for(auto g:garbage) {const auto m=prop_body_mass(g,b,true);CHECK(m.rejected && m.source==MassSource::cls && near(m.kg,prop_class_estimate(b)));}
        // implausibly far from the class (more than 12x) is rejected too
        CHECK(ragdoll_body_mass(6,half_of(1.f/100),true).rejected);
        CHECK(ragdoll_body_mass(6,half_of(1.f/.05f),true).rejected);
    }
    {   // plausible_read_ignored_until_enabled
        const auto h=half_of(.5f); // 2 kg
        const auto off=ragdoll_body_mass(6,h,false);
        CHECK(near(off.kg,1.6) && off.source==MassSource::cls && !off.rejected);
        const auto on=ragdoll_body_mass(6,h,true);
        CHECK(near(on.kg,2.0) && on.source==MassSource::measured && !on.rejected);
        const NativeGrabBounds b{{0,0,0},{.2,.2,.2}}; // 2.8 kg by volume
        CHECK(prop_body_mass(half_of(.25f),b,true).source==MassSource::measured && near(prop_body_mass(half_of(.25f),b,true).kg,4.));
        CHECK(prop_body_mass(half_of(.25f),b,false).source==MassSource::cls);
    }
    {   // no_read_is_not_rejected
        const auto m=ragdoll_body_mass(6,std::nullopt,true);
        CHECK(!m.rejected && m.kg==1.6 && m.source==MassSource::cls);
        CHECK(!prop_body_mass(std::nullopt,NativeGrabBounds{{0,0,0},{.1,.1,.1}},true).rejected);
    }
    {   // hanging_mass_ramp
        GrabTuning t;
        CHECK(hanging_mass(7,0,t)==0);
        CHECK(t.lift_fraction==.25);   // lab-tuned default (tools/corpse_drag_lab.cpp)
        CHECK(near(hanging_mass(7,.125,t),.25*.5*(75-.6)));
        CHECK(near(hanging_mass(7,.25,t),.25*(75-.6)) && near(hanging_mass(7,3,t),.25*(75-.6)));
        CHECK(hanging_mass(7,-1,t)==0 && hanging_mass(7,std::nan(""),t)==0 && hanging_mass(17,1,t)==0);
    }
    {   // csv row field count equals the header's (plain struct, no fixture)
        MassProbeRow r;r.bone=2;
        auto count=[](const std::string& s) {return std::count(s.begin(),s.end(),',');};
        CHECK(count(mass_probe_csv_row(r))==count(mass_probe_csv_header()));
    }
    std::puts("PASS: grab_mass class table, names, plausibility gate, hanging mass ramp");
}
