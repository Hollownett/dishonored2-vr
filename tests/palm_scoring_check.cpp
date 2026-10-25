#include "grab_force_cap.hpp"
#include "selection_policy.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
bool near(double a,double b,double tol=1e-9) {return std::fabs(a-b)<=tol;}
// Palm at the origin, fingers along +Z, palm facing +X. palm_point is (0,0,.015).
PalmProbe probe_at(Vec3 palm={},Vec3 direction={1,0,0},Vec3 velocity={}) {
    PalmProbe p;p.palm=palm;p.forward={0,0,1};p.direction=direction;p.velocity=velocity;p.valid=true;return p;
}
PalmTarget box(std::uint64_t id,PalmKind kind,Vec3 c,Vec3 half,double priority=1) {
    PalmTarget t;t.id=id;t.kind=kind;t.minimum=c-half;t.maximum=c+half;t.priority=priority;return t;
}
double cost_of(const PalmProbe& p,const PalmTarget& t,double gate=.45) {auto s=palm_cost(p,t,.25,gate);CHECK(s);return s->cost;}
// Rotate v about the Z axis by deg degrees.
Vec3 rot_z(Vec3 v,double deg) {const double r=deg*3.14159265358979323846/180,c=std::cos(r),s=std::sin(r);return {v.x*c-v.y*s,v.x*s+v.y*c,v.z};}
struct Rng {unsigned s=12345;double next() {s=s*1664525u+1013904223u;return (double)(s>>8)/(double)(1u<<24)*2-1;}};
}
int main() {
    const Vec3 tiny{.005,.005,.005};
    {   // facing_beats_nearer_behind_knuckles
        const auto p=probe_at();
        const auto book=box(1,PalmKind::prop,{.30,0,.015},{.02,.02,.02}),cup=box(2,PalmKind::prop,{-.12,0,.015},{.02,.02,.02});
        CHECK(cost_of(p,book)<cost_of(p,cup));
        std::vector<PalmTarget> t{cup,book};
        const auto pick=pick_by_palm(p,t,0,.25);
        CHECK(pick.index && *pick.index==1);
    }
    {   // back_of_hand_loses
        const auto book=box(1,PalmKind::prop,{.20,0,.015},{.02,.02,.02});
        const double front=cost_of(probe_at(),book),back=cost_of(probe_at({},{-1,0,0}),book);
        CHECK(back>=2.5*front);
    }
    {   // approach_breaks_tie, exact 0.8 and the 1.2 clamp
        const auto a=box(1,PalmKind::prop,{.20,.05,.015},tiny),b=box(2,PalmKind::prop,{.20,-.05,.015},tiny);
        const auto still=probe_at();
        CHECK(near(cost_of(still,a),cost_of(still,b)));
        auto closing=probe_at({},{1,0,0},Vec3{1,0,0});   // the target is on +X, so +1 m/s along X closes at 1 m/s
        const auto on=box(3,PalmKind::prop,{.20,0,.015},tiny);
        CHECK(near(cost_of(closing,on)/cost_of(still,on),.8,1e-12));
        CHECK(near(cost_of(probe_at({},{1,0,0},Vec3{-1,0,0}),on)/cost_of(still,on),1.2,1e-12));
        CHECK(near(cost_of(probe_at({},{1,0,0},Vec3{5,0,0}),on)/cost_of(still,on),.8,1e-12));    // clamp low
        CHECK(near(cost_of(probe_at({},{1,0,0},Vec3{-5,0,0}),on)/cost_of(still,on),1.2,1e-12));  // clamp high
        // a target moving away at the same speed closes slower: it is the relative velocity that counts
        auto fleeing=on;fleeing.velocity={1,0,0};
        CHECK(near(cost_of(closing,fleeing),cost_of(still,on),1e-12));
        // the closer-closing of two otherwise equal targets wins
        PalmProbe diag=probe_at({},{1,0,0},Vec3{0,1,0});   // moving toward +Y: closes on a (y=+.05), recedes from b
        std::vector<PalmTarget> t{b,a};
        const auto pick=pick_by_palm(diag,t,0,.25);
        CHECK(pick.index && t[*pick.index].id==1);
    }
    {   // coin_beats_table_shell
        const auto p=probe_at();
        // palm_point = (0,0,.015); the table top is 2 cm below it
        const auto coin=box(1,PalmKind::loot,{.12,0,-.005+.0025},{.025,.025,.0025},.8);
        const auto table=box(2,PalmKind::usable,{.3,0,-.02-.05},{.6,.6,.05},1.0);
        std::vector<PalmTarget> t{table,coin};
        const auto pick=pick_by_palm(p,t,0,.25);
        CHECK(pick.index && t[*pick.index].id==1);
        // padding: without it a 5 cm coin 12 cm off would sit at a larger distance
        auto bare=coin;bare.kind=PalmKind::prop;
        CHECK(palm_cost(p,coin,.25,.45)->distance<palm_cost(p,bare,.25,.45)->distance);
        // lab case from the G1 measurement: palm facing a coin on a desk beats a larger prop whose bounds are slightly nearer but off the palm direction
        const auto desk_coin=box(10,PalmKind::loot,{.10,0,.015},{.0125,.0125,.0025},.8);
        const auto typewriter=box(11,PalmKind::prop,{.05,-.20,.015},{.16,.16,.08}); // its bounds reach about 4 cm from the palm point, to the side
        CHECK(palm_cost(p,typewriter,.25,.45)->distance<palm_cost(p,desk_coin,.25,.45)->distance);
        std::vector<PalmTarget> desk{typewriter,desk_coin};
        const auto desk_pick=pick_by_palm(p,desk,0,.25);
        CHECK(desk_pick.index && desk[*desk_pick.index].id==10);
    }
    {   // cost_monotonic_in_distance
        const auto p=probe_at();
        double prev=-1;
        for(double d=.02;d<=.4495;d+=.01) {   // up to the 0.45 m gate (the last step is 0.44)
            const auto t=box(1,PalmKind::prop,{d,0,.015},{0,0,0});
            const double c=cost_of(p,t);
            CHECK(c>prev);prev=c;
        }
    }
    {   // jitter_never_flips (H9): the start state has the runner-up within 14 % of the holder's raw cost, and without hysteresis the same frames flip
        const auto coin_a=box(1,PalmKind::loot,{.25,.04,.015},{.0125,.0125,.0025},.8),coin_b=box(2,PalmKind::loot,{.25,-.04,.015},{.0125,.0125,.0025},.8);
        const std::vector<PalmTarget> coins{coin_a,coin_b};
        const Vec3 rest{0,.012,0};   // the hand is nearer coin A
        const double ca=cost_of(probe_at(rest),coin_a),cb=cost_of(probe_at(rest),coin_b);
        CHECK(cb>ca && cb<ca/palm_hysteresis);   // B is the runner-up, inside the hysteresis band: removing 0.86 changes the outcome somewhere below
        PalmSelection sel;
        CHECK(sel.update(probe_at(rest),coins,.25,0).index && sel.current()==1);
        Rng rng;unsigned raw_flips=0;std::uint64_t held_before=sel.hysteresis_held();
        for(int f=1;f<=500;++f) {
            const Vec3 j{rng.next()*.01,rng.next()*.01,rng.next()*.01};
            const auto pr=probe_at(rest+j,rot_z({1,0,0},rng.next()*3));
            sel.update(pr,coins,.25,static_cast<std::uint64_t>(f)*11);
            CHECK(sel.current()==1);
            if(cost_of(pr,coin_b)<cost_of(pr,coin_a)) ++raw_flips;   // frames where the raw cost alone would have picked B
        }
        CHECK(sel.switches_10s(500*11)==0);
        CHECK(raw_flips>0);                                        // the jitter does cross the raw tie, so this test is sensitive to the 0.86
        CHECK(sel.hysteresis_held()>held_before);
    }
    {   // clear_move_flips: 6 cm toward B flips (coins close enough that the lateral move changes the cost by more than the 14 % band; the 0.6 facing bias favours off-axis targets, so far coins would not flip) within 3 frames
        const auto coin_a=box(1,PalmKind::loot,{.04,.04,.015},{.0125,.0125,.0025},.8),coin_b=box(2,PalmKind::loot,{.04,-.04,.015},{.0125,.0125,.0025},.8);
        const std::vector<PalmTarget> coins{coin_a,coin_b};
        PalmSelection sel;
        sel.update(probe_at({0,0,0}),coins,.25,0);CHECK(sel.current()==1);
        unsigned frames=0;
        for(int i=1;i<=3 && sel.current()!=2;++i) {sel.update(probe_at({0,-.06,0}),coins,.25,i*11);++frames;}
        CHECK(sel.current()==2 && frames<=3);
        CHECK(sel.switches_10s(100)==1);
        CHECK(sel.switches_10s(11000)==0);   // the window is 10 s
    }
    {   // hysteresis_ratio_pinned: a challenger at 0.87x holds, at 0.85x switches. The challenger sits to the side so its cost is tuned by distance.
        const auto p=probe_at();
        const auto holder=box(1,PalmKind::prop,{.20,0,.015},{0,0,0});
        const double c0=cost_of(p,holder);
        auto make=[&](double ratio) {   // find the on-axis distance whose raw cost is ratio * c0 (cost is monotonic on the axis)
            double lo=.0,hi=.45;
            for(int i=0;i<80;++i) {const double m=(lo+hi)/2;if(cost_of(p,box(2,PalmKind::prop,{m,0,.015},{0,0,0}))<ratio*c0) lo=m;else hi=m;}
            return box(2,PalmKind::prop,{(lo+hi)/2,0,.015},{0,0,0});
        };
        for(auto [ratio,switches]:{std::pair<double,bool>{.87,false},{.85,true}}) {
            PalmSelection sel;
            const std::vector<PalmTarget> only{holder};
            sel.update(p,only,.25,0);CHECK(sel.current()==1);
            const std::vector<PalmTarget> both{holder,make(ratio)};
            sel.update(p,both,.25,10);
            CHECK(sel.current()==(switches?2u:1u));
        }
    }
    {   // facing_bias_pinned: probe the formula through a unit-cost target (distance 0 -> dn 0, still, priority 1)
        // use a target whose cost is isolated: same distance, only the facing differs
        const auto t=box(1,PalmKind::prop,{.1,0,.015},{0,0,0});
        const double front=cost_of(probe_at(),t),back=cost_of(probe_at({},{-1,0,0}),t);
        const double dn=.1/(default_arm_length*.25),base=dn*dn+1;
        CHECK(near(front,1.16*base,1e-12) && near(back,3.56*base,1e-12));
    }
    {   // gates_pinned
        const auto p=probe_at();
        const auto at=[&](double d) {return box(1,PalmKind::prop,{d,0,.015},{0,0,0});};
        CHECK(palm_cost(p,at(.449),.25,grab_acquire_distance));
        CHECK(!palm_cost(p,at(.451),.25,grab_acquire_distance));
        const std::vector<PalmTarget> near_t{at(.449)},far_t{at(.451)},held_near{at(.549)},held_far{at(.551)};
        CHECK(pick_by_palm(p,near_t,0,.25).index);
        CHECK(!pick_by_palm(p,far_t,0,.25).index);
        CHECK(!pick_by_palm(p,held_near,0,.25).index);       // not held: a new target needs the 0.45 gate
        CHECK(pick_by_palm(p,held_near,1,.25).index);        // held: kept at 0.549
        CHECK(!pick_by_palm(p,held_far,1,.25).index);        // dropped at 0.551
        PalmSelection sel;
        sel.update(p,near_t,.25,0);CHECK(sel.current()==1);
        sel.update(p,held_near,.25,1);CHECK(sel.current()==1);
        sel.update(p,held_far,.25,2);CHECK(sel.current()==0);
        // H10: eligibility is the distance from the palm point, not the hover sphere: a target past the sphere's far edge at 0.30 m with a small radius
        const std::vector<PalmTarget> beyond_sphere{at(.40)};
        CHECK(pick_by_palm(p,beyond_sphere,0,.1).index);
        // a palm on the item always keeps it
        CHECK(palm_cost(p,box(1,PalmKind::prop,{0,0,.015},{.01,.01,.01}),.25,0)->distance==0);
    }
    {   // loot_priority_pinned
        const GrabTuning t;
        CHECK(t.priority(PalmKind::loot)==.8 && t.priority(PalmKind::bone,0)==1.1 && t.priority(PalmKind::bone,1)==1.1);
        CHECK(t.priority(PalmKind::bone,5)==1 && t.priority(PalmKind::living)==1 && t.priority(PalmKind::usable)==1 && t.priority(PalmKind::prop)==1);
        // priority is applied once, as a plain multiplier, on the cost
        const auto a=box(1,PalmKind::prop,{.2,0,.015},{0,0,0},1.),b=box(1,PalmKind::prop,{.2,0,.015},{0,0,0},1.1);
        CHECK(near(cost_of(probe_at(),b),1.1*cost_of(probe_at(),a),1e-12));
    }
    {   // left_and_right_palm_directions_mirror
        WorldTrackedPose raw{{0,0,0},{Vec3{0,0,1},Vec3{1,0,0},Vec3{0,1,0}}};   // forward +Z, left +X, up +Y
        const PalmOffset none{};
        const auto l=palm_probe(0,raw,none,std::nullopt,{}),r=palm_probe(1,raw,none,std::nullopt,{});
        CHECK(l.valid && r.valid);
        CHECK(near(l.direction.x,-r.direction.x) && near(r.direction.x,1) && near(l.direction.x,-1));
        const auto on_left_side=box(1,PalmKind::prop,{-.2,0,.015},{0,0,0}),on_right_side=box(2,PalmKind::prop,{.2,0,.015},{0,0,0});
        const std::vector<PalmTarget> both{on_left_side,on_right_side};
        CHECK(pick_by_palm(l,both,0,.25).index==std::optional<std::size_t>(0));
        CHECK(pick_by_palm(r,both,0,.25).index==std::optional<std::size_t>(1));
        // palm_point/hover_centre geometry
        CHECK(near(palm_point(r).z,.015) && near(hover_centre(r,.25).x,.8*.25));
    }
    {   // non_finite_probe_keeps_pick
        const std::vector<PalmTarget> t{box(1,PalmKind::prop,{.2,0,.015},{0,0,0}),box(2,PalmKind::prop,{.1,0,.015},{0,0,0})};
        PalmSelection sel;
        sel.update(probe_at(),t,.25,0);CHECK(sel.current()==2);
        PalmProbe bad=probe_at({std::nan(""),0,0});bad.valid=false;
        const auto pick=sel.update(bad,t,.25,10);
        CHECK(pick.fallback && sel.current()==2 && pick.index==std::optional<std::size_t>(1) && sel.scoring_fallback()==1);
        WorldTrackedPose raw{{std::nan(""),0,0},{Vec3{0,0,1},Vec3{1,0,0},Vec3{0,1,0}}};
        CHECK(!palm_probe(0,raw,PalmOffset{},std::nullopt,{}).valid);
        sel.reset();
        const auto none=sel.update(bad,t,.25,20);   // nothing held and a bad probe: never a new pick
        CHECK(none.fallback && !none.index && sel.current()==0 && sel.scoring_fallback()==2);
    }
    {   // arm_length_default
        CHECK(palm_probe(0,WorldTrackedPose{{},{Vec3{0,0,1},Vec3{1,0,0},Vec3{0,1,0}}},PalmOffset{},std::nullopt,{}).arm_length==.65);
        CHECK(palm_probe(0,WorldTrackedPose{{},{Vec3{0,0,1},Vec3{1,0,0},Vec3{0,1,0}}},PalmOffset{},.8,{}).arm_length==.8);
        CHECK(palm_probe(0,WorldTrackedPose{{},{Vec3{0,0,1},Vec3{1,0,0},Vec3{0,1,0}}},PalmOffset{},std::nan(""),{}).arm_length==.65);
        const auto t=box(1,PalmKind::prop,{.13,0,.015},{0,0,0});
        const double dn=.13/(.65/4);
        CHECK(near(cost_of(probe_at(),t),1.16*(dn*dn+1),1e-12));
    }
    {   // ties go to the lower id, whichever order they are listed in
        const auto a=box(7,PalmKind::prop,{.2,.05,.015},{0,0,0}),b=box(3,PalmKind::prop,{.2,-.05,.015},{0,0,0});
        const std::vector<PalmTarget> ab{a,b},ba{b,a};
        CHECK(ab[*pick_by_palm(probe_at(),ab,0,.25).index].id==3 && ba[*pick_by_palm(probe_at(),ba,0,.25).index].id==3);
    }
    std::puts("palm_scoring_check passed");
    return 0;
}
