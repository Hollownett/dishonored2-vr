// Tier A lab for the held-object clamp (Part 5A): HeldClamp and SafeRelease against a half-space world (a wall at x = 1, a floor at z = 0),
// driven like the game drives them (one frame late sweep answers, smooth 90 Hz input unless a scenario says otherwise).
// Offline model: a rigid tank-like box as 3 proxy spheres on its long (z) axis; the object follows the published pose exactly. It pins the
// logic and the shape of the feel; only the headset (Tier C) declares feel met. Scene geometry is its own copy (push_lab models a hand).
// usage: held_clamp_lab <out_dir>      writes scenarios.json and one CSV per scenario; exit 1 on a judged failure.
#include "drawn_anchor.hpp"
#include "held_clamp.hpp"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
using namespace d2vr;

namespace {
constexpr double dt=1./90,tank_r=.15;
std::string fmt(const char* f,...) {char b[256];va_list a;va_start(a,f);std::vsnprintf(b,sizeof b,f,a);va_end(a);return b;}
std::string quote(const std::string& s) {std::string o="\"";for(char c:s) {if(c=='"'||c=='\\') o+='\\';o+=c;}return o+"\"";}
WorldTrackedPose pose_at(Vec3 p) {return {p,{{{1,0,0},{0,1,0},{0,0,1}}}};}
struct Check {std::string name;bool ok{},info{};std::string value,limit;};
Check below(const std::string& n,double v,double lim,const char* unit="",bool info=false) {return {n,v<=lim,info,fmt("%.4g %s",v,unit),fmt("<= %.4g %s",lim,unit)};}
Check above(const std::string& n,double v,double lim,const char* unit="",bool info=false) {return {n,v>=lim,info,fmt("%.4g %s",v,unit),fmt(">= %.4g %s",lim,unit)};}
Check truth(const std::string& n,bool ok,const std::string& v,bool info=false) {return {n,ok,info,v,"true"};}
struct Scenario {std::string name,title,expect;bool info=false,pass=false;std::vector<Check> checks;std::string csv="t,x,z,req_x,req_z,gap\n";};

HeldProxies tank() {
    const auto p=held_proxies({{-tank_r,-tank_r,-.45},{tank_r,tank_r,.45}},pose_at({0,0,0}));
    return *p;
}
struct World {
    bool wall=true,floor=true;
    // The deepest overlap of any proxy at `pose` (zero-length cast): hit and depth (radius - distance).
    std::optional<std::pair<ClampHit,double>> overlap(const WorldTrackedPose& pose,const HeldProxies& prox) const {
        std::optional<std::pair<ClampHit,double>> best;
        for(std::size_t i=0;i<prox.count;++i) {
            const Vec3 c=clamp_detail::world_point(pose,prox.sphere[i].local);
            const double r=prox.sphere[i].radius;
            const struct {Vec3 point,normal;bool on;} planes[2]{{{1,0,0},{-1,0,0},wall},{{0,0,0},{0,0,1},floor}};
            for(const auto& p:planes) {
                if(!p.on) continue;
                const double s0=dot(c-p.point,p.normal);
                if(s0<r && (!best || r-s0>best->second)) best=std::make_pair(ClampHit{c-p.normal*s0,p.normal,7,3,true},r-s0);
            }
        }
        return best;
    }
    std::optional<ClampHit> cast(const ClampSweep& s) const {
        if(!s.valid) return std::nullopt;
        const struct {Vec3 point,normal;bool on;} planes[2]{{{1,0,0},{-1,0,0},wall},{{0,0,0},{0,0,1},floor}};
        std::optional<ClampHit> best;double best_t=2;
        for(const auto& p:planes) {
            if(!p.on) continue;
            const double s0=dot(s.from-p.point,p.normal),s1=dot(s.to-p.point,p.normal);
            if(s0<s.radius) {if(best_t>=0) {best_t=-1;best=ClampHit{s.from-p.normal*s0,p.normal,7,3,true};}continue;}
            if(s1>=s.radius || best_t<0) continue;
            const double t=(s0-s.radius)/(s0-s1);
            if(t<best_t) {best_t=t;best=ClampHit{s.from+(s.to-s.from)*t-p.normal*s.radius,p.normal,7,3,false};}
        }
        return best;
    }
};
// The held object under the clamp, one frame of hits late like the game thread.
struct Carry {
    HeldClamp clamp;World world;HeldProxies proxies=tank();unsigned frame=0;
    std::array<ClampSweep,held_proxy_capacity> sweeps{};HeldClampOutput out{};WorldTrackedPose body=pose_at({0,0,1});
    double now() const {return frame*dt;}
    void step(const WorldTrackedPose& requested) {
        HeldClampInput in{};in.now=now();in.epoch=1;in.requested=requested;in.proxies=proxies;
        for(std::size_t i=0;i<held_proxy_capacity;++i) in.hits[i]=world.cast(sweeps[i]);
        out=clamp.update(in);sweeps=out.sweeps;
        if(out.valid) {body=out.target;clamp.note_published(out.target);}
        ++frame;
    }
    // Farthest a sphere surface reaches into the wall (x = 1) and lowest point below the floor, at the published body.
    double past_wall() const {double m=-1e9;for(std::size_t i=0;i<proxies.count;++i) m=std::max(m,clamp_detail::world_point(body,proxies.sphere[i].local).x+proxies.sphere[i].radius-1);return m;}
    double lowest() const {double m=1e9;for(std::size_t i=0;i<proxies.count;++i) m=std::min(m,clamp_detail::world_point(body,proxies.sphere[i].local).z-proxies.sphere[i].radius);return m;}
};
void row(Scenario& s,const Carry& c,Vec3 req) {s.csv+=fmt("%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\n",c.now(),c.body.position.x,c.body.position.z,req.x,req.z,c.out.gap_m);}

Scenario wall_carry(double speed,double hold_step=0,bool info=false) {
    Scenario s;
    {std::string v=fmt("%g",speed);std::replace(v.begin(),v.end(),'.','p');s.name=info?"stepped_30ms":"wall_carry_"+v+"_mps";}
    s.title=info?"INFO tank carried into the wall at 2 m/s, hand sampled every 30 ms":fmt("Tank carried into the wall at %g m/s",speed);
    s.expect="after the first answered hit the body ends at most 1 cm past the plane and does not back off more than 3 cm";
    s.info=info;
    Carry c;double x=-.5,held_x=x;double over=-1e9,short_by=-1e9;bool seen=false;
    const unsigned frames=static_cast<unsigned>(2.2/speed/dt)+180;
    for(unsigned f=0;f<frames;++f) {
        x=std::min(x+speed*dt,1.6);
        if(hold_step>0) {if(f%static_cast<unsigned>(std::lround(hold_step/dt))==0) held_x=x;} else held_x=x;
        const Vec3 req{held_x,0,1};
        c.step(pose_at(req));row(s,c,req);
        if(c.out.clamped) seen=true;
        if(seen) {over=std::max(over,c.past_wall());short_by=std::max(short_by,-c.past_wall());}
    }
    s.checks={truth("the wall stopped it",seen,seen?"clamped":"never clamped",info),below("deepest sphere surface past the plane",over*100,1,"cm",info),
              below("backed off from the plane (worst)",short_by*100-0,3,"cm",info)};
    // short_by is the worst (largest) shortfall seen; the closest approach matters, so report the final rest too.
    const double rest=-c.past_wall()*100;
    s.checks.push_back(below("rest distance short of the plane",rest,3,"cm",info));
    return s;
}

// Releases the clamped/placed body: the layer returns only after a free answer (one frame late); gravity then settles it on the floor.
struct ReleaseRun {
    bool restored=false,restored_inside=false,unresolved=false,wrote=false,finite_all=true;double restore_time=-1,max_speed=0;unsigned depen_frames=0;
    double final_bottom=0;
};
ReleaseRun release(const World& world,const HeldProxies& prox,WorldTrackedPose body,Scenario& s,double t0) {
    ReleaseRun r;SafeRelease safe;
    std::optional<std::pair<ClampHit,double>> last_answer=world.overlap(body,prox);bool have=false;
    double vz=0;bool layer_back=false;
    for(unsigned f=0;f<static_cast<unsigned>(5./dt);++f) {
        const double t=t0+f*dt;
        SafeReleaseInput in{};in.now=t;in.released=true;
        in.sweep_answered=have;if(have && last_answer) {in.overlap=last_answer->first;in.depth=last_answer->second;}
        const auto o=safe.update(in);
        if(o.write) {
            r.wrote=true;r.max_speed=std::max(r.max_speed,length(o.velocity));
            body.position=body.position+o.velocity*dt;
            if(!finite(o.velocity)) r.finite_all=false;
        } else if(layer_back) { // free fall onto the floor (the engine's own physics, modelled as gravity and a floor stop)
            vz-=9.81*dt;body.position.z+=vz*dt;
            double low=1e9;for(std::size_t i=0;i<prox.count;++i) low=std::min(low,clamp_detail::world_point(body,prox.sphere[i].local).z-prox.sphere[i].radius);
            if(world.floor && low<0) {body.position.z-=low;vz=0;}
        }
        if(o.restore_layer) {
            layer_back=true;r.restored=true;r.restore_time=t-t0;r.unresolved=o.unresolved;r.depen_frames=static_cast<unsigned>(o.depen_frames);
            if(world.overlap(body,prox) && !o.unresolved) r.restored_inside=true;
        }
        if(!finite(body.position)) r.finite_all=false;
        s.csv+=fmt("%.5f,%.5f,%.5f,0,0,0\n",t,body.position.x,body.position.z);
        last_answer=world.overlap(body,prox);have=true;   // the answer to this frame's cast, delivered next frame
    }
    double low=1e9;for(std::size_t i=0;i<prox.count;++i) low=std::min(low,clamp_detail::world_point(body,prox.sphere[i].local).z-prox.sphere[i].radius);
    r.final_bottom=low;
    return r;
}
std::vector<Check> release_checks(const ReleaseRun& r,bool expect_write) {
    return {truth("layer restored",r.restored,r.restored?fmt("at %.3f s",r.restore_time):"never"),
            truth("restored only after free (or counted unresolved)",!r.restored_inside,r.restored_inside?"restored inside":"clear"),
            truth("release_overlap_unresolved == 0",!r.unresolved,r.unresolved?"1":"0"),
            below("restore time",r.restore_time,.5+dt,"s"),below("largest commanded speed",r.max_speed,1.5+1e-9,"m/s"),
            truth(expect_write?"pushed out with a velocity":"no write",r.wrote==expect_write,r.wrote?"wrote":"no write"),
            truth("all poses finite",r.finite_all,r.finite_all?"yes":"no")};
}

Scenario floor_press() {
    Scenario s;s.name="floor_press";s.title="Tank pressed 0.3 m into the floor, then released";
    s.expect="held at the floor, layer restored only after free, rests above the floor plane after 5 s, nothing unresolved";
    Carry c;c.world.wall=false;
    const double rest_z=.45;   // centre height of a tank standing on the floor
    double lowest_pub=1e9;
    for(unsigned f=0;f<270;++f) {
        const double z=std::max(1.-f*dt*1.,rest_z-.3);
        const Vec3 req{0,0,z};c.step(pose_at(req));row(s,c,req);
        if(f>60) lowest_pub=std::min(lowest_pub,c.lowest());
    }
    auto r=release(c.world,c.proxies,c.body,s,c.now());
    s.checks=release_checks(r,false);
    s.checks.push_back(above("lowest point while pressed",lowest_pub*100,-1,"cm"));
    s.checks.push_back(above("rest height above the floor plane after 5 s",r.final_bottom*100,-1,"cm"));
    s.checks.push_back(below("rest height above the floor plane after 5 s (not floating)",r.final_bottom*100,2,"cm"));
    return s;
}
Scenario release_inside_floor(double depth=.04) {
    Scenario s;s.name=depth>.05?"release_deep_in_floor":"release_inside_floor";
    s.title=fmt("Tank released %g cm inside the floor",depth*100);s.expect="pushed out within 0.5 s at no more than 1.5 m/s, layer restored only once free";
    World w;w.wall=false;const auto prox=tank();
    auto r=release(w,prox,pose_at({0,0,.45-depth}),s,0);
    s.checks=release_checks(r,true);
    return s;
}
Scenario release_free_space() {
    Scenario s;s.name="release_free_space";s.title="Tank released in free space";s.expect="no write, layer restored on the first answer";
    World w;const auto prox=tank();
    auto r=release(w,prox,pose_at({0,0,1.5}),s,0);
    s.checks=release_checks(r,false);
    s.checks.push_back(below("restore time",r.restore_time,dt*1.5,"s"));
    return s;
}
Scenario corner() {
    Scenario s;s.name="corner";s.title="Tank pressed into the wall-and-floor corner, then released";s.expect="clamped outside both planes, release terminates with finite poses";
    Carry c;
    for(unsigned f=0;f<270;++f) {
        const double k=std::min(1.,f*dt);
        const Vec3 req{-.5+1.8*k,0,1.-1.3*k};c.step(pose_at(req));row(s,c,req);
    }
    const double pw=c.past_wall(),low=c.lowest();
    auto r=release(c.world,c.proxies,c.body,s,c.now());
    s.checks=release_checks(r,false);
    s.checks.push_back(below("past the wall while pressed",pw*100,1,"cm"));
    s.checks.push_back(above("lowest point while pressed",low*100,-1,"cm"));
    // And a release that starts inside both planes: resolves or is counted.
    World w;const auto prox=tank();Scenario t;
    auto q=release(w,prox,pose_at({1-tank_r+.03,0,.45-.03}),t,0);
    s.checks.push_back(truth("release from inside both planes terminates",q.restored && q.restore_time<=.5+dt && q.finite_all,fmt("%.3f s",q.restore_time)));
    s.checks.push_back(below("speed from inside both planes",q.max_speed,1.5+1e-9,"m/s"));
    return s;
}
Scenario blocked_hand_lag() {
    Scenario s;s.name="blocked_hand_lag";s.title="Hand keeps going 0.5 m past a stopped tank";
    s.expect="drawn hand stays within 0.35 m of the controller (DrawnAnchor::bound_m); release 0.25 s after the gap passes 0.35 m";
    Carry c;DrawnAnchor anchor;
    double x=.2,crossing=-1,release_time=-1,worst=0;
    const double speed=1.;
    for(unsigned f=0;f<400 && release_time<0;++f) {
        x+=speed*dt;
        const Vec3 raw{x,0,1};c.step(pose_at(raw));row(s,c,raw);
        const Vec3 grasp=c.body.position;
        const auto drawn=anchor.update(0,true,raw,grasp,c.now());
        // The caller's drawn pose: the grasp point on the stopped body, bounded to bound_m from the controller.
        Vec3 shown=drawn.position;
        const double gap=length(raw-shown);
        if(drawn.weight>=1) worst=std::max(worst,gap);
        if(crossing<0 && length(raw-c.body.position)>HeldClampConfig{}.release_gap) crossing=c.now();
        if(c.clamp.should_release()) release_time=c.now();
    }
    s.checks={truth("hold released",release_time>=0,release_time>=0?fmt("at %.3f s",release_time):"never"),
              below("worst drawn-hand gap from the controller",worst,DrawnAnchor::bound_m+1e-9,"m"),
              above("held at least 0.25 s past the 0.35 m gap",release_time-crossing,.25-dt,"s"),
              below("held at most 0.25 s + 3 frames past the gap",release_time-crossing,.25+3*dt,"s")};
    return s;
}
} // namespace

