#pragma once
// DLSS mode logic shared by the renderer, the settings and the tests: which anti-aliasing modes are DLSS, the
// engine render fraction they need, the per-session fallback state machine and the user-supplied provider lookup.
// Pure: no D3D, NGX or file system (the provider lookup takes its existence check as a callable).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace d2vr {
namespace dlss_mode {
// vr_preferences.hpp temporal_aa values. 0..6 keep their meanings; DLSS modes are appended.
inline constexpr int dlaa=7,quality=8,last=8;
inline constexpr unsigned generation=5; // FsrTaa::eye_upscaler_generation while DLSS produced the eye (3 = FSR 3, 4 = FSR 4)
inline constexpr float default_quality_fraction=2.f/3; // DLSS Quality (1.5x) until NGX optimal settings answer for the output size
inline bool active(int mode) noexcept {return mode==dlaa || mode==quality;}
inline bool is_quality(int mode) noexcept {return mode==quality;}
inline bool fsr4(int mode) noexcept {return mode==5 || mode==6;}
// The engine render fraction for a mode (dxgi_probe.cpp aa_mode_fraction). DLSS Quality follows the optimal settings
// while DLSS is engaged (offered and not failed this session); otherwise its fallback is FSR 3 Native at fraction 1.
inline float render_fraction(int mode,float quality_fraction,bool dlss_engaged) noexcept {
    if(mode==quality) {
        if(!dlss_engaged) return 1.f;
        return std::isfinite(quality_fraction) && quality_fraction>=.5f && quality_fraction<=1.f?quality_fraction:default_quality_fraction;
    }
    return mode==3 || mode==6?.77f:mode==4?.67f:1.f;
}
// The dynamic governor only steps the FSR scaled modes: DLSS features are created at a fixed render size.
inline bool governor_eligible(int mode,float fraction) noexcept {return mode>=2 && mode<=6 && fraction<.999f;}
// NGX optimal render size -> engine fraction, accepted only when it is a sane downscale of the output size.
inline std::optional<float> optimal_fraction(unsigned out_w,unsigned out_h,unsigned opt_w,unsigned opt_h) noexcept {
    if(!out_w || !out_h || !opt_w || !opt_h || opt_w>out_w || opt_h>out_h) return std::nullopt;
    const float fx=static_cast<float>(opt_w)/static_cast<float>(out_w),fy=static_cast<float>(opt_h)/static_cast<float>(out_h);
    if(fx<.5f || fy<.5f || std::abs(fx-fy)>.02f) return std::nullopt;
    return std::min(fx,fy);
}
} // namespace dlss_mode

// Per-session DLSS state. Any NGX failure (init, create, evaluate, device loss) parks the session on FSR 3 until the
// session ends; deselecting the mode retires the features without clearing a failure, so re-selecting does not
// retry a broken provider mid-session. Resize/epoch changes need a new feature (pending), not a failure.
class DlssFallback {
public:
    enum class State:unsigned {off=0,pending=1,active=2,failed=3};
private:
    State state_=State::off;
    bool selected_=false;
    unsigned failures_=0,last_reason_=0,waiting_=0;
    int last_result_=0;
public:
    // Dispatches a pending Quality request may wait for the engine to adopt NGX's render size before it is a failure.
    static constexpr unsigned waiting_budget=240;
    unsigned waiting() const noexcept {return waiting_;}
    State state() const noexcept {return state_;}
    bool selected() const noexcept {return selected_;}
    bool failed() const noexcept {return state_==State::failed;}
    // A DLSS mode is wanted and the provider has not failed this session: the renderer keeps or builds features.
    bool engaged() const noexcept {return state_==State::pending || state_==State::active;}
    bool wants_feature() const noexcept {return state_==State::pending;}
    bool should_evaluate() const noexcept {return state_==State::active;}
    unsigned failures() const noexcept {return failures_;}
    unsigned last_reason() const noexcept {return last_reason_;}
    int last_result() const noexcept {return last_result_;}
    // Mode selected (true) or deselected (false). Returns true when the state changed.
    bool select(bool on) noexcept {
        selected_=on;
        if(state_==State::failed) return false; // sticky for the session
        const auto next=on?(state_==State::off?State::pending:state_):State::off;
        const bool changed=next!=state_;state_=next;if(changed) waiting_=0;return changed;
    }
    // Feature (re)creation outcome while pending.
    void note_prepare(bool ok,unsigned reason=0,int result=0) noexcept {
        if(state_!=State::pending) return;
        if(ok) {state_=State::active;waiting_=0;return;}
        fail(reason,result);
    }
    // One more dispatch spent waiting while pending: true once the budget is exhausted (the caller fails the session).
    bool note_waiting() noexcept {return state_==State::pending && ++waiting_>waiting_budget;}
    // An evaluate result while active.
    void note_evaluate(bool ok,unsigned reason=0,int result=0) noexcept {
        if(state_!=State::active || ok) return;
        fail(reason,result);
    }
    void note_device_lost(unsigned reason=0,int result=0) noexcept {if(state_!=State::off) fail(reason,result);}
    // The feature no longer matches (resize, format change, owner change): rebuild before evaluating again.
    void note_rebuild() noexcept {if(state_==State::active) {state_=State::pending;waiting_=0;}}
    // The session ended (FsrTaa::clear): a later session starts clean and may retry the provider.
    void session_end() noexcept {state_=State::off;selected_=false;}
private:
    void fail(unsigned reason,int result) noexcept {state_=State::failed;++failures_;last_reason_=reason;last_result_=result;waiting_=0;}
};

