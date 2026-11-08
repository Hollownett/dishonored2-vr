#pragma once
#include "native_manual_equip.hpp"
#include "native_manual_equip_hooks.hpp"
#include "native_carry_snapshot.hpp"
#include "shoulder_carry.hpp"
#include <limits>

namespace d2vr {
class NativeManualEquipRuntime {
public:
    using Policy=NativeManualEquip;
    using Hooks=NativeManualEquipHooks;
    using Read=Policy::Read;
    using Token=std::uint64_t;
    using Submit=void (*)(void*,std::uintptr_t,const Policy::Params*);
    enum class HipReject {none,barrier,stale_action,context,idle_arm,unknown_occupancy,occupied,selected_missing,scope,policy_grant,accepted,count};
    struct HipAttempt {
        Token token{},current_generation{};
        NativePlayerStateLease owner;
        std::uint64_t owner_epoch{};
        std::uintptr_t machine{},selected_item{},selected_ammo{};
        bool selected_known{},occupancy_known{},occupied{};
        HipReject reason{HipReject::none};
    };
    struct Publication {
        std::optional<NativePlayerStateLease> owner;
        std::uint64_t xr_epoch{};
        std::optional<bool> physical_left_occupied;
        bool focused{};
    };
    struct HipContext {
        NativePlayerStateLease owner;
        std::uint64_t xr_epoch{},generation{};
        bool operator==(const HipContext& other)const noexcept {
            return owner==other.owner && xr_epoch==other.xr_epoch && generation==other.generation;
        }
    };
    struct Status {
        bool installed{},hooks_enabled{},writer_enabled{},armed{},exhausted{},focused{};
        std::uint64_t generation{},last_manual_generation{},xr_epoch{},hip_grants{},wheel_grants{},revocations{},vetoes{},owner_gaps{},occupancy_gaps{},wheel_rejections{};
        unsigned in_flight{};
        Policy::Observation observation;
        bool holster_pending{},holster_poisoned{};
        std::uint64_t holster_receipts{},holster_retirements{},holster_gaps{};
        HipAttempt last_hip_attempt;
        std::array<std::uint64_t,static_cast<std::size_t>(HipReject::count)> hip_rejections{};
    };
    explicit NativeManualEquipRuntime(Hooks& hooks=Hooks::process_owner()):hooks_(hooks){}
    static NativeManualEquipRuntime& process_owner(){static auto* owner=new NativeManualEquipRuntime;return *owner;}
    bool install(std::uintptr_t base,Read read) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(installed_ || holster_.pending || !hooks_.ready_for_install() || (base_ && (base_!=base || read_!=read)) || !policy_.start_observer(base,read))return false;
        base_=base;read_=read;armed_=false;
        if(!hooks_.install(base,read,{this,&processor_host,&wheel_before,&wheel_after})){policy_.stop();return false;}
        installed_=true;hooks_.enabled(true);return true;
    }
    bool disable(std::uint64_t timeout_ms=5000) {
        {std::lock_guard<std::mutex> lock(mutex_);if(holster_.pending)poison_holster_locked();installed_=false;armed_=false;revoke_locked();policy_.stop();}
        return hooks_.disable(timeout_ms);
    }
    void publish(const Publication& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool lifetime_changed=!(publication_.owner==value.owner) || publication_.xr_epoch!=value.xr_epoch;
        if(lifetime_changed && holster_.pending)poison_holster_locked();
        if(lifetime_changed || publication_.focused!=value.focused){revoke_locked();armed_=false;}
        if(value.physical_left_occupied.value_or(true) && !publication_.physical_left_occupied.value_or(true))revoke_locked();
        publication_=value;
    }
    bool ordinary_intent(){std::lock_guard<std::mutex> lock(mutex_);return revoke_locked();}
    Token holster(){std::lock_guard<std::mutex> lock(mutex_);return revoke_locked()?generation_:0;}
    bool allow_holster(Token release_generation,const NativePlayerStateLease& expected_owner,std::uint64_t xr_epoch,std::uintptr_t machine)const {
        std::lock_guard<std::mutex> lock(mutex_);Reader reader{read_};
        return !holster_.pending && installed_ && hooks_.enabled() && publication_.focused && xr_epoch && xr_epoch==publication_.xr_epoch &&
            publication_.owner && *publication_.owner==expected_owner && expected_owner.machine==machine &&
            release_generation && release_generation<=generation_ && last_manual_generation_<=release_generation &&
            native_player_state_lease(reader,base_,machine)==publication_.owner && state_locked(machine).has_value();
    }
    bool execute_holster(Token release_generation,const NativePlayerStateLease& expected_owner,std::uint64_t xr_epoch,std::uintptr_t machine,
                         const Policy::Params& params,Submit submit,void* context) {
        if(!submit || !admit_holster(release_generation,expected_owner,xr_epoch,machine,params))return false;
        bool completed=false,accepted=false;
        __try{submit(context,machine,&holster_.params);completed=true;}
        __finally{accepted=finish_holster(completed);}
        return accepted;
    }
    bool cancel(Token token){std::lock_guard<std::mutex> lock(mutex_);return token && action_.token==token && revoke_locked();}
    Token reserve_hip(const NativePlayerStateLease& owner) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(holster_.pending || exhausted_ || !context_locked(owner.machine) || !publication_.owner || !(owner==*publication_.owner))return 0;
        const auto token=next_locked();if(!token)return 0;
        action_={token,owner,publication_.xr_epoch,Policy::Source::hip,Phase::reserved};
        if(!policy_.reserve_manual(owner,token,Policy::Source::hip)){revoke_locked();return 0;}
        last_manual_generation_=token;
        return token;
    }
    bool grant_hip(Token token,std::uintptr_t machine) {
        std::lock_guard<std::mutex> lock(mutex_);
        HipAttempt attempt{};attempt.token=token;attempt.machine=machine;attempt.owner_epoch=publication_.xr_epoch;
        if(publication_.owner)attempt.owner=*publication_.owner;
        if(holster_.pending)return record_hip_locked(attempt,HipReject::barrier);
        if(!token || action_.token!=token || action_.source!=Policy::Source::hip || action_.phase!=Phase::reserved ||
           action_.epoch!=publication_.xr_epoch || !publication_.owner || !(action_.owner==*publication_.owner))return record_hip_locked(attempt,HipReject::stale_action);
        if(!context_locked(machine))return record_hip_locked(attempt,HipReject::context);
        if(!arm_locked(machine)){revoke_locked();return record_hip_locked(attempt,HipReject::idle_arm);}
        const auto occupied=occupied_locked(machine);attempt.occupancy_known=occupied.has_value();attempt.occupied=occupied.value_or(false);
        if(!occupied || *occupied){revoke_locked();return record_hip_locked(attempt,occupied?HipReject::occupied:HipReject::unknown_occupancy);}
        const auto state=state_locked(machine);Reader reader{read_};
        const auto controller=rig::value<std::uintptr_t>(reader,machine+0x2f8);
        const auto item=controller?rig::value<std::uintptr_t>(reader,*controller+0x190):std::nullopt,
            ammo=controller?rig::value<std::uintptr_t>(reader,*controller+0x198):std::nullopt;
        attempt.selected_known=item.has_value() && ammo.has_value();attempt.selected_item=item.value_or(0);attempt.selected_ammo=ammo.value_or(0);
        const auto scope=state && item && ammo?Policy::qualify(base_,read_,*state,*item,*ammo):std::nullopt;
        if(!scope){revoke_locked();return record_hip_locked(attempt,!item || !ammo || !*item?HipReject::selected_missing:HipReject::scope);}
        if(!policy_.grant(token,*scope)){revoke_locked();return record_hip_locked(attempt,HipReject::policy_grant);}
        action_.phase=Phase::granted;++hip_grants_;return record_hip_locked(attempt,HipReject::accepted);
    }
    bool hip_item_current(Token token)const {
        std::lock_guard<std::mutex> lock(mutex_);Reader reader{read_};
        if(!installed_ || !hooks_.enabled() || !publication_.focused || !publication_.xr_epoch || !publication_.owner ||
           !token || action_.token!=token || action_.source!=Policy::Source::hip || action_.phase!=Phase::granted ||
           action_.epoch!=publication_.xr_epoch || !(action_.owner==*publication_.owner))return false;
        const auto lease=policy_.lease();
        if(lease.generation!=token || lease.source!=Policy::Source::hip || !(lease.scope.owner==action_.owner) ||
           !(native_player_state_lease(reader,base_,action_.owner.machine)==publication_.owner) ||
           rig::value<std::uintptr_t>(reader,lease.scope.inventory+0x70)!=lease.scope.item)return false;
        const auto current=Policy::qualify(base_,read_,lease.scope.state,lease.scope.item,lease.scope.ammo);
        return current && *current==lease.scope && rig::value<std::uintptr_t>(reader,lease.scope.inventory+0x70)==lease.scope.item &&
            native_player_state_lease(reader,base_,action_.owner.machine)==publication_.owner;
    }
    std::optional<HipContext> hip_context()const {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!installed_ || !hooks_.enabled() || !publication_.focused || !publication_.owner || !publication_.xr_epoch || !generation_)return {};
        return HipContext{*publication_.owner,publication_.xr_epoch,generation_};
    }
    bool hip_empty_current(const HipContext& expected,Token after_generation=0) {
        std::lock_guard<std::mutex> lock(mutex_);return hip_empty_current_locked(expected,after_generation);
    }
