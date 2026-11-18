#pragma once
// Whether an upscaler rebuild (fsr_taa.hpp FsrTaa::prepare) tries to create FSR 4 contexts. Pure: no D3D, no provider.
//
// FSR 4 (the signed AMD amd_fidelityfx_upscaler_dx12.dll) runs on AMD GPUs only. Every create verifies the provider's
// Authenticode signature, loads it and creates a same-adapter D3D12 device before it can fail, so a GPU that cannot run
// it paid for that at every rebuild: 2026-11-19, RTX 5070, feedback_render.fsr4_create_failures 8, each rebuild 360-430 ms
// ("fsr4_ok=0 fsr4_failures=2" in resolution-apply.log). Plan 1.3 in docs/PERF-PLAN-90HZ-2026-11-19.md.
// Rules: no provider or a non-AMD adapter -> never try; the first failure before any success this session -> stop trying
// until the session ends (FsrTaa::clear). After a success, failures stay retryable, so a working AMD setup is unchanged.

namespace d2vr {
class Fsr4CreateGate {
public:
    static constexpr unsigned amd_vendor=0x1002; // PCI vendor id (DXGI_ADAPTER_DESC::VendorId)
    enum class Skip:unsigned {none=0,no_provider=1,vendor=2,failed=3};
    // An adapter FSR 4 may run on: AMD, or unknown (0 = the adapter query failed: one create decides).
    static constexpr bool vendor_may_run(unsigned vendor) noexcept {return vendor==0 || vendor==amd_vendor;}
    Skip skip(bool provider,unsigned vendor) const noexcept {
        if(!provider) return Skip::no_provider;
        if(!vendor_may_run(vendor)) return Skip::vendor;
        if(latched()) return Skip::failed;
        return Skip::none;
    }
    bool should_create(bool provider,unsigned vendor) const noexcept {return skip(provider,vendor)==Skip::none;}
    // One create's outcome. A failure before any success this session means this GPU/driver cannot run FSR 4: the other
    // eye and every later rebuild skip it. After a success a failure (resize, memory pressure, retiring contexts) is retried.
    void note_create(bool ok) noexcept {if(ok) created_=true; else if(!created_) failed_=true;}
    bool created() const noexcept {return created_;}
    bool latched() const noexcept {return failed_ && !created_;}
    // The session ended: the next one may try again (a driver update or another adapter).
    void session_end() noexcept {created_=failed_=false;}
private:
    bool created_=false,failed_=false;
};
inline const char* fsr4_skip_name(Fsr4CreateGate::Skip skip) noexcept {
    switch(skip) {
        case Fsr4CreateGate::Skip::none:return "none";
        case Fsr4CreateGate::Skip::no_provider:return "no_provider";
        case Fsr4CreateGate::Skip::vendor:return "vendor";
        case Fsr4CreateGate::Skip::failed:return "failed";
    }
    return "unknown";
}
} // namespace d2vr
