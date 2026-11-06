#pragma once
// v999 per-pass GPU window.  NativeListGpuTimer publishes one PassFrame per completed timer frame
// (frame totals by pass and phase); this keeps the last 900 such frames and turns them into the
// per-pass eye0/eye1 p50/p90 and lists_p50 of the status "gpu_passes" block.  The window also keeps
// the mod's own GPU spans (FSR eye 0/1, sharpen, copies, overlays) from the same feed, so the pass
// table and the mod cost are read side by side.  All percentiles are nearest-rank and include zero
// frames: a pass that only runs on some frames shows its per-frame cost, not a conditional cost.
#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>
#include "frame_split.hpp"
#include "native_list_gpu_timer.hpp"
#include "render_pass_table.hpp"

namespace d2vr {
class GpuPassWindow {
public:
    static constexpr std::size_t window=900;
    // Returns false for a duplicate/lagging sequence: FrameSplit is fed every Present, the native timer
    // is lagged, so only a new completed timer frame may enter the window.
    bool feed(std::uint64_t sequence,const NativeListGpuTimer::PassFrame& frame,const ModGpu* mod) noexcept {
        if(!frame.valid || !sequence) return false;
        if(sequence<=last_sequence_) {
            // Same sequence: the lagged timer has not published a new frame yet.  Lower sequence: the
            // timer was cleared/reprepared (resolution/AA config change), so discard the old window.
            if(sequence<last_sequence_) clear();
            return false;
        }
        last_sequence_=sequence;
        const auto slot=head_;
        for(unsigned pass=0;pass<NativeListGpuTimer::max_pass;++pass) {
            ms_[pass][0][slot]=static_cast<float>(frame.ms[pass][static_cast<unsigned>(Phase::eye0)]);
            ms_[pass][1][slot]=static_cast<float>(frame.ms[pass][static_cast<unsigned>(Phase::eye1)]);
            unsigned lists=0;
            for(unsigned phase=0;phase<phase_count;++phase) lists+=frame.lists[pass][phase];
            lists_[pass][slot]=static_cast<std::uint16_t>(std::min<unsigned>(lists,65535u));
        }
        const bool mod_ok=mod && mod->valid;
        mod_valid_[slot]=mod_ok;
        float mod_sum=0.f;
        for(unsigned span=0;span<mod_span_count;++span) {
            mod_ms_[span][slot]=mod_ok?static_cast<float>(mod->ms[span]):0.f;
            if(mod_ok) mod_sum+=mod_ms_[span][slot];
        }
        mod_sum_[slot]=mod_sum;
        head_=(head_+1)%window;
        if(count_<window) ++count_;
        return true;
    }
    void clear() noexcept {
        for(auto& pass:ms_) for(auto& eye:pass) eye.fill(0.f);
        for(auto& pass:lists_) pass.fill(0);
        for(auto& span:mod_ms_) span.fill(0.f);
        mod_sum_.fill(0.f);mod_valid_.fill(false);
        head_=0;count_=0;last_sequence_=0;
    }
    std::size_t frames() const noexcept {return count_;}
    std::uint64_t last_sequence() const noexcept {return last_sequence_;}
    // Nearest-rank percentiles over the window, zeros included; public so the lab checks the same
    // numbers the status writer prints.
    double pass_total_ms(unsigned pass) const noexcept {
        if(pass>=NativeListGpuTimer::max_pass || !count_) return 0;
        double total=0;
        for(std::size_t i=0;i<count_;++i) total+=static_cast<double>(ms_[pass][0][i])+static_cast<double>(ms_[pass][1][i]);
        return total/static_cast<double>(count_);
    }
    double pass_ms_percentile(unsigned pass,unsigned eye,double p) const noexcept {
        if(pass>=NativeListGpuTimer::max_pass || eye>1 || !count_) return 0;
        std::array<bool,window> valid{};
        for(std::size_t i=0;i<count_;++i) valid[i]=true;
        bool any=false;
        return percentile_sample(ms_[pass][eye],valid.data(),count_,p,any);
    }
    double pass_lists_percentile(unsigned pass,double p) const noexcept {
        if(pass>=NativeListGpuTimer::max_pass || !count_) return 0;
        std::array<bool,window> valid{};
        for(std::size_t i=0;i<count_;++i) valid[i]=true;
        bool any=false;
        return percentile_sample(lists_[pass],valid.data(),count_,p,any);
    }
    double mod_ms_percentile(unsigned span,double p) const noexcept {
        if(span>=mod_span_count || !count_) return 0;
        bool any=false;
        return percentile_sample(mod_ms_[span],mod_valid_.data(),count_,p,any);
    }
    std::string json(bool pass_map_installed) const {
        const std::size_t n=count_;
        struct Entry {unsigned id;double total;};
        std::vector<Entry> entries;
        double untagged_total=0;
        for(unsigned id=0;id<NativeListGpuTimer::max_pass;++id) {
            if(id>=render_pass_count && id!=render_pass_untagged) continue;
            unsigned lists=0;
            double total=0;
            for(std::size_t i=0;i<n;++i) {
                lists+=lists_[id][i];
                total+=static_cast<double>(ms_[id][0][i])+static_cast<double>(ms_[id][1][i]);
            }
            if(!lists) continue;
            total=n?total/static_cast<double>(n):0;
            if(id==render_pass_untagged) untagged_total=total;
            else entries.push_back({id,total});
        }
        std::sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.total>b.total;});
        std::ostringstream out;
        out<<"{\"enabled\":"<<(pass_map_installed?"true":"false")
           <<",\"frames\":"<<n<<",\"window\":"<<window
           <<",\"passes\":[";
        for(std::size_t i=0;i<entries.size();++i) {
            if(i) out<<',';
            entry_json(out,entries[i].id,entries[i].total);
        }
        out<<"],\"untagged\":";
        entry_json(out,render_pass_untagged,untagged_total);
        out<<",\"mod_gpu_ms_p50\":";
        mod_json(out,n);
        out<<'}';
        return out.str();
    }
