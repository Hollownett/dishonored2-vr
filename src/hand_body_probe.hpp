#pragma once
// Plan B Task B7: READ-ONLY runtime probe for the hand-body research (docs/native-hand-body-re.md, Q1/Q7/Q8). Dev knob build/hand-body-probe, off by default.
// It answers what static RE could not: the real filter words of the player's bodies, the userdata -> owner -> entity chain (pointers and vtable RVAs only,
// never more than 64 bytes of raw memory), which filter the world installs at world+0x470, and how often 0x89fb0 runs and whether physics is busy.
// Nothing here writes the engine or calls an engine function: every read goes through the caller's Read (the mod's guarded read_self).
#include "native_rigid_body.hpp"
#include "native_world_query_source.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace d2vr {
struct FilterWord {
    std::uint32_t raw{};
    unsigned layer() const {return raw&0x1fu;}          // low 5 bits: the body's own layer
    std::uint32_t mask() const {return raw>>5;}         // upper 27 bits: the layers it collides with
};
struct BodyProbeRow {
    std::uint64_t tick{};std::uint32_t handle{},filter{},flags{};
    std::uintptr_t userdata{},userdata_plus8{},userdata_vtable_rva{};
    const char* what{};
    // Owner chain (controller ruling 2026-10-23, for B11's owner choice): userdata+8 -> owner, owner+0x10 -> entity, entity+0xa8 -> id.
    std::uintptr_t owner_vtable_rva{},entity{},entity_vtable_rva{};std::uint32_t entity_id{};
    // G2 (docs Gates for B11): owner+0x10 is the player entity?; owner->vtbl[0] is decoded WITHOUT calling it (a `lea rax,[rip+rel32]; ret` stub) to the
    // typeinfo, whose +0x60 typenum is compared with the live range at RVA 0x39e8610 / 0x39e8614. typeinfo_decoded=false when the stub is not that shape.
    std::uint32_t owner_typenum{},type_range_lo{},type_range_hi{};bool typeinfo_decoded{},type_in_range{},owner_is_player{};
};
struct QueueProbe {
    std::uint64_t callbacks{},callbacks_with_commands{};bool busy_at_stage0{},busy_at_dispatch{};std::uintptr_t world_filter_vtable_rva{};
    // Extra (B7): counts behind the two booleans, and whether the 0x89fb0 hook ever ran off the Stage0 thread.
    std::uint64_t dispatch_samples{},dispatch_busy{},stage0_samples{},stage0_busy{},off_stage0_thread{};
    // G1: thread ids seen by the existing hooks and the render thread (supplied by the sampler); changes counts how often the 0x89fb0 hook's thread differed from its previous call.
    std::uint32_t dispatch_thread{},stage0_thread{},render_thread{};std::uint64_t dispatch_thread_changes{};
};

