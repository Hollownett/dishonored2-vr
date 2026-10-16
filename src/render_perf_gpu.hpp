#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <mutex>
#include <ostream>
#include <atomic>

namespace d2vr {
class RenderPerfGpu {
public:
    enum class LossReason : unsigned {query_create_failed,query_read_failed,prepare_owner,begin_owner,end_owner,
        abort_owner,span_owner,mark_owner,execute_owner,poll_owner,clear_owner,active_frame_conflict,
        frame_capacity,span_capacity,mark_capacity,hud_capacity,hud_capture_quota,hud_bounds_quota,span_end_owner,
        operation_admission_busy,operation_owner_conflict,operation_context_mismatch,operation_release_rejected,count};
    static constexpr unsigned loss_reason_count=static_cast<unsigned>(LossReason::count);
private:
    template<class ValueType> using Ptr=Microsoft::WRL::ComPtr<ValueType>;
    static constexpr unsigned frame_count=8,span_count=128,mark_count=32,hud_count=2048,row_count=4096;
    struct Pair {Ptr<ID3D11Query> start,end;};
    struct Tag {unsigned category{},phase{};int eye=-1;std::uint64_t id{},ticket{};bool closed{};};
    struct Span {Pair query;Tag tag;};
    struct Mark {Ptr<ID3D11Query> query;Tag tag;};
    struct Frame {
        Ptr<ID3D11Query> clock,start,end;
        std::array<Span,span_count> spans;
        std::array<Mark,mark_count> marks;
        unsigned spans_used{},marks_used{};bool pending{},invalid{},retirement_reported{};
        std::uint64_t present{},epoch{},serial{};
    };
    struct Hud {
        Pair query;Tag tag;Ptr<ID3D11DeviceContext> recorder;std::uintptr_t list{};
        std::uint64_t frame_serial{},origin_present{},origin_epoch{};unsigned executions{};int next=-1,record_next=-1;bool published{},discarded{};
    };
    struct Row {std::uint64_t epoch{},frame{},id{};unsigned category{},phase{};int eye=-1;double ms{};
        std::uint64_t dropped{},replays{},failures{};};
    std::array<Frame,frame_count> frames_{};
    std::array<Hud,hud_count> hud_{};
    std::array<Row,row_count> rows_{};
    std::array<int,4096> list_heads_=[] {std::array<int,4096> values{};values.fill(-1);return values;}();
    std::array<int,4096> record_heads_=[] {std::array<int,4096> values{};values.fill(-1);return values;}();
    Ptr<ID3D11Device> device_;Ptr<ID3D11DeviceContext> immediate_;
    std::mutex mutex_;int active_=-1;unsigned hud_used_{},row_begin_{},rows_used_{};
    std::uint64_t serial_{},ticket_{},dropped_{},replays_{},failures_{};
    std::uint64_t seen_present_{},seen_epoch_{};bool frame_seen_{};
    unsigned hud_stride_=120;std::uint64_t sampled_record_frames_{},last_sampled_present_{};bool sampled_seen_{};
    unsigned last_hud_used_{};std::uint64_t last_sampled_record_frames_{},retired_frames_{},retired_hud_{},queued_rows_lost_{};
    unsigned unmapped_{};
    DWORD owner_thread_{};
    unsigned operation_depth_{};std::uintptr_t operation_context_identity_{};
    std::atomic<std::uint64_t> operation_admission_busy_{};
    std::array<std::uint64_t,loss_reason_count> loss_reasons_{};
    HRESULT last_query_create_hresult_=S_OK,last_query_read_hresult_=S_OK;
    DWORD last_rejected_thread_{};
    std::uintptr_t owner_context_identity_{},last_rejected_context_{};
    std::uint64_t hud_quota_frame_{},hud_quota_epoch_{};static constexpr unsigned hud_category_quota_=32;
    std::array<unsigned,2> hud_category_admitted_{};std::array<std::uint64_t,2> hud_category_eligible_{};bool hud_quota_seen_{};
    struct Readiness {
        std::uint64_t epoch{},present{},serial{},poll_present{},context{},ticket{},list{},origin_present{},origin_epoch{},observations{},coalesced{};
        std::array<std::uint32_t,8> results{};
        unsigned stage{},index{},reads{},spans{},marks{},attached{},flags{},executions{},visited{};
        std::uint32_t result{};bool valid{},terminal{};
    };
    std::array<Readiness,frame_count> readiness_cache_{};
    std::uint64_t readiness_last_present_{};bool readiness_emitted_{};
    static void readiness_read(Readiness& observation,unsigned stage,unsigned index,HRESULT result,const Hud* hud_record=nullptr) {
        ++observation.reads;observation.visited|=1u<<(stage-1);observation.results[stage-1]=static_cast<std::uint32_t>(result);
        if(result==S_OK || (observation.stage && (FAILED(static_cast<HRESULT>(observation.result)) || !FAILED(result)))) return;
        observation.stage=stage;observation.index=index;observation.result=static_cast<std::uint32_t>(result);
        observation.ticket=observation.list=observation.origin_present=observation.origin_epoch=0;observation.executions=0;
        observation.flags&=15u;
        if(hud_record) {
            observation.ticket=hud_record->tag.ticket;observation.list=hud_record->list;
            observation.origin_present=hud_record->origin_present;observation.origin_epoch=hud_record->origin_epoch;
            observation.executions=hud_record->executions;
            observation.flags|=16u|(hud_record->tag.closed?32u:0u)|(hud_record->published?64u:0u)|(hud_record->discarded?128u:0u);
        }
    }
    void cache_readiness(const Frame& frame_record,Readiness observation,bool terminal) {
        const auto index=static_cast<unsigned>(&frame_record-frames_.data());auto& cached=readiness_cache_[index];
        observation.valid=true;observation.terminal=terminal;
        observation.observations=cached.observations+1;
        if(cached.valid && cached.terminal) {++cached.coalesced;++cached.observations;return;}
        observation.coalesced=cached.coalesced;cached=observation;
    }
    void report_readiness(bool force=false) {
        if(!force && readiness_emitted_ && seen_present_>=readiness_last_present_ && seen_present_-readiness_last_present_<60) return;
        readiness_emitted_=true;readiness_last_present_=seen_present_;
        for(auto& observation:readiness_cache_) if(observation.valid) {
            const auto metadata=[&](unsigned category,unsigned phase,std::uint64_t value) {
                if(rows_used_==row_count) {++dropped_;++queued_rows_lost_;return;}
                rows_[(row_begin_+rows_used_++)%row_count]={observation.epoch,observation.present,value,category,phase,-1,0,dropped_,replays_,failures_};
            };
            const std::array<std::uint64_t,18> fields{1,observation.serial,observation.poll_present,
                observation.poll_present>=observation.present?observation.poll_present-observation.present:0,
                observation.context,observation.stage,observation.result,observation.index,observation.ticket,
                observation.spans,observation.marks,observation.attached,observation.flags|(observation.terminal?256u:0u),
                observation.reads,observation.coalesced,observation.observations,seen_present_,observation.visited};
            for(unsigned field=0;field<fields.size();++field) metadata(15,field,fields[field]);
            for(unsigned stage=0;stage<observation.results.size();++stage) metadata(16,stage+1,observation.results[stage]);
            if(observation.flags&16u) {
                const std::array<std::uint64_t,5> hud_fields{observation.list,observation.origin_present,observation.origin_epoch,observation.executions,observation.ticket};
                for(unsigned field=0;field<hud_fields.size();++field) metadata(17,field,hud_fields[field]);
            }
            unsigned attached_count{};const Hud* attached_record=nullptr;
            for(unsigned index=0;index<hud_used_;++index) if(hud_[index].frame_serial==observation.serial) {
                ++attached_count;if(!attached_record) attached_record=&hud_[index];
            }
            metadata(18,0,attached_count);
            if(attached_record) {
                const std::array<std::uint64_t,6> attached_fields{attached_record->tag.ticket,attached_record->list,
                    attached_record->origin_present,attached_record->origin_epoch,attached_record->executions,
                    (attached_record->tag.closed?1u:0u)|(attached_record->published?2u:0u)|(attached_record->discarded?4u:0u)};
                for(unsigned field=0;field<attached_fields.size();++field) metadata(18,field+1,attached_fields[field]);
            }
            observation.valid=false;observation.coalesced=observation.observations=0;
        }
    }
    void record_hud_admission() {
        if(!hud_quota_seen_) return;
        for(unsigned category_index=0;category_index<2;++category_index) {
            if(rows_used_==row_count) {++dropped_;++queued_rows_lost_;continue;}
            rows_[(row_begin_+rows_used_++)%row_count]={hud_quota_epoch_,hud_quota_frame_,hud_category_eligible_[category_index],14,
                category_index+1,-1,static_cast<double>(hud_category_admitted_[category_index]),dropped_,replays_,failures_};
        }
    }
    void reject_owner(LossReason reason,ID3D11DeviceContext* context=nullptr) {
        ++failures_;++loss_reasons_[static_cast<unsigned>(reason)];last_rejected_thread_=GetCurrentThreadId();
        last_rejected_context_=reinterpret_cast<std::uintptr_t>(context);
    }
    bool owner(ID3D11DeviceContext* context) const {
        return context && context==immediate_.Get() && operation_depth_ &&
            reinterpret_cast<std::uintptr_t>(context)==operation_context_identity_ && GetCurrentThreadId()==owner_thread_;
    }
    bool same_device(ID3D11DeviceContext* context) const {
        if(!context || !device_) return false;Ptr<ID3D11Device> device;context->GetDevice(&device);return device.Get()==device_.Get();
    }
    template<class ValueType> static unsigned bucket(ValueType* pointer) {
        const auto address=reinterpret_cast<std::uintptr_t>(pointer);return static_cast<unsigned>((address>>4)^(address>>16))&4095u;
    }
    bool query(Ptr<ID3D11Query>& query_handle,D3D11_QUERY kind) {
        D3D11_QUERY_DESC desc{kind,0};const auto query_result=device_->CreateQuery(&desc,&query_handle);
        if(FAILED(query_result)) {++loss_reasons_[static_cast<unsigned>(LossReason::query_create_failed)];last_query_create_hresult_=query_result;}
        return SUCCEEDED(query_result);
    }
    bool pair(Pair& query_pair) {return query(query_pair.start,D3D11_QUERY_TIMESTAMP) && query(query_pair.end,D3D11_QUERY_TIMESTAMP);}
    template<class ValueType> HRESULT read(ID3D11DeviceContext* context,ID3D11Query* query_handle,ValueType& value) {
        const auto hr=context->GetData(query_handle,&value,sizeof value,D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(FAILED(hr)) {++failures_;++loss_reasons_[static_cast<unsigned>(LossReason::query_read_failed)];last_query_read_hresult_=hr;}return hr;
    }
    void emit(const Frame& frame_record,const Tag& tag_record,double ms) {
        if(rows_used_==row_count) {++dropped_;++queued_rows_lost_;return;}
        rows_[(row_begin_+rows_used_++)%row_count]={frame_record.epoch,frame_record.present,tag_record.id,tag_record.category,tag_record.phase,tag_record.eye,ms,dropped_,replays_,failures_};
    }
    bool inside(std::uint64_t start_timestamp,std::uint64_t end_timestamp,std::uint64_t first,std::uint64_t last) {
        return first<=start_timestamp && start_timestamp<=end_timestamp && end_timestamp<=last;
    }
    void release_frame(Frame& frame_record) {
        for(unsigned index=0;index<hud_used_;++index) if(hud_[index].frame_serial==frame_record.serial && !hud_[index].published) hud_[index].discarded=true;
        frame_record.pending=false;frame_record.invalid=false;frame_record.spans_used=frame_record.marks_used=0;
    }
    void clear_locked() {
        report_readiness(true);readiness_emitted_=false;readiness_last_present_=0;
        record_hud_admission();
        const auto record=[&](std::uint64_t epoch,std::uint64_t frame,std::uint64_t id,unsigned category,unsigned phase,double count) {
            if(rows_used_==row_count) {++dropped_;++queued_rows_lost_;return;}
            rows_[(row_begin_+rows_used_++)%row_count]={epoch,frame,id,category,phase,-1,count,dropped_,replays_,failures_};
        };
        if(device_) {
            last_hud_used_=hud_used_;last_sampled_record_frames_=sampled_record_frames_;
            record(0,sampled_record_frames_,hud_used_,7,hud_stride_,hud_count);
            for(unsigned index=0;index<frame_count;++index) {const auto& frame_record=frames_[index];
                if((frame_record.pending || active_==static_cast<int>(index)) && !frame_record.retirement_reported) {
                    ++retired_frames_;++dropped_;record(frame_record.epoch,frame_record.present,frame_record.serial,8,0,1);
                }
            }
            for(unsigned index=0;index<hud_used_;++index) {const auto& hud_record=hud_[index];if(!hud_record.published) {
                ++retired_hud_;++dropped_;record(hud_record.origin_epoch,hud_record.origin_present,hud_record.tag.ticket,8,1,1);
            }}
        }
        if(active_>=0 && immediate_) {
            auto& frame_record=frames_[static_cast<unsigned>(active_)];immediate_->End(frame_record.end.Get());immediate_->End(frame_record.clock.Get());
        }
        for(auto& frame_record:frames_) frame_record=Frame{};
        for(auto& hud_record:hud_) hud_record=Hud{};
        immediate_.Reset();device_.Reset();active_=-1;hud_used_=unmapped_=0;frame_seen_=false;list_heads_.fill(-1);record_heads_.fill(-1);
        sampled_record_frames_=0;sampled_seen_=false;
        hud_quota_seen_=false;hud_category_admitted_.fill(0);hud_category_eligible_.fill(0);
    }
public:
    struct Stats {unsigned hud_used{},hud_capacity{},hud_stride{};std::uint64_t sampled_record_frames{},dropped{},replays{},failures{},retired_frames{},retired_hud{},queued_rows_lost{};
        std::array<std::uint64_t,loss_reason_count> loss_reasons{};HRESULT last_query_create_hresult{},last_query_read_hresult{};DWORD owner_thread{},last_rejected_thread{};
        std::uintptr_t owner_context_identity{},last_rejected_context{};unsigned hud_per_category_quota=32;bool hud_category_samples_partial=true;};
    Stats stats() {std::lock_guard<std::mutex> lock(mutex_);
        auto reasons=loss_reasons_;reasons[static_cast<unsigned>(LossReason::operation_admission_busy)]=operation_admission_busy_.load(std::memory_order_relaxed);
        return {device_?hud_used_:last_hud_used_,hud_count,hud_stride_,device_?sampled_record_frames_:last_sampled_record_frames_,
                dropped_,replays_,failures_,retired_frames_,retired_hud_,queued_rows_lost_,reasons,last_query_create_hresult_,last_query_read_hresult_,owner_thread_,last_rejected_thread_,owner_context_identity_,last_rejected_context_,hud_category_quota_,true};}
    bool begin_operation(ID3D11DeviceContext* context) {
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()) {operation_admission_busy_.fetch_add(1,std::memory_order_relaxed);return false;}
        if(!context || context!=immediate_.Get() || !device_) {reject_owner(LossReason::operation_context_mismatch,context);return false;}
        if(operation_depth_ && (GetCurrentThreadId()!=owner_thread_ || reinterpret_cast<std::uintptr_t>(context)!=operation_context_identity_)) {
            reject_owner(LossReason::operation_owner_conflict,context);return false;
        }
        owner_thread_=GetCurrentThreadId();operation_context_identity_=reinterpret_cast<std::uintptr_t>(context);++operation_depth_;return true;
    }
    void end_operation(ID3D11DeviceContext* context) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!operation_depth_ || GetCurrentThreadId()!=owner_thread_ || reinterpret_cast<std::uintptr_t>(context)!=operation_context_identity_) {
            reject_owner(LossReason::operation_release_rejected,context);return;
        }
        if(!--operation_depth_) operation_context_identity_=0;
    }
    bool active_frame(ID3D11DeviceContext* context,std::uint64_t& present,std::uint64_t& epoch) {
        std::lock_guard<std::mutex> lock(mutex_);if(!owner(context) || active_<0) return false;
        const auto& frame_record=frames_[static_cast<unsigned>(active_)];present=frame_record.present;epoch=frame_record.epoch;return true;
    }
    void set_hud_stride(unsigned stride) {std::lock_guard<std::mutex> lock(mutex_);
        if(!stride) {++failures_;return;}hud_stride_=stride;}
    bool prepare(ID3D11Device* device) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(operation_depth_ && GetCurrentThreadId()!=owner_thread_) {reject_owner(LossReason::prepare_owner);return false;}
        if(device && device_.Get()==device) return true;
        if(device_ && !owner(immediate_.Get())) {reject_owner(LossReason::prepare_owner);return false;}
        clear_locked();if(!device) {++failures_;return false;}device_=device;owner_thread_=GetCurrentThreadId();device->GetImmediateContext(&immediate_);
        owner_context_identity_=reinterpret_cast<std::uintptr_t>(immediate_.Get());
        bool ok=immediate_.Get()!=nullptr;
        for(auto& frame_record:frames_) {
            ok=ok && query(frame_record.clock,D3D11_QUERY_TIMESTAMP_DISJOINT) && query(frame_record.start,D3D11_QUERY_TIMESTAMP) && query(frame_record.end,D3D11_QUERY_TIMESTAMP);
            for(auto& span_record:frame_record.spans) if(ok) ok=pair(span_record.query);
            for(auto& mark_record:frame_record.marks) if(ok) ok=query(mark_record.query,D3D11_QUERY_TIMESTAMP);
        }
        for(auto& hud_record:hud_) if(ok) ok=pair(hud_record.query);
        if(!ok) {++failures_;clear_locked();}return ok;
    }
    bool begin_frame(ID3D11DeviceContext* context,std::uint64_t present,std::uint64_t epoch) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!device_) return false;
        if(!owner(context)) {reject_owner(LossReason::begin_owner,context);return false;}
        if(active_>=0) {auto& active=frames_[static_cast<unsigned>(active_)];
            if(active.present==present && active.epoch==epoch) return true;
            ++failures_;++loss_reasons_[static_cast<unsigned>(LossReason::active_frame_conflict)];
            active.invalid=true;active.pending=true;active.retirement_reported=true;
            context->End(active.end.Get());context->End(active.clock.Get());++retired_frames_;++dropped_;
            emit(active,Tag{8,0,-1,active.serial,0,true},1);active_=-1;
        }
        if(!frame_seen_ || present>=seen_present_) {seen_present_=present;seen_epoch_=epoch;}frame_seen_=true;
        for(unsigned index=0;index<frame_count;++index) if(!frames_[index].pending) {
            auto& frame_record=frames_[index];frame_record.present=present;frame_record.epoch=epoch;frame_record.serial=++serial_;frame_record.spans_used=frame_record.marks_used=0;frame_record.invalid=false;frame_record.retirement_reported=false;
            active_=static_cast<int>(index);context->Begin(frame_record.clock.Get());context->End(frame_record.start.Get());return true;
        }
        ++dropped_;++loss_reasons_[static_cast<unsigned>(LossReason::frame_capacity)];
        return false;
    }
    void set_recording_frame(std::uint64_t present,std::uint64_t epoch=0) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(hud_quota_seen_ && present>hud_quota_frame_) {record_hud_admission();hud_quota_seen_=false;}
        if(!frame_seen_ || present>=seen_present_) {seen_present_=present;if(epoch) seen_epoch_=epoch;}frame_seen_=true;
    }
    void end_frame(ID3D11DeviceContext* context) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!device_) return;if(!owner(context)) {reject_owner(LossReason::end_owner,context);return;}if(active_<0) return;
        auto& frame_record=frames_[static_cast<unsigned>(active_)];
        for(unsigned index=0;index<frame_record.spans_used;++index) if(!frame_record.spans[index].tag.closed) {frame_record.invalid=true;++failures_;}
        context->End(frame_record.end.Get());context->End(frame_record.clock.Get());frame_record.pending=true;active_=-1;
    }
    void abort_frame(ID3D11DeviceContext* context) {
        mutex_.lock();
        __try {abort_frame_locked(context);}
        __finally {if(AbnormalTermination())maintenance_fault_locked();mutex_.unlock();}
    }
