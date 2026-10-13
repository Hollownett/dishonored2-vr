#pragma once
// "VR Settings" as the game's OWN options page (docs/native-vr-options-page.md, docs/native-vr-options-deferred.md): the game
// draws its option rows, steppers, sliders, brush highlight, fonts and description plate. The menu click sends
// VR_SETTINGS_EVT; the hub hook only queues it on the engine's deferred external-call queue (every Iggy Get/Set/name call
// takes a lock that AS3 execution holds, so nothing can be done inside the call). The engine drains that queue right after
// the player's tick, outside AS3, exactly where its own OnOptionSectionSelected writes optionsData.currentSection and calls
// root._C_ShowOptionSection(); the deferred handler does the same with the VR rows. Rows report changes with
// VR_SETTING_CHANGED (values read back by path), actions with VR_SETTING_ACTION_<n>; B and X on the page send
// OnBackFromSettingsList/OnRestoreSettings(100) (lib_options navigates back itself for sections >= 100).
// Drain thread: open/changed/restore/show. Render thread: publish rows, take values and actions.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace d2vr {
// Iggy API functions inside the game exe (Steam 17576867), pinned by their first 12 bytes; calling conventions from the
// engine's own call sites (artifacts/claude/attackgate/out/iggyapi.txt). Names and refs are pointer-sized; Set*RS return
// non-zero on success, GetS32RS returns 0 on success. A value path record is 0x28 bytes and points at its parent.
class IggyApi {
public:
    struct Pin {std::uintptr_t rva;std::array<unsigned char,12> code;};
    inline static constexpr Pin fast_name{0x1009960,{0x48,0x89,0x5c,0x24,0x20,0x57,0x48,0x81,0xec,0x50,0x08,0x00}},
        root_path{0x1009a90,{0x48,0x83,0xb9,0x40,0x29,0x00,0x00,0x00,0x74,0x08,0x48,0x8d}},
        make_name{0x1009de0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x49,0x8b}},
        make_index{0x1009da0,{0x48,0x85,0xd2,0x75,0x13,0x4c,0x8d,0x05,0x1c,0x64,0xf1,0x00}},
        set_s32{0x100a3e0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89}},
        set_bool{0x100a310,{0x40,0x53,0x48,0x83,0xec,0x30,0x48,0x8d,0x05,0x24,0x9d,0x41}},
        set_string{0x100a460,{0x4c,0x89,0x4c,0x24,0x20,0x4c,0x89,0x44,0x24,0x18,0x48,0x89}},
        set_ref{0x100a5c0,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89}},
        create_array{0x1009f00,{0x48,0x89,0x5c,0x24,0x18,0x89,0x54,0x24,0x10,0x48,0x89,0x4c}},
        get_s32{0x1009bf0,{0x40,0x53,0x48,0x83,0xec,0x40,0x48,0x8d,0x44,0x24,0x30,0x49}},
        ref_free{0x100a170,{0x4c,0x8b,0xc9,0x48,0x85,0xd2,0x0f,0x84,0x88,0x00,0x00,0x00}},
        call_method{0x100c4c0,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89}},
        register_call{0x2b7e0,{0x4c,0x89,0x44,0x24,0x18,0x48,0x89,0x4c,0x24,0x08,0x55,0x56}};
    using FastName=std::uintptr_t (__fastcall*)(void* iggy,const char* name,int length);
    using RootPath=void* (__fastcall*)(void* iggy);
    using MakeName=int (__fastcall*)(void* out,void* parent,const char* name);
    using MakeIndex=void (__fastcall*)(void* out,void* parent,int index);
    using SetS32=int (__fastcall*)(void* path,std::uintptr_t name,const char* sub,int value);
    using SetBool=int (__fastcall*)(void* path,std::uintptr_t name,const char* sub,bool value);
    using SetString=int (__fastcall*)(void* path,std::uintptr_t name,const char* sub,const char* value,int length);
    using SetRef=int (__fastcall*)(void* path,std::uintptr_t name,const char* sub,void* ref);
    using CreateArray=void* (__fastcall*)(void* iggy,int length);
    using GetS32=int (__fastcall*)(void* path,std::uintptr_t name,const char* sub,int* out);
    using RefFree=void (__fastcall*)(void* iggy,void* ref);
    struct DataValue {std::int32_t type,pad;void* temp_ref;double number;std::uint64_t extra;};   // IggyDataValue, 0x20 bytes
    using CallMethod=int (__fastcall*)(void* iggy,DataValue* result,void* path,std::uintptr_t method,int nargs,DataValue* args);
    // arkIggyDeferredExternalInterfaceHandler::RegisterCall: copies the target and the call into the file's queue.
    using DeferredFn=void (__fastcall*)(void* owner,void* file,void* args);
    struct CallTarget {void* file;void* owner;DeferredFn fn;};
    using RegisterCall=void (__fastcall*)(void* file,const CallTarget* target,const void* call);
    static bool verify(std::uintptr_t base) noexcept {
        if(!base) return false;
        for(const auto* pin:{&fast_name,&root_path,&make_name,&make_index,&set_s32,&set_bool,&set_string,&set_ref,&create_array,&get_s32,&ref_free,&call_method,&register_call}) {
            std::array<unsigned char,12> live{};SIZE_T read{};
            if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+pin->rva),live.data(),live.size(),&read) || read!=live.size() || live!=pin->code) return false;
        }
        return true;
    }
    template<class F> static F fn(std::uintptr_t base,const Pin& pin) noexcept {return reinterpret_cast<F>(base+pin.rva);}
};

