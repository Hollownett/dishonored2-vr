// v1289 lab (user, headset and sim window 2026-11-13: "a held weapon touching the corner of an object or a wall sometimes thrashes hard from
// side to side, not damped, not smooth"). Offline, no game: the per-frame chain of dxgi_probe.cpp's weapon block for one armed hand, with the
// real classes - HandClamp::update (strict answers, one frame late) -> the drawn grip -> WeaponCollision::update (the blade's sphere cast from
// the drawn grip along the controller's blade, its answer read one frame late, the fraction re-measured on this frame's segment, as the game's
// query lambda does) -> compose_blade -> align_weapon_blade -> HandClamp::note_drawn - against solid boxes answered like the engine's closest-hit
// sphere cast (a box edge reports the rounded normal: sweep centre minus the closest point). Scenarios: a flat wall, a convex 90 degree edge, a
// concave corner of two and of three planes, a thin shelf's edge; the controller steady, with tracking jitter (0.5 mm, 0.2 deg a frame), and
// sliding along the surface at 5 cm/s with jitter; 72/90/120 Hz; with one dropped frame (one render interval twice as long).
// Metrics on the DRAWN weapon: frame-to-frame tip/grip step and blade rotation, reversals per second (two consecutive tip steps over 5 mm that
// point more than 90 degrees apart), the largest jump, the drawn tip's path over the controller tip's, the deepest penetration of the drawn blade.
// Run: build/tmp-check-run.cmd weapon_contact_lab /DNOMINMAX; then build/tmp-weapon_contact_lab.exe table (every run), or
// build/tmp-weapon_contact_lab.exe <scenario> <weapon> <motion> [hz] [seconds] [legacy] (a per-frame trace).
#include "hand_clamp_feed.hpp"
#include "weapon_collision.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)

