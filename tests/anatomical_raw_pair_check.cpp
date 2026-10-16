#include "../src/anatomical_raw_pair.hpp"
#include "../src/hand_palette_contact.hpp"
#include <cassert>
#include <iostream>
#include <cstdlib>

namespace {
using namespace d2vr;
using Producer=AnatomicalRawPair;
using Store=AnatomicalWorldContact;
const std::array<float,15> model_frame{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
AnatomicalContactOwner owner() {return {0x2000000,0x2100000,0x2700000,0x2200000,0x2a00000,6,{7,10},{8,10}};}
NativeWorldQuerySource source() {
    NativeWorldQuerySource value{};value.base=0x140000000;value.game=owner().game;value.player=owner().player;value.camera=owner().camera;
    value.camera_controller=0x2300000;value.physics=0x2400000;value.delegate=0x2500000;value.controller=0x2600000;value.world=owner().world;
    value.pool=0x2800000;value.body=value.pool+2*0xb0;value.handle=0x12000002;value.spawn=owner().spawn;
    value.body_count=64;value.broadphase=1;value.body_flags=3;value.camera_identity=owner().camera_identity;value.physics_identity={9,10};
    value.component_list_data=0x2900000;value.component_list_count=value.component_list_capacity=4;assert(value.valid());return value;
}
rig::ArmBinding binding_for(unsigned side) {
    const auto skeleton=fullbody::skeleton(side?fullbody::data::kRightArm:fullbody::data::kLeftArm);
    const std::string prefix=side?"right":"left";rig::ArmBinding binding{};std::array<rig::Transform,69> palette{};
    binding.chain={static_cast<unsigned>(skeleton.find(prefix+"_arm_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_elbow_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_wrist_jnt"))};
    for(unsigned joint=0;joint<69;++joint){binding.parents[joint]=static_cast<std::int16_t>(skeleton.parent[joint]);palette[joint]=fullbody::to_transform(skeleton.bind[joint]);}
    const std::array<std::string,5> digits{"thumb","index","medium","ring","pinky"};
    for(unsigned finger=0;finger<5;++finger)for(unsigned segment=0;segment<3;++segment)
        binding.finger_chains[finger][segment]=static_cast<unsigned>(skeleton.find(prefix+"_"+digits[finger]+"_0"+std::to_string(segment+(finger?2:1))+"_jnt"));
    binding.palm={static_cast<unsigned>(skeleton.find(prefix+"_helper_underwrist_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_helper_upperwrist_jnt"))};
    binding.bind_wrist=palette[binding.chain.wrist];std::array<rig::Transform,4> knuckles{};
    for(unsigned finger=1;finger<5;++finger)knuckles[finger-1]=palette[binding.finger_chains[finger][0]];
    binding.bind_grip=rig::hand_bind_grip(binding.bind_wrist,knuckles);binding.fingers=rig::finger_binding(palette,binding.parents,binding.finger_chains);return binding;
}
std::shared_ptr<const rig::NativeArmBindingSnapshot> make_binding() {
    std::array<rig::Arm,2> arms{};
    for(unsigned side=0;side<2;++side)arms[side]={owner().player,0x2b00000,0x2c00000+side*0x1000,0x2d00000+side*0x1000,0x2e00000+side*0x1000,{11,10},{12+static_cast<int>(side),10},side};
    assert(rig::native_arm_pair_owned(arms));
    return std::make_shared<rig::NativeArmBindingSnapshot>(rig::NativeArmBindingSnapshot{arms,{binding_for(0),binding_for(1)},{}});
}
struct Fixture {
    Store store;Producer producer;std::shared_ptr<const rig::NativeArmBindingSnapshot> binding=make_binding();Store::AdmissionReceipt receipt{};
    Fixture(){producer.configure(true,store);observe();}
    std::optional<Producer::Observation> observation(std::uint64_t now) const{return producer.observation(store,now);}
    bool current(const Producer::Observation& expected,std::uint64_t now) const{return producer.current(expected,store,now);}
    void observe(){store.observe(0,owner());store.observe(1,owner());const auto admitted=store.admission();assert(admitted);receipt=*admitted;}
    Producer::Publication publication(unsigned side,std::uint64_t tick=3000,std::uint64_t frame=100) const {
        Producer::Publication value{};value.admission=receipt;value.source=source();value.binding=binding;value.side=side;
        value.tracking.tick=value.tracking.source_tick=tick;value.tracking.tracking_revision=20;value.tracking.pose_epoch=3;
        value.tracking.reference_epoch=4;value.tracking.mapping_revision=5;value.tracking.epoch=6;value.tracking.native_frame=7;
        value.tracking.tracked_hands=value.tracking.raw_grip_hands=3;
        for(unsigned hand=0;hand<2;++hand){value.tracking.grip_valid[hand]=value.tracking.hand_empty[hand]=true;value.tracking.raw_grip[hand]={};}
        value.model_frame=model_frame;value.frame=frame;value.solved_revision=frame;value.fit_revision=9;value.root_mapping_valid=true;
        const auto geometry=HandPaletteContact::bind(binding->bindings[side]);assert(geometry);
        const auto proxies=geometry->proxies(binding->bindings[side].fingers.open,model_frame);assert(proxies);value.proxies=*proxies;
        assert(length(value.proxies[0].centre-value.proxies[23].centre)>.01);return value;
    }
};
WorldTraceQueryResult empty_query(){WorldTraceQueryResult value{};value.complete=true;value.initial.emplace();value.regular.emplace();return value;}
WorldTraceQueryResult overlap_query(Vec3 point,const NativeWorldQuerySource& query_source) {
    auto value=empty_query();NativeInitialContact contact{};contact.point=point;contact.normal={0,0,1};contact.normal_valid=true;
    contact.signed_separation=-.002f;contact.source_body=query_source.handle;contact.body=0x12000003;contact.shape_key=0xffffffffu;
    value.initial->count=1;value.initial->hits[0]={contact,0x3000000};WorldTraceHit hit{};
    hit.point=point;hit.normal=contact.normal;hit.normal_valid=true;hit.fraction=0;hit.initial_overlap=true;
    hit.body_handle=contact.body;hit.body=contact.body&0xffffffu;hit.shape_key=contact.shape_key;hit.entity=0x3000000;
    value.regular->count=1;value.regular->hits[0]=hit;value.hit=hit;return value;
}
void no_queries(Fixture& fixture,std::uint64_t now=3000) {
    unsigned calls{};assert(fixture.store.service_batch(owner(),source(),now,[&](Vec3,Vec3,double){++calls;return empty_query();},[]{return true;})==0);assert(!calls);
}
void check_complete(Fixture& fixture,const Producer::Publication& left,const Producer::Publication& right,std::uint64_t now) {
    const auto observed=fixture.observation(now);assert(observed && observed->source==source() && observed->tick==left.tracking.tick);
    unsigned calls{},first_side=2;
    assert(fixture.store.service_batch(owner(),observed->source,now,[&](Vec3 from,Vec3 to,double radius){
        if(!calls){const bool matches_left=length(to-left.proxies[0].centre)<1e-12,matches_right=length(to-right.proxies[0].centre)<1e-12;
            assert(matches_left!=matches_right);first_side=matches_left?0u:1u;}
        const auto side=(first_side+calls/24)&1u,proxy=calls%24;++calls;const auto& sphere=(side?right:left).proxies[proxy];
        assert(length(to-sphere.centre)<1e-12 && radius==sphere.radius && length(from-(to+Vec3{0,0,.0015}))<1e-12);
        return overlap_query(to,observed->source);
    },[]{return true;})==48 && calls==48);
    bool consumed{};assert(fixture.store.consume_batch(observed->admission,observed->tokens,now,[&](const Store::ResultPair& values){
        consumed=true;for(unsigned side=0;side<2;++side){const auto& value=values[side];assert(value && value->token==*observed->tokens[side]);
            assert(value->source==observed->source && value->request_tick==left.tracking.tick && value->query_tick==now && value->local_probe && value->casts==24);
            assert(value->state==AnatomicalContactState::ready && value->witnesses && value->witnesses->size()==48);
            const auto witness=value->witnesses->at(0);assert(witness && witness->source_body==observed->source.handle && witness->shape_key==0xffffffffu);
            assert(length(witness->from.centre-(witness->to.centre+Vec3{0,0,.0015}))<1e-12);
        }
    }));assert(consumed && fixture.store.quiescent());
}
void paired_and_rate() {
    Fixture fixture;const auto left=fixture.publication(0),right=fixture.publication(1);
    assert(!fixture.producer.publish(left,fixture.store,3000));assert(!fixture.observation(3000));no_queries(fixture);
    assert(fixture.producer.publish(right,fixture.store,3000));const auto committed=fixture.observation(3000);assert(committed);
    assert(committed->tokens[0] && committed->tokens[1] && committed->tokens[0]->admission_epoch==fixture.receipt.epoch);
    assert(committed->tokens[0]->request_revision==committed->tokens[1]->request_revision);check_complete(fixture,left,right,3000);
    for(const auto now:{3001ull,3050ull}) {
        assert(!fixture.producer.publish(left,fixture.store,now));assert(!fixture.producer.publish(right,fixture.store,now));
        assert(fixture.current(*committed,now));no_queries(fixture,now);
    }
    assert(!fixture.producer.publish(fixture.publication(0,3050,101),fixture.store,3050));
    assert(!fixture.producer.publish(fixture.publication(1,3050,101),fixture.store,3050));
    assert(fixture.current(*committed,3050));no_queries(fixture,3050);
    assert(!fixture.producer.publish(left,fixture.store,3100));assert(!fixture.producer.publish(right,fixture.store,3100));
    assert(fixture.current(*committed,3100));no_queries(fixture,3100);
    const auto newer_left=fixture.publication(0,3100,101),newer_right=fixture.publication(1,3100,101);
    assert(!fixture.producer.publish(newer_left,fixture.store,3100));no_queries(fixture,3100);
    assert(fixture.producer.publish(newer_right,fixture.store,3100));const auto newer=fixture.observation(3100);
    assert(newer && newer->revision>committed->revision && !fixture.current(*committed,3100));
    check_complete(fixture,newer_left,newer_right,3100);
}
void pending_duplicates() {
    Fixture fixture;auto left=fixture.publication(0);const auto right=fixture.publication(1);
    assert(!fixture.producer.publish(left,fixture.store,3000));assert(!fixture.producer.publish(left,fixture.store,3001));
    assert(fixture.producer.publish(right,fixture.store,3001));check_complete(fixture,left,right,3001);
    Fixture changed;left=changed.publication(0);assert(!changed.producer.publish(left,changed.store,3000));
    left.model_frame[0]=1;assert(!changed.producer.publish(left,changed.store,3001));
    assert(!changed.producer.publish(changed.publication(1),changed.store,3001));assert(!changed.observation(3001));no_queries(changed,3001);
}
void committed_changes_and_reverse() {
    for(unsigned change=0;change<4;++change) {
        Fixture fixture;auto left=fixture.publication(0);const auto right=fixture.publication(1);
        assert(!fixture.producer.publish(right,fixture.store,3000));no_queries(fixture);
        assert(fixture.producer.publish(left,fixture.store,3000));const auto old=fixture.observation(3000);assert(old);
        if(change==0)left.model_frame[0]=1;
        if(change==1)left.proxies[0].centre.x+=.01;
        if(change==2)left.source.physics_identity.generation++;
        if(change==3)left.world_from_solve.p.z+=.01;
        assert(!fixture.producer.publish(left,fixture.store,3001));assert(!fixture.observation(3001) && !fixture.current(*old,3001));
        assert(!fixture.store.consume_batch(old->admission,old->tokens,3001,[](const auto&){assert(false);}));no_queries(fixture,3001);
    }
    Fixture fixture;const auto left=fixture.publication(0),right=fixture.publication(1);
    assert(!fixture.producer.publish(right,fixture.store,3000));assert(fixture.producer.publish(left,fixture.store,3000));
    check_complete(fixture,left,right,3000);assert(fixture.observation(3100) && !fixture.observation(3101));
    assert(!fixture.observation(2999));
}
void mismatched_samples() {
    for(unsigned change=0;change<10;++change) {
        Fixture fixture;const auto left=fixture.publication(0);auto right=fixture.publication(1);
        if(change==0)right.binding=make_binding();
        if(change==1)right.fit_revision++;
        if(change==2)right.tracking.mapping_revision++;
        if(change==3)right.world_from_solve.p.x+=.01;
        if(change==4)right.source.physics_identity.generation++;
        if(change==5)right.frame++;
        if(change==6)right.solved_revision++;
        if(change==7)right.tracking.reference_epoch++;
        if(change==8)right.tracking.tracking_revision++;
        if(change==9)right.tracking.native_frame++;
        assert(!fixture.producer.publish(left,fixture.store,3000));assert(!fixture.producer.publish(right,fixture.store,3001));
        assert(!fixture.observation(3001));no_queries(fixture,3001);
        const auto fresh_left=fixture.publication(0,3100,101),fresh_right=fixture.publication(1,3100,101);
        assert(!fixture.producer.publish(fresh_left,fixture.store,3100));assert(fixture.producer.publish(fresh_right,fixture.store,3100));
        check_complete(fixture,fresh_left,fresh_right,3100);
    }
}
void invalid_publications() {
    for(unsigned change=0;change<14;++change) {
        Fixture fixture;auto left=fixture.publication(0);std::uint64_t now=3000;
        if(change==0)left.source={};
        if(change==1)left.tracking.raw_grip_hands=0;
        if(change==2)left.tracking.tracked_hands=1;
        if(change==3)left.tracking.grip_valid[0]=false;
        if(change==4)left.tracking.hand_empty[0]=false;
        if(change==5)left.root_mapping_valid=false;
        if(change==6)left.world_from_solve.p.x=NAN;
        if(change==7)left.binding.reset();
        if(change==8)left.tracking.pose_epoch=0;
        if(change==9)left.tracking.reference_epoch=0;
        if(change==10)left.tracking.tracking_revision=0;
        if(change==11)now=3101;
        if(change==12){left.tracking.source_tick=2999;now=3100;}
        if(change==13)left.model_frame[0]=NAN;
        assert(!fixture.producer.publish(left,fixture.store,now));assert(!fixture.observation(now));no_queries(fixture,now);
    }
}
void clear_owner_and_stop() {
    Fixture fixture;const auto left=fixture.publication(0),right=fixture.publication(1);
    assert(!fixture.producer.publish(left,fixture.store,3000));fixture.store.clear();fixture.observe();
    assert(!fixture.producer.publish(right,fixture.store,3000));assert(!fixture.store.post_batch_current(left.admission,{}));no_queries(fixture);
    fixture.producer.clear(fixture.store);fixture.observe();const auto current_left=fixture.publication(0),current_right=fixture.publication(1);
    assert(!fixture.producer.publish(current_left,fixture.store,3000));assert(fixture.producer.publish(current_right,fixture.store,3000));
    assert(!fixture.store.post_batch_current(left.admission,{}) && fixture.store.has_pending(0) && fixture.store.has_pending(1));
    auto foreign=owner();foreign.camera+=0x1000;fixture.store.observe(0,foreign);fixture.store.observe(0,owner());
    assert(!fixture.producer.publish(current_right,fixture.store,3000));no_queries(fixture);
    fixture.producer.stop(fixture.store);assert(!fixture.producer.enabled() && fixture.store.stopped() && !fixture.observation(3000));
    assert(!fixture.producer.publish(current_left,fixture.store,3000));assert(fixture.store.start());fixture.observe();
    assert(!fixture.producer.publish(fixture.publication(0),fixture.store,3000));
    fixture.producer.configure(true,fixture.store);fixture.observe();const auto restarted_left=fixture.publication(0),restarted_right=fixture.publication(1);
    assert(restarted_left.admission.epoch>left.admission.epoch);
    assert(!fixture.producer.publish(restarted_left,fixture.store,3000));assert(fixture.producer.publish(restarted_right,fixture.store,3000));
    check_complete(fixture,restarted_left,restarted_right,3000);
}
void observation_authentication() {
    Fixture fixture;assert(!fixture.producer.publish(fixture.publication(0),fixture.store,3000));
    assert(fixture.producer.publish(fixture.publication(1),fixture.store,3000));const auto observed=fixture.observation(3000);assert(observed);
    for(unsigned change=0;change<15;++change) {
        auto altered=*observed;
        if(change==0)altered.source.handle+=0x1000000;
        if(change==1)altered.tick++;
        if(change==2)altered.source_tick++;
        if(change==3)altered.pose_epoch++;
        if(change==4)altered.frame++;
        if(change==5)altered.tracking_revision++;
        if(change==6)altered.fit_revision++;
        if(change==7)altered.revision++;
        if(change==8)altered.tokens[0]->request_revision++;
        if(change==9)altered.admission.epoch++;
        if(change==10)altered.admission.owners[0].camera+=0x1000;
        if(change==11)altered.tokens[1].reset();
        if(change==12)altered.arms[0].animation_identity.generation++;
        if(change==13)altered.model_frames[1][0]+=1;
        if(change==14)altered.world_from_solve.p.y+=.01;
        assert(!fixture.current(altered,3000) && fixture.current(*observed,3000));
    }
    for(unsigned change=0;change<3;++change) {
        Fixture standalone;assert(!standalone.producer.publish(standalone.publication(0),standalone.store,3000));
        assert(standalone.producer.publish(standalone.publication(1),standalone.store,3000));const auto old=standalone.observation(3000);assert(old);
        if(change==0){standalone.store.clear();standalone.observe();}
        if(change==1)standalone.store.stop();
        if(change==2){auto foreign=owner();foreign.camera+=0x1000;standalone.store.observe(0,foreign);standalone.store.observe(0,owner());}
        assert(!standalone.observation(3000) && !standalone.current(*old,3000));no_queries(standalone);
        assert(!standalone.store.consume_batch(old->admission,old->tokens,3000,[](const auto&){assert(false);}));
    }
}
void older_callbacks_do_not_revoke_newer() {
    Fixture fixture;const auto old_left=fixture.publication(0),old_right=fixture.publication(1);
    assert(!fixture.producer.publish(old_left,fixture.store,3000));assert(fixture.producer.publish(old_right,fixture.store,3000));
    check_complete(fixture,old_left,old_right,3000);
    const auto left=fixture.publication(0,3200,102),right=fixture.publication(1,3200,102);
    assert(!fixture.producer.publish(left,fixture.store,3200));
    assert(!fixture.producer.publish(old_right,fixture.store,3200));
    assert(!fixture.observation(3200));assert(fixture.producer.publish(right,fixture.store,3200));
    const auto newer=fixture.observation(3200);assert(newer && newer->frame==102 && newer->arms==fixture.binding->arms);
    assert(newer->model_frames[0]==left.model_frame && newer->model_frames[1]==right.model_frame);
    for(unsigned change=0;change<3;++change) {
        auto old=old_left;
        if(change==1)old.source={};
        if(change==2){old.frame=102;old.solved_revision=101;}
        assert(!fixture.producer.publish(old,fixture.store,3200));assert(fixture.current(*newer,3200));
        assert(fixture.store.has_pending(0) && fixture.store.has_pending(1));
    }
    check_complete(fixture,left,right,3200);
    assert(!fixture.producer.publish(old_right,fixture.store,3250) && fixture.current(*newer,3250));
    assert(fixture.store.consume_batch(newer->admission,newer->tokens,3250,[](const auto& values){
        assert(values[0] && values[1] && values[0]->request_tick==3200 && values[1]->request_tick==3200);
    }));
}
void capture_prefilter() {
    Fixture fixture;
    assert(fixture.producer.wants_publication(fixture.receipt,100,100,0,3000));
    assert(!fixture.producer.publish(fixture.publication(0),fixture.store,3000));
    assert(fixture.producer.wants_publication(fixture.receipt,100,100,1,3000));
    assert(fixture.producer.wants_publication(fixture.receipt,101,101,0,3010));
    assert(!fixture.producer.wants_publication(fixture.receipt,99,99,1,3000));
    assert(fixture.producer.publish(fixture.publication(1),fixture.store,3000));
    assert(fixture.producer.wants_publication(fixture.receipt,100,100,0,3000));
    assert(!fixture.producer.wants_publication(fixture.receipt,101,101,0,3099));
    assert(!fixture.producer.wants_publication(fixture.receipt,101,101,1,2999));
    assert(fixture.producer.wants_publication(fixture.receipt,101,101,1,3100));
    assert(!fixture.producer.wants_publication(fixture.receipt,101,101,2,3100));
    fixture.producer.clear(fixture.store);fixture.observe();
    assert(fixture.producer.wants_publication(fixture.receipt,1,1,0,3100));
    fixture.producer.stop(fixture.store);
    assert(!fixture.producer.wants_publication(fixture.receipt,1,1,0,3100));
}
}
int main() {
    _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    paired_and_rate();pending_duplicates();committed_changes_and_reverse();mismatched_samples();invalid_publications();clear_owner_and_stop();observation_authentication();older_callbacks_do_not_revoke_newer();capture_prefilter();
    std::cout<<"anatomical raw paired publication, exact-source local48 sampling, duplicate/rate/mismatch/ABA/stop checks passed\n";
}