class NativeOptionsPage {
public:
    enum class Kind {slider,stepper,toggle,action};
    struct Row {Kind kind{};std::string label,description;int minimum{},maximum{},interval{1},value{},fallback{};std::vector<std::string> values;};
    inline static constexpr int instance_index=100; // engine sections are 0..3; lib_options only navigates back itself for >= 100
    // lib_options OptionsList pools rowViewCount=10 items (OptionsListContainer._setPoolContainer) and never scrolls further:
    // an 11th row cannot be selected (sim v392). Tabs (categoryIndex) render, but LB/RB (grips) reset the list instead of
    // switching tabs in sim v393 - keep the page to one tab of at most 10 rows.
    inline static constexpr std::size_t max_rows=10;
    // Render thread: the page as it should open next (labels, choices, current values).
    static void publish(std::vector<Row> rows) {std::lock_guard<std::mutex> l(m_);rows_=std::move(rows);}
    // Hub thread, inside VR_SETTINGS_EVT (the patched click then shows the section): false leaves the game's data alone.
    static bool open(std::uintptr_t base,void* iggy) noexcept {
        std::vector<Row> rows;{std::lock_guard<std::mutex> l(m_);rows=rows_;}
        if(rows.empty()) {failed_step=1;return false;}
        if(!verified(base)) {failed_step=2;return false;}
        if(!iggy) {failed_step=3;return false;}
        failed_step=0;
        const bool ok=guarded([&] {return write_page(base,iggy,rows,false);});
        if(ok) {std::lock_guard<std::mutex> l(m_);open_rows_=std::move(rows);showing=true;++opens;}
        return ok;
    }
    // Hub thread: a widget changed (its definition already holds the new value): read every row back.
    static bool changed(std::uintptr_t base,void* iggy) noexcept {
        std::vector<Row> rows;{std::lock_guard<std::mutex> l(m_);rows=open_rows_;}
        if(rows.empty() || !showing || !verified(base) || !iggy) return false;
        std::vector<int> values(rows.size(),-1);
        const bool ok=guarded([&] {return read_values(base,iggy,rows,values);});
        if(ok) {
            std::lock_guard<std::mutex> l(m_);
            for(std::size_t i=0;i<values.size() && i<open_rows_.size();++i) if(open_rows_[i].kind==Kind::action) {
                if(values[i]==1 && open_rows_[i].value!=1) {actions_.push_back(static_cast<int>(i));++action_flips;} // each flip to "Starting..." runs once
                open_rows_[i].value=values[i];values[i]=-1;
            }
            pending_=values;++changes;
        }
        return ok;
    }
    // Hub thread, OnRestoreSettings(100): every row back to its default on the page and in the mod.
    static bool restore(std::uintptr_t base,void* iggy) noexcept {
        std::vector<Row> rows;{std::lock_guard<std::mutex> l(m_);rows=open_rows_;}
        if(rows.empty() || !showing || !verified(base) || !iggy) return false;
        for(auto& r:rows) r.value=r.fallback;
        const bool ok=guarded([&] {return write_page(base,iggy,rows,false);});
        if(ok) {std::vector<int> values;for(const auto& r:rows) values.push_back(r.kind==Kind::action?-1:r.value);
            std::lock_guard<std::mutex> l(m_);open_rows_=rows;pending_=values;++changes;}
        return ok;
    }
    // Hub-call arguments: count at call+0x10, the 0x20-byte IggyDataValues inline from call+0x18; the first number's F64 at +0x28.
    static std::optional<double> number_argument(void* call) noexcept {
        __try {
            const auto* c=static_cast<const unsigned char*>(call);
            if(*reinterpret_cast<const std::int32_t*>(c+0x10)<1) return std::nullopt;
            return *reinterpret_cast<const double*>(c+0x28);
        } __except(EXCEPTION_EXECUTE_HANDLER) {return std::nullopt;}
    }
    // Drain thread: run root._C_ShowOptionSection() as the engine does (0x503920: root path, no arguments, zeroed buffer).
    static bool show(std::uintptr_t base,void* iggy) noexcept {
        if(!verified(base) || !iggy) return false;
        int code=-1;
        guarded([&] {
            using A=IggyApi;void* root=A::fn<A::RootPath>(base,A::root_path)(iggy);
            const std::uintptr_t method=root?A::fn<A::FastName>(base,A::fast_name)(iggy,"_C_ShowOptionSection",-1):0;
            A::DataValue result{},none{};
            code=method?A::fn<A::CallMethod>(base,A::call_method)(iggy,&result,root,method,0,&none):-2;
            return code==0;});
        last_show=code;return code==0;
    }
    static void action(int row) {std::lock_guard<std::mutex> l(m_);actions_.push_back(row);}
    static void closed() noexcept {showing=false;}
    // Render thread: latest row values (index = row; -1 for actions) and requested actions since the last call.
    static std::optional<std::vector<int>> take_values() {std::lock_guard<std::mutex> l(m_);auto v=std::move(pending_);pending_.reset();return v;}
    static std::vector<int> take_actions() {std::lock_guard<std::mutex> l(m_);std::vector<int> a;a.swap(actions_);return a;}
    inline static std::atomic<bool> showing{},enabled{true};
    inline static std::atomic<std::uint64_t> opens{},changes{},faults{},action_flips{};
    inline static std::atomic<int> failed_step{},last_show{-1}; // first failing step of the last open; last show result (diagnostics)
private:
    inline static std::mutex m_;inline static std::vector<Row> rows_,open_rows_;inline static std::optional<std::vector<int>> pending_;
    inline static std::vector<int> actions_;
    static bool verified(std::uintptr_t base) noexcept {static const bool ok=IggyApi::verify(base);return ok && enabled.load();}
    template<class F> static bool guarded(F&& f) noexcept {return seh(&f,[](void* p) {return (*static_cast<F*>(p))();});}
    static bool seh(void* p,bool (*call)(void*)) noexcept {__try {return call(p);} __except(EXCEPTION_EXECUTE_HANDLER) {++faults;return false;}}