namespace hand_body_probe_detail {
inline constexpr std::uintptr_t image_span=0x41cb000;       // SizeOfImage of the pinned Dishonored2.exe
inline constexpr std::size_t max_raw_bytes=64;              // the most this probe ever copies from one engine address
inline std::uintptr_t rva_of(std::uintptr_t base,std::uintptr_t address) {return address>=base && address-base<image_span?address-base:0;}
inline bool user_pointer(std::uintptr_t p) {return p>=0x10000 && p<0x0000800000000000 && p%8==0;}
inline bool within_page(std::uintptr_t p,std::size_t size) {return (p&0xfff)+size<=0x1000;}
// Pointers and RVAs of the chain below a userdata pointer. A pointer that is not user space, is misaligned or whose first 16 bytes would cross a
// page is not followed at all.
template<class Read> void fill_type(Read& read,std::uintptr_t base,std::uintptr_t owner_vtable,BodyProbeRow& row) {
    const auto stub=rig::value<std::uintptr_t>(read,owner_vtable);
    if(!stub || !user_pointer(*stub) || !within_page(*stub,8)) return;
    std::array<unsigned char,8> code{};
    if(!read(*stub,code.data(),code.size()) || code[0]!=0x48 || code[1]!=0x8d || code[2]!=0x05 || code[7]!=0xc3) return;
    std::int32_t rel{};std::memcpy(&rel,&code[3],4);
    const std::uintptr_t typeinfo=*stub+7+static_cast<std::intptr_t>(rel);
    const auto number=rig::value<std::uint32_t>(read,typeinfo+0x60);
    const auto lo=rig::value<std::uint32_t>(read,base+0x39e8610),hi=rig::value<std::uint32_t>(read,base+0x39e8614);
    if(!number || !lo || !hi) return;
    row.typeinfo_decoded=true;row.owner_typenum=*number;row.type_range_lo=*lo;row.type_range_hi=*hi;row.type_in_range=*number>=*lo && *number<=*hi;
}
template<class Read> void fill_chain(Read& read,std::uintptr_t base,BodyProbeRow& row,std::uintptr_t player=0) {
    row.userdata_plus8=row.userdata_vtable_rva=row.owner_vtable_rva=row.entity=row.entity_vtable_rva=0;row.entity_id=0;
    row.owner_typenum=row.type_range_lo=row.type_range_hi=0;row.typeinfo_decoded=row.type_in_range=row.owner_is_player=false;
    if(!user_pointer(row.userdata) || !within_page(row.userdata,16)) return;
    std::array<std::uintptr_t,2> head{};
    if(!read(row.userdata,head.data(),sizeof head)) return;
    row.userdata_vtable_rva=rva_of(base,head[0]);row.userdata_plus8=head[1];
    const auto owner=head[1];
    if(!user_pointer(owner) || !within_page(owner,0x18)) return;
    const auto vtable=rig::value<std::uintptr_t>(read,owner);
    const auto entity=rig::value<std::uintptr_t>(read,owner+0x10);
    if(!vtable || !entity) return;
    row.owner_vtable_rva=rva_of(base,*vtable);
    fill_type(read,base,*vtable,row);
    if(!user_pointer(*entity)) return;
    row.entity=*entity;row.owner_is_player=player && *entity==player;
    if(const auto entity_vtable=rig::value<std::uintptr_t>(read,*entity)) row.entity_vtable_rva=rva_of(base,*entity_vtable);
    if(const auto id=rig::value<std::uint32_t>(read,*entity+0xa8)) row.entity_id=*id;
}
// The pool record of a handle (world+0x28, stride 0xb0): index range and the stored handle (+0x70) must agree.
template<class Read> std::optional<std::uintptr_t> record_of(Read& read,std::uintptr_t world,std::uint32_t handle) {
    const auto pool=rig::value<std::uintptr_t>(read,world+0x28);
    const auto count=rig::value<std::uint32_t>(read,world+0x30);
    const std::uint32_t index=handle&0xffffffu;
    if(!pool || !count || !rig::pointer(*pool) || *count>0x1000000u || index==0xffffffu || index>=*count) return {};
    const auto record=*pool+static_cast<std::uintptr_t>(index)*0xb0;
    if(rig::value<std::uint32_t>(read,record+0x70)!=handle) return {};
    return record;
}
}

inline std::string body_probe_csv_header() {return "tick,what,handle,filter,layer,mask,flags,userdata,userdata_plus8,userdata_vtable_rva,owner_vtable_rva,entity,entity_vtable_rva,entity_id,owner_typenum,type_range_lo,type_range_hi,type_in_range,owner_is_player,typeinfo_decoded";}
inline std::string body_probe_csv_row(const BodyProbeRow& r) {
    const FilterWord f{r.filter};
    std::ostringstream o;
    o<<r.tick<<','<<(r.what?r.what:"")<<','<<r.handle<<",0x"<<std::hex<<r.filter<<std::dec<<','<<f.layer()<<",0x"<<std::hex<<f.mask()<<std::dec<<','<<r.flags
     <<",0x"<<std::hex<<r.userdata<<",0x"<<r.userdata_plus8<<",0x"<<r.userdata_vtable_rva<<",0x"<<r.owner_vtable_rva<<",0x"<<r.entity<<",0x"<<r.entity_vtable_rva<<std::dec<<','<<r.entity_id<<','<<r.owner_typenum<<','<<r.type_range_lo<<','<<r.type_range_hi<<','<<(r.type_in_range?1:0)<<','<<(r.owner_is_player?1:0)<<','<<(r.typeinfo_decoded?1:0);
    return o.str();
}
inline std::string queue_probe_csv_header() {return "tick,callbacks,callbacks_per_s,callbacks_with_commands,busy_at_stage0,busy_at_dispatch,dispatch_samples,dispatch_busy,stage0_samples,stage0_busy,off_stage0_thread,dispatch_thread,stage0_thread,render_thread,dispatch_thread_changes,world_filter_vtable_rva,record_types";}
inline std::string queue_probe_csv_row(std::uint64_t tick,const QueueProbe& q,std::uint64_t per_second,const std::string& record_types) {
    std::ostringstream o;
    o<<tick<<','<<q.callbacks<<','<<per_second<<','<<q.callbacks_with_commands<<','<<(q.busy_at_stage0?1:0)<<','<<(q.busy_at_dispatch?1:0)<<','<<q.dispatch_samples<<','<<q.dispatch_busy
     <<','<<q.stage0_samples<<','<<q.stage0_busy<<','<<q.off_stage0_thread<<','<<q.dispatch_thread<<','<<q.stage0_thread<<','<<q.render_thread<<','<<q.dispatch_thread_changes<<",0x"<<std::hex<<q.world_filter_vtable_rva<<std::dec<<','<<record_types;
    return o.str();
}