private:
    bool hip_empty_current_locked(const HipContext& expected,Token after_generation,Token drawing_generation=0) {
        Reader reader{read_};
        const auto context_current=[&] {
            const auto lease=policy_.lease();
            const bool idle_action=action_.phase==Phase::none && !lease.generation && lease.source==Policy::Source::none;
            const bool drawing_action=drawing_generation && action_.phase==Phase::granted && action_.source==Policy::Source::hip &&
                action_.token==drawing_generation && expected.generation==drawing_generation &&
                lease.generation==drawing_generation && lease.source==Policy::Source::hip;
            return installed_ && hooks_.enabled() && publication_.focused && publication_.owner &&
                *publication_.owner==expected.owner && publication_.xr_epoch==expected.xr_epoch && generation_==expected.generation &&
                expected.generation>after_generation && !holster_.pending && !holster_.poisoned && !hooks_.in_flight() && !policy_.in_flight() &&
                (idle_action || drawing_action) &&
                native_player_state_lease(reader,base_,expected.owner.machine)==publication_.owner;
        };
        if(!context_current())return false;
        const auto state=state_locked(expected.owner.machine);
        const auto inventory=rig::value<std::uintptr_t>(reader,expected.owner.machine+0x328);
        const auto body=rig::value<std::uintptr_t>(reader,expected.owner.machine+0x300);
        const auto inventory_identity=inventory?rig::started(reader,*inventory):std::nullopt;
        if(!state || !inventory || !body || !inventory_identity || !rig::typed(reader,base_,*inventory,0x1b9a878) ||
           rig::value<std::uintptr_t>(reader,*inventory+0x10)!=expected.owner.player ||
           rig::value<std::uintptr_t>(reader,*body+0x60)!=inventory)return false;
        if(!publication_.physical_left_occupied || *publication_.physical_left_occupied)return false;
        const auto carry=rig::native_carry_snapshot(reader,base_,expected.owner.player,*body);
        if(!carry || carry->machine!=expected.owner.machine || carry->machine_identity!=expected.owner.identity || (carry->hands()&1u))return false;
        const auto records=rig::list(reader,*state+0x30,4096),requests=rig::list(reader,expected.owner.machine+0x220,4096);
        if(!records || records->packed_count || !requests)return false;
        for(unsigned pass=0;pass<2;++pass) {
            if(!context_current() || state_locked(expected.owner.machine)!=state ||
               rig::value<std::uintptr_t>(reader,expected.owner.machine+0x328)!=inventory ||
               rig::value<std::uintptr_t>(reader,expected.owner.machine+0x300)!=body || rig::started(reader,*inventory)!=inventory_identity ||
               !rig::typed(reader,base_,*inventory,0x1b9a878) || rig::value<std::uintptr_t>(reader,*inventory+0x10)!=expected.owner.player ||
               rig::value<std::uintptr_t>(reader,*body+0x60)!=inventory ||
               rig::value<std::uintptr_t>(reader,*inventory+0x70)!=std::optional<std::uintptr_t>{0} ||
               rig::value<unsigned char>(reader,*state+0x20)!=std::optional<unsigned char>{static_cast<unsigned char>(0)} ||
               rig::value<unsigned char>(reader,*state+0x150)!=std::optional<unsigned char>{static_cast<unsigned char>(0)} ||
               rig::value<std::uintptr_t>(reader,*state+0x108)!=std::optional<std::uintptr_t>{0} ||
               rig::value<std::uintptr_t>(reader,*state+0x118)!=std::optional<std::uintptr_t>{0})return false;
            const auto current_carry=rig::native_carry_current(reader,base_,*carry);
            if(!current_carry || *current_carry!=*carry)return false;
            const auto current_records=rig::list(reader,*state+0x30,4096),current_requests=rig::list(reader,expected.owner.machine+0x220,4096);
            if(!current_records || !current_requests || std::memcmp(&*records,&*current_records,sizeof(rig::List)) ||
               std::memcmp(&*requests,&*current_requests,sizeof(rig::List)))return false;
            for(unsigned index=0;index<requests->packed_count;++index) {
                const auto request=rig::value<QueueRecord>(reader,requests->data+index*sizeof(QueueRecord));
                if(!request || request->state==64)return false;
            }
        }
        return context_current();
    }
