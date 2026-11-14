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
// v1301 (user after v1295-v1298: "the weapon sits unstable in the hands and sometimes jumps out of the hands"; his session: contact engaged
// 227/253 s, 150/403 mm one-frame jumps, 29/45 deg turns): the blade's answers now come from an emulated game thread (GameThread: the render
// post overwrites slot 1's request, a scope casts what it finds every 1/rate render frames +-jitter, weapon_hit reads the last answer <= 100 ms
// old) instead of exactly one per frame, and the cases the game has and the v1289 lab had not: leaving a surface past its edge (the plane's
// infinite extension still cuts the blade), an equip change, a teleport and a snap turn during contact, hits on filtered things (an item the
// other hand holds, an NPC walking past, the player's own body), a dual-wield copy whose collision frame is the drawn copy two frames old, and a
// minute of free play in a furnished corner. Metrics added: share of frames in contact, the drawn weapon's largest deviation from the
// controller's (tip mm, blade deg), the longest time from the controller's weapon leaving the world to the drawn one being exactly it again.
// The same file builds against the v1298 header (src/weapon_collision.hpp before v1301: the 2-argument query, as v1298's game lambda) for the
// "before" columns; with v1301's header (D2VR_WEAPON_CONTACT_V1301) the chain uses the strict query and WeaponCollision::begin, as dxgi_probe.cpp.
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
// v1301: what a box stands for. Entities below 50 are world geometry; these are the kinds the game's blade query classifies and filters.
enum : std::uintptr_t {held_entity=50,npc_entity=60,dynamic_entity=70,self_entity=80};
#ifdef D2VR_WEAPON_CONTACT_V1301
constexpr const char* contact_name="v1301";
BladeHit kind_of(std::uintptr_t e) {return e==self_entity?BladeHit::self:e==held_entity?BladeHit::held:e==npc_entity?BladeHit::npc:e==dynamic_entity?BladeHit::dynamic:BladeHit::world;}
#else
constexpr const char* contact_name="v1289";
#endif
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
    // What should stop the blade: everything but the filtered kinds (an item the other hand holds, the player's own body).
    double solid_distance(Vec3 p) const {double d=1e9;for(const auto& b:boxes) if(b.entity!=held_entity && b.entity!=self_entity) d=std::min(d,signed_distance(b,p));return d;}
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
// dxgi_probe.cpp's rotate_hand_to: a gun's hand turns by the world rotation taking the collision frame's barrel to the drawn one.
bool rotate_hand_to(WorldTrackedPose& pose,Vec3 from,Vec3 to) {
    const double lf=length(from),lt=length(to);
    if(!(lf>1e-9) || !(lt>1e-9)) return false;
    const auto a=from*(1/lf),b=to*(1/lt);
    const auto axis=cross(a,b);const double sine=length(axis),cosine=std::clamp(dot(a,b),-1.,1.);
    if(sine<1e-12) return cosine>0;
    const auto u=axis*(1/sine);
    for(auto& v:pose.basis) v=v*cosine+cross(u,v)*sine+u*(dot(u,v)*(1-cosine));
    return true;
}

// v1301: the game thread's side of the blade's slot 1 (NativeInteractionAim): the render thread's post overwrites the request; a scope every
// `period` seconds (+-jitter) casts the request it finds and records the answer with its request id and segment; weapon_answer / weapon_hit
// return the last answer while it is at most 100 ms old (an older one, or one already read, is what the render thread then gets again).
struct GameThread {
    const World* world{};double period{},next{},jitter{};Rng rng{0x2545f4914f6cdd1dull};
    struct Req {Vec3 a{},b{};double r{};std::uint64_t id{};bool valid{};} req;
    struct Ans {Vec3 a{},b{};std::uint64_t id{};std::optional<ClampHit> hit;double tick{};bool valid{};} ans;
    void post(Vec3 a,Vec3 b,double r,std::uint64_t id) {req={a,b,r,id,true};}
    void run_until(double t) {
        while(next<=t) {
            if(req.valid) {ans={req.a,req.b,req.id,world->cast(req.a,req.b,req.r),next,true};req.valid=false;}
            next+=period*(1+jitter*(2*rng.uniform()-1));
        }
    }
    bool answered(double t) const {return ans.valid && t-ans.tick<=.1;}
};

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
    // v1301: share: frames the blade response is engaged (contact, or the weapon drawn off the controller's) over all; dev_m / dev_deg: the drawn
    // weapon's largest deviation from the controller's at the hand clamp's grip (tip distance, blade angle); exact_s: the longest time from the
    // controller's weapon leaving the solid world (no closer than its radius + 1 cm skin) to the drawn weapon being exactly the controller's again
    // (a run that ends before that counts to its end); leaves: such events.
    double share{},dev_m{},dev_deg{},exact_s{};int leaves{};
};
struct Frame {WorldTrackedPose drawn;Vec3 tip;};