// Row for one body handle: filter word (record +0x6c), flags (+0x44), userdata (+0x90) and its chain. Empty when the handle does not resolve.
template<class Read> std::optional<BodyProbeRow> probe_body_handle(Read& read,std::uintptr_t base,std::uintptr_t world,std::uint32_t handle,std::uint64_t tick,const char* what,std::uintptr_t player=0) {
    if(handle==0xffffffffu || (handle&0xffffffu)==0xffffffu) return {};
    const auto record=hand_body_probe_detail::record_of(read,world,handle);
    if(!record) return {};
    const auto filter=rig::value<std::uint32_t>(read,*record+0x6c);
    const auto flags=rig::value<std::uint32_t>(read,*record+0x44);
    if(!filter || !flags) return {};
    BodyProbeRow row;row.tick=tick;row.handle=handle;row.filter=*filter;row.flags=*flags;row.what=what;
    row.userdata=rig::value<std::uintptr_t>(read,*record+0x90).value_or(0);
    hand_body_probe_detail::fill_chain(read,base,row,player);
    return row;
}
// The arkPhysicsPlayer's bodies (0x1b6ec0 / 0x1b2290): second body handle +0x210, proxy handle copy +0x214, the configured filter +0x218 (param_1[0x86]),
// material +0x21c, proxy object +0x148 whose body handle sits at +0x38; plus the query source's own body (camera controller +0x38). Unresolved handles are skipped.
template<class Read> std::vector<BodyProbeRow> probe_physics_bodies(Read& read,std::uintptr_t base,std::uintptr_t world,std::uintptr_t physics,std::uint32_t query_handle,std::uint64_t tick,std::uintptr_t player=0) {
    std::vector<BodyProbeRow> rows;
    if(!rig::pointer(world) || !rig::pointer(physics)) return rows;
    if(const auto cfg=rig::value<std::uint32_t>(read,physics+0x218)) {
        BodyProbeRow row;row.tick=tick;row.filter=*cfg;row.what="player_cfg_filter";
        row.flags=rig::value<std::uint32_t>(read,physics+0x21c).value_or(0); // material word of the same cinfo
        rows.push_back(row);
    }
    const auto push=[&](std::optional<std::uint32_t> handle,const char* what) {
        if(!handle) return;
        if(const auto row=probe_body_handle(read,base,world,*handle,tick,what,player)) rows.push_back(*row);
    };
    push(rig::value<std::uint32_t>(read,physics+0x210),"player_second_body");
    std::optional<std::uint32_t> proxy;
    if(const auto object=rig::value<std::uintptr_t>(read,physics+0x148); object && rig::pointer(*object)) proxy=rig::value<std::uint32_t>(read,*object+0x38);
    if(!proxy) proxy=rig::value<std::uint32_t>(read,physics+0x214);
    push(proxy,"player_proxy_body");
    if(query_handle) push(query_handle,"player_query_source_body");
    return rows;
}
// Player bodies that resolve: needs the camera address (the interface of the brief has no camera, so it is a defaulted fourth argument; 0 gives no rows).
// When a stage fails a "diag_*" row says which (userdata column = the address or pointer involved), so a run with no player rows is still informative.
// If the strict query source is refused, the camera's own physics object and the controller handle are used instead (the same reads, fewer cross-checks).
template<class Read> std::vector<BodyProbeRow> probe_player_bodies(Read& read,std::uintptr_t base,std::uint64_t tick,std::uintptr_t camera_address=0) {
    std::vector<BodyProbeRow> rows;
    if(!camera_address) return rows;
    const auto diag=[&](const char* what,std::uintptr_t detail) {BodyProbeRow row;row.tick=tick;row.what=what;row.userdata=detail;rows.push_back(row);};
    const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
    if(!game) {diag("diag_no_game",0);return rows;}
    const auto world=rig::value<std::uintptr_t>(read,*game+0x60410);
    if(!world) {diag("diag_no_world",*game);return rows;}
    const auto source=native_world_query_source(read,base,camera_address,*world);
    if(source) return probe_physics_bodies(read,base,*world,source->physics,source->handle,tick,source->player);
    const auto camera=native_player_camera(read,base,camera_address);
    if(!camera) {diag("diag_no_camera",camera_address);return rows;}
    diag("diag_no_query_source",camera_address);
    std::uint32_t query_handle=0;
    if(const auto controller=rig::value<std::uintptr_t>(read,camera->heading_delegate+0x148); controller && rig::pointer(*controller))
        query_handle=rig::value<std::uint32_t>(read,*controller+0x38).value_or(0);
    auto fallback=probe_physics_bodies(read,base,*world,camera->physics,query_handle,tick,camera->player);
    rows.insert(rows.end(),fallback.begin(),fallback.end());
    return rows;
}
template<class Read> std::optional<BodyProbeRow> probe_prop_userdata(Read& read,std::uintptr_t base,const NativeRigidBody& body,std::uint64_t tick) {
    if(!rig::pointer(body.record)) return {};
    const auto filter=rig::value<std::uint32_t>(read,body.record+0x6c);
    if(!filter) return {};
    BodyProbeRow row;row.tick=tick;row.handle=body.handle;row.filter=*filter;row.flags=body.flags;row.what="prop";
    row.userdata=rig::value<std::uintptr_t>(read,body.record+0x90).value_or(0);
    hand_body_probe_detail::fill_chain(read,base,row);
    return row;
}
// world+0x470: the filter object the world installs (Q7); returns the RVA of its vtable, 0 when outside the image or unreadable.
template<class Read> std::uintptr_t world_filter_vtable_rva(Read& read,std::uintptr_t base,std::uintptr_t world) {
    if(!rig::pointer(world)) return 0;
    const auto filter=rig::value<std::uintptr_t>(read,world+0x470);
    if(!filter || !rig::pointer(*filter)) return 0;
    const auto vtable=rig::value<std::uintptr_t>(read,*filter);
    return vtable?hand_body_probe_detail::rva_of(base,*vtable):0;
}
// 0x8a7820 without calling it: its three reads (game+0x337121, game+0x337122, byte +0xc34 of the object at game+0x1e8960). nullopt when unreadable.
template<class Read> std::optional<bool> physics_busy_flag(Read& read,std::uintptr_t base) {
    const auto game=rig::value<std::uintptr_t>(read,base+0x39a5290);
    if(!game) return {};
    const auto a=rig::value<std::uint8_t>(read,*game+0x337121);
    const auto b=rig::value<std::uint8_t>(read,*game+0x337122);
    const auto object=rig::value<std::uintptr_t>(read,*game+0x1e8960);
    if(!a || !b || !object || !rig::pointer(*object)) return {};
    const auto c=rig::value<std::uint8_t>(read,*object+0xc34);
    if(!c) return {};
    return *a || *b || *c;
}

