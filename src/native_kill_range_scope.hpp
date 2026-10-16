#pragma once
#include <windows.h>
#include "native_slash_speed.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace d2vr {
class NativeKillRangeScope {
public:
    enum class Reason {
        none,read_failure,read_budget,work_budget,changed,invalid_owner,invalid_graph,
        unsupported_condition,unsupported_outcome,invalid_cache,aliased_cache,no_pairs,
        nonpaired_use,reference_count,warm_cache,wrong_consumer,expired_cycle,invalid_factor
    };
    struct Limits {
        std::uint32_t nodes=4096,handlers=32,rules_per_handler=64,conditions_per_rule=16,
            total_rules=2048,condition_edges=8192,witnesses=65536,cache_bytes=1048576;
        std::uint64_t read_bytes=2097152,work=1000000;
    };
    struct View {alignas(16) std::array<unsigned char,0x30> bytes{};};
    inline static constexpr std::array<std::uintptr_t,4> condition_vtables{0x1c8f158,0x1ca2140,0x1cc5cb0,0x1cc5ce0};
    inline static constexpr std::array<std::uintptr_t,4> condition_evaluators{0x8942b0,0x8ba770,0x902100,0x902100};
    inline static constexpr std::array<unsigned,5> slot_map{1,1,1,3,2};
    inline static constexpr std::uintptr_t outcome_vtable=0x1c8ece0,accessor_vtable=0x1c46470,slot_vtable=0x1b3f978;
    struct Pair {std::uintptr_t rule{},distance{},height{};};
private:
    struct Witness {std::uintptr_t address{};std::array<unsigned char,64> bytes{};std::size_t size{};};
    struct Handler {std::uintptr_t address{},cache{};std::uint32_t size{},time_offset{};};
    struct Edge {
        std::uintptr_t handler{},rule{},condition{},definition{},accessor{};
        std::uint32_t references{},bits{};std::int32_t result{},variable{},op{};unsigned type{};
    };
public:
    class Snapshot {
    public:
        bool ok() const noexcept {return reason_==Reason::none;}
        Reason reason() const noexcept {return reason_;}
        std::uint64_t read_bytes() const noexcept {return read_bytes_;}
        std::uint64_t read_calls() const noexcept {return read_calls_;}
        std::uint64_t view_read_calls() const noexcept {return view_read_calls_;}
        Reason last_view_reason() const noexcept {return last_view_reason_;}
        std::uint64_t work() const noexcept {return work_;}
        const std::vector<Pair>& pairs() const noexcept {return pairs_;}
        template<class Read> bool revalidate(Read& read) noexcept {
            if(!ok())return false;
            try {
                Reader<Read> reader{*this,read};
                for(const auto& witness:witnesses_) {
                    std::array<unsigned char,64> now{};
                    if(!reader.read(witness.address,now.data(),witness.size,false))return false;
                    if(std::memcmp(now.data(),witness.bytes.data(),witness.size))return fail(Reason::changed);
                }
                const auto serial=reader.template value<std::uint32_t>(handler_+0x40,false);
                if(!serial || *serial!=serial_)return fail(Reason::expired_cycle);
                return true;
            } catch(...) {return fail(Reason::work_budget);}
        }
        template<class Read> bool view(Read& read,std::uintptr_t caller,std::uintptr_t condition,std::uintptr_t context,View& output) noexcept {
            struct Metrics {Snapshot& owner;std::uint64_t start;~Metrics(){owner.view_read_calls_+=owner.read_calls_-start;if(!owner.ok() && owner.last_view_reason_==Reason::none)owner.last_view_reason_=owner.reason_;}} metrics{*this,read_calls_};
            last_view_reason_=Reason::none;
            if(!ok()) {last_view_reason_=reason_;return false;}
            if(caller!=base_+0x896473 || context!=handler_+0x40) {last_view_reason_=Reason::wrong_consumer;return false;}
            try {
                Reader<Read> reader{*this,read};
                const auto rule=reader.template value<std::uintptr_t>(context+0x38,false);if(!rule)return false;
                const auto found=std::find_if(pairs_.begin(),pairs_.end(),[&](const Pair& pair){return pair.rule==*rule && (pair.distance==condition || pair.height==condition);});
                if(found==pairs_.end()) {last_view_reason_=Reason::wrong_consumer;return false;}
                if(!revalidate(read))return false;
                View copy{};
                if(!reader.read(condition,copy.bytes.data(),copy.bytes.size(),false))return false;
                const auto original=std::find_if(witnesses_.begin(),witnesses_.end(),[&](const Witness& witness){return witness.address==condition && witness.size==copy.bytes.size();});
                if(original==witnesses_.end() || std::memcmp(original->bytes.data(),copy.bytes.data(),copy.bytes.size()))return fail(Reason::changed);
                if(reader.template value<std::uintptr_t>(context+0x38,false)!=rule || reader.template value<std::uint32_t>(context,false)!=serial_)return fail(Reason::expired_cycle);
                float threshold{};std::memcpy(&threshold,copy.bytes.data()+0x2c,4);threshold*=factor_;
                std::memcpy(copy.bytes.data()+0x2c,&threshold,4);output=copy;return true;
            } catch(...) {return fail(Reason::work_budget);}
        }
    private:
        friend class NativeKillRangeScope;
        template<class Read> struct Reader {
            Snapshot& snapshot;Read& source;
            bool read(std::uintptr_t address,void* output,std::size_t size,bool record=true) {
                if(!snapshot.ok())return false;
                if(size>64 || address<0x10000 || address>=0x0000800000000000ull-size)return snapshot.fail(Reason::read_failure);
                if(snapshot.read_bytes_>snapshot.limits_.read_bytes || size>snapshot.limits_.read_bytes-snapshot.read_bytes_)return snapshot.fail(Reason::read_budget);
                snapshot.read_bytes_+=size;++snapshot.read_calls_;
                if(!guarded(source,address,output,size))return snapshot.fail(Reason::read_failure);
                if(record) {
                    if(snapshot.witnesses_.size()>=snapshot.limits_.witnesses)return snapshot.fail(Reason::work_budget);
                    Witness witness{address,{},size};std::memcpy(witness.bytes.data(),output,size);snapshot.witnesses_.push_back(witness);
                }
                return true;
            }
            bool operator()(std::uintptr_t address,void* output,std::size_t size) {return read(address,output,size);}
            template<class Value> std::optional<Value> value(std::uintptr_t address,bool record=true) {
                Value result{};if(!read(address,&result,sizeof(result),record))return {};return result;
            }
            static bool cpp(Read& source,std::uintptr_t address,void* output,std::size_t size) noexcept {try {return source(address,output,size);}catch(...) {return false;}}
            static bool guarded(Read& source,std::uintptr_t address,void* output,std::size_t size) noexcept {
                __try {return cpp(source,address,output,size);}__except(EXCEPTION_EXECUTE_HANDLER) {return false;}
            }
        };
        bool fail(Reason reason) noexcept {if(ok())reason_=reason;return false;}
        bool step(std::uint64_t count=1) noexcept {
            if(work_>limits_.work || count>limits_.work-work_)return fail(Reason::work_budget);
            work_+=count;return true;
        }
        Limits limits_{};Reason reason_=Reason::invalid_owner;
        std::uintptr_t base_{},sensor_{},handler_{},slot_{};std::uint32_t serial_{};float factor_{};
        std::uint64_t read_bytes_{},read_calls_{},view_read_calls_{},work_{};
        Reason last_view_reason_=Reason::none;
        std::vector<Witness> witnesses_;
        std::vector<Pair> pairs_;
    };
    template<class Read> static Snapshot capture(Read& read,std::uintptr_t base,std::uintptr_t sensor,std::uintptr_t handler,float factor,const Limits& limits) noexcept {
        Snapshot snapshot;snapshot.reason_=Reason::none;snapshot.limits_=limits;snapshot.base_=base;snapshot.sensor_=sensor;snapshot.handler_=handler;snapshot.factor_=factor;
        try {
            typename Snapshot::template Reader<Read> reader{snapshot,read};
            build(snapshot,reader);
            if(snapshot.ok())snapshot.revalidate(read);
        } catch(...) {snapshot.fail(Reason::work_budget);}
        return snapshot;
    }
    template<class Read> static Snapshot capture(Read& read,std::uintptr_t base,std::uintptr_t sensor,std::uintptr_t handler,float factor) noexcept {
        return capture(read,base,sensor,handler,factor,Limits{});
    }
private:
    template<class Value,class Buffer> static Value field(const Buffer& buffer,std::size_t offset) noexcept {Value value{};std::memcpy(&value,buffer.data()+offset,sizeof(value));return value;}
    static bool interval(std::uintptr_t address,std::uint64_t bytes) noexcept {return address>=0x10000 && bytes && address<0x0000800000000000ull-bytes;}
    static bool overlap(std::uintptr_t first,std::uint64_t first_size,std::uintptr_t second,std::uint64_t second_size) noexcept {
        return first<second+second_size && second<first+first_size;
    }
    template<class Reader> static std::optional<rig::List> list(Snapshot& snapshot,Reader& reader,std::uintptr_t address,std::uint32_t maximum) {
        const auto stored=reader.template value<rig::List>(address);if(!stored)return {};
        auto result=*stored;result.packed_count&=0xffffff;result.packed_capacity>>=8;
        if(result.packed_count>result.packed_capacity) {snapshot.fail(Reason::invalid_graph);return {};}
        if(result.packed_count>maximum) {snapshot.fail(Reason::work_budget);return {};}
        if(result.packed_count && (!rig::pointer(result.data) || !interval(result.data,result.packed_count*8ull))) {snapshot.fail(Reason::invalid_graph);return {};}
        return result;
    }
    template<class Reader> static void build(Snapshot& snapshot,Reader& reader) {
        using namespace rig;
        if(!std::isfinite(snapshot.factor_) || snapshot.factor_<=1.f || snapshot.factor_>2.f) {snapshot.fail(Reason::invalid_factor);return;}
        const auto machine=reader.template value<std::uintptr_t>(snapshot.sensor_+0x20);
        const auto owner=machine?native_player_state_lease(reader,snapshot.base_,*machine):std::nullopt;
        const auto identity=started(reader,snapshot.sensor_);
        if(!owner || !identity || !typed(reader,snapshot.base_,snapshot.sensor_,0x1b3f998) ||
            reader.template value<std::uintptr_t>(snapshot.sensor_+0x10)!=owner->player ||
            reader.template value<unsigned char>(snapshot.sensor_+0x38).value_or(0)==0) {snapshot.fail(Reason::invalid_owner);return;}
        const auto components=list(snapshot,reader,owner->player+0x268,snapshot.limits_.nodes);if(!components)return;
        unsigned sensor_memberships{},machine_memberships{};
        for(unsigned index=0;index<components->packed_count;++index) {
            if(!snapshot.step())return;
            const auto component=reader.template value<std::uintptr_t>(components->data+index*8ull);
            if(component==snapshot.sensor_)++sensor_memberships;if(component==*machine)++machine_memberships;
        }
        if(sensor_memberships!=1 || machine_memberships!=1) {snapshot.fail(Reason::invalid_owner);return;}
        for(unsigned category=0;category<slot_map.size();++category) {
            const auto selected=reader.template value<std::uintptr_t>(snapshot.sensor_+0x1a8+category*8ull);if(!selected)return;
            if(*selected==snapshot.handler_) {
                const auto slot=snapshot.sensor_+0x40+slot_map[category]*0x38ull;
                if(snapshot.slot_ && snapshot.slot_!=slot) {snapshot.fail(Reason::invalid_graph);return;}
                snapshot.slot_=slot;
            }
        }
        if(!snapshot.slot_ || !typed(reader,snapshot.base_,snapshot.slot_,slot_vtable) ||
            reader.template value<std::uintptr_t>(snapshot.slot_+8)!=snapshot.sensor_) {snapshot.fail(Reason::invalid_owner);return;}
        const auto target=reader.template value<std::uint32_t>(snapshot.slot_+0x20);
        if(!target || !*target || *target==0x3fff0010) {snapshot.fail(Reason::invalid_owner);return;}
        const auto buckets=reader.template value<std::uintptr_t>(snapshot.sensor_+0x190);
        const auto bucket_count=reader.template value<std::int32_t>(snapshot.sensor_+0x198);
        const auto node_count=reader.template value<std::uint32_t>(snapshot.sensor_+0x19c);
        const auto mask=reader.template value<std::uint32_t>(snapshot.sensor_+0x1a0);
        if(!buckets || !pointer(*buckets) || !bucket_count || *bucket_count!=256 || !mask || *mask!=255 || !node_count || *node_count>snapshot.limits_.nodes) {snapshot.fail(Reason::invalid_graph);return;}
        std::array<std::uintptr_t,256> bucket_heads{};
        for(std::size_t offset=0;offset<bucket_heads.size();offset+=8)
            if(!reader.read(*buckets+offset*8,bucket_heads.data()+offset,64))return;
        std::vector<std::uintptr_t> nodes,rules;
        std::vector<Handler> handlers;
        std::vector<Edge> edges;
        for(std::int32_t bucket=0;bucket<*bucket_count;++bucket) {
            auto node=std::optional<std::uintptr_t>{bucket_heads[static_cast<unsigned>(bucket)]};
            while(*node) {
                if(!snapshot.step() || nodes.size()>=snapshot.limits_.nodes) {snapshot.fail(Reason::work_budget);return;}
                if(!snapshot.step(nodes.size()+handlers.size()))return;
                if(!pointer(*node) || std::find(nodes.begin(),nodes.end(),*node)!=nodes.end()) {snapshot.fail(Reason::invalid_graph);return;}
                nodes.push_back(*node);
                std::array<std::uintptr_t,2> links{};if(!reader.read(*node+0x28,links.data(),sizeof(links)))return;
                const auto current=std::optional<std::uintptr_t>{links[0]},next=std::optional<std::uintptr_t>{links[1]};
                if(!pointer(*current) || std::any_of(handlers.begin(),handlers.end(),[&](const Handler& item){return item.address==*current;})) {snapshot.fail(Reason::invalid_graph);return;}
                if(handlers.size()>=snapshot.limits_.handlers) {snapshot.fail(Reason::work_budget);return;}
                std::array<unsigned char,16> owner_fields{},cache_fields{};
                if(!reader.read(*current+0x30,owner_fields.data(),owner_fields.size()) || !reader.read(*current+0x48,cache_fields.data(),cache_fields.size()))return;
                const auto spawn=std::optional<std::uint32_t>{field<std::uint32_t>(owner_fields,0)};
                const auto cache=std::optional<std::uintptr_t>{field<std::uintptr_t>(cache_fields,0)};
                const auto size=std::optional<std::uint32_t>{field<std::uint32_t>(cache_fields,8)};
                const auto time=std::optional<std::uint32_t>{field<std::uint32_t>(owner_fields,8)};
                if(!spawn || *spawn!=owner->spawn || !cache || !size || !time) {snapshot.fail(Reason::invalid_owner);return;}
                if(*size>snapshot.limits_.cache_bytes || (*size && (!pointer(*cache) || !interval(*cache,*size)))) {snapshot.fail(Reason::invalid_cache);return;}
                if(*time!=0x80000000u && (*time%4 || *time>*size || *size-*time<8)) {snapshot.fail(Reason::invalid_cache);return;}
                for(const auto& existing:handlers)if(existing.size && *size && overlap(existing.cache,existing.size,*cache,*size)) {snapshot.fail(Reason::aliased_cache);return;}
                handlers.push_back({*current,*cache,*size,*time});
                const auto current_rules=list(snapshot,reader,*current+0x20,snapshot.limits_.rules_per_handler);if(!current_rules)return;
                for(unsigned rule_index=0;rule_index<current_rules->packed_count;++rule_index) {
                    if(!snapshot.step() || rules.size()>=snapshot.limits_.total_rules) {snapshot.fail(Reason::work_budget);return;}
                    if(!snapshot.step(rules.size()))return;
                    const auto rule=reader.template value<std::uintptr_t>(current_rules->data+rule_index*8ull);
                    if(!rule || !pointer(*rule) || std::find(rules.begin(),rules.end(),*rule)!=rules.end()) {snapshot.fail(Reason::invalid_graph);return;}
                    rules.push_back(*rule);
                    const auto outcome=reader.template value<std::uintptr_t>(*rule+0x10);
                    if(!outcome || !typed(reader,snapshot.base_,*outcome,outcome_vtable) ||
                        reader.template value<std::uintptr_t>(snapshot.base_+outcome_vtable+0x10)!=snapshot.base_+0x695390 ||
                        reader.template value<std::uintptr_t>(snapshot.base_+outcome_vtable+0x20)!=snapshot.base_+0x11d080) {snapshot.fail(Reason::unsupported_outcome);return;}
                    const auto conditions=list(snapshot,reader,*rule,snapshot.limits_.conditions_per_rule);if(!conditions)return;
                    std::vector<std::uintptr_t> rule_conditions;
                    std::uintptr_t distance{},height{};unsigned float_count{};
                    for(unsigned condition_index=0;condition_index<conditions->packed_count;++condition_index) {
                        if(!snapshot.step() || edges.size()>=snapshot.limits_.condition_edges) {snapshot.fail(Reason::work_budget);return;}
                        if(!snapshot.step(rule_conditions.size()))return;
                        const auto condition=reader.template value<std::uintptr_t>(conditions->data+condition_index*8ull);
                        if(!condition || !pointer(*condition) || std::find(rule_conditions.begin(),rule_conditions.end(),*condition)!=rule_conditions.end()) {snapshot.fail(Reason::invalid_graph);return;}
                        rule_conditions.push_back(*condition);
                        const auto vtable=reader.template value<std::uintptr_t>(*condition);
                        const auto found=vtable?std::find(condition_vtables.begin(),condition_vtables.end(),*vtable-snapshot.base_):condition_vtables.end();
                        if(found==condition_vtables.end()) {snapshot.fail(Reason::unsupported_condition);return;}
                        const auto type=static_cast<unsigned>(found-condition_vtables.begin());
                        if(reader.template value<std::uintptr_t>(*vtable+0x20)!=snapshot.base_+condition_evaluators[type]) {snapshot.fail(Reason::unsupported_condition);return;}
                        std::array<unsigned char,0x30> data{};if(!reader.read(*condition,data.data(),data.size()))return;
                        const auto references=std::optional<std::uint32_t>{field<std::uint32_t>(data,8)};
                        const auto result=std::optional<std::int32_t>{field<std::int32_t>(data,0xc)};
                        const auto variable=std::optional<std::int32_t>{field<std::int32_t>(data,0x10)};
                        const auto definition=std::optional<std::uintptr_t>{field<std::uintptr_t>(data,0x18)};
                        const auto accessor=std::optional<std::uintptr_t>{field<std::uintptr_t>(data,0x20)};
                        const auto operation=std::optional<std::int32_t>{field<std::int32_t>(data,0x28)};
                        const auto bits=std::optional<std::uint32_t>{field<std::uint32_t>(data,0x2c)};
                        if(!references || !result || !variable || !definition || !accessor || !operation || !bits)return;
                        if(*result<0 || *variable<0 || *result%4 || *variable%4 || static_cast<unsigned>(*result)>*size || *size-static_cast<unsigned>(*result)<8 ||
                            static_cast<unsigned>(*variable)>*size || *size-static_cast<unsigned>(*variable)<8) {snapshot.fail(Reason::invalid_cache);return;}
                        if(!pointer(*definition) || !pointer(*accessor) || *operation<0 || *operation>5) {snapshot.fail(Reason::invalid_graph);return;}
                        edges.push_back({*current,*rule,*condition,*definition,*accessor,*references,*bits,*result,*variable,*operation,type});
                        if(type==1) {
                            ++float_count;float threshold{};std::memcpy(&threshold,&*bits,4);
                            if(*current==snapshot.handler_ && *operation==4 && (threshold==3.f || threshold==1.7f) && pair_accessor(snapshot,reader,*accessor,*definition,threshold==3.f)) {
                                auto& destination=threshold==3.f?distance:height;if(!destination)destination=*condition;
                            }
                        }
                    }
                    if(*current==snapshot.handler_ && float_count==2 && distance && height) {
                        const auto distance_edge=std::find_if(edges.begin(),edges.end(),[&](const Edge& edge){return edge.rule==*rule && edge.condition==distance;});
                        const auto height_edge=std::find_if(edges.begin(),edges.end(),[&](const Edge& edge){return edge.rule==*rule && edge.condition==height;});
                        if(distance_edge->definition==height_edge->definition || overlap(static_cast<std::uintptr_t>(distance_edge->variable),8,static_cast<std::uintptr_t>(height_edge->variable),8)) {snapshot.fail(Reason::aliased_cache);return;}
                        snapshot.pairs_.push_back({*rule,distance,height});
                    }
                }
                node=next;
            }
        }
        if(nodes.size()!=*node_count || std::none_of(handlers.begin(),handlers.end(),[&](const Handler& item){return item.address==snapshot.handler_;})) {snapshot.fail(Reason::invalid_graph);return;}
        if(snapshot.pairs_.empty()) {snapshot.fail(Reason::no_pairs);return;}
        const auto selected=[&](const Edge& edge){return std::any_of(snapshot.pairs_.begin(),snapshot.pairs_.end(),[&](const Pair& pair){return pair.distance==edge.condition || pair.height==edge.condition;});};
        for(const auto& edge:edges) {
            std::uint64_t references{};
            for(const auto& other:edges) {
                if(!snapshot.step())return;
                if(other.condition==edge.condition)++references;
                if(other.handler!=edge.handler)continue;
                if(overlap(static_cast<std::uintptr_t>(edge.result),8,static_cast<std::uintptr_t>(other.variable),8)) {snapshot.fail(Reason::aliased_cache);return;}
                if(overlap(static_cast<std::uintptr_t>(edge.result),8,static_cast<std::uintptr_t>(other.result),8) && edge.condition!=other.condition) {snapshot.fail(Reason::aliased_cache);return;}
                if(overlap(static_cast<std::uintptr_t>(edge.variable),8,static_cast<std::uintptr_t>(other.variable),8) &&
                    (edge.variable!=other.variable || edge.definition!=other.definition || (edge.type==1)!=(other.type==1) || (edge.type==0)!=(other.type==0))) {snapshot.fail(Reason::aliased_cache);return;}
            }
            if(references!=static_cast<std::uint64_t>(edge.references)+1) {snapshot.fail(Reason::reference_count);return;}
            const auto current=std::find_if(handlers.begin(),handlers.end(),[&](const Handler& item){return item.address==edge.handler;});
            if(current->time_offset!=0x80000000u && overlap(static_cast<std::uintptr_t>(edge.result),8,current->time_offset,8)) {snapshot.fail(Reason::aliased_cache);return;}
            if(current->time_offset!=0x80000000u && overlap(static_cast<std::uintptr_t>(edge.variable),8,current->time_offset,8)) {snapshot.fail(Reason::aliased_cache);return;}
            if(selected(edge)) {
                if(edge.handler!=snapshot.handler_ || std::none_of(snapshot.pairs_.begin(),snapshot.pairs_.end(),[&](const Pair& pair){return pair.rule==edge.rule && (pair.distance==edge.condition || pair.height==edge.condition);})) {snapshot.fail(Reason::nonpaired_use);return;}
            }
        }
        const auto active=std::find_if(handlers.begin(),handlers.end(),[&](const Handler& item){return item.address==snapshot.handler_;});
        const auto serial=reader.template value<std::uint32_t>(snapshot.handler_+0x40,false);if(!serial)return;snapshot.serial_=*serial;
        for(const auto& edge:edges)if(selected(edge)) {
            const auto tag=reader.template value<std::uint32_t>(active->cache+static_cast<std::uintptr_t>(edge.result),false);
            if(!tag)return;if(*tag==*serial) {snapshot.fail(Reason::warm_cache);return;}
        }
    }
    template<class Reader> static bool pair_accessor(Snapshot& snapshot,Reader& reader,std::uintptr_t accessor,std::uintptr_t definition,bool distance) {
        std::array<unsigned char,0x20> data{};
        return rig::pointer(accessor) && reader.read(accessor,data.data(),data.size()) &&
            field<std::uintptr_t>(data,0)==snapshot.base_+accessor_vtable &&
            reader.template value<std::uintptr_t>(snapshot.base_+accessor_vtable+8)==snapshot.base_+0x6cbea0 &&
            field<std::uintptr_t>(data,8)==definition && field<std::uintptr_t>(data,0x10)==snapshot.slot_ &&
            field<std::uintptr_t>(data,0x18)==snapshot.base_+(distance?0x9e9df0:0x9e9e30);
    }
};
}
