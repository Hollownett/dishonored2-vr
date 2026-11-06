#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include "frame_split.hpp"

namespace d2vr {
// Optional diagnostic lane, called only by the game's immediate-context owner.
// One disjoint bracket per frame. Timestamp pairs cover original native lists;
// FSR/XR work between those lists is excluded from the list sum, not elapsed span.
class NativeListGpuTimer {
public:
    static constexpr unsigned max_lists=128;
    // v999 pass tags.  Named render passes are 0..render_pass_count-1; 63 is "no list->pass mapping".
    // 52..62 stay unused so a truncated pass id cannot alias a real pass.
    static constexpr unsigned max_pass=64;
    static constexpr std::uint8_t untagged_pass=63;
    // Per completed timer frame, summed by (pass, phase).  This is the raw input of GpuPassWindow;
    // ms/lists are frame totals, not percentiles.
    struct PassFrame {
        double ms[max_pass][phase_count]{};
        unsigned lists[max_pass][phase_count]{};
        bool valid{};
    };
private:
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Pair {Ptr<ID3D11Query> start,end;};
    struct Frame {
        Ptr<ID3D11Query> disjoint,span_end;
        std::array<Pair,max_lists> lists;
        std::array<std::uint8_t,max_lists> phase{};
        std::array<std::uint8_t,max_lists> pass{};
        unsigned count{},dropped{};
        bool pending{},invalid{};
        std::uint64_t sequence{};
    };
    std::array<Frame,8> frames_{};
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11DeviceContext> context_;
    int active_=-1;
    std::uint64_t next_ticket_{},open_ticket_{},sequence_{};
    bool suppressed_{};
public:
    using Ticket=std::uint64_t; // zero means no sample; tickets survive clear/reprepare without aliasing
    struct Stats {
        std::uint64_t frames{},lists{},recorded_lists{},dropped_frames{},dropped_lists{},
            unready_polls{},disjoint{},query_failures{},rejected_calls{},last_sequence{};
        unsigned last_lists{},last_dropped_lists{};
        PhaseGpu last_phase{}; // latest completed timer frame, summed by the phase each list was executed in
        PassFrame last_pass{}; // latest completed timer frame, summed by the pass tag each list carried
        double last_list_sum_ms{},last_elapsed_ms{},total_list_sum_ms{},total_elapsed_ms{};
    };
private:
    Stats stats_{};
    bool owns(ID3D11DeviceContext* context) const noexcept {
        return context && context==context_.Get();
    }
    // S_FALSE is expected asynchronous incompletion, never an invitation to wait/flush.
    bool read(ID3D11DeviceContext* context,ID3D11Query* query,void* data,UINT bytes,bool& failed) {
        const HRESULT result=context->GetData(query,data,bytes,D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if(FAILED(result)) {failed=true;++stats_.query_failures;}
        return result==S_OK;
    }
public:
    bool prepare(ID3D11Device* device) {
        if(!device) {++stats_.rejected_calls;return false;}
        if(device_.Get()==device) return true;
        clear();
        for(auto& frame:frames_) {
            D3D11_QUERY_DESC q{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
            bool ok=SUCCEEDED(device->CreateQuery(&q,&frame.disjoint));
            q.Query=D3D11_QUERY_TIMESTAMP;
            ok=ok && SUCCEEDED(device->CreateQuery(&q,&frame.span_end));
            for(auto& pair:frame.lists) {
                if(!ok) break;
                ok=SUCCEEDED(device->CreateQuery(&q,&pair.start)) && SUCCEEDED(device->CreateQuery(&q,&pair.end));
            }
            if(!ok) {clear();++stats_.query_failures;return false;}
        }
        device_=device;device->GetImmediateContext(&context_);
        if(!context_) {clear();++stats_.query_failures;return false;}
        return true;
    }
    void poll(ID3D11DeviceContext* context) {
        if(!owns(context)) {++stats_.rejected_calls;return;}
        for(auto& frame:frames_) if(frame.pending) {
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock{};bool failed=false;
            if(!read(context,frame.disjoint.Get(),&clock,sizeof clock,failed)) {
                if(failed) {frame.pending=false;++stats_.dropped_frames;} else ++stats_.unready_polls;
                continue;
            }
            if(clock.Disjoint || !clock.Frequency) {frame.pending=false;++stats_.disjoint;continue;}
            if(frame.invalid) {frame.pending=false;continue;}
            UINT64 first{},span_end{},sum{};bool complete=true;UINT64 phase_sum[phase_count]{};unsigned phase_lists[phase_count]{};
            UINT64 pass_sum[max_pass][phase_count]{};unsigned pass_lists[max_pass][phase_count]{};
            for(unsigned i=0;i<frame.count;++i) {
                UINT64 start{},end{};
                if(!read(context,frame.lists[i].start.Get(),&start,sizeof start,failed) ||
                   !read(context,frame.lists[i].end.Get(),&end,sizeof end,failed)) {complete=false;break;}
                if(end<start) {failed=true;++stats_.query_failures;complete=false;break;}
                if(!i) first=start;
                sum+=end-start;
                const unsigned phase=frame.phase[i]%phase_count;
                const unsigned pass=frame.pass[i]%max_pass;
                phase_sum[phase]+=end-start;++phase_lists[phase];
                pass_sum[pass][phase]+=end-start;++pass_lists[pass][phase];
            }
            if(complete) complete=read(context,frame.span_end.Get(),&span_end,sizeof span_end,failed);
            if(!complete) {
                if(failed) {frame.pending=false;++stats_.dropped_frames;} else ++stats_.unready_polls;
                continue;
            }
            frame.pending=false;
            if(span_end<first || sum>span_end-first) {++stats_.query_failures;++stats_.dropped_frames;continue;}
            const double scale=1000.0/static_cast<double>(clock.Frequency);
            const double list_ms=static_cast<double>(sum)*scale,elapsed_ms=static_cast<double>(span_end-first)*scale;
            ++stats_.frames;stats_.lists+=frame.count;stats_.total_list_sum_ms+=list_ms;stats_.total_elapsed_ms+=elapsed_ms;
            if(frame.sequence>stats_.last_sequence) {
                stats_.last_sequence=frame.sequence;stats_.last_lists=frame.count;stats_.last_dropped_lists=frame.dropped;
                stats_.last_list_sum_ms=list_ms;stats_.last_elapsed_ms=elapsed_ms;
                stats_.last_phase={};stats_.last_phase.valid=true;
                for(unsigned p=0;p<phase_count;++p) {stats_.last_phase.ms[p]=static_cast<double>(phase_sum[p])*scale;stats_.last_phase.lists[p]=phase_lists[p];}
                stats_.last_pass={};stats_.last_pass.valid=true;
                for(unsigned p=0;p<max_pass;++p) for(unsigned phase=0;phase<phase_count;++phase) {
                    stats_.last_pass.ms[p][phase]=static_cast<double>(pass_sum[p][phase])*scale;
                    stats_.last_pass.lists[p][phase]=pass_lists[p][phase];
                }
            }
        }
    }
    Ticket begin_list(ID3D11DeviceContext* context,Phase phase=Phase::pre,std::uint8_t pass=untagged_pass) {
        if(!owns(context)) {++stats_.rejected_calls;return 0;}
        if(suppressed_) {++stats_.dropped_lists;return 0;}
        if(open_ticket_) {++stats_.dropped_lists;++frames_[static_cast<unsigned>(active_)].dropped;return 0;}
        if(active_<0) {
            poll(context);
            for(unsigned i=0;i<frames_.size();++i) if(!frames_[i].pending) {active_=static_cast<int>(i);break;}
            if(active_<0) {suppressed_=true;++stats_.dropped_frames;++stats_.dropped_lists;return 0;}
            auto& frame=frames_[static_cast<unsigned>(active_)];
            frame.count=frame.dropped=0;frame.invalid=false;frame.sequence=++sequence_;
            context->Begin(frame.disjoint.Get());
        }
        auto& frame=frames_[static_cast<unsigned>(active_)];
        if(frame.count==frame.lists.size()) {++stats_.dropped_lists;++frame.dropped;return 0;}
        frame.phase[frame.count]=static_cast<std::uint8_t>(phase);
        frame.pass[frame.count]=static_cast<std::uint8_t>(pass%max_pass);
        context->End(frame.lists[frame.count].start.Get());
        open_ticket_=++next_ticket_;
        return open_ticket_;
    }
    void end_list(ID3D11DeviceContext* context,Ticket ticket) {
        if(!ticket) return;
        if(!owns(context) || active_<0 || ticket!=open_ticket_) {++stats_.rejected_calls;return;}
        auto& frame=frames_[static_cast<unsigned>(active_)];
        context->End(frame.lists[frame.count].end.Get());++frame.count;++stats_.recorded_lists;open_ticket_=0;
    }
    void finish_frame(ID3D11DeviceContext* context) {
        if(!owns(context)) {++stats_.rejected_calls;return;}
        suppressed_=false;
        if(active_<0) return;
        auto& frame=frames_[static_cast<unsigned>(active_)];
        if(open_ticket_) {frame.invalid=true;++stats_.dropped_frames;++stats_.dropped_lists;open_ticket_=0;}
        context->End(frame.span_end.Get());context->End(frame.disjoint.Get());frame.pending=true;active_=-1;
    }
    Stats stats() const noexcept {return stats_;}
    void clear() {
        // Cleanup is also an immediate-owner operation. Close an open clock
        // bracket before releasing its queries; no completion wait is needed.
        if(active_>=0 && context_) {
            auto& frame=frames_[static_cast<unsigned>(active_)];
            context_->End(frame.span_end.Get());context_->End(frame.disjoint.Get());
        }
        for(auto& frame:frames_) frame={};
        context_.Reset();device_.Reset();active_=-1;open_ticket_=0;suppressed_=false;stats_={};
        // Monotonic identities prevent a stale ticket from ending a new sample after reset.
    }
};
} // namespace d2vr