public:
    bool hip_draw_current(Token token) {
        std::lock_guard<std::mutex> lock(mutex_);Reader reader{read_};
        if(!token || !publication_.owner)return false;
        const HipContext context{*publication_.owner,publication_.xr_epoch,generation_};
        if(!hip_empty_current_locked(context,0,token) || !(action_.owner==context.owner) || action_.epoch!=context.xr_epoch)return false;
        const auto lease=policy_.lease();
        if(lease.generation!=token || lease.source!=Policy::Source::hip || !(lease.scope.owner==context.owner) ||
           rig::value<std::uintptr_t>(reader,lease.scope.controller+0x190)!=lease.scope.item ||
           rig::value<std::uintptr_t>(reader,lease.scope.controller+0x198)!=lease.scope.ammo ||
           rig::value<std::uintptr_t>(reader,lease.scope.inventory+0x70)!=std::optional<std::uintptr_t>{0})return false;
        const auto current=Policy::qualify(base_,read_,lease.scope.state,lease.scope.item,lease.scope.ammo);
        return current && *current==lease.scope && native_player_state_lease(reader,base_,context.owner.machine)==publication_.owner;
    }
    Token begin_wheel(const NativePlayerStateLease& owner) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(holster_.pending || exhausted_ || !context_locked(owner.machine) || !publication_.owner || !(owner==*publication_.owner))return 0;
        if(!revoke_locked())return 0;
        action_={generation_,owner,publication_.xr_epoch,Policy::Source::wheel,Phase::open};last_manual_generation_=generation_;return generation_;
    }
    Token commit_wheel(Token open_token) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(holster_.pending || !matches_locked(open_token,Policy::Source::wheel,Phase::open,action_.owner.machine))return 0;
        const auto token=next_locked();if(!token)return 0;
        action_.token=token;action_.phase=Phase::reserved;
        if(!policy_.reserve_manual(action_.owner,token,Policy::Source::wheel)){revoke_locked();return 0;}
        last_manual_generation_=token;
        return token;
    }
    bool service_game_callback(std::uintptr_t machine) {
        std::lock_guard<std::mutex> lock(mutex_);retire_holster_locked(machine);return arm_locked(machine);
    }
    Status status()const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {installed_,hooks_.enabled(),policy_.writer_enabled(),armed_,exhausted_,publication_.focused,generation_,last_manual_generation_,publication_.xr_epoch,
            hip_grants_,wheel_grants_,revocations_,vetoes_,owner_gaps_,occupancy_gaps_,wheel_rejections_,hooks_.in_flight(),policy_.observation(),
            holster_.pending,holster_.poisoned,holster_receipts_,holster_retirements_,holster_gaps_,last_hip_attempt_,hip_rejections_};
    }
