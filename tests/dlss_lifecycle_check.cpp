#include "dlss_eye_upscaler.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <set>
#include <thread>
#include <vector>
using U = d2vr::DlssEyeUpscaler;
namespace {
constexpr auto ok = NVSDK_NGX_Result_Success;
constexpr auto bad = NVSDK_NGX_Result_FAIL_OutOfGPUMemory;
template<class T> T* opaque(std::uintptr_t n) { return reinterpret_cast<T*>(n); }
struct Fixture;
Fixture* fixture{};
struct Fixture {
    std::vector<std::string> events;
    std::set<NVSDK_NGX_Parameter*> maps;
    std::set<NVSDK_NGX_Handle*> handles;
    std::array<unsigned,128> map_tokens{}, handle_tokens{};
    unsigned next_map{}, next_handle{}, allocation{}, creation{}, init_count{}, shutdown_count{}, eval_count{};
    std::string failure, null_operation;
    bool out_on_failure{}, null_on_success{}, alias_map{}, alias_handle{};
    int available = 1, driver{};
    bool zero_init{};
    std::atomic<int> entries{0};
    struct Guard {
        Fixture& f;
        explicit Guard(const char* name) : f(*fixture) { assert(++f.entries == 1); f.events.emplace_back(name); }
        ~Guard() { assert(--f.entries == 0); }
    };
    struct Validator final : U::Validation {
        Fixture& f;
        U::Reason path_reason = U::Reason::none, context_reason = U::Reason::none;
        U::Reason foreign_reason = U::Reason::none, observed_reason = U::Reason::none;
        struct Resource { ID3D11Resource* pointer; ID3D11Device* device; U::Surface shape; };
        std::vector<Resource> resources;
        explicit Validator(Fixture& value) : f(value) {}
        U::Reason paths(const U::Paths& p) noexcept override {
            assert(p.provider_directory == L"C:\\signed-provider" && p.cache_directory == L"C:\\cache");
            return path_reason;
        }
        U::Reason context(ID3D11Device* d, ID3D11DeviceContext* c) noexcept override {
            if (!d || !c || c != opaque<ID3D11DeviceContext>(reinterpret_cast<std::uintptr_t>(d) + 10)) return U::Reason::invalid_context;
            return context_reason;
        }
        U::Reason before_init(const U::Paths&) noexcept override { return foreign_reason; }
        U::Reason observed_provider(const U::Paths& p, std::wstring& out) noexcept override {
            out = p.provider_directory + L"\\nvngx_dlss.dll"; return observed_reason;
        }
        temporal_eye::Lease hold_device(ID3D11Device* d) override {
            return {d, [this](const void*) { f.events.emplace_back("device_release"); }};
        }
        temporal_eye::Lease hold_context(ID3D11DeviceContext* c) override {
            return {c, [this](const void*) { f.events.emplace_back("context_release"); }};
        }
        bool surface(ID3D11Device* d, ID3D11Resource* r, U::Surface& out) noexcept override {
            for (const auto& s : resources) if (s.pointer == r && s.device == d) { out = s.shape; return true; }
            return false;
        }
        ID3D11Resource* add(ID3D11Device* d, U::Format format, bool uav = false, unsigned size = 64) {
            auto* pointer = opaque<ID3D11Resource>(1000 + resources.size());
            resources.push_back({pointer, d, {size, size, 1, 1, 1, 0, format, uav}});
            return pointer;
        }
    };
    std::shared_ptr<Validator> validation = std::make_shared<Validator>(*this);
    std::shared_ptr<int> scratch = std::make_shared<int>(7);
    Fixture() { assert(!fixture); fixture = this; }
    ~Fixture() { U::collect_retired(); assert(maps.empty() && handles.empty()); fixture = nullptr; }
    bool fails(const std::string& op) const { return failure == op; }
    NVSDK_NGX_Parameter* map() {
        auto* p = reinterpret_cast<NVSDK_NGX_Parameter*>(&map_tokens.at(next_map++));
        assert(maps.insert(p).second); return p;
    }
    NVSDK_NGX_Handle* handle() {
        auto* p = reinterpret_cast<NVSDK_NGX_Handle*>(&handle_tokens.at(next_handle++));
        assert(handles.insert(p).second); return p;
    }
    static NVSDK_NGX_Result NVSDK_CONV init(const char* project, NVSDK_NGX_EngineType engine,
        const char* version, const wchar_t* cache, ID3D11Device* device,
        const NVSDK_NGX_FeatureCommonInfo* info, NVSDK_NGX_Version api) {
        Guard g("init"); auto& f = *fixture; ++f.init_count; f.allocation = f.creation = 0;
        assert(std::strlen(project) == 36 && engine == NVSDK_NGX_ENGINE_TYPE_CUSTOM && version[0]);
        assert(std::wstring(cache) == L"C:\\cache" && device && api == NVSDK_NGX_Version_API);
        assert(info && info->PathListInfo.Length == 1 && std::wstring(info->PathListInfo.Path[0]) == L"C:\\signed-provider");
        return f.fails("init") ? bad : (f.zero_init ? static_cast<NVSDK_NGX_Result>(0) : ok);
    }
    static NVSDK_NGX_Result NVSDK_CONV capability(NVSDK_NGX_Parameter** out) {
        Guard g("capability"); auto& f = *fixture;
        const bool failure = f.fails("capability");
        if ((!failure && !f.null_on_success && f.null_operation != "capability") || (failure && f.out_on_failure)) *out = f.map();
        return failure ? bad : ok;
    }
    static NVSDK_NGX_Result NVSDK_CONV allocate(NVSDK_NGX_Parameter** out) {
        Guard g("allocate"); auto& f = *fixture; const auto eye = f.allocation++ % 2;
        const bool failure = f.fails("allocate" + std::to_string(eye));
        if ((!failure && !f.null_on_success && f.null_operation != "allocate" + std::to_string(eye)) || (failure && f.out_on_failure)) {
            if (eye == 1 && f.alias_map) *out = reinterpret_cast<NVSDK_NGX_Parameter*>(&f.map_tokens[f.next_map - 1]);
            else *out = f.map();
        }
        return failure ? bad : ok;
    }
    static NVSDK_NGX_Result NVSDK_CONV get_i(NVSDK_NGX_Parameter* p, const char* key, int* out) {
        Guard g(key); auto& f = *fixture; assert(f.maps.count(p)); *out = 0;
        if (f.fails(key)) return bad;
        if (std::strcmp(key, NVSDK_NGX_Parameter_SuperSampling_Available) == 0) *out = f.available;
        else if (std::strcmp(key, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver) == 0) *out = f.driver;
        else if (std::strcmp(key, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor) == 0) *out = 555;
        else if (std::strcmp(key, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor) == 0) *out = 42;
        else if (std::strcmp(key, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult) == 0) *out = static_cast<int>(ok);
        else assert(false);
        return ok;
    }
    static NVSDK_NGX_Result create(ID3D11DeviceContext* c, NVSDK_NGX_Handle** out,
        NVSDK_NGX_Parameter* p, NVSDK_NGX_DLSS_Create_Params* create) {
        Guard g("create"); auto& f = *fixture; const auto eye = f.creation++ % 2;
        assert(c && f.maps.count(p) && create->Feature.InWidth == 64 && create->Feature.InHeight == 64);
        assert(create->Feature.InTargetWidth == 64 && create->Feature.InTargetHeight == 64);
        assert(create->Feature.InPerfQualityValue == NVSDK_NGX_PerfQuality_Value_DLAA && !create->InEnableOutputSubrects);
        assert(create->InFeatureCreateFlags == (NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
            NVSDK_NGX_DLSS_Feature_Flags_DepthInverted | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure));
        const bool failure = f.fails("create" + std::to_string(eye));
        if ((!failure && !f.null_on_success && f.null_operation != "create" + std::to_string(eye)) || (failure && f.out_on_failure)) {
            if (eye == 1 && f.alias_handle) *out = reinterpret_cast<NVSDK_NGX_Handle*>(&f.handle_tokens[f.next_handle - 1]);
            else *out = f.handle();
        }
        return failure ? bad : ok;
    }
    std::vector<NVSDK_NGX_Handle*> eval_handles;
    std::vector<NVSDK_NGX_Parameter*> eval_maps;
    std::vector<int> resets;
    static NVSDK_NGX_Result evaluate(ID3D11DeviceContext* c, NVSDK_NGX_Handle* h,
        NVSDK_NGX_Parameter* p, NVSDK_NGX_D3D11_DLSS_Eval_Params* e) {
        Guard g("evaluate"); auto& f = *fixture; ++f.eval_count;
        assert(c && f.handles.count(h) && f.maps.count(p));
        f.eval_handles.push_back(h); f.eval_maps.push_back(p); f.resets.push_back(e->InReset);
        assert(e->Feature.pInColor && e->Feature.pInOutput != e->Feature.pInColor && e->pInDepth && e->pInMotionVectors);
        assert(e->InJitterOffsetX == .25f && e->InJitterOffsetY == -.25f);
        assert(e->InMVScaleX == -64 && e->InMVScaleY == -64);
        assert(e->InRenderSubrectDimensions.Width == 64 && e->InRenderSubrectDimensions.Height == 64);
        assert(e->InPreExposure == 1 && e->InExposureScale == 1 && !e->pInExposureTexture);
        assert(!e->pInTransparencyMask && !e->pInBiasCurrentColorMask && !e->pInMotionVectors3D);
        assert(e->InColorSubrectBase.X == 0 && e->InOutputSubrectBase.Y == 0);
        for (int spin = 0; spin < 8; ++spin) std::this_thread::yield();
        return f.fails("evaluate") ? bad : ok;
    }
    static NVSDK_NGX_Result NVSDK_CONV release(NVSDK_NGX_Handle* p) {
        Guard g("release"); auto& f = *fixture; assert(f.handles.erase(p) == 1); return f.fails("release") ? bad : ok;
    }
    static NVSDK_NGX_Result NVSDK_CONV destroy(NVSDK_NGX_Parameter* p) {
        Guard g("destroy"); auto& f = *fixture; assert(f.maps.erase(p) == 1); return f.fails("destroy") ? bad : ok;
    }
    static NVSDK_NGX_Result NVSDK_CONV shutdown(ID3D11Device* p) {
        Guard g("shutdown"); auto& f = *fixture; assert(p); ++f.shutdown_count; return f.fails("shutdown") ? bad : ok;
    }
    U::Calls calls() { return {init, capability, allocate, get_i, create, evaluate, release, destroy, shutdown}; }
    U owner() { return U::injected_for_cpu_tests(calls(), validation); }
    U::Eval eval(unsigned eye = 0, std::uintptr_t device_number = 11) {
        U::Eval e;
        auto* device = opaque<ID3D11Device>(device_number);
        e.eye = eye; e.frame = e.pair = 1; e.device_generation = e.dimension_generation = e.feature_generation = 1;
        e.context = opaque<ID3D11DeviceContext>(device_number + 10);
        e.colour = validation->add(device, U::Format::rgba16_float);
        e.depth = validation->add(device, U::Format::r32_float);
        e.motion = validation->add(device, U::Format::rg16_float);
        e.output = validation->add(device, U::Format::rgba16_float, true);
        auto d = desc(); e.motion_contract = d.motion; e.colour_contract = d.colour_contract;
        e.jitter = {.25f, -.25f, 0, 0, 1, 32, false}; e.frame_time_ms = 11;
        e.scratch_owner = scratch; return e;
    }
    static U::Desc desc() {
        U::Desc d; d.width = d.height = 64;
        d.device_generation = d.dimension_generation = d.feature_generation = 1;
        d.motion = {-64, -64, false, temporal_eye::MotionDirection::current_to_previous,
            temporal_eye::MotionUnits::uv, temporal_eye::MotionResolution::active_render};
        d.colour_contract = {temporal_eye::ColourDomain::native_scene, temporal_eye::ColourEncoding::linear,
            temporal_eye::ExposureSource::automatic, true, true, 1, 1, {}, {}};
        d.inverted_depth = true; return d;
    }
    static U::Paths paths() { return {L"C:\\signed-provider", L"C:\\cache"}; }
    static bool prepare(U& u, U::Desc d = desc(), std::uintptr_t device = 11) {
        return u.prepare(opaque<ID3D11Device>(device), opaque<ID3D11DeviceContext>(device + 10), d, paths());
    }
};
void failure_cleanup() {
    const char* failures[] = {"init", "capability", NVSDK_NGX_Parameter_SuperSampling_Available,
        NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, "allocate0", "create0", "allocate1", "create1"};
    for (const auto* operation : failures) for (bool owned_out : {false, true}) {
        Fixture f; auto u = f.owner(); f.failure = operation; f.out_on_failure = owned_out;
        assert(!Fixture::prepare(u) && !u.ready());
        assert(u.status().result == bad);
        const auto calls = f.events.size();
        assert(!Fixture::prepare(u) && f.events.size() == calls); // no per-frame init storm
        assert(f.maps.empty() && f.handles.empty());
        assert(f.shutdown_count == (f.failure == "init" ? 0u : 1u));
        f.failure.clear(); auto d = Fixture::desc(); ++d.retry_generation;
        assert(Fixture::prepare(u, d) && u.ready());
        u.disable();
        auto last = std::find(f.events.rbegin(), f.events.rend(), "shutdown");
        assert(last != f.events.rend());
        auto first_release = std::find(f.events.begin(), f.events.end(), "release");
        if (first_release != f.events.end()) {
            auto after_release = std::find(first_release, f.events.end(), "destroy");
            assert(after_release != f.events.end());
        }
    }
    for (int which = 0; which < 3; ++which) {
        Fixture f; auto u = f.owner();
        if (which == 0) f.null_on_success = true;
        if (which == 1) f.alias_map = true;
        if (which == 2) f.alias_handle = true;
        assert(!Fixture::prepare(u) && f.maps.empty() && f.handles.empty());
        assert(u.status().result == ok && f.shutdown_count == 1);
    }
    for (const char* operation : {"capability", "allocate0", "allocate1", "create0", "create1"}) {
        Fixture f; auto u = f.owner(); f.null_operation = operation;
        assert(!Fixture::prepare(u) && !u.ready() && u.status().result == ok);
        assert(f.maps.empty() && f.handles.empty() && f.shutdown_count == 1);
    }
    for (bool handles : {false, true}) {
        Fixture f; auto u = f.owner(); f.out_on_failure = true;
        f.failure = handles ? "create1" : "allocate1";
        f.alias_handle = handles; f.alias_map = !handles;
        assert(!Fixture::prepare(u) && u.status().result == bad);
        assert(f.maps.empty() && f.handles.empty() && f.shutdown_count == 1);
    }
    {
        Fixture f; auto u = f.owner(); assert(Fixture::prepare(u)); u.disable();
        const std::vector<std::string> cleanup(f.events.end() - 8, f.events.end());
        assert(cleanup == std::vector<std::string>({"release", "release", "destroy", "destroy",
            "destroy", "shutdown", "context_release", "device_release"}));
    }

}
void reasons_and_validation() {
    for (auto reason : {U::Reason::invalid_paths, U::Reason::missing_provider, U::Reason::provider_signature, U::Reason::cache_unwritable}) {
        Fixture f; auto u = f.owner(); f.validation->path_reason = reason;
        assert(!Fixture::prepare(u) && u.status().reason == reason && f.init_count == 0);
    }
    for (int which = 0; which < 8; ++which) {
        Fixture f; auto u = f.owner(); auto d = Fixture::desc();
        if (which == 0) f.driver = 1;
        if (which == 1) f.available = 0;
        if (which == 2) f.validation->context_reason = U::Reason::wrong_device;
        if (which == 3) d.width = 0;
        if (which == 4) d.motion.scale_x = 0;
        if (which == 5) f.validation->foreign_reason = U::Reason::coexistence_unsupported;
        if (which == 6) f.validation->observed_reason = U::Reason::provider_selection_unverified;
        if (which == 7) d.colour_contract.hdr = false;
        const U::Reason expected[] = {U::Reason::driver_update, U::Reason::unavailable, U::Reason::wrong_device,
            U::Reason::invalid_dimensions, U::Reason::invalid_conventions, U::Reason::coexistence_unsupported,
            U::Reason::provider_selection_unverified, U::Reason::invalid_conventions};
        assert(!Fixture::prepare(u, d) && !u.ready());
        assert(u.status().reason == expected[which]);
        assert(f.maps.empty() && f.handles.empty());
    }
    {
        Fixture f; auto u = f.owner(); f.zero_init = true;
        assert(!Fixture::prepare(u) && u.status().reason == U::Reason::init_failed);
        assert(static_cast<int>(u.status().result) == 0 && f.shutdown_count == 0);
    }
    Fixture f; auto u = f.owner(); f.failure = NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor;
    assert(Fixture::prepare(u));
    assert(u.status().driver_major_result == bad && u.status().driver_minor == 42);
    assert(u.status().observed_provider_verified && u.status().expected_provider == u.status().observed_provider);
    u.disable();
}
void histories_and_retirement() {
    Fixture f; auto u = f.owner(); assert(Fixture::prepare(u)); auto lease = u.lease();
    auto left = f.eval(0), right = f.eval(1);
    assert(U::evaluate(lease, left) && U::evaluate(lease, right));
    assert(f.eval_handles[0] != f.eval_handles[1] && f.eval_maps[0] != f.eval_maps[1]);
    ++left.frame; ++left.pair; assert(U::evaluate(lease, left));
    assert(f.resets == std::vector<int>({1,1,0}));
    U::reset_histories(lease); ++left.frame; ++left.pair; assert(U::evaluate(lease, left) && f.resets.back() == 1);
    assert(!U::confirm_gpu_retirement(lease));
    int scratch_destroyed = 0;
    auto extra = std::shared_ptr<const void>(new int, [&](const void* p) { ++scratch_destroyed; delete static_cast<const int*>(p); });
    // A different bundle within one generation is refused rather than losing old GPU storage.
    left.scratch_owner = extra; assert(!U::evaluate(lease, left)); left.scratch_owner = f.scratch;
    auto d = Fixture::desc(); ++d.feature_generation;
    assert(Fixture::prepare(u, d) && f.init_count == 1 && f.shutdown_count == 0);
    assert(!U::evaluate(lease, left));
    auto retained = lease.retained_owner();
    assert(U::confirm_gpu_retirement(lease)); lease = {};
    U::collect_retired(); assert(f.handles.size() == 4); // T1 job lease still holds the old pair.
    retained.reset(); U::collect_retired(); assert(f.handles.size() == 2 && f.shutdown_count == 0);
    extra.reset(); assert(scratch_destroyed == 1);
    u.disable(); assert(f.handles.empty() && f.maps.empty() && f.shutdown_count == 1);
}
void evaluation_failures_and_invalid_inputs() {
    for (unsigned eye = 0; eye < 2; ++eye) {
        Fixture f; auto u = f.owner(); assert(Fixture::prepare(u)); auto lease = u.lease(); auto in = f.eval(eye);
        f.failure = "evaluate"; U::Status status;
        assert(!U::evaluate(lease, in, &status) && status.reason == U::Reason::evaluate_failed && status.result == bad);
        f.failure.clear(); ++in.frame; ++in.pair;
        assert(U::evaluate(lease, in) && f.resets.back() == 1);
        u.disable(); assert(f.handles.size() == 2); // failed evaluation also requires GPU retirement
        assert(U::confirm_gpu_retirement(lease)); lease = {}; U::collect_retired();
    }
    for (int mutation = 0; mutation < 12; ++mutation) {
        Fixture f; auto u = f.owner(); assert(Fixture::prepare(u)); auto lease = u.lease(); auto in = f.eval();
        switch (mutation) {
        case 0: in.eye = 2; break;
        case 1: ++in.dimension_generation; break;
        case 2: ++in.feature_generation; break;
        case 3: ++in.device_generation; break;
        case 4: in.context = opaque<ID3D11DeviceContext>(22); break;
        case 5: in.output = in.colour; break;
        case 6: in.motion_contract.scale_x = 0; break;
        case 7: in.colour_contract.pre_exposure = 0; break;
        case 8: in.jitter.phase = 32; break;
        case 9: in.scratch_owner.reset(); break;
        case 10: ++f.validation->resources[0].shape.sample_count; break;
        case 11: f.validation->resources[0].device = opaque<ID3D11Device>(12); break;
        default: assert(false);
        }
        assert(!U::evaluate(lease, in) && f.eval_count == 0);
        u.disable(); lease = {}; U::collect_retired();
    }
}
void bounded_quarantine() {
    Fixture f; auto u = f.owner(); std::vector<U::Lease> leases;
    for (unsigned n = 1; n <= 8; ++n) {
        auto d = Fixture::desc(); d.feature_generation = n;
        assert(Fixture::prepare(u, d)); auto lease = u.lease(); auto in = f.eval(); in.feature_generation = n;
        assert(U::evaluate(lease, in)); leases.push_back(lease);
    }
    auto d = Fixture::desc(); d.feature_generation = 9;
    assert(!Fixture::prepare(u, d) && u.status().reason == U::Reason::registry_full);
    assert(f.shutdown_count == 0 && f.handles.size() == 16);
    for (auto& lease : leases) assert(U::confirm_gpu_retirement(lease));
    leases.clear(); U::collect_retired(); assert(f.handles.empty());
    ++d.retry_generation; assert(Fixture::prepare(u, d)); u.disable();
}
void shared_core_and_concurrency() {
    Fixture f; auto a = f.owner(), b = f.owner();
    assert(Fixture::prepare(a) && Fixture::prepare(b)); assert(f.init_count == 1);
    auto la = a.lease(), lb = b.lease(); auto ia = f.eval(), ib = f.eval(1);
    std::thread left([&] { for (unsigned n = 1; n <= 40; ++n) { ia.frame = ia.pair = n; assert(U::evaluate(la, ia)); } });
    std::thread right([&] { for (unsigned n = 1; n <= 40; ++n) { ib.frame = ib.pair = n; assert(U::evaluate(lb, ib)); } });
    left.join(); right.join(); assert(f.eval_count == 80);
    a.disable(); assert(f.shutdown_count == 0); assert(U::confirm_gpu_retirement(la)); la = {}; U::collect_retired();
    assert(f.shutdown_count == 0 && f.handles.size() == 2);
    b.disable(); assert(U::confirm_gpu_retirement(lb)); lb = {}; U::collect_retired();
    assert(f.shutdown_count == 1);
}
NVSDK_NGX_Result NVSDK_CONV alternate_get_i(NVSDK_NGX_Parameter* p, const char* key, int* out) {
    return Fixture::get_i(p, key, out);
}
void incompatible_shared_owner() {
    Fixture f; auto first = f.owner(); assert(Fixture::prepare(first));
    auto other_calls = f.calls(); other_calls.get_i = alternate_get_i;
    auto second = U::injected_for_cpu_tests(other_calls, f.validation);
    assert(!Fixture::prepare(second) && second.status().reason == U::Reason::owner_conflict);
    assert(first.ready() && f.init_count == 1 && f.shutdown_count == 0);
    second.disable(); first.disable(); assert(f.shutdown_count == 1);
}
void cleanup_failures_are_once() {
    for (const char* failure : {"release", "destroy", "shutdown"}) {
        Fixture f; auto u = f.owner(); assert(Fixture::prepare(u)); f.failure = failure;
        u.disable(); assert(f.maps.empty() && f.handles.empty() && f.shutdown_count == 1);
        assert(u.status().cleanup_result == bad); u.disable(); assert(f.shutdown_count == 1);
    }
}
}
int main() {
    static_assert(static_cast<int>(NVSDK_NGX_PerfQuality_Value_DLAA) == 5, "pinned SDK DLAA ABI");
    failure_cleanup(); reasons_and_validation(); histories_and_retirement();
    evaluation_failures_and_invalid_inputs(); bounded_quarantine(); shared_core_and_concurrency(); incompatible_shared_owner(); cleanup_failures_are_once();
    std::cout << "dlss_lifecycle_check: passed (injected CPU calls only)\n";
}