private:
    void abort_frame_locked(ID3D11DeviceContext* context) {
        if(!device_) return;if(!owner(context)) {reject_owner(LossReason::abort_owner,context);return;}if(active_<0) return;
        auto& frame_record=frames_[static_cast<unsigned>(active_)];frame_record.invalid=true;
        context->End(frame_record.end.Get());context->End(frame_record.clock.Get());frame_record.pending=true;active_=-1;
    }
public:
    std::uint64_t begin_span(ID3D11DeviceContext* context,unsigned category,int eye=-1,std::uint64_t id=0) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!device_) return 0;
        if(!same_device(context) || (category>3 && category!=12)) {++failures_;return 0;}
        if(ticket_>=((~std::uint64_t{0})>>12)) {++dropped_;return 0;}
        Tag tag{category,0,eye,id,(++ticket_)<<12,false};
        if(context->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED) {
            if(category!=1 && category!=2) {++failures_;return 0;}
            if(!frame_seen_ || seen_present_%hud_stride_!=0) return 0;
            if(!hud_quota_seen_ || hud_quota_frame_!=seen_present_) {
                record_hud_admission();hud_quota_frame_=seen_present_;hud_quota_epoch_=seen_epoch_;hud_quota_seen_=true;
                hud_category_admitted_.fill(0);hud_category_eligible_.fill(0);
            }
            const auto category_index=category-1;
            ++hud_category_eligible_[category_index];
            if(hud_category_admitted_[category_index]>=hud_category_quota_) {
                ++loss_reasons_[static_cast<unsigned>(category==1?LossReason::hud_capture_quota:LossReason::hud_bounds_quota)];return 0;
            }
            if(hud_used_==hud_count) {++dropped_;++loss_reasons_[static_cast<unsigned>(LossReason::hud_capacity)];return 0;}
            ++hud_category_admitted_[category_index];
            if(!sampled_seen_ || last_sampled_present_!=seen_present_) {
                ++sampled_record_frames_;last_sampled_present_=seen_present_;sampled_seen_=true;
            }
            const auto index=hud_used_++;tag.ticket|=index+1;
            auto& hud_record=hud_[index];++unmapped_;hud_record.tag=tag;hud_record.recorder=context;
            const auto hash=bucket(context);hud_record.record_next=record_heads_[hash];record_heads_[hash]=static_cast<int>(index);
            context->End(hud_record.query.start.Get());return tag.ticket;
        }
        if(!owner(context)) {reject_owner(LossReason::span_owner,context);return 0;}
        if(active_<0) {++dropped_;return 0;}
        auto& frame_record=frames_[static_cast<unsigned>(active_)];
        if(frame_record.spans_used==span_count) {++dropped_;++loss_reasons_[static_cast<unsigned>(LossReason::span_capacity)];return 0;}
        if(category==1 || category==2)tag.id=tag.ticket;
        auto& span_record=frame_record.spans[frame_record.spans_used++];span_record.tag=tag;context->End(span_record.query.start.Get());return tag.ticket;
    }
    void end_span(ID3D11DeviceContext* context,std::uint64_t ticket) {
        if(!ticket) return;std::lock_guard<std::mutex> lock(mutex_);
        if(!(ticket&4095u) && context==immediate_.Get() && !owner(context)) {reject_owner(LossReason::span_end_owner,context);return;}
        if(owner(context) && active_>=0) {auto& frame_record=frames_[static_cast<unsigned>(active_)];
            for(unsigned index=0;index<frame_record.spans_used;++index) {auto& span_record=frame_record.spans[index];if(span_record.tag.ticket==ticket && !span_record.tag.closed) {
                context->End(span_record.query.end.Get());span_record.tag.closed=true;return;
            }}
        }
        const auto encoded=static_cast<unsigned>(ticket&4095u);
        if(encoded && encoded<=hud_used_) {auto& hud_record=hud_[encoded-1];
            if(hud_record.tag.ticket==ticket && hud_record.recorder.Get()==context && !hud_record.tag.closed && !hud_record.list) {
                context->End(hud_record.query.end.Get());hud_record.tag.closed=true;return;
            }
        }
        ++failures_;
    }
    void mark(ID3D11DeviceContext* context,unsigned category,int eye,std::uint64_t id,unsigned phase) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!device_) return;if(!owner(context)) {reject_owner(LossReason::mark_owner,context);return;}
        if(category>3) {++failures_;return;}if(active_<0) {++dropped_;return;}
        auto& frame_record=frames_[static_cast<unsigned>(active_)];if(frame_record.marks_used==mark_count) {++dropped_;++loss_reasons_[static_cast<unsigned>(LossReason::mark_capacity)];return;}
        auto& mark_record=frame_record.marks[frame_record.marks_used++];mark_record.tag={category,phase,eye,id,0,true};context->End(mark_record.query.Get());
    }
    void finished(ID3D11DeviceContext* context,ID3D11CommandList* list) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!device_) return;if(!same_device(context) || !list || context->GetType()!=D3D11_DEVICE_CONTEXT_DEFERRED) {++failures_;return;}
        const auto identity=reinterpret_cast<std::uintptr_t>(list);const auto hash=bucket(list);
        int* old=&list_heads_[hash];while(*old>=0) {
            auto& hud_record=hud_[static_cast<unsigned>(*old)];
            if(hud_record.list!=identity) {old=&hud_record.next;continue;}
            *old=hud_record.next;hud_record.next=-1;hud_record.list=0;
        }
        if(!unmapped_) return;
        Ptr<ID3D11Device> device;list->GetDevice(&device);if(device.Get()!=device_.Get()) {++failures_;return;}
        int* link=&record_heads_[bucket(context)];while(*link>=0) {
            const auto index=static_cast<unsigned>(*link);auto& hud_record=hud_[index];
            if(hud_record.recorder.Get()!=context) {link=&hud_record.record_next;continue;}
            *link=hud_record.record_next;
            --unmapped_;
            if(!hud_record.tag.closed) {hud_record.discarded=true;++failures_;}else {
                hud_record.list=identity;hud_record.next=list_heads_[hash];list_heads_[hash]=static_cast<int>(index);
            }
            hud_record.recorder.Reset();
        }
    }
    void executing(ID3D11DeviceContext* context,ID3D11CommandList* list) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!device_) return;if(!owner(context)) {reject_owner(LossReason::execute_owner,context);return;}
        if(!list) {++failures_;return;}
        for(int index=list_heads_[bucket(list)];index>=0;index=hud_[static_cast<unsigned>(index)].next) {
            auto& hud_record=hud_[static_cast<unsigned>(index)];if(hud_record.list!=reinterpret_cast<std::uintptr_t>(list)) continue;
            if(++hud_record.executions>1) {hud_record.discarded=true;++replays_;
                if(rows_used_==row_count) {++dropped_;++queued_rows_lost_;}
                else rows_[(row_begin_+rows_used_++)%row_count]={hud_record.origin_epoch,hud_record.origin_present,hud_record.tag.ticket,6,hud_record.tag.category,hud_record.tag.eye,0,dropped_,replays_,failures_};
                continue;
            }
            if(active_<0) {hud_record.discarded=true;++dropped_;continue;}
            const auto& frame_record=frames_[static_cast<unsigned>(active_)];hud_record.frame_serial=frame_record.serial;hud_record.origin_present=frame_record.present;hud_record.origin_epoch=frame_record.epoch;
        }
    }
    void poll(ID3D11DeviceContext* context) {
        mutex_.lock();
        __try {poll_locked(context);}
        __finally {if(AbnormalTermination())maintenance_fault_locked();mutex_.unlock();}
    }
