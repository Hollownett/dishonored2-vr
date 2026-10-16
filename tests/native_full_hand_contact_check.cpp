#include "../src/native_full_hand_contact.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <condition_variable>
using namespace d2vr;
using namespace d2vr::fullbody;
namespace {
std::atomic<std::uint64_t> clock_value{};
std::uint64_t fake_clock() noexcept {return clock_value.load();}
constexpr std::array<float,15> identity_frame{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
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
    const std::string prefix=side?"right":"left";
    rig::ArmBinding binding{};
    binding.chain={static_cast<unsigned>(skeleton.find(prefix+"_arm_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_elbow_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_wrist_jnt"))};
    std::array<rig::Transform,69> palette{};
    for(unsigned joint=0;joint<69;++joint) {binding.parents[joint]=static_cast<std::int16_t>(skeleton.parent[joint]);palette[joint]=fullbody::to_transform(skeleton.bind[joint]);}
    const std::array<std::string,5> digits{"thumb","index","medium","ring","pinky"};
    for(unsigned finger=0;finger<5;++finger)for(unsigned segment=0;segment<3;++segment)
        binding.finger_chains[finger][segment]=static_cast<unsigned>(skeleton.find(prefix+"_"+digits[finger]+"_0"+std::to_string(segment+(finger?2:1))+"_jnt"));
    binding.palm={static_cast<unsigned>(skeleton.find(prefix+"_helper_underwrist_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_helper_upperwrist_jnt"))};
    binding.bind_wrist=palette[binding.chain.wrist];std::array<rig::Transform,4> knuckles{};
    for(unsigned finger=1;finger<5;++finger)knuckles[finger-1]=palette[binding.finger_chains[finger][0]];
    binding.bind_grip=rig::hand_bind_grip(binding.bind_wrist,knuckles);
    binding.fingers=rig::finger_binding(palette,binding.parents,binding.finger_chains);
    return binding;
}
struct Rig {
    body::Skeleton body=skeleton(data::kBody);
    body::Skeleton arms[2]{skeleton(data::kLeftArm),skeleton(data::kRightArm)};
    std::array<Transform,20> native_body{};
    std::array<Transform,69> native_arm[2]{};
    Transform bind_palm[2]{};
    unsigned wrist[2]{};
    Tracking tracking{};
    Rig() {
        tracking.eye=body.bind[body.find("camera_jnt")];
        tracking.epoch=1;tracking.pose_epoch=2;tracking.reference_epoch=3;tracking.mapping_revision=4;
        tracking.tick=100;tracking.source_tick=100;tracking.seconds=.1;tracking.tracked_hands=3;tracking.raw_grip_hands=3;
        for(unsigned j=0;j<20;++j)native_body[j]=to_transform(body.bind[j]);
        for(unsigned side=0;side<2;++side) {
            const auto& arm=arms[side];const std::string prefix=side?"right":"left";
            for(unsigned j=0;j<69;++j)native_arm[side][j]=to_transform(arm.bind[j]);
            wrist[side]=static_cast<unsigned>(arm.find(prefix+"_wrist_jnt"));
            std::array<Transform,4> knuckles{};const char* fingers[]{"index","medium","ring","pinky"};
            for(unsigned f=0;f<4;++f)knuckles[f]=native_arm[side][arm.find(prefix+"_"+fingers[f]+"_02_jnt")];
            bind_palm[side]=rig::hand_bind_grip(native_arm[side][wrist[side]],knuckles);
            tracking.grip[side]=body::mul(body.bind[body.find("center_spine03_jnt")],to_xf(bind_palm[side]));
            // Put the test hand comfortably inside the actual rig's reachable workspace.
            tracking.grip[side].p.y+=side?.12:-.12;
            tracking.grip[side].p.x+=.08;
            tracking.raw_grip[side]=tracking.grip[side];
            tracking.grip_valid[side]=true;tracking.hand_empty[side]=true;
        }
    }
    void seed(Driver& driver) const {
        driver.tune(0,1);driver.note_root(identity_frame);
        for(unsigned side=0;side<2;++side)(void)driver.arm(side,native_arm[side],identity_frame);
        driver.publish(tracking,{});assert(driver.body(native_body,identity_frame,1));
    }
    Xf palm(const std::array<Transform,69>& palette,const std::array<float,15>& frame,unsigned side=0) const {
        const auto local=rig::rigid_retarget(bind_palm[side],native_arm[side][wrist[side]],palette[wrist[side]]);
        return to_world(frame_of(frame),to_xf(local));
    }
};
struct Fixture {
    Rig rig;
    Driver driver;
    std::shared_ptr<const rig::NativeArmBindingSnapshot> binding;
    Fixture() {
        std::array<rig::Arm,2> arms{};std::array<rig::ArmBinding,2> bindings{};
        for(unsigned side=0;side<2;++side) {arms[side].player=owner().player;arms[side].side=side;bindings[side]=binding_for(side);}
        binding=std::make_shared<rig::NativeArmBindingSnapshot>(rig::NativeArmBindingSnapshot{arms,bindings,{}});
        rig.tracking.tracking_revision=9;rig.seed(driver);
    }
    NativeFullHandContact::Sample sample(NativeFullHandContact& lane,unsigned side=0,std::uint64_t frame=1,std::uint64_t tick=100,std::uint64_t source_tick=0) {
        const auto producer=lane.producer(side);
        auto tracking=rig.tracking;tracking.tick=tick;tracking.source_tick=source_tick?source_tick:tick;tracking.seconds=tick/1000.;tracking.tracking_revision+=frame;
        driver.publish(tracking,{});
        const auto actual=driver.arm_frozen(side,rig.native_arm[side],identity_frame,tick,frame+1);assert(actual);
        const auto prepared=driver.prepare_contact_arm(side,*actual,identity_frame,tick);assert(prepared);
        const auto geometry=HandPaletteContact::bind(binding->bindings[side]);assert(geometry);
        const auto proxies=geometry->proxies(*actual->raw_joints,identity_frame);assert(proxies);
        NativeFullHandContact::Sample value{};value.owner=owner();value.source=source();value.binding=binding;value.side=side;
        value.frame=frame;value.fit_revision=actual->fit_revision;value.tracking=actual->tracking;value.prepared=*prepared;value.proxies=*proxies;if(producer)value.producer=*producer;return value;
    }
};
WorldTraceQueryResult empty() {WorldTraceQueryResult r{};r.complete=true;r.initial.emplace();r.regular.emplace();return r;}
auto clear_cast=[](const NativeFullHandContact::Sample&,Vec3 from,Vec3 to,double radius) {
    assert(std::abs(from.z-to.z-.0015)<1e-9 && radius>0);return empty();
};
auto sample_ok=[](const NativeFullHandContact::Sample&) {return true;};
NativeFullHandContact::Receipt ready(NativeFullHandContact& lane,Fixture& f,unsigned side=0,std::uint64_t frame=1,std::uint64_t tick=100) {
    lane.observe(owner());assert(lane.publish(f.sample(lane,side,frame,tick),tick));lane.service(owner(),tick+1,clear_cast,sample_ok);
    const auto result=lane.candidate(side,tick+2);assert(result && result->result().ready());return *result;
}
void lifecycle() {
    Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;assert(!lane.publish(f.sample(lane),100));
    auto receipt=ready(lane,f);assert(lane.casts==48 && lane.queries==1 && lane.quiescent());
    assert(receipt.sample().tracking.source_tick==100 && receipt.query_tick()==101);
    assert(!lane.candidate(1,102));assert(!lane.candidate(0,133));
    unsigned original_calls{};
    assert(lane.consume(receipt,102,sample_ok,[&](const auto& current) {
        assert(lane.in_callback() && !lane.quiescent() && lane.current(current,102));
        assert(lane.acquire(current.id(),102));++original_calls;assert(!lane.quiescent());lane.release(current.id());
        assert(!lane.consume(current,102,sample_ok,[](const auto&){assert(false);}));
    }));assert(original_calls==1 && lane.quiescent() && !lane.in_callback());
    const WorldTrackedPose pose{};assert(!lane.resolve(0,pose,2,3,f.binding,102));
    assert(!lane.resolve_checked(0,pose,2,3,f.binding,102,sample_ok));
    assert(lane.publish(f.sample(lane,0,2,110),110));assert(lane.current(receipt,111));
    assert(lane.candidate(0,111)->id()==receipt.id());assert(!lane.current(receipt,133));
    assert(lane.acquire(receipt.id(),111));std::thread refresh([&]{lane.service(owner(),111,clear_cast,sample_ok);});refresh.join();
    assert(lane.candidate(0,112)->id()==receipt.id() && lane.current(receipt,112) && lane.deferred==1);
    assert(lane.acquire(receipt.id(),112));lane.release(receipt.id());
    assert(!lane.quiescent());lane.release(receipt.id());assert(lane.quiescent());
    assert(lane.publish(f.sample(lane,0,3,113),113));lane.service(owner(),114,clear_cast,sample_ok);
    const auto next=lane.candidate(0,115);assert(next && next->id()!=receipt.id());
    assert(!lane.current(receipt,115) && !lane.acquire(receipt.id(),115));
    assert(lane.acquire(next->id(),115));lane.revoke(0);assert(!lane.current(*next,115));assert(!lane.acquire(next->id(),115));
    lane.release(next->id());assert(lane.quiescent());
    lane.stop();assert(!lane.publish(f.sample(lane,0,4,116),116));assert(lane.start());
}
void incompatible_and_validation() {
    for(unsigned change=0;change<6;++change) {
        Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;const auto receipt=ready(lane,f);
        if(change==0)f.rig.tracking.finger_turn[0][0][0]+=.1f;
        if(change==1)f.rig.tracking.mapping_revision++;
        if(change==2)f.rig.tracking.reference_epoch++;
        if(change==3)f.rig.tracking.pose_epoch++;
        if(change==5)f.rig.tracking.grip_value[0]=.2;
        auto sample=f.sample(lane,0,2,110);
        if(change==4)sample.source.broadphase++;
        assert(lane.publish(sample,110));assert(!lane.current(receipt,110) && !lane.candidate(0,110));
    }
    Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;lane.observe(owner());
    auto sample=f.sample(lane);sample.tracking.hand_empty[1]=false;assert(lane.publish(sample,100));
    auto right=f.sample(lane,1);right.tracking.hand_empty[1]=false;assert(!lane.publish(right,100));
    lane.service(owner(),101,clear_cast,sample_ok);assert(lane.candidate(0,102) && !lane.candidate(1,102));
    auto bad=f.sample(lane,0,2,110);bad.proxies[0].centre.x+=.001;assert(!lane.publish(bad,110));
    bad=f.sample(lane,0,3,110);bad.tracking.source_tick=77;assert(!lane.publish(bad,110));
    bad=f.sample(lane,0,4,110);bad.fit_revision++;assert(!lane.publish(bad,110));
}
void cancellation() {
    for(const unsigned failure:{12u,36u}) {
        Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;lane.observe(owner());assert(lane.publish(f.sample(lane),100));unsigned calls{};
        lane.service(owner(),101,[&](const auto&,Vec3,Vec3,double) {auto r=empty();if(++calls==failure)r.complete=false;return r;},sample_ok);
        assert(calls==failure && !lane.candidate(0,102) && lane.quiescent());
    }
    {
        Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;const auto receipt=ready(lane,f);unsigned original{};
        assert(lane.consume(receipt,102,sample_ok,[&](const auto&) {
            ++original;lane.stop();assert(!lane.start() && !lane.quiescent() && lane.in_callback());
            assert(!lane.current(receipt,102) && !lane.acquire(receipt.id(),102));
        }));assert(original==1 && lane.quiescent() && lane.start());
    }

    for(unsigned mode=0;mode<3;++mode) {
        Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;lane.observe(owner());
        assert(lane.publish(f.sample(lane,0),100));assert(lane.publish(f.sample(lane,1),100));
        unsigned calls{},checks{};
        lane.service(owner(),101,[&](const auto&,Vec3,Vec3,double) {
            if(mode==0 && ++calls==60)lane.revoke(1);return empty();
        },[&](const auto&) {
            if(mode==1 && ++checks==4) {lane.clear();return false;}
            return true;
        },[&](const auto&) {
            if(mode==2 && ++checks==12) {lane.clear();return false;}
            return true;
        });
        assert(!lane.candidate(0,102) && !lane.candidate(1,102) && lane.quiescent());
    }
    Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;const auto receipt=ready(lane,f);
    assert(!lane.consume(receipt,102,[&](const auto&) {lane.clear();return true;},[](const auto&){assert(false);}));
    assert(lane.quiescent());
}
constexpr DWORD synthetic_fault=0xe0424242;
void fail(bool cpp) {if(cpp)throw std::runtime_error("synthetic callback");RaiseException(synthetic_fault,0,0,nullptr);}
struct FaultCast {
    std::shared_ptr<int> owned;bool enabled{},cpp{};unsigned calls{};
    WorldTraceQueryResult operator()(const NativeFullHandContact::Sample&,Vec3,Vec3,double) {if(enabled && ++calls==30)fail(cpp);return empty();}
};
struct FaultValidate {
    std::shared_ptr<int> owned;bool enabled{},cpp{};unsigned calls{},at{2};
    bool operator()(const NativeFullHandContact::Sample&) {if(enabled && ++calls==at)fail(cpp);return true;}
};
struct FaultConsumer {
    std::shared_ptr<int> owned;NativeFullHandContact* lane{};bool cpp{};unsigned* original{};
    void operator()(const NativeFullHandContact::Receipt&) {assert(lane->in_callback() && !lane->quiescent());++*original;fail(cpp);}
};
DWORD seh_service(NativeFullHandContact* lane,FaultCast* cast,FaultValidate* validate,FaultValidate* light) {
    __try {lane->service(owner(),101,*cast,*validate,*light);return 0;}
    __except(GetExceptionCode()==synthetic_fault?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return GetExceptionCode();}
}
DWORD seh_consume(NativeFullHandContact* lane,NativeFullHandContact::Receipt* receipt,FaultValidate* validate,FaultConsumer* consumer) {
    __try {lane->consume(*receipt,102,*validate,*consumer);return 0;}
    __except(GetExceptionCode()==synthetic_fault?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return GetExceptionCode();}
}
void exceptions() {
    for(unsigned mode=0;mode<5;++mode)for(const bool cpp:{false,true}) {
        Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;auto owned=std::make_shared<int>(5);
        FaultCast cast{owned,mode==0,cpp};FaultValidate validate{owned,mode==1,cpp,0,2};
        FaultValidate light{owned,mode==2,cpp,0,30};const auto baseline=owned.use_count();
        if(mode<3) {
            lane.observe(owner());assert(lane.publish(f.sample(lane),100));
            if(cpp) {bool caught{};try {lane.service(owner(),101,cast,validate,light);}catch(const std::runtime_error&){caught=true;}assert(caught);}
            else assert(seh_service(&lane,&cast,&validate,&light)==synthetic_fault);
            assert(!lane.candidate(0,102));
        } else {
            auto receipt=ready(lane,f);unsigned original{};FaultConsumer consumer{owned,&lane,cpp,&original};
            validate.enabled=mode==3;validate.at=1;consumer.cpp=cpp;
            if(cpp) {bool caught{};try {lane.consume(receipt,102,validate,consumer);}catch(const std::runtime_error&){caught=true;}assert(caught);}
            else assert(seh_consume(&lane,&receipt,&validate,&consumer)==synthetic_fault);
            assert(original==(mode==4?1u:0u));assert(owned.use_count()==baseline+1);
        }
        assert(owned.use_count()==baseline && lane.quiescent() && !lane.in_callback());
        lane.clear();assert(f.binding.use_count()==1);const auto receipt=ready(lane,f,0,2,110);assert(lane.current(receipt,112));
    }
}
void concurrent_drain() {
    Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;const auto receipt=ready(lane,f);
    std::mutex mutex;std::condition_variable condition;bool entered{},resume{};unsigned originals{};
    std::thread consumer([&] {
        assert(lane.consume(receipt,102,sample_ok,[&](const auto&) {
            std::unique_lock<std::mutex> lock(mutex);entered=true;condition.notify_one();condition.wait(lock,[&]{return resume;});
            assert(!lane.current(receipt,102));++originals;
        }));
    });
    {std::unique_lock<std::mutex> lock(mutex);condition.wait(lock,[&]{return entered;});}
    lane.stop();assert(!lane.quiescent() && !lane.start());assert(!lane.acquire(receipt.id(),102));
    assert(!lane.consume(receipt,102,sample_ok,[](const auto&){assert(false);}));
    {std::lock_guard<std::mutex> lock(mutex);resume=true;}condition.notify_one();consumer.join();
    assert(originals==1 && lane.quiescent() && lane.start());
    const auto next=ready(lane,f,0,2,110);assert(lane.current(next,112));
}
void pinned_original_interleaving() {
    Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;const auto receipt=ready(lane,f);
    std::mutex mutex;std::condition_variable condition;bool entered{},staged{};unsigned original_calls{};
    std::thread original([&] {
        assert(lane.consume(receipt,102,sample_ok,[&](const auto& current) {
            ++original_calls;
            {std::unique_lock<std::mutex> lock(mutex);entered=true;condition.notify_one();condition.wait(lock,[&]{return staged;});}
            assert(lane.current(current,112) && lane.deferred==1);
            assert(lane.acquire(current.id(),112));
            assert(lane.current(current,112) && !lane.quiescent());lane.release(current.id());
        }));
    });
    {std::unique_lock<std::mutex> lock(mutex);condition.wait(lock,[&]{return entered;});}
    assert(lane.publish(f.sample(lane,0,2,110),110));lane.service(owner(),111,clear_cast,sample_ok);
    assert(lane.current(receipt,112) && lane.candidate(0,112)->id()==receipt.id() && lane.deferred==1);
    {std::lock_guard<std::mutex> lock(mutex);staged=true;}condition.notify_one();original.join();
    assert(original_calls==1 && lane.quiescent());
    assert(lane.publish(f.sample(lane,0,3,113),113));lane.service(owner(),114,clear_cast,sample_ok);
    const auto next=lane.candidate(0,115);assert(next && next->id()!=receipt.id());
    assert(!lane.current(receipt,115) && !lane.acquire(receipt.id(),115));
    assert(lane.acquire(next->id(),115));
    f.rig.tracking.finger_turn[0][0][0]+=.1f;
    assert(lane.publish(f.sample(lane,0,4,116),116));
    assert(!lane.current(*next,116) && !lane.acquire(next->id(),116) && !lane.quiescent());
    lane.release(next->id());assert(lane.quiescent());
}
void bounded_budget() {
    Fixture f;NativeFullHandContact lane(fake_clock);assert(lane.budget_ns==3000000);
    lane.budget_ns=1;lane.observe(owner());assert(lane.publish(f.sample(lane),100));
    lane.service(owner(),101,clear_cast,sample_ok);
    assert(lane.budget_rejected==1 && lane.rejected==1 && !lane.candidate(0,102) && lane.quiescent());
}

void rejected_publication_frontier() {
    for(unsigned mismatch=0;mismatch<10;++mismatch) {
        Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;const auto old=ready(lane,f);
        assert(lane.acquire(old.id(),102));auto bad=f.sample(lane,0,2,110);
        if(mismatch==0)bad.proxies[4].centre.x+=.001;
        if(mismatch==1)bad.tracking.raw_grip[0].p.x+=.001;
        if(mismatch==2)bad.tracking.raw_grip[0].q=body::rot_z(.1);
        if(mismatch==3)--bad.tracking.tick;
        if(mismatch==4)--bad.tracking.source_tick;
        if(mismatch==5)++bad.tracking.reference_epoch;
        if(mismatch==6)bad.tracking.finger_turn[0][0][0]+=.1f;
        if(mismatch==7)++bad.tracking.tracking_revision;
        if(mismatch==8)++bad.tracking.native_frame;
        if(mismatch==9)bad.tracking.finger_axis[0][0][0][0]=1;
        assert(!lane.publish(bad,110));assert(!lane.current(old,110) && !lane.candidate(0,110) && !lane.acquire(old.id(),110));
        assert(!lane.quiescent());lane.release(old.id());assert(lane.quiescent());
    }
    {
        Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;const auto old=ready(lane,f);
        auto root=identity_frame;root[0]=.025f;root[1]=-.015f;
        const auto c=static_cast<float>(std::cos(.08)),sn=static_cast<float>(std::sin(.08));
        root[3]=c;root[4]=sn;root[6]=-sn;root[7]=c;
        auto unmapped=f.sample(lane,0,2,110);f.driver.note_root(root);
        const auto moved=f.driver.arm_frozen(0,f.rig.native_arm[0],identity_frame,110,3);assert(moved);
        unmapped.prepared=f.driver.prepare_contact_arm(0,*moved,identity_frame,110);assert(unmapped.prepared);
        const auto shape=HandPaletteContact::bind(f.binding->bindings[0]);assert(shape);
        const auto proxies=shape->proxies(*moved->raw_joints,identity_frame);assert(proxies);unmapped.proxies=*proxies;
        const auto& captured=unmapped.prepared->original();
        assert(length(captured.world_from_solve.p)>.01);
        assert(!lane.publish(unmapped,110) && !lane.current(old,110));
        unmapped.tracking.raw_grip[0]=body::mul(captured.world_from_solve,captured.tracking.raw_grip[0]);
        unmapped.producer=*lane.producer(0);unmapped.prepared=f.driver.prepare_contact_arm(0,*moved,identity_frame,110);assert(unmapped.prepared);
        assert(lane.publish(unmapped,110));lane.service(owner(),111,clear_cast,sample_ok);assert(lane.candidate(0,112));
    }
    {
        Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;
        lane.observe(owner());auto older=f.sample(lane,0,1,100);older.proxies[0].radius=NAN;
        const auto current=ready(lane,f,0,3,110);
        assert(!lane.publish(older,111));assert(lane.current(current,111) && lane.candidate(0,111)->id()==current.id());
    }
    {
        Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;auto replacement_owner=owner();++replacement_owner.spawn;
        lane.observe(replacement_owner);auto fresh=f.sample(lane,0,2,110);fresh.owner=replacement_owner;fresh.source.spawn=replacement_owner.spawn;
        assert(lane.publish(fresh,110));lane.service(replacement_owner,111,clear_cast,sample_ok);
        const auto current=lane.candidate(0,112);assert(current);
        auto foreign=f.sample(lane,0,3,113);foreign.proxies[0].centre.x+=.001;
        assert(!lane.publish(foreign,113));assert(lane.current(*current,113) && lane.owner()==replacement_owner);
    }
}
void capture_gate_order() {
    Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;lane.observe(owner());
    assert(lane.publish(f.sample(lane,0),100) && lane.publish(f.sample(lane,1),100));
    std::array<unsigned,2> full{},light{};std::array<bool,2> captured{};
    lane.service(owner(),101,clear_cast,[&](const auto& sample) {++full[sample.side];captured[sample.side]=true;return true;},
        [&](const auto& sample) {assert(captured[sample.side]);++light[sample.side];return captured[sample.side];});
    assert(full[0]==2 && full[1]==2 && light[0]>=48 && light[1]>=48);
    assert(lane.candidate(0,102) && lane.candidate(1,102));
    lane.clear();lane.observe(owner());assert(lane.publish(f.sample(lane,0,2,110),110));unsigned captures{};
    lane.service(owner(),111,clear_cast,[&](const auto&) {if(++captures==2)lane.budget_ns=1;return true;});
    assert(captures==2 && lane.budget_rejected==1 && !lane.candidate(0,112));
}

void producer_receipt_frontier() {
    for(unsigned action=0;action<3;++action) {
        Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;lane.observe(owner());
        auto previous=f.sample(lane,0,1,100);assert(previous.producer.epoch);
        if(action==0)lane.clear();
        if(action==1)lane.revoke(0);
        if(action==2) {lane.stop();assert(lane.start());}
        lane.observe(owner());assert(!lane.publish(previous,100));
        auto current=f.sample(lane,0,1,100);assert(lane.publish(current,100));lane.service(owner(),101,clear_cast,sample_ok);
        const auto receipt=lane.candidate(0,102);assert(receipt);
        assert(!lane.publish(previous,102) && lane.current(*receipt,102));
    }
}
void fresh_commit_clock() {
    for(unsigned action=0;action<3;++action) {
        clock_value=1000;Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;
        if(action==0) {
            const auto receipt=ready(lane,f,0,1,1000);unsigned originals{};
            assert(!lane.consume(receipt,1002,[&](const auto&) {clock_value=1033;return true;},[&](const auto&){++originals;}));
            assert(originals==0 && !lane.acquire(receipt.id(),1002));
        } else {
            lane.observe(owner());assert(lane.publish(f.sample(lane,0,1,1000),1000));unsigned full_calls{};
            lane.service(owner(),1001,clear_cast,[&](const auto&) {if(++full_calls==2 && action==1)clock_value=1033;return true;},
                [&](const auto&) {if(action==2 && lane.casts==48 && full_calls==1)clock_value=1033;return true;});
            assert(!lane.candidate(0,1002));
        }
        assert(lane.quiescent() && !lane.in_callback());clock_value=0;
    }
}
void committed_continuation() {
    for(unsigned action=0;action<5;++action) {
        Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;const auto receipt=ready(lane,f);
        std::mutex mutex;std::condition_variable condition;bool entered{},retired{};unsigned originals{};
        std::thread original([&] {
            assert(lane.consume(receipt,102,sample_ok,[&](const auto& current) {
                ++originals;
                {std::unique_lock<std::mutex> lock(mutex);entered=true;condition.notify_one();condition.wait(lock,[&]{return retired;});}
                assert(!lane.current(current,102) && !lane.acquire(current.id(),102));
                assert(NativeFullHandContact::acquire_attachment(&lane,current.id()));
                assert(!lane.quiescent());NativeFullHandContact::release_attachment(&lane,current.id());
            }));
            assert(!NativeFullHandContact::acquire_attachment(&lane,receipt.id()));
        });
        {std::unique_lock<std::mutex> lock(mutex);condition.wait(lock,[&]{return entered;});}
        if(action==0)lane.clear();
        if(action==1)lane.revoke(0);
        if(action==2)lane.stop();
        if(action==3) {
            assert(lane.publish(f.sample(lane,0,2,100),100));
            lane.service(owner(),101,[](const auto&,Vec3,Vec3,double) {auto result=empty();result.complete=false;return result;},sample_ok);
        }
        if(action==4) {
            assert(lane.publish(f.sample(lane,0,2,100),100));
            FaultCast cast{std::make_shared<int>(5),true,false};FaultValidate full{},light{};
            assert(seh_service(&lane,&cast,&full,&light)==synthetic_fault);
        }
        assert(!lane.quiescent() && !lane.acquire(receipt.id(),102));
        assert(!NativeFullHandContact::acquire_attachment(&lane,receipt.id()));
        {std::lock_guard<std::mutex> lock(mutex);retired=true;}condition.notify_one();original.join();
        assert(originals==1 && lane.quiescent());
        assert(!NativeFullHandContact::acquire_attachment(&lane,receipt.id()));
        assert(lane.start());
    }
    Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;const auto receipt=ready(lane,f);
    assert(!lane.consume(receipt,102,[&](const auto&) {
        lane.clear();assert(!NativeFullHandContact::acquire_attachment(&lane,receipt.id()));return true;
    },[](const auto&){assert(false);}));
    assert(lane.quiescent());
}

void target_witness_retention() {
    for(const unsigned mode:{0u,1u,2u,3u,4u,5u,7u,8u,6u}) {
        Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;lane.observe(owner());
        const std::uint64_t tick=1000;assert(lane.publish(f.sample(lane,0,1,tick),tick));
        unsigned casts{},full_calls{},raw_revision{},final_revision{};bool delayed{};
        const auto cast=[&](const auto&,Vec3,Vec3 to,double radius) {
            ++casts;
            if(mode==8 && casts==24)++raw_revision;
            if(mode==1 && casts==25)++raw_revision;
            auto result=empty();
            if(casts==1 || casts==25) {
                result.regular->count=1;auto& hit=result.regular->hits[0];
                hit.normal={1,0,0};hit.point=to-hit.normal*(radius-(casts==1?.004:0.));hit.fraction=.5;
                hit.body_handle=casts==1?0x12000003u:0x12000004u;hit.body=hit.body_handle&0xffffffu;
                hit.shape_key=casts==1?7u:8u;hit.entity=0x3100000;
            }
            return result;
        };
        const auto full=[&](const auto&) {
            if(++full_calls==2) {if(mode==2)++raw_revision;if(mode==3)++final_revision;}
            return true;
        };
        const auto watch=[&](const auto& sample,std::uint32_t handle,std::uint32_t key)->std::optional<FullHandTargetWitness::Witness> {
            if(full_calls==2 && !delayed) {
                delayed=true;if(mode==5)lane.budget_ns=1;if(mode==6)clock_value=tick+33;
            }
            if(mode==7 && full_calls==2)return {};
            FullHandTargetWitness::Witness result{};result.source=sample.source;result.handle=handle;result.contact_key=key;
            result.pose[0]=handle==0x12000003u?raw_revision:final_revision;return result;
        };
        if(mode==4)lane.service(owner(),tick+1,cast,full,sample_ok);
        else lane.service(owner(),tick+1,cast,full,sample_ok,watch);
        const auto receipt=lane.candidate(0,tick+2);
        if(mode==0) {
            assert(receipt && receipt->result().contact && casts==48 && full_calls==2);
            const auto& result=receipt->result();assert(result.raw_targets.size()==1 && result.final_targets.size()==1);
            assert(result.raw_targets[0].handle==0x12000003u && result.raw_targets[0].contact_key==7);
            assert(result.final_targets[0].handle==0x12000004u && result.final_targets[0].contact_key==8);
        } else {
            if(receipt || lane.rejected!=1)std::cerr<<"witness rejection mode="<<mode<<" rejected="<<lane.rejected<<" casts="<<casts<<" full="<<full_calls<<"\n";
            assert(!receipt && lane.rejected==1);
            if(mode==5)assert(lane.budget_rejected==1);
        }
        assert(lane.quiescent() && !lane.in_callback());clock_value=0;
    }
}

static_assert(std::is_trivially_destructible_v<WorldTraceQueryResult>);
struct WatchFaultCast {
    unsigned calls{};
    WorldTraceQueryResult operator()(const NativeFullHandContact::Sample&,Vec3,Vec3 to,double radius) {
        auto result=empty();
        if(++calls==1 || calls==25) {
            result.regular->count=1;auto& hit=result.regular->hits[0];hit.normal={1,0,0};hit.point=to-hit.normal*radius;
            hit.fraction=.5;hit.body_handle=0x12000003u;hit.body=3;hit.shape_key=7;hit.entity=0x3100000;
        }
        return result;
    }
};
struct FaultTargetWatch {
    std::shared_ptr<int> owned;
    NativeFullHandContact* lane{};
    bool final{},cpp{};
    std::optional<FullHandTargetWitness::Witness> operator()(const NativeFullHandContact::Sample& sample,std::uint32_t handle,std::uint32_t key) {
        assert(lane->in_callback() && !lane->quiescent());
        if(!final || lane->casts>=25)fail(cpp);
        FullHandTargetWitness::Witness value{};value.source=sample.source;value.handle=handle;value.contact_key=key;return value;
    }
};
DWORD seh_watch(NativeFullHandContact* lane,WatchFaultCast* cast,FaultTargetWatch* watch) {
    __try {lane->service(owner(),101,*cast,sample_ok,sample_ok,std::move(*watch));return 0;}
    __except(GetExceptionCode()==synthetic_fault?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return GetExceptionCode();}
}
void watch_exception_cleanup() {
    for(const bool final:{false,true})for(const bool cpp:{false,true}) {
        Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;lane.observe(owner());
        assert(lane.publish(f.sample(lane),100));
        WatchFaultCast cast;auto retained=std::make_shared<int>(5);std::weak_ptr<int> lifetime=retained;
        FaultTargetWatch watch{std::move(retained),&lane,final,cpp};assert(!lifetime.expired());
        if(cpp) {
            bool caught{};try {lane.service(owner(),101,cast,sample_ok,sample_ok,std::move(watch));}
            catch(const std::runtime_error&) {caught=true;}assert(caught);
        } else assert(seh_watch(&lane,&cast,&watch)==synthetic_fault);
        assert(lifetime.expired() && !watch.owned);
        assert(lane.casts==(final?25u:1u) && lane.quiescent() && !lane.in_callback() && !lane.candidate(0,102));
        lane.clear();assert(f.binding.use_count()==1);
        const auto receipt=ready(lane,f,0,2,110);assert(lane.current(receipt,112));
    }
}

void attachment_scope_authority() {
    Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;const auto receipt=ready(lane,f);
    assert(!NativeFullHandContact::acquire_attachment(&lane,receipt.id()));
    assert(lane.acquire(receipt.id(),102));assert(!NativeFullHandContact::acquire_attachment(&lane,receipt.id()));lane.release(receipt.id());
    assert(lane.consume(receipt,102,[&](const auto&) {
        assert(!NativeFullHandContact::acquire_attachment(&lane,receipt.id()));return true;
    },[&](const auto& current) {
        assert(!NativeFullHandContact::acquire_attachment(&lane,current.id()+1));
        std::thread other([&] {assert(!NativeFullHandContact::acquire_attachment(&lane,current.id()));});other.join();
        assert(NativeFullHandContact::acquire_attachment(&lane,current.id()));NativeFullHandContact::release_attachment(&lane,current.id());
        lane.clear();assert(!lane.acquire(current.id(),102));
        assert(NativeFullHandContact::acquire_attachment(&lane,current.id()));NativeFullHandContact::release_attachment(&lane,current.id());
    }));
    assert(!NativeFullHandContact::acquire_attachment(&lane,receipt.id()));
    assert(lane.attachment_continued==2 && lane.attachment_rejected==6 && lane.quiescent() && !lane.in_callback());
}

void admitted_publication_ticket() {
    for(unsigned action=0;action<4;++action) {
        Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;const auto old=ready(lane,f);
        f.rig.tracking.has_finger[0]=true;f.rig.tracking.finger_turn[0][0][0]=.2f;
        auto sample=f.sample(lane,0,2,110);const auto input=sample.producer;assert(lane.producer_current(input));
        NativeFullHandContact::ProducerReceipt admitted{};
        assert(lane.publish(sample,110,&admitted));
        const auto actual=lane.producer(0);assert(actual && lane.producer_current(admitted));
        assert(admitted.owner==actual->owner && admitted.epoch==actual->epoch && admitted.side_epoch==actual->side_epoch && admitted.side==actual->side);
        assert(admitted.side_epoch==input.side_epoch+1 && !lane.producer_current(input) && !lane.current(old,110));
        if(action==0)lane.clear();
        if(action==1)lane.revoke(0);
        if(action==2)lane.stop();
        if(action==3) {
            auto malformed=f.sample(lane,0,3,111);malformed.proxies[0].radius=NAN;
            auto output=admitted;assert(!lane.publish(malformed,111,&output));
            assert(output.epoch==0 && output.side_epoch==0 && output.side==0 && !output.owner.valid());
        }
        assert(!lane.producer_current(admitted));
    }
}

void duplicate_raw_publication_ticket() {
    for(unsigned side=0;side<2;++side) {
        Fixture f;NativeFullHandContact lane(fake_clock);lane.budget_ns=0;lane.observe(owner());
        const auto first=f.sample(lane,side);auto duplicate=first;
        NativeFullHandContact::ProducerReceipt admitted{};
        assert(lane.publish(first,100,&admitted));
        const auto first_guard=admitted,duplicate_guard=duplicate.producer;
        const auto guards_current=[&] {return lane.producer_current(first_guard) && lane.producer_current(duplicate_guard);};
        auto duplicate_output=admitted;
        assert(!lane.publish(duplicate,100,&duplicate_output));
        assert(!duplicate_output.owner.valid() && duplicate_output.epoch==0 && duplicate_output.side_epoch==0 && duplicate_output.side==0);
        assert(guards_current() && lane.producer_current(first.producer));
        assert(lane.casts==0 && lane.queries==0 && lane.batches==0);
        lane.service(owner(),101,clear_cast,sample_ok);
        assert(lane.casts==48 && lane.queries==1 && lane.batches==1 && guards_current());
        const auto response=lane.candidate(side,102);assert(response);
        duplicate_output=admitted;
        assert(!lane.publish(duplicate,102,&duplicate_output) && duplicate_output.epoch==0 && guards_current());
        lane.service(owner(),102,clear_cast,sample_ok);
        assert(lane.casts==48 && lane.queries==1 && lane.batches==1 && lane.current(*response,102));
        auto malformed=duplicate;malformed.proxies[0].radius=NAN;
        assert(!lane.publish(malformed,102,&duplicate_output));
        assert(duplicate_output.epoch==0 && !guards_current() && !lane.current(*response,102));
        lane.service(owner(),102,clear_cast,sample_ok);
        assert(lane.casts==48 && lane.queries==1 && lane.batches==1 && lane.quiescent());
    }
}

void publish_contact(NativeFullHandContact& lane,Fixture& fixture,unsigned side=0,std::uint64_t frame=1,std::uint64_t tick=100,std::uint64_t source_tick=0) {
    lane.observe(owner());lane.budget_ns=0;
    assert(lane.publish(fixture.sample(lane,side,frame,tick,source_tick),tick));
    unsigned calls{},validations{},watches{};
    auto cast=[&](const auto&,Vec3,Vec3 to,double radius) {
        auto query=empty();
        if(++calls==1 || calls==25) {
            query.regular->count=1;auto& hit=query.regular->hits[0];
            hit.normal={1,0,0};hit.point=to-hit.normal*(radius-(calls==1?.004:0.));hit.fraction=.5;
            hit.body_handle=0x12000003;hit.body=3;hit.shape_key=0xffffffffu;
        }
        return query;
    };
    auto validate=[&](const auto&) {++validations;(void)lane.diagnostics();return true;};
    auto watch=[&](const auto& sample,std::uint32_t handle,std::uint32_t key)->std::optional<FullHandTargetWitness::Witness> {
        ++watches;FullHandTargetWitness::Witness value{};value.source=sample.source;value.handle=handle;value.contact_key=key;return value;
    };
    lane.service(owner(),tick+1,cast,validate,sample_ok,watch);
    assert(calls==48 && validations==2 && watches==7 && lane.quiescent());
}

NativeFullHandContact::ContactLifecycle last_contact(const NativeFullHandContact& lane) {
    const auto diagnostic=lane.diagnostics();assert(diagnostic.count);
    return diagnostic.records[(diagnostic.next+31)%32];
}

void contact_diagnostic_lifecycle() {
    using Reason=NativeFullHandContact::ContactReason;
    static_assert(std::is_trivially_copyable_v<NativeFullHandContact::ContactDiagnostics>);
    static_assert(sizeof(NativeFullHandContact::ContactDiagnostics)<=8192);
    clock_value=0;
    Fixture fixture;NativeFullHandContact lane(fake_clock);
    lane.observe(owner());assert(!lane.candidate(0,100) && lane.diagnostics().count==0);
    ready(lane,fixture);assert(lane.diagnostics().published==0);
    lane.clear();publish_contact(lane,fixture,0,2,110);
    const auto published=last_contact(lane);
    assert(published.receipt && published.side==0 && published.frame==2 && published.epoch && published.tracking_revision);
    assert(published.tracking_tick==110 && published.source_tick==110 && published.query_tick==111 && published.publish_tick==111);
    assert(published.selected_tick==0 && published.lookup.reason==Reason::none && published.destruction.reason==Reason::none);
    assert(lane.candidate(0,112));assert(last_contact(lane).selected_tick==112);
    assert(lane.candidate(0,113));assert(last_contact(lane).selected_tick==112);
    assert(!lane.candidate(0,143));const auto expired=last_contact(lane);
    assert(expired.lookup.tick==143 && expired.lookup.reason==Reason::tracking_age && expired.lookup.latest_frame==2);
    assert(!lane.candidate(0,150));assert(last_contact(lane).lookup.tick==143);
    clock_value=151;lane.clear(Reason::scope_unqualified);
    const auto cleared=last_contact(lane);
    assert(cleared.destruction.reason==Reason::scope_unqualified && cleared.destruction.tick==151 && cleared.lookup.tick==143);
    lane.stop();assert(lane.start());assert(last_contact(lane).destruction.tick==151 && lane.diagnostics().published==1);
    assert(!lane.candidate(0,151));assert(lane.diagnostics().count==1);clock_value=0;
    publish_contact(lane,fixture,0,3,160,150);
    assert(lane.candidate(0,182));assert(!lane.candidate(0,183));
    assert(last_contact(lane).lookup.reason==Reason::source_age && last_contact(lane).lookup.tick==183);
    assert(last_contact(lane).lookup.latest_tracking_tick==160 && last_contact(lane).lookup.latest_source_tick==150);
}

void contact_diagnostic_destructions() {
    using Reason=NativeFullHandContact::ContactReason;
    for(unsigned action=0;action<11;++action) {
        clock_value=0;Fixture fixture;NativeFullHandContact lane(fake_clock);publish_contact(lane,fixture);
        const auto original=last_contact(lane);clock_value=110;
        Reason expected{};
        if(action==0) {lane.revoke(0);expected=Reason::explicit_revoke;}
        if(action==1) {lane.stop();expected=Reason::lifecycle_stop;}
        if(action==2) {assert(lane.start());expected=Reason::lifecycle_start;}
        if(action==3) {auto changed=owner();++changed.spawn;lane.observe(changed);expected=Reason::owner_changed;}
        if(action==4) {
            auto malformed=fixture.sample(lane,0,2,110);malformed.proxies[0].radius=NAN;
            assert(!lane.publish(malformed,110));expected=Reason::publish_invalid;
        }
        if(action==5) {
            fixture.rig.tracking.grip_value[0]=.2;fixture.rig.tracking.trigger[0]=.3;
            fixture.rig.tracking.has_finger[0]=true;fixture.rig.tracking.finger_turn[0][0][0]=.1f;
            assert(lane.publish(fixture.sample(lane,0,2,110),110));expected=Reason::publish_incompatible;
            const auto bits=last_contact(lane).destruction.mismatch_bits;
            assert((bits&NativeFullHandContact::different_grip) && (bits&NativeFullHandContact::different_trigger));
            assert((bits&NativeFullHandContact::different_finger_presence) && (bits&NativeFullHandContact::different_finger_turn));
        }
        if(action==6) {
            assert(lane.publish(fixture.sample(lane,0,2,110),110));
            lane.service(owner(),111,clear_cast,sample_ok);expected=Reason::replaced_clear;
        }
        if(action==7) {
            assert(lane.publish(fixture.sample(lane,0,2,110),110));
            lane.service(owner(),111,clear_cast,[](const auto&) {return false;});expected=Reason::batch_rejected;
            assert(last_contact(lane).destruction.work_mask==1);
        }
        if(action==8) {
            assert(lane.publish(fixture.sample(lane,0,2,110),110));
            bool caught{};try {lane.service(owner(),111,[](const auto&,Vec3,Vec3,double)->WorldTraceQueryResult {throw std::runtime_error("diagnostic");},sample_ok);}
            catch(const std::runtime_error&) {caught=true;}assert(caught);expected=Reason::service_exception;
        }
        if(action==9) {
            publish_contact(lane,fixture,0,2,110);expected=Reason::replaced_contact;
        }
        if(action==10) {
            assert(lane.publish(fixture.sample(lane,0,2,110),110));
            FaultCast cast{};FaultValidate validate{},light{};validate.enabled=true;validate.at=1;
            assert(seh_service(&lane,&cast,&validate,&light)==synthetic_fault);expected=Reason::service_exception;
        }
        const auto diagnostic=lane.diagnostics();
        const auto found=std::find_if(diagnostic.records.begin(),diagnostic.records.end(),[&](const auto& record) {return record.receipt==original.receipt;});
        assert(found!=diagnostic.records.end() && found->destruction.reason==expected && found->destruction.tick>=110);
        assert(lane.quiescent() && !lane.in_callback());
    }
    clock_value=0;
}

void contact_diagnostic_paired_failure() {
    Fixture fixture;NativeFullHandContact lane(fake_clock);clock_value=0;publish_contact(lane,fixture);
    assert(lane.publish(fixture.sample(lane,0,2,110),110));
    assert(lane.publish(fixture.sample(lane,1,2,110),110));
    unsigned calls{};
    auto cast=[&](const auto&,Vec3,Vec3,double) {++calls;return empty();};
    lane.service(owner(),111,cast,[](const auto& sample) {return sample.side==0;});
    assert(calls==48 && !lane.candidate(0,112) && !lane.candidate(1,112));
    const auto destroyed=last_contact(lane).destruction;
    assert(destroyed.reason==NativeFullHandContact::ContactReason::batch_rejected && destroyed.work_mask==3);
    assert(lane.rejected==2 && lane.quiescent());
    const auto before=lane.diagnostics();
    lane.service(owner(),112,cast,sample_ok);assert(lane.diagnostics().published==before.published);
    assert(last_contact(lane).destruction.tick==destroyed.tick);
}

void contact_diagnostic_ring() {
    Fixture fixture;NativeFullHandContact lane(fake_clock);clock_value=0;
    for(unsigned ordinal=0;ordinal<33;++ordinal)publish_contact(lane,fixture,ordinal%2,ordinal+1,100+ordinal*10);
    const auto diagnostic=lane.diagnostics();
    assert(diagnostic.count==32 && diagnostic.next==1 && diagnostic.published==33 && diagnostic.overwritten==1);
    assert(diagnostic.records[0].frame==33 && last_contact(lane).frame==33);
    for(const auto& record:diagnostic.records)assert(record.receipt && record.frame!=1);
    const auto retained=diagnostic.published;lane.clear();lane.stop();assert(lane.start());
    assert(lane.diagnostics().published==retained && lane.diagnostics().overwritten==1);
}

}
int main() {lifecycle();incompatible_and_validation();cancellation();exceptions();concurrent_drain();pinned_original_interleaving();bounded_budget();rejected_publication_frontier();capture_gate_order();producer_receipt_frontier();fresh_commit_clock();committed_continuation();target_witness_retention();watch_exception_cleanup();attachment_scope_authority();admitted_publication_ticket();duplicate_raw_publication_ticket();contact_diagnostic_lifecycle();contact_diagnostic_destructions();contact_diagnostic_paired_failure();contact_diagnostic_ring();std::cout<<"native full hand receipt mailbox check passed\n";}

