#pragma once
#include "native_door_ingress.hpp"
#include "native_door_progress.hpp"
#include "physical_door_control.hpp"
#include <limits>

namespace d2vr {
class NativeDoorInteraction {
public:
    using Read=NativeDoorEventAdmission::Read;
    using Logic=UINT (*)();
    using Contact=NativeDoorIngress::Contact;
    using Contacts=std::array<std::optional<Contact>,2>;
    using Request=NativeDoorEventAdmission::Request;
    struct Status {std::uint64_t ownership{};bool active{},pending{},closing{},use_close{},blocked{};};
    static NativeDoorInteraction& process_owner() {static auto* owner=new NativeDoorInteraction;return *owner;}
    bool configure(std::uintptr_t base,Read read,Logic logic,NativeGrab* grab) noexcept {return install(base,read,logic,grab);}
    bool install(std::uintptr_t base,Read read,Logic logic,NativeGrab* grab) noexcept {
        if(servicing_ || clearing() || service_depth_)return false;
        std::unique_lock<std::mutex> lifecycle(lifecycle_,std::try_to_lock);
        if(!lifecycle.owns_lock() || !base || !read || !logic || !grab)return false;
        if(enabled())return base_==base && read_==read && logic_==logic && grab_==grab;
        if(base_ && (base_!=base || read_!=read || logic_!=logic || grab_!=grab))return false;
        auto& animation=NativeDoorAnimation::process_owner();auto& event=NativeDoorEventAdmission::process_owner();
        if(!animation.stopped() || !event.stopped())return false;
        stopping_=true;clear();base_=base;read_=read;logic_=logic;grab_=grab;
        if(!animation.install(base,read))return false;
        if(!event.install(base,read,&qualify,animation)) {event.disable();animation.disable();return false;}
        enabled_=true;stopping_=false;return true;
    }
    bool enabled() const noexcept {return enabled_ && !stopping_ && NativeDoorEventAdmission::process_owner().enabled() && NativeDoorAnimation::process_owner().enabled();}
    bool stopped() const noexcept {return !enabled_ && !servicing_ && !clearing() && NativeDoorEventAdmission::process_owner().stopped() && NativeDoorAnimation::process_owner().stopped();}
    bool disable(std::uint64_t timeout_ms=5000) noexcept {
        stopping_=true;enabled_=false;clear();
        if(service_depth_)return false;
        const auto began=GetTickCount64();
        while(servicing_) {if(GetTickCount64()-began>=timeout_ms)return false;Sleep(1);}
        std::unique_lock<std::mutex> lifecycle(lifecycle_,std::try_to_lock);if(!lifecycle.owns_lock())return false;
        if(!NativeDoorEventAdmission::process_owner().disable(timeout_ms))return false;
        return NativeDoorAnimation::process_owner().disable(timeout_ms);
    }
    void clear() noexcept {
        enter_clear();
        std::uint64_t ownership{};
        {std::lock_guard<std::mutex> guard(state_);if(authority_.active)ownership=authority_.request.operation.ownership;authority_={};lane_={};advance_locked();}
        if(ownership)NativeDoorEventAdmission::process_owner().revoke(ownership);
        leave_clear();
    }
    Status status() const noexcept {
        std::lock_guard<std::mutex> guard(state_);
        return {lane_.request.operation.ownership,authority_.active,lane_.progress.pending_valid || lane_.one_shot_pending,
            lane_.close_pending,lane_.one_shot_latched,lane_.blocked};
    }
    void service(const NativePlayerCamera& camera,std::uintptr_t sensor,const rig::Identity& identity,
                 const Contacts& contacts,std::uint64_t now) noexcept {
        std::uint64_t revision{};if(!enter_service_frame(revision))return;
        __try {service_cpp(camera,sensor,identity,contacts,now,revision);}
        __finally {leave_service();}
    }
    void service(const NativePlayerCamera& camera,std::uintptr_t sensor,const rig::Identity& identity,
                 NativeDoorIngress& ingress,std::uint64_t now) noexcept {
        std::uint64_t revision{};if(!enter_service_frame(revision))return;
        __try {service_ingress_cpp(camera,sensor,identity,ingress,now,revision);}
        __finally {leave_service();}
    }
private:
    friend struct NativeDoorInteractionFixture;
    struct Lane {
        Contact contact{};NativePlayerCamera camera{};rig::Identity sensor_identity{};std::uintptr_t sensor{};
        Request request{};NativeDoorProgress::State progress{};PhysicalDoorController::State policy{};
        std::uint64_t last_tick{},submitted_tick{},closing_generation{};
        bool valid{},one_shot_pending{},one_shot_latched{},close_pending{},closing_seen{},blocked{};
    };
    struct Authority {Contact contact{};NativePlayerCamera camera{};rig::Identity sensor_identity{};std::uintptr_t sensor{};Request request{};bool active{},closing{};};
    struct Curve {float fps{},duration{};};
    struct Command {NativeDoorEventAdmission::Action action{};Request request{};bool submit{},release{};};
    struct Reader {
        Read input{};
        static bool cpp(Read read,std::uintptr_t address,void* output,std::size_t size) noexcept {try {return read && read(address,output,size);}catch(...) {return false;}}
        bool operator()(std::uintptr_t address,void* output,std::size_t size) const noexcept {
            __try {return cpp(input,address,output,size);}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
        }
    };
    inline static constexpr std::uint64_t initial_timeout=500,pending_timeout=500,close_blocked_timeout=2000;
    inline static constexpr float right_angle=1.5707963267948966f;
    static UINT cpp_logic(Logic logic) noexcept {try {return logic?logic():0;}catch(...) {return 0;}}
    static UINT safe_logic(Logic logic) noexcept {__try {return cpp_logic(logic);}__except(EXCEPTION_EXECUTE_HANDLER) {return 0;}}
    static bool same_identity(const rig::Identity& first,const rig::Identity& second) noexcept {
        return first.generation==second.generation && ((first.flags^second.flags)&NativeDoorOwner::stable_identity_flags)==0;
    }
    static bool same_request(const Request& first,const Request& second) noexcept {
        const auto& left=first.operation;const auto& right=second.operation;
        return first.game==second.game && first.manager==second.manager && first.component==second.component && first.door==second.door &&
            first.component_generation==second.component_generation && first.door_spawn==second.door_spawn && first.player_spawn==second.player_spawn && first.part_spawn==second.part_spawn &&
            left.ownership==right.ownership && left.world==right.world && left.player_generation==right.player_generation && left.part_generation==right.part_generation &&
            left.player==right.player && left.part==right.part && left.animation==right.animation && left.container==right.container && left.binding==right.binding &&
            left.opening_clip==right.opening_clip && left.closing_clip==right.closing_clip && left.desired_seconds==right.desired_seconds &&
            left.close_authorized==right.close_authorized && left.linked==right.linked;
    }
    static bool contact_valid(const Contact& contact,unsigned side,std::uint64_t now) noexcept {
        return side<2 && contact.side==side && contact.publication.curls[side]<=.55f && contact.contact_token && contact.observed_tick && now>=contact.observed_tick && now-contact.observed_tick<=100 &&
            NativeDoorIngress::fresh(contact.publication,now) && contact.publication.velocity_ready && contact.owner==contact.clip.owner && contact.owner==contact.skeleton.owner &&
            contact.surface.entity==contact.owner.door && contact.clip.content.ccw==contact.opening_ccw && contact.empty.epoch==contact.publication.pose_epoch &&
            contact.publication.source.player==contact.owner.player && contact.publication.source.game==contact.owner.game && contact.publication.source.world==contact.owner.world &&
            std::isfinite(contact.geometry.current_angle_radians) && std::isfinite(contact.geometry.target_angle_radians) && std::isfinite(contact.geometry.inward_speed);
    }
    static bool same_contact(const Contact& first,const Contact& second) noexcept {
        return first.owner==second.owner && first.side==second.side && first.contact_token==second.contact_token && first.opening_ccw==second.opening_ccw &&
            first.surface.full_body==second.surface.full_body && first.surface.shape_key==second.surface.shape_key &&
            first.publication.pose_epoch==second.publication.pose_epoch && first.publication.reference_epoch==second.publication.reference_epoch &&
            NativeDoorIngress::same_source(first.publication.source,second.publication.source);
    }
    static const Contact* choose(const Contacts& contacts,const Lane& lane,std::uint64_t now) noexcept {
        const Contact* selected{};const Contact* matching{};
        for(unsigned side=0;side<2;++side)if(contacts[side] && contact_valid(*contacts[side],side,now)) {
            const auto& contact=*contacts[side];
            if(lane.valid && same_contact(lane.contact,contact))matching=&contact;
            if(!selected)selected=&contact;
            else if(!(selected->owner==contact.owner) || selected->publication.revision!=contact.publication.revision ||
                    selected->geometry.pressure!=contact.geometry.pressure)return nullptr;
        }
        return lane.valid?matching:selected;
    }
    static bool initial_state(const NativeDoorOwner::State& state) noexcept {return (state.current==0 || state.current==2) && state.current==state.next;}
    static float angle_seconds(const void* context,float angle) noexcept {
        if(!context || !std::isfinite(angle) || angle<0 || angle>right_angle)return std::numeric_limits<float>::quiet_NaN();
        const auto& curve=*static_cast<const Curve*>(context);
        if(!std::isfinite(curve.fps) || curve.fps<=0 || !std::isfinite(curve.duration) || curve.duration<=0)return std::numeric_limits<float>::quiet_NaN();
        const auto result=angle*17.f/(right_angle*curve.fps);
        return result<curve.duration?result:std::numeric_limits<float>::quiet_NaN();
    }
    bool advance_locked() noexcept {
        if(revision_==UINT64_MAX) {enabled_=false;stopping_=true;return false;}++revision_;return true;
    }
    bool clearing() const noexcept {return clearing_.load()!=0;}
    bool enter_service() noexcept {
        std::uint64_t revision{};return enter_service_frame(revision);
    }
    bool enter_service_frame(std::uint64_t& revision) noexcept {
        if(service_depth_)return false;
        {std::lock_guard<std::mutex> guard(state_);if(!enabled_ || stopping_ || clearing())return false;revision=revision_;}
        return enter_service(revision);
    }
    bool enter_service(std::uint64_t revision) noexcept {
        if(service_depth_ || !lifecycle_.try_lock())return false;
        {std::lock_guard<std::mutex> guard(state_);
            if(!enabled_ || stopping_ || revision_!=revision || !try_enter_service()) {lifecycle_.unlock();return false;}}
        ++service_depth_;return true;
    }
    void leave_service() noexcept {--service_depth_;servicing_=false;lifecycle_.unlock();}
    void service_ingress_cpp(const NativePlayerCamera& camera,std::uintptr_t sensor,const rig::Identity& identity,
                             NativeDoorIngress& ingress,std::uint64_t now,std::uint64_t revision) noexcept {
        try {const auto contacts=ingress.contacts(now);service_cpp(camera,sensor,identity,contacts,now,revision);}catch(...) {clear();}
    }
    void service_cpp(const NativePlayerCamera& camera,std::uintptr_t sensor,const rig::Identity& identity,
                     const Contacts& contacts,std::uint64_t now,std::uint64_t revision) noexcept {
        try {if(revision_current(revision))service_body(camera,sensor,identity,contacts,now,revision);}catch(...) {clear();}
    }
    bool try_enter_service() noexcept {
        if(clearing() || servicing_.exchange(true))return false;
        if(clearing()) {servicing_=false;return false;}
        return true;
    }
    void enter_clear() noexcept {
        auto depth=clearing_.load();
        for(;;) {
            if(depth==UINT64_MAX) {enabled_=false;stopping_=true;return;}
            if(clearing_.compare_exchange_weak(depth,depth+1)) {
                if(depth+1==UINT64_MAX) {enabled_=false;stopping_=true;}
                return;
            }
        }
    }
    void leave_clear() noexcept {
        auto depth=clearing_.load();
        while(depth && depth!=UINT64_MAX)if(clearing_.compare_exchange_weak(depth,depth-1))return;
    }
    bool revision_current(std::uint64_t revision) const noexcept {
        std::lock_guard<std::mutex> guard(state_);return !clearing() && enabled_ && !stopping_ && revision_==revision;
    }
    void finish_submit(const Request& request,std::uint64_t revision,bool submitted) noexcept {
        if(!submitted || !revision_current(revision)) {NativeDoorEventAdmission::process_owner().revoke(request.operation.ownership);clear();}
    }
    bool validate(const Authority& authority,std::uint64_t now,bool fresh_contact) const {
        if(!read_ || !grab_ || safe_logic(logic_)!=2 || !authority.sensor || authority.camera.authored_view() || authority.contact.side>1 ||
           !(authority.contact.publication.curls[authority.contact.side]>=0.f && authority.contact.publication.curls[authority.contact.side]<=.55f) ||
           (fresh_contact && (!NativeDoorIngress::fresh(authority.contact.publication,now) || now<authority.contact.observed_tick || now-authority.contact.observed_tick>100)))return false;
        Reader read{read_};const auto camera=native_player_camera(read,base_,authority.camera.camera);const auto sensor=rig::started(read,authority.sensor);
        if(!camera || camera->authored_view() || camera->player!=authority.camera.player || camera->body!=authority.camera.body ||
           camera->controller!=authority.camera.controller || camera->physics!=authority.camera.physics || !same_identity(camera->camera_identity,authority.camera.camera_identity) ||
           !sensor || !same_identity(*sensor,authority.sensor_identity) || !rig::typed(read,base_,authority.sensor,0x1b403b8) ||
           rig::value<std::uintptr_t>(read,authority.sensor+0x10)!=camera->player || rig::value<unsigned char>(read,authority.sensor+0x38).value_or(0)==0 ||
           rig::value<std::uintptr_t>(read,authority.sensor+0x40)!=camera->camera ||
           !NativeDoorOwner::current(read,authority.contact.owner))return false;
        const auto source=native_world_query_source(read,base_,camera->camera,authority.contact.owner.world);
        const auto state=NativeDoorOwner::state(read,authority.contact.owner);
        return source && NativeDoorIngress::same_source(*source,authority.contact.publication.source) && state && state->eligible() &&
            NativeDoorEmptyGate::current(base_,read,*camera,*grab_,authority.contact.empty,now) &&
            NativeDoorClipSelection::current(read,authority.contact.clip) && NativeDoorSkeletonContent::current(read,authority.contact.skeleton);
    }
    static bool qualify(const Request& request,NativeDoorEventAdmission::Phase) {
        auto& self=process_owner();Authority authority{};std::uint64_t revision{};
        {std::lock_guard<std::mutex> guard(self.state_);if(self.clearing() || !self.enabled_ || self.stopping_ || !self.authority_.active)return false;authority=self.authority_;revision=self.revision_;}
        return same_request(request,authority.request) && self.validate(authority,GetTickCount64(),!authority.closing) && self.revision_current(revision);
    }
    static PhysicalDoorController::Input policy_input(const Lane& lane,const Contact& contact,const NativeDoorOwner::State& state,
                                                     const NativeDoorAnimation::Status& animation,std::uint64_t now,const Curve& curve) noexcept {
        PhysicalDoorController::Input input{};input.owner_token=lane.request.operation.ownership;input.contact_identity=contact.contact_token;
        input.monotonic_tick_ms=now;input.dt_seconds=lane.last_tick?static_cast<float>(now-lane.last_tick)*.001f:.001f;
        if(input.dt_seconds>.1f)input.dt_seconds=.1f;
        input.native_state=state.current==0?PhysicalDoorController::NativeState::Closed:state.current==1?PhysicalDoorController::NativeState::Animated:
            state.current==2?PhysicalDoorController::NativeState::Opened:PhysicalDoorController::NativeState::Other;
        input.current_angle_radians=contact.geometry.current_angle_radians;input.target_angle_radians=contact.geometry.target_angle_radians;
        input.fresh_contact=true;input.pressure=contact.geometry.pressure;input.inward_speed=contact.geometry.inward_speed;
        input.fast_open_speed=1.2f;input.begin_dwell_seconds=.06f;input.close_dwell_seconds=.06f;
        input.normal_slew_seconds_per_second=1.f;input.closing_slew_seconds_per_second=2.f;input.angle_deadband_radians=.01f;
        input.mapping={&angle_seconds,&curve,curve.duration,true};input.evaluated_seconds=animation.evaluated?animation.applied_seconds:0.f;
        input.animation_receipt_active=animation.input_owned && animation.evaluated;input.zero_ack=animation.zero_ack;
        input.native_use_eligible=state.eligible() && state.current==state.next;
        input.close_authorized=input.pressure==PhysicalDoorController::Pressure::Closing && std::abs(input.target_angle_radians)<=input.angle_deadband_radians;
        return input;
    }
    Command plan(Lane& lane,const Contact* contact,const NativeDoorOwner::State& state,const NativeDoorEventAdmission::Status& event_status,
                 const NativeDoorAnimation::Status& acknowledged,const NativeDoorAnimation::Status& pending,std::uint64_t now) {
        Command command{};
        if(lane.close_pending) {
            if(state.current==0 || !event_status.epoch || event_status.epoch!=lane.progress.event_epoch || !acknowledged.published) {command.release=true;return command;}
            if(acknowledged.closing) {
                if(lane.closing_seen && acknowledged.input_generation!=lane.closing_generation) {command.release=true;return command;}
                lane.closing_seen=true;lane.closing_generation=acknowledged.input_generation;
            }
            if(now>=lane.submitted_tick && now-lane.submitted_tick>=close_blocked_timeout)lane.blocked=true;
            lane.last_tick=now;return command;
        }
        if(!contact) {command.release=true;return command;}
        if(lane.one_shot_latched) {
            if(lane.one_shot_pending && (!event_status.epoch || event_status.stage==NativeDoorEventAdmission::Stage::Unknown))lane.one_shot_pending=false;
            else if(lane.one_shot_pending && now-lane.submitted_tick>=initial_timeout)command.release=true;
            lane.last_tick=now;return command;
        }
        auto animation=acknowledged;
        if(lane.progress.begun) {
            const bool had_pending=lane.progress.pending_valid;
            const auto result=NativeDoorProgress::poll(lane.progress,event_status,acknowledged,pending,now,initial_timeout,pending_timeout);
            if(result==NativeDoorProgress::Result::Expired) {command.release=true;return command;}
            if(result==NativeDoorProgress::Result::Wait) {lane.last_tick=now;return command;}
            lane.request=lane.progress.acknowledged;animation=had_pending?pending:acknowledged;
            if(!animation.evaluated || animation.duration_seconds!=contact->clip.content.duration) {command.release=true;return command;}
        }
        const Curve curve{contact->clip.content.fps,contact->clip.content.duration};const auto input=policy_input(lane,*contact,state,animation,now,curve);
        const auto decision=PhysicalDoorController::step(lane.policy,input);lane.last_tick=now;
        if(decision.action==PhysicalDoorController::Action::ReleaseOpen) {command.release=true;return command;}
        if(decision.action==PhysicalDoorController::Action::BeginNativeOpen) {
            if(!NativeDoorProgress::begin(lane.progress,lane.request,now)) {command.release=true;return command;}
            command={NativeDoorEventAdmission::Action::Open,lane.request,true,false};
        } else if(decision.action==PhysicalDoorController::Action::NativeUseClose) {
            lane.one_shot_latched=true;lane.one_shot_pending=true;lane.submitted_tick=now;command={NativeDoorEventAdmission::Action::UseClose,lane.request,true,false};
        } else if(decision.action==PhysicalDoorController::Action::UpdateOpen) {
            auto request=lane.request;request.operation.desired_seconds=decision.seconds;request.operation.close_authorized=input.close_authorized;
            if(!same_request(request,lane.request)) {
                if(!NativeDoorProgress::queue(lane.progress,request,now)) {command.release=true;return command;}
                lane.request=request;command={NativeDoorEventAdmission::Action::Update,request,true,false};
            }
        } else if(decision.action==PhysicalDoorController::Action::CloseOwned) {
            lane.close_pending=true;lane.submitted_tick=now;NativeDoorProgress::mark_handoff(lane.progress);
            command={NativeDoorEventAdmission::Action::Close,lane.request,true,false};
        }
        return command;
    }
    void service_body(const NativePlayerCamera& camera,std::uintptr_t sensor,const rig::Identity& identity,const Contacts& contacts,std::uint64_t now,std::uint64_t entry_revision) {
        Lane lane{};std::uint64_t revision{};
        {std::lock_guard<std::mutex> guard(state_);if(clearing() || revision_!=entry_revision || !enabled_ || stopping_)return;lane=lane_;revision=revision_;}
        if(!enabled() || !now) {clear();return;}
        if(lane.last_tick && now<=lane.last_tick)return;
        const auto* contact=choose(contacts,lane,now);
        if(!lane.valid) {
            if(!contact || !initial_state(contact->state))return;
            std::uint64_t token{};
            {std::lock_guard<std::mutex> guard(state_);if(revision_!=revision || sequence_==UINT64_MAX)return;token=++sequence_;}
            const NativeDoorRequest::Tokens tokens{token,token,token,token,token};const auto request=NativeDoorRequest::make(contact->owner,tokens,contact->opening_ccw);if(!request)return;
            lane.valid=true;lane.request=*request;lane.contact=*contact;
        } else if(contact && !lane.close_pending)lane.contact=*contact;
        lane.camera=camera;lane.sensor=sensor;lane.sensor_identity=identity;
        Authority authority{lane.contact,camera,identity,sensor,lane.request,false,lane.close_pending};
        if(!validate(authority,now,!lane.close_pending)) {clear();return;}
        Reader read{read_};const auto state=NativeDoorOwner::state(read,lane.contact.owner);if(!state) {clear();return;}
        auto& event=NativeDoorEventAdmission::process_owner();auto& animation=NativeDoorAnimation::process_owner();
        const auto event_status=event.status(lane.request);
        const auto acknowledged=lane.progress.acknowledged_valid?animation.status(lane.progress.acknowledged.operation):animation.status(lane.request.operation);
        const auto pending=lane.progress.pending_valid?animation.status(lane.progress.pending.operation):NativeDoorAnimation::Status{};
        const auto command=plan(lane,contact,*state,event_status,acknowledged,pending,now);
        if(command.release) {clear();return;}
        authority.contact=lane.contact;authority.request=lane.request;authority.closing=lane.close_pending;
        authority.active=lane.progress.begun || lane.one_shot_pending;
        if(lane.one_shot_latched && !lane.one_shot_pending)authority.active=false;
        {
            std::lock_guard<std::mutex> guard(state_);if(clearing() || revision_!=revision || !enabled_ || stopping_ || !advance_locked())return;
            authority_=authority;lane_=lane;revision=revision_;
        }
        if(command.submit) {
            const bool submitted=event.submit(command.request,command.action);
            finish_submit(command.request,revision,submitted);
        }
        if(lane.progress.begun && lane.progress.event_epoch) {
            const auto after=event.status(lane.request);
            if(after.epoch!=lane.progress.event_epoch)clear();
        }
    }
    mutable std::mutex state_;std::mutex lifecycle_;
    std::atomic<bool> enabled_{},stopping_{true},servicing_{};
    std::atomic<std::uint64_t> clearing_{};
    inline static thread_local unsigned service_depth_{};
    std::uintptr_t base_{};Read read_{};Logic logic_{};NativeGrab* grab_{};
    std::uint64_t sequence_{},revision_{};Lane lane_{};Authority authority_{};
};
}
