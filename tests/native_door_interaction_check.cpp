#include "../src/native_door_interaction.hpp"
#include <cassert>
#include <thread>
#include <iostream>
#include <map>

extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*) {return MH_ERROR_UNSUPPORTED_FUNCTION;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {return MH_ERROR_NOT_CREATED;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {return MH_ERROR_NOT_CREATED;}

namespace d2vr {
struct NativeDoorEventAdmissionFixture {
    using Event=NativeDoorEventAdmission;
    using Request=Event::Request;
    static Event& owner() {return Event::process_owner();}
    static void enable_for_fixture() {
        auto& event=owner();
        for(auto& installed:event.installed_)installed=true;
        event.stopping_=false;
    }
    static void lock_with_open_receipt(const Request& request) {
        auto& event=owner();
        event.state_.lock();
        event.receipts_[0]={request,71,event.revocation_epoch_.load(),{},Event::Action::Open,true,false,true,true,true};
    }
    static void unlock() {owner().state_.unlock();}
    static bool has_receipt(std::uint64_t ownership) {
        auto& event=owner();
        std::lock_guard<std::mutex> guard(event.state_);
        for(const auto& receipt:event.receipts_)if(receipt.live && receipt.request.operation.ownership==ownership)return true;
        return false;
    }
    static void reset_uninstalled() {
        auto& event=owner();
        for(auto& installed:event.installed_)installed=false;
        event.stopping_=true;event.stop_complete_=true;
    }
    static void flight(unsigned value) {owner().in_flight_=value;}
};

struct NativeDoorInteractionFixture {
    using Controller=NativeDoorInteraction;
    using Lane=Controller::Lane;
    using Contact=Controller::Contact;
    using Request=Controller::Request;
    using AnimationStatus=NativeDoorAnimation::Status;
    using Event=NativeDoorEventAdmission;
    static Request request() {
        Request result{};
        result.game=0x24000;result.manager=result.game+0x1e8968;result.component=0x26000;result.door=0x27000;
        result.component_generation=50;result.door_spawn=0x1001;result.player_spawn=0x1002;result.part_spawn=0x1003;
        auto& operation=result.operation;
        operation.ownership=10;operation.world=20;operation.player_generation=30;operation.part_generation=40;
        operation.player=0x20000;operation.part=0x21000;operation.animation=0x22000;operation.container=operation.animation+0x338;
        operation.binding=0x23000;operation.opening_clip=1;operation.closing_clip=2;
        return result;
    }
    static Contact contact() {
        Contact result{};result.contact_token=91;result.observed_tick=1000;result.owner.door=0x27000;
        result.owner.player=0x2100000;result.owner.game=0x2000000;result.owner.world=0x2700000;
        result.clip.owner=result.owner;result.skeleton.owner=result.owner;result.surface.entity=result.owner.door;
        result.clip.content.duration=1.f;result.clip.content.fps=30.f;
        auto& publication=result.publication;auto& source=publication.source;
        source.base=0x140000000;source.game=result.owner.game;source.player=result.owner.player;source.camera=0x2200000;source.camera_controller=0x2300000;
        source.physics=0x2400000;source.delegate=0x2500000;source.controller=0x2600000;source.world=result.owner.world;source.pool=0x2800000;source.body=0x28000b0;
        source.handle=0xab000001;source.spawn=0x10001;source.body_count=4;source.broadphase=3;source.body_flags=1;
        source.camera_identity={7,0x4a};source.physics_identity={9,0x4a};source.component_list_data=0x2900000;source.component_list_count=4;source.component_list_capacity=4;
        publication.mapping_axes={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};
        publication.tick=1000;publication.source_tick=1000;publication.pose_epoch=1;publication.reference_epoch=2;publication.revision=3;
        publication.hands=3;publication.velocity_ready=true;result.empty.epoch=1;
        result.geometry={.2f,.5f,.1f,PhysicalDoorController::Pressure::Opening};return result;
    }
    static NativeDoorOwner::State state(unsigned current) {
        NativeDoorOwner::State result{};result.current=current;result.next=current;result.usable=1;result.locker=0x3fff0010u;return result;
    }
    static AnimationStatus owned(std::uint64_t revision=1) {
        AnimationStatus result{};result.published=true;result.input_owned=true;result.evaluated=true;result.input_generation=11;
        result.desired_revision=revision;result.evaluated_revision=revision;result.duration_seconds=1.f;return result;
    }
    static Event::Status emitted() {return {Event::Stage::Emitted,70};}
    static Lane opening() {
        Lane lane{};lane.valid=true;lane.request=request();lane.contact=contact();lane.last_tick=1000;
        lane.policy.active=true;lane.policy.phase=PhysicalDoorController::Phase::Opening;
        lane.policy.owner_token=10;lane.policy.contact_identity=91;lane.policy.commanded_seconds=.2f;
        assert(NativeDoorProgress::begin(lane.progress,lane.request,999));
        assert(NativeDoorProgress::poll(lane.progress,emitted(),{},owned(),1000,500,500)==NativeDoorProgress::Result::Ready);
        return lane;
    }
    static void test_contact_and_mapping() {
        const Controller::Curve curve{30.f,1.f};
        assert(Controller::angle_seconds(&curve,0.f)==0.f);
        assert(std::abs(Controller::angle_seconds(&curve,Controller::right_angle)-17.f/30.f)<.000001f);
        assert(std::isnan(Controller::angle_seconds(&curve,-.01f)));
        assert(std::isnan(Controller::angle_seconds(&curve,Controller::right_angle+.01f)));
        assert(std::isnan(Controller::angle_seconds(nullptr,0.f)));
        assert(Controller::initial_state(state(0)) && Controller::initial_state(state(2)));
        assert(!Controller::initial_state(state(1)));auto transition=state(0);transition.next=1;assert(!Controller::initial_state(transition));
        Controller::Contacts contacts{};contacts[0]=contact();assert(Controller::contact_valid(*contacts[0],0,1000));
        Lane lane{};assert(Controller::choose(contacts,lane,1000));
        lane.valid=true;lane.contact=*contacts[0];contacts[1]=contact();contacts[1]->side=1;
        assert(Controller::choose(contacts,lane,1000)==&*contacts[0]);
        contacts[1]->geometry.pressure=PhysicalDoorController::Pressure::Closing;assert(!Controller::choose(contacts,lane,1000));
        contacts[1].reset();contacts[0]->empty.epoch++;assert(!Controller::contact_valid(*contacts[0],0,1000));
        contacts[0]=contact();contacts[0]->owner.door++;assert(!Controller::contact_valid(*contacts[0],0,1000));
        contacts[0]=contact();contacts[0]->publication.curls[1]=.56f;assert(Controller::contact_valid(*contacts[0],0,1000));
        contacts[0]->publication.curls[0]=.56f;assert(!Controller::contact_valid(*contacts[0],0,1000));
        contacts[0]->publication.curls[0]=1.01f;assert(!Controller::contact_valid(*contacts[0],0,1000));
        const auto input=Controller::policy_input(lane,lane.contact,state(1),owned(),1010,curve);assert(input.fast_open_speed==1.2f);
    }
    static void test_open_pending_and_revision() {
        Controller controller;Lane lane{};lane.valid=true;lane.request=request();lane.contact=contact();lane.last_tick=1000;
        auto selected=lane.contact;selected.geometry.current_angle_radians=0.f;
        auto command=controller.plan(lane,&selected,state(0),{}, {}, {},1060);
        assert(command.submit && command.action==Event::Action::Open && lane.progress.pending_valid);
        command=controller.plan(lane,&selected,state(1),emitted(),{}, {},1070);assert(!command.submit && !command.release);
        command=controller.plan(lane,&selected,state(1),emitted(),{},owned(),1080);
        assert(command.submit && command.action==Event::Action::Update && lane.progress.pending_valid);
        assert(command.request.operation.desired_seconds>0.f);
        const auto pending_request=lane.request;
        command=controller.plan(lane,&selected,state(1),emitted(),owned(),{},1090);assert(!command.release && !command.submit);
        auto evaluated=owned(2);evaluated.applied_seconds=pending_request.operation.desired_seconds;
        selected.geometry.current_angle_radians=selected.geometry.target_angle_radians;
        command=controller.plan(lane,&selected,state(1),emitted(),{},evaluated,1100);
        assert(!command.release && !command.submit && !lane.progress.pending_valid && lane.progress.acknowledged_revision==2);
        command=controller.plan(lane,&selected,state(1),{},evaluated,{},1110);assert(command.release);
    }
    static void test_zero_and_close_retention() {
        Controller controller;auto lane=opening();auto selected=lane.contact;
        selected.geometry.pressure=PhysicalDoorController::Pressure::Closing;selected.geometry.target_angle_radians=0.f;
        lane.policy.commanded_seconds=0.f;
        auto command=controller.plan(lane,&selected,state(1),emitted(),owned(),{},1060);
        assert(command.submit && command.action==Event::Action::Update && command.request.operation.close_authorized);
        auto zero=owned(2);zero.zero_ack=true;
        command=controller.plan(lane,&selected,state(1),emitted(),{},zero,1070);
        assert(command.submit && command.action==Event::Action::Close && lane.close_pending);
        command=controller.plan(lane,nullptr,state(1),emitted(),zero,{},4071);
        assert(!command.submit && !command.release && lane.blocked);
        auto in_progress=zero;in_progress.input_owned=false;in_progress.evaluated=false;
        command=controller.plan(lane,nullptr,state(1),emitted(),in_progress,{},4072);assert(!command.release && !command.submit);
        auto closing=zero;closing.closing=true;closing.input_generation=12;
        command=controller.plan(lane,nullptr,state(1),emitted(),closing,{},4073);assert(!command.release && lane.closing_seen);
        command=controller.plan(lane,nullptr,state(0),emitted(),closing,{},4074);assert(command.release);
        auto lost=lane;command=controller.plan(lost,nullptr,state(1),emitted(),{}, {},4074);assert(command.release);
        lost=lane;command=controller.plan(lost,nullptr,state(1),{},in_progress,{},4074);assert(command.release);
        lost=lane;auto replaced=closing;replaced.input_generation++;
        command=controller.plan(lost,nullptr,state(1),emitted(),replaced,{},4074);assert(command.release);
    }
    static void test_one_shot_and_foreign_loss() {
        Controller controller;Lane lane{};lane.valid=true;lane.request=request();lane.contact=contact();lane.last_tick=1000;
        auto selected=lane.contact;selected.geometry.pressure=PhysicalDoorController::Pressure::Closing;
        auto command=controller.plan(lane,&selected,state(2),{}, {}, {},1060);
        assert(command.submit && command.action==Event::Action::UseClose && lane.one_shot_pending && lane.one_shot_latched);
        command=controller.plan(lane,&selected,state(2),{Event::Stage::Submitted,70},{},{},1070);assert(!command.submit && !command.release);
        command=controller.plan(lane,&selected,state(1),{}, {}, {},1080);assert(!command.submit && !command.release && !lane.one_shot_pending);
        command=controller.plan(lane,&selected,state(2),{}, {}, {},1090);assert(!command.submit);
        command=controller.plan(lane,&selected,state(0),{}, {}, {},1091);assert(!command.submit && !command.release && lane.one_shot_latched);
        command=controller.plan(lane,nullptr,state(2),{}, {}, {},1100);assert(command.release);
        auto active=opening();command=controller.plan(active,nullptr,state(1),emitted(),owned(),{},1060);assert(command.release);
        active=opening();auto changed=active.request;changed.operation.ownership++;assert(!Controller::same_request(active.request,changed));
        changed=active.request;changed.operation.desired_seconds=.1f;assert(!Controller::same_request(active.request,changed));
        selected=active.contact;selected.geometry.inward_speed=1.19f;selected.geometry.current_angle_radians=selected.geometry.target_angle_radians;
        command=controller.plan(active,&selected,state(1),emitted(),owned(),{},1060);assert(!command.release);
        active=opening();selected.geometry.inward_speed=1.2f;
        command=controller.plan(active,&selected,state(1),emitted(),owned(),{},1060);assert(command.release);
        controller.revision_=UINT64_MAX;assert(!controller.advance_locked() && controller.stopping_ && !controller.enabled_);
    }
    static void test_concurrent_clear_barrier() {
        Controller controller;
        const auto old_request=request();
        NativeDoorEventAdmissionFixture::enable_for_fixture();
        NativeDoorEventAdmissionFixture::lock_with_open_receipt(old_request);
        {
            std::lock_guard<std::mutex> guard(controller.state_);
            controller.lane_.valid=true;
            controller.lane_.request=old_request;
            controller.authority_.request=old_request;
            controller.authority_.active=true;
            controller.revision_=9;
        }
        controller.servicing_=true;
        const auto stale_revision=controller.revision_;
        std::thread clearer([&] {controller.clear();});
        unsigned spins{};
        while(!controller.clearing_.load() && spins++<1000000)std::this_thread::yield();
        assert(controller.clearing_.load());
        spins=0;
        bool invalidated{};
        while(!invalidated && spins++<1000000) {
            {std::lock_guard<std::mutex> guard(controller.state_);invalidated=!controller.authority_.active && !controller.lane_.valid;}
            if(!invalidated)std::this_thread::yield();
        }
        assert(invalidated);
        std::unique_lock<std::mutex> state_guard(controller.state_);
        std::thread second_clearer([&] {controller.clear();});
        spins=0;
        while(controller.clearing_.load()<2 && spins++<1000000)std::this_thread::yield();
        assert(controller.clearing_.load()==2);
        state_guard.unlock();
        spins=0;
        bool both_invalidated{};
        while(!both_invalidated && spins++<1000000) {
            {std::lock_guard<std::mutex> guard(controller.state_);both_invalidated=controller.revision_==11 && !controller.authority_.active && !controller.lane_.valid;}
            if(!both_invalidated)std::this_thread::yield();
        }
        assert(both_invalidated);
        std::uint64_t revision{};
        {std::lock_guard<std::mutex> guard(controller.state_);revision=controller.revision_;}
        controller.service({},0,{}, {},GetTickCount64());
        {std::lock_guard<std::mutex> guard(controller.state_);assert(controller.revision_==revision);}
        assert(!controller.try_enter_service());
        NativeDoorEventAdmissionFixture::unlock();
        clearer.join();
        second_clearer.join();
        assert(!controller.clearing_.load() && !NativeDoorEventAdmissionFixture::has_receipt(old_request.operation.ownership));
        assert(!controller.try_enter_service());
        assert(NativeDoorEventAdmissionFixture::owner().submit(old_request));
        controller.finish_submit(old_request,stale_revision,true);
        assert(!NativeDoorEventAdmissionFixture::has_receipt(old_request.operation.ownership));
        controller.servicing_=false;
        assert(controller.try_enter_service());
        auto fresh_request=old_request;
        fresh_request.operation.ownership=11;
        fresh_request.operation.world=12;
        fresh_request.operation.player_generation=13;
        fresh_request.operation.part_generation=14;
        assert(NativeDoorEventAdmissionFixture::owner().submit(fresh_request));
        assert(NativeDoorEventAdmissionFixture::has_receipt(fresh_request.operation.ownership));
        NativeDoorEventAdmissionFixture::owner().revoke(fresh_request.operation.ownership);
        controller.servicing_=false;

        Controller saturated;
        saturated.clearing_=UINT64_MAX-1;
        saturated.enter_clear();
        assert(saturated.clearing_.load()==UINT64_MAX && saturated.stopping_ && !saturated.enabled_);
        saturated.leave_clear();
        assert(saturated.clearing_.load()==UINT64_MAX && !saturated.try_enter_service());
    }
    inline static std::atomic<unsigned> install_reads{};
    static bool rejected_profile(std::uintptr_t,void*,std::size_t) {++install_reads;return false;}
    static UINT gameplay() {return 2;}
    inline static std::map<std::uintptr_t,std::vector<unsigned char>> profiles{};
    template<class Value> static void profile(std::uintptr_t rva,const Value& value) {
        const auto* bytes=reinterpret_cast<const unsigned char*>(&value);profiles[0x140000000+rva]={bytes,bytes+sizeof(value)};
    }
    static bool profile_read(std::uintptr_t address,void* output,std::size_t size) {
        const auto found=profiles.find(address);if(found==profiles.end() || found->second.size()!=size)return false;
        std::memcpy(output,found->second.data(),size);return true;
    }
    static void ingress_profiles() {
        using Trace=NativeWorldTrace;
        profile(Trace::cast_rva,Trace::cast_code);profile(Trace::filter_check_rva,Trace::filter_code);
        profile(Trace::filter_body_rva,Trace::filter_body_code);profile(Trace::filter_pair_rva,Trace::filter_pair_code);
        profile(Trace::collector_add_rva,Trace::collector_add_code);profile(Trace::any_hit_add_rva,Trace::any_hit_add_code);
        profile(Trace::sphere_ctor_rva,NativeHandVolume::constructor_code);
        profile(Trace::filter_vtable_rva+0x20,0x140000000ull+Trace::filter_check_rva);
        profile(Trace::filter_vtable_rva+0x28,0x140000000ull+Trace::filter_pair_rva);
        profile(Trace::filter_vtable_rva+0x30,0x140000000ull+Trace::filter_body_rva);
        profile(Trace::collector_vtable_rva+0x18,0x140000000ull+Trace::collector_add_rva);
        profile(Trace::any_hit_collector_vtable_rva+0x18,0x140000000ull+Trace::any_hit_add_rva);
    }
    static void test_service_lifecycle_drain() {
        NativeDoorEventAdmissionFixture::reset_uninstalled();
        Controller controller;controller.enabled_=true;controller.stopping_=false;
        NativeGrab grab;
        const auto entry_revision=controller.revision_;
        std::atomic<bool> entered{},self_stop{},self_stopped{},release{},returned{};
        std::thread worker([&] {
            std::uint64_t admitted_revision{};assert(controller.enter_service_frame(admitted_revision));entered=true;
            while(!self_stop)std::this_thread::yield();
            assert(!controller.disable(0));
            assert(!controller.install(0x140000000,&rejected_profile,&gameplay,&grab));
            assert(!controller.enter_service());self_stopped=true;
            while(!release)std::this_thread::yield();
            controller.service_cpp({},0,{}, {},GetTickCount64(),admitted_revision);
            controller.leave_service();returned=true;
        });
        while(!entered)std::this_thread::yield();
        assert(!controller.disable(0) && !controller.stopped());
        assert(!controller.install(0x140000000,&rejected_profile,&gameplay,&grab) && install_reads==0);
        self_stop=true;while(!self_stopped)std::this_thread::yield();
        std::uint64_t before{};{std::lock_guard<std::mutex> guard(controller.state_);before=controller.revision_;}
        std::atomic<bool> retried{},retry_result{};
        std::thread stopper([&] {retry_result=controller.disable(1000);retried=true;});
        for(;;) {
            std::lock_guard<std::mutex> guard(controller.state_);
            if(controller.revision_>before)break;
        }
        assert(!retried && !returned && !controller.stopped());
        release=true;worker.join();stopper.join();
        assert(returned && retried && retry_result && controller.stopped());
        assert(!controller.status().active && !controller.status().pending);
        controller.enabled_=true;controller.stopping_=false;
        assert(!controller.enter_service(entry_revision));
        assert(!controller.servicing_ && Controller::service_depth_==0);
        controller.enabled_=false;controller.stopping_=true;
        assert(!controller.install(0x140000000,&rejected_profile,&gameplay,&grab) && install_reads>0);
        assert(controller.stopped());
        Controller qualified_callback;
        const auto before_reads=install_reads.load();
        NativeDoorEventAdmissionFixture::flight(1);
        assert(!qualified_callback.install(0x140000000,&rejected_profile,&gameplay,&grab));
        assert(install_reads==before_reads && !qualified_callback.base_ && !qualified_callback.stopped());
        assert(!qualified_callback.disable(0));
        NativeDoorEventAdmissionFixture::flight(0);
        assert(qualified_callback.disable(0) && qualified_callback.stopped());
    }
    static void test_contacts_snapshot_after_admission() {
        Controller controller;controller.enabled_=true;controller.stopping_=false;
        NativeDoorIngress ingress;
        Controller::Contacts copied{};copied[0]=contact();
        ingress_profiles();assert(ingress.configure(0x140000000,&profile_read));
        auto publication=copied[0]->publication;assert(!ingress.publish(publication));
        publication.tick=publication.source_tick=1010;assert(ingress.publish(publication));
        std::atomic<bool> admitted{},resume{};
        std::thread worker([&] {
            std::uint64_t revision{};assert(controller.enter_service_frame(revision));admitted=true;
            while(!resume)std::this_thread::yield();
            controller.service_ingress_cpp({},0,{},ingress,1030,revision);
            controller.service_cpp({},0,{},copied,1000,revision);
            controller.service_body({},0,{},copied,1000,revision);
            controller.leave_service();
        });
        while(!admitted)std::this_thread::yield();
        ingress.clear();controller.clear();
        publication.tick=publication.source_tick=1020;assert(!ingress.publish(publication));
        publication.tick=publication.source_tick=1030;assert(ingress.publish(publication));
        const auto after_clear=controller.revision_;
        resume=true;worker.join();
        assert(controller.revision_==after_clear && controller.sequence_==0 && !controller.status().active && !controller.status().pending);
        assert(!controller.servicing_ && Controller::service_depth_==0);
        controller.enabled_=false;controller.stopping_=true;
        controller.service({},0,{},ingress,1001);
    }
    static void run() {test_contact_and_mapping();test_open_pending_and_revision();test_zero_and_close_retention();test_one_shot_and_foreign_loss();test_concurrent_clear_barrier();test_service_lifecycle_drain();test_contacts_snapshot_after_admission();}
};
}
int main() {d2vr::NativeDoorInteractionFixture::run();std::cout<<"native door interaction check passed\n";}