private:
    void poll_locked(ID3D11DeviceContext* context) {
        if(!owner(context)) {reject_owner(LossReason::poll_owner,context);return;}
        for(auto& frame_record:frames_) if(frame_record.pending) {
            Readiness observation{};observation.results.fill(0xffffffffu);
            observation.epoch=frame_record.epoch;observation.present=frame_record.present;observation.serial=frame_record.serial;
            observation.poll_present=seen_present_;observation.context=reinterpret_cast<std::uintptr_t>(context);
            observation.spans=frame_record.spans_used;observation.marks=frame_record.marks_used;
            observation.flags=(frame_record.pending?1u:0u)|(frame_record.invalid?2u:0u);
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock{};std::uint64_t first{},last{};
            const auto hc=read(context,frame_record.clock.Get(),clock),hs=read(context,frame_record.start.Get(),first),he=read(context,frame_record.end.Get(),last);
            readiness_read(observation,1,0,hc);readiness_read(observation,2,0,hs);readiness_read(observation,3,0,he);
            if(FAILED(hc) || FAILED(hs) || FAILED(he)) {cache_readiness(frame_record,observation,true);release_frame(frame_record);continue;}
            if(hc!=S_OK || hs!=S_OK || he!=S_OK) {cache_readiness(frame_record,observation,false);continue;}
            if(frame_record.invalid || clock.Disjoint || !clock.Frequency || last<first) {
                observation.stage=9;observation.flags|=(clock.Disjoint?4u:0u)|(!clock.Frequency?8u:0u)|(last<first?512u:0u);
                ++failures_;cache_readiness(frame_record,observation,true);release_frame(frame_record);continue;
            }
            bool ready=true,bad=false;
            const auto ready_pair=[&](const Pair& query_pair,unsigned stage,unsigned index,const Hud* hud_record=nullptr) {std::uint64_t start_timestamp{},end_timestamp{};
                const auto ha=read(context,query_pair.start.Get(),start_timestamp),hb=read(context,query_pair.end.Get(),end_timestamp);
                readiness_read(observation,stage,index,ha,hud_record);readiness_read(observation,stage+1,index,hb,hud_record);
                bad=bad || FAILED(ha) || FAILED(hb);return ha==S_OK && hb==S_OK;};
            for(unsigned index=0;index<frame_record.spans_used;++index) if(!ready_pair(frame_record.spans[index].query,4,index)) ready=false;
            for(unsigned index=0;index<frame_record.marks_used;++index) {std::uint64_t value{};const auto hr=read(context,frame_record.marks[index].query.Get(),value);
                readiness_read(observation,6,index,hr);bad=bad || FAILED(hr);if(hr!=S_OK) ready=false;}
            for(unsigned index=0;index<hud_used_;++index) {const auto& hud_record=hud_[index];
                if(hud_record.frame_serial==frame_record.serial && !hud_record.discarded && !hud_record.published) {
                    ++observation.attached;if(!ready_pair(hud_record.query,7,index,&hud_record)) ready=false;
                }}
            cache_readiness(frame_record,observation,bad);
            if(bad) {release_frame(frame_record);continue;}if(!ready) continue;
            const double scale=1000.0/static_cast<double>(clock.Frequency);
            emit(frame_record,Tag{5,0,-1,0,0,true},static_cast<double>(last-first)*scale);
            const auto publish=[&](const Pair& query_pair,const Tag& tag_record) {std::uint64_t start_timestamp{},end_timestamp{};
                if(read(context,query_pair.start.Get(),start_timestamp)==S_OK && read(context,query_pair.end.Get(),end_timestamp)==S_OK && inside(start_timestamp,end_timestamp,first,last)) emit(frame_record,tag_record,static_cast<double>(end_timestamp-start_timestamp)*scale);
                else ++failures_;};
            for(unsigned index=0;index<frame_record.spans_used;++index) publish(frame_record.spans[index].query,frame_record.spans[index].tag);
            for(unsigned index=0;index<frame_record.marks_used;++index) {auto& mark_record=frame_record.marks[index];std::uint64_t value{};
                if(read(context,mark_record.query.Get(),value)==S_OK && inside(value,value,first,last)) emit(frame_record,mark_record.tag,static_cast<double>(value-first)*scale);else ++failures_;}
            for(unsigned index=0;index<hud_used_;++index) {auto& hud_record=hud_[index];if(hud_record.frame_serial==frame_record.serial && !hud_record.discarded && !hud_record.published) {
                auto unique=hud_record.tag;unique.id=hud_record.tag.ticket;publish(hud_record.query,unique);hud_record.published=true;
            }}
            release_frame(frame_record);
        }
        report_readiness();
    }