private:
    void entry_json(std::ostringstream& out,unsigned id,double total) const {
        const double eye0_p50=pass_ms_percentile(id,0,.50);
        const double eye0_p90=pass_ms_percentile(id,0,.90);
        const double eye1_p50=pass_ms_percentile(id,1,.50);
        const double eye1_p90=pass_ms_percentile(id,1,.90);
        const double lists_p50=pass_lists_percentile(id,.50);
        out<<"{\"name\":\""<<(id==render_pass_untagged?"untagged":render_pass_name(id))
           <<"\",\"total_ms\":"<<total
           <<",\"eye0_ms_p50\":"<<eye0_p50<<",\"eye1_ms_p50\":"<<eye1_p50
           <<",\"eye0_ms_p90\":"<<eye0_p90<<",\"eye1_ms_p90\":"<<eye1_p90
           <<",\"lists_p50\":"<<static_cast<std::uint64_t>(lists_p50+.5)<<'}';
    }
    void mod_json(std::ostringstream& out,std::size_t n) const {
        bool any=false;
        const double first=percentile_sample(mod_sum_,mod_valid_.data(),n,.50,any);
        if(!any) {out<<"null";return;}
        out<<'{';
        for(unsigned span=0;span<mod_span_count;++span) {
            bool have=false;
            out<<(span?",":"")<<'"'<<mod_span_name(span)<<"\":"<<percentile_sample(mod_ms_[span],mod_valid_.data(),n,.50,have);
        }
        out<<",\"sum\":"<<first<<'}';
    }
    std::array<std::array<std::array<float,window>,2>,NativeListGpuTimer::max_pass> ms_{};
    std::array<std::array<std::uint16_t,window>,NativeListGpuTimer::max_pass> lists_{};
    std::array<std::array<float,window>,mod_span_count> mod_ms_{};
    std::array<float,window> mod_sum_{};
    std::array<bool,window> mod_valid_{};
    std::size_t head_{},count_{};
    std::uint64_t last_sequence_{};
};
} // namespace d2vr
