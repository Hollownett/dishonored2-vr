#pragma once
#include "native_held_object.hpp"
#include "native_left_weapon_socket_owner.hpp"
#include "native_attachment_pose.hpp"
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <type_traits>

namespace d2vr {
class LeftWeaponRenderReceipt {
    friend struct LeftWeaponRenderReceiptCheck;
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    struct Pose {std::array<float,3> position{},linear{},angular{};std::array<float,9> axis{};bool valid{};};
    struct Row {
        std::uint64_t activation{},id{},tick{},publication_revision{},token{},pose{},reference{},mapping{},publication_tick{};
        std::uintptr_t caller{},item{},entity{},model{},rf{},parent{},arm_model{},animation{},inventory{};
        std::uint32_t spawn{},parent_spawn{},player_spawn{},thread{};
        rig::Identity rf_identity{},animation_identity{};
        int mode{};short joint{};float dt{};unsigned reasons{},reader_count{},reader_stage{};
        std::uint64_t reader_revision{},reader_tick{},reader_source_tick{};
        std::array<float,15> arm_model_frame{};rig::Transform selected_local{};
        bool reader_substituted{},reader_returned{},local_valid{},native_seen{},final_seen{},placement_branch_selected{};
        NativeHeldObject::Target held_before{},held_after{};bool held_before_active{},held_after_active{};
        Pose native{},final{};
    };
    static_assert(std::is_trivially_copyable_v<Row>);
private:
    struct Pending {Row row{};NativeLeftWeaponSocketOwner::Diagnostic owner{};bool sampled{};};
    static thread_local std::array<Pending,8> pending_; // defined after the class: clang-cl needs Pending's member initializers complete
    inline static thread_local unsigned depth_{};
    std::atomic<bool> active_{},drain_failed_{},reconfigurable_{true},write_failed_{};Read read_{};std::uintptr_t base_{};
    std::atomic<std::uint64_t> next_tick_{},attempts_{},calls_{},missing_owner_{},other_binding_{},busy_{},overflow_{},cap_{},dropped_{},written_{};
    std::atomic<std::uint64_t> callback_calls_{},callback_ticks_{},callback_max_ticks_{};
    std::uint64_t activation_{},frequency_{},last_summary_tick_{};
    std::mutex queue_mutex_;std::array<Row,256> rows_{};unsigned head_{},count_{};
    std::ofstream log_;std::filesystem::path summary_;bool limit_{};std::uint64_t bytes_{};
    bool try_sample(std::uint64_t now,std::uint64_t& id) noexcept {
        if(!enabled())return false;
        auto expected=next_tick_.load(std::memory_order_relaxed);
        if(now<expected || !next_tick_.compare_exchange_strong(expected,now+100)){++busy_;return false;}
        id=attempts_.fetch_add(1)+1;if(id>32768){++cap_;active_=false;return false;}
        return true;
    }
    bool read(std::uintptr_t at,void* out,std::size_t bytes) noexcept {
        try{return read_ && read_(at,out,bytes);}catch(...){return false;}
    }
    bool current(const NativeLeftWeaponSocketOwner::Diagnostic& owner) noexcept {
        try {auto safe=[&](std::uintptr_t at,void* out,std::size_t bytes){return read(at,out,bytes);};
            return owner.lease && native_equipment_current(safe,owner.lease->equipment);
        }catch(...){return false;}
    }
    static bool same_publication(const NativeLeftWeaponSocketOwner::Diagnostic& first,const NativeLeftWeaponSocketOwner::Diagnostic& second) noexcept {
        return first.token==second.token && first.revision==second.revision && first.lease==second.lease &&
            first.binding==second.binding && first.epochs==second.epochs;
    }
    template<class Value> static bool saved(const NativeEquipmentOwnership& equipment,std::uintptr_t at,Value& out) noexcept {
        for(const auto& guard:equipment.guards)if(guard.address<=at && at-guard.address<=guard.bytes.size() &&
            sizeof(Value)<=guard.bytes.size()-(at-guard.address)) {
            std::memcpy(&out,guard.bytes.data()+(at-guard.address),sizeof(Value));return true;
        }
        return false;
    }
    Pose pose(const float* position,const float* axis,const float* linear,const float* angular) noexcept {
        Pose out;
        if(!position || !axis || !read(reinterpret_cast<std::uintptr_t>(position),out.position.data(),sizeof(out.position)) ||
           !read(reinterpret_cast<std::uintptr_t>(axis),out.axis.data(),sizeof(out.axis)))return {};
        if(linear && !read(reinterpret_cast<std::uintptr_t>(linear),out.linear.data(),sizeof(out.linear)))return {};
        if(angular && !read(reinterpret_cast<std::uintptr_t>(angular),out.angular.data(),sizeof(out.angular)))return {};
        for(float value:out.position)if(!std::isfinite(value))return {};
        for(float value:out.axis)if(!std::isfinite(value))return {};
        for(float value:out.linear)if(!std::isfinite(value))return {};
        for(float value:out.angular)if(!std::isfinite(value))return {};
        out.valid=true;return out;
    }
    void enqueue(const Row& row) noexcept {
        try {
            std::unique_lock<std::mutex> lock(queue_mutex_,std::try_to_lock);
            if(!lock.owns_lock() || count_==rows_.size()){++dropped_;return;}
            rows_[(head_+count_)%rows_.size()]=row;++count_;
        }catch(...){++dropped_;}
    }
    void bind(unsigned phase,std::uintptr_t rf,int mode,std::uintptr_t caller,const float* position,const float* axis,
              const float* linear,const float* angular,float dt) noexcept {
        if(phase==0) {
            ++depth_;if(depth_>pending_.size()){++overflow_;return;}
            auto& pending=pending_[depth_-1];pending={};
            if(!active_.load(std::memory_order_acquire))return;
            ++calls_;const auto now=GetTickCount64();if(now<next_tick_.load(std::memory_order_relaxed))return;
            auto owner=NativeLeftWeaponSocketOwner::process_owner().diagnostic();
            if(!owner.token || !owner.lease || !owner.binding){++missing_owner_;return;}
            const auto& equipment=owner.lease->equipment;const auto& candidate=equipment.attachment;
            if(candidate.binding!=rf || candidate.arm.side!=0 || candidate.joint!=59){++other_binding_;return;}
            std::uint64_t id{};if(!try_sample(now,id))return;
            pending.sampled=true;pending.owner=std::move(owner);auto& row=pending.row;
            row.activation=activation_;row.id=id;row.tick=now;row.thread=GetCurrentThreadId();row.caller=caller>=base_?caller-base_:0;row.mode=mode;
            if(std::isfinite(dt))row.dt=dt;else row.reasons|=1024u;
            row.token=pending.owner.token;row.publication_revision=pending.owner.revision;row.publication_tick=pending.owner.tick;
            row.pose=pending.owner.epochs.pose;row.reference=pending.owner.epochs.reference;row.mapping=pending.owner.epochs.mapping;
            row.item=equipment.item;row.entity=candidate.entity;row.rf=rf;row.parent=candidate.parent;
            row.spawn=equipment.child_id;row.parent_spawn=equipment.parent_id;row.player_spawn=equipment.player_id;
            row.inventory=candidate.inventory;row.rf_identity=candidate.binding_identity;row.arm_model=candidate.arm.model;
            row.animation=candidate.arm.animation;row.animation_identity=candidate.arm.animation_identity;row.joint=candidate.joint;
            if(!saved(equipment,candidate.entity+0xf0,row.model) || !row.model)row.reasons|=1u;
            if(!current(pending.owner))row.reasons|=2u;
            if(!row.publication_tick || now<row.publication_tick || now-row.publication_tick>100)row.reasons|=512u;
            return;
        }
        if(!depth_)return;
        if(depth_>pending_.size()){if(phase==3)--depth_;return;}
        auto& pending=pending_[depth_-1];
        if(pending.sampled) {
            auto& row=pending.row;
            if(phase==1) {
                row.native_seen=true;row.native=pose(position,axis,linear,angular);
                row.held_before_active=NativeHeldObject::diagnostic_target(row.held_before);
            }else if(phase==2) {
                row.final_seen=true;row.final=pose(position,axis,linear,angular);
                row.held_after_active=NativeHeldObject::diagnostic_target(row.held_after);
                const auto owner=NativeLeftWeaponSocketOwner::process_owner().diagnostic();
                if(!same_publication(owner,pending.owner) || !current(pending.owner))row.reasons|=4u;
            }else if(phase==4)row.placement_branch_selected=true;
            else if(phase==3) {
                if(!row.native_seen || !row.final_seen)row.reasons|=8u;
                if(!row.native.valid || !row.final.valid)row.reasons|=16u;
                if(!row.reader_count)row.reasons|=32u;
                if(!row.reader_substituted || !row.reader_returned || !row.local_valid)row.reasons|=64u;
                enqueue(row);
            }
        }
        if(phase==3){pending={};--depth_;}
    }
    static void output_pose(std::ostream& out,const Pose& pose) {
        out<<"{\"valid\":"<<pose.valid<<",\"position\":[";array(out,pose.position);out<<"],\"axis\":[";array(out,pose.axis);
        out<<"],\"linear\":[";array(out,pose.linear);out<<"],\"angular\":[";array(out,pose.angular);out<<"]}";
    }
    static void number(std::ostream& out,float value){if(std::isfinite(value))out<<value;else out<<"null";}
    template<class Values> static void array(std::ostream& out,const Values& values){bool first=true;for(auto value:values){if(!first)out<<',';number(out,value);first=false;}}
    static void output_target(std::ostream& out,const NativeHeldObject::Target& target,bool active) {
        out<<"{\"active\":"<<active<<",\"rf\":"<<target.rf<<",\"entity\":"<<target.entity<<",\"serial\":"<<target.placement_serial
           <<",\"pose_epoch\":"<<target.pose_epoch<<",\"side\":"<<target.source_side<<",\"tick\":"<<target.tick<<'}';
    }
public:
    static LeftWeaponRenderReceipt& process_owner(){static auto* value=new LeftWeaponRenderReceipt;return *value;}
    bool enabled()const noexcept{return active_.load(std::memory_order_acquire);}
    bool failed()const noexcept{return write_failed_.load(std::memory_order_acquire) || drain_failed_.load(std::memory_order_acquire);}
    bool configure(Read read,std::uintptr_t base,const std::filesystem::path& output) noexcept {
        if(output.empty())return false;   // v1046: diagnostics off = never active, and not a write failure (failed() gates the teardown success)
        if(!reconfigurable_.exchange(false,std::memory_order_acq_rel))return false;
        active_=false;
        try {
            read_=read;base_=base;next_tick_=0;attempts_=0;calls_=0;missing_owner_=0;other_binding_=0;busy_=0;overflow_=0;cap_=0;dropped_=0;written_=0;drain_failed_=false;
            activation_=GetTickCount64();last_summary_tick_=0;callback_calls_=0;callback_ticks_=0;callback_max_ticks_=0;
            LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);frequency_=frequency.QuadPart>0?static_cast<std::uint64_t>(frequency.QuadPart):0;
            {std::lock_guard<std::mutex> lock(queue_mutex_);head_=count_=0;}
            if(log_.is_open())log_.close();log_.clear();summary_=output/L"left-weapon-bind-summary.json";write_failed_=false;limit_=false;bytes_=0;
            log_.open(output/L"left-weapon-bind-receipts.jsonl",std::ios::trunc);log_<<std::setprecision(9)<<std::boolalpha;
            active_=read_ && base_ && log_.is_open() && bool(log_);if(!enabled())write_failed_=true;return enabled();
        }catch(...){write_failed_=true;return false;}
    }
    static void observe(unsigned phase,std::uintptr_t rf,int mode,std::uintptr_t caller,const float* position,const float* axis,
                        const float* linear,const float* angular,float dt) noexcept {
        auto& self=process_owner();const bool timed=self.enabled();LARGE_INTEGER begin{},end{};
        if(timed)QueryPerformanceCounter(&begin);
        self.bind(phase,rf,mode,caller,position,axis,linear,angular,dt);
        if(timed){QueryPerformanceCounter(&end);const auto elapsed=end.QuadPart>=begin.QuadPart?static_cast<std::uint64_t>(end.QuadPart-begin.QuadPart):0;
            ++self.callback_calls_;self.callback_ticks_+=elapsed;auto peak=self.callback_max_ticks_.load();
            while(elapsed>peak && !self.callback_max_ticks_.compare_exchange_weak(peak,elapsed)) {}}
    }
    void reader(const NativeAttachmentSource& source,std::uintptr_t animation,short joint,unsigned stage,
                bool substituted,bool returned,const rig::Transform* local) noexcept {
        if(!depth_ || depth_>pending_.size())return;auto& pending=pending_[depth_-1];if(!pending.sampled)return;
        auto& row=pending.row;
        if(source.diagnostic_side!=0 || source.diagnostic_socket_token!=row.token || animation!=row.animation || source.model!=row.arm_model || joint!=59 ||
           source.animation_identity!=row.animation_identity ||
           source.epoch!=row.pose || source.reference_epoch!=row.reference || source.mapping_revision!=row.mapping){row.reasons|=128u;return;}
        if(row.reader_count && (row.reader_revision!=source.revision || row.arm_model_frame!=source.model_frame ||
           (local && row.local_valid && std::memcmp(row.selected_local.f.data(),local->f.data(),sizeof(local->f)))))row.reasons|=256u;
        ++row.reader_count;row.reader_revision=source.revision;row.reader_tick=source.tick;row.reader_source_tick=source.source_tick;
        row.reader_stage=stage;row.reader_substituted=substituted;row.reader_returned=returned;row.arm_model_frame=source.model_frame;
        if(!substituted || !returned || !local)row.reasons|=64u;
        const auto now=GetTickCount64();
        if(!source.source_tick || now<source.source_tick || now-source.source_tick>250 || !source.tick || now<source.tick || now-source.tick>250)row.reasons|=512u;
        for(float value:row.arm_model_frame)if(!std::isfinite(value)){row.arm_model_frame={};row.reasons|=1024u;break;}
        if(local){row.local_valid=rig::valid(*local);for(float value:local->f)if(!std::isfinite(value))row.local_valid=false;
            if(row.local_valid)row.selected_local=*local;else{row.selected_local={};row.reasons|=64u;}}
    }
    void retire(bool drained) noexcept {
        if(!drained)drain_failed_=true;flush(drained);if(drained)reconfigurable_=true;
    }
    void flush(bool final=false) noexcept {
        if(final)active_=false;
        try {
            for(;;) {Row row;{std::lock_guard<std::mutex> lock(queue_mutex_);if(!count_)break;row=rows_[head_];head_=(head_+1)%rows_.size();--count_;}
                if(!log_.is_open() || limit_){++dropped_;continue;}
                if(bytes_>=64ull*1024*1024-32768){limit_=true;active_=false;++dropped_;continue;}
                log_<<"{\"activation\":"<<row.activation<<",\"id\":"<<row.id<<",\"tick\":"<<row.tick<<",\"thread\":"<<row.thread<<",\"caller_rva\":"<<row.caller
                    <<",\"item\":"<<row.item<<",\"entity\":"<<row.entity<<",\"fullspawn\":"<<row.spawn<<",\"weapon_model\":"<<row.model
                    <<",\"rf\":"<<row.rf<<",\"rf_generation\":"<<row.rf_identity.generation<<",\"rf_flags\":"<<row.rf_identity.flags
                    <<",\"parent\":"<<row.parent<<",\"parent_fullspawn\":"<<row.parent_spawn<<",\"player_fullspawn\":"<<row.player_spawn
                    <<",\"arm_model\":"<<row.arm_model<<",\"animation\":"<<row.animation<<",\"animation_generation\":"<<row.animation_identity.generation
                    <<",\"animation_flags\":"<<row.animation_identity.flags
                    <<",\"inventory\":"<<row.inventory<<",\"joint\":"<<row.joint<<",\"mode\":"<<row.mode<<",\"dt\":";number(log_,row.dt);
                log_<<",\"token\":"<<row.token<<",\"publication_revision\":"<<row.publication_revision<<",\"publication_tick\":"<<row.publication_tick
                    <<",\"pose_epoch\":"<<row.pose<<",\"reference_epoch\":"<<row.reference<<",\"mapping_revision\":"<<row.mapping
                    <<",\"reasons\":"<<row.reasons<<",\"reader_count\":"<<row.reader_count<<",\"reader_revision\":"<<row.reader_revision
                    <<",\"reader_tick\":"<<row.reader_tick<<",\"reader_source_tick\":"<<row.reader_source_tick<<",\"reader_stage\":"<<row.reader_stage
                    <<",\"substituted\":"<<row.reader_substituted<<",\"reader_returned\":"<<row.reader_returned<<",\"local_valid\":"<<row.local_valid
                    <<",\"selected_local59\":[";array(log_,row.selected_local.f);log_<<"],\"arm_model_frame\":[";array(log_,row.arm_model_frame);
                log_<<"],\"native_composed\":";output_pose(log_,row.native);log_<<",\"post_placement\":";output_pose(log_,row.final);
                log_<<",\"placement_branch_selected\":"<<row.placement_branch_selected;
                log_<<",\"held_before\":";output_target(log_,row.held_before,row.held_before_active);
                log_<<",\"held_after\":";output_target(log_,row.held_after,row.held_after_active);log_<<",\"terminal_draw_proven\":false}\n";
                if(!log_){write_failed_=true;active_=false;break;}++written_;
                const auto position=log_.tellp();if(position<0){write_failed_=true;active_=false;break;}bytes_=static_cast<std::uint64_t>(position);
            }
            if(final && log_.is_open()){log_.flush();if(!log_)write_failed_=true;log_.close();if(log_.fail())write_failed_=true;}
            const auto now=GetTickCount64();
            if(!summary_.empty() && (final || now-last_summary_tick_>=1000)) {last_summary_tick_=now;std::ofstream out(summary_,std::ios::trunc);
                if(!out.is_open() || !out){write_failed_=true;active_=false;return;}
                out<<std::boolalpha<<"{\"activation\":"<<activation_<<",\"enabled\":"<<enabled()<<",\"calls\":"<<calls_.load()
                <<",\"attempts\":"<<attempts_.load()<<",\"written\":"<<written_.load()<<",\"missing_owner\":"<<missing_owner_.load()
                <<",\"other_binding\":"<<other_binding_.load()<<",\"sampling_race\":"<<busy_.load()<<",\"depth_overflow\":"<<overflow_.load()
                <<",\"cap_skipped\":"<<cap_.load()<<",\"queue_dropped\":"<<dropped_.load()<<",\"write_failed\":"<<write_failed_.load()
                <<",\"drain_failed\":"<<drain_failed_.load()
                <<",\"limit_reached\":"<<(limit_ || cap_.load()!=0)<<",\"bytes\":"<<bytes_<<",\"byte_limit\":67108864"
                <<",\"callback_calls\":"<<callback_calls_.load()<<",\"callback_qpc_ticks\":"<<callback_ticks_.load()<<",\"callback_max_qpc_ticks\":"<<callback_max_ticks_.load()
                <<",\"qpc_frequency\":"<<frequency_<<",\"ring_capacity\":256,\"sample_cap\":32768,\"minimum_interval_ms\":100,\"terminal_draw_proven\":false}\n";
                if(!out)write_failed_=true;out.flush();if(!out)write_failed_=true;out.close();if(out.fail())write_failed_=true;
                if(write_failed_.load())active_=false;
            }
        }catch(...){write_failed_=true;active_=false;}
    }
};
inline thread_local std::array<LeftWeaponRenderReceipt::Pending,8> LeftWeaponRenderReceipt::pending_;
}