// Counters fed by the existing hooks (0x89fb0 in NativeGrab::invoke, Stage0 in NativeInteractionAim). All relaxed atomics; a disabled probe costs one load.
struct HandBodyProbeCounters {
    static constexpr std::size_t record_type_slots=64;
    inline static std::atomic<bool> enabled{false};
    inline static std::atomic<std::uint64_t> callbacks{},dispatch_samples{},dispatch_busy{},stage0_samples{},stage0_busy{},off_stage0_thread{};
    inline static std::atomic<bool> last_dispatch_busy{},last_stage0_busy{};
    inline static std::atomic<std::uint32_t> stage0_thread{},dispatch_thread{};
    inline static std::atomic<std::uint64_t> dispatch_thread_changes{};
    inline static std::array<std::atomic<std::uint64_t>,record_type_slots> record_types{};
    // callbacks_with_commands: 0x89fb0 is called once per queued record, so the hook cannot see a "batch"; it is not counted (the record type histogram replaces it).
    template<class Read> static void note_dispatch(Read& read,std::uintptr_t base,std::uintptr_t command,std::uint32_t thread_id) {
        callbacks.fetch_add(1,std::memory_order_relaxed);
        if(const auto type=rig::value<std::uint16_t>(read,command+4); type && *type<record_type_slots) record_types[*type].fetch_add(1,std::memory_order_relaxed);
        const auto previous=dispatch_thread.exchange(thread_id,std::memory_order_relaxed);
        if(previous && previous!=thread_id) dispatch_thread_changes.fetch_add(1,std::memory_order_relaxed);
        const auto known=stage0_thread.load(std::memory_order_relaxed);
        if(known && known!=thread_id) off_stage0_thread.fetch_add(1,std::memory_order_relaxed);
        if(const auto busy=physics_busy_flag(read,base)) {
            dispatch_samples.fetch_add(1,std::memory_order_relaxed);last_dispatch_busy.store(*busy,std::memory_order_relaxed);
            if(*busy) dispatch_busy.fetch_add(1,std::memory_order_relaxed);
        }
    }
    template<class Read> static void note_stage0(Read& read,std::uintptr_t base,std::uint32_t thread_id) {
        stage0_thread.store(thread_id,std::memory_order_relaxed);
        if(const auto busy=physics_busy_flag(read,base)) {
            stage0_samples.fetch_add(1,std::memory_order_relaxed);last_stage0_busy.store(*busy,std::memory_order_relaxed);
            if(*busy) stage0_busy.fetch_add(1,std::memory_order_relaxed);
        }
    }
    static QueueProbe snapshot() {
        QueueProbe q;q.callbacks=callbacks.load();q.busy_at_stage0=last_stage0_busy.load();q.busy_at_dispatch=last_dispatch_busy.load();
        q.dispatch_samples=dispatch_samples.load();q.dispatch_busy=dispatch_busy.load();q.stage0_samples=stage0_samples.load();q.stage0_busy=stage0_busy.load();
        q.off_stage0_thread=off_stage0_thread.load();q.dispatch_thread=dispatch_thread.load();q.stage0_thread=stage0_thread.load();q.dispatch_thread_changes=dispatch_thread_changes.load();return q;
    }
    static std::string record_type_text() {
        std::ostringstream o;bool first=true;
        for(std::size_t i=0;i<record_type_slots;++i) if(const auto n=record_types[i].load()) {if(!first) o<<';';first=false;o<<'t'<<i<<':'<<n;}
        return first?"none":o.str();
    }
    static void reset() {
        callbacks=dispatch_samples=dispatch_busy=stage0_samples=stage0_busy=off_stage0_thread=0;last_dispatch_busy=last_stage0_busy=false;stage0_thread=0;dispatch_thread=0;dispatch_thread_changes=0;
        for(auto& slot:record_types) slot=0;
    }
};