// One armed hand through the chain, `seconds` long at `hz`, measured after `settle`.
struct Run {
    const Scenario& sc;const Weapon& w;Motion motion;double hz;bool drop;bool legacy=false;
    double seconds=2.4,settle=.4,trace=0;   // trace: seconds of per-frame lines after settle
    std::vector<Frame>* record=nullptr;     // every frame's drawn pose (the free-air identity check)
    std::function<Vec3(double)> path;       // optional: the controller grip's offset at controller time t (the release check)
    // v1301 cases (all optional):
    double game_rate=1,game_jitter=0;                     // game-thread scopes per render frame (1: one after every post), +- the period's jitter
    std::function<const Weapon*(double)> weapon_at;       // the weapon in the hand at controller time t (an equip change); default w
    std::function<void(double,Vec3&,double&)> rig;        // the play space at time t: moved by (a teleport) and turned about the player (a snap turn)
    std::function<void(double,WorldTrackedPose&)> pose;   // the controller grip's pose at time t instead of the scenario's (free play)
    std::function<void(double,std::vector<Box>&)> move;   // the boxes at time t (an NPC walking past, an item the other hand holds)
    int copy_lag=0;   // a gun whose collision frame is the drawn weapon copy_lag frames ago (v1298: a dual-wield copy's held_copy_pose)
    double drop_dt=0;   // the dropped frame's interval (0: two frames); a game hitch: 0.1 s (the game's dt clamp)
    WeaponContactStats stats{};
    Metrics go() {
        World world{sc.boxes};
        HandClamp clamp;WeaponCollision wc;wc.set_legacy(legacy);ClampReacquireHold hold;HandMotion hand_motion;
        const unsigned side=w.sword?1u:0u;
        const Vec3 anchor0{0,0,1.6};
        // The open hand's spheres in the grip frame (the game builds them from the rig; these are plain stand-ins).
        std::array<ClampProbe,ClampInput::hand_capacity> hand{};std::size_t hand_count=0;
        for(const ClampProbe p:{ClampProbe{{0,0,0},.04},ClampProbe{{.05,0,0},.03},ClampProbe{{.1,0,0},.02},ClampProbe{{.1,.025,0},.02},
                                ClampProbe{{.1,-.025,0},.02},ClampProbe{{.04,.045,-.02},.02}}) hand[hand_count++]=p;
        ClampProbe tip=default_clamp_tip(),wall_tip=default_clamp_wall_tip();
        ClampOutput last{};bool have_last=false;
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
        std::vector<Vec3> tips,grips,axes,raw_tips,raw_axes,clamp_tips,hand_offsets;std::vector<bool> contact,reset_frame;
        std::vector<WorldTrackedPose> drawn_history;
        double t=10;   // seconds (any origin)
        const double step=1/hz;
        GameThread game{&world,step/game_rate};game.jitter=game_jitter;game.next=t+step+.5*step;   // the first scope runs after the first post
        const int frames=static_cast<int>(seconds*hz);
        const int drop_frame=static_cast<int>(1.2*hz);
        double controller_time=0,left_at=-1;bool touching_before=false;std::uint64_t weapon_key=0;Vec3 last_shift{};double last_yaw=0;
        for(int k=0;k<frames;++k) {
            const double dt=drop && k==drop_frame?(drop_dt>0?drop_dt:2*step):step;
            t+=dt;controller_time+=dt;
            const Weapon& wk=weapon_at?*weapon_at(controller_time):w;
            if(move) {world.boxes=sc.boxes;move(controller_time,world.boxes);}
            // The play space: moved and turned about the player's vertical axis (through the anchor).
            Vec3 shift{};double yaw=0;if(rig) rig(controller_time,shift,yaw);
            const auto turn=[&](Vec3 v) {return Vec3{v.x*std::cos(yaw)-v.y*std::sin(yaw),v.x*std::sin(yaw)+v.y*std::cos(yaw),v.z};};
            const Vec3 pivot{anchor0.x,anchor0.y,0},anchor=anchor0+shift,chest{anchor.x,anchor.y,anchor.z-.35};
            const auto root=level_frame(anchor,turn({1,0,0}));
            const WorldTrackedPose play{shift+pivot-turn(pivot),{turn({1,0,0}),turn({0,1,0}),{0,0,1}}};
            // The controller: base pose, slide, jitter (or the free-play pose), carried by the play space.
            WorldTrackedPose target{grip0,base};
            if(pose) pose(controller_time,target);
            else {
                if(motion==Motion::slide) target.position=target.position+sc.slide*controller_time;
                if(path) target.position=target.position+path(controller_time);
            }
            if(motion!=Motion::steady) {
                target.position=target.position+Vec3{rng.normal(),rng.normal(),rng.normal()}*.0005;
                const Vec3 axis=unit3(Vec3{rng.normal(),rng.normal(),rng.normal()+1e-9});
                const double angle=rng.normal()*.2*pi/180;
                for(auto& v:target.basis) v=rotate_about(v,axis,angle);
            }
            target.position=turn(target.position-pivot)+pivot+shift;for(auto& v:target.basis) v=turn(v);
            game.run_until(t);
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
            // The blade's collision frame: the hand's (a gun: its barrel above and behind the fist), or a dual-wield copy's drawn frame copy_lag frames ago.
            const auto offset_of=[&](const WorldTrackedPose& p) {return wk.sword?Vec3{}:p.basis[2]*wk.up-p.basis[0]*wk.back;};
            Vec3 axis=sword_blade_axis(g,false),offset=offset_of(g);
            if(copy_lag>0 && !wk.sword && static_cast<int>(drawn_history.size())>=copy_lag) {
                const auto& old=drawn_history[drawn_history.size()-copy_lag];
                axis=old.basis[0];offset=old.position+offset_of(old)-g.position;
            }
            const double cast_radius=WeaponCollision::cast_radius(wk.radius);
#ifdef D2VR_WEAPON_CONTACT_V1301
            // dxgi_probe.cpp v1301: post with the request id, return the last completed answer (its id, the segment swept, the classified hit).
            const auto query=[&](Vec3 a,Vec3 b,std::uint64_t id)->std::optional<BladeAnswer> {
                game.post(a,b,cast_radius,id);
                if(!game.answered(t)) return std::nullopt;
                BladeAnswer r{game.ans.id,game.ans.a,game.ans.b,std::nullopt};
                if(const auto& h=game.ans.hit) {CollisionHit c{h->point,h->normal,0,h->entity,h->material};c.kind=kind_of(h->entity);r.hit=c;}
                return r;
            };
            const std::uint64_t key=static_cast<std::uint64_t>(wk.length*1e6)^(static_cast<std::uint64_t>(wk.radius*1e6)<<24);
            wc.begin(side,ContactFrame{static_cast<std::uint64_t>(k),1,key,t,anchor,std::atan2(root.basis[0].y,root.basis[0].x),true});
#else
            // v1298's game lambda: post this frame's segment, return the last answer's hit (<= 100 ms old, whatever it answers); the player's own
            // body is no hit (blade_hit_is_self).
            const auto query=[&](Vec3 a,Vec3 b)->std::optional<CollisionHit> {
                game.post(a,b,cast_radius,0);
                if(!game.answered(t) || !game.ans.hit || game.ans.hit->entity==self_entity) return std::nullopt;
                const auto& h=*game.ans.hit;CollisionHit c{h.point,h.normal,0,h.entity,h.material};
                const Vec3 d=b-a;const double l2=dot(d,d);
                c.fraction=l2>0?std::clamp(dot(c.point-a,d)/l2,0.,1.):0.;
                return c;
            };
            const std::uint64_t key=0;
#endif
            const bool equip_changed=weapon_key!=key && k>0;weapon_key=key;
            const bool rig_jumped=length(shift-last_shift)>.35 || yaw!=last_yaw;last_shift=shift;last_yaw=yaw;   // a teleport or a snap turn (not a jump's or a stair's rise)
            const auto r=wc.update(side,dt,g.position,axis,wk.length,query,offset,wk.radius);
            const auto composed=compose_blade(clamp,side,g.basis,g.position,r,std::nullopt);
            g.position=composed.drawn;
            if(r.blade_dir) {
                if(wk.sword) CHECK(align_weapon_blade(g,*r.blade_dir,false));
                else {
                    const auto before=g.basis;
                    CHECK(rotate_hand_to(g,axis,*r.blade_dir));
#ifdef D2VR_WEAPON_CONTACT_V1301
                    // dxgi_probe.cpp v1301: the hand turns about the barrel's start (WeaponCollision's pivot), not about the fist.
                    if(!legacy) g.position=g.position+offset-turned_with(before,g.basis,offset);
#else
                    (void)before;
#endif
                }
            }
            clamp.note_drawn(side,root,composed.noted);
            hold.note(side,1,root,g.position);
            drawn_history.push_back(g);
            if(trace && t-10>=settle && t-10<settle+trace) std::printf("  t=%.3f answer=%s n=(%.2f %.2f %.2f) blade_dir=%s shift=%.1fmm grip=(%.3f %.3f %.3f) clamp=%d held=%d valid=%d target=(%.3f %.3f %.3f) tip=(%.3f %.3f %.3f)\n",t-10,
                game.answered(t) && game.ans.hit?"hit":game.answered(t)?"clear":"none",game.ans.hit?game.ans.hit->normal.x:0.,game.ans.hit?game.ans.hit->normal.y:0.,game.ans.hit?game.ans.hit->normal.z:0.,
                r.blade_dir?"y":"n",length(r.shift)*1000,g.position.x,g.position.y,g.position.z,static_cast<int>(clamped.state),
                static_cast<int>(clamped.held),static_cast<int>(clamped.validated),target.position.x,target.position.y,target.position.z,
                (g.position+g.basis[0]*wk.length).x,(g.position+g.basis[0]*wk.length).y,(g.position+g.basis[0]*wk.length).z);
            // The drawn weapon, and the one the controller would hold at the hand clamp's grip.
            const Vec3 drawn_axis=sword_blade_axis(g,false),from=g.position+offset_of(g);
            const Vec3 drawn_tip=from+drawn_axis*wk.length;
            const Vec3 raw_from=target.position+offset_of(target),clamp_from=clamp_grip+offset_of(target);
            const Vec3 clamp_tip=clamp_from+target.basis[0]*wk.length;
            if(record) record->push_back({g,drawn_tip});
            // Touching: the controller's weapon at the clamp's grip within its radius + 1 cm skin of what should stop it.
            double nearest=1e9;
            for(int i=0;i<=16;++i) nearest=std::min(nearest,world.solid_distance(clamp_from+target.basis[0]*(wk.length*i/16.)));
            const bool touching=nearest<wk.radius+.01;
            const bool exact=g.position.x==clamp_grip.x && g.position.y==clamp_grip.y && g.position.z==clamp_grip.z &&
                             drawn_axis.x==target.basis[0].x && drawn_axis.y==target.basis[0].y && drawn_axis.z==target.basis[0].z;
            if(t-10>=settle) {
                tips.push_back(drawn_tip);grips.push_back(g.position);axes.push_back(drawn_axis);
                raw_tips.push_back(raw_from+target.basis[0]*wk.length);raw_axes.push_back(target.basis[0]);
                clamp_tips.push_back(clamp_tip);hand_offsets.push_back(clamp_grip-target.position);
                contact.push_back(r.contact || r.blade_dir.has_value() || clamped.state==ClampState::contact);
                reset_frame.push_back(equip_changed || rig_jumped);
                for(int i=0;i<=32;++i) m.blade_depth=std::max(m.blade_depth,wk.radius-world.solid_distance(from+drawn_axis*(wk.length*i/32.)));   // the sword: its centre line
                if(r.contact || r.blade_dir || !exact) m.share+=1;
                m.dev_m=std::max(m.dev_m,length(drawn_tip-clamp_tip));m.dev_deg=std::max(m.dev_deg,axis_angle_deg(drawn_axis,target.basis[0]));
                if(touching_before && !touching) left_at=controller_time;
                if(touching) left_at=-1;
                else if(left_at>=0 && exact) {m.exact_s=std::max(m.exact_s,controller_time-left_at);++m.leaves;left_at=-1;}
            }
            touching_before=touching;
        }
        if(left_at>=0) {m.exact_s=std::max(m.exact_s,controller_time-left_at);++m.leaves;}
        stats=wc.stats(side);
        m.seconds=static_cast<double>(tips.size())/hz;
        m.share/=static_cast<double>(std::max<std::size_t>(1,tips.size()));
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
            if(!reset_frame[i]) {   // a scripted teleport / snap turn / equip change moves the weapon with the rig: not the response's jump
                m.jump=std::max(m.jump,length((tips[i]-clamp_tips[i])-(tips[i-1]-clamp_tips[i-1])));m.turn=std::max(m.turn,length(correction(i)-correction(i-1))*180/pi);
                m.hand=std::max(m.hand,length(hand_offsets[i]-hand_offsets[i-1]));
            }
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
#ifdef D2VR_WEAPON_CONTACT_V1301
    // The offset vector turned with the hand (its coordinates in the old basis, rebuilt in the new one).
    static Vec3 turned_with(const std::array<Vec3,3>& from,const std::array<Vec3,3>& to,Vec3 v) {
        return to[0]*dot(v,from[0])+to[1]*dot(v,from[1])+to[2]*dot(v,from[2]);
    }
#endif
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
// v1301: the game thread's answer cadence: one scope per render frame (the v1289 lab), half the render rate (a 45 fps game under 90 Hz: the
// render thread posts twice per answer), and 0.7 of it with +-30 % jitter (frame pacing).
struct Cadence {const char* name;double rate,jitter;};
const Cadence cadences[]={{"full",1,0},{"half",.5,0},{"jitter",.7,.3}};

// Free air: nothing to meet. contact_step must draw exactly what legacy_step draws (both: the controller), bit for bit.
void free_air_is_identical() {
    const Scenario none{"free",{},{.4,-.2,1.2},{1,.3,-.2},{0,.05,0}};
    for(const Weapon* w:weapons) for(const Motion motion:motions) for(const double hz:rates) for(const auto& c:cadences) {
        std::vector<Frame> a,b;
        Run legacy{none,*w,motion,hz,true,true};legacy.record=&a;legacy.game_rate=c.rate;legacy.game_jitter=c.jitter;legacy.go();
        Run fixed{none,*w,motion,hz,true,false};fixed.record=&b;fixed.game_rate=c.rate;fixed.game_jitter=c.jitter;fixed.go();
        CHECK(a.size()==b.size() && !a.empty());
        for(std::size_t i=0;i<a.size();++i) {
            CHECK(std::memcmp(&a[i].drawn.position,&b[i].drawn.position,sizeof(Vec3))==0);
            for(unsigned j=0;j<3;++j) CHECK(std::memcmp(&a[i].drawn.basis[j],&b[i].drawn.basis[j],sizeof(Vec3))==0);
        }
        CHECK(fixed.stats.frames==0 && fixed.stats.damped_seconds==0);
    }
    std::printf("free air: legacy and %s bit-identical (3 weapons x 3 motions x 72/90/120 Hz x 3 answer cadences, one dropped frame)\n",contact_name);
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

// ---- v1301 cases ----
// One row: legacy and the contact path on the same inputs, at each answer cadence; the worst over the given weapons and rates.
struct CaseRow {Metrics legacy,contact;WeaponContactStats stats;};
void fold(Metrics& o,const Metrics& m) {
    o.share=std::max(o.share,m.share);o.dev_m=std::max(o.dev_m,m.dev_m);o.dev_deg=std::max(o.dev_deg,m.dev_deg);o.jump=std::max(o.jump,m.jump);
    o.turn=std::max(o.turn,m.turn);o.exact_s=std::max(o.exact_s,m.exact_s);o.reversals=std::max(o.reversals,m.reversals);o.blade_depth=std::max(o.blade_depth,m.blade_depth);
    o.leaves+=m.leaves;
}
void print_header(const char* title) {
    std::printf("\n%s\n  %-34s %-6s | %-52s | %-52s\n",title,"case","answers","legacy: share dev_mm dev_deg jump_mm turn_deg exact_ms","contact: share dev_mm dev_deg jump_mm turn_deg exact_ms");
}
void print_row(const char* name,const char* cadence,const Metrics& a,const Metrics& b) {
    std::printf("  %-34s %-6s | %5.2f %6.0f %7.1f %7.1f %8.1f %8.0f        | %5.2f %6.0f %7.1f %7.1f %8.1f %8.0f\n",name,cadence,
                a.share,a.dev_m*1000,a.dev_deg,a.jump*1000,a.turn,a.exact_s*1000,b.share,b.dev_m*1000,b.dev_deg,b.jump*1000,b.turn,b.exact_s*1000);
}
template<class Setup> Metrics run_case(const Scenario& sc,const Weapon& w,Motion motion,double hz,bool legacy,const Cadence& c,double seconds,Setup&& setup,
                                       WeaponContactStats* stats=nullptr) {
    Run run{sc,w,motion,hz,false,legacy};run.seconds=seconds;run.settle=0;run.game_rate=c.rate;run.game_jitter=c.jitter;setup(run);
    const auto m=run.go();if(stats) *stats=run.stats;return m;
}
// The v1301 bounds a fixed chain must meet in every case (the invariant and the exit), checked below.
struct Bounds {double dev_deg,exact_s;};
#ifdef D2VR_WEAPON_CONTACT_V1301
const CollisionConfig v1301{};
#endif
bool failed_cases=false;
void expect(const char* name,const char* cadence,const Metrics& m,double exact_limit) {
#ifdef D2VR_WEAPON_CONTACT_V1301
    const double turn_limit=v1301.max_turn*180/pi+1e-6;
    if(!(m.dev_deg<=turn_limit) || !(m.exact_s<=exact_limit)) {
        failed_cases=true;std::printf("  OVER: %s %s: dev %.1f deg (bound %.1f), exact %.0f ms (limit %.0f)\n",name,cadence,m.dev_deg,turn_limit,m.exact_s*1000,exact_limit*1000);
    }
#else
    (void)name;(void)cadence;(void)m;(void)exact_limit;
#endif
}

void v1301_cases() {
    const auto all=scenarios();
    const Scenario& wall=all[0];
    const Weapon* const blades[]={&sword,&pistol};
    // (1) Leaving a surface past its edge: the blade pressed 5 cm into a table top near its side edge (y = .3), slid sideways off it at 0.5 m/s
    // (0.8-1.6 s) and held there: the top's plane still cuts the blade, but nothing is there. The weapon must be the controller's again promptly.
    {   const Scenario table{"table-edge",{{{.5,-1,0},{1.5,.3,1.0},7}},{.3,0,1.25},{std::cos(.48),0,-std::sin(.48)},{0,0,0}};
        const auto off=[](double t) {const double u=std::clamp((t-.8)/.8,0.,1.);return Vec3{0,.4*u,0};};
        print_header("v1301 (1) leaving a table top past its edge, then still 1.4 s (worst of sword/pistol, 72/90/120 Hz; exact_ms: controller off the table -> drawn = controller)");
        for(const auto& c:cadences) {
            Metrics a{},b{};
            for(const Weapon* w:blades) for(const double hz:rates) {
                fold(a,run_case(table,*w,Motion::jitter,hz,true,c,3.0,[&](Run& r) {r.path=off;}));
                fold(b,run_case(table,*w,Motion::jitter,hz,false,c,3.0,[&](Run& r) {r.path=off;}));
            }
            print_row("table edge, slid off and held",c.name,a,b);expect("table edge",c.name,b,.35);
        }
    }
    // (2) An equip change during contact: the sword pressed into the wall 0.8 s, then a pistol in the same hand (in free air: shorter).
    {   print_header("v1301 (2) equip change during contact: sword in the wall, pistol from 0.8 s (free air)");
        for(const auto& c:cadences) {
            Metrics a{},b{};
            for(const double hz:rates) for(const bool legacy:{true,false})
                fold(legacy?a:b,run_case(wall,sword,Motion::jitter,hz,legacy,c,2.0,[&](Run& r) {r.weapon_at=[](double t) {return t<.8?&sword:&pistol;};}));
            print_row("sword -> pistol",c.name,a,b);expect("equip change",c.name,b,.05);
        }
    }
    // (3) A teleport during contact: the play space moves 6 m along the wall, past its end (the wall's plane still cuts the blade, no wall is there).
    {   print_header("v1301 (3) teleport during contact: 6 m along the wall, past its end, at 0.8 s");
        for(const auto& c:cadences) {
            Metrics a{},b{};
            for(const Weapon* w:blades) for(const double hz:rates) for(const bool legacy:{true,false})
                fold(legacy?a:b,run_case(wall,*w,Motion::jitter,hz,legacy,c,2.0,[&](Run& r) {r.rig=[](double t,Vec3& s,double&) {if(t>=.8) s={0,6,0};};}));
            print_row("teleport 6 m",c.name,a,b);expect("teleport",c.name,b,.05);
        }
    }
    // (4) A snap turn during contact: the play space turns 45 degrees left about the player; the blade leaves the wall with it.
    {   print_header("v1301 (4) snap turn during contact: 45 deg left at 0.8 s (the blade ends in free air)");
        for(const auto& c:cadences) {
            Metrics a{},b{};
            for(const Weapon* w:blades) for(const double hz:rates) for(const bool legacy:{true,false})
                fold(legacy?a:b,run_case(wall,*w,Motion::jitter,hz,legacy,c,2.0,[&](Run& r) {r.rig=[](double t,Vec3&,double& y) {if(t>=.8) y=pi/4;};}));
            print_row("snap turn 45 deg",c.name,a,b);expect("snap turn",c.name,b,.05);
        }
    }
    // (5) Filtered hits. (a) An item the other hand holds, swinging through the blade's path 3 times a second (a corpse or prop in that hand: never a
    // surface for this blade). (b) The player's own body column crossing the blade (a hanging sword along the legs). (c) An NPC walking through the
    // blade at 1.2 m/s: a surface while it is there (as v1288), never remembered after it has gone.
    {   const Scenario free_sword{"free",{},{.3,-.25,1.1},{.9,.2,-.4},{0,0,0}};
        const auto held=[](double t,std::vector<Box>& b) {const double y=-.25+.3*std::sin(2*pi*3*t);b.push_back({{.55,y-.06,.75},{.75,y+.06,1.0},held_entity});};
        const auto self=[](double,std::vector<Box>& b) {b.push_back({{.45,-.4,0},{.75,-.1,1.8},self_entity});};
        const auto npc=[](double t,std::vector<Box>& b) {const double y=-1.5+1.2*(t-.4);b.push_back({{.5,y-.2,0},{.8,y+.2,1.8},npc_entity});};
        print_header("v1301 (5) filtered hits, the sword otherwise in free air (exact_ms for the NPC: after it has passed)");
        for(const auto& c:cadences) {
            Metrics a[3]{},b[3]{};
            for(const double hz:rates) for(const bool legacy:{true,false}) {
                fold(legacy?a[0]:b[0],run_case(free_sword,sword,Motion::jitter,hz,legacy,c,2.0,[&](Run& r) {r.move=held;}));
                fold(legacy?a[1]:b[1],run_case(free_sword,sword,Motion::jitter,hz,legacy,c,2.0,[&](Run& r) {r.move=self;}));
                fold(legacy?a[2]:b[2],run_case(free_sword,sword,Motion::jitter,hz,legacy,c,3.0,[&](Run& r) {r.move=npc;}));
            }
            print_row("item held by the other hand",c.name,a[0],b[0]);print_row("own body",c.name,a[1],b[1]);print_row("NPC walking through",c.name,a[2],b[2]);
#ifdef D2VR_WEAPON_CONTACT_V1301
            if(b[0].dev_m>0 || b[1].dev_m>0) {failed_cases=true;std::printf("  OVER: a filtered hit moved the weapon (%s)\n",c.name);}
#endif
            expect("NPC",c.name,b[2],.35);
        }
    }
    // (6) A dual-wield copy (v1298): the gun's collision frame is the drawn copy two frames old (held_copy_pose), so each correction is computed
    // on the last one. v1301's call site builds the frame from this frame's hand and the copy's grip (copy_lag 0, the pistol rows).
    {   print_header("v1301 (6) dual-wield copy pistol, wall and corner, jitter: collision frame = the drawn copy 2 frames old vs this frame's hand");
        for(const auto& c:cadences) for(const int sc_index:{0,2}) for(const int lag:{2,0}) {
            Metrics a{},b{};double rev[2]{};
            for(const double hz:rates) for(const bool legacy:{true,false}) {
                const auto m=run_case(all[sc_index],pistol,Motion::jitter,hz,legacy,c,2.4,[&](Run& r) {r.copy_lag=lag;r.settle=.4;});
                fold(legacy?a:b,m);rev[legacy?0:1]=std::max(rev[legacy?0:1],m.reversals);
            }
            char name[64];std::snprintf(name,sizeof(name),"%s, frame %s rev/s %.0f|%.0f",all[sc_index].name,lag?"2 old":"now",rev[0],rev[1]);
            print_row(name,c.name,a,b);
        }
    }
    // (8) A jump (user after v1298: "the weapon jumps when I move my hand and when I go up: jumping, climbing stairs"): the sword's tip 4.6 cm
    // into a wall beside a counter; the play space rises 0.3 s (10 cm a frame at 90 Hz slowing to 2: 1.6 m), falls back 0.3 s, lands. The hand still
    // in the player's frame, or swinging sideways 0.15 m at 3 or 4 Hz (2.8 / 3.8 m/s peak) across the wall and over the counter's edge.
    // (9) Stairs: 0.18 m risers every 0.4 m from 0.9 m ahead, the player walking up at 1.2 m/s, the sword held low ahead so its tip meets the
    // nosings; the play space rising smoothly (0.45 m per m) or in steps (each 0.18 m over 3 frames, 6 cm a frame); the hand still or swinging
    // fore and aft 0.15 m at 3 / 4 Hz.
    {   const auto jump_rig=[](double t,Vec3& s,double&) {
            const auto rise=[](double u) {return 9*u-12*u*u;};   // metres after u seconds of the rise (10 -> 2 cm a frame at 90 Hz)
            double z=0;
            if(t>=.6 && t<.9) z=rise(t-.6);else if(t>=.9 && t<1.2) z=rise(.3)-rise(t-.9);
            s={0,0,z};
        };
        const Scenario jump_wall{"jump-wall",{{{.75,-4,0},{4.75,4,4},7},{{.2,.15,0},{.75,.6,.95},8},{{-4,-4,-1},{4,4,0},9}},{.2,-.15,1.15},{1,.35,-.25},{0,0,0}};
        std::vector<Box> steps;for(int i=0;i<10;++i) steps.push_back({{.9+.4*i,-1,0},{20,1,.18*(i+1)},static_cast<std::uintptr_t>(20+i)});
        steps.push_back({{-4,-4,-1},{20,4,0},19});
        const Scenario stairs{"stairs",steps,{.3,-.2,.9},{1,0,-1.2},{0,0,0}};
        const auto stairs_rig=[](bool stepped) {return [stepped](double t,Vec3& s,double&) {
            const double x=1.2*std::max(0.,t-.4);double z=.45*std::max(0.,x-.7);
            if(stepped) {const double below=std::floor(z/.18)*.18,frac=(z-below)/.18;z=below+.18*std::clamp((frac-12./13)*13.,0.,1.);}   // each rise in the last 1/13 of its run
            s={x,0,z};};};
        const auto swing=[](double hz,Vec3 axis) {return [hz,axis](double t) {return axis*(.15*std::sin(2*pi*hz*t));};};
        const Weapon* const both[]={&sword,&pistol};
        for(const int kind:{8,9}) {
            print_header(kind==8?"v1301 (8) jump beside a wall and a counter (rev/s legacy|contact in the name)":"v1301 (9) stairs, walking up (rev/s legacy|contact in the name)");
            for(const auto& c:cadences) for(const int variant:{0,1,2,3}) {   // jump: still, 3 Hz, 4 Hz; stairs: smooth still, smooth 4 Hz, stepped still, stepped 4 Hz
                if(kind==8 && variant==3) continue;
                Metrics a{},b{};double rev[2]{};
                for(const Weapon* w:both) for(const double hz:rates) for(const bool legacy:{true,false}) {
                    const auto m=run_case(kind==8?jump_wall:stairs,*w,Motion::jitter,hz,legacy,c,kind==8?2.0:3.0,[&](Run& r) {
                        if(kind==8) {r.rig=jump_rig;if(variant) r.path=swing(variant==1?3.:4.,{0,1,0});}
                        else {r.rig=stairs_rig(variant>=2);if(variant&1) r.path=swing(4.,{1,0,0});}
                    });
                    fold(legacy?a:b,m);rev[legacy?0:1]=std::max(rev[legacy?0:1],m.reversals);
                    const CollisionConfig cfg{};
                    if(!legacy && !(m.jump<=(cfg.follow_speed+cfg.guard_speed)/hz+.002)) {
#ifdef D2VR_WEAPON_CONTACT_V1301
                        failed_cases=true;
#endif
                        std::printf("  OVER: %s %s %.0f Hz %s variant %d: jump %.1f mm\n",kind==8?"jump":"stairs",w->name,hz,c.name,variant,m.jump*1000);
                    }
                }
                const char* names8[]={"still","swing 3 Hz (2.8 m/s)","swing 4 Hz (3.8 m/s)"};
                const char* names9[]={"smooth, still","smooth, swing 4 Hz","stepped, still","stepped, swing 4 Hz"};
                char name[64];std::snprintf(name,sizeof(name),"%s rev %.0f|%.0f",kind==8?names8[variant]:names9[variant],rev[0],rev[1]);
                print_row(name,c.name,a,b);
            }
        }
    }
    // (10) A hitch during contact: one 100 ms frame (dxgi_probe.cpp clamps the blade's dt to 0.1 s) while the hand slides along the wall at 0.5 m/s
    // or swings across a corner at 2.8 m/s. v1289's follower took the whole 0.1 s in one step (up to 150 mm + 200 mm of guard).
    {   print_header("v1301 (10) one 100 ms hitch at 1.2 s during contact (jump_mm includes the hitch frame)");
        for(const auto& c:cadences) for(const int sc_index:{0,2}) {
            Metrics a{},b{};
            for(const Weapon* w:blades) for(const double hz:rates) for(const bool legacy:{true,false}) {
                Run run{all[sc_index],*w,Motion::jitter,hz,true,legacy};run.seconds=2.0;run.game_rate=c.rate;run.game_jitter=c.jitter;run.drop_dt=.1;
                run.path=[sc_index](double t) {return sc_index==0?Vec3{0,.5*t,0}:Vec3{0,0,.15*std::sin(2*pi*3*t)};};
                fold(legacy?a:b,run.go());
            }
            print_row(sc_index==0?"wall, sliding 0.5 m/s":"corner, swinging 2.8 m/s",c.name,a,b);
        }
    }
    // (7) Free play: a minute of reaching around a furnished corner (two walls, a table with a free edge, a pillar), the hand moving to a new
    // random pose every 0.4-1.2 s with tracking jitter: brief touches, near misses, long free stretches.
    {   const Scenario room{"room",{{{1.25,-4,0},{4,4,3},7},{{-4,1.2,0},{4,4,3},8},{{.55,-.9,0},{1.25,-.1,.75},9},{{-1.2,.4,0},{-.9,.7,3},10},{{-4,-4,-1},{4,4,0},11}},
                            {0,0,0},{1,0,0},{0,0,0}};
        print_header("v1301 (7) free play, 60 s around a furnished corner (exact_ms: the longest leave -> exact)");
        for(const auto& c:cadences) for(const Weapon* w:{&sword}) {   // the guns are too short to reach this room's surfaces
            Metrics a{},b{};WeaponContactStats st{};
            for(const bool legacy:{true,false}) {
                Rng pick{0xc0ffee123456789ull};double t0=0,t1=0;Vec3 p0{.2,-.2,1.1},p1=p0,d0{1,0,-.3},d1=d0;
                const auto wander=[pick,t0,t1,p0,p1,d0,d1](double t,WorldTrackedPose& pose) mutable {
                    while(t>=t1) {
                        t0=t1;t1=t0+.4+.8*pick.uniform();p0=p1;d0=d1;
                        const double yaw=(pick.uniform()*2-1)*1.4,pitch=-1.1+1.5*pick.uniform(),reach=.3+.45*pick.uniform();
                        const Vec3 dir{std::cos(pitch)*std::cos(yaw),std::cos(pitch)*std::sin(yaw),std::sin(pitch)};
                        p1=Vec3{.15,-.2,1.35}+unit3(Vec3{std::cos(yaw),std::sin(yaw),.6*std::sin(pitch)})*reach;d1=dir;
                    }
                    const double u=std::clamp((t-t0)/(t1-t0),0.,1.),s=u*u*(3-2*u);
                    pose.position=p0+(p1-p0)*s;pose.basis=basis_along(d0+(d1-d0)*s);
                };
                fold(legacy?a:b,run_case(room,*w,Motion::jitter,90,legacy,c,60,[&](Run& r) {r.pose=wander;},legacy?nullptr:&st));
            }
            char name[64];std::snprintf(name,sizeof(name),"%s, %d leaves",w->name,b.leaves);
            print_row(name,c.name,a,b);expect("free play",c.name,b,.35);
        }
    }
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
    // Every scenario x weapon x motion x 72/90/120 Hz x (no drop, one dropped frame) x answer cadence: legacy and the contact path on identical
    // inputs. The summary is the worst case over weapons, rates, the drop and the cadences. rev/s: reversals per second (raw: the controller tip's
    // own); jump/turn: what the contact response moved the tip (mm) / the blade (deg) in one frame; depth: the drawn blade's deepest point inside
    // geometry (mm; the sword's centre line); dev: the drawn weapon's largest deviation from the controller's (tip mm / deg).
    std::printf("\n%-12s %-6s | %6s | %-50s | %-50s\n","scenario","motion","raw/s","legacy: rev/s  jump_mm turn_deg depth dev_mm dev_deg",
                (std::string(contact_name)+":  rev/s  jump_mm turn_deg depth dev_mm dev_deg").c_str());
    bool failed=false;
    for(const auto& sc:all) for(const Motion motion:motions) {
        Metrics worst[2]{};double raw=0;
        for(const Weapon* w:weapons) for(const double hz:rates) for(const bool drop:{false,true}) for(const auto& c:cadences) for(const bool legacy:{true,false}) {
            Run run{sc,*w,motion,hz,drop,legacy};run.game_rate=c.rate;run.game_jitter=c.jitter;const auto m=run.go();
            auto& o=worst[legacy?0:1];
            o.reversals=std::max(o.reversals,m.reversals);o.jump=std::max(o.jump,m.jump);o.turn=std::max(o.turn,m.turn);o.blade_depth=std::max(o.blade_depth,m.blade_depth);
            o.hand=std::max(o.hand,m.hand);o.dev_m=std::max(o.dev_m,m.dev_m);o.dev_deg=std::max(o.dev_deg,m.dev_deg);
            raw=std::max(raw,m.raw_reversals);
            if(table) std::printf("  %-12s %-8s %-6s %4.0f %-4s %-6s %-6s contact %.2f rev/s %5.1f (raw %4.1f) tip %6.1f mm rot %6.2f deg grip %6.1f mm jump %6.1f mm turn %6.2f deg path %5.2f depth %6.1f mm dev %6.1f mm %5.1f deg | stats: frames %llu changes %llu rev %llu jump %.1f mm turn %.2f deg damped %.2f s\n",
                sc.name,w->name,motion_name(motion),hz,drop?"drop":"",c.name,legacy?"legacy":contact_name,m.contact,m.reversals,m.raw_reversals,m.tip_max*1000,m.rot_max,m.grip_max*1000,m.jump*1000,m.turn,m.path_ratio,m.blade_depth*1000,m.dev_m*1000,m.dev_deg,
                static_cast<unsigned long long>(run.stats.frames),static_cast<unsigned long long>(run.stats.plane_changes),static_cast<unsigned long long>(run.stats.reversals),run.stats.max_jump_m*1000,run.stats.max_turn_deg,run.stats.damped_seconds);
            // At most two reversals a second beyond the controller's own; the blade response never moves the tip faster than the follower and the
            // guard together (follow_speed + guard_speed: 3.5 m/s, 49 mm a frame at 72 Hz, 29 at 120 Hz - a glide, in seconds, not frames). The
            // blade's depth inside geometry is reported, not held to a bound: v1301's max_turn / max_push cap the correction (the weapon never
            // leaves the hand), so a press deeper than the cap allows leaves the blade partly inside.
            const CollisionConfig cfg{};
            const double bound=(cfg.follow_speed+cfg.guard_speed)*(drop?2:1)/hz+.002;
            if(!legacy && !(m.reversals<=m.raw_reversals+2. && m.jump<=bound)) {
                failed=true;std::printf("  OVER: %s %s %s %.0f Hz%s %s: rev/s %.1f (raw %.1f) jump %.1f mm turn %.2f deg depth %.1f mm\n",sc.name,w->name,motion_name(motion),hz,drop?" drop":"",
                                        c.name,m.reversals,m.raw_reversals,m.jump*1000,m.turn,m.blade_depth*1000);
            }
        }
        std::printf("%-12s %-6s | %6.1f | %6.1f %8.1f %8.2f %6.1f %6.0f %6.1f        | %6.1f %8.1f %8.2f %6.1f %6.0f %6.1f\n",sc.name,motion_name(motion),raw,
                    worst[0].reversals,worst[0].jump*1000,worst[0].turn,worst[0].blade_depth*1000,worst[0].dev_m*1000,worst[0].dev_deg,
                    worst[1].reversals,worst[1].jump*1000,worst[1].turn,worst[1].blade_depth*1000,worst[1].dev_m*1000,worst[1].dev_deg);
    }
    v1301_cases();
    CHECK(!failed);
    CHECK(!failed_cases);
    std::puts("PASS");
}
