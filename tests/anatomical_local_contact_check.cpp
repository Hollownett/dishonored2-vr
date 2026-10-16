#include "../src/anatomical_world_contact.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <cstdlib>
#include <new>
#include <thread>
namespace d2vr {
struct AnatomicalAdmissionFixture {
    static void exhaust(AnatomicalWorldContact& mailbox) {
        std::lock_guard<std::mutex> lock(mailbox.mutex_);mailbox.admission_epoch_=UINT64_MAX;
    }
};
}
namespace {std::atomic<bool> fail_allocation{};std::atomic<long long> allocations{};}
void* operator new(std::size_t size) {
    if(fail_allocation.exchange(false))throw std::bad_alloc();
    if(auto* allocation=std::malloc(size?size:1)){++allocations;return allocation;}throw std::bad_alloc();
}
void operator delete(void* allocation) noexcept {if(allocation)--allocations;std::free(allocation);}
void operator delete(void* allocation,std::size_t) noexcept {if(allocation)--allocations;std::free(allocation);}
using namespace d2vr;
namespace {
AnatomicalContactOwner owner() {return {0x2000000,0x2100000,0x2700000,0x2200000,0x2a00000,6,{7,10},{8,10}};}
NativeWorldQuerySource query_source() {
    NativeWorldQuerySource value{};value.base=0x140000000;value.game=owner().game;value.player=owner().player;value.camera=owner().camera;
    value.camera_controller=0x2300000;value.physics=0x2400000;value.delegate=0x2500000;value.controller=0x2600000;value.world=owner().world;
    value.pool=0x2800000;value.body=value.pool+2*0xb0;value.handle=0x12000002;value.spawn=owner().spawn;
    value.body_count=64;value.broadphase=1;value.body_flags=3;value.camera_identity=owner().camera_identity;value.physics_identity={9,10};
    value.component_list_data=0x2900000;value.component_list_count=value.component_list_capacity=4;assert(value.valid());return value;
}
AnatomicalContactRequest request(unsigned side=0,std::uint64_t revision=1,std::uint64_t tick=1000) {
    AnatomicalContactRequest value{};value.token={owner(),1,1,1,revision,side};value.tick=tick;value.local_probe=true;
    value.safe_from=false;
    value.source=query_source();
    for(unsigned proxy=0;proxy<hand_proxy_count;++proxy) {
        value.to[proxy]={{0,static_cast<double>(proxy),0},.02};
        value.from[proxy]={value.to[proxy].centre+Vec3{-.0015,0,0},.02};
    }
    return value;
}
NativeInitialContact initial(Vec3 normal,float separation,std::uint32_t body=0x12000003u) {
    NativeInitialContact value{};value.point={0,0,0};value.normal=normal;value.signed_separation=separation;
    value.source_body=0x12000002u;value.body=body;value.shape_key=7;value.normal_valid=true;return value;
}
WorldTraceQueryResult empty_query() {
    WorldTraceQueryResult result{};result.complete=true;result.initial.emplace();result.regular.emplace();return result;
}
WorldTraceQueryResult overlap_query(const NativeInitialContact& contact,std::uintptr_t entity=0) {
    WorldTraceQueryResult result{};result.complete=true;result.initial.emplace();result.initial->count=1;
    result.initial->hits[0]={contact,entity};
    WorldTraceHit regular{};regular.fraction=0;regular.initial_overlap=true;regular.normal_valid=true;
    regular.body_handle=contact.body;regular.body=contact.body&0xffffffu;regular.shape_key=contact.shape_key;result.hit=regular;
    result.regular.emplace();result.regular->count=1;result.regular->hits[0]=regular;return result;
}
WorldTraceHit regular_contact(Vec3 point,Vec3 normal,std::uint32_t handle) {
    WorldTraceHit hit{};hit.point=point;hit.normal=normal;hit.fraction=.5;hit.normal_valid=true;
    hit.body_handle=handle;hit.body=handle&0xffffffu;return hit;
}
__declspec(noinline) void check_incomplete() {
    const auto source=request();AnatomicalLocalContactReducer reducer(source);
    for(unsigned proxy=0;proxy+1<hand_proxy_count;++proxy)assert(reducer.add(proxy,empty_query()));
    assert(reducer.finish().state==AnatomicalContactState::invalid);
}
__declspec(noinline) void check_clear() {
    const auto source=request();AnatomicalLocalContactReducer reducer(source);
    for(unsigned proxy=0;proxy<hand_proxy_count;++proxy)assert(reducer.add(proxy,empty_query()));
    const auto result=reducer.finish();
    assert(result.state==AnatomicalContactState::ready && !result.contact && !result.actionable() &&
        result.local_probe && result.casts==0 && !source.safe_from);
}
__declspec(noinline) void check_mtd() {
    const auto source=request();AnatomicalLocalContactReducer reducer(source);
    for(unsigned proxy=0;proxy<hand_proxy_count;++proxy) {
        const auto query=proxy==0?overlap_query(initial({-1,0,0},-.02f)):empty_query();
        assert(reducer.add(proxy,query));
    }
    const auto result=reducer.finish();
    assert(result.state==AnatomicalContactState::ready && result.actionable() && result.local_probe &&
        result.proxy==0 && std::abs(result.shift.x+.0215)<1e-9 &&
        std::abs((source.to[0].centre+result.shift).x+.0215)<1e-9);
}
__declspec(noinline) void check_corner() {
    const auto source=request();AnatomicalLocalContactReducer reducer(source);
    for(unsigned proxy=0;proxy<hand_proxy_count;++proxy) {
        auto query=empty_query();
        if(proxy==0) {
            query=overlap_query(initial({-1,0,0},-.02f));query.initial->count=2;
            query.initial->hits[1]={initial({0,-1,0},-.015f,0x12000004u),0};
        }
        assert(reducer.add(proxy,query));
    }
    const auto result=reducer.finish();
    assert(result.actionable() && std::abs(result.shift.x+.0215)<1e-9 && std::abs(result.shift.y+.015)<1e-9);
}
__declspec(noinline) void check_multiple_regular_planes() {
    const auto source=request();AnatomicalLocalContactReducer reducer(source);auto two_planes=empty_query();
    two_planes.regular->count=2;
    two_planes.regular->hits[0]=regular_contact({.015,0,0},{-1,0,0},0x12000003u);
    two_planes.regular->hits[1]=regular_contact({0,.015,0},{0,-1,0},0x12000004u);
    two_planes.hit=two_planes.regular->hits[0];
    for(unsigned proxy=0;proxy<hand_proxy_count;++proxy)
        assert(reducer.add(proxy,proxy==0?two_planes:empty_query()));
    const auto result=reducer.finish();
    assert(result.actionable() && result.hits[0] && result.hits[0]->point.x==.015);
    assert(std::abs(result.shift.x+.005)<1e-9 && std::abs(result.shift.y+.005)<1e-9);
}
__declspec(noinline) void check_opposing() {
    const auto source=request();AnatomicalLocalContactReducer reducer(source);
    for(unsigned proxy=0;proxy<hand_proxy_count;++proxy) {
        auto query=empty_query();
        if(proxy==0) {
            query=overlap_query(initial({-1,0,0},-.05f));query.initial->count=2;
            query.initial->hits[1]={initial({1,0,0},-.05f,0x12000004u),0};
        }
        assert(reducer.add(proxy,query));
    }
    const auto result=reducer.finish();assert(result.state==AnatomicalContactState::limited && !result.actionable());
}
__declspec(noinline) void check_outward_limit() {
    const auto source=request();AnatomicalLocalContactReducer reducer(source);
    for(unsigned proxy=0;proxy<hand_proxy_count;++proxy) {
        auto query=empty_query();
        if(proxy==0)query=overlap_query(initial({-1,0,0},-.02f));
        if(proxy==1)query=overlap_query(initial({1,0,0},-.0005f,0x12000004u));
        assert(reducer.add(proxy,query));
    }
    assert(reducer.finish().state==AnatomicalContactState::limited);
}
__declspec(noinline) void check_cap() {
    const auto source=request();AnatomicalLocalContactReducer reducer(source,.08);
    for(unsigned proxy=0;proxy<hand_proxy_count;++proxy)
        assert(reducer.add(proxy,proxy==0?overlap_query(initial({-1,0,0},-.12f)):empty_query()));
    const auto result=reducer.finish();
    assert(result.state==AnatomicalContactState::limited && !result.actionable() && std::abs(length(result.shift)-.08)<1e-9);
}
__declspec(noinline) void reject_query(const WorldTraceQueryResult& bad) {
    const auto source=request();AnatomicalLocalContactReducer reducer(source);assert(!reducer.add(0,bad));
    assert(reducer.finish().state==AnatomicalContactState::invalid);
}
__declspec(noinline) void check_bad_normal() {const auto bad=overlap_query(initial({0,0,0},-.02f));reject_query(bad);}
__declspec(noinline) void check_bad_scalar() {const auto bad=overlap_query(initial({-1,0,0},std::numeric_limits<float>::quiet_NaN()));reject_query(bad);}
__declspec(noinline) void check_bad_sign() {const auto bad=overlap_query(initial({-1,0,0},.001f));reject_query(bad);}
__declspec(noinline) void check_missing_initial() {auto bad=overlap_query(initial({-1,0,0},-.02f));bad.initial.reset();reject_query(bad);}
__declspec(noinline) void check_missing_regular() {auto bad=overlap_query(initial({-1,0,0},-.02f));bad.regular.reset();reject_query(bad);}
__declspec(noinline) void check_incomplete_query() {auto bad=overlap_query(initial({-1,0,0},-.02f));bad.complete=false;reject_query(bad);}
__declspec(noinline) void check_initial_overflow() {auto bad=empty_query();bad.initial->count=33;reject_query(bad);}
__declspec(noinline) void check_regular_overflow() {auto bad=empty_query();bad.regular->count=33;reject_query(bad);}
__declspec(noinline) void check_separate_normal() {
    const auto source=request();AnatomicalLocalContactReducer reducer(source);
    auto invalid_regular_normal=overlap_query(initial({-1,0,0},-.02f));
    invalid_regular_normal.hit->normal_valid=false;invalid_regular_normal.regular->hits[0].normal_valid=false;
    for(unsigned proxy=0;proxy<hand_proxy_count;++proxy)
        assert(reducer.add(proxy,proxy==0?invalid_regular_normal:empty_query()));
    assert(reducer.finish().actionable());
}
__declspec(noinline) void check_repeated() {
    const auto source=request();AnatomicalLocalContactReducer reducer(source);
    assert(reducer.add(0,empty_query()));assert(!reducer.add(0,empty_query()));
    assert(reducer.finish().state==AnatomicalContactState::invalid);
}
__declspec(noinline) void check_unmatched() {
    const auto source=request();AnatomicalLocalContactReducer reducer(source);
    auto hit=overlap_query(initial({-1,0,0},-.02f,0x12000004u));
    hit.hit->body_handle=0x12000003u;hit.regular->hits[0].body_handle=0x12000003u;
    assert(!reducer.add(0,hit));assert(reducer.finish().state==AnatomicalContactState::invalid);
}
__declspec(noinline) void check_shape_key() {
    const auto bad=overlap_query(initial({-1,0,0},-.02f));auto mismatch=bad;++mismatch.regular->hits[0].shape_key;
    reject_query(mismatch);
}
__declspec(noinline) void check_held() {
    auto source=request();source.held_entity=50;AnatomicalLocalContactReducer reducer(source);
    assert(!reducer.add(0,overlap_query(initial({-1,0,0},-.02f),50)));
    const auto result=reducer.finish();assert(result.state==AnatomicalContactState::occluded && !result.actionable());
}
__declspec(noinline) void check_mailbox() {
    AnatomicalWorldContact mailbox;mailbox.observe(0,owner());mailbox.observe(1,owner());
    auto left=request(0,1,2000),right=request(1,1,2000);std::array<std::optional<AnatomicalContactRequest>,2> pair{left,right};
    assert(mailbox.post_batch(pair));unsigned casts{};
    const auto cast=[&](Vec3 from,Vec3 to,double radius)->WorldTraceQueryResult {
        ++casts;assert(std::abs(length(to-from)-.0015)<1e-12 && radius==.02);return empty_query();
    };
    assert(mailbox.service_batch(owner(),2000,cast,[]{return true;})==48 && casts==48);
    for(const auto& value:pair) {
        const auto result=mailbox.result(value->token,2000);
        assert(result && result->state==AnatomicalContactState::ready && result->local_probe && !result->contact && result->casts==24);
    }
    left=request(0,2,2001);right=request(1,2,2001);pair={left,right};assert(mailbox.post_batch(pair));casts=0;
    assert(mailbox.service_batch(owner(),2001,cast,[]{return true;})==48 && casts==48);
    left=request(0,3,2002);right=request(1,3,2002);pair={left,right};assert(mailbox.post_batch(pair));casts=0;
    fail_allocation=true;assert(mailbox.service_batch(owner(),left.source,2002,cast,[]{return true;})==0 && !fail_allocation && casts==0);
    assert(!mailbox.result(left.token,2002) && !mailbox.result(right.token,2002));
    assert(mailbox.service_batch(owner(),left.source,2002,cast,[]{return true;})==48 && casts==48);
}
__declspec(noinline) void check_witness_transport() {
    const auto source=request();AnatomicalContactResult result{};
    {
        AnatomicalLocalContactReducer reducer(source);auto query=overlap_query(initial({-1,0,0},-.02f),50);
        query.initial->count=2;query.initial->hits[1]={initial({0,-1,0},-.015f,0x12000004),60};
        query.initial->hits[1].contact.shape_key=0xffffffffu;
        for(unsigned proxy=0;proxy<hand_proxy_count;++proxy)assert(reducer.add(proxy,proxy?empty_query():query));
        result=reducer.finish();assert(result.actionable() && result.entity==50 && result.hits[0]->body_handle==0x12000003);
        assert(result.witnesses && result.witnesses->size()==3 && result.source==source.source && result.request_tick==1000);
        const auto hidden=result.witnesses->at(1);assert(hidden && hidden->body==0x12000004 && hidden->entity==60 && hidden->shape_key==0xffffffffu);
        assert(hidden->source_body==source.source.handle && hidden->initial && hidden->contributes && hidden->proxy==0);
        assert(hidden->signed_separation==-.015f && hidden->from.radius==source.from[0].radius && length(hidden->to.centre-source.to[0].centre)==0);
        assert(std::abs(result.shift.y+.015)<1e-9 && !result.witnesses->at(3));
        assert(!reducer.add(0,query) && reducer.finish().witnesses==result.witnesses);
    }
    for(unsigned index=0;index<result.witnesses->size();++index) {
        const auto witness=result.witnesses->at(index);assert(witness);
        if(witness->contributes)assert(dot(result.shift,witness->normal)+1e-6>=witness->depth);
    }
    auto malformed=overlap_query(initial({-1,0,0},-.02f));malformed.initial->hits[0].contact.source_body+=0x1000000;reject_query(malformed);
    malformed=overlap_query(initial({-1,0,0},-.02f,0xffffffffu));reject_query(malformed);
    malformed=empty_query();malformed.regular->count=1;malformed.regular->hits[0]=regular_contact({0,0,0},{-1,0,0},0xffffffffu);reject_query(malformed);
    malformed=empty_query();malformed.regular->count=1;malformed.regular->hits[0]=regular_contact({NAN,0,0},{-1,0,0},0x12000003);reject_query(malformed);
    AnatomicalLocalContactReducer bounded(source,.08,1);auto two=overlap_query(initial({-1,0,0},-.02f));
    assert(!bounded.add(0,two) && !bounded.finish().witnesses && !bounded.finish().actionable());
    AnatomicalLocalContactReducer oversized(source,.08,AnatomicalContactWitnessBundle::capacity+1);
    assert(!oversized.add(0,empty_query()) && !oversized.finish().witnesses);
    fail_allocation=true;AnatomicalLocalContactReducer failed(source);assert(!fail_allocation && !failed.add(0,empty_query()));
    assert(failed.finish().state==AnatomicalContactState::invalid && !failed.finish().witnesses);
    AnatomicalLocalContactReducer full(source);auto many=empty_query();many.initial->count=many.regular->count=32;
    for(unsigned index=0;index<32;++index) {
        const auto contact=initial({-1,0,0},-.001f,0x12000003+index);many.initial->hits[index]={contact,0};
        auto regular=overlap_query(contact).regular->hits[0];many.regular->hits[index]=regular;
    }
    for(unsigned proxy=0;proxy<hand_proxy_count;++proxy)assert(full.add(proxy,many));
    assert(full.finish().witnesses->size()==AnatomicalContactWitnessBundle::capacity);
}
__declspec(noinline) void check_source_and_age() {
    const auto cast=[](Vec3,Vec3,double){return empty_query();};
    AnatomicalWorldContact mailbox;mailbox.observe(0,owner());mailbox.observe(1,owner());
    const auto sample=request();assert(mailbox.post(sample));
    assert(mailbox.service_batch(owner(),sample.source,1099,cast,[]{return true;})==24);
    const auto boundary=mailbox.result(sample.token,1100);assert(boundary && boundary->request_tick==1000 && boundary->query_tick==1099);
    assert(!mailbox.result(sample.token,1101) && !mailbox.result(sample.token,1198) && !mailbox.result(sample.token,1098));
    for(unsigned change=0;change<5;++change) {
        mailbox.clear();mailbox.observe(0,owner());mailbox.observe(1,owner());assert(mailbox.post(sample));
        auto current=sample.source;
        if(change==0)current.handle+=0x1000000;
        if(change==1)current.physics_identity.generation++;
        if(change==2)current.controller+=0x1000;
        if(change==3)current.body_filter_info++;
        if(change==4)current.camera_identity.flags^=0x10;
        unsigned calls{};assert(mailbox.service_batch(owner(),current,1000,[&](Vec3,Vec3,double){++calls;return empty_query();},[]{return true;})==0);
        assert(calls==0 && !mailbox.result(sample.token,1000));
    }
    mailbox.clear();mailbox.observe(0,owner());mailbox.observe(1,owner());assert(mailbox.post(sample));unsigned calls{};
    assert(mailbox.service_batch(owner(),sample.source,1000,[&](Vec3,Vec3,double) {
        ++calls;auto newer=sample;newer.token.request_revision++;newer.source.physics_identity.generation++;assert(mailbox.post(newer));return empty_query();
    },[]{return true;})==1 && calls==1 && !mailbox.result(sample.token,1000));
    mailbox.clear();mailbox.observe(0,owner());mailbox.observe(1,owner());auto legacy=sample;legacy.local_probe=false;legacy.safe_from=true;
    assert(mailbox.post(legacy));
    auto hit=[](Vec3,Vec3,double){auto query=empty_query();query.hit=regular_contact({0,0,0},{-1,0,0},0x12000003);return query;};
    assert(mailbox.service_batch(owner(),legacy.source,1001,hit,[]{return true;})==24);
    auto retained=legacy;retained.token.request_revision++;retained.tick=1100;retained.from=retained.to;assert(mailbox.post(retained));
    assert(mailbox.service_batch(owner(),retained.source,1100,hit,[]{return true;},[](const auto&){return true;})==0);
    const auto value=mailbox.result(retained.token,1100);assert(value && value->request_tick==1100 && value->query_tick==1001);
    assert(mailbox.result(retained.token,1101));
    assert(!mailbox.result(retained.token,1102));
    for(unsigned change=0;change<2;++change) {
        mailbox.clear();mailbox.observe(0,owner());mailbox.observe(1,owner());auto nonlocal=sample;nonlocal.local_probe=false;nonlocal.safe_from=true;
        if(change==0)nonlocal.source={};else nonlocal.source.physics_identity.generation++;
        assert(mailbox.post(nonlocal));unsigned rejected_calls{};
        assert(mailbox.service_batch(owner(),sample.source,1000,[&](Vec3,Vec3,double){++rejected_calls;return empty_query();},[]{return true;})==0);
        assert(!rejected_calls && !mailbox.result(nonlocal.token,1000));
    }
    for(unsigned change=0;change<2;++change) {
        mailbox.clear();mailbox.observe(0,owner());mailbox.observe(1,owner());assert(mailbox.post(sample));auto current=sample.source;unsigned completed_calls{};
        assert(mailbox.service_batch(owner(),sample.source,1000,[&](Vec3,Vec3,double){
            if(++completed_calls==24){if(change==0)current.physics_identity.generation++;else current.handle+=0x1000000;}
            return empty_query();
        },[&]{return current==sample.source;})==24);
        assert(completed_calls==24 && !mailbox.result(sample.token,1000));
    }
}
struct FaultBatch {AnatomicalWorldContact* mailbox;NativeWorldQuerySource source;unsigned mode;unsigned casts;};
unsigned invoke_fault_batch(FaultBatch* context) {
    return context->mailbox->service_batch(owner(),context->source,3000,[&](Vec3,Vec3,double){
        if(++context->casts==25 && context->mode==1)RaiseException(0xe0424242,0,0,nullptr);
        return overlap_query(initial({-1,0,0},-.002f));
    },[&]{if(context->casts==25 && context->mode==2)RaiseException(0xe0424242,0,0,nullptr);return true;});
}
bool catch_fault_batch(FaultBatch* context) {
    __try {invoke_fault_batch(context);return false;}
    __except(GetExceptionCode()==0xe0424242?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
}
__declspec(noinline) void check_seh_ownership() {
    for(unsigned mode=1;mode<=2;++mode) {
        AnatomicalWorldContact mailbox;mailbox.observe(0,owner());mailbox.observe(1,owner());
        std::array<std::optional<AnatomicalContactRequest>,2> pair{request(0,1,3000),request(1,1,3000)};
        assert(mailbox.post_batch(pair));const auto baseline=allocations.load();FaultBatch context{&mailbox,query_source(),mode,0};
        assert(catch_fault_batch(&context) && context.casts==25 && allocations==baseline && mailbox.quiescent());
        assert(!mailbox.result(pair[0]->token,3000) && !mailbox.result(pair[1]->token,3000));
        for(auto& pending:pair)pending->token.request_revision++;
        assert(mailbox.post_batch(pair));
        assert(mailbox.service_batch(owner(),context.source,3000,[](Vec3,Vec3,double){return empty_query();},[]{return true;})==48);
        assert(mailbox.result(pair[0]->token,3000) && mailbox.result(pair[1]->token,3000));
        mailbox.clear();assert(allocations==baseline);
    }
}
using Admission=AnatomicalWorldContact::AdmissionReceipt;
using RequestPair=AnatomicalWorldContact::RequestPair;
using TokenPair=AnatomicalWorldContact::TokenPair;
Admission observe_admission(AnatomicalWorldContact& mailbox) {
    mailbox.observe(0,owner());mailbox.observe(1,owner());const auto receipt=mailbox.admission();assert(receipt);return *receipt;
}
RequestPair admitted_requests(const Admission& receipt,std::uint64_t revision=1) {
    RequestPair pair{request(0,revision,3000),request(1,revision,3000)};
    for(auto& pending:pair)pending->token.admission_epoch=receipt.epoch;
    return pair;
}
TokenPair expected_tokens(const RequestPair& pair) {
    TokenPair tokens{};for(unsigned side=0;side<2;++side)if(pair[side])tokens[side]=pair[side]->token;return tokens;
}
void ready_admitted(AnatomicalWorldContact& mailbox,const Admission& receipt,const RequestPair& pair) {
    assert(mailbox.post_batch_current(receipt,pair));
    assert(mailbox.service_batch(owner(),query_source(),3000,[](Vec3,Vec3,double){return empty_query();},[]{return true;})==48);
}
void wait_entered(const std::atomic<bool>& entered) {
    const auto deadline=GetTickCount64()+5000;
    while(!entered && GetTickCount64()<deadline)std::this_thread::yield();assert(entered);
}
__declspec(noinline) void check_admission_aba() {
    AnatomicalWorldContact mailbox;auto receipt=observe_admission(mailbox);const auto pair=admitted_requests(receipt);
    assert(!mailbox.post_batch_current(receipt,RequestPair{request(0,1,3000),request(1,1,3000)}));
    ready_admitted(mailbox,receipt,pair);const auto copy=mailbox.result(pair[0]->token,3000);assert(copy && copy->witnesses);
    mailbox.clear();const auto newer=observe_admission(mailbox);assert(newer.epoch>receipt.epoch);
    assert(!mailbox.post_batch_current(receipt,{}) && !mailbox.post(pair[0].value()) && !mailbox.post_batch(pair));
    assert(!mailbox.consume_batch(receipt,expected_tokens(pair),3000,[](const auto&){assert(false);}));
    assert(copy->witnesses && !mailbox.result(pair[0]->token,3000));
    auto strict=admitted_requests(newer);assert(mailbox.post_batch_current(newer,strict));
    assert(!mailbox.post_batch_current(receipt,{}) && mailbox.has_pending(0) && mailbox.has_pending(1));
    auto foreign=owner();foreign.camera+=0x1000;mailbox.observe(0,foreign);mailbox.observe(0,owner());
    const auto restored=mailbox.admission();assert(restored && restored->epoch>newer.epoch);
    unsigned calls{};assert(mailbox.service_batch(owner(),query_source(),3000,[&](Vec3,Vec3,double){++calls;return empty_query();},[]{return true;})==0 && !calls);
    assert(!mailbox.post_batch_current(newer,strict));
    strict=admitted_requests(*restored);ready_admitted(mailbox,*restored,strict);
    auto tokens=expected_tokens(strict);auto wrong=tokens;++wrong[1]->request_revision;
    assert(!mailbox.consume_batch(*restored,wrong,3000,[](const auto&){assert(false);}));
    assert(!mailbox.consume_batch(*restored,tokens,3101,[](const auto&){assert(false);}));
    tokens[1].reset();bool consumed{};
    assert(mailbox.consume_batch(*restored,tokens,3000,[&](const auto& values){consumed=true;assert(values[0] && !values[1]);}));assert(consumed);
    assert(mailbox.post_batch_current(*restored,{}));
    assert(!mailbox.consume_batch(*restored,expected_tokens(strict),3000,[](const auto&){assert(false);}));
    mailbox.stop();assert(mailbox.stopped() && !mailbox.admission());assert(mailbox.start());
    const auto restarted=observe_admission(mailbox);assert(restarted.epoch>restored->epoch && !mailbox.post_batch_current(*restored,{}));
    AnatomicalAdmissionFixture::exhaust(mailbox);mailbox.clear();assert(!mailbox.admission() && mailbox.stopped() && !mailbox.start());
}
__declspec(noinline) void check_parked_service() {
    for(unsigned stop=0;stop<2;++stop) {
        AnatomicalWorldContact mailbox;const auto receipt=observe_admission(mailbox);const auto pair=admitted_requests(receipt);
        assert(mailbox.post_batch_current(receipt,pair));std::atomic<bool> entered{},release{};unsigned casts{};
        std::thread worker([&]{casts=mailbox.service_batch(owner(),query_source(),3000,[&](Vec3,Vec3,double){
            entered=true;while(!release)std::this_thread::yield();return empty_query();
        },[]{return true;});});
        wait_entered(entered);assert(!mailbox.quiescent());if(stop)mailbox.stop();else mailbox.clear();
        mailbox.observe(0,owner());mailbox.observe(1,owner());
        assert(!mailbox.admission() && !mailbox.start() && !mailbox.post_batch_current(receipt,{}));
        assert(!mailbox.consume_batch(receipt,expected_tokens(pair),3000,[](const auto&){assert(false);}));
        release=true;worker.join();assert(casts==1 && mailbox.quiescent() && !mailbox.result(pair[0]->token,3000));
        if(stop){assert(mailbox.stopped() && mailbox.start());observe_admission(mailbox);}else assert(mailbox.admission());
    }
}
__declspec(noinline) void check_exact_admitted_pair() {
    AnatomicalWorldContact mailbox;const auto receipt=observe_admission(mailbox);auto pair=admitted_requests(receipt);ready_admitted(mailbox,receipt,pair);
    const auto old=expected_tokens(pair);auto invalid=admitted_requests(receipt,2);invalid[1]->token.side=0;
    assert(!mailbox.post_batch_current(receipt,invalid));
    assert(mailbox.consume_batch(receipt,old,3000,[](const auto& values){assert(values[0] && values[1]);}));
    pair=admitted_requests(receipt,2);assert(mailbox.post_batch_current(receipt,pair));
    assert(!mailbox.consume_batch(receipt,old,3000,[](const auto&){assert(false);}));
    assert(mailbox.service_batch(owner(),query_source(),3000,[](Vec3,Vec3,double){return empty_query();},[]{return true;})==48);
    assert(!mailbox.consume_batch(receipt,old,3000,[](const auto&){assert(false);}));
    const auto expected=expected_tokens(pair);fail_allocation=true;
    assert(!mailbox.consume_batch(receipt,expected,3000,[](const auto&){assert(false);}) && !fail_allocation && mailbox.quiescent());
    assert(mailbox.consume_batch(receipt,expected,3000,[](const auto& values){assert(values[0]->token.request_revision==2 && values[1]->token.request_revision==2);}));
}
__declspec(noinline) void check_parked_consumer() {
    AnatomicalWorldContact mailbox;const auto receipt=observe_admission(mailbox);const auto pair=admitted_requests(receipt);ready_admitted(mailbox,receipt,pair);
    const auto tokens=expected_tokens(pair);std::atomic<bool> entered{},release{};bool consumed{};
    std::thread worker([&]{consumed=mailbox.consume_batch(receipt,tokens,3000,[&](const auto& values){
        assert(values[0] && values[1] && !mailbox.quiescent());entered=true;while(!release)std::this_thread::yield();
        assert(values[0]->witnesses && values[1]->witnesses);
    });});
    wait_entered(entered);mailbox.clear();mailbox.observe(0,owner());mailbox.observe(1,owner());assert(!mailbox.admission() && !mailbox.start());
    mailbox.stop();assert(!mailbox.stopped());release=true;worker.join();assert(consumed && mailbox.stopped());assert(mailbox.start());
    const auto newer=observe_admission(mailbox);assert(newer.epoch>receipt.epoch);
}
struct ConsumeFault {AnatomicalWorldContact* mailbox;Admission receipt;TokenPair tokens;};
void invoke_consume_fault(ConsumeFault* context) {
    assert(context->mailbox->consume_batch(context->receipt,context->tokens,3000,[&](const auto& values){
        assert(values[0]->witnesses && values[1]->witnesses && !context->mailbox->quiescent());RaiseException(0xe0424242,0,0,nullptr);
    }));
}
bool catch_consume_fault(ConsumeFault* context) {
    __try {invoke_consume_fault(context);return false;}
    __except(GetExceptionCode()==0xe0424242?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
}
__declspec(noinline) void check_consumer_cleanup() {
    AnatomicalWorldContact mailbox;auto receipt=observe_admission(mailbox);auto pair=admitted_requests(receipt);ready_admitted(mailbox,receipt,pair);
    const auto tokens=expected_tokens(pair);const auto baseline=allocations.load();bool caught{};
    try {mailbox.consume_batch(receipt,tokens,3000,[](const auto& values){assert(values[0] && values[1]);if(values[0])throw 17;});}
    catch(int value){assert(value==17);caught=true;}
    assert(caught && mailbox.quiescent() && allocations==baseline);
    ConsumeFault context{&mailbox,receipt,tokens};assert(catch_consume_fault(&context) && mailbox.quiescent() && allocations==baseline);
    assert(mailbox.consume_batch(receipt,tokens,3000,[&](const auto& values){
        assert(values[0] && !mailbox.consume_batch(receipt,tokens,3000,[](const auto&){assert(false);}));
        mailbox.clear();assert(!mailbox.quiescent() && !mailbox.admission() && !mailbox.start());mailbox.stop();assert(!mailbox.stopped());
    }));
    assert(mailbox.stopped() && mailbox.start());receipt=observe_admission(mailbox);pair=admitted_requests(receipt);
    assert(mailbox.post_batch_current(receipt,pair));
    assert(mailbox.service_batch(owner(),query_source(),3000,[&](Vec3,Vec3,double){mailbox.clear();assert(!mailbox.admission() && !mailbox.quiescent());return empty_query();},[]{return true;})==1);
    assert(mailbox.quiescent() && !mailbox.result(pair[0]->token,3000));
}
}
int main() {
    check_incomplete();check_clear();check_mtd();check_corner();check_multiple_regular_planes();check_opposing();
    check_outward_limit();check_cap();check_bad_normal();check_bad_scalar();check_bad_sign();check_missing_initial();
    check_missing_regular();check_separate_normal();check_incomplete_query();check_initial_overflow();check_regular_overflow();
    check_repeated();check_unmatched();check_shape_key();check_held();check_mailbox();
    check_witness_transport();check_source_and_age();check_seh_ownership();
    check_admission_aba();check_parked_service();check_exact_admitted_pair();check_parked_consumer();check_consumer_cleanup();
    static_assert(sizeof(AnatomicalContactResult)<16384 && sizeof(AnatomicalLocalContactReducer)<16384);
    static_assert(!std::is_copy_constructible_v<AnatomicalLocalContactReducer>);
    std::cout<<"witness="<<sizeof(AnatomicalContactWitness)<<" bundle="<<sizeof(AnatomicalContactWitnessBundle)
        <<" result="<<sizeof(AnatomicalContactResult)<<" reducer="<<sizeof(AnatomicalLocalContactReducer)<<'\n';
    std::cout<<"anatomical local-contact MTD, all-plane checked batches and paired 48-cast service passed\n";
}
