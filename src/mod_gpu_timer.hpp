#pragma once
// The mod's own GPU work (plan E task 3b): timestamp pairs around FSR dispatch, AA/sharpen, eye copies, overlay draws and the
// native_eye_capture copy. One disjoint bracket per Present frame (first span opens it, finish_frame() at the end of the Present
// hook closes it). Polled with DONOTFLUSH only, never flushes or waits. Enabled by the same native-gpu-timing knob as
// NativeListGpuTimer; every call is a cheap no-op until prepare() ran. A mutex guards the tiny state (spans come from the
// render thread; the status thread only reads stats()).
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include "frame_split.hpp"

namespace d2vr {
class ModGpuTimer {
public:
    static constexpr unsigned max_spans=32,ring=8;
    using Ticket=std::uint64_t; // 0 = no sample
    struct Stats {std::uint64_t frames{},dropped_spans{},disjoint{},query_failures{},last_sequence{};ModGpu last{};};
private:
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Span {Ptr<ID3D11Query> start,end;unsigned char label{};bool closed{};};
    struct Frame {Ptr<ID3D11Query> disjoint;std::array<Span,max_spans> spans;unsigned count{};bool pending{},open{};std::uint64_t sequence{};};
    std::array<Frame,ring> frames_{};
    Ptr<ID3D11Device> device_;Ptr<ID3D11DeviceContext> context_;
    int active_=-1;std::uint64_t sequence_{};Stats stats_{};
    std::atomic<bool> ready_{false};
    mutable std::mutex mutex_;
    void clear_locked() {
        if(active_>=0 && context_) context_->End(frames_[static_cast<unsigned>(active_)].disjoint.Get());
        for(auto& f:frames_) f={};
        context_.Reset();device_.Reset();active_=-1;stats_={};ready_=false;
    }
public:
    bool active() const noexcept {return ready_.load(std::memory_order_acquire);}
    bool prepare(ID3D11Device* device) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!device) return false;
        if(device_.Get()==device) return true;
        clear_locked();
        for(auto& f:frames_) {
            D3D11_QUERY_DESC q{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
            bool ok=SUCCEEDED(device->CreateQuery(&q,&f.disjoint));
            q.Query=D3D11_QUERY_TIMESTAMP;
            for(auto& s:f.spans) {if(!ok) break;ok=SUCCEEDED(device->CreateQuery(&q,&s.start)) && SUCCEEDED(device->CreateQuery(&q,&s.end));}
            if(!ok) {clear_locked();++stats_.query_failures;return false;}
        }
        device_=device;device->GetImmediateContext(&context_);
        if(!context_) {clear_locked();return false;}
        ready_=true;return true;
    }
    Ticket begin(ID3D11DeviceContext* context,ModSpan label) {
        if(!active()) return 0;
        std::lock_guard<std::mutex> lock(mutex_);
        if(!context || context!=context_.Get()) return 0;
        if(active_<0) {
            for(unsigned i=0;i<ring;++i) if(!frames_[i].pending) {active_=static_cast<int>(i);break;}
            if(active_<0) {++stats_.dropped_spans;return 0;}
            auto& f=frames_[static_cast<unsigned>(active_)];
            f.count=0;f.sequence=++sequence_;context->Begin(f.disjoint.Get());
        }
        auto& f=frames_[static_cast<unsigned>(active_)];
        if(f.count==max_spans) {++stats_.dropped_spans;return 0;}
        auto& s=f.spans[f.count];s.label=static_cast<unsigned char>(label);s.closed=false;
        context->End(s.start.Get());
        const auto index=f.count++;
        return f.sequence*(max_spans+1)+index+1;
    }
    void end(ID3D11DeviceContext* context,Ticket ticket) {
        if(!ticket || !active()) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if(!context || context!=context_.Get() || active_<0) return;
        auto& f=frames_[static_cast<unsigned>(active_)];
        const auto seq=ticket/(max_spans+1);const auto idx=static_cast<unsigned>(ticket%(max_spans+1))-1;
        if(seq!=f.sequence || idx>=f.count || f.spans[idx].closed) return;
        context->End(f.spans[idx].end.Get());f.spans[idx].closed=true;
    }
    // End of the Present hook: close the bracket so its results become pollable.
    void finish_frame(ID3D11DeviceContext* context) {
        if(!active()) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if(!context || context!=context_.Get() || active_<0) return;
        auto& f=frames_[static_cast<unsigned>(active_)];
        context->End(f.disjoint.Get());f.pending=true;active_=-1;
    }
    void poll(ID3D11DeviceContext* context) {
        if(!active()) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if(!context || context!=context_.Get()) return;
        for(auto& f:frames_) if(f.pending) {
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock{};
            const HRESULT r=context->GetData(f.disjoint.Get(),&clock,sizeof clock,D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(FAILED(r)) {f.pending=false;++stats_.query_failures;continue;}
            if(r!=S_OK) continue;
            if(clock.Disjoint || !clock.Frequency) {f.pending=false;++stats_.disjoint;continue;}
            ModGpu sum{};bool complete=true;
            for(unsigned i=0;i<f.count && complete;++i) {
                if(!f.spans[i].closed) continue;
                UINT64 a{},b{};
                if(context->GetData(f.spans[i].start.Get(),&a,sizeof a,D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK ||
                   context->GetData(f.spans[i].end.Get(),&b,sizeof b,D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK) {complete=false;break;}
                if(b>=a) sum.ms[f.spans[i].label%mod_span_count]+=static_cast<double>(b-a)*1000.0/static_cast<double>(clock.Frequency);
            }
            if(!complete) continue;
            f.pending=false;sum.valid=true;++stats_.frames;
            if(f.sequence>stats_.last_sequence) {stats_.last_sequence=f.sequence;stats_.last=sum;}
        }
    }
    Stats stats() const {std::lock_guard<std::mutex> lock(mutex_);return stats_;}
    void clear() {std::lock_guard<std::mutex> lock(mutex_);clear_locked();}
};
inline ModGpuTimer& mod_gpu_timer() {static ModGpuTimer timer;return timer;}
// Scoped span on the immediate context; a no-op while the timer is off.
class ModGpuScope {
    ID3D11DeviceContext* context_;ModGpuTimer::Ticket ticket_{};
public:
    ModGpuScope(ID3D11DeviceContext* context,ModSpan label) noexcept : context_(context) {
        if(mod_gpu_timer().active()) {try {ticket_=mod_gpu_timer().begin(context,label);} catch(...) {}}
    }
    ~ModGpuScope() {if(ticket_) {try {mod_gpu_timer().end(context_,ticket_);} catch(...) {}}}
    ModGpuScope(const ModGpuScope&)=delete;ModGpuScope& operator=(const ModGpuScope&)=delete;
};
} // namespace d2vr
