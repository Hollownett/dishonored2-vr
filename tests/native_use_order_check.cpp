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
bool same(Vec3 a,Vec3 b) {return a.x==b.x && a.y==b.y && a.z==b.z;}
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
    [[maybe_unused]] const auto point=[](Vec3 p,unsigned id) {auto h=hit(0,id);const std::array<float,3> xyz{float(p.x),float(p.y),float(p.z)};std::memcpy(h.data(),xyz.data(),12);return h;};
    // Closest-point (bounds) distance from the palm point to a REAL box (before the 3 cm loot scoring pad): ruling H9's distances.
    [[maybe_unused]] const auto box_distance=[](const PalmProbe& p,Vec3 lo,Vec3 hi) {
        const Vec3 o=palm_point(p);return length(Vec3{std::clamp(o.x,lo.x,hi.x),std::clamp(o.y,lo.y,hi.y),std::clamp(o.z,lo.z,hi.z)}-o);};
    // Palm at the origin, fingers +Z, palm facing +X: palm point (0,0,.015).
    const auto facing_x=palm({0,0,0},{0,0,1},{1,0,0});
    std::array<NativeUseHit,3> records{};
    {   // Nearest actual owner bounds lead; native hit bytes and fractions stay intact.
        const auto a=hit(.15f,1,-.9f),b=hit(.08f,2,.8f),c=hit(.19f,3,-.5f);
        records={a,c,b};
        assert(order_native_use_hits(records.data(),3,facing_x,1,0,owner,no_loot,no_bounds,tuning));
        assert(records[0]==b && records[1]==a && records[2]==c);
        // The previous owner, palm facing and loot priority cannot beat a closer owner.
        const auto bounds=[](std::uintptr_t id)->Box {
            const double x=id==1?.15:-.08;return std::pair{Vec3{x,0,.015},Vec3{x,0,.015}};};
        const auto loot=[](std::uintptr_t id) {return id==1;};
        std::array<NativeUseHit,2> r{a,b};
        assert(order_native_use_hits(r.data(),2,facing_x,1,0,owner,loot,bounds,tuning) && owner(r[0])==2);
        // The other hand's hover reserves nothing; nearest still wins.
        r={a,b};assert(order_native_use_hits(r.data(),2,facing_x,0,2,owner,loot,bounds,tuning) && owner(r[0])==2);
        // An actual table surface 2 cm away beats a coin 4 cm away (no scoring pad).
        const auto surfaces=[](std::uintptr_t id)->Box {
            if(id==1)return std::pair{Vec3{.04,0,.015},Vec3{.04,0,.015}};
            return std::pair{Vec3{.02,-1,-1},Vec3{2,1,1}};};
        r={a,b};assert(order_native_use_hits(r.data(),2,facing_x,1,0,owner,loot,surfaces,tuning) && owner(r[0])==2);
    }
    {   // A non-finite palm keeps the old pick: the preferred owner's hits lead, the rest keep the native order.
        PalmProbe bad=facing_x;bad.direction={NAN,0,0};bad.valid=false;
        records={hit(.1f,1),hit(.2f,2),hit(.3f,3)};
        assert(order_native_use_hits(records.data(),3,bad,3,0,owner,no_loot,no_bounds,tuning));
        assert(owner(records[0])==3 && owner(records[1])==1 && owner(records[2])==2);
    }
    {   // invalid_palm_falls_back_to_beam (fix round 1): a hand-mode cycle without a valid palm probe (no raw grip pose, palm_probe threw or got
        // a non-finite input) is ordered by the v610 beam from the volume centre along the aim axis, never by the native sweep-fraction order.
        const auto at=[](float x,float y,unsigned id){auto h=hit(x,id);std::memcpy(h.data()+4,&y,4);return h;};
        const std::array<NativeUseHit,3> start{at(.05f,.12f,1),at(.40f,.01f,2),at(.20f,.30f,3)};
        const Vec3 centre{0,0,0},aim{2,0,0};
        WorldTrackedPose nan_grip{};nan_grip.position={NAN,0,0};nan_grip.basis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};
        for(const PalmProbe& bad:{PalmProbe{},palm_probe(1,nan_grip,default_palm_offset(1),std::nullopt,{})}) {
            assert(!bad.valid);
            const auto use=native_use_order_probe(bad,false,false,centre,aim);
            assert(use.legacy && use.beam_fallback && same(use.probe.palm,centre) && same(use.probe.forward,aim));
            auto r=start;
            assert(order_native_use_hits(r.data(),3,use.probe,0,0,owner,no_loot,no_bounds,tuning,use.legacy) && owner(r[0])==2 && owner(r[1])==1);
            r=start; // what the hand-mode path alone would do with that probe: the native order (the reason for the fallback)
            assert(order_native_use_hits(r.data(),3,bad,0,0,owner,no_loot,no_bounds,tuning) && r==start);
        }
        const auto good=native_use_order_probe(facing_x,false,false,centre,aim);
        assert(!good.legacy && !good.beam_fallback && same(good.probe.palm,facing_x.palm) && same(good.probe.direction,facing_x.direction));
        for(const auto& [knob,reach]:{std::pair{true,false},std::pair{false,true},std::pair{true,true}}) {
            const auto use=native_use_order_probe(facing_x,knob,reach,centre,aim);   // the knob and gaze/reach: the beam, not a fallback
            assert(use.legacy && !use.beam_fallback && same(use.probe.palm,centre) && same(use.probe.forward,aim));
        }
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
    std::cout<<"PASS: nearest hand distance orders native Use hits (actual bounds, no preference bonus, other hand last), legacy beam knob, stable ties, record integrity and invalid-buffer rejection\n";
}
