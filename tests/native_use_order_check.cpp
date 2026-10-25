#include "../src/grab_force_cap.hpp"
#include "../src/selection_policy.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <cassert>
#include <iostream>
using namespace d2vr;
NativeUseHit hit(float x,unsigned id,float fraction=0) {
    NativeUseHit h{};std::memcpy(h.data(),&x,4);std::memcpy(h.data()+0x48,&id,4);std::memcpy(h.data()+0x20,&fraction,4);
    for(unsigned i=0x50;i<112;++i)h[i]=std::byte{static_cast<unsigned char>(id)};return h;
}
std::uintptr_t owner(const NativeUseHit& h) {unsigned id{};std::memcpy(&id,h.data()+0x48,4);return id;}
int main() {
    assert(native_use_query_outcome(0x123456789abcde01ull,123,2,2)==NativeUseQueryOutcome::multiple);
    assert(native_use_query_outcome(0x123456789abcde00ull,0,-1,0)==NativeUseQueryOutcome::incomplete);
    assert(native_use_query_outcome(0x123456789abcde01ull,0,1,1)==NativeUseQueryOutcome::rejected);
    assert(native_use_query_outcome(0,0,-1,0)==NativeUseQueryOutcome::incomplete);
    assert(native_use_query_outcome(1,0,0,0)==NativeUseQueryOutcome::empty);
    assert(native_use_query_outcome(1,123,1,1)==NativeUseQueryOutcome::single);
    assert(native_use_query_outcome(1,123,2,2)==NativeUseQueryOutcome::multiple);
    assert(native_use_query_outcome(1,0,1,1)==NativeUseQueryOutcome::rejected);
    assert(native_use_query_outcome(1,123,-1,2)==NativeUseQueryOutcome::rejected);
    assert(native_use_query_outcome(1,123,65,65)==NativeUseQueryOutcome::rejected);
    assert(native_use_query_outcome(1,123,2,1)==NativeUseQueryOutcome::rejected);
    assert(native_use_query_outcome(2,123,2,2)==NativeUseQueryOutcome::rejected);
    using Box=std::optional<std::pair<Vec3,Vec3>>;
    const auto no_bounds=[](std::uintptr_t)->Box {return std::nullopt;};
    const NoLoot no_loot{};
    const GrabTuning tuning{};
    // The legacy path's inputs: probe.palm is the hand point (volume_centre) and probe.forward the beam axis (zero: plain distance).
    const auto legacy=[](Vec3 hand,Vec3 axis=Vec3{}) {PalmProbe p;p.palm=hand;p.forward=axis;p.valid=true;return p;};
    const auto palm=[](Vec3 at,Vec3 forward,Vec3 direction) {PalmProbe p;p.palm=at;p.forward=forward;p.direction=direction;p.valid=true;return p;};
    const auto point=[](Vec3 p,unsigned id) {auto h=hit(0,id);const std::array<float,3> xyz{float(p.x),float(p.y),float(p.z)};std::memcpy(h.data(),xyz.data(),12);return h;};
    // Closest-point (bounds) distance from the palm point to a REAL box (before the 3 cm loot scoring pad): ruling H9's distances.
    const auto box_distance=[](const PalmProbe& p,Vec3 lo,Vec3 hi) {
        const Vec3 o=palm_point(p);return length(Vec3{std::clamp(o.x,lo.x,hi.x),std::clamp(o.y,lo.y,hi.y),std::clamp(o.z,lo.z,hi.z)}-o);};
    // Palm at the origin, fingers +Z, palm facing +X: palm point (0,0,.015).
    const auto facing_x=palm({0,0,0},{0,0,1},{1,0,0});
    std::array<NativeUseHit,3> records{};
    {   // Record integrity, ties and fractions in hand mode: no bounds -> each hit point is a zero-size box.
        const auto safe=hit(.40f,1),loot=hit(.10f,2),door=hit(.30f,3);
        records={safe,door,loot};
        assert(order_native_use_hits(records.data(),3,facing_x,0,0,owner,no_loot,no_bounds,tuning));
        assert(records[0]==loot && records[1]==door && records[2]==safe); // complete records unchanged
        records={hit(.4f,1,-.9f),hit(.1f,2,0),hit(.3f,3,-.5f)}; // native sweep fractions do not rank
        assert(order_native_use_hits(records.data(),3,facing_x,0,0,owner,no_loot,no_bounds,tuning) && owner(records[0])==2);
        assert(order_native_use_hits(records.data(),3,palm({.5,0,-.015},{0,0,1},{-1,0,0}),0,0,owner,no_loot,no_bounds,tuning) && owner(records[0])==1);
        records={hit(.2f,1),hit(.2f,2),hit(.2f,3)};const auto ties=records;
        assert(order_native_use_hits(records.data(),3,facing_x,0,0,owner,no_loot,no_bounds,tuning) && records==ties);
        records={hit(.10f,0),hit(.20f,1),hit(.30f,3)}; // an unresolved owner (0) is still scored, as a usable
        assert(order_native_use_hits(records.data(),3,facing_x,0,0,owner,no_loot,no_bounds,tuning) && owner(records[0])==0);
        std::array<NativeUseHit,3> same{hit(.35f,2),hit(.20f,1),hit(.10f,2)}; // every hit of the chosen item leads
        assert(order_native_use_hits(same.data(),3,facing_x,0,0,owner,no_loot,no_bounds,tuning));
        assert(owner(same[0])==2 && owner(same[1])==2 && owner(same[2])==1 && same[0]==hit(.10f,2));
    }
    // G1: palm cost replaces the forward beam.
    {   // palm_faced_coin_beats_nearer_shell: a palm-down hand 10 cm over a desk coin; a usable shell (no bounds) beside the palm is nearer.
        const auto down=palm({-.015,0,.10},{1,0,0},{0,0,-1});   // palm point (0,0,.10)
        const Vec3 coin_lo{-.025,-.025,0},coin_hi{.025,.025,.004};
        const auto coin=[](std::uintptr_t id){return id==2;};
        const auto bounds=[&](std::uintptr_t id)->Box {if(id==2) return std::pair{coin_lo,coin_hi};return std::nullopt;};
        const Vec3 shell{0,.05,.10};
        assert(length(shell-palm_point(down))<box_distance(down,coin_lo,coin_hi)); // the shell is nearer than the coin's real bounds
        const std::array<NativeUseHit,2> start{point(shell,1),point({0,0,.004},2)};
        auto r=start;
        assert(order_native_use_hits(r.data(),2,down,0,0,owner,coin,bounds,tuning) && owner(r[0])==2);
        GrabTuning flat=tuning;flat.priority_loot=1;r=start; // facing alone does it, not the loot priority
        assert(order_native_use_hits(r.data(),2,down,0,0,owner,coin,bounds,flat) && owner(r[0])==2);
        r=start; // the old forward beam (axis along the fingers) took the shell: the coin below the palm is off the 6 cm beam
        assert(order_native_use_hits(r.data(),2,legacy({-.015,0,.10},{1,0,0}),0,0,owner,coin,bounds,tuning,true) && owner(r[0])==1);
    }
    {   // coin_behind_knuckles_loses (anti-goal): the palm faces a bottle (a native carryable, so loot too) or a usable; a nearer coin is behind the hand.
        const auto loots=[](std::uintptr_t id){return id!=3;};
        const auto bounds=[](std::uintptr_t id)->Box {
            if(id==1) return std::pair{Vec3{.17,-.03,-.015},Vec3{.23,.03,.045}};        // bottle / usable in front, 17 cm
            if(id==2 || id==3) return std::pair{Vec3{-.102,-.025,-.01},Vec3{-.098,.025,.04}}; // coin behind the knuckles, 10 cm
            return std::nullopt;};
        records={point({-.10,0,.015},2),point({.17,0,.015},1),point({.4,.3,0},4)};
        assert(order_native_use_hits(records.data(),3,facing_x,0,0,owner,loots,bounds,tuning) && owner(records[0])==1 && owner(records[1])==2);
        const auto usable_in_front=[](std::uintptr_t id){return id==2;};   // only the coin is loot: its 0.8 priority still loses
        records={point({-.10,0,.015},2),point({.17,0,.015},1),point({.4,.3,0},4)};
        assert(order_native_use_hits(records.data(),3,facing_x,0,0,owner,usable_in_front,bounds,tuning) && owner(records[0])==1);
    }
    {   // loot_priority_replaces_bonus. Distances are BOUNDS distances (ruling H9): palm point to the closest point of each item's real box.
        // The palm faces down and toward +Y at 45 degrees, so the coin straight below and the safe face to the +Y side are faced equally.
        const double s=std::sqrt(.5);
        const auto p=palm({-.015,0,0},{1,0,0},{0,s,-s});   // palm point (0,0,0)
        const auto coin=[](std::uintptr_t id){return id==2;};
        const auto run=[&](Vec3 coin_lo,Vec3 coin_hi,Vec3 safe_lo,Vec3 safe_hi,const GrabTuning& t) {
            assert(std::fabs(dot(Vec3{0,0,-1},p.direction)-dot(Vec3{0,1,0},p.direction))<1e-12);
            const auto bounds=[&](std::uintptr_t id)->Box {if(id==2) return std::pair{coin_lo,coin_hi};if(id==1) return std::pair{safe_lo,safe_hi};return std::nullopt;};
            std::array<NativeUseHit,2> r{point({0,safe_lo.y,0},1),point({0,0,coin_hi.z},2)};
            assert(order_native_use_hits(r.data(),2,p,0,0,owner,coin,bounds,t));
            return owner(r[0]);
        };
        const Vec3 coin15_lo{-.025,-.025,-.154},coin15_hi{.025,.025,-.15},safe10_lo{-.3,.10,-.3},safe10_hi{.3,.6,.3};
        assert(std::fabs(box_distance(p,coin15_lo,coin15_hi)-.15)<1e-9 && std::fabs(box_distance(p,safe10_lo,safe10_hi)-.10)<1e-9);
        assert(run(coin15_lo,coin15_hi,safe10_lo,safe10_hi,tuning)==2);   // a coin 0.15 m away beats a safe face 0.10 m away
        GrabTuning flat=tuning;flat.priority_loot=1;
        assert(run(coin15_lo,coin15_hi,safe10_lo,safe10_hi,flat)==1);     // ... because of the 0.8 priority (no hidden distance bonus)
        const Vec3 coin60_lo{-.025,-.025,-.604},coin60_hi{.025,.025,-.6},safe05_lo{-.3,.05,-.3},safe05_hi{.3,.6,.3};
        assert(std::fabs(box_distance(p,coin60_lo,coin60_hi)-.60)<1e-9 && std::fabs(box_distance(p,safe05_lo,safe05_hi)-.05)<1e-9);
        assert(run(coin60_lo,coin60_hi,safe05_lo,safe05_hi,tuning)==1);   // a coin 0.6 m away does not beat a safe 0.05 m away
    }
    {   // other_hand_pick_ranked_last: the other hand's item sorts after everything, even after hits beyond the hover gate.
        const auto coin=[](std::uintptr_t id){return id==2;};
        const std::array<NativeUseHit,3> start{point({.50,0,.015},3),point({.30,0,.015},1),point({.10,0,.015},2)};
        auto r=start;
        assert(order_native_use_hits(r.data(),3,facing_x,0,0,owner,coin,no_bounds,tuning));
        assert(owner(r[0])==2 && owner(r[1])==1 && owner(r[2])==3); // beyond the 0.45 m gate (owner 3): after every in-gate hit
        r=start;
        assert(order_native_use_hits(r.data(),3,facing_x,0,2,owner,coin,no_bounds,tuning));
        assert(owner(r[0])==1 && owner(r[1])==3 && owner(r[2])==2);
        r=start; // the other hand's pick loses even when this hand had it before
        assert(order_native_use_hits(r.data(),3,facing_x,2,2,owner,coin,no_bounds,tuning) && owner(r[2])==2);
    }
    {   // preferred_keeps_with_086: Task 2's one hysteresis (pick_by_palm), no second bonus.
        const auto loots=[](std::uintptr_t){return true;};
        const auto at=[&](Vec3 c){return std::pair{c-Vec3{.03,.03,.03},c+Vec3{.03,.03,.03}};};
        const auto cost=[&](std::pair<Vec3,Vec3> b) {
            PalmTarget t;t.id=9;t.kind=PalmKind::loot;t.minimum=b.first;t.maximum=b.second;t.priority=tuning.priority_loot;
            return palm_cost(facing_x,t,tuning.hover_radius_m,grab_acquire_distance)->cost;};
        auto a=at({.23,.06,.015}),b=at({.22,-.06,.015});
        assert(cost(b)<cost(a) && cost(b)>palm_hysteresis*cost(a)); // B is cheaper, but inside the 14 % band
        const auto run=[&](std::uintptr_t preferred) {
            const auto bounds=[&](std::uintptr_t id)->Box {if(id==1) return a;if(id==2) return b;return std::nullopt;};
            std::array<NativeUseHit,3> r{point(a.first,1),point(b.first,2),point({.4,.3,0},3)};
            assert(order_native_use_hits(r.data(),3,facing_x,preferred,0,owner,loots,bounds,tuning));
            return owner(r[0]);
        };
        assert(run(0)==2);   // nothing held: the cheaper wins
        assert(run(1)==1);   // A held: x 0.86 keeps it
        assert(run(2)==2);
        b=at({.12,-.06,.015});
        assert(cost(b)<palm_hysteresis*cost(a));
        assert(run(1)==2);   // a clear move toward B flips it
        // The held pick stays eligible out to the 0.55 m retain gate; a new one needs 0.45 m.
        // A (usable) is faced at 0.515 m; B (usable) is 0.40 m away behind the hand.
        a={Vec3{.515,-.03,-.015},Vec3{.575,.03,.045}};b={Vec3{-.46,-.03,-.015},Vec3{-.40,.03,.045}};
        const auto far=[&](std::uintptr_t preferred) {
            const auto bounds=[&](std::uintptr_t id)->Box {if(id==1) return a;if(id==2) return b;return std::nullopt;};
            std::array<NativeUseHit,2> r{point(b.first,2),point(a.first,1)};
            assert(order_native_use_hits(r.data(),2,facing_x,preferred,0,owner,no_loot,bounds,tuning));
            return owner(r[0]);
        };
        assert(box_distance(facing_x,a.first,a.second)>grab_acquire_distance && box_distance(facing_x,a.first,a.second)<grab_retain_distance);
        assert(far(0)==2 && far(1)==1);
    }
    {   // A non-finite palm keeps the old pick: the preferred owner's hits lead, the rest keep the native order.
        PalmProbe bad=facing_x;bad.direction={NAN,0,0};bad.valid=false;
        records={hit(.1f,1),hit(.2f,2),hit(.3f,3)};
        assert(order_native_use_hits(records.data(),3,bad,3,0,owner,no_loot,no_bounds,tuning));
        assert(owner(records[0])==3 && owner(records[1])==1 && owner(records[2])==2);
    }
    {   // legacy_beam_knob_keeps_old_order (build/legacy-palm-select, and gaze/reach mode): the v610 forward beam, 2 cm preference and 6 cm loot bonus.
        records={hit(.11f,1),hit(.10f,2),hit(.3f,3)};
        assert(order_native_use_hits(records.data(),3,legacy({0,0,0}),1,0,owner,no_loot,no_bounds,tuning,true) && owner(records[0])==1);
        records={hit(.14f,1),hit(.10f,2),hit(.3f,3)};
        assert(order_native_use_hits(records.data(),3,legacy({0,0,0}),1,0,owner,no_loot,no_bounds,tuning,true) && owner(records[0])==2);
        records={hit(.4f,1,-.9f),hit(.1f,2,0),hit(.3f,3,-.5f)};
        assert(order_native_use_hits(records.data(),3,legacy({.5,0,0}),0,0,owner,no_loot,no_bounds,tuning,true) && owner(records[0])==1);
        const auto at=[](float x,float y,unsigned id){auto h=hit(x,id);std::memcpy(h.data()+4,&y,4);return h;};
        records={at(.05f,.12f,1),at(.40f,.01f,2),at(.20f,.30f,3)};
        assert(order_native_use_hits(records.data(),3,legacy({0,0,0},{2,0,0}),0,0,owner,no_loot,no_bounds,tuning,true) && owner(records[0])==2 && owner(records[1])==1);
        records={at(.40f,.0f,1),at(.10f,.02f,2),at(.20f,.30f,3)}; // both on the beam: the nearer along it wins
        assert(order_native_use_hits(records.data(),3,legacy({0,0,0},{1,0,0}),0,0,owner,no_loot,no_bounds,tuning,true) && owner(records[0])==2);
        const auto coin=[](std::uintptr_t id){return id==2;};
        records={at(.10f,.0f,1),at(.15f,.02f,2),at(.20f,.30f,3)};
        assert(order_native_use_hits(records.data(),3,legacy({0,0,0},{1,0,0}),0,0,owner,coin,no_bounds,tuning,true) && owner(records[0])==2);
        records={at(.05f,.0f,1),at(.60f,.05f,2),at(.20f,.30f,3)};
        assert(order_native_use_hits(records.data(),3,legacy({0,0,0},{1,0,0}),0,0,owner,coin,no_bounds,tuning,true) && owner(records[0])==1);
        // the other hand's owner is not part of the old path
        records={hit(.10f,1),hit(.20f,2),hit(.3f,3)};
        assert(order_native_use_hits(records.data(),3,legacy({0,0,0}),0,1,owner,no_loot,no_bounds,tuning,true) && owner(records[0])==1);
        records={hit(.2f,1),hit(NAN,2),hit(.1f,3)};const auto invalid=records;
        assert(!order_native_use_hits(records.data(),3,legacy({0,0,0}),0,0,owner,no_loot,no_bounds,tuning,true) && records==invalid);
    }
    records={hit(.2f,1),hit(NAN,2),hit(.1f,3)};const auto invalid=records;
    assert(!order_native_use_hits(records.data(),3,facing_x,0,0,owner,no_loot,no_bounds,tuning) && records==invalid);
    assert(!order_native_use_hits(records.data(),65,facing_x,0,0,owner,no_loot,no_bounds,tuning));
    assert(continue_native_use_scan(1,0,true,false));
    assert(!continue_native_use_scan(0,0,true,false)); // Clear LOS unchanged.
    assert(!continue_native_use_scan(1,1,true,false)); // Native already continues.
    assert(!continue_native_use_scan(1,0,false,false)); // Unowned scan unchanged.
    assert(!continue_native_use_scan(1,0,true,true)); // NPC blocked metadata unchanged.
    assert(!continue_native_use_scan(1,0,true,{})); // Unknown type unchanged.
    // Match verified native stage0's break/accept branches. A hand-near
    // blocked loot must remain rejected while a farther visible safe/front
    // item gets its own native permission and LOS evaluation.
    struct Candidate {unsigned id;bool eligible;std::uintptr_t blocked;};
    const std::array<Candidate,3> closed{{{2,true,1},{3,false,0},{1,true,0}}};
    const auto native_scan=[](const auto& candidates,bool ranked) {
        unsigned checks=0,selected=0;
        for(const auto& c:candidates) {
            if(!c.eligible)continue;++checks;unsigned char flag=0;
            const auto native_result=c.blocked;
            if(continue_native_use_scan(native_result,flag,ranked,false))flag=1;
            if(!(native_result&0xff)){selected=c.id;break;}
            if(!flag)break;
        }
        return std::array<unsigned,2>{selected,checks};
    };
    assert((native_scan(closed,false)==std::array<unsigned,2>{0,1}));
    assert((native_scan(closed,true)==std::array<unsigned,2>{1,2}));
    const std::array<Candidate,2> open{{{2,true,0},{1,true,0}}};
    assert((native_scan(open,true)==std::array<unsigned,2>{2,1}));
    const std::array<Candidate,2> all_blocked{{{2,true,1},{1,true,1}}};
    assert((native_scan(all_blocked,true)==std::array<unsigned,2>{0,2}));
    std::cout<<"PASS: palm cost orders native Use hits (palm-faced coin, knuckles, loot priority, other hand last, one 0.86 hysteresis), legacy beam knob, stable ties, record integrity and invalid-buffer rejection\n";
}
