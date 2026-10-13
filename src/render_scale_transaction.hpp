#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>

namespace d2vr {
// The headset output override is a separate ownership layer: native default
// zero -> full-scale one -> transaction fraction -> one -> native default.
// A native reset to the exact saved default may be rebound while that layer
// remains owned. Other values are left for the transaction's ownership check.
template<class Read,class CAS>
bool rebind_render_scale_baseline(unsigned axis,std::uint32_t saved,std::uint32_t full,bool owned,Read read,CAS cas) {
    if(!owned) return true;
    const auto current=read(axis);if(!current) return false;
    if(*current!=saved || saved==full) return true;
    if(!cas(axis,saved,full)) return false;
    return read(axis)==std::optional<std::uint32_t>{full};
}
template<class Read,class CAS>
bool restore_render_scale_baseline(unsigned axis,std::uint32_t saved,std::uint32_t full,bool owned,Read read,CAS cas) {
    if(!owned) return true;
    const auto current=read(axis);if(!current) return false;
    if(*current==saved) return true; // Native already restored it.
    if(*current!=full || !cas(axis,full,saved)) return false;
    return read(axis)==std::optional<std::uint32_t>{saved};
}
// Read(index) returns optional bits; CAS(index, expected, desired) reports success.
// Enable(reduced) sets AND verifies the caller-owned rs_enable caches. Failure
// must leave their prior state intact. This helper knows no native addresses/ABI.
class RenderScaleTransaction {
public:
    struct Axis { bool original_saved{}, owned{}; std::uint32_t saved{}, last_owned{}; };
    struct Result {
        bool committed{}, failed{}, foreign{}, baseline_reset{}, rollback_failed{}, enable_failed{};
        std::array<std::optional<std::uint32_t>,2> actual{};
    };
    std::array<Axis,2> axes{};
    bool active{};
    bool enable_reduced{}; // Last successful Enable callback, including rollback.
    static std::uint32_t bits(float value) noexcept {
        std::uint32_t out{};std::memcpy(&out,&value,sizeof(out));return out;
    }
    template<class Read,class CAS,class Enable>
    Result apply(float requested,Read read,CAS cas,Enable enable) {
        Result result;
        auto observe=[&] {for(unsigned i=0;i<2;++i) result.actual[i]=read(i);};
        observe();
        if(!std::isfinite(requested)||requested<0.5f||requested>1.0f) {result.failed=true;return result;}
        const bool reduced=requested<1.0f;
        if(!reduced) {
            // Restore independently: a foreign X cache must not prevent safely
            // restoring our Y cache, nor may it be overwritten.
            for(unsigned i=0;i<2;++i) {
                auto& axis=axes[i];
                if(!result.actual[i]) {result.failed=true;continue;}
                if(!axis.original_saved) continue;
                const auto current=*result.actual[i];
                if(current==axis.saved) {axis.owned=false;continue;}
                if(!axis.owned||current!=axis.last_owned) {result.failed=true;result.foreign=true;continue;}
                if(!cas(i,current,axis.saved)) {result.failed=true;continue;}
                result.actual[i]=read(i);
                if(!result.actual[i]||*result.actual[i]!=axis.saved) {result.failed=true;continue;}
                axis.owned=false;
            }
            active=axes[0].owned||axes[1].owned;
            if(!result.failed) {
                if(!enable(false)) {result.failed=true;result.enable_failed=true;}
                else enable_reduced=false;
            }
            observe();
            for(unsigned i=0;i<2;++i) {
                if(!result.actual[i]||(axes[i].original_saved&&*result.actual[i]!=axes[i].saved)) result.failed=true;
            }
            if(!result.failed) {result.committed=true;axes={};active=false;}
            return result;
        }
        const auto prior=axes;
        const bool previous_reduced=enable_reduced;
        std::array<std::uint32_t,2> before{},desired{};
        std::array<bool,2> written{};
        for(unsigned i=0;i<2;++i) {
            if(!result.actual[i]) {result.failed=true;return result;}
            before[i]=*result.actual[i];
            auto& axis=axes[i];
            if(!axis.original_saved) {axis.original_saved=true;axis.saved=before[i];}
            if(before[i]!=axis.saved && (!axis.owned||before[i]!=axis.last_owned)) {
                // The game rewrote its own cache: adopt that as the baseline instead of refusing forever.
                result.foreign=true;axis.saved=before[i];axis.owned=false;
            }
            if(axis.owned && axis.last_owned!=axis.saved && before[i]==axis.saved) result.baseline_reset=true;
            desired[i]=reduced?bits(requested):axis.saved;
        }
        auto rollback=[&] {
            for(unsigned i=0;i<2;++i) {
                if(!written[i]) continue;
                if(cas(i,desired[i],before[i])) axes[i]=prior[i];
                else {result.rollback_failed=true; /* retain last_owned for a later safe restore */}
            }
            active=axes[0].owned||axes[1].owned;
            observe();
        };
        for(unsigned i=0;i<2;++i) {
            if(before[i]==desired[i]) continue;
            if(!cas(i,before[i],desired[i])) {result.failed=true;rollback();return result;}
            written[i]=true;axes[i].owned=true;axes[i].last_owned=desired[i];
        }
        observe();
        for(unsigned i=0;i<2;++i) {
            if(!result.actual[i]||*result.actual[i]!=desired[i]) {
                result.failed=true;rollback();return result;
            }
        }
        if(!enable(reduced)) {
            result.failed=true;result.enable_failed=true;rollback();return result;
        }
        enable_reduced=reduced;
        // A cvar operation may run concurrently with a native cache reset.
        observe();
        for(unsigned i=0;i<2;++i) {
            if(!result.actual[i]||*result.actual[i]!=desired[i]) {
                result.failed=true;
                if(!enable(previous_reduced)) {result.enable_failed=true;result.rollback_failed=true;}
                else enable_reduced=previous_reduced;
                rollback();return result;
            }
        }
        result.committed=true;
        if(reduced) {
            for(unsigned i=0;i<2;++i) {axes[i].owned=true;axes[i].last_owned=desired[i];}
            active=true;
        } else {axes={};active=false;}
        return result;
    }
};
} // namespace d2vr
