#include "grab_force_cap.hpp"
#include "selection_policy.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace d2vr;
PalmProbe probe(Vec3 at={}) {PalmProbe p;p.palm=at;p.forward={0,0,1};p.direction={1,0,0};p.valid=true;return p;}
PalmTarget target(unsigned id,Vec3 centre,PalmKind kind=PalmKind::prop,double priority=1) {
    return {id,kind,centre,centre,priority,{}};
}
int main() {
    auto p=probe();
    // A nearby object behind the knuckles beats a farther, well-facing object.
    const auto near=target(2,{-.08,0,.015}),far=target(1,{.15,0,.015});
    std::vector<PalmTarget> both{far,near};
    for(auto kind:{PalmKind::loot,PalmKind::usable,PalmKind::prop,PalmKind::bone,PalmKind::living}) {
        both[0].kind=kind;both[0].priority=.01;both[1].priority=100;
        for(double velocity:{-10.,0.,10.}) {
            p.velocity={velocity,0,0};
            assert(pick_by_palm(p,both,1,.25).index==1); // current/facing/kind/speed cannot beat distance
        }
    }
    // A newly closer object wins on this publication, even by one millimetre.
    PalmSelection selection;
    selection.update(p,{far},.25,1);assert(selection.current()==1);
    both={far,target(2,{.149,0,.015})};
    selection.update(p,both,.25,12);assert(selection.current()==2 && !selection.hysteresis_held());
    // Identical bounds are deterministic; exact ties retain the previous hover only.
    both={target(7,{.1,0,.015}),target(3,{.1,0,.015})};
    assert(pick_by_palm(p,both,0,.25).index==1);
    assert(pick_by_palm(p,both,7,.25).index==0);
    // Actual surface distance: neither a 3 cm loot pad nor a large object's centre changes the winner.
    auto coin=target(1,{.04,0,.015},PalmKind::loot,.8);
    PalmTarget surface{2,PalmKind::prop,{.02,-1,-1},{2,1,1},1,{}};
    assert(pick_by_palm(p,std::vector<PalmTarget>{coin,surface},1,.25).index==1);
    assert(std::abs(palm_cost(p,coin,.25,.2)->distance-.04)<1e-9);
    // 25 cm applies equally to new and previous hovers; a small hover sphere does not alter reach.
    for(std::uint64_t current:{0ull,1ull}) {
        assert(pick_by_palm(p,std::vector<PalmTarget>{target(1,{.25,0,.015})},current,.01).index);
        assert(!pick_by_palm(p,std::vector<PalmTarget>{target(1,{.2501,0,.015})},current,.5).index);
    }
    assert(hand_pickup_in_range(probe(),{.25,0,.015},{.25,0,.015}));
    assert(!hand_pickup_in_range(probe(),{.2501,0,.015},{.2501,0,.015}));
    assert(!hand_pickup_in_range(probe(),{NAN,0,0},{.1,0,0}));
    assert(!hand_pickup_in_range(probe(),{.1,0,0},{0,0,0}));
    // Translation and mirrored hand direction leave the nearest result unchanged.
    p=probe({3,4,5});p.direction={-1,0,0};
    both={target(1,{3.15,4,5.015}),target(2,{2.92,4,5.015})};
    assert(pick_by_palm(p,both,1,.25).index==1);
    // Invalid targets are excluded; an invalid probe never acquires a fresh target.
    both[1].minimum.x=NAN;
    assert(pick_by_palm(p,both,0,.25).index==0);
    p.valid=false;assert(!pick_by_palm(p,both,0,.25).index);
    assert(pick_by_palm(p,both,1,.25).fallback);
    std::puts("PASS: nearest actual surface wins across kind, direction, approach and previous hover; 25 cm boundary, ties and invalid inputs");
}
