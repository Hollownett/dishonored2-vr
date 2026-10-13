#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <climits>

namespace d2vr {
// Immediate-context timestamp samples. Polling never flushes or waits for GPU work.
class GpuPassTimer {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Slot {Ptr<ID3D11Query> disjoint,start,end;bool pending{},recording{};std::uint64_t sequence{};int ticket=-1;};
    std::array<Slot,8> slots_{};
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11DeviceContext> context_;
    int next_ticket_{};
public:
    struct Stats {std::uint64_t samples{},disjoint{},dropped{};double last_ms{},total_ms{},max_ms{};};
private:
    Stats stats_{};
    std::uint64_t sequence_{},published_sequence_{};
public:
    bool valid() const noexcept {return device_.Get()!=nullptr;}
    bool prepare(ID3D11Device* device) {
        if(!device) return false;
        if(device_.Get()==device) return true;
        clear();
        for(auto& slot:slots_) {
            D3D11_QUERY_DESC q{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
            if(FAILED(device->CreateQuery(&q,&slot.disjoint))) {clear();return false;}
            q.Query=D3D11_QUERY_TIMESTAMP;
            if(FAILED(device->CreateQuery(&q,&slot.start)) || FAILED(device->CreateQuery(&q,&slot.end))) {clear();return false;}
        }
        device_=device;device->GetImmediateContext(&context_);return context_.Get()!=nullptr;
    }
    void poll(ID3D11DeviceContext* context) {
        if(!device_ || !context || context!=context_.Get()) return;
        for(auto& slot:slots_) if(slot.pending) {
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock{};UINT64 start{},end{};
            if(context->GetData(slot.disjoint.Get(),&clock,sizeof clock,D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK) continue;
            if(clock.Disjoint || !clock.Frequency) {slot.pending=false;++stats_.disjoint;continue;}
            if(context->GetData(slot.start.Get(),&start,sizeof start,D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK ||
               context->GetData(slot.end.Get(),&end,sizeof end,D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK) continue;
            slot.pending=false;
            if(end<start) {++stats_.disjoint;continue;}
            const double ms=static_cast<double>(end-start)*1000.0/static_cast<double>(clock.Frequency);
            stats_.total_ms+=ms;if(ms>stats_.max_ms) stats_.max_ms=ms;++stats_.samples;
            if(slot.sequence>=published_sequence_) {stats_.last_ms=ms;published_sequence_=slot.sequence;}
        }
    }
    int begin(ID3D11DeviceContext* context) {
        if(!device_ || !context || context!=context_.Get()) return -1;
        for(const auto& slot:slots_) if(slot.recording) {++stats_.dropped;return -1;}
        poll(context);
        for(unsigned i=0;i<slots_.size();++i) if(!slots_[i].pending && !slots_[i].recording) {
            if(next_ticket_==INT_MAX) {++stats_.dropped;return -1;}
            auto& slot=slots_[i];slot.recording=true;slot.ticket=next_ticket_++;slot.sequence=++sequence_;context->Begin(slot.disjoint.Get());context->End(slot.start.Get());return slot.ticket;
        }
        ++stats_.dropped;return -1;
    }
    void end(ID3D11DeviceContext* context,int ticket) {
        if(!context || context!=context_.Get() || ticket<0) return;
        for(auto& slot:slots_) if(slot.recording && slot.ticket==ticket) {
            context->End(slot.end.Get());context->End(slot.disjoint.Get());slot.recording=false;slot.pending=true;return;
        }
    }
    Stats stats() const noexcept {return stats_;}
    void clear() {
        for(auto& slot:slots_) {
            if(slot.recording && context_) {context_->End(slot.end.Get());context_->End(slot.disjoint.Get());}
            slot={};
        }
        context_.Reset();device_.Reset();stats_={};sequence_=published_sequence_=0;
    }
};
} // namespace d2vr
