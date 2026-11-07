// Offline hand-pose lab: poses the RIGHT arm in full-body mode with the CURRENT finger solver and writes a frames JSON for
//   python tools/render_body_ik.py <frames.json> <out_dir> --hand-views
// usage: hand_pose_lab <out.json> [corvo|emily] [--snapshot FILE[:index]]    (tools/hand_lab.sh [tag] compiles, runs and renders everything)
// --snapshot poses and renders exactly one record of artifacts/finger-snapshots.jsonl (the live solver inputs the game wrote: palm pose, shapes,
// squeeze, trigger, reach) instead of the built-in scenes; `index` is the 0-based line, negative from the end, default the last.
//   --refit rebuilds the held shape from the record's raw bounds and axes with the current code (held_cylinder, the radius profile, seat_held) instead of
//   replaying the shape the game had; --along F sets the grip point (fraction of the shaft: 0.3 body, 0.85 neck) and --profile r0,..,r7 the cross-section
//   radius at eight stations along it (a record from a run with the profile casts carries its own). A record that carries the prop as the hand
//   targeted it (held.cylinder.pre) and the controller's grip at the press (held.touch.press) has the touch measured again on those, as the game does.
//   --lift (held_lift) / --lift-mm N stand the shaft off the palm skin: an experiment for thick props (the game seats with none).
// --palette FILE[:index] draws the hand the game really submitted (artifacts/arm-palettes.jsonl, one 69-joint palette per line while a hand held something)
//   instead of the solver's pose, carried onto the replay's palm; --write-palette FILE writes the solver's own right arm in that format;
//   --write-geometry FILE writes the finger geometry (palm normal, hinges, thumb bind) as the game writes artifacts/finger-geometry.jsonl: diff the two.
// It follows dxgi_probe's finger path: hand_geometry(bind) -> finger_contact(shapes) -> finger_turns -> body::Inputs.finger_turn,
// then reads the posed joints back to report what each joint really did. Read only: nothing here touches src/.
#include "../src/full_body_driver.hpp"
#include "../src/finger_ik.hpp"
#include "../src/finger_snapshot.hpp"
#include "../src/finger_sphere.hpp"
#include "../src/physical_grab.hpp"
#include "../src/weapon_draw_place.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <iostream>
#include <memory>
#include <sstream>

using namespace d2vr;
namespace r=d2vr::rig;
namespace bd=d2vr::body;
using bd::Xf;

