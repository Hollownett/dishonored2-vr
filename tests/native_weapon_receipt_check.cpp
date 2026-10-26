#define NOMINMAX
#include "../src/native_interaction_aim.hpp"
#include "../src/hand_clamp.hpp"
#include "../src/hand_body_probe_clearance.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace d2vr;using Aim=NativeInteractionAim;
    HandClamp clamp;ClampInput in{};in.now=1;in.epoch=1;in.root=level_frame({0,0,0},{1,0,0});
    in.target=in.root;in.target.position={.8,0,.5};in.chest={0,0,.5};in.strict_answers=true;in.tip_enabled=false;
    const auto post=[&](const ClampOutput& o) {
        assert(o.palm_sweep.valid && o.request_ids[0]);
        Aim::post_weapon_segment(0,0,o.palm_sweep.from,o.palm_sweep.to,o.palm_sweep.radius,0,-1,0,Aim::producer_clamp,o.request_ids[0]);
    };
    auto a=clamp.update(0,in);clamp.note_drawn(0,in.root,a.drawn.position);post(a);
    auto taken_a=Aim::take_weapon_requests();assert(taken_a[0][0].request_id==a.request_ids[0]);
    // Game has taken A; render publishes B before A completes.
    in.now+=.03;in.root.position.x=.21;in.target.position.x=1.01;
    auto b=clamp.update(0,in);clamp.note_drawn(0,in.root,b.drawn.position);post(b);
    assert(b.held && !b.validated && b.drawn.position.x<.95);
    const auto taken_b=Aim::take_weapon_requests();assert(taken_b[0][0].request_id==b.request_ids[0]);
    std::array<std::array<std::optional<WorldTraceHit>,Aim::slot_count>,2> hits{};
    Aim::record_weapon_answers(taken_a,hits,1000);
    const auto answer_a=Aim::weapon_answer(0,0,1001);assert(answer_a && !answer_a->hit);
    assert(answer_a->request_id==a.request_ids[0] && answer_a->segment.to.x==taken_a[0][0].to.x);
    assert(answer_a->request_id!=b.request_ids[0]);
    in.now+=.01;in.answer_ids[0]=answer_a->request_id;
    auto c=clamp.update(0,in);clamp.note_drawn(0,in.root,c.drawn.position);
    assert(c.chest_answered && !c.validated && c.held && c.drawn.position.x<.95);
    // B actually hit the wall. Its original geometry and hit arrive together.
    WorldTraceHit wall{};wall.point={1,0,.5};wall.normal={-1,0,0};wall.normal_valid=true;
    hits[0][0]=wall;Aim::record_weapon_answers(taken_b,hits,1010);
    const auto answer_b=Aim::weapon_answer(0,0,1011);assert(answer_b && answer_b->hit && answer_b->request_id==b.request_ids[0]);
    assert(answer_b->segment.from.x==taken_b[0][0].from.x && answer_b->tick==1010);
    in.now+=.01;in.answer_ids[0]=answer_b->request_id;in.palm_hit=to_clamp_hit(*answer_b->hit);
    const auto d=clamp.update(0,in);clamp.note_drawn(0,in.root,d.drawn.position);
    assert(d.palm_answered && d.drawn.position.x<=.9450001);
    assert(!Aim::weapon_answer(0,0,1011,Aim::producer_held));
    assert(!Aim::weapon_answer(0,0,1009));assert(!Aim::weapon_answer(0,0,1111));
    assert(!Aim::weapon_answer(2,0,1011));assert(!Aim::weapon_answer(0,Aim::slot_count,1011));
    // Taking without a completion never fabricates a miss or replaces the last receipt.
    const auto empty=Aim::take_weapon_requests();Aim::record_weapon_answers(empty,{},1020);
    const auto unchanged=Aim::weapon_answer(0,0,1021);assert(unchanged && unchanged->request_id==b.request_ids[0] && unchanged->tick==1010);
    { // The creation probe counts actual completions, never render frames, replay or a different palm.
        HandBodyProbeClearance proof;const Vec3 palm{.2,.3,.4};
        assert(!proof.observe(1,1000,palm,0,.05,10,{}));
        auto completion=[&](std::uint64_t id,std::uint64_t tick) {return HandBodyProbeAnswer{id,tick,palm+Vec3{0,0,.01},palm,.05,0,false};};
        assert(!proof.observe(1,1001,palm,0,.05,11,completion(11,1000)));
        for(unsigned t=1002;t<1010;++t)assert(!proof.observe(1,t,palm,0,.05,11,completion(11,1000)));
        assert(!proof.observe(1,1011,palm,0,.05,12,completion(12,1010)));
        assert(proof.observe(1,1021,palm,0,.05,13,completion(13,1020)));
        assert(proof.observe(1,1030,palm,0,.05,13,{})); // short pending interval retains, never renews proof
        assert(!proof.observe(1,1121,palm,0,.05,13,completion(13,1020)));
        assert(!proof.observe(2,1122,palm,0,.05,13,completion(13,1020))); // new epoch retires old in-flight IDs
        assert(!proof.observe(2,1123,palm,0,.05,14,completion(14,1123)));
        auto wrong=completion(15,1124);wrong.exclude=99;
        assert(!proof.observe(2,1124,palm,0,.05,15,wrong));
        assert(!proof.observe(2,1125,palm,0,.05,16,completion(16,1125)));
        assert(!proof.observe(2,1126,palm+Vec3{.01,0,0},0,.05,17,completion(17,1126)));
        auto blocked=completion(18,1127);blocked.blocked=true;
        assert(!proof.observe(2,1127,palm,0,.05,18,blocked));
        proof.reset(18);assert(!proof.observe(2,1128,palm,0,.05,18,completion(18,1127)));
    }
    std::cout<<"PASS: native in-flight A/B completion preserves exact geometry/identity and cannot validate a newer hand through a wall\n";
}
