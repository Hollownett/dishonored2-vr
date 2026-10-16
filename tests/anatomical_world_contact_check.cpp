#include "../src/anatomical_world_contact.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#if defined(_WIN32)
#include <windows.h>
#endif
using namespace d2vr;
namespace {
AnatomicalContactOwner owner() {return {1,2,3,4,5,6,{7,10},{8,10}};}
AnatomicalContactRequest request(unsigned side=0,std::uint64_t epoch=1) {
    AnatomicalContactRequest value{};value.token={owner(),1,1,1,epoch,side};value.tick=1000;value.safe_from=true;
    for(unsigned proxy=0;proxy<hand_proxy_count;++proxy) {
        value.from[proxy]={{-.1,static_cast<double>(proxy),0},.02};
        value.to[proxy]={{.01,static_cast<double>(proxy),0},.02};
    }
    return value;
}
WorldTraceHit hit(Vec3 point,Vec3 normal,std::uintptr_t entity=50) {
    WorldTraceHit value{};value.point=point;value.normal=normal;value.normal_valid=true;value.fraction=.5;value.entity=entity;return value;
}
WorldTraceQueryResult checked(std::optional<WorldTraceHit> value) {return {true,std::move(value)};}
#if defined(_WIN32)
struct SehBatchContext {AnatomicalWorldContact* mailbox;const AnatomicalContactOwner* owner;std::uint64_t now;};
unsigned invoke_seh_batch(SehBatchContext* context) {
    return context->mailbox->service_batch(*context->owner,context->now,
        [](Vec3,Vec3,double)->WorldTraceQueryResult {RaiseException(0xe0424242,0,0,nullptr);return {true,{}};},[]{return true;});
}
bool catch_seh_batch(SehBatchContext* context) {
    __try {invoke_seh_batch(context);return false;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return true;}
}
#endif
rig::HandGeometry geometry() {
    rig::HandGeometry value{};value.palm_in={0,0,-1};
    for(unsigned finger=0;finger<5;++finger) {
        value.finger[finger].pivot={.04-.02*finger,finger?.08:.03,0};
        value.finger[finger].bone={Vec3{0,.03,0},Vec3{0,.02,0},Vec3{0,.02,0}};
        value.finger[finger].hinge={Vec3{-1,0,0},Vec3{-1,0,0},Vec3{-1,0,0}};
        value.thickness[finger]=.008;
    }
    return value;
}
}
int main() {
    const auto hand=geometry();WorldTrackedPose palm{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
    for(const float curl:{0.f,.5f,1.f}) {
        HandJointTurns turns{};
        for(unsigned finger=0;finger<5;++finger)turns[finger]=rig::finger_turn_at(hand.finger[finger],finger,curl);
        const auto solved=hand_proxies(hand,palm,turns),legacy=hand_proxies(hand,palm,std::array<float,5>{curl,curl,curl,curl,curl});
        for(unsigned proxy=0;proxy<hand_proxy_count;++proxy)assert(length(solved[proxy].centre-legacy[proxy].centre)<1e-12 && solved[proxy].radius==legacy[proxy].radius);
    }
    HandJointTurns asymmetric{};asymmetric[2]={.1,.7,.2};
    const auto posed=hand_proxies(hand,palm,asymmetric);const auto points=rig::finger_points(hand.finger[2],asymmetric[2]);
    assert(length(posed[hand_palm_spheres+2*4+2].centre-points[2])<1e-12);
    auto source=request();
    std::array<std::optional<WorldTraceHit>,hand_proxy_count> hits{};
    hits[0]=hit({0,0,0},{-1,0,0});
    auto result=anatomical_contact_reduce(source,hits);
    assert(result.actionable() && std::abs(result.depth-.03)<1e-12 && std::abs(result.shift.x+.03)<1e-12 && result.proxy==0);
    source.to[1].centre.x=1;
    result=anatomical_contact_reduce(source,hits);
    assert(result.actionable() && std::abs(result.shift.x+.03)<1e-12);
    hits[1]=hit({0,1,0},{-1,0,0});
    result=anatomical_contact_reduce(source,hits);assert(result.state==AnatomicalContactState::limited && !result.actionable() && length(result.shift)<=.45);
    source=request();hits={};hits[0]=hit({0,0,0},{-1,0,0});hits[1]=hit({.01,1,0},{0,-1,0});
    result=anatomical_contact_reduce(source,hits);assert(result.actionable() && std::abs(result.shift.x+.03)<1e-12 && std::abs(result.shift.y+.02)<1e-12);
    hits[1]->normal={1,0,0};hits[1]->point={.05,1,0};
    assert(anatomical_contact_reduce(source,hits).state==AnatomicalContactState::limited);
    hits[1].reset();hits[0]->normal_valid=false;assert(!anatomical_contact_reduce(source,hits).actionable());
    hits[0]=hit({0,0,0},{-1,0,0});hits[0]->initial_overlap=true;assert(!anatomical_contact_reduce(source,hits).actionable());
    hits[0]=hit({0,0,0},{-1,0,0});source.held_entity=50;
    assert(anatomical_contact_reduce(source,hits).state==AnatomicalContactState::occluded);
    source.held_entity=0;hits[0]->point.x=std::numeric_limits<double>::quiet_NaN();assert(!anatomical_contact_reduce(source,hits).actionable());
    AnatomicalWorldContact mailbox;mailbox.observe(0,owner());mailbox.observe(1,owner());
    auto first=request(),latest=request(0,2),other=request(1);
    auto other_owner=owner();other_owner.camera=40;other_owner.sensor=50;other_owner.sensor_identity.generation=80;
    mailbox.observe(1,other_owner);other.token.owner=other_owner;
    assert(mailbox.post(first) && mailbox.post(latest) && mailbox.post(other) && !mailbox.post(first));
    unsigned calls{};
    const auto finite_leaf=[&](Vec3 from,Vec3 to,double radius)->std::optional<WorldTraceHit> {
        ++calls;assert(radius==.02 && from.x<0 && to.x>0);
        if(std::abs(to.y)>.2)return {};
        return hit({0,to.y,to.z},{-1,0,0});
    };
    assert(mailbox.service(0,owner(),1001,finite_leaf,[]{return true;})==24);
    assert(!mailbox.result(other.token,1001));
    assert(mailbox.service(1,other_owner,1001,finite_leaf,[]{return true;})==24 && calls==48);
    assert(!mailbox.result(first.token,1001));
    result=*mailbox.result(latest.token,1001);assert(result.actionable() && result.casts==24 && result.hits[0] && !result.hits[1]);
    assert(mailbox.result(other.token,1001)->actionable());
    mailbox.observe(0,owner());assert(mailbox.result(other.token,1001) && mailbox.owner(1)==other_owner);
    assert(mailbox.service(0,owner(),1002,finite_leaf,[]{return true;})==0 && !mailbox.post(latest));
    assert(!mailbox.result(latest.token,1102));
    auto stationary=request(0,3);stationary.from=stationary.to;
    assert(mailbox.post(stationary));assert(mailbox.service(0,owner(),1003,finite_leaf,[]{return true;})==0);
    assert(mailbox.result(stationary.token,1003)->state==AnatomicalContactState::acquisition_required);
    auto reset=request(0,4);reset.safe_from=false;
    assert(mailbox.post(reset));assert(mailbox.service(0,owner(),1004,finite_leaf,[]{return true;})==0);
    assert(!mailbox.result(reset.token,1004)->actionable());
    auto jump=request(0,5);jump.to[0].centre.x=1;
    assert(mailbox.post(jump));assert(mailbox.service(0,owner(),1005,finite_leaf,[]{return true;})==0);
    assert(mailbox.result(jump.token,1005)->state==AnatomicalContactState::acquisition_required);
    auto stale=request(0,6);assert(mailbox.post(stale));assert(mailbox.service(0,owner(),1200,finite_leaf,[]{return true;})==0);
    assert(!mailbox.result(stale.token,1200));
    auto held=request(0,7);held.held_entity=50;held.tick=1200;assert(mailbox.post(held));
    assert(mailbox.service(0,owner(),1201,finite_leaf,[]{return true;})==24);
    assert(mailbox.result(held.token,1201)->state==AnatomicalContactState::occluded);
    auto changed=request(0,8);changed.token.ownership_epoch=2;changed.tick=1201;
    assert(mailbox.post(changed) && !mailbox.result(held.token,1201) && !mailbox.post(held));
    auto replacement=changed;replacement.token.request_revision=9;
    bool replaced{};
    assert(mailbox.service(0,owner(),1202,[&](Vec3 from,Vec3 to,double radius) {
        if(!replaced) {replaced=true;assert(mailbox.post(replacement));}
        return finite_leaf(from,to,radius);
    },[]{return true;})==1);
    assert(!mailbox.result(changed.token,1202));
    assert(mailbox.service(0,owner(),1203,finite_leaf,[]{return true;})==24 && mailbox.result(replacement.token,1203));
    auto invalidated=replacement;invalidated.token.request_revision=10;assert(mailbox.post(invalidated));
    unsigned validations{};assert(mailbox.service(0,owner(),1204,finite_leaf,[&]{return ++validations<4;})==2);
    assert(!mailbox.owner(0).valid() && !mailbox.result(invalidated.token,1204));
    mailbox.observe(0,owner());mailbox.observe(1,owner());assert(mailbox.post(first));auto recycled=owner();++recycled.spawn;
    assert(mailbox.service(0,recycled,1001,finite_leaf,[]{return true;})==0 && !mailbox.result(first.token,1001) && !mailbox.post(first));
    mailbox.clear();assert(!mailbox.owner(0).valid());
    mailbox.observe(0,owner());mailbox.observe(1,owner());auto acquisition=request();acquisition.safe_from=false;
    for(unsigned proxy=0;proxy<hand_proxy_count;++proxy) {
        acquisition.acquire[proxy]=true;acquisition.from[proxy].centre=acquisition.to[proxy].centre+Vec3{-.002,0,0};
    }
    assert(mailbox.post(acquisition));
    assert(mailbox.service(0,owner(),1001,[](Vec3,Vec3,double)->std::optional<WorldTraceHit>{return {};},[]{return true;})==24);
    assert(mailbox.result(acquisition.token,1001)->state==AnatomicalContactState::acquisition_required && !mailbox.result(acquisition.token,1001)->actionable());
    ++acquisition.token.request_revision;assert(mailbox.post(acquisition));
    assert(mailbox.service(0,owner(),1002,[](Vec3,Vec3,double)->std::optional<WorldTraceHit>{return hit({0,0,0},{-1,0,0});},[]{return true;})==24);
    assert(mailbox.result(acquisition.token,1002)->state==AnatomicalContactState::acquisition_required && !mailbox.result(acquisition.token,1002)->actionable());
    ++acquisition.token.request_revision;acquisition.held_entity=50;assert(mailbox.post(acquisition));
    assert(mailbox.service(0,owner(),1003,[](Vec3,Vec3,double)->std::optional<WorldTraceHit>{return hit({0,0,0},{-1,0,0});},[]{return true;})==24);
    assert(mailbox.result(acquisition.token,1003)->state==AnatomicalContactState::occluded);
    mailbox.clear();mailbox.observe(0,owner());mailbox.observe(1,owner());auto safe_anchor=request();safe_anchor.safe_anchor_acquisition=true;
    for(auto& sphere:safe_anchor.from)sphere.centre.x=-.8;
    assert(mailbox.post(safe_anchor));assert(mailbox.service(0,owner(),1001,finite_leaf,[]{return true;})==24);
    assert(mailbox.result(safe_anchor.token,1001)->actionable());
    auto resting=safe_anchor;resting.token.request_revision=2;resting.from=resting.to;
    assert(mailbox.post(resting));
    assert(mailbox.service(0,owner(),1002,finite_leaf,[]{return true;},[](const AnatomicalContactResult& previous){return previous.entity==50;})==0);
    const auto retained=mailbox.result(resting.token,1002);
    assert(retained && retained->state==AnatomicalContactState::retained && retained->contact && !retained->actionable() &&
        retained->query_tick==1001 && retained->tick==1002 && retained->casts==0 && length(retained->shift-Vec3{-.03,0,0})<1e-12);
    ++resting.token.request_revision;assert(mailbox.post(resting));
    assert(mailbox.service(0,owner(),1003,finite_leaf,[]{return true;},[](const AnatomicalContactResult&){return false;})==0);
    assert(mailbox.result(resting.token,1003)->state==AnatomicalContactState::acquisition_required);
    auto reference=resting;reference.token.pose_epoch=2;reference.token.request_revision=1;reference.from=request().from;
    assert(mailbox.post(reference) && !mailbox.result(resting.token,1003) && !mailbox.post(resting));
    assert(mailbox.service(0,owner(),1004,finite_leaf,[]{return true;})==24 && mailbox.result(reference.token,1004));
    mailbox.clear();mailbox.observe(0,owner());mailbox.observe(1,owner());
    const auto timed_request=[](unsigned side,std::uint64_t revision,std::uint64_t tick) {auto value=request(side,revision);value.tick=tick;return value;};
    auto left=timed_request(0,1,1300),right=timed_request(1,1,1300);right.from[0].centre.x=-.2;
    for(unsigned proxy=1;proxy<hand_proxy_count;++proxy)right.from[proxy].centre.x=-.2;
    assert(mailbox.post(left) && mailbox.post(right));unsigned batch_calls{};double first_cast_x{};
    const auto batch_cast=[&](Vec3 from,Vec3 to,double radius)->std::optional<WorldTraceHit> {
        if(!batch_calls)first_cast_x=from.x;++batch_calls;return finite_leaf(from,to,radius);
    };
    const auto checked_batch_cast=[&](Vec3 from,Vec3 to,double radius)->WorldTraceQueryResult {return checked(batch_cast(from,to,radius));};
    assert(mailbox.service_batch(owner(),1300,checked_batch_cast,[]{return true;})==48 && batch_calls==48);
    assert(first_cast_x==-.1 && mailbox.result(left.token,1300)->actionable() && mailbox.result(right.token,1300)->actionable());
    auto left2=timed_request(0,2,1301),right2=timed_request(1,2,1301);right2.from=right.from;assert(mailbox.post(left2) && mailbox.post(right2));
    batch_calls=0;assert(mailbox.service_batch(owner(),1301,checked_batch_cast,[]{return true;})==48 && first_cast_x==-.2);
    auto left3=timed_request(0,3,1302),right3=timed_request(1,3,1302);right3.from=right.from;assert(mailbox.post(left3) && mailbox.post(right3));
    batch_calls=0;bool reposted{};
    const auto repost_cast=[&](Vec3 from,Vec3 to,double radius)->WorldTraceQueryResult {
        ++batch_calls;
        if(batch_calls==24 && !reposted) {reposted=true;auto newer=timed_request(1,4,1302);newer.from=right.from;assert(mailbox.post(newer));}
        return checked(finite_leaf(from,to,radius));
    };
    assert(mailbox.service_batch(owner(),1302,repost_cast,[]{return true;})==24);
    assert(!mailbox.result(left3.token,1302) && !mailbox.result(right3.token,1302));
    auto right4=timed_request(1,4,1303);right4.from=right.from;
    assert(mailbox.service_batch(owner(),1303,checked_batch_cast,[]{return true;})==24 && mailbox.result(right4.token,1303));
    bool touched_without_pending{};
    assert(mailbox.service_batch(owner(),1303,[&](Vec3,Vec3,double)->WorldTraceQueryResult{touched_without_pending=true;return {true,{}};},[&]{touched_without_pending=true;return true;})==0 && !touched_without_pending);
    auto left5=timed_request(0,5,1304),right5=timed_request(1,5,1304);right5.from=right.from;assert(mailbox.post(left5) && mailbox.post(right5));
    batch_calls=0;assert(mailbox.service_batch(owner(),1304,[&](Vec3 from,Vec3 to,double radius) {
        if(++batch_calls==1)mailbox.clear();return checked(finite_leaf(from,to,radius));
    },[]{return true;})==1);
    assert(!mailbox.result(left5.token,1304) && !mailbox.result(right5.token,1304) && !mailbox.owner(0).valid());
    mailbox.observe(0,owner());mailbox.observe(1,owner());auto fault=timed_request(0,1,1305);assert(mailbox.post(fault));
    assert(mailbox.service_batch(owner(),1305,[](Vec3,Vec3,double)->WorldTraceQueryResult{throw 7;},[]{return true;})==1);
    assert(!mailbox.result(fault.token,1305));auto after_fault=timed_request(0,2,1306);assert(mailbox.post(after_fault));
    unsigned nested_calls{};batch_calls=0;
    assert(mailbox.service_batch(owner(),1306,[&](Vec3 from,Vec3 to,double radius) {
        if(!batch_calls++)assert(mailbox.service_batch(owner(),1306,[&](Vec3,Vec3,double)->WorldTraceQueryResult{++nested_calls;return {true,{}};},[]{return true;})==0);
        return checked(finite_leaf(from,to,radius));
    },[]{return true;})==24 && nested_calls==0 && mailbox.result(after_fault.token,1306));
#if defined(_WIN32)
    mailbox.clear();mailbox.observe(0,owner());mailbox.observe(1,owner());auto seh_request=timed_request(0,1,1306);assert(mailbox.post(seh_request));
    const auto seh_owner=owner();SehBatchContext seh_context{&mailbox,&seh_owner,1306};assert(catch_seh_batch(&seh_context));
    assert(!mailbox.result(seh_request.token,1306));auto after_seh=timed_request(0,2,1307);assert(mailbox.post(after_seh));
    assert(mailbox.service_batch(owner(),1307,checked_batch_cast,[]{return true;})==24 && mailbox.result(after_seh.token,1307));
#endif
    mailbox.clear();mailbox.observe(0,owner());mailbox.observe(1,owner());
    auto epoch_left=timed_request(0,1,1307),epoch_right=timed_request(1,1,1307);epoch_right.from=right.from;assert(mailbox.post(epoch_left) && mailbox.post(epoch_right));
    batch_calls=0;assert(mailbox.service_batch(owner(),1307,[&](Vec3 from,Vec3 to,double radius) {
        if(++batch_calls==24) {auto newer=timed_request(1,2,1307);newer.from=right.from;newer.token.geometry_epoch=2;assert(mailbox.post(newer));}
        return checked(finite_leaf(from,to,radius));
    },[]{return true;})==24);
    assert(!mailbox.result(epoch_left.token,1307) && !mailbox.result(epoch_right.token,1307));
    mailbox.clear();mailbox.observe(0,owner());mailbox.observe(1,owner());auto failed_left=timed_request(0,1,1308),failed_right=timed_request(1,1,1308);
    failed_right.from=right.from;assert(mailbox.post(failed_left) && mailbox.post(failed_right));batch_calls=0;
    assert(mailbox.service_batch(owner(),1308,[&](Vec3 from,Vec3 to,double radius)->WorldTraceQueryResult {
        if(++batch_calls==31)return {false,{}};return checked(finite_leaf(from,to,radius));
    },[]{return true;})==31);
    assert(!mailbox.result(failed_left.token,1308) && !mailbox.result(failed_right.token,1308));
    mailbox.clear();mailbox.observe(0,owner());mailbox.observe(1,owner());
    auto expired_left=timed_request(0,1,1000),fresh_right=timed_request(1,1,1400);
    assert(mailbox.post(expired_left) && mailbox.post(fresh_right));batch_calls=0;
    assert(mailbox.service_batch(owner(),1400,[&](Vec3 from,Vec3 to,double radius) {
        ++batch_calls;return checked(finite_leaf(from,to,radius));
    },[]{return true;})==0 && batch_calls==0);
    assert(!mailbox.result(expired_left.token,1400) && !mailbox.result(fresh_right.token,1400));
    mailbox.clear();mailbox.observe(0,owner());mailbox.observe(1,owner());
    auto pair_left=timed_request(0,1,1500),pair_right=timed_request(1,1,1500);pair_right.from=right.from;pair_right.token.geometry_epoch=2;
    std::array<std::optional<AnatomicalContactRequest>,2> pair{pair_left,pair_right};assert(mailbox.post_batch(pair));
    batch_calls=0;assert(mailbox.service_batch(owner(),1500,checked_batch_cast,[]{return true;})==48);
    const auto left_result=mailbox.result(pair_left.token,1500),right_result=mailbox.result(pair_right.token,1500);
    assert(left_result && right_result);
    auto next_left=timed_request(0,2,1501),next_right=timed_request(1,2,1501);next_right.from=right.from;next_right.token.geometry_epoch=2;
    auto invalid_right=next_right;invalid_right.to[0].radius=-1;
    std::array<std::optional<AnatomicalContactRequest>,2> invalid_pair{next_left,invalid_right};
    assert(!mailbox.post_batch(invalid_pair) && mailbox.result(pair_left.token,1501) && mailbox.result(pair_right.token,1501));
    pair={next_left,next_right};assert(mailbox.post_batch(pair));
    auto newer_left=timed_request(0,3,1501),stale_right=timed_request(1,1,1501);stale_right.from=right.from;stale_right.token.geometry_epoch=2;
    std::array<std::optional<AnatomicalContactRequest>,2> stale_pair{newer_left,stale_right};
    assert(!mailbox.post_batch(stale_pair) && mailbox.has_pending(0) && mailbox.has_pending(1));
    batch_calls=0;assert(mailbox.service_batch(owner(),1501,checked_batch_cast,[]{return true;})==48);
    assert(mailbox.result(next_left.token,1501) && mailbox.result(next_right.token,1501));
    std::array<std::optional<AnatomicalContactRequest>,2> cancel_left{std::nullopt,timed_request(1,3,1502)};
    cancel_left[1]->from=right.from;cancel_left[1]->token.geometry_epoch=2;
    assert(mailbox.post_batch(cancel_left) && !mailbox.has_pending(0) && mailbox.has_pending(1));
    assert(!mailbox.result(next_left.token,1502));
    batch_calls=0;assert(mailbox.service_batch(owner(),1502,checked_batch_cast,[]{return true;})==24);
    assert(!mailbox.result(next_left.token,1502) && mailbox.result(cancel_left[1]->token,1502));
    auto cancel_during_left=timed_request(0,4,1503),cancel_during_right=timed_request(1,4,1503);cancel_during_right.from=right.from;cancel_during_right.token.geometry_epoch=2;
    pair={cancel_during_left,cancel_during_right};assert(mailbox.post_batch(pair));batch_calls=0;
    const auto cancel_cast=[&](Vec3 from,Vec3 to,double radius)->WorldTraceQueryResult {
        ++batch_calls;if(batch_calls==1) {std::array<std::optional<AnatomicalContactRequest>,2> empty{};assert(mailbox.post_batch(empty));}
        return checked(finite_leaf(from,to,radius));
    };
    assert(mailbox.service_batch(owner(),1503,cancel_cast,[]{return true;})==1);
    assert(!mailbox.result(cancel_during_left.token,1503) && !mailbox.result(cancel_during_right.token,1503));
    for(unsigned mismatch=0;mismatch<3;++mismatch) {
        mailbox.clear();mailbox.observe(0,owner());mailbox.observe(1,owner());
        auto mixed_left=timed_request(0,1,1600),mixed_right=timed_request(1,1,1600);
        mixed_right.from=right.from;
        if(mismatch==0)++mixed_right.token.pose_epoch;
        else if(mismatch==1)++mixed_right.token.request_revision;
        else ++mixed_right.tick;
        assert(mailbox.post(mixed_left) && mailbox.post(mixed_right));batch_calls=0;
        assert(mailbox.service_batch(owner(),1601,checked_batch_cast,[]{return true;})==0 && batch_calls==0);
        assert(!mailbox.result(mixed_left.token,1601) && !mailbox.result(mixed_right.token,1601));
    }
    std::cout<<"anatomical world contact geometry, finite-edge constraints, tokens, acquisition and call bounds passed\n";
}