namespace {
constexpr double kDeg=180/3.14159265358979323846;
const char* const kFingers[5]={"thumb","index","medium","ring","pinky"};

std::string num(double v) {char b[48];std::snprintf(b,sizeof b,"%.6f",v);return b;}
std::string vec(Vec3 v) {return "["+num(v.x)+","+num(v.y)+","+num(v.z)+"]";}
// v1080 real shapes (src/prop_hull.hpp): each prop's render mesh (artifacts/prop-meshes/<kind>.json) as the runtime builds it from prop-shapes.bin - the
// whole mesh's frame and its convex slabs - and its Havok hull (artifacts/prop-meshes/hulls.jsonl), both read from the game's files by
// tools/prop_mesh_export. Local game data like the player asset pack: never committed; without the files there are no real-shape scenes.
struct RealProp {std::string kind,source;d2vr::PropShape shape;std::vector<Vec3> mesh;std::vector<std::array<int,3>> tris;std::size_t hull_vertices{};};
std::map<const d2vr::ConvexHull*,const RealProp*> real_of;
std::string only_prefix; // --only PREFIX: just the scenes whose name starts with it // the first piece of each prop draws its render mesh
std::vector<double> json_numbers(const std::string& s,const std::string& key) { // every number in the (nested) array after "key"
    std::vector<double> out;auto at=s.find("\""+key+"\"");if(at==std::string::npos) return out;at=s.find('[',at);if(at==std::string::npos) return out;
    int depth=0;for(const char* p=s.c_str()+at;*p;) {
        if(*p=='[') {++depth;++p;} else if(*p==']') {if(--depth==0) break;++p;}
        else if((*p>='0' && *p<='9') || *p=='-' || *p=='.') {char* e{};out.push_back(std::strtod(p,&e));p=e;} else ++p;}
    return out;
}
std::string json_text(const std::string& s,const std::string& key) {
    auto at=s.find("\""+key+"\"");if(at==std::string::npos) return {};at=s.find('"',s.find(':',at)+1);return at==std::string::npos?std::string{}:s.substr(at+1,s.find('"',at+1)-at-1);}
double json_value(const std::string& s,const std::string& key,double fallback) {auto at=s.find("\""+key+"\"");return at==std::string::npos?fallback:std::strtod(s.c_str()+s.find(':',at)+1,nullptr);}
// [[[x,y,z],...],...] after "key": a contact-sheet prop's Havok hull runs (one per child of a compound)
std::vector<std::vector<Vec3>> json_runs(const std::string& s,const std::string& key) {
    std::vector<std::vector<Vec3>> out;auto at=s.find("\""+key+"\"");if(at==std::string::npos) return out;at=s.find('[',at);if(at==std::string::npos) return out;
    int depth=0;std::vector<double> nums;
    for(const char* p=s.c_str()+at;*p;) {
        if(*p=='[') {if(++depth==2) out.emplace_back();++p;}
        else if(*p==']') {if(depth==3 && nums.size()>=3) out.back().push_back({nums[0],nums[1],nums[2]});nums.clear();if(--depth==0) break;++p;}
        else if((*p>='0' && *p<='9') || *p=='-' || *p=='.') {char* e{};nums.push_back(std::strtod(p,&e));p=e;} else ++p;
    }
    return out;
}
std::vector<RealProp>& real_props() {
    static std::vector<RealProp> props;static bool loaded=false;if(loaded) return props;loaded=true;
    std::map<std::string,std::pair<std::vector<Vec3>,double>> havok; // the biggest Havok hull per kind
    std::ifstream hulls("artifacts/prop-meshes/hulls.jsonl");
    for(std::string line;std::getline(hulls,line);) {
        const auto kind=json_text(line,"kind");const auto v=json_numbers(line,"vertices");if(kind.empty() || v.size()<3) continue;
        std::vector<Vec3> p;for(std::size_t i=0;i+2<v.size();i+=3) p.push_back({v[i],v[i+1],v[i+2]});
        if(p.size()>havok[kind].first.size()) havok[kind]={p,json_value(line,"radius",0)};
    }
    for(const char* kind:{"book","spoon","screwdriver","bottle","plate","box","head"}) {
        std::ifstream f(std::string("artifacts/prop-meshes/")+kind+".json");if(!f) continue;
        const std::string s((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());const auto v=json_numbers(s,"vertices"),t=json_numbers(s,"triangles");
        RealProp r{};r.kind=kind;for(std::size_t i=0;i+2<v.size();i+=3) r.mesh.push_back({v[i],v[i+1],v[i+2]});
        for(std::size_t i=0;i+2<t.size();i+=3) r.tris.push_back({int(t[i]),int(t[i+1]),int(t[i+2])});
        r.hull_vertices=havok[kind].first.size();
        // as the DLL finds it: prop-shapes.bin under the key of the Havok hull's vertices (also padded to a multiple of 4 with the last one, as Havok stores them)
        static const std::size_t db_entries=d2vr::load_prop_shape_db("artifacts/prop-meshes/prop-shapes.bin");(void)db_entries;
        auto hv=havok[kind].first;const std::vector<Vec3>* found=nullptr;
        for(int pad=0;pad<2 && !found && !hv.empty();++pad) {if(pad) while(hv.size()%4) hv.push_back(hv.back());
            if(const auto it=d2vr::prop_shape_db().find(d2vr::hull_key(hv));it!=d2vr::prop_shape_db().end()) found=&it->second;}
        if(found) {if(auto ps=d2vr::build_prop_shape(*found,0,true)) {r.shape=std::move(*ps);r.source="prop-shapes.bin by its Havok hull";props.push_back(std::move(r));}}
        else if(auto ps=d2vr::build_prop_shape(r.mesh,0,true)) {r.shape=std::move(*ps);r.source="render mesh vertices (no database entry)";props.push_back(std::move(r));}
    }
    // v1082 contact sheet (tools/prop_mesh_export: DB=1 SHEET=n ./run.sh): the user's examples and a random sample of the database, each found in
    // prop-shapes.bin by the key its Havok shape gives at run time (a compound: its children's keys, prop_hull.hpp compound_key), as the DLL does.
    static const std::size_t sheet_db=d2vr::load_prop_shape_db("artifacts/prop-meshes/prop-shapes.bin");(void)sheet_db;
    std::ifstream sheet("artifacts/prop-meshes/sheet.jsonl");
    for(std::string line;std::getline(sheet,line);) {
        RealProp r{};r.kind="sheet_"+json_text(line,"name");const auto runs=json_runs(line,"children");
        const auto v=json_numbers(line,"mesh"),t=json_numbers(line,"triangles");
        for(std::size_t i=0;i+2<v.size();i+=3) r.mesh.push_back({v[i],v[i+1],v[i+2]});
        for(std::size_t i=0;i+2<t.size();i+=3) r.tris.push_back({int(t[i]),int(t[i+1]),int(t[i+2])});
        if(runs.empty() || r.mesh.empty()) continue;
        const std::vector<Vec3>* found=nullptr;auto& db=d2vr::prop_shape_db();
        if(runs.size()==1) {auto hv=runs[0];for(int pad=0;pad<2 && !found;++pad) {if(pad) while(hv.size()%4) hv.push_back(hv.back());if(const auto it=db.find(d2vr::hull_key(hv));it!=db.end()) found=&it->second;}}
        else {std::vector<std::uint64_t> keys;for(const auto& c:runs) keys.push_back(d2vr::hull_key(d2vr::hull_unpadded(c)));if(const auto it=db.find(d2vr::compound_key(keys));it!=db.end()) found=&it->second;}
        for(const auto& c:runs) r.hull_vertices+=c.size();
        if(!found) {std::fprintf(stderr,"%s: no database entry for its %zu-hull Havok shape\n",r.kind.c_str(),runs.size());continue;}
        if(auto ps=d2vr::build_prop_shape(*found,0,true)) {r.shape=std::move(*ps);r.source=std::string("prop-shapes.bin by its Havok ")+(runs.size()>1?std::to_string(runs.size())+"-hull compound":"hull")+" ("+json_text(line,"kind")+" file)";props.push_back(std::move(r));}
    }
    // body parts as the game holds them (a corpse's bones, a severed part): their own Havok hull from the live dump (prop-hulls.jsonl lines + a "name")
    std::ifstream parts("artifacts/prop-meshes/parts.jsonl");
    for(std::string line;std::getline(parts,line);) {
        const auto v=json_numbers(line,"vertices");std::vector<Vec3> q;for(std::size_t i=0;i+2<v.size();i+=3) q.push_back({v[i],v[i+1],v[i+2]});
        RealProp r{};r.kind="sheet_"+json_text(line,"name");r.hull_vertices=q.size();
        if(auto ps=d2vr::build_prop_shape(q,json_value(line,"radius",0),false)) {r.shape=std::move(*ps);r.source="its own Havok hull (live dump)";props.push_back(std::move(r));}
    }
    for(const auto& p:props) real_of[&p.shape.pieces.front()]=&p;
    return props;
}
Vec3 closest_on_triangle(Vec3 p,Vec3 a,Vec3 b,Vec3 c) {
    const Vec3 ab=b-a,ac=c-a,ap=p-a;const double d1=dot(ab,ap),d2=dot(ac,ap);if(d1<=0 && d2<=0) return a;
    const Vec3 bp=p-b;const double d3=dot(ab,bp),d4=dot(ac,bp);if(d3>=0 && d4<=d3) return b;
    const double vc=d1*d4-d3*d2;if(vc<=0 && d1>=0 && d3<=0) return a+ab*(d1/(d1-d3));
    const Vec3 cp=p-c;const double d5=dot(ab,cp),d6=dot(ac,cp);if(d6>=0 && d5<=d6) return c;
    const double vb=d5*d2-d1*d6;if(vb<=0 && d2>=0 && d6<=0) return a+ac*(d2/(d2-d6));
    const double va=d3*d6-d5*d4;if(va<=0 && (d4-d3)>=0 && (d5-d6)>=0) return b+(c-b)*((d4-d3)/((d4-d3)+(d5-d6)));
    const double den=1/(va+vb+vc);return a+ab*(vb*den)+ac*(vc*den);
}
// v1082 running path (user 2026-11-07: "some desynchronization with hand and object when running"): a prop is taken (still, or while walking at
// 2 m/s), held still 1 s, then the player runs at 5 m/s with the hand swinging (+-12 cm, +-20 deg at 1.3 Hz, a 4 cm bob) for 3 s. Against the hand *
// the hold: the v1073 draw (weapon_draw_place.hpp HeldBind learning the hold from the physics body, PhysicalGrab's velocity motor integrated at
// 60 Hz, the engine frame one tick old) and its finger shapes (read from the body 0.2-0.6 s in), and the v1074 exact draw (hand * the grab's own
// hold, PhysicalGrab::relative) with v1082's fingers (read at the hold). The real classes; the hand frame is the palm (rigid with the grip).
std::string running_report(const WorldTrackedPose& hold) {
    namespace wd=d2vr::weapon_draw;std::string out;
    for(const double walk_grab:{0.,2.}) {
        const auto player_x=[&](double t) {if(t<1) return walk_grab*t;const double u=t-1,v0=walk_grab,a=10.;const double ta=(5-v0)/a;return walk_grab+(u<ta?v0*u+.5*a*u*u:v0*ta+.5*a*ta*ta+5*(u-ta));};
        const auto hand_at=[&](double t) {
            const double run=t<1?0.:std::min(1.,(t-1)*2),w=2*3.14159265358979*1.3*t,ang=.35*run*std::sin(w);
            WorldTrackedPose h{};h.position={player_x(t)+.10*run*std::sin(w+.5),.12*run*std::sin(w),1.2+.04*run*std::sin(2*3.14159265358979*2.6*t)};
            h.basis={Vec3{std::cos(ang),0,-std::sin(ang)},Vec3{0,1,0},Vec3{std::sin(ang),0,std::cos(ang)}};return h;};
        const auto compose=[](const WorldTrackedPose& h,const WorldTrackedPose& g) {WorldTrackedPose o{};o.position=h.position+grab_world(h,g.position);for(unsigned i=0;i<3;++i) o.basis[i]=grab_world(h,g.basis[i]);return o;};
        PhysicalGrab grab;const auto h0=hand_at(0);grab.sample(h0,0.f,true,1,1);grab.sample(h0,1.f,true,1,12);
        WorldTrackedPose body=compose(h0,hold);Vec3 v{},w{};
        if(!grab.bind(body,h0.position,.3)) return "running: the grab did not bind";
        wd::HeldBind bind;double old_max=0,old_sum=0,run_max=0;unsigned frames=0,engine=0,run_frames=0;double finger_old=0;WorldTrackedPose engine_frame=body;
        double next_physics=0;
        for(double t=1./90;t<4;t+=1./90) {
            while(next_physics<=t) {   // the game's physics ticks up to this render (60 Hz): the motor, its velocity applied over the tick
                const double ms=next_physics*1000;const auto hand=hand_at(next_physics);grab.sample(hand,1.f,true,1,static_cast<std::uint64_t>(ms)+20);
                const double vx=next_physics<1?walk_grab:(player_x(next_physics+.001)-player_x(next_physics))/.001;
                engine_frame=body;   // the frame the renders before this tick saw
                if(const auto m=grab.motor(body,true,static_cast<std::uint64_t>(ms)+20,static_cast<std::uint64_t>(ms)+20,Vec3{vx,0,0}); m && !m->release) {v=m->linear;w=m->angular;}
                body.position=body.position+v*(1./60);const double a=length(w)/60;
                if(a>1e-12) {const Vec3 k=w*(1/length(w));for(auto& b:body.basis) b=b*std::cos(a)+cross(k,b)*std::sin(a)+k*(dot(k,b)*(1-std::cos(a)));}
                if(next_physics<=.6 && next_physics+1./60>.6) {   // v1073's finger shapes: the body read 0.6 s in, in the hand frame
                    const auto hh=hand_at(next_physics);finger_old=length(grab_local(hh,body.position-hh.position)-hold.position);}
                next_physics+=1./60;
            }
            const auto hand=hand_at(t),want=compose(hand,hold);
            const double speed=t<1?walk_grab:(player_x(t+.001)-player_x(t))/.001;
            const auto hp=hand_at(t-1./90);const double track=length((hand.position-hp.position)*90.-Vec3{speed,0,0});
            wd::Mat placed{};const bool on=bind.frame(0,1,wd::pose_mat(hand),wd::pose_mat(engine_frame),t,placed,track,speed);
            const double off=on?wd::distance(placed,wd::pose_mat(want)):length(engine_frame.position-want.position);
            old_max=std::max(old_max,off);old_sum+=off;++frames;engine+=!on;
            if(t>=1.5) {run_max=std::max(run_max,off);++run_frames;}
        }
        char b[420];std::snprintf(b,sizeof b,"RUNNING (%s, held still 1 s, then 5 m/s with a swinging hand for 3 s): drawn thing off hand * hold: v1073 (HeldBind on the physics body) max %.0f mm, mean %.0f mm, max while running %.0f mm, %u%% of frames at the lagging body; v1074 exact + v1082: 0 mm (drawn at hand * hold). Finger shapes off the drawn thing: v1073 %.0f mm (read from the body 0.6 s in), v1082 0 mm (read at the hold).",
            walk_grab>0?"taken while walking at 2 m/s":"taken standing",old_max*1000,old_sum/std::max(1u,frames)*1000,run_max*1000,frames?engine*100/frames:0,finger_old*1000);
        out+=(out.empty()?"":"\n")+std::string(b);
        // v1084 (user: "the same path as bottles and glasses: I grab it, it sticks to my hand"): the held BODY itself, under the motor the game
        // runs for a prop (PhysicalGrab::corpse_motor through NativeGrab::capped_tick, the shipped prop tuning), weighted (v1083: force-capped)
        // against stick (v1084): its distance from hand * hold while running (from 1.5 s), for a 2 kg and an 8 kg prop.
        const auto physics=[&](bool stick,double kg,double& mx,double& mean) {
            PhysicalGrab g2;g2.sample(h0,0.f,true,1,1);g2.sample(h0,1.f,true,1,12);WorldTrackedPose bd=compose(h0,hold);Vec3 lv{},av{};
            mx=mean=0;unsigned n=0;if(!g2.bind(bd,h0.position,.3)) return;
            const GrabTuning tuning=GrabTuning{}.prop_view();
            for(double tp=0;tp<4;tp+=1./60) {
                const auto ms=static_cast<std::uint64_t>(tp*1000)+20;g2.sample(hand_at(tp),1.f,true,1,ms);
                CorpseMotorInput in;in.body_velocity=lv;in.body_spin=av;in.velocity_known=true;in.corpse=false;in.object_kg=kg;in.inertia_kgm2=bone_inertia(kg,.1);
                in.dt=1./60;in.tuning=&tuning;in.stick=stick;
                if(const auto m=g2.corpse_motor(bd,true,ms,ms,in); m && m->write && !m->motor.release) {lv=m->motor.linear;av=m->motor.angular;}
                bd.position=bd.position+lv*(1./60);const double a=length(av)/60;
                if(a>1e-12) {const Vec3 k=av*(1/length(av));for(auto& e:bd.basis) e=e*std::cos(a)+cross(k,e)*std::sin(a)+k*(dot(k,e)*(1-std::cos(a)));}
                if(tp>=1.5) {const double off=length(bd.position-compose(hand_at(tp+1./60),hold).position);mx=std::max(mx,off);mean+=off;++n;}
            }
            mean/=std::max(1u,n);
        };
        // a quick wrist turn (12 rad/s about the forearm for 0.4 s, then back), standing: the largest turn of the held body away from hand * hold
        const auto flick=[&](bool stick,double kg) {
            const auto wrist=[&](double t) {WorldTrackedPose h=h0;const double a=t<1?0.:t<1.4?12*(t-1):t<1.8?4.8-12*(t-1.4):0.;
                h.basis={h0.basis[0],h0.basis[1]*std::cos(a)+h0.basis[2]*std::sin(a),h0.basis[2]*std::cos(a)-h0.basis[1]*std::sin(a)};return h;};
            PhysicalGrab g2;g2.sample(h0,0.f,true,1,1);g2.sample(h0,1.f,true,1,12);WorldTrackedPose bd=compose(h0,hold);Vec3 lv{},av{};double worst=0;
            if(!g2.bind(bd,h0.position,.3)) return 0.;const GrabTuning tuning=GrabTuning{}.prop_view();
            for(double tp=0;tp<2.2;tp+=1./60) {
                const auto ms=static_cast<std::uint64_t>(tp*1000)+20;g2.sample(wrist(tp),1.f,true,1,ms);
                CorpseMotorInput in;in.body_velocity=lv;in.body_spin=av;in.velocity_known=true;in.corpse=false;in.object_kg=kg;in.inertia_kgm2=bone_inertia(kg,.1);in.dt=1./60;in.tuning=&tuning;in.stick=stick;
                if(const auto m=g2.corpse_motor(bd,true,ms,ms,in); m && m->write && !m->motor.release) {lv=m->motor.linear;av=m->motor.angular;}
                bd.position=bd.position+lv*(1./60);const double a=length(av)/60;
                if(a>1e-12) {const Vec3 k=av*(1/length(av));for(auto& e:bd.basis) e=e*std::cos(a)+cross(k,e)*std::sin(a)+k*(dot(k,e)*(1-std::cos(a)));}
                const auto want=compose(wrist(tp+1./60),hold);worst=std::max(worst,std::acos(std::clamp(dot(unit(bd.basis[1]),unit(want.basis[1])),-1.,1.)));
            }
            return worst*kDeg;};
        if(walk_grab==0) {
            char f[200];std::snprintf(f,sizeof f,"WRIST TURN (12 rad/s for 0.4 s): the held body's largest turn away from hand * hold: weighted (v1083) %.0f deg, stick (v1084) %.0f deg (2 kg).",flick(false,2),flick(true,2));
            out+="\n"+std::string(f);
            double w2x,w2m,w8x,w8m,s2x,s2m,s8x,s8m;physics(false,2,w2x,w2m);physics(false,8,w8x,w8m);physics(true,2,s2x,s2m);physics(true,8,s8x,s8m);
            char p[360];std::snprintf(p,sizeof p,"PHYSICS while running (the held body off hand * hold, 5 m/s, swinging hand): weighted hold (v1083, force cap) 2 kg max %.0f mean %.0f mm, 8 kg max %.0f mean %.0f mm; stick (v1084, as the game holds a bottle) 2 kg max %.0f mean %.0f mm, 8 kg max %.0f mean %.0f mm.",
                w2x*1000,w2m*1000,w8x*1000,w8m*1000,s2x*1000,s2m*1000,s8x*1000,s8m*1000);
            out+="\n"+std::string(p);
        }
    }
    return out;
}
std::string put(const Xf& x,double s) { // 4x4 column-major, rotation columns scaled by s (renderer's transform format)
    const Vec3 c[3]={rotated(x.q,{s,0,0}),rotated(x.q,{0,s,0}),rotated(x.q,{0,0,s})};
    char buf[400];
    std::snprintf(buf,sizeof buf,"[%.6f,%.6f,%.6f,0,%.6f,%.6f,%.6f,0,%.6f,%.6f,%.6f,0,%.6f,%.6f,%.6f,1]",
        c[0].x,c[0].y,c[0].z,c[1].x,c[1].y,c[1].z,c[2].x,c[2].y,c[2].z,x.p.x,x.p.y,x.p.z);
    return buf;
}
std::string list(const std::vector<Xf>& v,const Xf* frame,double s) {
    std::string out="[";
    for(size_t i=0;i<v.size();++i) out+=(i?",":"")+put(frame?bd::mul(bd::inverse(*frame),v[i]):v[i],s);
    return out+"]";
}
std::string quote(const std::string& s) {
    std::string o="\"";
    for(char c:s) {if(c=='"'||c=='\\') o+='\\';o+=c;}
    return o+"\"";
}
double component(Vec3 v,unsigned i) {return i==0?v.x:i==1?v.y:v.z;}
double deg(double rad) {return rad*kDeg;}
// The grip axes (WorldTrackedPose basis) that put the hand's fingers along `fingers` and its palm normal along `normal`.
std::array<Vec3,3> grip_axes(const r::HandGeometry& g,Vec3 fingers,Vec3 normal) {
    const Vec3 n_g=g.palm_in;
    const Vec3 f_g=unit(g.finger[2].pivot-n_g*dot(g.finger[2].pivot,n_g)); // middle knuckle: ahead of the palm centre
    const std::array<Vec3,3> gb{f_g,n_g,cross(f_g,n_g)};
    const Vec3 n_w=unit(normal),f_w=unit(fingers-n_w*dot(fingers,n_w));
    const std::array<Vec3,3> wb{f_w,n_w,cross(f_w,n_w)};
    std::array<Vec3,3> axes{};
    for(unsigned i=0;i<3;++i) axes[i]=wb[0]*component(gb[0],i)+wb[1]*component(gb[1],i)+wb[2]*component(gb[2],i);
    return axes;
}
WorldTrackedPose as_pose(const r::Transform& t) {return {r::position(t),{r::column(t,0),r::column(t,1),r::column(t,2)}};}

struct Scene {
    const char* name;const char* title;
    Vec3 where,fingers,normal;          // palm target relative to the eye; finger direction and palm-in direction (world)
    float squeeze,index;bool held;      // controller grip / trigger and dxgi_probe's "holding" flag
    std::function<std::vector<r::HeldShape>(const WorldTrackedPose&,const r::HandGeometry&)> shapes;
    std::string shape_name;
    float reach{-1};                    // finger_contact's reach; < 0: held_curl when held, else 1
    bool snapshot{};                    // the hand lies as the snapshot's palm did
    std::vector<std::string> notes;     // extra report lines (snapshot: what the game read)
    std::shared_ptr<std::string> late_note; // a line only the shapes callback knows (the touch --refit measured again), printed with the report
    float thumb{-1};                    // >= 0: the thumb's want (a touched thumb sensor asks for 1, finger_touch_curl.hpp), else finger_wants'
    // The sphere-grip comparison (finger_sphere.hpp): the shapes the solver is given for a held thing whose pose is jittered (position m, axis-angle rad) the way a
    // lagging motor moves it (old: the thing's own shape, classify_held; sphere: the ball in the palm, which does not care). `ghost`: drawn only, never a contact.
    std::function<std::vector<r::HeldShape>(const WorldTrackedPose&,const r::HandGeometry&,Vec3,Vec3)> moving;
    std::function<std::vector<r::HeldShape>(const WorldTrackedPose&,const r::HandGeometry&)> ghost;
};

std::string shape_json(const r::HeldShape& s,const std::string& name,const char* colour) {
    std::string o="{\"name\":"+quote(name)+",\"color\":"+colour+",\"center\":"+vec(s.frame.position);
    const auto world=[&](Vec3 q) {return s.frame.position+s.frame.basis[0]*q.x+s.frame.basis[1]*q.y+s.frame.basis[2]*q.z;};
    if(s.hull && !s.hull->solid)
        o+=",\"kind\":\"capsule\",\"axis\":"+vec(s.frame.basis[0])+",\"half\":"+num(std::abs(s.hull->b.x-s.hull->a.x)*.5)+",\"radius\":"+num(s.hull->radius);
    else if(s.hull) { // a real-shape piece: its hull's corners pushed out by the convex radius along their faces; the first piece also carries the render mesh
        o+=",\"kind\":\"hull\",\"points\":[";bool first=true;
        for(const auto& v:s.hull->vertices) {bool any=false;
            for(const auto& p:s.hull->planes) if(std::abs(p[0]*v.x+p[1]*v.y+p[2]*v.z-p[3])<1e-4) {o+=(first?"":",")+vec(world(v+Vec3{p[0],p[1],p[2]}*s.hull->radius));first=false;any=true;}
            if(!any) {o+=(first?"":",")+vec(world(v));first=false;}}
        o+="]";
        if(const auto it=real_of.find(s.hull);it!=real_of.end() && !it->second->mesh.empty()) {
            const auto& m=*it->second;
            o+=",\"mesh\":{\"points\":[";for(std::size_t i=0;i<m.mesh.size();++i) o+=(i?",":"")+vec(world(s.hull->to_own(m.mesh[i])));
            o+="],\"triangles\":[";for(std::size_t i=0;i<m.tris.size();++i) {const auto& t=m.tris[i];o+=std::string(i?",":"")+"["+std::to_string(t[0])+","+std::to_string(t[1])+","+std::to_string(t[2])+"]";}
            o+="]}";
        }
    }
    else if(s.rim>=0 && s.half.x>0)
        o+=",\"kind\":\"cylinder\",\"axis\":"+vec(s.frame.basis[0])+",\"half\":"+num(s.half.x)+",\"radius\":"+num(s.radius);
    else if(s.radius>0 && s.half.y==0 && s.half.z==0)
        o+=",\"kind\":\"capsule\",\"axis\":"+vec(s.frame.basis[0])+",\"half\":"+num(s.half.x)+",\"radius\":"+num(s.radius)+(s.radius_end>=0?",\"radius_end\":"+num(s.radius_end):std::string());
    else // a box, or a rounded box drawn as the box that reaches as far (a snapshot of a prop may be one)
        o+=",\"kind\":\"box\",\"axes\":["+vec(s.frame.basis[0])+","+vec(s.frame.basis[1])+","+vec(s.frame.basis[2])+"],\"half\":"+vec(s.half+Vec3{s.radius,s.radius,s.radius});
    return o+"}";
}

// The one scene of a snapshot record. Its shapes are kept in the palm's own frame, so whatever palm the lab's arm settles on carries them
// exactly as the game's palm carried them; the hand's orientation is the game's (fingers and palm normal in the palm frame).
r::FingerSnapshot loaded_snapshot;
// What --refit does besides rebuilding the cylinder: --along F (the grip point as a fraction of the shaft: 0.3 body, 0.85 neck of a bottle) and
// --profile r0,..,r7 (the cross-section radius at the eight stations along the shaft, base to tip, instead of the record's measured one).
bool snapshot_sphere=false; // --sphere: a snapshot's "grab" shape is replaced by the sphere grip's ball (finger_sphere.hpp), sized from that shape's own half sides
bool grip_fit=true,no_seat=false;double fixed_shift=-1,fixed_slide=-1,extra_half=0; // see the flags in run()
// The seat a hold gets: --shift / --slide, else the game's grip fit (finger_ik.hpp fit_grip), else the constants. `note`: what the fit found.
struct Seating {double shift{-1},slide{0};};
Seating seat_of(const r::HandGeometry& g,const WorldTrackedPose& palm,const r::HeldCylinder& c,Vec3 in,Vec3 centre,double along,double roll,double lift,unsigned mode,std::string* note=nullptr) {
    Seating s{};
    if(fixed_shift>=0 || fixed_slide>=0) {s.shift=fixed_shift;s.slide=std::max(0.,fixed_slide);return s;}
    if(!grip_fit) return s;
    const auto fit=r::fit_grip(g,palm,c,in,centre,along,roll,lift,mode);
    s.shift=fit.shift;s.slide=fit.slide;
    if(note) {char b[260];std::snprintf(b,sizeof b,"fit: knuckle shift %.0f mm (the constant: %.0f mm), slide %.0f mm along the hole axis; the fingers float %.1f mm summed on it, %.1f mm on the constant's seat; %u seats tried",fit.shift*1000,std::min(r::held_knuckle_shift,.8*c.radius)*1000,fit.slide*1000,fit.fitted*1000,fit.plain*1000,fit.tried);*note=b;}
    return s;
}
// --lag FILE:ENTITY: the recorded motor lag of one physically held prop (artifacts/native-physical-grab.csv, 60 Hz): per frame the object's position minus its goal
// (the motor's error, m) and its spin / 12 (the motor commands 12 x the rotation error), a 90-frame window of it with the least spread, detrended: the object's own
// jitter about its place in the hand, which the stability metric replays for the old path and the sphere.
std::vector<std::pair<Vec3,Vec3>> recorded_lag;std::string recorded_lag_note;
void load_lag(const std::string& spec) {
    const auto colon=spec.rfind(':');if(colon==std::string::npos) throw std::invalid_argument("--lag FILE:ENTITY");
    std::ifstream file(spec.substr(0,colon));if(!file) throw std::invalid_argument("Cannot read lag file");
    const std::string entity=spec.substr(colon+1);std::string line;std::getline(file,line);
    std::vector<std::pair<Vec3,Vec3>> all;
    while(std::getline(file,line)) {
        std::vector<std::string> c;std::stringstream ls(line);std::string item;while(std::getline(ls,item,',')) c.push_back(item);
        if(c.size()<20 || c[2]!=entity) continue;
        try {all.push_back({{std::stod(c[8])-std::stod(c[11]),std::stod(c[9])-std::stod(c[12]),std::stod(c[10])-std::stod(c[13])},{std::stod(c[17])/12,std::stod(c[18])/12,std::stod(c[19])/12}});} catch(...) {}
    }
    if(all.size()<90) throw std::invalid_argument("fewer than 90 frames of that entity");
    double best=1e9;size_t at=0;
    for(size_t i=0;i+90<=all.size();i+=5) {Vec3 m{};for(size_t k=0;k<90;++k) m=m+all[i+k].first*(1./90);double sd=0;for(size_t k=0;k<90;++k) sd+=length(all[i+k].first-m);if(sd<best) {best=sd;at=i;}}
    Vec3 m{};for(size_t k=0;k<90;++k) m=m+all[at+k].first*(1./90);
    double step=0,spin=0;
    for(size_t k=0;k<90;++k) {recorded_lag.push_back({all[at+k].first-m,all[at+k].second});if(k) step+=length(all[at+k].first-all[at+k-1].first);spin+=length(all[at+k].second);}
    char b[200];std::snprintf(b,sizeof b,"recorded lag of entity %s, frames %zu..%zu: mean position step %.1f mm a frame, mean rotation error %.1f deg",entity.c_str(),at,at+89,step/89*1000,spin/90*kDeg);recorded_lag_note=b;
}
struct Refit {bool on{};double along{-1};std::vector<double> radii;bool lift{};double lift_mm{-1};}; // lift_mm >= 0 (--lift-mm N): a fixed stand-off; lift: stand the shaft off the palm by held_lift (the thumb's root clear of it), an experiment: the game seats with none
// --palette FILE[:index]: the arm palette the game skinned the held hand with (artifacts/arm-palettes.jsonl, dxgi_probe.cpp dump_held_palette), drawn in
// place of the solver's pose: what the game really drew, next to what the solver planned (the report and the shapes stay the replay's).
std::optional<std::array<std::array<float,16>,69>> game_palette;std::string game_palette_note;
void load_palette(const std::string& spec) {
    std::string path=spec;int index=-1;
    const auto colon=path.rfind(':');
    if(colon!=std::string::npos && colon>1 && colon+1<path.size() && path.find_first_not_of("-0123456789",colon+1)==std::string::npos) {index=std::atoi(path.c_str()+colon+1);path.resize(colon);}
    std::ifstream file(path,std::ios::binary);if(!file) throw std::invalid_argument("Cannot read palette file "+path);
    std::stringstream text;text<<file.rdbuf();
    const auto record=r::parse_json(r::snapshot_line(text.str(),index));
    const auto* joints=record.get("joints");
    if(!joints || joints->type!=r::Json::array || joints->a.size()!=69) throw std::invalid_argument("A palette record needs 69 joints");
    std::array<std::array<float,16>,69> out{};
    for(unsigned j=0;j<69;++j) {
        if(joints->a[j].type!=r::Json::array || joints->a[j].a.size()!=16) throw std::invalid_argument("A palette joint needs 16 numbers");
        for(unsigned k=0;k<16;++k) out[j][k]=static_cast<float>(joints->a[j].a[k].n);
    }
    game_palette=out;
    game_palette_note="palette: the game's submitted hand, tick "+std::to_string(static_cast<unsigned long long>(record.get("tick")?record.get("tick")->n:0))+" (drawn in place of the solver's pose)";
}
// The palette's joints as the renderer takes them (relative to the arm frame): the game's hand carried rigidly so that its palm lies where the replay's
// palm does (the camera looks at that), the forearm following.
std::string palette_list(const Xf& arm_frame,const Xf& lab_palm,unsigned wrist,const std::array<unsigned,4>& knuckles) {
    std::array<r::Transform,69> pal{};
    for(unsigned j=0;j<69;++j) pal[j].f=(*game_palette)[j];
    std::array<r::Transform,4> k{};for(unsigned i=0;i<4;++i) k[i]=pal[knuckles[i]];
    const Xf palette_palm=bd::mul(arm_frame,fullbody::to_xf(r::hand_bind_grip(pal[wrist],k)));
    const Xf carry=bd::mul(lab_palm,bd::inverse(palette_palm));
    std::string out="[";
    for(unsigned j=0;j<69;++j) {
        const Xf placed=bd::mul(bd::inverse(arm_frame),bd::mul(carry,bd::mul(arm_frame,fullbody::to_xf(pal[j]))));
        out+=(j?",":"")+put(placed,1);
    }
    return out+"]";
}
std::vector<Scene> snapshot_scenes(const std::string& spec,const Refit& refit) {
    std::string path=spec;int index=-1;
    const auto colon=path.rfind(':');  // FILE[:index]; a drive colon (C:\x) is not an index separator
    if(colon!=std::string::npos && colon>1 && colon+1<path.size() && path.find_first_not_of("-0123456789",colon+1)==std::string::npos) {index=std::atoi(path.c_str()+colon+1);path.resize(colon);}
    std::ifstream file(path,std::ios::binary);if(!file) throw std::invalid_argument("Cannot read snapshot file "+path);
    std::stringstream text;text<<file.rdbuf();
    loaded_snapshot=r::parse_snapshot(r::snapshot_line(text.str(),index));
    const auto& sn=loaded_snapshot;
    auto local=std::make_shared<std::vector<r::HeldShape>>();
    const auto to_local=[&](Vec3 v) {return Vec3{dot(v,sn.palm.basis[0]),dot(v,sn.palm.basis[1]),dot(v,sn.palm.basis[2])};};
    for(const auto& x:sn.shapes) {
        r::HeldShape h{};h.frame.position=to_local(x.position-sn.palm.position);
        for(unsigned i=0;i<3;++i) h.frame.basis[i]=to_local(x.basis[i]);
        h.half=x.half;h.radius=x.radius;h.radius_end=x.radius_end;h.rim=x.rim;local->push_back(h);
    }
    Scene scene{};
    scene.name="snapshot";scene.snapshot=true;scene.squeeze=sn.squeeze;scene.index=sn.index;scene.held=sn.holding;scene.reach=sn.reach;
    scene.where={.36,-.20,-.38};
    char title[240];std::snprintf(title,sizeof title,"Snapshot tick %llu, %s hand, %s (%s), squeeze %.2f trigger %.2f, %u shape(s)",static_cast<unsigned long long>(sn.tick),sn.side?"right":"left",
        sn.reason.c_str(),sn.source.c_str(),sn.squeeze,sn.index,static_cast<unsigned>(sn.shapes.size()));
    static std::string stored_title;stored_title=title;scene.title=stored_title.c_str();
    scene.shapes=[local](const WorldTrackedPose& palm,const r::HandGeometry&) {
        std::vector<r::HeldShape> out;
        const auto world=[&](Vec3 l) {return palm.basis[0]*l.x+palm.basis[1]*l.y+palm.basis[2]*l.z;};
        for(const auto& l:*local) {
            r::HeldShape h{};h.frame.position=palm.position+world(l.frame.position);
            for(unsigned i=0;i<3;++i) h.frame.basis[i]=world(l.frame.basis[i]);
            h.half=l.half;h.radius=l.radius;h.radius_end=l.radius_end;h.rim=l.rim;out.push_back(h);
        }
        return out;
    };
    if(snapshot_sphere) { // the old path's recorded shape -> the ball (the grab shape's local half sides: a capsule half+r, r, r; a box its half)
        for(auto& x:sn.shapes) if(x.kind=="grab") {
            const double rad=r::sphere_grip_radius(x.radius>0 && x.half.y==0 && x.half.z==0?Vec3{x.half.x+x.radius,x.radius,x.radius}:Vec3{x.half.x+x.radius,x.half.y+x.radius,x.half.z+x.radius});
            auto inner=scene.shapes;
            scene.shapes=[inner,rad](const WorldTrackedPose& palm,const r::HandGeometry& g) {
                auto all=inner(palm,g);std::vector<r::HeldShape> out;
                for(auto& h:all) if(!(h.rim<0 && h.radius_end<0 && h.half.y==0 && h.half.z==0 && h.radius>0) && !(h.radius==0)) out.push_back(h); // the other shapes (probes) stay
                out.push_back(r::sphere_grip_shape(palm,g,rad));return out;};
            scene.notes.push_back("sphere grip: the grab shape is replaced by a ball of radius "+std::to_string(rad*100).substr(0,4)+" cm");break;
        }
    }
    for(const auto& x:sn.shapes) {scene.shape_name+=(scene.shape_name.empty()?"":" + ")+x.kind;}
    if(refit.on && sn.held.present && sn.held.have_now) {
        // --refit: the held shape is rebuilt from the record's raw bounds and axes by the CURRENT held_cylinder, profile and seat (the way the
        // game would seat it now), in the lab's own palm, instead of the shape the game had then.
        const auto& hd=sn.held;
        std::array<Vec3,3> rows;for(unsigned i=0;i<3;++i) rows[i]=unit(Vec3{hd.axes[i*3],hd.axes[i*3+1],hd.axes[i*3+2]});
        const NativeGrabBounds bounds{{hd.box[0],hd.box[1],hd.box[2]},{hd.box[4],hd.box[5],hd.box[6]}};
        double along=refit.along>=0?refit.along:hd.touch_along,roll=hd.touch_roll;
        const auto radii=refit.radii;const auto recorded=hd.profile;
        const auto latch=std::make_shared<std::optional<Vec3>>(); // (the game's held_extents: solved once per hold)
        const auto cylinder_on=[radii,recorded,latch](const std::array<Vec3,3>& axes_in,const NativeGrabBounds& bounds_in) {
            auto cyl=r::held_cylinder(axes_in,bounds_in,latch.get());
            cyl.profile=recorded;
            if(radii.size()==r::profile_stations) {
                cyl.profile={};cyl.profile.half=cyl.half;cyl.profile.axis=cyl.axis;
                for(unsigned i=0;i<r::profile_stations;++i) cyl.profile.plus[i]=cyl.profile.minus[i]=static_cast<float>(radii[i]);
            }
            return cyl;
        };
        const auto cylinder_of=[cylinder_on,rows,bounds]() {return cylinder_on(rows,bounds);};
        // Without --along, a record that carries the prop as it was targeted before the pickup gets its touch measured again on that (the record's
        // palm is where the hand was at the press when it is the first record of the hold): where the hand pointed decides the grip.
        const bool repeat_touch=refit.along<0 && hd.have_pre;
        std::array<Vec3,3> pre_rows{};NativeGrabBounds pre_bounds{};Vec3 pre_centre{};
        if(repeat_touch) {
            for(unsigned i=0;i<3;++i) pre_rows[i]=unit(Vec3{hd.pre_axes[i*3],hd.pre_axes[i*3+1],hd.pre_axes[i*3+2]});
            pre_bounds={{hd.pre_box[0],hd.pre_box[1],hd.pre_box[2]},{hd.pre_box[4],hd.pre_box[5],hd.pre_box[6]}};
            pre_centre=(pre_bounds.minimum+pre_bounds.maximum)*.5;
        }
        if(hd.have_pre && !repeat_touch) {
            for(unsigned i=0;i<3;++i) pre_rows[i]=unit(Vec3{hd.pre_axes[i*3],hd.pre_axes[i*3+1],hd.pre_axes[i*3+2]});
            pre_bounds={{hd.pre_box[0],hd.pre_box[1],hd.pre_box[2]},{hd.pre_box[4],hd.pre_box[5],hd.pre_box[6]}};
        }
        if(hd.have_pre) (void)cylinder_on(pre_rows,pre_bounds); // the latch, as the game took it
        // (the record's palm is the drawn palm; a record that carries the controller's grip at the press measures the touch there)
        const WorldTrackedPose snapshot_palm=hd.have_press?WorldTrackedPose{hd.press_position,hd.press_basis}:sn.palm;
        scene.late_note=std::make_shared<std::string>();const auto late=scene.late_note;
        const bool lift_on=refit.lift || refit.lift_mm>=0;const double lift_fixed=refit.lift_mm>=0?refit.lift_mm/1000:-1.;
        const unsigned recorded_mode=hd.touch_mode;
        scene.shapes=[cylinder_of,cylinder_on,along,roll,repeat_touch,pre_rows,pre_bounds,pre_centre,snapshot_palm,late,lift_on,lift_fixed,recorded_mode](const WorldTrackedPose& palm,const r::HandGeometry& g) {
            double grip=along,spin=roll;unsigned mode=recorded_mode;
            if(repeat_touch) {
                const auto touch=r::held_touch(cylinder_on(pre_rows,pre_bounds),pre_rows,pre_centre,snapshot_palm,r::palm_normal(snapshot_palm,g),r::held_aim_reach);
                grip=touch.along;spin=touch.roll;mode=touch.mode;
                char note[260];std::snprintf(note,sizeof note,"refit touch: measured=%d along %.2f roll %.0f deg gap %.1f cm; seat %u (0 wall, 1 wall reversed, 2/3 end-on), turns the prop %.1f deg from its pre-pickup axes (the pre-pickup prop, the press frame)",
                    touch.touched?1:0,touch.along,touch.roll*kDeg,touch.gap*100,touch.mode,touch.rotation*kDeg);*late=note;
            }
            const auto cyl_now=cylinder_of();const auto in_now=r::palm_normal(palm,g);
            const double stand_off=lift_fixed>=0?lift_fixed:lift_on?r::held_lift(g,palm,cyl_now,in_now,grip,spin):0.;
            if(lift_on) {char note[120];std::snprintf(note,sizeof note,"refit lift: the shaft stands %.1f mm off the palm skin (held_lift, thumb root clear)",stand_off*1000);if(late->empty()) *late=note;else if(late->find("lift")==std::string::npos) *late+="\n"+std::string(note);}
            std::string fit_note;
            auto shaft=cyl_now;shaft.half+=extra_half;
            const auto seating=seat_of(g,palm,shaft,in_now,{},grip,spin,stand_off,mode,&fit_note);
            if(!fit_note.empty() && late->find("fit:")==std::string::npos) *late+=(late->empty()?"":"   |   ")+fit_note; // (one line: a newline in a report line breaks the JSON)
            const auto seat=r::seat_held(shaft,palm,in_now,{},grip,spin,stand_off,seating.shift,seating.slide,mode);
            return std::vector<r::HeldShape>(seat.shapes.begin(),seat.shapes.begin()+seat.count);
        };
        const auto cyl=cylinder_of();
        char note[300];std::snprintf(note,sizeof note,"refit: held_cylinder now gives axis %u half %.1f cm radius %.1f cm (the game had half %.1f radius %.1f); %s; grip at %s",cyl.axis,cyl.half*100,cyl.radius*100,
            hd.cylinder_half*100,hd.cylinder_radius*100,r::held_profiled(cyl)?"profiled (the fingers close on the local cross-section)":"no profile (one cylinder)",
            repeat_touch?(hd.have_press?"the touch measured again on the pre-pickup prop, at the controller's press":"the touch measured again on the pre-pickup prop"):(std::to_string(along).substr(0,4)+" of the shaft").c_str());
        scene.notes.push_back(note);
        scene.shape_name="bottle (refit)";
    }
    const auto b3=[](const std::array<float,3>& v) {char b[80];std::snprintf(b,sizeof b,"%.3f %.3f %.3f",v[0],v[1],v[2]);return std::string(b);};
    const auto& h=sn.held;char buf[400];
    std::snprintf(buf,sizeof buf,"live: finger solve %.1f us; reach %.2f; %s",sn.cost_us,sn.reach,sn.held.present?"native hold block follows":"no native hold block (physical or none)");
    scene.notes.push_back(buf);
    if(h.present) {
        std::snprintf(buf,sizeof buf,"hold: entity %llu  gate |model origin - published| %.1f mm  ok=%d  cylinder fit frames %llu, gate-failed frames %llu",static_cast<unsigned long long>(h.entity),
            h.gate_distance*1000,h.gate_ok?1:0,static_cast<unsigned long long>(h.cylinder_set_frames),static_cast<unsigned long long>(h.gate_fail_frames));scene.notes.push_back(buf);
        std::snprintf(buf,sizeof buf,"hold: cylinder ok=%d axis %u half %.1f cm radius %.1f cm  error \"%s\"  local box half %s",h.cylinder_ok?1:0,h.cylinder_axis,h.cylinder_half*100,h.cylinder_radius*100,h.cylinder_error.c_str(),
            h.have_extents?(std::to_string(h.extents.x*100).substr(0,4)+" "+std::to_string(h.extents.y*100).substr(0,4)+" "+std::to_string(h.extents.z*100).substr(0,4)+" cm").c_str():"unsolved");scene.notes.push_back(buf);
        std::snprintf(buf,sizeof buf,"hold: touch measured=%d along %.2f roll %.0f deg gap %.1f cm (bounds: %s)  error \"%s\"   seated=%d",h.touched?1:0,h.touch_along,h.touch_roll*kDeg,h.touch_gap*100,h.touch_source.c_str(),h.touch_error.c_str(),h.seated?1:0);scene.notes.push_back(buf);
        if(h.touch_rotation>=0) {std::snprintf(buf,sizeof buf,"hold: the game's seat %u (0 wall, 1 wall reversed, 2/3 end-on) turns the prop %.1f deg from its pre-pickup axes",h.touch_mode,h.touch_rotation*kDeg);scene.notes.push_back(buf);}
        if(h.have_press) {std::snprintf(buf,sizeof buf,"hold: press frame (the controller grip when the squeeze began) %.0f ms old, squeeze %.2f, at %.3f %.3f %.3f",h.press_age,h.press_squeeze,h.press_position.x,h.press_position.y,h.press_position.z);scene.notes.push_back(buf);}
        std::snprintf(buf,sizeof buf,"hold: the probes last saw entity %llu (%.0f ms old) on the left, %llu (%.0f ms old) on the right; the held one is %llu",static_cast<unsigned long long>(h.seen_entity[0]),h.seen_age[0],
            static_cast<unsigned long long>(h.seen_entity[1]),h.seen_age[1],static_cast<unsigned long long>(h.entity));scene.notes.push_back(buf);
        if(!h.held_class.empty() || !h.match.empty()) {
            char big[800];std::snprintf(big,sizeof big,"hold: classes: held %s, seen left \"%s\" right \"%s\"; the touch's prop matched by \"%s\"; seen centres (%.2f %.2f %.2f) (%.2f %.2f %.2f), local half extents (%.3f %.3f %.3f) (%.3f %.3f %.3f)",
                h.held_class.c_str(),h.seen_class[0].c_str(),h.seen_class[1].c_str(),h.match.c_str(),h.seen_centre[0].x,h.seen_centre[0].y,h.seen_centre[0].z,h.seen_centre[1].x,h.seen_centre[1].y,h.seen_centre[1].z,
                h.seen_half[0].x,h.seen_half[0].y,h.seen_half[0].z,h.seen_half[1].x,h.seen_half[1].y,h.seen_half[1].z);scene.notes.push_back(big);
        }
        if(h.have_first) {scene.notes.push_back("hold: first-frame bounds min "+b3({h.first_box[0],h.first_box[1],h.first_box[2]})+"  max "+b3({h.first_box[4],h.first_box[5],h.first_box[6]})+"  origin "+b3(h.first_origin));}
        if(h.have_now) {scene.notes.push_back("hold: now bounds min "+b3({h.box[0],h.box[1],h.box[2]})+"  max "+b3({h.box[4],h.box[5],h.box[6]})+"  origin "+b3(h.origin));
            scene.notes.push_back("hold: now model axes rows "+b3({h.axes[0],h.axes[1],h.axes[2]})+" | "+b3({h.axes[3],h.axes[4],h.axes[5]})+" | "+b3({h.axes[6],h.axes[7],h.axes[8]}));}
    }
    return {scene};
}

int run(int argc,char** argv) {
    if(argc<2) {std::cerr<<"usage: hand_pose_lab <out.json> [corvo|emily] [--snapshot FILE[:index]]\n";return 2;}
    std::string character="corvo",snapshot_spec,palette_spec,write_palette,write_geometry;Refit refit;
    for(int i=2;i<argc;++i) {
        const std::string a=argv[i];
        if(a=="--snapshot" && i+1<argc) snapshot_spec=argv[++i];
        else if(a=="--only" && i+1<argc) only_prefix=argv[++i];
        else if(a.rfind("--snapshot=",0)==0) snapshot_spec=a.substr(11);
        else if(a=="--refit") refit.on=true;
        else if(a=="--sphere") snapshot_sphere=true;
        else if(a=="--no-seat") no_seat=true;
        else if(a=="--no-fit") grip_fit=false;                                // the knuckle shift of v532 and before (held_knuckle_shift), to compare against
        else if(a=="--shift" && i+1<argc) fixed_shift=std::atof(argv[++i])/1000; // a fixed knuckle shift (mm)
        else if(a=="--slide" && i+1<argc) fixed_slide=std::atof(argv[++i])/1000; // a fixed slide toward the thumb (mm)
        else if(a=="--half" && i+1<argc) extra_half=std::atof(argv[++i])/1000;   // lengthen every held capsule's shaft by this much (mm): an experiment
        else if(a=="--lag" && i+1<argc) load_lag(argv[++i]);
        else if(a=="--lift") refit.lift=true;
        else if(a=="--lift-mm" && i+1<argc) refit.lift_mm=std::atof(argv[++i]);
        else if(a=="--palette" && i+1<argc) palette_spec=argv[++i];
        else if(a=="--write-geometry" && i+1<argc) write_geometry=argv[++i]; // the lab's finger geometry as the game writes it (finger-geometry.jsonl)
        else if(a=="--write-palette" && i+1<argc) write_palette=argv[++i]; // the solved right arm in the arm-palettes.jsonl format (round-trips through --palette)
        else if(a=="--along" && i+1<argc) refit.along=std::atof(argv[++i]);
        else if(a=="--profile" && i+1<argc) {std::stringstream list(argv[++i]);std::string item;while(std::getline(list,item,',')) refit.radii.push_back(std::atof(item.c_str()));}
        else character=a;
    }
    const auto body=fullbody::skeleton(fullbody::data::kBody),left=fullbody::skeleton(fullbody::data::kLeftArm),
        right=fullbody::skeleton(fullbody::data::kRightArm);
    const double eye_height=body.bind[body.find("camera_jnt")].p.z; // avatar scale 1: the bind hand is the drawn hand

    const Vec3 hold_at{.36,-.20,-.38},hold_fingers=unit(Vec3{1,0,-.35}),hold_in{0,1,0};
    const auto cylinder_scene=[&](double radius,double length) {
        return [radius,length](const WorldTrackedPose& palm,const r::HandGeometry& g) {
            const NativeGrabBounds box{{-radius,-radius,0},{radius,radius,length}};
            const auto cyl=r::held_cylinder({{{1,0,0},{0,1,0},{0,0,1}}},box);
            const auto in=r::palm_normal(palm,g);
            auto shaft=cyl;shaft.half+=extra_half;
            const auto seating=seat_of(g,palm,shaft,in,{0,0,length/2},r::held_grip_fraction,0,0,0);
            return std::vector<r::HeldShape>{r::seat_held(shaft,palm,in,{0,0,length/2},r::held_grip_fraction,0,0,seating.shift,seating.slide).shape};
        };
    };
    // ---- the sphere grip's cases: six things, each held the old way (the thing's own shape at its place in the hand, jittered when it moves) and on the ball.
    // Half extents are the thing's own (m): a spoon 18 cm, a fork 20 cm, a candlestick 30 cm with a 2.2 cm shaft, a book 22 x 16 x 4 cm, a folder 32 x 22 x 2 cm, a head 18 x 14 x 20 cm.
    struct Thing {const char* name;const char* title;Vec3 half;};
    const std::vector<Thing> things{{"spoon","Spoon 18 cm",{.09,.015,.004}},{"fork","Fork 20 cm",{.10,.012,.003}},{"candle","Candlestick 30 cm, shaft 2.2 cm",{.15,.022,.022}},
        {"book","Book 22 x 16 x 4 cm",{.11,.08,.02}},{"folder","Folder 32 x 22 x 2 cm",{.16,.11,.01}},{"skull","Skull / head 18 x 14 x 20 cm",{.09,.07,.10}},
        {"crate","Crate 40 x 40 x 40 cm",{.2,.2,.2}},{"plank","Plank 100 x 10 x 3 cm",{.5,.05,.015}},{"bottlebox","Bottle box 7 x 7 x 25 cm",{.035,.035,.125}}};
    const auto box_of=[](const WorldTrackedPose& pose,Vec3 half) { // the engine's world AABB of the thing's box turned by pose (e_j = sum_i half_i |axis_i . j|)
        const auto a=[&](unsigned i,unsigned j) {return std::abs(j==0?pose.basis[i].x:j==1?pose.basis[i].y:pose.basis[i].z);};
        const double h[3]={half.x,half.y,half.z};Vec3 e{};
        e.x=h[0]*a(0,0)+h[1]*a(1,0)+h[2]*a(2,0);e.y=h[0]*a(0,1)+h[1]*a(1,1)+h[2]*a(2,1);e.z=h[0]*a(0,2)+h[1]*a(1,2)+h[2]*a(2,2);
        return NativeGrabBounds{pose.position-e,pose.position+e};
    };
    // The old path's thing: its axes the palm's (long side along the thumb axis, across the palm), centre on the palm skin by its smallest half side. Jitter moves and
    // turns it about its centre. Rows of the pose are the model axes (classify_held's body.basis).
    // A round thing (a bottle-sized capsule, a limb) seats like seat_held: its axis a knuckle shift toward the finger bases about the palm skin point, the skin on its surface.
    const auto round_centre=[](const WorldTrackedPose& palm,const r::HandGeometry& g,double radius) {
        const Vec3 n=r::palm_normal(palm,g),toward=unit(palm.basis[2]*-1.);const double shift=std::min(r::held_knuckle_shift,.8*radius),sine=shift/radius,cosine=std::sqrt(1-sine*sine);
        return palm.position+n*r::held_palm_depth+(n*cosine+toward*sine)*radius;};
    // v1062: the seat - the grab moves the thing along the palm normal until the open hand lies on it (finger_ik.hpp held_standoff); --no-seat: as placed.
    const auto seat_out=[](std::vector<r::HeldShape> v,const WorldTrackedPose& palm,const r::HandGeometry& g) {
        if(no_seat) return v;const auto into=[&](Vec3 x) {return Vec3{dot(x,palm.basis[0]),dot(x,palm.basis[1]),dot(x,palm.basis[2])};};
        double dmax=-1e9;for(const auto& s:v) {const r::PalmShape l{into(s.frame.position-palm.position),{into(s.frame.basis[0]),into(s.frame.basis[1]),into(s.frame.basis[2])},s.half,s.radius,s.radius_end,s.rim,s.fingers_only,s.hull};
            dmax=std::max(dmax,r::held_standoff(g,l));}
        for(auto& s:v) {const r::PalmShape l{into(s.frame.position-palm.position),{into(s.frame.basis[0]),into(s.frame.basis[1]),into(s.frame.basis[2])},s.half,s.radius,s.radius_end,s.rim,s.fingers_only,s.hull};
            const double d=dmax;if(std::getenv("LAB_SEAT_DEBUG")) std::fprintf(stderr,"standoff %.1f mm local %.3f %.3f %.3f half %.3f %.3f %.3f\n",d*1000,l.position.x,l.position.y,l.position.z,l.half.x,l.half.y,l.half.z);s.frame.position=s.frame.position+r::palm_normal(palm,g)*d;}
        return v;};
    const auto old_pose=[](const WorldTrackedPose& palm,const r::HandGeometry& g,Vec3 half,Vec3 dp,Vec3 turn) {
        const Vec3 n=r::palm_normal(palm,g);const double lo=std::min({half.x,half.y,half.z});
        WorldTrackedPose pose{palm.position+n*(lo+r::held_palm_depth)+dp,{palm.basis[0],palm.basis[1],palm.basis[2]}};
        const double angle=length(turn);
        if(angle>1e-9) for(auto& row:pose.basis) row=rotated(bd::axis_angle(turn*(1/angle),angle),row);
        return pose;
    };
    // v1066: a native Use hold of a severed part: the bottle seat, pushed out along the palm normal until the hand lies on it (dxgi_probe native_seat_push)
    const auto native_push=[](auto inner) {return [inner](const WorldTrackedPose& palm,const r::HandGeometry& g) {auto v=inner(palm,g);
        const auto into=[&](Vec3 x) {return Vec3{dot(x,palm.basis[0]),dot(x,palm.basis[1]),dot(x,palm.basis[2])};};
        double d=0;for(const auto& sh:v) d=std::max(d,r::held_standoff(g,{into(sh.frame.position-palm.position),{into(sh.frame.basis[0]),into(sh.frame.basis[1]),into(sh.frame.basis[2])},sh.half,sh.radius,sh.radius_end,sh.rim,sh.fingers_only,sh.hull},r::seat_max_push,0.));
        for(auto& sh:v) sh.frame.position=sh.frame.position+r::palm_normal(palm,g)*d;return v;};};
    std::vector<Scene> scenes{
        {"open","Open hand (grip 0, trigger 0)",hold_at,hold_fingers,hold_in,0.f,0.f,false,{},""},
        {"fist","Plain fist, nothing held (grip 1, trigger 1)",hold_at,hold_fingers,hold_in,1.f,1.f,false,{},""},
        {"bottle","Bottle r=3.5 cm, 25 cm (seat_held, grip 1)",hold_at,hold_fingers,hold_in,1.f,0.f,true,cylinder_scene(.035,.25),"bottle (solver capsule)"},
        {"bottle45","Wide bottle r=4.5 cm, 25 cm (seat_held, grip 1)",hold_at,hold_fingers,hold_in,1.f,0.f,true,cylinder_scene(.045,.25),"bottle (solver capsule)"},
        {"shaft40","Shaft r=4.0 cm, 25 cm (seat_held, grip 1)",hold_at,hold_fingers,hold_in,1.f,0.f,true,cylinder_scene(.04,.25),"shaft (solver capsule)"},
        {"handle","Thin handle r=1.7 cm, 20 cm (seat_held, grip 1)",hold_at,hold_fingers,hold_in,1.f,0.f,true,cylinder_scene(.017,.20),"handle (solver capsule)"},
        {"severed_forearm","Severed forearm held with the game's Use (v1066: seated like a bottle), r=4 cm, 26 cm",hold_at,hold_fingers,hold_in,1.f,0.f,true,native_push(cylinder_scene(.04,.26)),"limb (solver capsule)"},
        {"severed_head","Severed head held with the game's Use (v1066: seated like a bottle), ball r=9 cm",hold_at,hold_fingers,hold_in,1.f,0.f,true,native_push(cylinder_scene(.09,.18)),"head (solver capsule)"},
        {"table","Hand resting on a table (palm down, slab 2.5 cm under the palm)",{.40,-.12,-.52},{1,0,0},{0,0,-1},0.f,0.f,false,
         [](const WorldTrackedPose& palm,const r::HandGeometry& g) {
             const auto in=r::palm_normal(palm,g);
             return std::vector<r::HeldShape>{r::slab_shape(palm.position+in*.025,in*-1.)};},"table slab"},
        // A thumb resting on the controller (stick, face buttons, thumbrest): the touch curl asks the thumb for 1 whatever the fingers do.
        {"thumb_open","Thumb on the controller, fingers open (grip 0, trigger 0)",hold_at,hold_fingers,hold_in,0.f,0.f,false,{},"",-1,false,{},{},1.f},
        {"thumb_rest","Thumb on the controller, index resting on the trigger (grip 0, index 0.3)",hold_at,hold_fingers,hold_in,0.f,.3f,false,{},"",-1,false,{},{},1.f},
        {"thumb_half","Thumb on the controller, half grip (grip 0.35, trigger 0)",hold_at,hold_fingers,hold_in,.35f,0.f,false,{},"",-1,false,{},{},1.f},
        {"thumb_grip","Thumb on the controller, grip only (grip 1, index 0.3)",hold_at,hold_fingers,hold_in,1.f,.3f,false,{},"",-1,false,{},{},1.f},
        {"thumb_fist","Thumb on the controller, fist (grip 1, trigger 1)",hold_at,hold_fingers,hold_in,1.f,1.f,false,{},"",-1,false,{},{},1.f},
    };

    for(const auto& t:things) {
        const auto half=t.half;
        Scene sphere{};sphere.name=_strdup((std::string(t.name)+"_sphere").c_str());sphere.held=true;sphere.where=hold_at;sphere.fingers=hold_fingers;sphere.normal=hold_in;sphere.squeeze=1.f;sphere.index=0.f;
        sphere.title=_strdup((std::string(t.title)+": the sphere grip (ball round the whole object, every corner inside)").c_str());sphere.shape_name="grip ball";
        sphere.shapes=[half](const WorldTrackedPose& palm,const r::HandGeometry& g) {return std::vector<r::HeldShape>{r::sphere_grip_shape(palm,g,r::sphere_grip_radius(half))};};
        sphere.moving=[half](const WorldTrackedPose& palm,const r::HandGeometry& g,Vec3,Vec3) {return std::vector<r::HeldShape>{r::sphere_grip_shape(palm,g,r::sphere_grip_radius(half))};};
        sphere.ghost=[half](const WorldTrackedPose& palm,const r::HandGeometry& g) { // the thing, its centre in the ball, pre-pickup axes = the palm's
            const double rad=r::sphere_grip_radius(half);
            r::HeldShape box{};box.frame.position=palm.position+r::palm_normal(palm,g)*(rad+r::held_palm_depth);box.frame.basis=palm.basis;box.half=half;return std::vector<r::HeldShape>{box};};
        scenes.push_back(sphere);
        Scene ball{};ball.name=_strdup((std::string(t.name)+"_ball").c_str());ball.held=true;ball.where=hold_at;ball.fingers=hold_fingers;ball.normal=hold_in;ball.squeeze=1.f;ball.index=0.f;
        ball.title=_strdup((std::string(t.title)+": the grip ball (a small ball at the grip place, the thing's thin side)").c_str());ball.shape_name="grip ball";
        ball.shapes=[half](const WorldTrackedPose& palm,const r::HandGeometry& g) {return std::vector<r::HeldShape>{r::sphere_grip_shape(palm,g,r::sphere_grip_ball_radius(half))};};
        ball.moving=[half](const WorldTrackedPose& palm,const r::HandGeometry& g,Vec3,Vec3) {return std::vector<r::HeldShape>{r::sphere_grip_shape(palm,g,r::sphere_grip_ball_radius(half))};};
        ball.ghost=[half](const WorldTrackedPose& palm,const r::HandGeometry& g) { // the thing as the motor seats it: its own surface on the palm skin
            r::HeldShape box{};box.frame.position=palm.position+r::palm_normal(palm,g)*(std::min({half.x,half.y,half.z})+r::held_palm_depth);box.frame.basis=palm.basis;box.half=half;return std::vector<r::HeldShape>{box};};
        scenes.push_back(ball);
        if(r::grip_is_flat(half)) {   // v1028 flat grip: a wide thin prop lies on the palm, no contact shape, a relaxed reach
            Scene flat{};flat.name=_strdup((std::string(t.name)+"_flat").c_str());flat.held=true;flat.where=hold_at;flat.fingers=hold_fingers;flat.normal=hold_in;flat.squeeze=1.f;flat.index=0.f;
            flat.title=_strdup((std::string(t.title)+": the flat grip (the thing lies on the palm, the fingers rest on its face)").c_str());flat.shape_name="no shape (relaxed reach)";
            flat.shapes=[](const WorldTrackedPose&,const r::HandGeometry&) {return std::vector<r::HeldShape>{};};
            flat.moving=[](const WorldTrackedPose&,const r::HandGeometry&,Vec3,Vec3) {return std::vector<r::HeldShape>{};};
            flat.ghost=[half](const WorldTrackedPose& palm,const r::HandGeometry& g) { // the thing as the motor seats it: its face on the palm skin
                r::HeldShape box{};box.frame.position=palm.position+r::palm_normal(palm,g)*(std::min({half.x,half.y,half.z})+r::held_palm_depth);box.frame.basis=palm.basis;box.half=half;
                return std::vector<r::HeldShape>{box};};
            flat.reach=r::grip_flat_reach;
            scenes.push_back(flat);
        }
        // Surface grip (native hands phase 2b): the thing's OWN box where the motor holds it (the bind relation, fixed in the palm frame):
        // the fingers wrap the real surface under the hand and the shape never moves under them (moving ignores the jitter).
        Scene surface{};surface.name=_strdup((std::string(t.name)+"_surface").c_str());surface.held=true;surface.where=hold_at;surface.fingers=hold_fingers;surface.normal=hold_in;surface.squeeze=1.f;surface.index=0.f;
        surface.title=_strdup((std::string(t.title)+": the surface grip (its own box at the bind relation, fixed in the palm)").c_str());surface.shape_name="object surface (bind relation)";
        // A real hold lays the thing the way a hand takes it: longest side across the palm (bar grip, along the thumb axis), thinnest along the palm normal
        // (the fingers curl over it), the middle along the fingers; old_pose has the palm's axes (x thumb, y normal, z fingers), so reorder the half extents.
        const Vec3 held_half=[&] {std::array<double,3> v{half.x,half.y,half.z};std::sort(v.begin(),v.end());return Vec3{v[2],v[0],v[1]};}();
        surface.moving=[half=held_half,old_pose,box_of,round_centre,seat_out](const WorldTrackedPose& palm,const r::HandGeometry& g,Vec3,Vec3) {
            const auto pose=old_pose(palm,g,half,{},{});auto shape=r::surface_grip_shape(pose,box_of(pose,half),-1,palm);
            if(shape.radius>0) shape.frame.position=round_centre(palm,g,shape.radius);return seat_out({shape},palm,g);};
        surface.shapes=[m=surface.moving](const WorldTrackedPose& palm,const r::HandGeometry& g) {return m(palm,g,Vec3{},Vec3{});};
        surface.ghost=surface.shapes;
        scenes.push_back(surface);
        if(std::string(t.name)=="plank" || std::string(t.name)=="book") { // the palm on the EDGE: the thin side along the fingers (the other seat of the same thing)
            Scene edge=surface;edge.name=_strdup((std::string(t.name)+"_surface_edge").c_str());edge.title=_strdup((std::string(t.title)+": surface grip, palm on the edge (thin side along the fingers)").c_str());
            // the palm against the edge: the width (y, the middle side) stands out along the palm normal, the thin side (z) lies along the fingers, the plank's middle a finger-base ahead of the palm
            edge.moving=[half,old_pose,box_of,seat_out](const WorldTrackedPose& palm,const r::HandGeometry& g,Vec3,Vec3) {
                auto pose=old_pose(palm,g,half,{},{});pose.position=palm.position+r::palm_normal(palm,g)*(half.y+r::held_palm_depth)+palm.basis[2]*-.045;return seat_out({r::surface_grip_shape(pose,box_of(pose,half),-1,palm)},palm,g);};
            edge.shapes=[m=edge.moving](const WorldTrackedPose& palm,const r::HandGeometry& g) {return m(palm,g,Vec3{},Vec3{});};edge.ghost=edge.shapes;
            scenes.push_back(edge);
        }
        Scene old{};old.name=_strdup((std::string(t.name)+"_old").c_str());old.held=true;old.where=hold_at;old.fingers=hold_fingers;old.normal=hold_in;old.squeeze=1.f;old.index=0.f;
        old.title=_strdup((std::string(t.title)+": v638 path (classify_held on the thing's own world box)").c_str());old.shape_name="object (classify_held)";
        old.moving=[half,old_pose,box_of](const WorldTrackedPose& palm,const r::HandGeometry& g,Vec3 dp,Vec3 turn) {
            const auto pose=old_pose(palm,g,half,dp,turn);return std::vector<r::HeldShape>{r::classify_held(pose,box_of(pose,half),-1,palm)};};
        old.shapes=[old_moving=old.moving](const WorldTrackedPose& palm,const r::HandGeometry& g) {return old_moving(palm,g,Vec3{},Vec3{});};
        scenes.push_back(old);
    }
    for(const auto& limb:std::vector<std::pair<int,const char*>>{{7,"Corpse forearm (bone 7, r 5 cm)"},{10,"Corpse forearm (bone 10, r 6 cm)"},{12,"Corpse calf (bone 12, r 6 cm)"},{15,"Corpse calf (bone 15, r 8 cm)"},{4,"Corpse head (bone 4, r 11 cm)"}}) {
        Scene c{};c.name=_strdup(("corpse_bone"+std::to_string(limb.first)).c_str());c.held=true;c.where=hold_at;c.fingers=hold_fingers;c.normal=hold_in;c.squeeze=1.f;c.index=0.f;
        c.title=_strdup((std::string(limb.second)+": classify_held bone path, laid across the palm").c_str());c.shape_name="ragdoll bone capsule";
        const int bone=limb.first;
        // v1063: the bone's padded body box (limb: 13 cm half long across the palm, the table radius round it; head: a cube), its visible limb in the palm
        c.moving=[bone,round_centre,seat_out,box_of](const WorldTrackedPose& palm,const r::HandGeometry& g,Vec3,Vec3) {
            const double rad=d2vr::ragdoll_bone_radius[static_cast<std::size_t>(bone)];const Vec3 at=round_centre(palm,g,rad*r::surface_bone_share);
            const Vec3 half=bone==4?Vec3{rad,rad,rad}:Vec3{.13,rad,rad};const WorldTrackedPose pose{at,palm.basis};
            return seat_out({r::surface_grip_shape(pose,box_of(pose,half),bone,palm)},palm,g);};
        c.shapes=[m=c.moving](const WorldTrackedPose& palm,const r::HandGeometry& g) {return m(palm,g,Vec3{},Vec3{});};c.ghost=c.shapes;
        scenes.push_back(c);
    }
    // v1063: things TAKEN at an awkward angle (tilted, turned off square, held off centre): the grab's natural seat turns them into the hand, then the seat-out.
    struct Taken {const char* name;const char* title;Vec3 half;Vec3 tilt;Vec3 offset;};
    for(const auto& t:std::vector<Taken>{{"taken_book","Book taken tilted 25 deg, 20 deg off square",{.11,.08,.02},{.44,0,.35},{.02,0,.03}},
        {"taken_plank","Plank taken 35 deg off across the palm, near its end",{.5,.05,.015},{0,.61,0},{.3,0,0}},
        {"taken_crate","Crate side taken tilted 30 deg",{.2,.2,.2},{.52,.2,0},{.05,.05,0}},
        {"taken_bottlebox","Bottle box taken 40 deg off across",{.125,.035,.035},{0,.7,.2},{.04,0,0}},
        {"taken_spoon","Spoon taken along the fingers (not swung)",{.09,.015,.004},{0,1.4,0},{0,0,0}},
        {"taken_screwdriver","Screwdriver (20 x 2.4 cm) taken by its handle, 20 deg off across",{.1,.012,.012},{0,.35,0},{.05,0,0}}}) {
        Scene c{};c.name=_strdup(t.name);c.held=true;c.where=hold_at;c.fingers=hold_fingers;c.normal=hold_in;c.squeeze=1.f;c.index=0.f;
        c.title=_strdup((std::string(t.title)+": natural seat + seat-out").c_str());c.shape_name="object surface (natural seat)";
        c.moving=[t,box_of,seat_out](const WorldTrackedPose& palm,const r::HandGeometry& g,Vec3,Vec3) {
            const Vec3 n=r::palm_normal(palm,g),fp=g.finger[2].pivot,f0=palm.basis[0]*fp.x+palm.basis[1]*fp.y+palm.basis[2]*fp.z;Vec3 fw=unit(cross(n,palm.basis[0]));if(dot(fw,f0)<0) fw=fw*-1.; // the hand's forward (the game's grip axis), not the knuckle direction
            WorldTrackedPose pose{palm.position+n*(r::held_palm_depth+std::min({t.half.x,t.half.y,t.half.z}))+palm.basis[0]*t.offset.x+palm.basis[2]*t.offset.z,palm.basis};
            const auto spin=[](Vec3 v,Vec3 k,double a) {return v*std::cos(a)+cross(k,v)*std::sin(a)+k*(dot(k,v)*(1-std::cos(a)));};
            for(unsigned k=0;k<3;++k) {const double a=k==0?t.tilt.x:k==1?t.tilt.y:t.tilt.z;const Vec3 ax=unit(palm.basis[k]);if(a!=0) for(auto& row:pose.basis) row=spin(row,ax,a);}
            const auto seat=r::natural_seat(pose,box_of(pose,t.half),palm.position+n*r::held_palm_depth,fw,n);
            if(seat && std::getenv("LAB_SEAT_DEBUG")) std::fprintf(stderr,"%s fw %.3f %.3f %.3f n %.3f %.3f %.3f in0 %.3f %.3f %.3f out0 %.3f %.3f %.3f\n",t.name,fw.x,fw.y,fw.z,n.x,n.y,n.z,pose.basis[0].x,pose.basis[0].y,pose.basis[0].z,seat->basis[0].x,seat->basis[0].y,seat->basis[0].z);
            auto use=seat?*seat:pose;if(!seat) {std::fprintf(stderr,"%s: natural seat unsolvable, support seat\n",t.name);const auto b=box_of(pose,t.half);use.position=use.position+(palm.position+n*(r::held_palm_depth+r::surface_seat_depth(pose,b,n))-(b.minimum+b.maximum)*.5);}
            return seat_out({r::surface_grip_shape(use,box_of(use,t.half),-1,palm)},palm,g);};
        c.shapes=[m=c.moving](const WorldTrackedPose& palm,const r::HandGeometry& g) {return m(palm,g,Vec3{},Vec3{});};c.ghost=c.shapes;
        scenes.push_back(c);
    }
    // v1080 real shapes: each prop's render-mesh shape (real_props, as the runtime builds it) taken off square, seated by the natural seat (a pinch for a
    // thin bar) and the seat-out, the fingers on its convex pieces; the render mesh drawn under them. Long things are also taken near an end.
    for(const auto& p:real_props()) {
        struct Take {const char* tag;Vec3 tilt;Vec3 offset;};
        const d2vr::PropShape* shape=&p.shape;
        if(std::getenv("LAB_SEAT_DEBUG")) {std::fprintf(stderr,"%s pieces (own half width x thickness, mm):",p.kind.c_str());
            for(const auto& q:shape->pieces) std::fprintf(stderr," %.1fx%.1f",q.half.y*1000,q.half.z*1000);std::fprintf(stderr,"\n");}
        for(const auto& t:std::vector<Take>{{"",{.25,.2,.15},{.02,0,.02}},{"_end",{.1,.3,0},{.07,0,0}}}) {
            if(*t.tag && (shape->pieces.size()<2 || p.kind.rfind("sheet_",0)==0)) continue;   // v1082: one take for a contact-sheet prop
            Scene c{};c.name=_strdup(("real_"+p.kind+t.tag).c_str());c.held=true;c.where=hold_at;c.fingers=hold_fingers;c.normal=hold_in;c.squeeze=1.f;c.index=0.f;
            const auto& w=shape->whole;
            c.title=_strdup((p.kind+(*t.tag?" taken near an end":"")+" ("+p.source+", "+std::to_string(shape->pieces.size())+" piece(s), "+
                num(w.half.x*200).substr(0,4)+" x "+num(w.half.y*200).substr(0,4)+" x "+num(w.half.z*200).substr(0,4)+" cm; Havok hull "+std::to_string(p.hull_vertices)+
                " vertices): natural seat + seat-out on the real shape; grip map: "+d2vr::grip_kind_name(shape->grip.kind)+
                (shape->grip.span?" over "+num((shape->grip.x1-shape->grip.x0)*100).substr(0,4)+" cm of its length":std::string())).c_str());
            c.shape_name="real shape (render mesh pieces)";if(std::getenv("LAB_REAL_REACH")) c.reach=static_cast<float>(std::atof(std::getenv("LAB_REAL_REACH")));
            c.moving=[t,shape,seat_out,name=c.name](const WorldTrackedPose& palm,const r::HandGeometry& g,Vec3,Vec3) {
                const Vec3 n=r::palm_normal(palm,g),fp=g.finger[2].pivot,f0=palm.basis[0]*fp.x+palm.basis[1]*fp.y+palm.basis[2]*fp.z;Vec3 fw=unit(cross(n,palm.basis[0]));if(dot(fw,f0)<0) fw=fw*-1.;
                // as a hand takes it: its long side across the palm, its thinnest toward the palm, then tilted (the pose's rows: the body axes in the world)
                const auto& w=shape->whole;const Vec3 t2=n,t0=unit(palm.basis[0]-n*dot(palm.basis[0],n)),t1=cross(t2,t0);WorldTrackedPose pose{};
                for(unsigned j=0;j<3;++j) {const double c0=j==0?w.axes[0].x:j==1?w.axes[0].y:w.axes[0].z,c1=j==0?w.axes[1].x:j==1?w.axes[1].y:w.axes[1].z,c2=j==0?w.axes[2].x:j==1?w.axes[2].y:w.axes[2].z;
                    pose.basis[j]=t0*c0+t1*c1+t2*c2;}
                const auto spin=[](Vec3 v,Vec3 k,double a) {return v*std::cos(a)+cross(k,v)*std::sin(a)+k*(dot(k,v)*(1-std::cos(a)));};
                for(unsigned k=0;k<3;++k) {const double a=k==0?t.tilt.x:k==1?t.tilt.y:t.tilt.z;const Vec3 ax=unit(palm.basis[k]);if(a!=0) for(auto& row:pose.basis) row=spin(row,ax,a);}
                pose.position=palm.position+n*(r::held_palm_depth+shape->whole.half.z)+palm.basis[0]*t.offset.x+palm.basis[2]*t.offset.z-r::body_direction(pose,shape->whole.centre);
                const double e0=length(shape->whole.half);const NativeGrabBounds b{pose.position-Vec3{e0,e0,e0},pose.position+Vec3{e0,e0,e0}}; // (a real shape ignores the bounds)
                bool pinch=false;const auto seat=r::natural_seat(pose,b,palm.position+n*r::held_palm_depth,fw,n,shape,&pinch);
                auto use=seat?*seat:pose;
                if(!seat) {std::fprintf(stderr,"%s: natural seat unsolvable, support seat\n",name);use.position=use.position+(palm.position+n*(r::held_palm_depth+r::surface_seat_depth(pose,b,n,shape))-r::grip_box(pose,b,shape)->centre);}
                if(std::getenv("LAB_SEAT_DEBUG")) std::fprintf(stderr,"%s: %s seat\n",name,pinch?"pinch":"palm");
                std::vector<r::HeldShape> v(shape->pieces.size());v.resize(r::prop_grip_shapes(use,*shape,v.data(),static_cast<unsigned>(v.size())));
                return seat_out(v,palm,g);};
            c.shapes=[m=c.moving](const WorldTrackedPose& palm,const r::HandGeometry& g) {return m(palm,g,Vec3{},Vec3{});};c.ghost=c.shapes;
            scenes.push_back(c);
        }
    }
    if(!only_prefix.empty()) scenes.erase(std::remove_if(scenes.begin(),scenes.end(),[](const Scene& s) {return std::string(s.name).rfind(only_prefix,0)!=0;}),scenes.end());
    if(!snapshot_spec.empty()) scenes=snapshot_scenes(snapshot_spec,refit);
    if(!palette_spec.empty()) load_palette(palette_spec);
    std::string json="{\"character\":"+quote(character)+",\"frames\":[";
    for(size_t index=0;index<scenes.size();++index) {
        const auto& scene=scenes[index];
        bd::BodySolver solver(body,left,right,{eye_height,0});
        const auto& sk=solver.skeleton(2);
        if(sk.local.size()!=69) throw std::invalid_argument("right arm must have 69 joints");
        // The live geometry: the native bind palette (fingers.open == bind), chains thumb 01-03 / others 02-04, the underwrist/upperwrist palm helpers.
        std::array<r::Transform,69> bind{};r::ArmHierarchy parents{};
        for(unsigned j=0;j<69;++j) {bind[j]=fullbody::to_transform(sk.bind[j]);parents[j]=static_cast<std::int16_t>(sk.parent[j]);}
        r::FingerChains chains{};
        for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s)
            chains[f][s]=static_cast<unsigned>(sk.find(std::string("right_")+kFingers[f]+"_0"+std::to_string(s+(f?2:1))+"_jnt"));
        const int wrist=sk.find("right_wrist_jnt"),attach=sk.find("right_hand_attachment_jnt");
        const char* knuckle_names[4]={"index","medium","ring","pinky"};
        std::array<r::Transform,4> knuckles{};
        for(unsigned i=0;i<4;++i) knuckles[i]=bind[sk.find(std::string("right_")+knuckle_names[i]+"_02_jnt")];
        const auto bind_grip=r::hand_bind_grip(bind[wrist],knuckles);
        const r::HandJoints palm_helpers{static_cast<unsigned>(sk.find("right_helper_underwrist_jnt")),static_cast<unsigned>(sk.find("right_helper_upperwrist_jnt"))};
        const auto geometry=r::hand_geometry(bind,parents,chains,palm_helpers,static_cast<unsigned>(wrist),bind_grip);
        if(!write_geometry.empty() && index==0) {
            std::ofstream out(write_geometry,std::ios::binary|std::ios::trunc);
            out<<r::geometry_json(1,geometry,r::hinge_axes(geometry,bind_grip),bind_grip,{bind[wrist],bind[chains[0][0]],bind[chains[0][1]],bind[chains[0][2]]})<<"\n";
        }
        const Xf grip_to_attachment=bd::mul(bd::inverse(fullbody::to_xf(bind_grip)),sk.bind[attach]);

        bd::Inputs in;
        in.eye={Quaternion{},{0,0,eye_height}};in.hand_valid[0]=false;in.dt=1/90.;
        const auto hinges=r::hinge_axes(geometry,bind_grip); // the axes the contact tests: the solver bends the bones about them
        for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) {in.finger_axis[1][f][s][0]=static_cast<float>(hinges[f][s].x);in.finger_axis[1][f][s][1]=static_cast<float>(hinges[f][s].y);in.finger_axis[1][f][s][2]=static_cast<float>(hinges[f][s].z);}
        in.has_finger_axis[1]=true;
        in.hand_empty[1]=true;in.grip[1]=scene.squeeze;in.trigger[1]=scene.index;
        Vec3 scene_fingers=scene.fingers,scene_normal=scene.normal;
        if(scene.snapshot) { // the hand lies as the game's did: its finger direction and palm normal in the snapshot's palm frame
            const Vec3 n_g=geometry.palm_in,f_g=unit(geometry.finger[2].pivot-n_g*dot(geometry.finger[2].pivot,n_g));
            const auto& b=loaded_snapshot.palm.basis;
            scene_fingers=b[0]*f_g.x+b[1]*f_g.y+b[2]*f_g.z;scene_normal=b[0]*n_g.x+b[1]*n_g.y+b[2]*n_g.z;
        }
        const auto axes=grip_axes(geometry,scene_fingers,scene_normal);
        const Xf grip{bd::from_axes(axes[0],axes[1],axes[2]),in.eye.p+scene.where};
        in.hand[1]=bd::mul(grip,grip_to_attachment);

        const auto palm_of=[&](const bd::Output& out) {
            std::array<r::Transform,4> k{};
            for(unsigned i=0;i<4;++i) k[i]=fullbody::to_transform(out.arm[1][sk.find(std::string("right_")+knuckle_names[i]+"_02_jnt")]);
            return r::hand_bind_grip(fullbody::to_transform(out.arm[1][wrist]),k);
        };
        const auto settle=[&](unsigned steps) {bd::Output out;for(unsigned i=0;i<steps;++i) out=solver.solve(in);return out;};
        // Pose -> palm -> shapes -> contact -> turns -> pose, until the palm stops moving under the fingers (live: the solved palm of the last frame).
        auto out=settle(60);
        std::vector<r::HeldShape> shapes;r::FingerCurls contact{};std::array<std::array<float,3>,5> turn{};WorldTrackedPose palm{};std::array<float,5> wants_used{};
        for(unsigned pass=0;pass<4;++pass) {
            palm=as_pose(palm_of(out));
            shapes=scene.shapes?scene.shapes(palm,geometry):std::vector<r::HeldShape>{};
            r::FingerIkState state{};
            auto wants=r::finger_wants(scene.held,scene.squeeze,scene.index);
            if(scene.thumb>=0) wants[0]=scene.thumb;
            contact=r::finger_contact(geometry,palm,shapes.data(),static_cast<unsigned>(shapes.size()),0,state,scene.reach>=0?scene.reach:scene.held?r::held_curl:1.f,r::finger_solve_all,&wants);
            turn=r::finger_turns(geometry,contact,wants);wants_used=wants;
            in.has_finger[1]=true;
            for(unsigned f=0;f<5;++f) for(unsigned s=0;s<3;++s) in.finger_turn[1][f][s]=turn[f][s];
            out=settle(30);
        }
        palm=as_pose(palm_of(out));
        shapes=scene.shapes?scene.shapes(palm,geometry):std::vector<r::HeldShape>{};

        // ---- report: what each joint really did, against the hinge the solver planned around.
        std::vector<std::string> report;
        const auto local_coords=[&](Vec3 world) {const auto d=world-palm.position;return Vec3{dot(d,palm.basis[0]),dot(d,palm.basis[1]),dot(d,palm.basis[2])};};
        const auto shape_clearance=[&](Vec3 p) {double d=1e9;for(const auto& s:shapes) d=std::min(d,r::shape_distance(s,p));return d;};
        {char title[200];std::snprintf(title,sizeof title,"%s  [solver scale %.3f, shapes %u]",scene.title,solver.scale(),static_cast<unsigned>(shapes.size()));report.push_back(title);}
        if(!write_palette.empty()) {std::ofstream f(write_palette,std::ios::binary|std::ios::trunc);f<<"{\"tick\":0,\"side\":1,\"joints\":"+list(out.arm[1],&out.arm_frame[1],1)+"}\n";}
        for(const auto& note:scene.notes) report.push_back(note);
        if(scene.late_note && !scene.late_note->empty()) report.push_back(*scene.late_note);
        if(game_palette) report.push_back(game_palette_note);
        if(scene.snapshot) { // the replay against the game's own answer (the game smooths toward its target over frames; the lab solves it in one)
            const auto& sn=loaded_snapshot;
            double worst=0;std::string per;
            for(unsigned f=0;f<5;++f) {double d=0;for(unsigned j=0;j<3;++j) d=std::max(d,static_cast<double>(std::abs(contact.pose[f][j]-sn.contact.pose[f][j])));worst=std::max(worst,d);
                char b[40];std::snprintf(b,sizeof b,"%s%s %.1f",f?", ":"",kFingers[f],deg(d));per+=b;}
            char b[600];std::snprintf(b,sizeof b,"replay vs the game's contact pose, largest joint difference (deg): %s  (worst %.1f)   palm basis vs the snapshot's: %.2f deg",per.c_str(),deg(worst),
                deg(std::acos(std::clamp((palm.basis[0].x*sn.palm.basis[0].x+palm.basis[0].y*sn.palm.basis[0].y+palm.basis[0].z*sn.palm.basis[0].z),-1.,1.))));report.push_back(b);
        }
        report.push_back("last column group: mean gap of each phalanx's skin to the shapes (mm, mean of four points along it, 0 = lying on the surface) [prox mid dist]");
        report.push_back("finger  planned turns (deg)  [MCP PIP DIP]  posed hinge (deg)       off-axis (deg)         meta   touch  bone clearance mm [prox mid dist]  plan-vs-pose mm [MCP PIP DIP tip]  contact limit % of full [MCP PIP DIP]");
        report.push_back("        (thumb: CMC MCP IP)    positive = closing toward the palm;  off-axis = posed hinge vs the solver's planar hinge;  meta = metacarpal (grip only)");
        std::string planned_json;double gap_total=0;
        for(unsigned f=0;f<5;++f) {
            const auto& g=geometry.finger[f];
            double posed_angle[3]{},off_axis[3]{};
            for(unsigned s=0;s<3;++s) {
                const int j=static_cast<int>(chains[f][s]);
                const Xf local=bd::mul(bd::inverse(out.arm[1][sk.parent[j]]),out.arm[1][j]);
                auto q=bd::mul(bd::conj(sk.local[j].q),local.q);
                if(q.w<0) q={-q.x,-q.y,-q.z,-q.w};
                const double sine=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z),angle=2*std::atan2(sine,q.w);
                if(sine<1e-9) continue;
                const Vec3 axis_bind=rotated(sk.bind[j].q,Vec3{q.x/sine,q.y/sine,q.z/sine});
                const double along=dot(axis_bind,r::column(bind_grip,0))*g.hinge[s].x+dot(axis_bind,r::column(bind_grip,1))*g.hinge[s].y+dot(axis_bind,r::column(bind_grip,2))*g.hinge[s].z;
                posed_angle[s]=deg(angle)*(along<0?-1:1);off_axis[s]=deg(std::acos(std::min(1.,std::abs(along))));
            }
            double meta=0;
            if(f) { // the metacarpal (joint 01) bends by grip alone (body_ik finger_local), outside the contact model
                const int j=sk.find(std::string("right_")+kFingers[f]+"_01_jnt");
                const Xf local=bd::mul(bd::inverse(out.arm[1][sk.parent[j]]),out.arm[1][j]);
                auto q=bd::mul(bd::conj(sk.local[j].q),local.q);if(q.w<0) q={-q.x,-q.y,-q.z,-q.w};
                meta=deg(2*std::atan2(std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z),q.w));
            }
            // Posed bones against the shapes, and the planned tip against the posed tip (palm frame).
            const int end_joint=sk.find(std::string("right_")+kFingers[f]+"_end_jnt");
            std::array<Vec3,4> joint_world{};
            for(unsigned s=0;s<3;++s) joint_world[s]=out.arm[1][chains[f][s]].p;
            joint_world[3]=out.arm[1][end_joint].p;
            double bone_clearance[3]{1e9,1e9,1e9}; // least surface clearance of each posed phalanx (thickness subtracted): < 0 is inside
            for(unsigned s=0;s<3;++s) for(unsigned k=0;k<=4;++k)
                bone_clearance[s]=std::min(bone_clearance[s],shape_clearance(joint_world[s]+(joint_world[s+1]-joint_world[s])*(k/4.))-geometry.thickness[f]);
            const auto planned_local=r::finger_points(g,{turn[f][0],turn[f][1],turn[f][2]});
            std::array<Vec3,4> planned{};
            for(unsigned s=0;s<4;++s) planned[s]=palm.position+palm.basis[0]*planned_local[s].x+palm.basis[1]*planned_local[s].y+palm.basis[2]*planned_local[s].z;
            const Vec3 pivot=planned_local[3];
            planned_json+=std::string(f?",":"")+"["+vec(planned[0])+","+vec(planned[1])+","+vec(planned[2])+","+vec(planned[3])+"]";
            double model_error[4]{}; // planned vs posed position of MCP, PIP, DIP and the tip (mm): the solver's geometry against the pose that is drawn
            for(unsigned k=0;k<4;++k) model_error[k]=length(planned[k]-joint_world[k])*1000;
            char buf[420],mm[3][16];
            const auto gaps=r::finger_gaps(g,geometry.thickness[f],{turn[f][0],turn[f][1],turn[f][2]},
                [&](Vec3 p) {return palm.position+palm.basis[0]*p.x+palm.basis[1]*p.y+palm.basis[2]*p.z;},[&](Vec3 w) {return shapes.empty()?1.:shape_clearance(w);});
            gap_total+=gaps.sum;
            for(unsigned s=0;s<3;++s) {if(shapes.empty()) std::snprintf(mm[s],sizeof mm[s],"%6s","-");else std::snprintf(mm[s],sizeof mm[s],"%6.1f",bone_clearance[s]*1000);}
            std::snprintf(buf,sizeof buf,"%-7s %5.1f %5.1f %5.1f             %6.1f %6.1f %6.1f          %5.1f %5.1f %5.1f      %5.1f   %-5s %s %s %s   %5.1f %5.1f %5.1f %5.1f    %3.0f %3.0f %3.0f",
                kFingers[f],deg(turn[f][0]),deg(turn[f][1]),deg(turn[f][2]),posed_angle[0],posed_angle[1],posed_angle[2],
                off_axis[0],off_axis[1],off_axis[2],meta,contact.touching[f]?"yes":"no",mm[0],mm[1],mm[2],model_error[0],model_error[1],model_error[2],model_error[3],
                contact.pose[f][0]/(r::finger_limit_degrees[f][0]/kDeg)*100.,contact.pose[f][1]/(r::finger_limit_degrees[f][1]/kDeg)*100.,contact.pose[f][2]/(r::finger_limit_degrees[f][2]/kDeg)*100.);
            std::string line=buf;char tail[80];std::snprintf(tail,sizeof tail,"   mean gap mm %4.1f %4.1f %4.1f",gaps.mean[0]*1000,gaps.mean[1]*1000,gaps.mean[2]*1000);
            report.push_back(line+tail);
        }
        if(!shapes.empty()) { // penetration: the deepest any posed phalanx (thickness included) or the palm skin (wrist -> knuckle lines, held_palm_depth out) goes into the shapes
            double fingers=0,palm_in=0;const Vec3 n=r::palm_normal(palm,geometry);
            for(unsigned f=0;f<5;++f) {const int e=sk.find(std::string("right_")+kFingers[f]+"_end_jnt");Vec3 j[4]{out.arm[1][chains[f][0]].p,out.arm[1][chains[f][1]].p,out.arm[1][chains[f][2]].p,out.arm[1][e].p};
                for(unsigned s=0;s<3;++s) for(unsigned k=0;k<=4;++k) fingers=std::max(fingers,geometry.thickness[f]-shape_clearance(j[s]+(j[s+1]-j[s])*(k/4.)));}
            for(unsigned f=1;f<5;++f) for(unsigned k=0;k<=4;++k) {const Vec3 a=out.arm[1][wrist].p,b=out.arm[1][chains[f][0]].p;palm_in=std::max(palm_in,-shape_clearance(a+(b-a)*(k/4.)+n*r::held_palm_depth));}
            const auto into=[&](Vec3 w) {return palm.position+palm.basis[0]*w.x+palm.basis[1]*w.y+palm.basis[2]*w.z;};const auto tp=[&](const std::array<double,3>& a) {return -r::finger_clearance(geometry.finger[0],geometry.thickness[0],r::finger_turns_of(geometry.finger[0],a),into,shape_clearance,0u,4,-1e300,0,2,1);};const auto drawn_a=r::finger_drawn_pose(geometry,contact,wants_used);char b[200];std::snprintf(b,sizeof b,"PENETRATION mm: fingers %.1f  palm %.1f   (thumb contact pose %.1f, drawn %.1f)",fingers*1000,palm_in*1000,tp({contact.pose[0][0],contact.pose[0][1],contact.pose[0][2]})*1000,tp({drawn_a[0][0],drawn_a[0][1],drawn_a[0][2]})*1000);report.push_back(b);
        }

        // v1082: the real shape's grip map entry, the fingertips against its RENDER mesh (what the player sees: the gap of each tip pad, thickness
        // included; <= 3 mm counts as touching), and the running path for its hold (running_report)
        for(const auto& sh:shapes) if(sh.hull) if(const auto it=real_of.find(sh.hull);it!=real_of.end()) {
            const auto& prop=*it->second;const auto& h=*sh.hull;
            const auto world=[&](Vec3 body_point) {const Vec3 q=h.to_own(body_point);return sh.frame.position+sh.frame.basis[0]*q.x+sh.frame.basis[1]*q.y+sh.frame.basis[2]*q.z;};
            std::vector<Vec3> mesh;mesh.reserve(prop.mesh.size());for(const auto& m:prop.mesh) mesh.push_back(world(m));
            const auto gap=[&](Vec3 p) {double d=1e9;
                if(!prop.tris.empty()) {for(const auto& t:prop.tris) if(t[0]>=0 && t[1]>=0 && t[2]>=0 && std::size_t(std::max({t[0],t[1],t[2]}))<mesh.size()) d=std::min(d,length(p-closest_on_triangle(p,mesh[t[0]],mesh[t[1]],mesh[t[2]])));}
                else for(const auto& sh2:shapes) d=std::min(d,r::shape_distance(sh2,p));
                return d;};
            std::string tips;unsigned touching=0;double worst=0;
            for(unsigned f=0;f<5;++f) {   // the finger's nearest pad (its three phalanges, 5 points each, thickness included) and its tip's
                const int e=sk.find(std::string("right_")+kFingers[f]+"_end_jnt");const Vec3 j[4]{out.arm[1][chains[f][0]].p,out.arm[1][chains[f][1]].p,out.arm[1][chains[f][2]].p,out.arm[1][e].p};
                double g=1e9;for(unsigned q=0;q<3;++q) for(unsigned k=0;k<=4;++k) g=std::min(g,gap(j[q]+(j[q+1]-j[q])*(k/4.))-geometry.thickness[f]);
                const double tip=gap(j[3])-geometry.thickness[f];
                if(g<=.003) ++touching;worst=std::max(worst,g);char b[60];std::snprintf(b,sizeof b,"%s%s %.1f (tip %.1f)",f?", ":"",kFingers[f],g*1000,tip*1000);tips+=b;}
            char line[400];std::snprintf(line,sizeof line,"FINGER GAP to the render mesh (mm, nearest pad, - = into it): %s; touching (<= 3 mm) %u/5, worst %.1f",tips.c_str(),touching,worst*1000);report.push_back(line);
            std::snprintf(line,sizeof line,"GRIP MAP: %s%s (real shape %.1f x %.1f x %.1f cm, %zu piece(s))",d2vr::grip_kind_name(prop.shape.grip.kind),
                prop.shape.grip.span?(", taken within "+num(prop.shape.grip.x0*100).substr(0,5)+".."+num(prop.shape.grip.x1*100).substr(0,5)+" cm along its length").c_str():"",
                prop.shape.whole.half.x*200,prop.shape.whole.half.y*200,prop.shape.whole.half.z*200,prop.shape.pieces.size());report.push_back(line);
            if(&h==&prop.shape.pieces.front()) {   // the body pose from its first piece (finger_sphere.hpp hull_grip_shape, inverted), in the palm frame: the hold
                WorldTrackedPose held{};for(unsigned j=0;j<3;++j) held.basis[j]=sh.frame.basis[0]*component(h.axes[0],j)+sh.frame.basis[1]*component(h.axes[1],j)+sh.frame.basis[2]*component(h.axes[2],j);
                held.position=sh.frame.position-(held.basis[0]*h.centre.x+held.basis[1]*h.centre.y+held.basis[2]*h.centre.z);
                WorldTrackedPose hold{};hold.position=grab_local(palm,held.position-palm.position);for(unsigned j=0;j<3;++j) hold.basis[j]=grab_local(palm,held.basis[j]);
                std::stringstream lines(running_report(hold));for(std::string l;std::getline(lines,l);) report.push_back(l);
            }
            break;
        }
        if(scene.moving) { // stability (the twitch): 90 frames of the live finger call (one finger solved a frame, 60 Hz, the held reach), the thing jittered about its place
            // in the hand the way a lagging motor moves it (small: +-3 mm, +-2 deg a frame; heavy: x4, v638's mean was 13 mm); the metric is the largest change of any finger
            // joint's contact pose between two frames over frames 60..90 (after a 1 s settle), in degrees.
            for(const double amp:{1.,4.,-1.}) {
                if(amp<0 && recorded_lag.empty()) continue;
                r::FingerIkState state{};auto wants=r::finger_wants(true,scene.squeeze,scene.index);
                unsigned seed=12345u;const auto rnd=[&seed]() {seed=seed*1664525u+1013904223u;return (seed>>8)/double(1u<<24)*2-1;};
                std::array<std::array<float,3>,5> last{},last_drawn{};double worst=0,sum=0,worst_drawn=0,sum_drawn=0;unsigned counted=0;r::FingerPoseFilter drawn_filter{};
                for(unsigned k=0;k<90;++k) {
                    Vec3 dp{rnd()*.003*amp,rnd()*.003*amp,rnd()*.003*amp},spin{rnd()*.035*amp,rnd()*.035*amp,rnd()*.035*amp};
                    if(amp<0) {dp=recorded_lag[k].first;spin=recorded_lag[k].second;}
                    const auto jittered=scene.moving(palm,geometry,dp,spin);
                    const auto c=r::finger_contact(geometry,palm,jittered.data(),static_cast<unsigned>(jittered.size()),1/60.,state,r::held_curl,1,&wants);
                    const auto drawn=drawn_filter.step(r::finger_turns(geometry,c,wants),1/60.); // what the game draws: the turns through the pose spring
                    if(k>=60) {double step=0,step_drawn=0;for(unsigned f=0;f<5;++f) for(unsigned j=0;j<3;++j) {step=std::max(step,static_cast<double>(std::abs(c.pose[f][j]-last[f][j])));step_drawn=std::max(step_drawn,static_cast<double>(std::abs(drawn[f][j]-last_drawn[f][j])));}
                        worst=std::max(worst,step);sum+=step;worst_drawn=std::max(worst_drawn,step_drawn);sum_drawn+=step_drawn;++counted;}
                    last=c.pose;last_drawn=drawn;
                }
                char note[260];
                if(amp<0) std::snprintf(note,sizeof note,"stability, the thing moving as the real motor moved it (%s): largest per-frame change of any finger joint %.2f deg, mean of the frames' largest %.2f deg",recorded_lag_note.c_str(),deg(worst),deg(sum/std::max(1u,counted)));
                else std::snprintf(note,sizeof note,"stability, thing jittered +-%.0f mm +-%.0f deg a frame: largest per-frame change of any finger joint %.2f deg, mean of the frames' largest %.2f deg",3*amp,2*amp,deg(worst),deg(sum/std::max(1u,counted)));
                report.push_back(note);
                std::snprintf(note,sizeof note,"  drawn (finger_turns through FingerPoseFilter %.0f ms): largest per-frame change %.2f deg, mean of the frames' largest %.2f deg",r::finger_pose_smooth_time*1000,deg(worst_drawn),deg(sum_drawn/std::max(1u,counted)));
                report.push_back(note);
            }
        }
        {char note[120];std::snprintf(note,sizeof note,"wrap: summed mean gap of the fifteen phalanges %.1f mm (grip fit %s)",gap_total*1000,grip_fit?"on":"off");report.push_back(note);}
        // ---- frame JSON (renderer format) plus the shapes and the hand frame for the close-up cameras.
        // The close-up cameras want an orthonormal hand frame: T = the grip's hole axis (pinky -> index, what seat_held aligns the shaft
        // with), N = the palm normal made perpendicular to it, F = fingers (wrist -> medium knuckle side of N x T).
        const Vec3 axis_t=palm.basis[0],raw_normal=r::palm_normal(palm,geometry);
        const Vec3 normal=unit(raw_normal-axis_t*dot(raw_normal,axis_t));
        Vec3 fingers_dir=unit(cross(normal,axis_t));
        if(dot(fingers_dir,out.arm[1][chains[2][0]].p-out.arm[1][wrist].p)<0) fingers_dir=fingers_dir*-1;
        {char note[200];std::snprintf(note,sizeof note,"palm normal vs hole axis: %.1f deg apart (90 = perpendicular); fingers vs medium knuckle direction: %.1f deg",
            deg(std::acos(std::clamp(dot(raw_normal,axis_t),-1.,1.))),deg(std::acos(std::clamp(dot(fingers_dir,unit(out.arm[1][chains[2][0]].p-out.arm[1][wrist].p)),-1.,1.))));report.push_back(note);}
        {   // Is the thumb inside the hand? Its IP joint and tip in the palm frame: across (pinky -> index, the knuckles' span given), along the fingers
            // from the palm centre, and height over the palm skin (palm_normal * held_palm_depth); below the skin inside the knuckles' span = in the palm.
            const Vec3 skin=palm.position+raw_normal*r::held_palm_depth;
            const auto at=[&](Vec3 p) {const Vec3 d=p-skin;return Vec3{dot(d,axis_t),dot(d,fingers_dir),dot(d,raw_normal)};};
            const auto index_k=at(out.arm[1][chains[1][0]].p),pinky_k=at(out.arm[1][chains[4][0]].p);
            const auto ip=at(out.arm[1][chains[0][2]].p),tip=at(out.arm[1][sk.find("right_thumb_end_jnt")].p);
            const double lo=std::min(index_k.x,pinky_k.x),hi=std::max(index_k.x,pinky_k.x);
            // in the palm (the pre-v697 lab test): inside the knuckles' span, short of the middle phalanges (knuckles + 3 cm along) and 5 mm under the skin plane
            const auto inside=[&](Vec3 p) {return p.x>lo && p.x<hi && p.y<std::max(index_k.y,pinky_k.y)+.03 && p.z<-.005;};
            const auto root=at(out.arm[1][chains[0][0]].p),mcp=at(out.arm[1][chains[0][1]].p);
            char note[400];std::snprintf(note,sizeof note,"thumb vs palm (mm, across/along/height over the skin; knuckle span %.0f..%.0f, knuckles along %.0f; thumb CMC %.0f/%.0f/%.0f MCP %.0f/%.0f/%.0f): IP %.0f/%.0f/%.0f, tip %.0f/%.0f/%.0f -> %s",
                lo*1000,hi*1000,std::max(index_k.y,pinky_k.y)*1000,root.x*1000,root.y*1000,root.z*1000,mcp.x*1000,mcp.y*1000,mcp.z*1000,ip.x*1000,ip.y*1000,ip.z*1000,tip.x*1000,tip.y*1000,tip.z*1000,inside(tip) || inside(ip)?"IN THE PALM":"clear");
            report.push_back(note);
            const auto drawn=r::finger_drawn_pose(geometry,contact,wants_used);
            std::snprintf(note,sizeof note,"thumb clearance from the palm and the fingers (finger_ik.hpp thumb_clearance, gap %.0f mm included): %.1f mm",r::thumb_palm_gap*1000,r::thumb_clearance(geometry,drawn,{drawn[0][0],drawn[0][1],drawn[0][2]})*1000);
            report.push_back(note);
        }
        if(!shapes.empty() && shapes[0].half.x>0) { // how the held shaft lies in the hand: along the hole axis with the palm on its wall (90 to the palm normal), or end-on to the palm
            const Vec3 axis=shapes[0].frame.basis[0];char note[240];
            std::snprintf(note,sizeof note,"held shape axis vs the hole axis: %.1f deg; vs the palm normal: %.1f deg (90 = the palm lies on its wall, 0 = on its end); its centre %.1f cm out of the palm",
                deg(std::acos(std::min(1.,std::abs(dot(axis,axis_t))))),deg(std::acos(std::min(1.,std::abs(dot(axis,raw_normal))))),dot(shapes[0].frame.position-palm.position,raw_normal)*100);
            report.push_back(note);
        }
        std::string frame="{\"label\":"+quote(scene.name)+",\"title\":"+quote(scene.title)+",\"t\":0"+
            ",\"body\":"+list(out.body,nullptr,out.scale)+
            ",\"left_arm\":"+list(out.arm[0],&out.arm_frame[0],out.scale)+",\"right_arm\":"+(game_palette?palette_list(out.arm_frame[1],Xf{bd::from_axes(palm.basis[0],palm.basis[1],palm.basis[2]),palm.position},static_cast<unsigned>(wrist),
                {static_cast<unsigned>(sk.find("right_index_02_jnt")),static_cast<unsigned>(sk.find("right_medium_02_jnt")),static_cast<unsigned>(sk.find("right_ring_02_jnt")),static_cast<unsigned>(sk.find("right_pinky_02_jnt"))}):list(out.arm[1],&out.arm_frame[1],out.scale))+
            ",\"left_arm_frame\":"+put(out.arm_frame[0],1)+",\"right_arm_frame\":"+put(out.arm_frame[1],1)+
            ",\"eye\":{\"position\":"+vec(in.eye.p)+",\"rotation_xyzw\":[0,0,0,1]}"+
            ",\"markers\":[{\"name\":\"right_grip\",\"position\":"+vec(grip.p)+"}]"+
            ",\"hand\":{\"center\":"+vec(palm.position+fingers_dir*.03+normal*.01)+",\"fingers\":"+vec(fingers_dir)+",\"normal\":"+vec(normal)+",\"thumb\":"+vec(palm.basis[0])+"}"+
            ",\"planned\":["+planned_json+"]"+
            ",\"shapes\":[";
        for(size_t i=0;i<shapes.size();++i)
            frame+=(i?",":"")+shape_json(shapes[i],scene.shape_name.empty()?"shape":scene.shape_name,scene.held?"[0.95,0.75,0.25]":"[0.35,0.65,1.0]");
        if(scene.ghost) for(const auto& g:scene.ghost(palm,geometry)) frame+=std::string(shapes.empty()?"":",")+shape_json(g,"object (not a contact)","[0.55,0.55,0.55]");
        frame+="],\"report\":[";
        for(size_t i=0;i<report.size();++i) frame+=(i?",":"")+quote(report[i]);
        frame+="]}";
        json+=(index?",":"")+frame;
        for(const auto& line:report) std::cout<<line<<"\n";
        std::cout<<"\n";
    }
    json+="]}";
    std::ofstream f(argv[1],std::ios::binary);f<<json;
    if(!f) {std::cerr<<"cannot write "<<argv[1]<<"\n";return 2;}
    return 0;
}
} // namespace

int main(int argc,char** argv) {
    try {return run(argc,argv);}
    catch(const std::exception& e) {std::cerr<<"exception: "<<e.what()<<"\n";return 3;}
}
