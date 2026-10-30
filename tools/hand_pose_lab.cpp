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
#include <cstdio>
#include <fstream>
#include <functional>
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
    if(s.rim>=0 && s.half.x>0)
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
bool grip_fit=true;double fixed_shift=-1,fixed_slide=-1,extra_half=0; // see the flags in run()
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
        else if(a.rfind("--snapshot=",0)==0) snapshot_spec=a.substr(11);
        else if(a=="--refit") refit.on=true;
        else if(a=="--sphere") snapshot_sphere=true;
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
        {"book","Book 22 x 16 x 4 cm",{.11,.08,.02}},{"folder","Folder 32 x 22 x 2 cm",{.16,.11,.01}},{"skull","Skull / head 18 x 14 x 20 cm",{.09,.07,.10}}};
    const auto box_of=[](const WorldTrackedPose& pose,Vec3 half) { // the engine's world AABB of the thing's box turned by pose (e_j = sum_i half_i |axis_i . j|)
        const auto a=[&](unsigned i,unsigned j) {return std::abs(j==0?pose.basis[i].x:j==1?pose.basis[i].y:pose.basis[i].z);};
        const double h[3]={half.x,half.y,half.z};Vec3 e{};
        e.x=h[0]*a(0,0)+h[1]*a(1,0)+h[2]*a(2,0);e.y=h[0]*a(0,1)+h[1]*a(1,1)+h[2]*a(2,1);e.z=h[0]*a(0,2)+h[1]*a(1,2)+h[2]*a(2,2);
        return NativeGrabBounds{pose.position-e,pose.position+e};
    };
    // The old path's thing: its axes the palm's (long side along the thumb axis, across the palm), centre on the palm skin by its smallest half side. Jitter moves and
    // turns it about its centre. Rows of the pose are the model axes (classify_held's body.basis).
    const auto old_pose=[](const WorldTrackedPose& palm,const r::HandGeometry& g,Vec3 half,Vec3 dp,Vec3 turn) {
        const Vec3 n=r::palm_normal(palm,g);const double lo=std::min({half.x,half.y,half.z});
        WorldTrackedPose pose{palm.position+n*(lo+r::held_palm_depth)+dp,{palm.basis[0],palm.basis[1],palm.basis[2]}};
        const double angle=length(turn);
        if(angle>1e-9) for(auto& row:pose.basis) row=rotated(bd::axis_angle(turn*(1/angle),angle),row);
        return pose;
    };
    std::vector<Scene> scenes{
        {"open","Open hand (grip 0, trigger 0)",hold_at,hold_fingers,hold_in,0.f,0.f,false,{},""},
        {"fist","Plain fist, nothing held (grip 1, trigger 1)",hold_at,hold_fingers,hold_in,1.f,1.f,false,{},""},
        {"bottle","Bottle r=3.5 cm, 25 cm (seat_held, grip 1)",hold_at,hold_fingers,hold_in,1.f,0.f,true,cylinder_scene(.035,.25),"bottle (solver capsule)"},
        {"bottle45","Wide bottle r=4.5 cm, 25 cm (seat_held, grip 1)",hold_at,hold_fingers,hold_in,1.f,0.f,true,cylinder_scene(.045,.25),"bottle (solver capsule)"},
        {"shaft40","Shaft r=4.0 cm, 25 cm (seat_held, grip 1)",hold_at,hold_fingers,hold_in,1.f,0.f,true,cylinder_scene(.04,.25),"shaft (solver capsule)"},
        {"handle","Thin handle r=1.7 cm, 20 cm (seat_held, grip 1)",hold_at,hold_fingers,hold_in,1.f,0.f,true,cylinder_scene(.017,.20),"handle (solver capsule)"},
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
        Scene old{};old.name=_strdup((std::string(t.name)+"_old").c_str());old.held=true;old.where=hold_at;old.fingers=hold_fingers;old.normal=hold_in;old.squeeze=1.f;old.index=0.f;
        old.title=_strdup((std::string(t.title)+": v638 path (classify_held on the thing's own world box)").c_str());old.shape_name="object (classify_held)";
        old.moving=[half,old_pose,box_of](const WorldTrackedPose& palm,const r::HandGeometry& g,Vec3 dp,Vec3 turn) {
            const auto pose=old_pose(palm,g,half,dp,turn);return std::vector<r::HeldShape>{r::classify_held(pose,box_of(pose,half),-1,palm)};};
        old.shapes=[old_moving=old.moving](const WorldTrackedPose& palm,const r::HandGeometry& g) {return old_moving(palm,g,Vec3{},Vec3{});};
        scenes.push_back(old);
    }
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
