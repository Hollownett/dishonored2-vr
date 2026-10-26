#include "../src/grab_force_cap.hpp"
#include "../src/selection_policy.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace d2vr;
PalmTarget point_target(std::uint64_t id,Vec3 p) {return {id,PalmKind::prop,p,p,1,{}};}
int main() {
    static_assert(grab_acquire_distance==.25 && grab_retain_distance==grab_acquire_distance);
    const Vec3 centre{3,4,5};
    const auto sphere=hand_sphere_probe(centre);
    assert(sphere.valid && length(palm_point(sphere)-centre)==0);
    assert(!hand_sphere_probe({NAN,0,0}).valid);
    assert(!hand_sphere_probe({0,INFINITY,0}).valid);
    // Every side of the grip has the same gate; no forward cone or palm offset.
    for(const Vec3 axis:{Vec3{1,0,0},Vec3{-1,0,0},Vec3{0,1,0},Vec3{0,-1,0},Vec3{0,0,1},Vec3{0,0,-1}}) {
        const auto inside=point_target(2,centre+axis*.25),outside=point_target(3,centre+axis*.251);
        assert(hand_pickup_in_range(sphere,inside.minimum,inside.maximum));
        assert(pick_by_palm(sphere,std::vector<PalmTarget>{inside},0,.01).index);
        assert(!hand_pickup_in_range(sphere,outside.minimum,outside.maximum));
        assert(!pick_by_palm(sphere,std::vector<PalmTarget>{outside},3,1).index);
        const std::vector<PalmTarget> targets{point_target(1,centre+axis*.15),point_target(2,centre-axis*.08)};
        // A nearer target behind the hand beats the previously selected target.
        assert(pick_by_palm(sphere,targets,1,.01).index==1);
        for(const Vec3 facing:{axis,axis*-1.,Vec3{NAN,0,0}}) {
            PalmProbe old;old.palm={100,200,300};old.forward=facing;old.direction=facing;old.velocity=facing;old.valid=finite(facing);
            for(bool knob:{false,true}) {
                const auto use=native_use_order_probe(old,knob,false,centre,facing);
                assert(use.probe.valid && !use.legacy && !use.beam_fallback);
                assert(length(palm_point(use.probe)-centre)==0);
                assert(pick_by_palm(use.probe,targets,1,.01).index==1);
            }
        }
    }
    // Distance uses the nearest actual surface, even when its centre is far away.
    const PalmTarget box{7,PalmKind::prop,{3.02,3,4},{7,5,6},1,{}};
    assert(pick_by_palm(sphere,std::vector<PalmTarget>{point_target(1,{3.04,4,5}),box},1,.01).index==1);
    const std::vector<PalmTarget> tied{point_target(7,{3.1,4,5}),point_target(3,{3.1,4,5})};
    assert(pick_by_palm(sphere,tied,0,.01).index==1);
    assert(pick_by_palm(sphere,tied,7,.01).index==0);
    std::puts("PASS: raw grip sphere, all directions, 25 cm gate, invalid palm independence and nearest surface wins");
}
