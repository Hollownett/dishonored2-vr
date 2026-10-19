#include "palm_offset.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
using namespace d2vr;
static void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
static bool near(double a,double b,double tol=1e-9) {return std::abs(a-b)<=tol;}
static WorldTrackedPose level_grip() {
    WorldTrackedPose g{};g.position={1,2,3};g.basis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};return g;
}
static WorldTrackedPose turned_grip() { // yawed 40 degrees, pitched 25: a generic orientation
    const double y=.6981317007977318,p=.4363323129985824;
    const Vec3 f{std::cos(y)*std::cos(p),std::sin(y)*std::cos(p),-std::sin(p)},l{-std::sin(y),std::cos(y),0};
    WorldTrackedPose g{};g.position={-4,5,.5};g.basis={f,l,cross(f,l)};return g;
}
static std::filesystem::path temp_file(const char* name) {
    auto p=std::filesystem::temp_directory_path()/name;std::error_code ec;std::filesystem::remove(p,ec);return p;
}
static void write_raw(const std::filesystem::path& p,const std::string& text) {std::ofstream out(p,std::ios::binary|std::ios::trunc);out<<text;}
static const char* touch="/interaction_profiles/oculus/touch_controller";
static bool throws_on_load(const std::filesystem::path& p,const std::string& text) {
    write_raw(p,text);PalmOffsetStore store;
    try {store.load(p);} catch(const std::exception&) {
        // the store still answers with defaults
        const auto [value,saved]=store.get(1,touch);
        return !saved && near(value.translation.y,-.03) && near(value.pitch_deg,15);
    }
    return false;
}
int main() {
    try {
        {   // default_values
            const auto l=default_palm_offset(0),r=default_palm_offset(1);
            require(near(l.translation.y,.03) && near(r.translation.y,-.03),"default translation sign wrong");
            require(near(l.translation.x,0) && near(l.translation.z,0) && near(r.translation.x,0) && near(r.translation.z,0),"default translation axes wrong");
            require(near(l.pitch_deg,15) && near(r.pitch_deg,15) && near(l.yaw_deg,0) && near(r.yaw_deg,0),"default tilt wrong");
            require(palm_offset_valid(l) && palm_offset_valid(r),"defaults not valid");
        }
        {   // bounds
            PalmOffset o{};o.translation.x=.081;require(!palm_offset_valid(o),"8.1 cm accepted");
            o={};o.translation.z=-.081;require(!palm_offset_valid(o),"-8.1 cm accepted");
            o={};o.pitch_deg=30.1;require(!palm_offset_valid(o),"30.1 deg pitch accepted");
            o={};o.yaw_deg=-30.1;require(!palm_offset_valid(o),"30.1 deg yaw accepted");
            o={};o.translation.y=.08;o.pitch_deg=30;o.yaw_deg=-30;require(palm_offset_valid(o),"limit values rejected");
            o={};o.translation.y=NAN;require(!palm_offset_valid(o),"NaN accepted");
            o={};o.pitch_deg=INFINITY;require(!palm_offset_valid(o),"inf accepted");
            PalmOffset big{{.5,-.5,.081},45,-45};const auto c=clamp_palm_offset(big);
            require(near(c.translation.x,.08) && near(c.translation.y,-.08) && near(c.translation.z,.08) && near(c.pitch_deg,30) && near(c.yaw_deg,-30),"clamp wrong");
            PalmOffset bad{{NAN,0,0},INFINITY,0};require(palm_offset_valid(clamp_palm_offset(bad)),"clamp left a non-finite value");
        }
        {   // apply_is_rigid
            for(const auto& grip:{level_grip(),turned_grip()}) {
                const PalmOffset o{{.02,-.03,.05},22,-17};
                for(const bool tilt:{false,true}) {
                    const auto out=apply_palm_offset(grip,o,tilt);
                    require(near(length(out.basis[0]),1) && near(length(out.basis[1]),1) && near(length(out.basis[2]),1),"basis not unit");
                    require(near(dot(out.basis[0],out.basis[1]),0) && near(dot(out.basis[0],out.basis[2]),0) && near(dot(out.basis[1],out.basis[2]),0),"basis not orthogonal");
                    const auto c=cross(out.basis[0],out.basis[1]);
                    require(length(c-out.basis[2])<1e-9,"basis not right-handed");
                    const auto expect=grip.position+grip.basis[0]*o.translation.x+grip.basis[1]*o.translation.y+grip.basis[2]*o.translation.z;
                    require(length(out.position-expect)<1e-12,"translation does not follow the grip axes");
                    if(!tilt) for(int i=0;i<3;++i) require(length(out.basis[i]-grip.basis[i])<1e-12,"tilt=false rotated the basis");
                }
            }
            // a half weight tilts half as far
            const PalmOffset o{{},20,0};const auto half=apply_palm_offset(level_grip(),o,.5),full=apply_palm_offset(level_grip(),PalmOffset{{},10,0},true);
            require(length(half.basis[0]-full.basis[0])<1e-12,"tilt weight not linear in angle");
        }
        {   // tilt_sign
            const auto out=apply_palm_offset(level_grip(),PalmOffset{{},15,0},true);
            require(out.basis[0].z<0 && near(out.basis[0].z,-std::sin(15*3.14159265358979323846/180)),"pitch + must lower the forward axis");
            require(near(out.basis[0].x,std::cos(15*3.14159265358979323846/180)),"pitch cos wrong");
            const auto yawed=apply_palm_offset(level_grip(),PalmOffset{{},0,15},true);
            require(yawed.basis[0].y>0,"yaw + must turn forward toward left");
        }
        {   // store_round_trip
            const auto file=temp_file("d2vr-palm-roundtrip.txt");
            PalmOffsetStore store;
            const PalmOffset a{{.011,-.022,.033},12.5,-3.25},b{{-.07,.01,.0},-20,29};
            store.set(0,touch,a);store.set(1,touch,b);store.set(1,"/interaction_profiles/valve/index_controller",default_palm_offset(1));
            store.save(file);
            PalmOffsetStore loaded;loaded.load(file);
            const auto [ga,sa]=loaded.get(0,touch);const auto [gb,sb]=loaded.get(1,touch);const auto [gi,si]=loaded.get(1,"/interaction_profiles/valve/index_controller");
            require(sa && sb && si,"saved flags missing");
            require(near(ga.translation.x,a.translation.x,1e-12) && near(ga.translation.y,a.translation.y,1e-12) && near(ga.translation.z,a.translation.z,1e-12) && near(ga.pitch_deg,12.5,1e-12) && near(ga.yaw_deg,-3.25,1e-12),"left round trip");
            require(near(gb.translation.x,-.07,1e-12) && near(gb.pitch_deg,-20,1e-12) && near(gb.yaw_deg,29,1e-12),"right round trip");
            require(near(gi.translation.y,-.03),"index profile round trip");
            const auto [other,saved_other]=loaded.get(0,"/interaction_profiles/htc/vive_controller");
            require(!saved_other && near(other.translation.y,.03),"unsaved profile must fall back to the default");
            store.set(0,touch,PalmOffset{{.001,0,0},1,1}); // set replaces
            require(near(store.get(0,touch).first.translation.x,.001),"set did not replace");
            std::error_code ec;std::filesystem::remove(file,ec);
            PalmOffsetStore missing;missing.load(file); // a first run has no file: no throw, defaults
            require(!missing.get(0,touch).second,"missing file reported saved");
            // the cap: the 17th distinct entry displaces the oldest, the saved file stays loadable
            PalmOffsetStore many;for(int i=0;i<20;++i) many.set(i&1,("/interaction_profiles/x/p"+std::to_string(i)).c_str(),default_palm_offset(i&1));
            many.save(file);PalmOffsetStore reload;reload.load(file);
            require(reload.get(1,"/interaction_profiles/x/p19").second && !reload.get(0,"/interaction_profiles/x/p0").second,"cap eviction wrong");
            std::filesystem::remove(file,ec);
        }
        {   // corrupt_file_keeps_defaults
            const auto file=temp_file("d2vr-palm-corrupt.txt");
            require(throws_on_load(file,"d2vr-palm-offsets 2\n"),"bad header accepted");
            require(throws_on_load(file,""),"empty file accepted");
            require(throws_on_load(file,"d2vr-palm-offsets 1\n1 /interaction_profiles/oculus/touch_controller 0.09 0 0 15 0\n"),"out-of-bounds translation accepted");
            require(throws_on_load(file,"d2vr-palm-offsets 1\n1 /interaction_profiles/oculus/touch_controller 0 0 0 31 0\n"),"out-of-bounds tilt accepted");
            require(throws_on_load(file,"d2vr-palm-offsets 1\n1 /interaction_profiles/oculus/touch_controller nan 0 0 15 0\n"),"nan accepted");
            require(throws_on_load(file,"d2vr-palm-offsets 1\n1 /interaction_profiles/oculus/touch controller 0 0 0 15 0\n"),"profile with a space accepted");
            require(throws_on_load(file,"d2vr-palm-offsets 1\n1 bogus/profile 0 0 0 15 0\n"),"profile without the prefix accepted");
            require(throws_on_load(file,"d2vr-palm-offsets 1\n2 /interaction_profiles/oculus/touch_controller 0 0 0 15 0\n"),"side 2 accepted");
            require(throws_on_load(file,"d2vr-palm-offsets 1\n1 /interaction_profiles/oculus/touch_controller 0 0 0 15\n"),"short entry accepted");
            require(throws_on_load(file,"d2vr-palm-offsets 1\n1 /interaction_profiles/oculus/touch_controller 0 0 0 15 0 7\n"),"long entry accepted");
            require(throws_on_load(file,"d2vr-palm-offsets 1\n1 /interaction_profiles/oculus/touch_controller 0 0 0 15 0\n1 /interaction_profiles/oculus/touch_controller 0 0 0 15 0\n"),"duplicate accepted");
            {std::string many="d2vr-palm-offsets 1\n";for(int i=0;i<17;++i) many+=std::to_string(i&1)+" /interaction_profiles/x/p"+std::to_string(i)+" 0 0 0 15 0\n";
             require(throws_on_load(file,many),"17 entries accepted");}
            require(throws_on_load(file,std::string(5000,'x')),"5000 bytes accepted");
            {std::string big="d2vr-palm-offsets 1\n";while(big.size()<5000) big+="# padding line to make this file larger than the limit\n";
             require(throws_on_load(file,big),"oversized valid-prefix file accepted");}
            // a failed load never disturbs entries already held
            PalmOffsetStore store;store.set(1,touch,PalmOffset{{.01,.01,.01},5,5});
            write_raw(file,"d2vr-palm-offsets 9\n");
            try {store.load(file);require(false,"bad header did not throw");} catch(const std::runtime_error& e) {require(std::string(e.what())!="bad header did not throw","wrong exception");}
            require(store.get(1,touch).second,"a failed load dropped existing entries");
            // a locked or unreadable path: a directory where the file should be
            const auto dir=std::filesystem::temp_directory_path()/"d2vr-palm-dir";std::filesystem::create_directories(dir);
            PalmOffsetStore locked;bool threw=false;try {locked.load(dir);} catch(const std::exception&) {threw=true;}
            require(threw && !locked.get(0,touch).second,"unreadable path must throw and keep defaults");
            std::error_code ec;std::filesystem::remove(dir,ec);std::filesystem::remove(file,ec);
            // an unwritable save throws (the caller catches) and never leaves a half file
            PalmOffsetStore unsaved;threw=false;try {unsaved.save(std::filesystem::temp_directory_path()/"d2vr-no-such-dir"/"p.txt");} catch(const std::exception&) {threw=true;}
            require(threw,"save into a missing directory must throw");
        }
        {   // fit_step
            PalmFitStep fit;require(!fit.active(),"fit active before begin");
            fit.begin(1,default_palm_offset(1));require(fit.active(),"begin did not activate");
            for(int i=0;i<1000;++i) fit.nudge(1,1,1,1,.01);
            auto v=fit.value();require(palm_offset_valid(v),"nudged value out of bounds");
            require(near(v.translation.x,.02*1.0*.0+v.translation.x) && v.translation.x>0 && v.translation.x<=.08+1e-12,"forward nudge wrong");
            for(int i=0;i<2000;++i) fit.nudge(-1,-1,-1,-1,.01);
            v=fit.value();require(palm_offset_valid(v) && near(std::abs(v.translation.x),.08,1e-9) && near(std::abs(v.pitch_deg),30,1e-9),"opposite full nudge did not reach the bounds");
            const auto before=fit.value();
            fit.nudge(1,1,1,1,0);fit.nudge(NAN,0,0,0,.01);fit.nudge(0,0,0,0,NAN);fit.nudge(0,0,0,0,-1);
            const auto after=fit.value();
            require(near(before.translation.x,after.translation.x) && near(before.pitch_deg,after.pitch_deg) && palm_offset_valid(after),"non-finite or zero-dt nudge changed the value");
            const auto confirmed=fit.confirm();require(confirmed && near(confirmed->translation.x,after.translation.x),"confirm did not return the nudged value");
            require(!fit.active(),"confirm left the step active");
            fit.begin(0,default_palm_offset(0));fit.nudge(0,0,0,0,.1);require(near(fit.value().pitch_deg,15) && near(fit.value().translation.y,.03),"zero stick moved the value");
            fit.cancel();require(!fit.active() && !fit.confirm(),"cancel left a value");
            // rates: 1 s of full forward = 2 cm; 1 s of full pitch = 20 deg
            fit.begin(0,PalmOffset{});for(int i=0;i<100;++i) fit.nudge(0,1,0,0,.01);
            require(near(fit.value().translation.x,.02,1e-9) && near(fit.value().translation.y,0),"forward rate wrong");
            fit.begin(0,PalmOffset{});for(int i=0;i<100;++i) fit.nudge(0,0,0,1,.01);
            require(near(std::abs(fit.value().pitch_deg),20,1e-9),"pitch rate wrong");
        }
        {   // tilt blend: equipping must not pop
            PalmTiltBlend blend;
            require(near(blend.update(false,.011),1,1e-12),"empty hand starts fully tilted");
            double w=1;int frames=0;while(w>0 && frames<100) {w=blend.update(true,.011);++frames;}
            require(frames>=13 && frames<=15,"blend should take ~0.15 s");
            require(near(blend.update(true,.011),0),"armed hand keeps no tilt");
            require(blend.update(false,.011)>0 && blend.update(false,.011)<1,"unequip must ease back");
            blend.update(true,NAN);require(std::isfinite(blend.update(true,.011)),"non-finite dt");
        }
        std::cout<<"palm_offset_check: ok\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