namespace {
constexpr double pi=3.14159265358979323846;
struct Box {Vec3 lo,hi;std::uintptr_t entity;};
Vec3 closest(const Box& b,Vec3 p) {return {std::clamp(p.x,b.lo.x,b.hi.x),std::clamp(p.y,b.lo.y,b.hi.y),std::clamp(p.z,b.lo.z,b.hi.z)};}
// Signed distance to a box: negative inside.
double signed_distance(const Box& b,Vec3 p) {
    const Vec3 c=closest(b,p);
    if(length(p-c)>0) return length(p-c);
    return -std::min({p.x-b.lo.x,b.hi.x-p.x,p.y-b.lo.y,b.hi.y-p.y,p.z-b.lo.z,b.hi.z-p.z});
}
struct World {
    std::vector<Box> boxes;
    double distance(Vec3 p) const {double d=1e9;for(const auto& b:boxes) d=std::min(d,signed_distance(b,p));return d;}
    // Closest-hit sphere sweep. The distance from a point moving on a line to a convex box is convex in t: golden section for its minimum,
    // bisection for the first touch.
    std::optional<ClampHit> cast(Vec3 from,Vec3 to,double r) const {
        std::optional<ClampHit> best;double best_t=2;
        const Vec3 d=to-from;
        for(const auto& b:boxes) {
            const auto f=[&](double t) {const Vec3 p=from+d*t;return length(p-closest(b,p))-r;};
            if(f(0)<=0) {   // starts touching: initial overlap
                const Vec3 c=closest(b,from);Vec3 n=from-c;
                if(length(n)<1e-9) {   // centre inside: the face of least depth
                    const double depth[6]{from.x-b.lo.x,b.hi.x-from.x,from.y-b.lo.y,b.hi.y-from.y,from.z-b.lo.z,b.hi.z-from.z};
                    const Vec3 normal[6]{{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
                    n=normal[std::min_element(depth,depth+6)-depth];
                } else n=n*(1/length(n));
                if(best_t>0) {best_t=0;best=ClampHit{c,n,b.entity,1,true,true,BodyClass::world};}
                continue;
            }
            double lo=0,hi=1;const double g=(std::sqrt(5.)-1)/2;
            double x1=hi-g*(hi-lo),x2=lo+g*(hi-lo),f1=f(x1),f2=f(x2);
            for(int i=0;i<90;++i) {
                if(f1<f2) {hi=x2;x2=x1;f2=f1;x1=hi-g*(hi-lo);f1=f(x1);}
                else {lo=x1;x1=x2;f1=f2;x2=lo+g*(hi-lo);f2=f(x2);}
            }
            double tmin=(lo+hi)*.5;if(f(1)<f(tmin)) tmin=1;
            if(f(tmin)>0) continue;
            double a=0,c=tmin;
            for(int i=0;i<64;++i) {const double m=(a+c)*.5;if(f(m)>0) a=m;else c=m;}
            if(c>=best_t) continue;
            const Vec3 centre=from+d*c,point=closest(b,centre);Vec3 n=centre-point;
            n=length(n)>1e-9?n*(1/length(n)):d*(-1/length(d));
            best_t=c;best=ClampHit{point,n,b.entity,1,false,true,BodyClass::world};
        }
        return best;
    }
    std::optional<ClampHit> cast(const ClampSweep& s) const {return s.valid?cast(s.from,s.to,s.radius):std::nullopt;}
};

// Deterministic noise (the same inputs for the legacy and the fixed chain).
struct Rng {
    std::uint64_t s;
    double uniform() {s^=s<<13;s^=s>>7;s^=s<<17;return static_cast<double>(s>>11)*(1./9007199254740992.);}
    double normal() {double a=0;for(int i=0;i<4;++i) a+=uniform();return (a-2)*std::sqrt(3.);}   // mean 0, sd 1 (sum of 4 uniforms)
};
Vec3 unit3(Vec3 v) {return v*(1/length(v));}
std::array<Vec3,3> basis_along(Vec3 forward) {
    const Vec3 f=unit3(forward);Vec3 up{0,0,1};
    if(std::abs(dot(up,f))>.95) up={0,1,0};
    const Vec3 z=unit3(up-f*dot(up,f));return {f,cross(z,f),z};
}
Vec3 rotate_about(Vec3 v,Vec3 axis,double angle) {
    return v*std::cos(angle)+cross(axis,v)*std::sin(angle)+axis*(dot(axis,v)*(1-std::cos(angle)));
}
double axis_angle_deg(Vec3 a,Vec3 b) {return std::acos(std::clamp(dot(a,b)/(length(a)*length(b)),-1.,1.))*180/pi;}

struct Weapon {const char* name;double length,radius,up,back;bool sword;};
const Weapon sword{"sword",.65,0,0,0,true},pistol{"pistol",.36,.035,.06,.06,false},crossbow{"crossbow",.28,.06,.05,.03,false};
struct Scenario {const char* name;std::vector<Box> boxes;Vec3 grip,dir,slide;};   // grip: the sword's; a shorter weapon's grip is moved along dir so its tip ends where the sword's does
enum class Motion {steady,jitter,slide};
const char* motion_name(Motion m) {return m==Motion::steady?"steady":m==Motion::jitter?"jitter":"slide";}

struct Metrics {
    double seconds{},contact{},tip_max{},rot_max{},grip_max{},reversals{},raw_reversals{},path_ratio{},blade_depth{},jump{},turn{},hand{};
    // jump / turn: the largest frame-to-frame change of the drawn tip's offset from the weapon the controller would hold at the hand clamp's grip
    // (m) and of the drawn blade's rotation from the controller's (deg): what the blade response moved (the status counters' max_jump_m /
    // max_turn_deg, computed here independently). hand: the same for the hand clamp's grip against the controller's (the hand itself at a surface).
};
struct Frame {WorldTrackedPose drawn;Vec3 tip;};

// One armed hand through the chain, `seconds` long at `hz`, measured after `settle`.
struct Run {
    const Scenario& sc;const Weapon& w;Motion motion;double hz;bool drop;bool legacy=false;
    double seconds=2.4,settle=.4,trace=0;   // trace: seconds of per-frame lines after settle
    std::vector<Frame>* record=nullptr;     // every frame's drawn pose (the free-air identity check)
    std::function<Vec3(double)> path;       // optional: the controller grip's offset at controller time t (the release check)
    WeaponContactStats stats{};
    Metrics go() {
        World world{sc.boxes};
        HandClamp clamp;WeaponCollision wc;wc.set_legacy(legacy);ClampReacquireHold hold;HandMotion hand_motion;
        const unsigned side=w.sword?1u:0u;
        const Vec3 anchor{0,0,1.6},chest{anchor.x,anchor.y,anchor.z-.35};
        const auto root=level_frame(anchor,{1,0,0});
        const WorldTrackedPose play{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}};
        // The open hand's spheres in the grip frame (the game builds them from the rig; these are plain stand-ins).
        std::array<ClampProbe,ClampInput::hand_capacity> hand{};std::size_t hand_count=0;
        for(const ClampProbe p:{ClampProbe{{0,0,0},.04},ClampProbe{{.05,0,0},.03},ClampProbe{{.1,0,0},.02},ClampProbe{{.1,.025,0},.02},
                                ClampProbe{{.1,-.025,0},.02},ClampProbe{{.04,.045,-.02},.02}}) hand[hand_count++]=p;
        ClampProbe tip=default_clamp_tip(),wall_tip=default_clamp_wall_tip();
        ClampOutput last{};bool have_last=false;
        std::optional<ClampHit> blade_answer;   // the game thread's answer to last frame's blade post (weapon_hit, slot 1)
        ClampSweep blade_posted{};
        Rng rng{0x9e3779b97f4a7c15ull ^ static_cast<std::uint64_t>(hz*1000) ^ (static_cast<std::uint64_t>(motion)<<40)};
        const auto base=basis_along(sc.dir);
        // A gun's grip: its muzzle 6 cm past where the sword's blade line enters the geometry (the sword: the scenario's grip).
        Vec3 grip0=sc.grip;
        if(!w.sword) {
            double entry=.45;
            if(const auto h=world.cast(sc.grip,sc.grip+unit3(sc.dir)*sword.length,1e-4)) entry=dot(h->point-sc.grip,unit3(sc.dir));
            grip0=sc.grip+unit3(sc.dir)*(entry+.06-(w.length-w.back))-base[2]*w.up;
        }
        Metrics m{};
        std::vector<Vec3> tips,grips,axes,raw_tips,raw_axes,clamp_tips,hand_offsets;std::vector<bool> contact;
        double t=10;   // seconds (any origin)
        const double step=1/hz;
        const int frames=static_cast<int>(seconds*hz);
        const int drop_frame=static_cast<int>(1.2*hz);
        double controller_time=0;
        for(int k=0;k<frames;++k) {
            const double dt=drop && k==drop_frame?2*step:step;
            t+=dt;controller_time+=dt;
            // The controller: base pose, slide, jitter.
            WorldTrackedPose target{grip0,base};
            if(motion==Motion::slide) target.position=target.position+sc.slide*controller_time;
            if(path) target.position=target.position+path(controller_time);
            if(motion!=Motion::steady) {
                target.position=target.position+Vec3{rng.normal(),rng.normal(),rng.normal()}*.0005;
                const Vec3 axis=unit3(Vec3{rng.normal(),rng.normal(),rng.normal()+1e-9});
                const double angle=rng.normal()*.2*pi/180;
                for(auto& v:target.basis) v=rotate_about(v,axis,angle);
            }
            // The clamp, its answers one frame late (strict request ids).
            ClampInput in{};in.now=t;in.epoch=1;in.root=root;in.play=play;in.target=target;in.chest=chest;in.post=true;in.tip_enabled=true;
            in.strict_answers=true;
            hand_motion.sample(target.position,t);
            tip=leading_probe(hand,target,hand_motion.velocity(),tip);wall_tip=wall_leading_probe(hand,target,hand_motion.velocity(),wall_tip);
            in.hand=hand;in.hand_count=hand_count;in.tip=tip;in.wall_tip=wall_tip;
            if(have_last) {
                in.answer_ids=last.request_ids;
                in.palm_hit=world.cast(last.palm_sweep);in.tip_hit=world.cast(last.tip_sweep);in.wall_tip_hit=world.cast(last.wall_tip_sweep);
                in.exclude_hit=world.cast(last.exclude_sweep);
            }
            const auto clamped=clamp.update(side,in);last=clamped;have_last=true;
            WorldTrackedPose g=target;g.position=clamped.drawn.position;
            if(clamped.reacquired) if(const auto kept=hold.hold(side,1,root,target.position,clamp.config().snap_gap)) g.position=*kept;
            const Vec3 clamp_grip=g.position;
            // The blade (the game's query lambda: post this frame's segment, read last frame's answer, re-measure the fraction).
            const Vec3 axis=sword_blade_axis(g,false);
            const Vec3 offset=w.sword?Vec3{}:g.basis[2]*w.up-g.basis[0]*w.back;
            const auto query=[&](Vec3 a,Vec3 b)->std::optional<CollisionHit> {
                blade_posted=ClampSweep{a,b,WeaponCollision::cast_radius(w.radius),true};
                if(!blade_answer) return std::nullopt;
                CollisionHit c{blade_answer->point,blade_answer->normal,0,blade_answer->entity,blade_answer->material};
                const Vec3 d=b-a;const double l2=dot(d,d);
                c.fraction=l2>0?std::clamp(dot(c.point-a,d)/l2,0.,1.):0.;
                return c;
            };
            const auto r=wc.update(side,dt,g.position,axis,w.length,query,offset,w.radius);
            const auto composed=compose_blade(clamp,side,g.basis,g.position,r,std::nullopt);
            g.position=composed.drawn;
            if(r.blade_dir) CHECK(align_weapon_blade(g,*r.blade_dir,false));
            clamp.note_drawn(side,root,composed.noted);
            hold.note(side,1,root,g.position);
            if(trace && t-10>=settle && t-10<settle+trace) std::printf("  t=%.3f hit=%s n=(%.2f %.2f %.2f) blade_dir=%s shift=%.1fmm grip=(%.3f %.3f %.3f) clamp=%d held=%d valid=%d target=(%.3f %.3f %.3f) tip=(%.3f %.3f %.3f)\n",t-10,
                blade_answer?"y":"n",blade_answer?blade_answer->normal.x:0.,blade_answer?blade_answer->normal.y:0.,blade_answer?blade_answer->normal.z:0.,
                r.blade_dir?"y":"n",length(r.shift)*1000,g.position.x,g.position.y,g.position.z,static_cast<int>(clamped.state),
                static_cast<int>(clamped.held),static_cast<int>(clamped.validated),target.position.x,target.position.y,target.position.z,
                (g.position+g.basis[0]*w.length).x,(g.position+g.basis[0]*w.length).y,(g.position+g.basis[0]*w.length).z);
            // The game thread answers what was posted this frame.
            blade_answer=world.cast(blade_posted);
            // The drawn weapon.
            const Vec3 drawn_axis=sword_blade_axis(g,false),from=g.position+(w.sword?Vec3{}:g.basis[2]*w.up-g.basis[0]*w.back);
            const Vec3 drawn_tip=from+drawn_axis*w.length;
            const Vec3 raw_from=target.position+(w.sword?Vec3{}:target.basis[2]*w.up-target.basis[0]*w.back);
            if(record) record->push_back({g,drawn_tip});
            if(t-10>=settle) {
                tips.push_back(drawn_tip);grips.push_back(g.position);axes.push_back(drawn_axis);
                raw_tips.push_back(raw_from+target.basis[0]*w.length);raw_axes.push_back(target.basis[0]);
                clamp_tips.push_back(raw_from+(clamp_grip-target.position)+target.basis[0]*w.length);hand_offsets.push_back(clamp_grip-target.position);
                contact.push_back(r.contact || r.blade_dir.has_value() || clamped.state==ClampState::contact);
                for(int i=0;i<=32;++i) m.blade_depth=std::max(m.blade_depth,w.radius-world.distance(from+drawn_axis*(w.length*i/32.)));   // the sword: its centre line
            }
        }
        stats=wc.stats(side);
        m.seconds=static_cast<double>(tips.size())/hz;
        double drawn_path=0,raw_path=0;
        constexpr double big=.005;
        const auto correction=[&](std::size_t i) {   // the drawn blade's rotation from the controller's (rotation vector)
            const Vec3 a=raw_axes[i],b=axes[i],c=cross(a,b);const double sn=length(c);
            return sn>1e-12?c*(std::atan2(sn,dot(a,b))/sn):Vec3{};
        };
        for(std::size_t i=1;i<tips.size();++i) {
            const Vec3 d=tips[i]-tips[i-1],rd=raw_tips[i]-raw_tips[i-1];
            drawn_path+=length(d);raw_path+=length(rd);
            m.tip_max=std::max(m.tip_max,length(d));m.grip_max=std::max(m.grip_max,length(grips[i]-grips[i-1]));
            m.rot_max=std::max(m.rot_max,axis_angle_deg(axes[i],axes[i-1]));
            m.jump=std::max(m.jump,length((tips[i]-clamp_tips[i])-(tips[i-1]-clamp_tips[i-1])));m.turn=std::max(m.turn,length(correction(i)-correction(i-1))*180/pi);
            m.hand=std::max(m.hand,length(hand_offsets[i]-hand_offsets[i-1]));
            if(contact[i]) m.contact+=1;
            if(i>=2) {
                const Vec3 p=tips[i-1]-tips[i-2],rp=raw_tips[i-1]-raw_tips[i-2];
                if(length(d)>big && length(p)>big && dot(d,p)<0) m.reversals+=1;
                if(length(rd)>big && length(rp)>big && dot(rd,rp)<0) m.raw_reversals+=1;
            }
        }
        m.path_ratio=raw_path>1e-9?drawn_path/raw_path:(drawn_path>1e-9?1e9:1);
        m.contact/=static_cast<double>(std::max<std::size_t>(1,tips.size()-1));
        m.reversals/=m.seconds;m.raw_reversals/=m.seconds;
        return m;
    }
};

std::vector<Scenario> scenarios() {
    const double big=4;
    std::vector<Scenario> s;
    // (a) a flat wall 0.75 m ahead; the blade yawed 35 degrees, its tip 18 cm into the wall. Slide: along the wall.
    s.push_back({"wall",{{{.75,-big,0},{.75+big,big,3},7}},{.4,-.2,1.2},{std::cos(.61),std::sin(.61),0},{0,.05,0}});
    // (b) a convex 90 degree edge: a pillar (x >= .7, y >= 0); the blade crosses its vertical edge. Slide: across the edge.
    s.push_back({"convex-edge",{{{.7,0,0},{1.2,.5,3},7}},{.35,-.15,1.2},{.5,.16,0},{0,.05,0}});
    // (c) a concave corner of two walls (x >= .75, y >= .45); the blade points into the corner line. Slide: up the corner.
    s.push_back({"corner-2",{{{.75,-big,0},{.75+big,big,3},7},{{-big,.45,0},{big,.45+big,3},8}},{.35,.05,1.2},{1,1,0},{0,0,.05}});
    // (c') three planes: the same corner on a table top (z <= 1.0); the blade points down into it. Slide: along wall A.
    s.push_back({"corner-3",{{{.75,-big,0},{.75+big,big,3},7},{{-big,.45,0},{big,.45+big,3},8},{{-big,-big,0},{big,big,1.0},9}},
                 {.35,.05,1.2},{1,1,-.8},{0,.05,0}});
    // (d) a thin shelf's front edge (2 cm thick, top at 1.10 m, front face at x = .6); the blade crosses it near the edge. Slide: along the edge.
    s.push_back({"thin-edge",{{{.6,-big,1.08},{1.4,big,1.10},7}},{.3,-.1,1.2},{std::cos(.35),0,-std::sin(.35)},{0,.05,0}});
    return s;
}
const Weapon* const weapons[]={&sword,&pistol,&crossbow};
const Motion motions[]={Motion::steady,Motion::jitter,Motion::slide};
const double rates[]={72.,90.,120.};

// Free air: nothing to meet. contact_step must draw exactly what legacy_step draws (both: the controller), bit for bit.
void free_air_is_identical() {
    const Scenario none{"free",{},{.4,-.2,1.2},{1,.3,-.2},{0,.05,0}};
    for(const Weapon* w:weapons) for(const Motion motion:motions) for(const double hz:rates) {
        std::vector<Frame> a,b;
        Run legacy{none,*w,motion,hz,true,true};legacy.record=&a;legacy.go();
        Run fixed{none,*w,motion,hz,true,false};fixed.record=&b;fixed.go();
        CHECK(a.size()==b.size() && !a.empty());
        for(std::size_t i=0;i<a.size();++i) {
            CHECK(std::memcmp(&a[i].drawn.position,&b[i].drawn.position,sizeof(Vec3))==0);
            for(unsigned j=0;j<3;++j) CHECK(std::memcmp(&a[i].drawn.basis[j],&b[i].drawn.basis[j],sizeof(Vec3))==0);
        }
        CHECK(fixed.stats.frames==0 && fixed.stats.damped_seconds==0);
    }
    std::puts("free air: legacy and v1289 bit-identical (3 weapons x 3 motions x 72/90/120 Hz, one dropped frame)");
}

// Pressed into the corner, then pulled straight back out to free air: the drawn weapon returns to the controller without overshoot (the
// tip's offset from the controller's never grows during the pull-back) and then passes it through exactly, at every rate.
void release_settles_exactly() {
    const auto all=scenarios();
    const Scenario& corner=all[2];
    const auto back=[](double t) {const double u=std::clamp((t-.6)/.2,0.,1.);return Vec3{-.28,-.28,0}*(u*u*(3-2*u));};   // in the corner until 0.6 s, back 40 cm by 0.8 s
    const auto base=basis_along(corner.dir);
    for(const double hz:rates) {
        std::vector<Frame> frames;
        Run run{corner,sword,Motion::steady,hz,false,false};run.record=&frames;run.seconds=2.0;run.path=back;
        run.go();
        double worst_growth=0,settled_at=-1,last_offset=1e9;
        for(std::size_t i=0;i<frames.size();++i) {
            const double t=static_cast<double>(i+1)/hz;
            const Vec3 grip=corner.grip+back(t),raw_tip=grip+base[0]*sword.length;
            const double offset=length(frames[i].tip-raw_tip);
            if(t>.8) {
                worst_growth=std::max(worst_growth,offset-last_offset);
                if(settled_at<0 && frames[i].drawn.position.x==grip.x && frames[i].drawn.position.y==grip.y && frames[i].drawn.position.z==grip.z &&
                   frames[i].drawn.basis[0].x==base[0].x && frames[i].drawn.basis[0].y==base[0].y && frames[i].drawn.basis[0].z==base[0].z) settled_at=t;
            }
            last_offset=offset;
        }
        std::printf("release at %3.0f Hz: the drawn sword is the controller's exactly %.0f ms after the pull-back ended; offset growth while releasing %.3f mm\n",
                    hz,(settled_at-.8)*1000,worst_growth*1000);
        CHECK(settled_at>0 && settled_at-.8<.6);
        CHECK(worst_growth<=1e-6);
    }
}

// The follower against a fixed wall, the controller still: the blade's rotation from the controller's climbs to the target without passing it
// (critically damped: no overshoot), leaves the wall as a quick glide (guard_speed) rather than a one-frame jump, is never deeper than slack past
// the standoff from 0.1 s on, and settles on legacy_step's pose.
void follower_does_not_overshoot() {
    for(const double hz:rates) {
        WeaponCollision wc;const double dt=1/hz;
        const auto wall=[](Vec3 a,Vec3 b)->std::optional<CollisionHit> {   // x = 1, normal -x, as a 2 cm sphere sweep reports it
            const double r=.02;if(b.x<1-r) return std::nullopt;
            if(a.x>=1-r) return CollisionHit{{1,a.y,a.z},{-1,0,0},0,7};
            const double t=(1-r-a.x)/(b.x-a.x);const Vec3 c=a+(b-a)*t;return CollisionHit{{1,c.y,c.z},{-1,0,0},t,7};
        };
        const Vec3 grip{.5,0,0},dir=unit3(Vec3{1,1,0});
        double previous=0;Vec3 posted_a{},posted_b{},last_tip{};bool posted=false;
        const auto late=[&](Vec3 a,Vec3 b)->std::optional<CollisionHit> {   // the game's query: post this segment, answer last frame's
            std::optional<CollisionHit> h;if(posted) h=wall(posted_a,posted_b);
            posted_a=a;posted_b=b;posted=true;return h;
        };
        for(int k=0;k<static_cast<int>(hz);++k) {
            const auto r=wc.update(0,dt,grip,dir,.9,late);
            const double angle=r.blade_dir?std::acos(std::clamp(dot(*r.blade_dir,dir),-1.,1.)):0.;
            CHECK(angle>=previous-1e-12);previous=angle;
            // The tip (14.6 cm into the wall when the first answer lands on frame 1) glides out - guard_speed plus the follower's own step: 23-43 mm
            // a frame at 120-72 Hz, never a 5 cm jump - and is never deeper than slack past the 1 cm standoff from 0.1 s on (legacy_step: out on
            // frame 1, in one 146 mm jump).
            const Vec3 tip=grip+(r.blade_dir?*r.blade_dir:dir)*.9;
            if(k>=2) CHECK(length(tip-last_tip)<=.05);
            if(k>=static_cast<int>(.1*hz)) CHECK(tip.x<=1-.01+.015+1e-9);
            last_tip=tip;
        }
        WeaponCollision old;old.set_legacy(true);old.update(0,dt,grip,dir,.9,wall);
        const auto settled=old.update(0,dt,grip,dir,.9,wall);
        CHECK(settled.blade_dir && std::abs(previous-std::acos(std::clamp(dot(*settled.blade_dir,dir),-1.,1.)))<1e-6);   // settled on the legacy pose
    }
    std::puts("follower: monotone, no overshoot, glides out at guard_speed, within slack from 0.1 s, settles on legacy_step's pose (72/90/120 Hz)");
}
} // namespace

int main(int argc,char** argv) {
    const auto all=scenarios();
    if(argc>=4) {   // weapon_contact_lab <scenario> <weapon> <motion> [hz] [seconds] [legacy]: a per-frame trace
        for(const auto& sc:all) if(std::strcmp(sc.name,argv[1])==0) for(const Weapon* w:weapons) if(std::strcmp(w->name,argv[2])==0)
            for(const Motion motion:motions) if(std::strcmp(motion_name(motion),argv[3])==0) {
                Run run{sc,*w,motion,argc>=5?std::atof(argv[4]):90.,false,argc>=7};run.trace=argc>=6?std::atof(argv[5]):.25;run.go();}
        return 0;
    }
    const bool table=argc>=2 && std::strcmp(argv[1],"table")==0;
    free_air_is_identical();
    release_settles_exactly();
    follower_does_not_overshoot();
    // Every scenario x weapon x motion x 72/90/120 Hz x (no drop, one dropped frame): legacy and v1289 on identical inputs. The summary is the worst
    // case over weapons, rates and the drop. rev/s: reversals per second (raw: the controller tip's own); jump/turn: what the contact response moved
    // the tip (mm) / the blade (deg) in one frame; depth: the drawn blade's deepest point inside geometry (mm; the sword's centre line).
    std::printf("\n%-12s %-6s | %6s | %-43s | %-43s\n","scenario","motion","raw/s","legacy: rev/s  jump_mm turn_deg depth hand_mm","v1289:  rev/s  jump_mm turn_deg depth hand_mm");
    bool failed=false;
    for(const auto& sc:all) for(const Motion motion:motions) {
        Metrics worst[2]{};double raw=0;
        for(const Weapon* w:weapons) for(const double hz:rates) for(const bool drop:{false,true}) for(const bool legacy:{true,false}) {
            Run run{sc,*w,motion,hz,drop,legacy};const auto m=run.go();
            auto& o=worst[legacy?0:1];
            o.reversals=std::max(o.reversals,m.reversals);o.jump=std::max(o.jump,m.jump);o.turn=std::max(o.turn,m.turn);o.blade_depth=std::max(o.blade_depth,m.blade_depth);
            o.hand=std::max(o.hand,m.hand);
            raw=std::max(raw,m.raw_reversals);
            if(table) std::printf("  %-12s %-8s %-6s %4.0f %-4s %-6s contact %.2f rev/s %5.1f (raw %4.1f) tip %6.1f mm rot %6.2f deg grip %6.1f mm jump %6.1f mm turn %6.2f deg path %5.2f depth %6.1f mm | stats: frames %llu changes %llu rev %llu jump %.1f mm turn %.2f deg damped %.2f s\n",
                sc.name,w->name,motion_name(motion),hz,drop?"drop":"",legacy?"legacy":"v1289",m.contact,m.reversals,m.raw_reversals,m.tip_max*1000,m.rot_max,m.grip_max*1000,m.jump*1000,m.turn,m.path_ratio,m.blade_depth*1000,
                static_cast<unsigned long long>(run.stats.frames),static_cast<unsigned long long>(run.stats.plane_changes),static_cast<unsigned long long>(run.stats.reversals),run.stats.max_jump_m*1000,run.stats.max_turn_deg,run.stats.damped_seconds);
            // v1289: at most two reversals a second beyond the controller's own; the blade response never moves the tip faster than the follower and
            // the guard together (follow_speed + guard_speed: 3.5 m/s, 49 mm a frame at 72 Hz, 29 at 120 Hz - a glide, in seconds, not frames); the
            // blade at most 4 cm inside. The hand clamp's own steps (hand_mm) are reported, not held to it: the same in legacy and v1289 (a second
            // cause, hand_clamp.hpp).
            const CollisionConfig cfg{};
            const double bound=(cfg.follow_speed+cfg.guard_speed)*(drop?2:1)/hz+.002;
            if(!legacy && !(m.reversals<=m.raw_reversals+2. && m.jump<=bound && m.blade_depth<=.04)) {
                failed=true;std::printf("  OVER: %s %s %s %.0f Hz%s: rev/s %.1f (raw %.1f) jump %.1f mm turn %.2f deg depth %.1f mm\n",sc.name,w->name,motion_name(motion),hz,drop?" drop":"",
                                        m.reversals,m.raw_reversals,m.jump*1000,m.turn,m.blade_depth*1000);
            }
        }
        std::printf("%-12s %-6s | %6.1f | %6.1f %8.1f %8.2f %6.1f %7.1f        | %6.1f %8.1f %8.2f %6.1f %7.1f\n",sc.name,motion_name(motion),raw,
                    worst[0].reversals,worst[0].jump*1000,worst[0].turn,worst[0].blade_depth*1000,worst[0].hand*1000,
                    worst[1].reversals,worst[1].jump*1000,worst[1].turn,worst[1].blade_depth*1000,worst[1].hand*1000);
    }
    CHECK(!failed);
    std::puts("PASS");
}