    struct Path {alignas(8) unsigned char bytes[0x28];};
    // Path records chain to their parents: a fixed pool keeps every record alive for the whole write.
    struct Paths {
        std::array<Path,512> pool{};std::size_t used{};IggyApi::MakeName make_name;IggyApi::MakeIndex make_index;
        void* name(void* parent,const char* n) {if(!parent || used==pool.size()) return nullptr;auto* p=&pool[used++];return make_name(p,parent,n)?p:nullptr;}
        void* index(void* parent,int i) {if(!parent || used==pool.size()) return nullptr;auto* p=&pool[used++];make_index(p,parent,i);return p;}
    };
    static bool write_page(std::uintptr_t base,void* iggy,const std::vector<Row>& rows,bool values_only) {
        using A=IggyApi;
        const auto fast=A::fn<A::FastName>(base,A::fast_name);const auto setn=A::fn<A::SetS32>(base,A::set_s32);
        const auto setb=A::fn<A::SetBool>(base,A::set_bool);const auto sets=A::fn<A::SetString>(base,A::set_string);
        const auto setr=A::fn<A::SetRef>(base,A::set_ref);const auto array=A::fn<A::CreateArray>(base,A::create_array);
        const auto free_ref=A::fn<A::RefFree>(base,A::ref_free);
        auto paths=std::make_unique<Paths>();paths->make_name=A::fn<A::MakeName>(base,A::make_name);paths->make_index=A::fn<A::MakeIndex>(base,A::make_index);
        const auto name=[&](const char* n) {return fast(iggy,n,-1);};
        const auto str=[&](void* p,const char* field,const std::string& v) {return p && sets(p,field?name(field):0,nullptr,v.c_str(),static_cast<int>(v.size()))!=0;};
        const auto num=[&](void* p,const char* field,int v) {return p && setn(p,name(field),nullptr,v)!=0;};
        const auto flag=[&](void* p,const char* field,bool v) {return p && setb(p,name(field),nullptr,v)!=0;};
        const auto list=[&](void* p,const char* field,const std::vector<std::string>& items) {
            if(!p) return false;
            void* ref=array(iggy,static_cast<int>(items.size()));if(!ref) return false;
            const bool set=setr(p,name(field),nullptr,ref)!=0;free_ref(iggy,ref);if(!set) return false;
            void* at=paths->name(p,field);
            for(int k=0;k<static_cast<int>(items.size());++k) if(!str(paths->index(at,k),nullptr,items[static_cast<std::size_t>(k)])) return false;
            return true;};
        void* section=paths->name(paths->name(A::fn<A::RootPath>(base,A::root_path)(iggy),"optionsData"),"currentSection");
        if(values_only) {
            void* data=paths->name(paths->name(section,"settings"),"data");
            for(int i=0;i<static_cast<int>(rows.size());++i) {
                const auto& r=rows[static_cast<std::size_t>(i)];if(r.kind==Kind::action) continue;
                void* w=paths->name(paths->index(data,i),r.kind==Kind::slider?"slider":r.kind==Kind::stepper?"optionStepper":"optionStepperBoolean");
                if(!(r.kind==Kind::slider?num(w,"value",r.value):num(w,"selectedIndex",r.value)) || !flag(w,"locked",true) || !flag(w,"locked",false)) return false;
            }
            return true;
        }
        const auto step=[&](int n,bool ok) {if(!ok && !failed_step) failed_step=n;return ok;};
        if(!step(10,section!=nullptr) || !step(11,str(section,"label","VR Settings")) || !step(12,num(section,"instanceIndex",instance_index)) ||
           !step(13,str(section,"screenClassName","")) || !step(14,list(section,"categories",{"VR"}))) return false;
        void* settings=paths->name(section,"settings");
        if(!step(15,num(settings,"length",static_cast<int>(rows.size())))) return false;
        void* data=paths->name(settings,"data");
        for(int i=0;i<static_cast<int>(rows.size());++i) {
            const auto& r=rows[static_cast<std::size_t>(i)];void* row=paths->index(data,i);
            // label first: the widget getter builds its definition from the current label
            if(!step(100+i*10,str(row,"label",r.label)) || !step(101+i*10,str(row,"description",r.description)) || !step(102+i*10,num(row,"categoryIndex",0))) return false;
            // Actions are two-value steppers ("Select" -> "Starting..."): the game's link widget only calls an AS3 method on root.
            void* w=paths->name(row,r.kind==Kind::slider?"slider":r.kind==Kind::toggle?"optionStepperBoolean":"optionStepper");
            bool ok=str(w,"updateGameCallback","VR_SETTING_CHANGED");
            if(r.kind==Kind::slider) ok=ok && num(w,"minimum",r.minimum) && num(w,"maximum",r.maximum) && num(w,"interval",r.interval) &&
                flag(w,"displayValue",true) && num(w,"value",r.value) && flag(w,"refreshEnabled",true);
            else if(r.kind==Kind::stepper || r.kind==Kind::action) ok=ok && list(w,"values",r.values) && num(w,"selectedIndex",r.value) && flag(w,"refreshEnabled",true);
            else if(r.kind==Kind::toggle) ok=ok && num(w,"selectedIndex",r.value?1:0) && flag(w,"refreshEnabled",true);
            if(!step(103+i*10,ok)) return false;
        }
        return true;
    }
    static bool read_values(std::uintptr_t base,void* iggy,const std::vector<Row>& rows,std::vector<int>& values) {
        using A=IggyApi;
        const auto fast=A::fn<A::FastName>(base,A::fast_name);const auto get=A::fn<A::GetS32>(base,A::get_s32);
        auto paths=std::make_unique<Paths>();paths->make_name=A::fn<A::MakeName>(base,A::make_name);paths->make_index=A::fn<A::MakeIndex>(base,A::make_index);
        void* section=paths->name(paths->name(A::fn<A::RootPath>(base,A::root_path)(iggy),"optionsData"),"currentSection");
        int index=-1;
        if(!section || get(section,fast(iggy,"instanceIndex",-1),nullptr,&index)!=0 || index!=instance_index) return false;
        void* data=paths->name(paths->name(section,"settings"),"data");
        for(int i=0;i<static_cast<int>(rows.size());++i) {
            const auto& r=rows[static_cast<std::size_t>(i)];
            void* w=paths->name(paths->index(data,i),r.kind==Kind::slider?"slider":r.kind==Kind::toggle?"optionStepperBoolean":"optionStepper");
            int v=-1;if(!w || get(w,fast(iggy,r.kind==Kind::slider?"value":"selectedIndex",-1),nullptr,&v)!=0) return false;
            values[static_cast<std::size_t>(i)]=v;
        }
        return true;
    }
};
} // namespace d2vr