private:
    friend struct NativeManualEquipRuntimeFixture;
    enum class Phase {none,open,reserved,granted};
    struct Action {Token token{};NativePlayerStateLease owner;std::uint64_t epoch{};Policy::Source source{Policy::Source::none};Phase phase{Phase::none};};
    struct WheelCall {NativeManualEquipRuntime* runtime{};std::uintptr_t controller{},item{},ammo{};Token granted{};};
    struct Holster {
        bool pending{},submitting{},receipted{},poisoned{};
        NativePlayerStateLease owner;std::uint64_t epoch{};std::uintptr_t pool{},state{};Policy::Params params;
    };
    struct QueueRecord {std::uint32_t kind{},state{},id{};};
    static_assert(sizeof(QueueRecord)==12);
    struct Reader {
        Read read{};
        static bool cpp(Read read,std::uintptr_t address,void* output,std::size_t size)noexcept {try{return read && read(address,output,size);}catch(...){return false;}}
        bool operator()(std::uintptr_t address,void* output,std::size_t size)const noexcept {
            __try{return cpp(read,address,output,size);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
        }
    };
    void poison_holster_locked(){if(!holster_.poisoned){holster_.poisoned=true;++holster_gaps_;}}
    bool record_hip_locked(HipAttempt attempt,HipReject reason) {
        attempt.current_generation=generation_;attempt.reason=reason;last_hip_attempt_=attempt;
        auto& count=hip_rejections_[static_cast<std::size_t>(reason)];if(count!=std::numeric_limits<std::uint64_t>::max())++count;
        return reason==HipReject::accepted;
    }
    bool holster_context_locked()const {
        Reader reader{read_};
        return installed_ && hooks_.enabled() && publication_.owner && *publication_.owner==holster_.owner &&
            publication_.xr_epoch==holster_.epoch && native_player_state_lease(reader,base_,holster_.owner.machine)==publication_.owner &&
            rig::value<std::uintptr_t>(reader,holster_.owner.machine+0x90)==holster_.pool && state_locked(holster_.owner.machine)==holster_.state;
    }
    bool holster_records_locked(unsigned& copied,unsigned& queued)const {
        Reader reader{read_};const auto machine=holster_.owner.machine;const auto id=holster_.params.get<std::uint32_t>(0);
        const auto params=rig::value<rig::List>(reader,holster_.state+0x30),queue=rig::value<rig::List>(reader,machine+0x220);
        const auto records=rig::list(reader,holster_.state+0x30,4096),requests=rig::list(reader,machine+0x220,4096);
        if(!params || !queue || !records || !requests)return false;
        copied=queued=0;
        for(unsigned index=0;index<records->packed_count;++index){
            Policy::Params value;if(!reader(records->data+index*0x30ull,&value,sizeof(value)))return false;
            if(value.get<std::uint32_t>(0)==id){if(!(value==holster_.params) || ++copied>1)return false;}
        }
        for(unsigned index=0;index<requests->packed_count;++index){
            const auto value=rig::value<QueueRecord>(reader,requests->data+index*12ull);if(!value)return false;
            if(value->id==id){if(value->kind!=1 || value->state!=64 || ++queued>1)return false;}
        }
        const auto params_after=rig::value<rig::List>(reader,holster_.state+0x30),queue_after=rig::value<rig::List>(reader,machine+0x220);
        return params_after && queue_after && std::memcmp(&*params,&*params_after,sizeof(rig::List))==0 &&
            std::memcmp(&*queue,&*queue_after,sizeof(rig::List))==0 && holster_context_locked();
    }
    bool admit_holster(Token release_generation,const NativePlayerStateLease& expected_owner,std::uint64_t xr_epoch,std::uintptr_t machine,const Policy::Params& params) {
        std::lock_guard<std::mutex> lock(mutex_);Reader reader{read_};
        const auto state=state_locked(machine),pool=rig::value<std::uintptr_t>(reader,machine+0x90);
        if(holster_.pending || !state || !pool || !installed_ || !hooks_.enabled() || !publication_.focused || !publication_.owner ||
           !(*publication_.owner==expected_owner) || expected_owner.machine!=machine || !xr_epoch || publication_.xr_epoch!=xr_epoch ||
           !release_generation || release_generation>generation_ || last_manual_generation_>release_generation ||
           !(native_player_state_lease(reader,base_,machine)==publication_.owner) || !params.get<std::uint32_t>(0) || params.get<std::uint32_t>(0)>0x7fffffff ||
           params.get<std::uint32_t>(4)!=0x5c || params.get<std::uintptr_t>(8) || params.get<std::uintptr_t>(16) || params.get<std::uint32_t>(24)!=0)return false;
        holster_={true,true,false,false,expected_owner,xr_epoch,*pool,*state,params};
        unsigned copied{},queued{};
        if(!holster_records_locked(copied,queued) || copied || queued){holster_={};++holster_gaps_;return false;}
        return true;
    }
    bool finish_holster(bool completed) {
        std::lock_guard<std::mutex> lock(mutex_);holster_.submitting=false;unsigned copied{},queued{};
        if(!completed || holster_.poisoned || !holster_records_locked(copied,queued) || copied!=1 || queued!=1){poison_holster_locked();return false;}
        holster_.receipted=true;++holster_receipts_;return true;
    }
    void retire_holster_locked(std::uintptr_t machine) {
        if(!holster_.pending || holster_.submitting || holster_.poisoned)return;
        if(!holster_context_locked()){poison_holster_locked();return;}
        if(!publication_.focused)return;
        unsigned copied{},queued{};Reader reader{read_};
        if(!holster_.receipted || machine!=holster_.owner.machine || !holster_records_locked(copied,queued)){poison_holster_locked();return;}
        const auto active=rig::value<unsigned char>(reader,holster_.state+0x20),deferred=rig::value<unsigned char>(reader,holster_.state+0x150);
        const auto equip_operation=rig::value<std::uintptr_t>(reader,holster_.state+0x108),unequip_operation=rig::value<std::uintptr_t>(reader,holster_.state+0x118);
        if(!active || !deferred || !equip_operation || !unequip_operation){poison_holster_locked();return;}
        if(copied || queued || hooks_.in_flight() || *active || *equip_operation || *unequip_operation || *deferred)return;
        if(!holster_context_locked()){poison_holster_locked();return;}
        holster_={};++holster_retirements_;
    }
    Token next_locked() {
        if(exhausted_)return 0;
        if(generation_>=std::numeric_limits<Token>::max()-1){
            generation_=std::numeric_limits<Token>::max();exhausted_=true;action_={};policy_.revoke(generation_);++revocations_;return 0;
        }
        return ++generation_;
    }
    bool revoke_locked() {
        const auto token=next_locked();action_={};
        if(!token)return false;policy_.revoke(token);++revocations_;return true;
    }
    bool context_locked(std::uintptr_t machine) {
        if(!installed_ || !hooks_.enabled() || !publication_.focused || !publication_.xr_epoch || !publication_.owner || publication_.owner->machine!=machine)return false;
        Reader reader{read_};
        if(native_player_state_lease(reader,base_,machine)==publication_.owner)return true;
        ++owner_gaps_;revoke_locked();armed_=false;return false;
    }
    bool matches_locked(Token token,Policy::Source source,Phase phase,std::uintptr_t machine) {
        return token && action_.token==token && action_.source==source && action_.phase==phase && action_.epoch==publication_.xr_epoch &&
            publication_.owner && action_.owner==*publication_.owner && context_locked(machine);
    }
    std::optional<std::uintptr_t> state_locked(std::uintptr_t machine)const {
        Reader reader{read_};const auto pool=rig::value<std::uintptr_t>(reader,machine+0x90);
        const auto state=pool?rig::value<std::uintptr_t>(reader,*pool+0x10+64*8):std::nullopt;
        if(!state || !rig::typed(reader,base_,*state,0x1c79cf8) || rig::value<int>(reader,*state+8)!=64 || rig::value<std::uintptr_t>(reader,*state+0x10)!=machine)return {};
        return state;
    }
    std::optional<bool> occupied_locked(std::uintptr_t machine) {
        if(!publication_.physical_left_occupied){++occupancy_gaps_;return {};}
        Reader reader{read_};const auto body=rig::value<std::uintptr_t>(reader,machine+0x300);
        const auto carry=body && publication_.owner?rig::native_carry_snapshot(reader,base_,publication_.owner->player,*body):std::nullopt;
        if(!carry || carry->machine!=machine || carry->machine_identity!=publication_.owner->identity){++occupancy_gaps_;return {};}
        return *publication_.physical_left_occupied || (carry->hands()&1u)!=0;
    }
    bool arm_locked(std::uintptr_t machine) {
        if(!context_locked(machine))return false;
        if(armed_)return true;
        const auto state=state_locked(machine);if(!state || !occupied_locked(machine))return false;
        armed_=policy_.enable_at_idle(*state);return armed_;
    }
    std::optional<Policy::Scope> wheel_scope_locked(std::uintptr_t controller,std::uintptr_t item_decl,std::uintptr_t ammo_decl) {
        Reader reader{read_};const auto machine=rig::value<std::uintptr_t>(reader,controller+0x58);
        if(!machine || !context_locked(*machine) || !rig::typed(reader,base_,controller,0x1c72918) ||
           rig::value<std::uintptr_t>(reader,controller+0x10)!=publication_.owner->player || rig::value<std::uintptr_t>(reader,*machine+0x2f8)!=controller ||
           rig::value<unsigned char>(reader,controller+0x171)!=1)return {};
        const auto inventory=rig::value<std::uintptr_t>(reader,*machine+0x328),state=state_locked(*machine);
        if(!inventory || !state || !rig::typed(reader,base_,*inventory,0x1b9a878) || rig::value<std::uintptr_t>(reader,*inventory+0x10)!=publication_.owner->player)return {};
        const auto entries=rig::list(reader,*inventory+0x38,4096);if(!entries || !item_decl)return {};
        std::uintptr_t item{},ammo{};
        for(unsigned index=0;index<entries->packed_count;++index){
            const auto record=entries->data+index*0x38ull;const auto declaration=rig::value<std::uintptr_t>(reader,record);if(!declaration)return {};
            if(*declaration==item_decl){if(item)return {};item=record;}
            if(ammo_decl && *declaration==ammo_decl){if(ammo)return {};ammo=record;}
        }
        if(!item || (ammo_decl && !ammo))return {};
        return Policy::qualify(base_,read_,*state,item,ammo);
    }
    static bool processor_host(void* context,std::uintptr_t,std::uintptr_t state,Hooks::Params& params) {
        auto& self=*static_cast<NativeManualEquipRuntime*>(context);std::lock_guard<std::mutex> lock(self.mutex_);
        Reader reader{self.read_};const auto machine=rig::value<std::uintptr_t>(reader,state+0x10);
        if(machine) {self.keep_held_sword_locked(*machine,state,params);self.keep_left_hand_in_carry(*machine,state,params);self.veto_right_draw_locked(*machine,state,params);self.instant_left_equip_locked(*machine,state,params);}
        if(!machine || !self.context_locked(*machine) || self.state_locked(*machine)!=std::optional<std::uintptr_t>{state})return true;
        if(!self.armed_ && !self.arm_locked(*machine))return true;
        const auto occupied=self.occupied_locked(*machine);if(!occupied)self.revoke_locked();
        Policy::Params owned{};owned.bytes=params.bytes;
        if(!self.policy_.prepare(state,owned,occupied.value_or(true)))return false;
        params.bytes=owned.bytes;const auto observation=self.policy_.observation();if(observation.changed)++self.vetoes_;
        if(observation.holster_revoke)self.revoke_locked();return true;
    }
    static bool wheel_before(void* context,std::uintptr_t caller,std::uintptr_t controller,std::uintptr_t item,std::uintptr_t ammo) {
        wheel_choice_decl.store(item,std::memory_order_relaxed);wheel_choice_seq.fetch_add(1,std::memory_order_release);   // the chosen item, whatever the grant decides (dual wield: the exact same-item test)
        if(const auto pick=wheel_pick.load(std::memory_order_acquire)) if(const auto keep=pick(item,ammo)) Hooks::wheel_ammo_override.store(keep,std::memory_order_release); // v1101: a copy hand's ammo choice
        auto& self=*static_cast<NativeManualEquipRuntime*>(context);std::lock_guard<std::mutex> lock(self.mutex_);
        if(wheel_depth_>=wheel_calls_.size()){++wheel_overflow_;self.revoke_locked();return false;}
        auto& call=wheel_calls_[wheel_depth_++];call={&self,controller,item,ammo,0};
        if(self.holster_.pending || !Policy::actual_wheel_origin(self.base_,caller) || !self.matches_locked(self.action_.token,Policy::Source::wheel,Phase::reserved,self.action_.owner.machine)){
            // why (status wheel_reject_why, v763: 16 of 30 headset wheel choices were refused): 0 holster pending, 1 not the wheel's call,
            // 2 the lease still open (the release not yet committed), 3 another lease or phase
            ++wheel_reject_why[self.holster_.pending?0:!Policy::actual_wheel_origin(self.base_,caller)?1:
                (self.action_.source==Policy::Source::wheel && self.action_.phase==Phase::open)?2:3];
            ++self.wheel_rejections_;if(self.action_.source==Policy::Source::wheel && self.action_.phase==Phase::open)self.revoke_locked();return true;
        }
        const auto machine=self.action_.owner.machine;
        const auto occupied=self.occupied_locked(machine);const auto scope=self.wheel_scope_locked(controller,item,ammo);
        unsigned why=!occupied?4u:*occupied?5u:!scope?6u:0u; // 4 occupancy unknown, 5 occupied, 6 no scope, 7 not armed, 8 grant refused
        if(!why && !self.arm_locked(machine)) why=7;
        if(!why && !self.policy_.grant(self.action_.token,*scope)) why=8;
        if(why){++wheel_reject_why[why];++self.wheel_rejections_;self.revoke_locked();return true;}
        call.granted=self.action_.token;self.action_.phase=Phase::granted;++self.wheel_grants_;return true;
    }
    static bool wheel_after(void* context,std::uintptr_t,std::uintptr_t controller,std::uintptr_t item,std::uintptr_t ammo,bool completed,bool current) {
        auto& self=*static_cast<NativeManualEquipRuntime*>(context);std::lock_guard<std::mutex> lock(self.mutex_);
        if(wheel_overflow_){--wheel_overflow_;return true;}
        if(!wheel_depth_)return false;
        const auto call=wheel_calls_[--wheel_depth_];wheel_calls_[wheel_depth_]={};
        if(call.runtime!=&self || call.controller!=controller || call.item!=item || call.ammo!=ammo){self.revoke_locked();return false;}
        if(call.granted && call.granted==self.action_.token && (!completed || !current || !self.context_locked(self.action_.owner.machine)))self.revoke_locked();
        return true;
    }
    // v692 (user, headset v693: "the sword is only in the hand when I take it from my back or pick it up from the floor"): the
    // right hand's EquipChange (state 53, the same class and processor 8363f0 as the left's 64) draws only for the mod's own
    // requests (SwordGrip draw 0x04 / pickup 0x44, authorized by request id). The game's own right draws are taken out: code 4/8
    // ("draw the remembered item", which falls back to the default sword with an empty hand: the restore after Choke/the cure,
    // HoldObject exit 0x16, the Undraw toggle, a left Draw 8's other-hand request) and an explicit item (the wheel). Lock and
    // unlock bits stay, so the game's lock bookkeeping is unchanged (docs/native-weapon-equip-research.md 2b/2c).
    void veto_right_draw_locked(std::uintptr_t machine,std::uintptr_t state,Hooks::Params& params) {
        if(!right_draw_veto.load(std::memory_order_relaxed))return;
        Reader reader{read_};const auto pool=rig::value<std::uintptr_t>(reader,machine+0x90);
        if(!pool || rig::value<std::uintptr_t>(reader,*pool+0x10+53*8)!=state || !rig::typed(reader,base_,state,0x1c79cf8) ||
           rig::value<int>(reader,state+8)!=53 || !native_player_state_lease(reader,base_,machine))return;
        std::uint32_t id{},code{};std::uintptr_t item{};
        std::memcpy(&id,params.bytes.data(),4);std::memcpy(&item,params.bytes.data()+8,8);std::memcpy(&code,params.bytes.data()+0x18,4);
        if(!(code&0x0cu) && !item)return;
        if(right_draw_authorized(id)){++right_draws_allowed;return;}
        // v780 (user, headset v779: "the sword in my hand folds away when I choose a weapon for the OTHER hand"; 65 vetoes, 1 sheathe of the mod's
        // own): a left draw asks the right hand to draw too (code 8), and the emptied request put away the sword the hand was holding. A sword its
        // hand holds (SwordGrip) keeps the game's request: the veto is for a sword drawn into an empty hand.
        if(sword_kept.load(std::memory_order_relaxed)){++right_draws_kept;return;}
        const bool from_draw=(code&0x08u)!=0;
        code&=~0x0cu;item=0;std::memcpy(params.bytes.data()+0x18,&code,4);std::memcpy(params.bytes.data()+8,&item,8);
        std::memset(params.bytes.data()+0x10,0,8);++right_draw_vetoes;
        // Code 8 comes from Draw (54, "attack with an empty hand"), which requests this synchronously from its activate 831670
        // after storing its follow-on state at +0x30; its update 8319e0 waits for the hand to fill unless +0x30 is 0x5b, then
        // stops itself (jmp 854190). Give it 0x5b so it ends instead of waiting for a sword that never comes.
        if(from_draw) if(const auto draw=rig::value<std::uintptr_t>(reader,*pool+0x10+54*8);draw && *draw && rig::value<int>(reader,*draw+8)==54)
            __try {*reinterpret_cast<volatile std::uint32_t*>(*draw+0x30)=0x5b;++right_draw_ends;} __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    // v782 (user, headset v780: "picking up a bottle or a glass makes the sword disappear"): taking a throwable (HoldObject entry 83d770, requester
    // 49) locks and empties the right hand (code 0x21), where the game holds such objects (docs/native-weapon-equip-research.md 2c). A sword its
    // hand holds (the object is then in the other hand) keeps the lock but stays drawn: the request draws the remembered item instead (0x04 with a
    // lock bit: only remembered while locked), so the hand is not emptied. HoldObject's exit (0x16) unlocks as before. build/hold-sheathes-sword: v781.
    void keep_held_sword_locked(std::uintptr_t machine,std::uintptr_t state,Hooks::Params& params) {
        // v790: never when the object goes into the sword's own hand (the lock keeps the sword out of that hand; headset v799 drew it into the bottle)
        const bool no_lock=hold_no_lock.load(std::memory_order_relaxed) && !hold_in_sword_hand.load(std::memory_order_relaxed),keep=hold_keeps_sword.load(std::memory_order_relaxed) && sword_kept.load(std::memory_order_relaxed);
        if(!no_lock && !keep)return;
        Reader reader{read_};const auto pool=rig::value<std::uintptr_t>(reader,machine+0x90);
        if(!pool || rig::value<std::uintptr_t>(reader,*pool+0x10+53*8)!=state || !rig::typed(reader,base_,state,0x1c79cf8) ||
           rig::value<int>(reader,state+8)!=53 || !native_player_state_lease(reader,base_,machine))return;
        std::uintptr_t item{};std::uint32_t code{},requester{};
        std::memcpy(&item,params.bytes.data()+8,8);std::memcpy(&code,params.bytes.data()+0x18,4);std::memcpy(&requester,params.bytes.data()+0x1c,4);
        if(item || requester!=49 || !(code&0x01u) || (code&0x0eu))return; // only HoldObject's lock-and-empty
        // v789 (user, headset v796: "a bottle is just an item in the hand, like a weapon: it must not block the other hand or drawing the sword"):
        // with dual wield the lock bit goes. The lock (S+0x158) kept every later draw of this hand to a remembered selection for the whole
        // HoldObject (no sword from the back, no thrown sword taken back). Without it the empty stays (0x20 instant), a held sword is kept by
        // drawing the selected item (0x04 without lock bits = the selected one, the drawn sword), and HoldObject's exit (0x16) finds no lock
        // and no restore item (an unequip without 0x1 clears it): the hand stays as the player left it. knob build/hold-locks-sword: v782.
        if(no_lock) {code&=~0x01u;++hold_locks_dropped;}
        if(keep) {code|=0x04u;++hold_swords_kept;}
        std::memcpy(params.bytes.data()+0x18,&code,4);
    }
    // v1038 (user, headset v1037: "carrying a corpse, my hands are disabled: no powers, no weapons"): CarryCorpse (requester 45) locks and
    // empties the LEFT hand too (activate 828770 case 0), though its decl takes the right arm only. shoulder_carry.hpp carry_left_equip_code
    // drops the left lock (v1041: the item is still put away, the carry needs the hand empty; CarryLeftRedraw draws it again), so every later draw, wheel pick and power switch goes through; the right
    // hand stays the carry's (the sword cannot be drawn while a body is on the shoulder: Slash is blocked by the carry's right-arm lock).
    void keep_left_hand_in_carry(std::uintptr_t machine,std::uintptr_t state,Hooks::Params& params) {
        if(!carry_keeps_left.load(std::memory_order_relaxed))return;
        Reader reader{read_};const auto pool=rig::value<std::uintptr_t>(reader,machine+0x90);
        if(!pool || rig::value<std::uintptr_t>(reader,*pool+0x10+64*8)!=state || !rig::typed(reader,base_,state,0x1c79cf8) ||
           rig::value<int>(reader,state+8)!=64 || !native_player_state_lease(reader,base_,machine))return;
        std::uintptr_t item{};std::uint32_t code{},requester{};
        std::memcpy(&item,params.bytes.data()+8,8);std::memcpy(&code,params.bytes.data()+0x18,4);std::memcpy(&requester,params.bytes.data()+0x1c,4);
        const auto kept=carry_left_equip_code(code,requester,item!=0);
        if(kept==code)return;
        std::memcpy(params.bytes.data()+0x18,&kept,4);++carry_left_kept;
        if(left_holds_item.load(std::memory_order_relaxed)) carry_left_unequips.fetch_add(1,std::memory_order_release);   // v1041: the control thread draws it again (CarryLeftRedraw)
    }
    // v781 (user, headset v780: "a weapon picked up or chosen from the wheel must be in the hand at once; a caught mine sits in the hand only after
    // a moment"): the left hand's EquipChange (state 64: guns, gadgets, powers, the Heart) draws without its equip midnight. Code bit 0x10 makes
    // 836120 pick no scene (the Heart's 0x30 restore was immediate); with tracked arms the scene only delayed the item. Unequips are untouched.
    void instant_left_equip_locked(std::uintptr_t machine,std::uintptr_t state,Hooks::Params& params) {
        if(!instant_left_equip.load(std::memory_order_relaxed))return;
        Reader reader{read_};const auto pool=rig::value<std::uintptr_t>(reader,machine+0x90);
        if(!pool || rig::value<std::uintptr_t>(reader,*pool+0x10+64*8)!=state || !rig::typed(reader,base_,state,0x1c79cf8) ||
           rig::value<int>(reader,state+8)!=64 || !native_player_state_lease(reader,base_,machine))return;
        std::uintptr_t item{};std::uint32_t code{};std::memcpy(&item,params.bytes.data()+8,8);std::memcpy(&code,params.bytes.data()+0x18,4);
        if((!item && !(code&0x0cu)) || (code&0x10u))return; // an unequip, or already without the scene
        code|=0x10u;std::memcpy(params.bytes.data()+0x18,&code,4);++left_equips_instant;
    }
public:
    inline static std::atomic<bool> instant_left_equip{true}; // build/equip-midnight: the game's equip scenes (v780)
    inline static std::atomic<std::uint64_t> left_equips_instant{},hold_swords_kept{};
    inline static std::atomic<bool> hold_in_sword_hand{true}; // v790: the hand taking the HoldObject object is the sword's (dxgi, each control frame)
    inline static std::atomic<bool> hold_no_lock{false}; // v789: dual wield (knob build/hold-locks-sword keeps the lock)
    inline static std::atomic<std::uint64_t> hold_locks_dropped{};
    inline static std::atomic<bool> hold_keeps_sword{true}; // build/hold-sheathes-sword: HoldObject puts a held sword away (v781)
    inline static std::atomic<bool> carry_keeps_left{true}; // v1038: build/carry-locks-hands restores the game's lock of the left hand
    inline static std::atomic<bool> left_holds_item{}; // v1038: the left slot holds an item (gun, gadget, power, the Heart): dxgi, each hand-state read
    inline static std::atomic<std::uint64_t> carry_left_kept{},carry_left_unequips{};   // v1041: unequips of a held left item the carry required (it needs both hands empty)
    inline static std::atomic<bool> right_draw_veto{true};
    inline static std::array<std::atomic<std::uint64_t>,9> wheel_reject_why{}; // see wheel_before
    inline static std::atomic<std::uint64_t> right_draw_vetoes{},right_draws_allowed{},right_draw_ends{},right_draws_kept{};
    // The declaration the game's equip 0x7fb350 was last asked for (the wheel's choice) and a counter of its calls, written on the game thread at entry,
    // before the game decides to skip an item already in the left hand (docs/native-weapon-equip-research.md 2c). dxgi_probe.cpp compares it with the
    // declaration slot 1 held when the wheel closed: the same item gives the opening arm a copy at once, a different one waits for the ticketed arrival.
    inline static std::atomic<std::uintptr_t> wheel_choice_decl{};inline static std::atomic<std::uint64_t> wheel_choice_seq{};
    // v1101: asked with the wheel's (item, ammo) before the game's equip; returns the ammo item the game should equip instead, 0 = as chosen.
    inline static std::atomic<std::uintptr_t (*)(std::uintptr_t,std::uintptr_t)> wheel_pick{};
    inline static std::atomic<bool> sword_kept{}; // v780: the sword is drawn and its hand holds it (input thread, SwordGrip)
    // Game thread (NativeWeaponHands::equip): the request id 835590 gave a SwordGrip draw or pickup.
    static void authorize_right_draw(std::uint32_t id) noexcept {authorized_[authorized_next_.fetch_add(1)%authorized_.size()].store(id);}
    static bool right_draw_authorized(std::uint32_t id) noexcept {
        if(!id) return false;
        for(auto& a:authorized_) {std::uint32_t want=id;if(a.compare_exchange_strong(want,0u)) return true;}
        return false;
    }
private:
    inline static std::array<std::atomic<std::uint32_t>,8> authorized_{};inline static std::atomic<unsigned> authorized_next_{};
    Hooks& hooks_;Policy policy_;mutable std::mutex mutex_;
    std::uintptr_t base_{};Read read_{};Publication publication_;Action action_;Holster holster_;
    bool installed_{},armed_{},exhausted_{};
    Token generation_{},last_manual_generation_{};
    std::uint64_t hip_grants_{},wheel_grants_{},revocations_{},vetoes_{},owner_gaps_{},occupancy_gaps_{},wheel_rejections_{};
    std::uint64_t holster_receipts_{},holster_retirements_{},holster_gaps_{};
    HipAttempt last_hip_attempt_;
    std::array<std::uint64_t,static_cast<std::size_t>(HipReject::count)> hip_rejections_{};
    static thread_local std::array<WheelCall,8> wheel_calls_; // defined after the class: clang-cl needs WheelCall's member initializers complete
    inline static thread_local unsigned wheel_depth_{};
    inline static thread_local unsigned wheel_overflow_{};
};
inline thread_local std::array<NativeManualEquipRuntime::WheelCall,8> NativeManualEquipRuntime::wheel_calls_{};
}
