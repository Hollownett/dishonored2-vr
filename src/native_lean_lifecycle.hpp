#pragma once
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include "native_lean_query_trace.hpp"
#include "native_head_query_lifetime.hpp"

namespace d2vr {
// Default-off passive observer. All engine calls here are original hook call-throughs.
class NativeLeanLifecycle {
public:
    using Read=bool(*)(std::uintptr_t,void*,std::size_t);
    inline static constexpr std::array<std::uintptr_t,9> rvas{0x94970,0x96610,0x958c0,0x90ac0,0x91a50,0x97670,0x96240,0x8ab0f0,0x8a4830};
    inline static constexpr std::array<const char*,9> signatures{
        "48895c24084889742410574883ec20482bd1488db10440000048b86766666666",
        "40534883ec200fb6828c000000488bda83f8050f878d000000488d15d099f6ff",
        "48895c2408574883ec200fb6828c000000488bda488bf983f8057759488d0d1d",
        "48895c2408574883ec20818188000000002000008bfa488bd9e8b2d3a000448b",
        "40534883ec208b02488bda488d9180820000448bc04181e0ff1f00004f8d0480",
        "405355565741574883ec50488b8190831400488bf14c8d0dd4c8ffff488b9890",
        "488bc455488da8a8feffff4881ec5002000048c7442440feffffff4889581848",
        "40534883ec3048c7442420feffffff488bd941b001488bd1488d4c2440e8ae7c",
        "48894c240855535657415441564157488bec4883ec4048c745f0feffffff488b"};
    struct Ticket {unsigned index{};std::uint64_t serial{};std::uint32_t token{},flags{};std::uintptr_t caller{};explicit operator bool()const noexcept{return serial!=0;}};
private:
    friend struct NativeLeanLifecycleFixture;
    using Pair=void(__fastcall*)(std::uintptr_t,std::uintptr_t);
    using Destroy=std::uintptr_t(__fastcall*)(std::uintptr_t,std::uint64_t);
    using Shutdown=void(__fastcall*)(std::uintptr_t);
    struct Record {NativeLeanQuerySnapshot state{};std::uintptr_t slot{},id{};std::uint32_t token{};std::uint64_t serial{};bool active{},started{},completed{},cancelled{},proof{},retired{};};
    struct Boundary { Record state{};Ticket ticket{};bool admitted{};unsigned kind{};bool historical_owner{}; };
    struct ShutdownSeed {NativeLeanLifecycle* self;const Boundary* boundary;};
    struct Seed {NativeLeanLifecycle* self;const NativeLeanQueryEvent* event;};
    struct Row {
        const char* name{};std::uint64_t sequence{},tick{},serial{};unsigned thread{},active{},pending{};
        std::uintptr_t caller{},player{},manager{},world{},shape{},slot{},id{},current_world{},game{},machine{},sensor{};
        int sensor_generation{},machine_generation{};std::uint32_t token{},flags{},after{};
        unsigned owner_scope{},guard_reason{},guard_readable{};std::uintptr_t guard_expected{},guard_observed{};
    };
    inline static NativeLeanLifecycle* instance_{};
    inline static thread_local const Seed* seed_{};
    inline static thread_local const Ticket* cleanup_{};
    inline static thread_local const ShutdownSeed* shutdown_scope_{};
    std::array<Record,8> records_{};
    std::array<Row,256> events_{};unsigned reserved_rows_{},boundary_rejection_rows_{};
    std::array<bool,9> enabled_{};
    Pair publish_{},worker_{},cleanup_original_{},cancel_{};
    Destroy destroy_{};Shutdown shutdown_{},join_{},game_shutdown_{},destructor_{};
    Read read_{};std::uintptr_t base_{};
    std::mutex mutex_;std::ofstream file_;
    std::atomic<bool> active_{};std::atomic<unsigned> depth_{};
    std::atomic<std::uint64_t> rows_{},dropped_{},rejected_{},overflow_{};
    std::atomic<bool> failed_{};std::uint64_t serial_{},last_join_tick_{};NativeLeanQuerySnapshot last_join_state_{};bool have_join_state_{};
    struct Depth {NativeLeanLifecycle& self;Depth(NativeLeanLifecycle& s):self(s){++self.depth_;}~Depth(){--self.depth_;}};
    Record* record(Ticket t) noexcept {return t && t.index<records_.size() && records_[t.index].serial==t.serial?&records_[t.index]:nullptr;}
    void event(const char* name,const Record& r,Ticket t={},std::uint32_t after=0,std::uintptr_t current_world=0,unsigned limit=192,unsigned owner_scope=0,unsigned reason=0,std::uintptr_t expected=0,std::uintptr_t observed=0,bool readable=true) noexcept {
        // Caller holds mutex_. Capture immutable fields only; no callback file I/O.
        const auto index=rows_.load();if(index+reserved_rows_>=limit || index>=events_.size()){++dropped_;return;}
        unsigned pending=0,running=0;for(const auto& v:records_)if(v.serial && !v.retired){++pending;if(v.active)++running;}
        events_[static_cast<std::size_t>(index)]={name,index,GetTickCount64(),r.serial,GetCurrentThreadId(),running,pending,
            t.caller,r.state.player,r.state.manager,r.state.world,r.state.shape,r.slot,r.id,current_world,r.state.game,r.state.machine,r.state.sensor,
            r.state.sensor_identity.generation,r.state.machine_identity.generation,r.token,t.flags,after,owner_scope,reason,readable?1U:0U,expected,observed};
        ++rows_;
    }
    std::optional<std::uint32_t> slot(Ticket t,std::uintptr_t manager,std::uintptr_t at,bool require_shape=true) {
        const auto* r=record(t);if(!r || r->retired || r->state.manager!=manager || r->slot!=at)return {};
        const auto token=rig::value<std::uint32_t>(read_,at+0x88);
        const auto kind=rig::value<unsigned char>(read_,at+0x8c),variant=rig::value<unsigned char>(read_,at+0x8d);
        const auto flags=rig::value<unsigned char>(read_,at+0x8e);
        if(!token || *token!=r->token || !kind || *kind!=4 || !variant || *variant!=1 || !flags ||
           (require_shape && (rig::value<std::uintptr_t>(read_,at+0xb8)!=r->state.shape || rig::value<std::uintptr_t>(read_,manager+0x190)!=r->state.world)))return {};
        return *flags;
    }
    Ticket find(std::uintptr_t manager,std::uintptr_t at,std::uintptr_t caller) {
        for(unsigned i=0;i<records_.size();++i) {const auto& r=records_[i];if(r.serial && !r.retired && r.state.manager==manager && r.slot==at)return {i,r.serial,r.token,0,caller};}return {};
    }
    static void __fastcall publish_hook(std::uintptr_t pool,std::uintptr_t at) {
        auto& s=*instance_;Depth depth(s);const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        passive_lean_call([&]{if(seed_ && seed_->self==&s)s.before_publish(*seed_->event,pool,at,caller);},[&]{s.publish_(pool,at);},[]{});
    }
    static void __fastcall worker_hook(std::uintptr_t manager,std::uintptr_t at) {
        auto& s=*instance_;Depth depth(s);Ticket t{};const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        passive_lean_call([&]{t=s.before_worker(manager,at,caller);},[&]{s.worker_(manager,at);},[&]{s.after_worker(t);});
    }
    static void __fastcall cleanup_hook(std::uintptr_t manager,std::uintptr_t at) {
        auto& s=*instance_;Depth depth(s);Ticket t{};const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        struct Scope {const Ticket* previous;Scope(const Ticket* next):previous(cleanup_){cleanup_=next;}~Scope(){cleanup_=previous;}};
        passive_lean_call([&]{t=s.before_cleanup(manager,at,caller);},[&]{Scope scope(&t);s.cleanup_original_(manager,at);},[]{});
    }
    static std::uintptr_t __fastcall destroy_hook(std::uintptr_t at,std::uint64_t flags) {
        auto& s=*instance_;Depth depth(s);Ticket t{};const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        return passive_lean_call([&]{if(cleanup_ && flags==0 && caller==s.base_+0x9592b)t=*cleanup_;},[&]{return s.destroy_(at,flags);},[&](std::uintptr_t result){s.after_destroy(t,at,result);});
    }
    static void __fastcall cancel_hook(std::uintptr_t manager,std::uintptr_t id) {
        auto& s=*instance_;Depth depth(s);Ticket t{};const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        passive_lean_call([&]{t=s.before_cancel(manager,id,caller);},[&]{s.cancel_(manager,id);},[&]{s.after_cancel(t,id);});
    }
    static void __fastcall shutdown_hook(std::uintptr_t manager) {
        auto& s=*instance_;Depth depth(s);Boundary t{};const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        try {passive_lean_call([&]{t=s.before_boundary(manager,caller,2);},[&]{s.shutdown_(manager);},[&]{s.after_boundary(t);});}
        catch(...) {s.after_boundary(t,true);throw;}
    }
    static void __fastcall join_hook(std::uintptr_t manager) {
        auto& s=*instance_;Depth depth(s);Boundary t{};const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        try {passive_lean_call([&]{t=s.before_boundary(manager,caller,0);},[&]{s.join_(manager);},[&]{s.after_boundary(t);});}
        catch(...) {s.after_boundary(t,true);throw;}
    }
    static void __fastcall game_shutdown_hook(std::uintptr_t game) {
        auto& s=*instance_;Depth depth(s);Boundary t{};const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        try {passive_lean_call([&]{t=s.before_boundary(game,caller,1);},[&]{GameShutdownScope scope(s,t);s.game_shutdown_(game);},[&]{s.after_boundary(t);});}
        catch(...) {s.after_boundary(t,true);throw;}
    }

