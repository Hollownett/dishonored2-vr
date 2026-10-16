#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <mutex>
#include <ostream>

namespace d2vr {
class RenderPerfCpu {
public:
    enum Category : unsigned {hud_validate,hud_begin,hud_end,hud_publish,fsr_flush,hud_hook_self,fsr_record,fsr_handoff,fsr_dispatch,category_count};
    struct Row {std::uint64_t epoch{},frame{},thread{},category{},count{},ns{},maximum{};};
private:
    struct Metric {std::uint64_t count{},ns{},maximum{};};
    struct Thread {
        std::mutex mutex;DWORD id{};std::uint64_t epoch{},frame{};
        std::array<Metric,category_count> values{};
    };
    std::array<Thread,32> threads_{};
    std::mutex registry_,queue_mutex_;
    std::array<Row,8192> queue_{};
    std::size_t read_{},write_{},count_{};
    std::uint64_t dropped_{};
    void push(const Row& row_record) noexcept {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if(count_==queue_.size()) {++dropped_;return;}
        queue_[write_]=row_record;write_=(write_+1)%queue_.size();++count_;
    }
    void flush(Thread& thread_record) noexcept {
        for(unsigned category_index=0;category_index<category_count;++category_index) {
            auto& metric_totals=thread_record.values[category_index];
            if(metric_totals.count) push({thread_record.epoch,thread_record.frame,thread_record.id,category_index,metric_totals.count,metric_totals.ns,metric_totals.maximum});
            metric_totals={};
        }
    }
public:
    static std::uint64_t now_ns() noexcept {
        static const std::uint64_t frequency=[] {LARGE_INTEGER frequency_counter{};QueryPerformanceFrequency(&frequency_counter);return static_cast<std::uint64_t>(frequency_counter.QuadPart);}();
        LARGE_INTEGER counter_value{};QueryPerformanceCounter(&counter_value);
        const auto ticks=static_cast<std::uint64_t>(counter_value.QuadPart);
        return frequency?(ticks/frequency)*1000000000ull+(ticks%frequency)*1000000000ull/frequency:0;
    }
    void record(std::uint64_t epoch,std::uint64_t frame,unsigned category,std::uint64_t ns) noexcept {
        if(category>=category_count)return;
        const auto id=GetCurrentThreadId();Thread* chosen{};
        {
            std::lock_guard<std::mutex> registry(registry_);
            for(auto& thread_record:threads_) if(thread_record.id==id) {chosen=&thread_record;break;}
            if(!chosen) for(auto& thread_record:threads_) if(!thread_record.id) {std::lock_guard<std::mutex> lock(thread_record.mutex);thread_record.id=id;chosen=&thread_record;break;}
        }
        if(!chosen) {std::lock_guard<std::mutex> lock(queue_mutex_);++dropped_;return;}
        std::lock_guard<std::mutex> lock(chosen->mutex);
        if(chosen->epoch!=epoch || chosen->frame!=frame) {flush(*chosen);chosen->epoch=epoch;chosen->frame=frame;}
        auto& metric_totals=chosen->values[category];++metric_totals.count;metric_totals.ns+=ns;if(ns>metric_totals.maximum)metric_totals.maximum=ns;
    }
    class Scope {
        RenderPerfCpu* owner_{};std::uint64_t epoch_{},frame_{},start_{};unsigned category_{};
    public:
        Scope(RenderPerfCpu* owner,std::uint64_t epoch,std::uint64_t frame,unsigned category) noexcept
            :owner_(owner),epoch_(epoch),frame_(frame),start_(owner?now_ns():0),category_(category) {}
        Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
        ~Scope(){stop();}
        void stop() noexcept {if(owner_){const auto end=now_ns();owner_->record(epoch_,frame_,category_,end>=start_?end-start_:0);owner_=nullptr;}}
    };
    void drain(std::ostream& output) {
        for(auto& thread_record:threads_) {std::lock_guard<std::mutex> lock(thread_record.mutex);flush(thread_record);}
        for(;;) {
            std::array<Row,64> batch{};std::size_t batch_count{};
            {std::lock_guard<std::mutex> lock(queue_mutex_);
                while(count_ && batch_count<batch.size()){batch[batch_count++]=queue_[read_];read_=(read_+1)%queue_.size();--count_;}}
            if(!batch_count)break;
            for(std::size_t category_index=0;category_index<batch_count;++category_index){const auto& row_record=batch[category_index];output<<row_record.epoch<<','<<row_record.frame<<','<<row_record.thread<<','<<row_record.category<<','<<row_record.count<<','<<row_record.ns<<','<<row_record.maximum<<'\n';}
        }
    }
    std::uint64_t dropped() {std::lock_guard<std::mutex> lock(queue_mutex_);return dropped_;}
};
}