public:
    void drain(std::ostream& out,bool final=false) {
        if(final) {std::lock_guard<std::mutex> lock(mutex_);report_readiness(true);}
        out.precision(17);
        for(;;) {Row row;{
            std::lock_guard<std::mutex> lock(mutex_);if(!rows_used_) break;
            row=rows_[row_begin_];row_begin_=(row_begin_+1)%row_count;--rows_used_;
        }
            out<<row.epoch<<','<<row.frame<<','<<row.category<<','<<row.eye<<','<<row.id<<','<<row.phase<<','<<row.ms
               <<','<<row.dropped<<','<<row.replays<<','<<row.failures<<'\n';
        }
        std::array<std::uint64_t,loss_reason_count> loss_snapshot{};std::array<std::uint64_t,6> identity_snapshot{};
        std::array<std::uint64_t,2> eligible_snapshot{};std::array<unsigned,2> admitted_snapshot{};
        std::uint64_t admission_epoch{},admission_frame{};unsigned quota_snapshot{};bool admission_present{};
        std::uint64_t dropped{},replays{},failures{},sampled_frames{},retired_frames{},retired_hud{},queued_lost{};unsigned hud_used{},stride{};{
            std::lock_guard<std::mutex> lock(mutex_);dropped=dropped_;replays=replays_;failures=failures_;
            hud_used=device_?hud_used_:last_hud_used_;stride=hud_stride_;sampled_frames=device_?sampled_record_frames_:last_sampled_record_frames_;
            retired_frames=retired_frames_;retired_hud=retired_hud_;queued_lost=queued_rows_lost_;
            loss_snapshot=loss_reasons_;
            loss_snapshot[static_cast<unsigned>(LossReason::operation_admission_busy)]=operation_admission_busy_.load(std::memory_order_relaxed);
            identity_snapshot={static_cast<std::uint32_t>(last_query_create_hresult_),static_cast<std::uint32_t>(last_query_read_hresult_),
                owner_thread_,last_rejected_thread_,owner_context_identity_,last_rejected_context_};
            eligible_snapshot=hud_category_eligible_;admitted_snapshot=hud_category_admitted_;quota_snapshot=hud_category_quota_;
            admission_epoch=hud_quota_epoch_;admission_frame=hud_quota_frame_;admission_present=hud_quota_seen_;
        }
        out<<0<<','<<0<<','<<4<<','<<-1<<','<<0<<','<<0<<','<<0<<','<<dropped<<','<<replays<<','<<failures<<'\n';
        out<<0<<','<<sampled_frames<<','<<7<<','<<-1<<','<<hud_used<<','<<stride<<','<<hud_count<<','<<dropped<<','<<replays<<','<<failures<<'\n';
        out<<0<<','<<0<<','<<8<<','<<-1<<','<<0<<','<<2<<','<<retired_frames<<','<<dropped<<','<<replays<<','<<failures<<'\n';
        out<<0<<','<<0<<','<<8<<','<<-1<<','<<0<<','<<3<<','<<retired_hud<<','<<dropped<<','<<replays<<','<<failures<<'\n';
        out<<0<<','<<0<<','<<8<<','<<-1<<','<<0<<','<<4<<','<<queued_lost<<','<<dropped<<','<<replays<<','<<failures<<'\n';
        for(unsigned reason_index=0;reason_index<loss_reason_count;++reason_index)
            out<<0<<','<<0<<','<<9<<','<<-1<<','<<reason_index<<','<<0<<','<<loss_snapshot[reason_index]<<','<<dropped<<','<<replays<<','<<failures<<'\n';
        for(unsigned identity_index=0;identity_index<identity_snapshot.size();++identity_index)
            out<<0<<','<<0<<','<<10<<','<<-1<<','<<identity_snapshot[identity_index]<<','<<identity_index<<','<<0<<','<<dropped<<','<<replays<<','<<failures<<'\n';
        out<<0<<','<<0<<','<<11<<','<<-1<<','<<quota_snapshot<<','<<1<<','<<1<<','<<dropped<<','<<replays<<','<<failures<<'\n';
        out<<0<<','<<0<<','<<11<<','<<-1<<','<<quota_snapshot<<','<<2<<','<<1<<','<<dropped<<','<<replays<<','<<failures<<'\n';
        if(admission_present) for(unsigned category_index=0;category_index<2;++category_index)
            out<<admission_epoch<<','<<admission_frame<<','<<14<<','<<-1<<','<<eligible_snapshot[category_index]<<','<<category_index+1<<','
               <<admitted_snapshot[category_index]<<','<<dropped<<','<<replays<<','<<failures<<'\n';
    }
    void emit(std::uint64_t epoch,std::uint64_t frame,unsigned category,int eye,
              std::uint64_t id,unsigned phase,double ms) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if(rows_used_==row_count) {++dropped_;++queued_rows_lost_;return;}
        rows_[(row_begin_+rows_used_++)%row_count]={epoch,frame,id,category,phase,eye,ms,dropped_,replays_,failures_};
    }
    void observe(std::uint64_t epoch,std::uint64_t frame,unsigned category,int eye,
                 std::uint64_t id,unsigned phase,double ms) noexcept {emit(epoch,frame,category,eye,id,phase,ms);}
    void clear() {
        mutex_.lock();
        __try {clear_owned_locked();}
        __finally {if(AbnormalTermination())maintenance_fault_locked();mutex_.unlock();}
    }
private:
    void clear_owned_locked() {
        if(device_ && !owner(immediate_.Get())) {reject_owner(LossReason::clear_owner);return;}clear_locked();
    }
    void maintenance_fault_locked() noexcept {
        ++failures_;queued_rows_lost_+=rows_used_;dropped_+=rows_used_;rows_used_=0;
        for(auto& frame_record:frames_)frame_record.invalid=true;
    }
};
}
