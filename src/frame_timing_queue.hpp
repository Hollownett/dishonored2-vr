#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <ostream>

namespace d2vr {
struct FrameTimingRecord {
    std::array<std::int64_t,58> xr{};
    std::array<std::int64_t,18> present{};
    bool has_xr{};
};
constexpr std::size_t frame_timing_columns(const char* header) {
    std::size_t count=1;for(;*header;++header)if(*header==',')++count;return count;
}
// A slow diagnostic writer must never retain a lock needed by Present.
class FrameTimingQueue {
public:
    static constexpr unsigned capacity=256;
private:
    std::mutex mutex_;
    std::array<FrameTimingRecord,capacity> records_{};
    unsigned head_{},count_{};
    bool stopped_{};
    std::atomic<std::uint64_t> dropped_{};
public:
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);head_=count_=0;stopped_=false;dropped_=0;
    }
    void stop() {std::lock_guard<std::mutex> lock(mutex_);stopped_=true;}
    bool submit(const FrameTimingRecord& record) noexcept {
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock || stopped_ || count_==capacity) {++dropped_;return false;}
        records_[(head_+count_)%capacity]=record;++count_;return true;
    }
    std::optional<FrameTimingRecord> pop() {
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock || !count_)return {};
        auto record=records_[head_];head_=(head_+1)%capacity;--count_;return record;
    }
    std::uint64_t dropped() const noexcept {return dropped_.load();}
};
template<std::size_t N> void write_frame_timing_row(std::ostream& out,const std::array<std::int64_t,N>& row) {
    for(std::size_t i=0;i<N;++i) {if(i)out<<',';out<<row[i];}out<<'\n';
}
}