    static void __fastcall destructor_hook(std::uintptr_t game) {
        auto& s=*instance_;Depth depth(s);Boundary t{};const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        try {passive_lean_call([&]{t=s.before_boundary(game,caller,3);},[&]{GameShutdownScope scope(s,t);s.destructor_(game);},[&]{s.after_boundary(t);});}
        catch(...) {s.after_boundary(t,true);throw;}
    }

public:
    class GameShutdownScope {
        ShutdownSeed seed;const ShutdownSeed* previous;
    public:
        GameShutdownScope(NativeLeanLifecycle& s,const Boundary& b):seed{&s,&b},previous(shutdown_scope_){shutdown_scope_=&seed;}
        ~GameShutdownScope(){shutdown_scope_=previous;}
        GameShutdownScope(const GameShutdownScope&)=delete;GameShutdownScope& operator=(const GameShutdownScope&)=delete;
    };
    template<class Invoke> auto queue_call(const NativeLeanQueryEvent* event,Invoke&& invoke)->decltype(invoke()) {
        Seed seed{this,event};const auto* previous=seed_;seed_=event && active_?&seed:nullptr;
        __try {return invoke();}
        __finally {seed_=previous;}
    }
    // Public observation methods also permit isolated synthetic ABI tests.
    bool before_publish(const NativeLeanQueryEvent& e,std::uintptr_t pool,std::uintptr_t at,std::uintptr_t caller) {
        if(!active_ || caller!=base_+0x95600 || pool!=e.state.manager+0x200)return false;
        std::lock_guard<std::mutex> lock(mutex_);
        const auto current=native_lean_query_snapshot(read_,base_,e.state.sensor);
        const auto token=rig::value<std::uint32_t>(read_,at+0x88);
        std::array<std::byte,112> request{};std::array<std::byte,48> axes{};
        if(!current || !same_lean_resources(e.state,*current) || !token || at!=head_query_slot(e.state.manager,*token) ||
           rig::value<unsigned char>(read_,at+0x8c)!=4 || rig::value<unsigned char>(read_,at+0x8d)!=1 ||
           rig::value<unsigned char>(read_,at+0x8e).value_or(255)!=0 || rig::value<std::uint32_t>(read_,at+0x80)!=e.mask ||
           rig::value<std::uint32_t>(read_,at+0x84)!=e.flags || !read_(at+0x90,request.data(),112) || request!=e.request ||
           !read_(at+0x100,axes.data(),48) || axes!=e.axes) {++rejected_;return false;}
        if(find(e.state.manager,at,caller)){++rejected_;return false;}
        for(unsigned i=0;i<records_.size();++i)if(!records_[i].serial || records_[i].retired) {
            records_[i]={e.state,at,e.id_pointer,*token,++serial_};event("issued",records_[i],{i,serial_,*token,0,caller});return true;
        }
        ++overflow_;return false;
    }
    Ticket before_worker(std::uintptr_t manager,std::uintptr_t at,std::uintptr_t caller) {
        std::lock_guard<std::mutex> lock(mutex_);auto t=find(manager,at,caller);if(!t)return {};
        const auto flags=slot(t,manager,at);auto* r=record(t);
        if(caller!=base_+0x999ad || !flags || (*flags&1) || r->started){++rejected_;return {};}
        t.flags=*flags;r->started=r->active=true;event("worker_begin",*r,t);return t;
    }
    void after_worker(Ticket t) {
        std::lock_guard<std::mutex> lock(mutex_);auto* r=record(t);if(!r)return;
        // Normal 096610 return proves the cast returned. Never reread the slot here.
        r->active=false;r->completed=true;event("worker_end",*r,t,2);
        if(r->proof){r->retired=true;event("retired_deferred",*r,t);}
    }
    Ticket before_cleanup(std::uintptr_t manager,std::uintptr_t at,std::uintptr_t caller) {
        std::lock_guard<std::mutex> lock(mutex_);auto t=find(manager,at,caller);if(!t)return {};
        const auto flags=slot(t,manager,at);if(!flags){++rejected_;return {};}
        t.flags=*flags;event("cleanup_begin",*record(t),t);return t;
    }
    void after_destroy(Ticket t,std::uintptr_t at,std::uintptr_t result) {
        std::lock_guard<std::mutex> lock(mutex_);auto* r=record(t);if(!r)return;
        const auto generation=rig::value<std::uint32_t>(read_,at+0x88);
        if(at!=r->slot || result!=at || !generation || *generation!=std::uint32_t(r->token+0x2000U) ||
           rig::value<std::uint32_t>(read_,at+8)!=0U || rig::value<std::uintptr_t>(read_,at)!=std::uintptr_t{0}){++rejected_;return;}
        const bool completed=(t.flags&2)!=0 && r->started;
        const bool cancelled=t.caller==base_+0x999a6 && (t.flags&1) && !r->started;
        if(!completed && !cancelled){++rejected_;event("retirement_unproven",*r,t,*generation);return;}
        r->proof=true;if(cancelled)r->cancelled=true;if(!r->active)r->retired=true;
        event(r->retired?"retired":"retirement_deferred",*r,t,*generation);
    }
    Ticket before_cancel(std::uintptr_t manager,std::uintptr_t id,std::uintptr_t caller) {
        std::lock_guard<std::mutex> lock(mutex_);
        for(unsigned i=0;i<records_.size();++i){auto& r=records_[i];if(!r.serial || r.retired || r.state.manager!=manager)continue;
            // Lean may copy the issued token into sensor fields, so bind by full token rather than original stack ID pointer.
            const auto token=rig::value<std::uint32_t>(read_,id);if(!token || *token!=r.token)continue;
            Ticket t{i,r.serial,r.token,0,caller};const auto flags=slot(t,manager,r.slot);if(!flags){++rejected_;return {};}
            t.flags=*flags;r.cancelled=true;event("cancel_begin",r,t);return t;
        }return {};
    }
    void after_cancel(Ticket t,std::uintptr_t id) {
        std::lock_guard<std::mutex> lock(mutex_);auto* r=record(t);if(!r)return;
        const auto token=rig::value<std::uint32_t>(read_,id);if(!token || *token!=0xffffffffU){++rejected_;return;}event("cancel_return",*r,t,*token);
    }
    // Join/standalone/destructor entry verifies live actor ownership; GameLocal shutdown verifies historical world scope.
    // Return consumes only this owned copy, never native memory.
    // Registered counts are a partial observer set, not a global scheduler fence.
    Boundary before_boundary(std::uintptr_t object,std::uintptr_t caller,unsigned kind,std::uint64_t tick=GetTickCount64()) {
        if(!active_ || kind>3)return {};
        std::lock_guard<std::mutex> lock(mutex_);
        if(kind==2 && shutdown_scope_ && shutdown_scope_->self==this) {
            const auto& parent=*shutdown_scope_->boundary;
            // 8533e0 tail-jumps to097670: caller is the scoped shutdown/destructor virtual CALL return.
            // Earlier owner cleanup may have freed player/FSM/sensor. Read only manager fields.
            if(!parent.admitted || (parent.kind!=1 && parent.kind!=3) || object!=parent.state.state.manager ||
               caller!=base_+(parent.kind==3?0x8a4bb2:0x8ab13f) ||
               rig::value<std::uintptr_t>(read_,object)!=base_+0x1c80090 ||
               rig::value<std::uintptr_t>(read_,object+0x190)!=parent.state.state.world) {++rejected_;return {};}
            if(rows_+reserved_rows_+2>256){dropped_+=2;return {};}
            ++reserved_rows_;
            Boundary t{parent.state,parent.ticket,true,2,true};t.ticket.caller=caller;
            event("world_shutdown_begin",t.state,t.ticket,0,t.state.state.world,256,t.historical_owner?1U:0U);
            return t;
        }
        const Record* latest=nullptr;for(const auto& r:records_)if(r.serial &&
            ((kind==1 || kind==3)?r.state.game==object:r.state.manager==object) && (!latest || r.serial>latest->serial))latest=&r;
        if(!latest)return {};
        const auto& state=latest->state;
        // Each guard reads once. Rejection diagnostics contain that captured value only.
        auto equal=[&](std::uintptr_t address,std::uintptr_t expected,unsigned reason) {
            const auto observed=rig::value<std::uintptr_t>(read_,address);
            if(observed && *observed==expected)return true;
            ++rejected_;
            if(boundary_rejection_rows_<8) {++boundary_rejection_rows_;
                event("boundary_rejected",*latest,{0,latest->serial,latest->token,0,caller},kind,0,256,
                    kind==1?1U:0U,reason,expected,observed.value_or(0),observed.has_value());}
            return false;
        };
        if(kind!=1 && kind!=3 && !equal(base_+0x39a5290,state.game,4))return {};
        if(!equal(state.game,base_+0x1c940a8,1) ||
           !equal(state.manager,base_+0x1c80090,2) ||
           !equal(state.manager+0x190,state.world,3))return {};
        // GameLocal shutdown establishes a typed WORLD scope only. The actor can
        // already be retired and global current-game can point to its replacement.
        // Historical owner/token fields identify the registered query, not a live actor.
        if(kind!=1) {
            if(!equal(state.game+0x3130b0,state.player,5) ||
               !equal(state.player,base_+0x1c5e428,6) ||
               !equal(state.machine,base_+0x1c73d58,7) ||
               !equal(state.machine+0x10,state.player,8) ||
               !equal(state.sensor,base_+0x1b40648,9) ||
               !equal(state.sensor+0x10,state.player,10) ||
               !equal(state.sensor+0x20,state.machine,11) ||
               !equal(state.sensor+8,state.declaration,12) ||
               !equal(state.sensor+0x40,state.shape,13))return {};
            std::uintptr_t sensor_identity{},machine_identity{};
            static_assert(sizeof(rig::Identity)==sizeof(std::uintptr_t),"Pinned identity ABI");
            std::memcpy(&sensor_identity,&state.sensor_identity,sizeof(sensor_identity));
            std::memcpy(&machine_identity,&state.machine_identity,sizeof(machine_identity));
            if(!equal(state.sensor+0x18,sensor_identity,14) || !equal(state.machine+0x18,machine_identity,15))return {};
        }
        if(kind==0 && have_join_state_ && same_lean_resources(last_join_state_,state) && tick>=last_join_tick_ && tick-last_join_tick_<500)return {};
        const unsigned limit=kind==0?224:256;
        // Two rows reserved together. Existing in-flight returns consume their reserved capacity.
        if(rows_+reserved_rows_+2>limit){dropped_+=2;return {};}
        ++reserved_rows_;
        if(kind==0){last_join_state_=state;have_join_state_=true;last_join_tick_=tick;}
        Boundary t{*latest,{0,latest->serial,latest->token,0,caller},true,kind,kind==1};
        event(kind==0?"join_begin":kind==1?"game_shutdown_begin":kind==3?"destructor_begin":"world_shutdown_begin",t.state,t.ticket,0,state.world,limit,t.historical_owner?1U:0U);
        return t;
    }
    void after_boundary(const Boundary& t,bool unwind=false) {
        if(!t.admitted)return;
        std::lock_guard<std::mutex> lock(mutex_);--reserved_rows_;
        event(unwind?(t.kind==0?"join_unwind":t.kind==1?"game_shutdown_unwind":t.kind==3?"destructor_unwind":"world_shutdown_unwind"):
            (t.kind==0?"join_return":t.kind==1?"game_shutdown_return":t.kind==3?"destructor_return":"world_shutdown_return"),t.state,t.ticket,0,0,256,(t.kind==3 || t.historical_owner)?1U:0U);
    }
    void install(std::uintptr_t base,Read read,const std::filesystem::path& path) {
        base_=base;read_=read;records_={};events_={};serial_=last_join_tick_=0;last_join_state_={};have_join_state_=false;reserved_rows_=boundary_rejection_rows_=0;rows_=dropped_=rejected_=overflow_=0;failed_=false;
        for(unsigned i=0;i<rvas.size();++i){std::array<unsigned char,32> bytes{};if(!read(base+rvas[i],bytes.data(),32))throw std::runtime_error("Cannot read native Lean lifetime code");
            for(unsigned j=0;j<32;++j){const auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};if(bytes[j]!=nibble(signatures[i][j*2])*16+nibble(signatures[i][j*2+1]))throw std::runtime_error("Native Lean lifetime code differs");}}
        file_.open(path,std::ios::trunc);if(!file_)throw std::runtime_error("Cannot open native Lean lifecycle trace");
        file_<<"sequence,tick_ms,thread,event,caller,serial,player,sensor_generation,machine_generation,manager,world,shape,slot,id_pointer,token,before_flags,after_value,active,pending,current_world,game,machine,sensor,owner_scope,guard_reason,guard_readable,guard_expected,guard_observed\n";
        instance_=this;
        std::array<void*,9> hooks{reinterpret_cast<void*>(&publish_hook),reinterpret_cast<void*>(&worker_hook),reinterpret_cast<void*>(&cleanup_hook),reinterpret_cast<void*>(&destroy_hook),reinterpret_cast<void*>(&cancel_hook),reinterpret_cast<void*>(&shutdown_hook),reinterpret_cast<void*>(&join_hook),reinterpret_cast<void*>(&game_shutdown_hook),reinterpret_cast<void*>(&destructor_hook)};
        std::array<void**,9> originals{reinterpret_cast<void**>(&publish_),reinterpret_cast<void**>(&worker_),reinterpret_cast<void**>(&cleanup_original_),reinterpret_cast<void**>(&destroy_),reinterpret_cast<void**>(&cancel_),reinterpret_cast<void**>(&shutdown_),reinterpret_cast<void**>(&join_),reinterpret_cast<void**>(&game_shutdown_),reinterpret_cast<void**>(&destructor_)};
        for(unsigned i=0;i<9;++i){auto* target=reinterpret_cast<void*>(base+rvas[i]);if(!*originals[i] && MH_CreateHook(target,hooks[i],originals[i])!=MH_OK)throw std::runtime_error("Cannot create native Lean lifecycle hook");
            if(MH_EnableHook(target)!=MH_OK)throw std::runtime_error("Cannot enable native Lean lifecycle hook");enabled_[i]=true;}
        active_=true;
    }
    bool disable() noexcept {
        active_=false;bool okay=true;for(unsigned i=0;i<9;++i)if(enabled_[i]){if(MH_DisableHook(reinterpret_cast<void*>(base_+rvas[i]))==MH_OK)enabled_[i]=false;else okay=false;}
        const auto deadline=GetTickCount64()+5000;while(depth_ && GetTickCount64()<deadline)Sleep(1);if(depth_)return false;
        const std::array<bool,9> created{publish_!=nullptr,worker_!=nullptr,cleanup_original_!=nullptr,destroy_!=nullptr,cancel_!=nullptr,shutdown_!=nullptr,join_!=nullptr,game_shutdown_!=nullptr,destructor_!=nullptr};
        for(unsigned i=0;i<9;++i)if(created[i]){std::array<unsigned char,32> bytes{};if(!read_(base_+rvas[i],bytes.data(),32)){okay=false;continue;}
            for(unsigned j=0;j<32;++j){const auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};if(bytes[j]!=nibble(signatures[i][j*2])*16+nibble(signatures[i][j*2+1]))okay=false;}}
        try {std::lock_guard<std::mutex> lock(mutex_);if(file_.is_open()){
            // Hooks are disabled and depth is zero. This is the only event serialization site.
            for(std::size_t i=0;i<rows_.load();++i){const auto& r=events_[i];
                file_<<r.sequence<<','<<r.tick<<','<<r.thread<<','<<r.name<<','<<r.caller<<','<<r.serial<<','<<r.player<<','
                    <<r.sensor_generation<<','<<r.machine_generation<<','<<r.manager<<','<<r.world<<','<<r.shape<<','<<r.slot<<','
                    <<r.id<<','<<r.token<<','<<r.flags<<','<<r.after<<','<<r.active<<','<<r.pending<<','<<r.current_world<<','<<r.game<<','<<r.machine<<','<<r.sensor<<','<<r.owner_scope<<','<<r.guard_reason<<','<<r.guard_readable<<','<<r.guard_expected<<','<<r.guard_observed<<'\n';}
            file_.flush();if(!file_)failed_=true;file_.close();if(file_.fail())failed_=true;}}catch(...){failed_=true;}
        return okay && !failed_;
    }
    std::uint64_t rows()const noexcept{return rows_;}std::uint64_t dropped()const noexcept{return dropped_;}
    std::uint64_t rejected()const noexcept{return rejected_;}std::uint64_t overflow()const noexcept{return overflow_;}
    bool failed()const noexcept{return failed_;}
};
} // namespace d2vr