// One set per second, at most 120 sets, written by its own thread (the caller supplies the sampler, which reads through the mod's guarded read_self).
class HandBodyProbeRunner {
public:
    struct Sample {std::vector<BodyProbeRow> rows;std::uintptr_t world_filter_vtable_rva{};std::uint32_t render_thread{};};
    using Sampler=std::function<Sample(std::uint64_t tick)>;
    static constexpr unsigned max_sets=120;
    ~HandBodyProbeRunner() {stop();}
    bool start(const std::filesystem::path& directory,Sampler sampler) {
        stop();
        bodies_.open(directory/L"hand-body-probe.csv",std::ios::trunc);queue_.open(directory/L"hand-body-queue.csv",std::ios::trunc);
        if(!bodies_ || !queue_) {bodies_.close();queue_.close();return false;}
        bodies_<<body_probe_csv_header()<<'\n';queue_<<queue_probe_csv_header()<<'\n';bodies_.flush();queue_.flush();
        HandBodyProbeCounters::reset();HandBodyProbeCounters::enabled.store(true);
        sampler_=std::move(sampler);{std::lock_guard<std::mutex> lock(mutex_);quit_=false;}
        worker_=std::thread([this] {run();});
        return true;
    }
    void stop() {
        {std::lock_guard<std::mutex> lock(mutex_);quit_=true;}
        wake_.notify_all();
        if(worker_.joinable()) worker_.join();
        HandBodyProbeCounters::enabled.store(false);
        bodies_.close();queue_.close();
    }
private:
    void run() {
        std::uint64_t previous=0;
        for(unsigned set=0;set<max_sets;++set) {
            {std::unique_lock<std::mutex> lock(mutex_);if(wake_.wait_for(lock,std::chrono::seconds(1),[this] {return quit_;})) return;}
            const std::uint64_t tick=set+1;
            try {
                const auto sample=sampler_?sampler_(tick):Sample{};
                for(const auto& row:sample.rows) bodies_<<body_probe_csv_row(row)<<'\n';
                auto q=HandBodyProbeCounters::snapshot();q.world_filter_vtable_rva=sample.world_filter_vtable_rva;q.render_thread=sample.render_thread;
                queue_<<queue_probe_csv_row(tick,q,q.callbacks-previous,HandBodyProbeCounters::record_type_text())<<'\n';previous=q.callbacks;
                bodies_.flush();queue_.flush();
            } catch(...) {return;}
        }
    }
    std::ofstream bodies_,queue_;Sampler sampler_;std::thread worker_;std::mutex mutex_;std::condition_variable wake_;bool quit_{};
};
}
