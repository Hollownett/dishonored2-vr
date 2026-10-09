#pragma once
#include "game_rig.hpp"
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <stdexcept>
#include <type_traits>

namespace d2vr {
struct NativeLeanQuerySnapshot {
    std::uintptr_t game{},player{},machine{},sensor{},declaration{},shape{},manager{},world{};
    rig::Identity sensor_identity{},machine_identity{};
    std::uint64_t shape_word{},world_word{};
    std::array<std::byte,24> declaration_geometry{};
    std::array<float,3> origin{};
    std::array<float,9> axes{};
    std::array<int,2> ids{};
    std::array<float,2> limits{};
    std::uint32_t mask{};
    bool enabled{};
};
inline bool same_lean_resources(const NativeLeanQuerySnapshot& a,const NativeLeanQuerySnapshot& b) {
    return a.game==b.game && a.player==b.player && a.machine==b.machine && a.sensor==b.sensor &&
        a.declaration==b.declaration && a.shape==b.shape && a.manager==b.manager && a.world==b.world &&
        a.sensor_identity==b.sensor_identity && a.machine_identity==b.machine_identity &&
        a.shape_word==b.shape_word && a.world_word==b.world_word && a.declaration_geometry==b.declaration_geometry;
}
template<class Read> std::optional<NativeLeanQuerySnapshot> native_lean_query_snapshot(Read& read,std::uintptr_t base,std::uintptr_t sensor) noexcept {
    try {
        using namespace rig;
        if(!typed(read,base,sensor,0x1b40648)) return {};
        NativeLeanQuerySnapshot s{};s.sensor=sensor;
        const auto game=value<std::uintptr_t>(read,base+0x39a5290);
        if(!game || !typed(read,base,*game,0x1c940a8)) return {};s.game=*game;
        const auto player=value<std::uintptr_t>(read,*game+0x3130b0);
        const auto machine=value<std::uintptr_t>(read,sensor+0x20);
        const auto identity=started(read,sensor);
        if(!player || !typed(read,base,*player,0x1c5e428) || value<std::uintptr_t>(read,sensor+0x10)!=player ||
           !machine || !typed(read,base,*machine,0x1c73d58) || value<std::uintptr_t>(read,*machine+0x10)!=player || !identity) return {};
        const auto machine_identity=started(read,*machine);if(!machine_identity) return {};
        s.player=*player;s.machine=*machine;s.sensor_identity=*identity;s.machine_identity=*machine_identity;
        const auto decl=value<std::uintptr_t>(read,sensor+8),shape=value<std::uintptr_t>(read,sensor+0x40);
        const auto world=value<std::uintptr_t>(read,*game+0x60410);
        if(!decl || !pointer(*decl) || !shape || !pointer(*shape) || !world || !pointer(*world)) return {};
        s.declaration=*decl;s.shape=*shape;s.manager=*game+0x60280;s.world=*world;
        const auto shape_word=value<std::uint64_t>(read,*shape),world_word=value<std::uint64_t>(read,*world);
        const auto enabled=value<unsigned char>(read,sensor+0x38);
        if(!shape_word || !world_word || !enabled || *enabled>1 || !read(*decl+0x70,s.declaration_geometry.data(),24) ||
           !read(sensor+0x48,s.origin.data(),12) || !read(sensor+0x54,s.axes.data(),36) ||
           !read(sensor+0x78,s.ids.data(),8) || !read(sensor+0x80,s.limits.data(),8)) return {};
        s.shape_word=*shape_word;s.world_word=*world_word;s.enabled=*enabled!=0;
        std::array<float,4> geometry{};std::memcpy(geometry.data(),s.declaration_geometry.data(),16);
        std::memcpy(&s.mask,s.declaration_geometry.data()+20,4);
        for(float f:geometry) if(!std::isfinite(f) || f<=0 || f>10) return {};
        for(float f:s.origin) if(!std::isfinite(f)) return {};
        for(float f:s.axes) if(!std::isfinite(f)) return {};
        for(float f:s.limits) if(!std::isfinite(f)) return {};
        std::array<std::byte,24> again{};
        if(value<std::uintptr_t>(read,base+0x39a5290)!=game || value<std::uintptr_t>(read,*game+0x3130b0)!=player ||
           !typed(read,base,sensor,0x1b40648) || value<std::uintptr_t>(read,sensor+0x10)!=player ||
           value<std::uintptr_t>(read,sensor+0x20)!=machine || started(read,sensor)!=identity || started(read,*machine)!=machine_identity ||
           !typed(read,base,*machine,0x1c73d58) || value<std::uintptr_t>(read,*machine+0x10)!=player ||
           value<std::uintptr_t>(read,sensor+8)!=decl || value<std::uintptr_t>(read,sensor+0x40)!=shape ||
           value<std::uintptr_t>(read,*game+0x60410)!=world || value<std::uint64_t>(read,*shape)!=shape_word ||
           value<std::uint64_t>(read,*world)!=world_word || !read(*decl+0x70,again.data(),24) || again!=s.declaration_geometry) return {};
        return s;
    } catch(...) {return {};}
}
struct NativeLeanQueryEvent {
    NativeLeanQuerySnapshot state;
    unsigned phase{},thread{},rejection{};
    std::uintptr_t caller{},id_pointer{},request_pointer{},axes_pointer{},result_pointer{};
    std::uint32_t mask{},flags{};
    int query_id{-1};
    std::array<std::byte,112> request{};
    std::array<std::byte,48> axes{};
};
template<class Read> bool native_lean_query_arguments(Read& read,const NativeLeanQuerySnapshot& s,
    std::uintptr_t caller,std::uintptr_t base,std::uintptr_t manager,std::uintptr_t request,std::uintptr_t axes,
    std::uint32_t mask,std::uint32_t flags,NativeLeanQueryEvent& out) noexcept {
    try {
        if(!s.enabled || manager!=s.manager || (caller!=base+0x3b9962 && caller!=base+0x3b9a49) ||
           mask!=s.mask || flags!=0x40 || !request || !axes ||
           !read(request,out.request.data(),112) || !read(axes,out.axes.data(),48)) return false;
        std::uintptr_t shape{};std::memcpy(&shape,out.request.data()+0x28,8);
        if(shape!=s.shape) return false;
        out.state=s;out.caller=caller;out.request_pointer=request;out.axes_pointer=axes;out.mask=mask;out.flags=flags;
        return true;
    } catch(...) {return false;}
}
// Observation failures never suppress/retry the native call or alter its result.
template<class Before,class Original,class After> auto passive_lean_call(Before before,Original original,After after)->decltype(original()) {
    try {before();} catch(...) {}
    if constexpr(std::is_void_v<decltype(original())>) {original();try {after();} catch(...) {}}
    else {auto result=original();try {after(result);} catch(...) {}return result;}
}
struct NativeLeanTimedEvent {NativeLeanQueryEvent event;std::uint64_t tick{};};
struct NativeLeanQueryTransaction {
    std::array<NativeLeanTimedEvent,8> events{};
    std::size_t size{},total{};
    bool complete{true};
    void push(const NativeLeanQueryEvent& event,std::uint64_t tick) noexcept {
        ++total;if(size<events.size()) events[size++]={event,tick};else complete=false;
    }
};
class NativeLeanSamplingPolicy {
    std::optional<NativeLeanQuerySnapshot> last_;
    std::uint64_t selected_tick_{},seen_tick_{};
    bool last_rejection_{};
public:
    void reset() noexcept {last_.reset();selected_tick_=seen_tick_=0;last_rejection_=false;}
    unsigned select(const NativeLeanQuerySnapshot& before,const NativeLeanQuerySnapshot& after,std::uint64_t tick,bool rejection) {
        unsigned reason=0;
        if(!last_) reason|=1;
        else {
            if(!same_lean_resources(*last_,before) || !same_lean_resources(before,after)) reason|=2;
            if(last_->enabled!=before.enabled || before.enabled!=after.enabled) reason|=4;
            if(tick<seen_tick_) reason|=32;
            else if(tick-selected_tick_>=500) reason|=16;
        }
        if(rejection!=last_rejection_) reason|=8;
        last_=after;last_rejection_=rejection;seen_tick_=tick;if(reason) selected_tick_=tick;
        return reason;
    }
};
class NativeLeanQueryTrace {
    std::ofstream file_;
    std::mutex mutex_;
    NativeLeanSamplingPolicy sampling_;
    std::atomic<std::uint64_t> rows_{},dropped_{},rejected_{},queues_{},transactions_{},skipped_{};
    std::atomic<bool> failed_{};
    template<std::size_t N> void hex(const std::array<std::byte,N>& bytes) {
        file_<<std::hex<<std::setfill('0');for(auto b:bytes) file_<<std::setw(2)<<std::to_integer<unsigned>(b);file_<<std::dec<<std::setfill(' ');
    }
    void row(const NativeLeanTimedEvent& timed,std::uint64_t transaction,unsigned reason) {
        const auto& event=timed.event;const auto& s=event.state;
        file_<<rows_+1<<','<<timed.tick<<','<<event.thread<<','<<event.phase<<','<<event.rejection<<','<<event.caller<<','<<s.sensor<<','<<s.player<<','<<s.machine<<','
            <<s.sensor_identity.generation<<','<<s.sensor_identity.flags<<','<<s.machine_identity.generation<<','<<s.machine_identity.flags<<','<<s.declaration<<','<<s.shape<<','
            <<s.shape_word<<','<<s.manager<<','<<s.world<<','<<s.world_word<<','<<s.enabled<<','<<s.ids[0]<<','<<s.ids[1]<<','<<s.limits[0]<<','<<s.limits[1]<<','
            <<event.id_pointer<<','<<event.query_id<<','<<event.result_pointer<<','<<event.request_pointer<<','<<event.axes_pointer<<','<<event.mask<<','<<event.flags<<',';
        hex(s.declaration_geometry);file_<<',';hex(event.request);file_<<',';hex(event.axes);file_<<','<<transaction<<','<<reason<<'\n';++rows_;
    }
public:
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);if(file_.is_open()) file_.close();file_.clear();sampling_.reset();
        rows_=dropped_=rejected_=queues_=transactions_=skipped_=0;failed_=false;
    }
    void open(const std::filesystem::path& path) {
        std::lock_guard<std::mutex> lock(mutex_);file_.open(path,std::ios::trunc);
        if(!file_) throw std::runtime_error("Cannot open passive Lean query trace");
        sampling_.reset();rows_=dropped_=rejected_=queues_=transactions_=skipped_=0;failed_=false;
        file_<<std::setprecision(9)<<"sequence,tick_ms,thread,phase,rejection,caller,sensor,player,machine,sensor_generation,sensor_flags,machine_generation,machine_flags,declaration,shape,shape_word,manager,world,world_word,enabled,left_id,right_id,left_limit,right_limit,id_pointer,query_id,result_pointer,request_pointer,axes_pointer,mask,extra_flags,declaration24,request112,axes48,transaction,sampling_reason\n";
        file_.flush();if(!file_) throw std::runtime_error("Cannot save passive Lean query header");
    }
    void reject() noexcept {++rejected_;}
    void queue() noexcept {++queues_;}
    // Clock is sampled under this lock so worker arrival order cannot imitate rollback.
    template<class Clock> void commit(const NativeLeanQueryTransaction& tx,Clock clock) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if(!tx.complete || tx.size<2 || tx.events[0].event.phase!=0 || tx.events[tx.size-1].event.phase!=1) {
                dropped_+=tx.total;reject();return;
            }
            for(std::size_t i=1;i+1<tx.size;i+=2) {
                if(i+1>=tx.size-1 || tx.events[i].event.phase!=2 || tx.events[i+1].event.phase!=3 ||
                   tx.events[i].event.caller!=tx.events[i+1].event.caller ||
                   tx.events[i].event.id_pointer!=tx.events[i+1].event.id_pointer) {dropped_+=tx.total;reject();return;}
            }
            bool rejection=false;for(std::size_t i=0;i<tx.size;++i) rejection=rejection || tx.events[i].event.rejection!=0;
            const auto reason=sampling_.select(tx.events[0].event.state,tx.events[tx.size-1].event.state,clock(),rejection);
            if(!reason) {++skipped_;return;}
            if(rows_+tx.size>256) {dropped_+=tx.size;return;}
            const auto transaction=++transactions_;
            for(std::size_t i=0;i<tx.size;++i) row(tx.events[i],transaction,reason);
            file_.flush();if(!file_) failed_=true;
        } catch(...) {failed_=true;}
    }
    bool close() noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if(file_.is_open()) {file_.flush();if(!file_) failed_=true;file_.close();if(file_.fail()) failed_=true;}
            return !failed_;
        } catch(...) {failed_=true;return false;}
    }
    std::uint64_t rows() const noexcept {return rows_;}
    std::uint64_t dropped() const noexcept {return dropped_;}
    std::uint64_t rejected() const noexcept {return rejected_;}
    std::uint64_t queues() const noexcept {return queues_;}
    std::uint64_t transactions() const noexcept {return transactions_;}
    std::uint64_t skipped() const noexcept {return skipped_;}
    bool failed() const noexcept {return failed_;}
};
} // namespace d2vr