// Consecutive EVENT-slot refusals (the owner's bounded ring is full for longer than GPU latency explains) become a
// session failure after `threshold`, instead of silently alternating DLSS and FSR 3 eyes.
class DlssRefusalStreak {
    unsigned streak_=0;
public:
    static constexpr unsigned threshold=8;
    unsigned streak() const noexcept {return streak_;}
    // Returns true when this refusal exhausts the streak budget.
    bool note(bool refused) noexcept {
        if(!refused) {streak_=0;return false;}
        return ++streak_>=threshold;
    }
};

// GPU-retirement bookkeeping for DLSS feature pairs: each evaluate holds an EVENT ticket on its pair's generation;
// a retired pair is releasable once its last ticket completed. A device/context change or a session end abandons
// outstanding tickets (the GPU work is dead or long submitted) so no pair strands the SDK owner's 8-slot registry.
class DlssRetirementLedger {
    std::uint64_t active_=0,active_outstanding_=0;
    std::vector<std::pair<std::uint64_t,std::uint64_t>> retired_; // generation, outstanding tickets
public:
    std::uint64_t active() const noexcept {return active_;}
    std::uint64_t active_outstanding() const noexcept {return active_outstanding_;}
    std::size_t retired_count() const noexcept {return retired_.size();}
    bool retired_pending(std::uint64_t generation) const noexcept {for(const auto& r:retired_) if(r.first==generation) return r.second!=0;return false;}
    // A new active pair; the previous one (if any) must have been retired first.
    void activate(std::uint64_t generation) noexcept {active_=generation;active_outstanding_=0;}
    void reserved() noexcept {if(active_) ++active_outstanding_;}
    // A ticket completed for `generation`; returns true when that generation is a retired pair that is now releasable.
    bool completed(std::uint64_t generation) noexcept {
        if(generation==active_ && active_) {if(active_outstanding_) --active_outstanding_;return false;}
        for(auto& r:retired_) if(r.first==generation) {if(r.second) --r.second;return r.second==0;}
        return false;
    }
    // The active pair retires; returns true when it has no outstanding ticket (releasable at once).
    bool retire() {
        if(!active_) return false;
        retired_.push_back({active_,active_outstanding_});const bool releasable=active_outstanding_==0;
        active_=0;active_outstanding_=0;return releasable;
    }
    // Generations releasable now (outstanding == 0), removed from the ledger.
    std::vector<std::uint64_t> take_releasable() {
        std::vector<std::uint64_t> out;
        for(auto r=retired_.begin();r!=retired_.end();) {if(r->second==0) {out.push_back(r->first);r=retired_.erase(r);} else ++r;}
        return out;
    }
    // Device/context change or session end: every outstanding ticket is abandoned; all retired generations become releasable.
    void abandon_outstanding() noexcept {for(auto& r:retired_) r.second=0;active_outstanding_=0;}
};

// Runs work at a noexcept boundary: an escaping exception (allocation failure, SDK wrapper) becomes the fallback
// instead of std::terminate. Returns false when `on_exception` ran.
template<class Work,class OnException> bool dlss_guarded(Work&& work,OnException&& on_exception) noexcept {
    try {work();return true;} catch(...) {try {on_exception();} catch(...) {} return false;}
}

// Where the user's nvngx_dlss.dll is. The mod never bundles the provider: the mod home root is searched first, then
// build/dlss-dll-path (the launcher's "DLSS: point to nvngx_dlss.dll from one of your games"), which may name the
// DLL or its folder, with surrounding whitespace or quotes. Only a file called nvngx_dlss.dll is accepted.
struct DlssProvider {
    std::filesystem::path directory; // NGX PathListInfo entry (absolute)
    enum class Source {mod_home,configured} source;
};
inline std::wstring trim_dlss_path(std::wstring text) {
    // A hand-edited file may start with a UTF-8 BOM (U+FEFF after conversion); whitespace and quotes are decoration.
    const auto space=[](wchar_t ch) {return ch==L' ' || ch==L'\t' || ch==L'\r' || ch==L'\n' || ch==L'"' || ch==L'\'' || ch==L'\xFEFF';};
    while(!text.empty() && space(text.back())) text.pop_back();
    std::size_t start=0;while(start<text.size() && space(text[start])) ++start;
    return text.substr(start);
}
inline bool is_dlss_provider_name(const std::filesystem::path& p) {
    auto name=p.filename().wstring();for(auto& ch:name) ch=static_cast<wchar_t>(std::towlower(ch));
    return name==L"nvngx_dlss.dll";
}
template<class Exists> std::optional<DlssProvider> resolve_dlss_provider(const std::filesystem::path& mod_home,const std::wstring& knob_text,Exists&& exists) {
    const auto home=mod_home/L"nvngx_dlss.dll";
    if(exists(home)) return DlssProvider{mod_home,DlssProvider::Source::mod_home};
    const auto text=trim_dlss_path(knob_text);
    if(text.empty()) return std::nullopt;
    std::filesystem::path configured(text);
    if(!is_dlss_provider_name(configured)) configured/=L"nvngx_dlss.dll";
    if(!configured.is_absolute() || !exists(configured)) return std::nullopt;
    return DlssProvider{configured.parent_path(),DlssProvider::Source::configured};
}
} // namespace d2vr