int main(int argc,char** argv) {
    if(argc!=2) {std::fprintf(stderr,"usage: held_clamp_lab <out_dir>\n");return 2;}
    const std::filesystem::path dir=argv[1];std::filesystem::create_directories(dir);
    std::vector<Scenario> all;
    for(double v:{.1,.5,1.,2.,3.}) all.push_back(wall_carry(v));
    all.push_back(floor_press());all.push_back(release_inside_floor());all.push_back(release_inside_floor(.10));all.push_back(release_free_space());all.push_back(corner());
    all.push_back(blocked_hand_lag());
    all.push_back(wall_carry(2.,.03,true));
    unsigned failed=0;
    std::string json="{\"honesty\":"+quote("Offline model, not the Havok solver: the held object is 3 proxy spheres that follow the published pose exactly, a wall at x = 1 and a floor at z = 0 as half-spaces, gravity as a floor stop. HeldClamp, SafeRelease and DrawnAnchor are the production headers. It pins logic and the shape of the feel; only the headset (Tier C) declares feel met.")+",\"scenarios\":[";
    for(std::size_t n=0;n<all.size();++n) {
        auto& s=all[n];
        s.pass=true;for(const auto& c:s.checks) s.pass=s.pass && (c.ok || c.info);
        if(!s.info && !s.pass) ++failed;
        std::ofstream(dir/(s.name+".csv"))<<s.csv;
        std::printf("%-22s %s\n",s.name.c_str(),s.info?"info":s.pass?"pass":"FAIL");
        for(const auto& c:s.checks) if(!c.ok && !s.info) std::printf("    FAIL %s: %s (want %s)\n",c.name.c_str(),c.value.c_str(),c.limit.c_str());
        json+=std::string(n?",":"")+"{\"name\":"+quote(s.name)+",\"title\":"+quote(s.title)+",\"expect\":"+quote(s.expect)+",\"status\":"+quote(s.info?"info":s.pass?"pass":"FAIL")+",\"checks\":[";
        for(std::size_t k=0;k<s.checks.size();++k) json+=std::string(k?",":"")+"{\"name\":"+quote(s.checks[k].name)+",\"ok\":"+(s.checks[k].ok?"true":"false")+",\"info\":"+(s.checks[k].info?"true":"false")+",\"value\":"+quote(s.checks[k].value)+",\"limit\":"+quote(s.checks[k].limit)+"}";
        json+="]}";
    }
    json+="]}\n";
    std::ofstream(dir/"scenarios.json")<<json;
    std::printf("held_clamp_lab: %zu scenarios, %u failed\n",all.size(),failed);
    return failed?1:0;
}
