#include "dlss_eye_upscaler.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <iterator>
#include <map>
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
    NVSDK_NGX_Parameter* external_map{};
    NVSDK_NGX_Handle* external_handle{};
    std::string external_at;
    // Parameter map contents for the optimal-settings callback (per map, per key).
    std::map<std::pair<NVSDK_NGX_Parameter*,std::string>,long long> values;
    bool no_optimal_callback{}, optimal_fails{}, optimal_zero{};
    unsigned optimal_calls{};
    // Expected create parameters (DLAA at 64x64 unless a test changes them).
    unsigned expect_render_w = 64, expect_render_h = 64, expect_target_w = 64, expect_target_h = 64;
    NVSDK_NGX_PerfQuality_Value expect_quality = NVSDK_NGX_PerfQuality_Value_DLAA;
    int expect_flags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_DepthInverted | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    unsigned expect_subrect_w = 64, expect_subrect_h = 64;
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
        ID3D11DeviceContext* extra_context{};
        explicit Validator(Fixture& value) : f(value) {}
        U::Reason paths(const U::Paths& p) noexcept override {
            assert(p.provider_directory == L"C:\\signed-provider" && p.cache_directory == L"C:\\cache");
            return path_reason;
        }
        U::Reason context(ID3D11Device* d, ID3D11DeviceContext* c) noexcept override {
            if (!d || !c || (c != opaque<ID3D11DeviceContext>(reinterpret_cast<std::uintptr_t>(d) + 10) && c != extra_context)) return U::Reason::invalid_context;
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
        if ((!failure && !f.null_on_success && f.null_operation != "capability") || (failure && f.out_on_failure))
            *out = f.external_at == "capability" ? f.external_map : f.map();
        return failure ? bad : ok;
    }
    static NVSDK_NGX_Result NVSDK_CONV allocate(NVSDK_NGX_Parameter** out) {
        Guard g("allocate"); auto& f = *fixture; const auto eye = f.allocation++ % 2;
        const bool failure = f.fails("allocate" + std::to_string(eye));
        if ((!failure && !f.null_on_success && f.null_operation != "allocate" + std::to_string(eye)) || (failure && f.out_on_failure)) {
            if (f.external_at == "allocate" + std::to_string(eye)) *out = f.external_map;
            else if (eye == 1 && f.alias_map) *out = reinterpret_cast<NVSDK_NGX_Parameter*>(&f.map_tokens[f.next_map - 1]);
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
        assert(c && f.maps.count(p) && create->Feature.InWidth == f.expect_render_w && create->Feature.InHeight == f.expect_render_h);
        assert(create->Feature.InTargetWidth == f.expect_target_w && create->Feature.InTargetHeight == f.expect_target_h);
        assert(create->Feature.InPerfQualityValue == f.expect_quality && !create->InEnableOutputSubrects);
        assert(create->InFeatureCreateFlags == f.expect_flags);
        const bool failure = f.fails("create" + std::to_string(eye));
        if ((!failure && !f.null_on_success && f.null_operation != "create" + std::to_string(eye)) || (failure && f.out_on_failure)) {
            if (f.external_at == "create" + std::to_string(eye)) *out = f.external_handle;
            else if (eye == 1 && f.alias_handle) *out = reinterpret_cast<NVSDK_NGX_Handle*>(&f.handle_tokens[f.next_handle - 1]);
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
        assert(e->InRenderSubrectDimensions.Width == f.expect_subrect_w && e->InRenderSubrectDimensions.Height == f.expect_subrect_h);
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
    static void NVSDK_CONV set_ui(NVSDK_NGX_Parameter* p, const char* key, unsigned value) {
        Guard g("set_ui"); auto& f = *fixture; assert(f.maps.count(p)); f.values[{p, key}] = value;
    }
    static void NVSDK_CONV set_i(NVSDK_NGX_Parameter* p, const char* key, int value) {
        Guard g("set_i"); auto& f = *fixture; assert(f.maps.count(p)); f.values[{p, key}] = value;
    }
    static NVSDK_NGX_Result NVSDK_CONV get_ui(NVSDK_NGX_Parameter* p, const char* key, unsigned* out) {
        Guard g("get_ui"); auto& f = *fixture; assert(f.maps.count(p));
        const auto found = f.values.find({p, key}); if (found == f.values.end()) { *out = 0; return bad; }
        *out = static_cast<unsigned>(found->second); return ok;
    }
    // The SDK's optimal-settings callback: Quality renders at 2/3, DLAA at the output size; dynamic range 1/2..1.
    static NVSDK_NGX_Result NVSDK_CONV optimal_callback(NVSDK_NGX_Parameter* p) {
        auto& f = *fixture; ++f.optimal_calls; assert(f.maps.count(p));
        const auto w = f.values.at({p, NVSDK_NGX_Parameter_Width}), h = f.values.at({p, NVSDK_NGX_Parameter_Height});
        const auto q = f.values.at({p, NVSDK_NGX_Parameter_PerfQualityValue});
        assert(f.values.at({p, NVSDK_NGX_Parameter_RTXValue}) == 0);
        if (f.optimal_fails) return bad;
        const bool dlaa = q == NVSDK_NGX_PerfQuality_Value_DLAA;
        const long long ow = f.optimal_zero ? 0 : dlaa ? w : w * 2 / 3, oh = f.optimal_zero ? 0 : dlaa ? h : h * 2 / 3;
        f.values[{p, NVSDK_NGX_Parameter_OutWidth}] = ow; f.values[{p, NVSDK_NGX_Parameter_OutHeight}] = oh;
        f.values[{p, NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Width}] = w; f.values[{p, NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Height}] = h;
        f.values[{p, NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width}] = w / 2; f.values[{p, NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height}] = h / 2;
        return ok;
    }
    static NVSDK_NGX_Result NVSDK_CONV get_void_pointer(NVSDK_NGX_Parameter* p, const char* key, void** out) {
        Guard g("get_void_pointer"); auto& f = *fixture; assert(f.maps.count(p));
        assert(std::strcmp(key, NVSDK_NGX_Parameter_DLSSOptimalSettingsCallback) == 0);
        *out = f.no_optimal_callback ? nullptr : reinterpret_cast<void*>(&optimal_callback); return ok;
    }
    U::Calls calls() { return {init, capability, allocate, get_i, create, evaluate, release, destroy, shutdown, set_ui, set_i, get_ui, get_void_pointer}; }
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
void aliases_across_live_generations() {
    for (bool handles : {false, true}) for (bool failure : {false, true}) for (bool other_facade : {false, true}) {
        Fixture f; auto first = f.owner(); auto second = f.owner();
        assert(Fixture::prepare(first)); auto old = first.lease(); auto in = f.eval();
        assert(U::evaluate(old, in)); // Must not free these objects before real retirement.
        const auto old_maps = f.maps; const auto old_handles = f.handles;
        f.external_map = *std::next(f.maps.begin()); // Existing left-eye map, not the capability.
        f.external_handle = *f.handles.begin();
        f.external_at = handles ? "create0" : "allocate0";
        f.failure = failure ? f.external_at : ""; f.out_on_failure = failure;
        auto next = Fixture::desc(); ++next.feature_generation;
        auto& target = other_facade ? second : first;
        assert(!Fixture::prepare(target, next));
        assert(target.status().result == (failure ? bad : ok));
        assert(target.status().reason == (failure ? (handles ? U::Reason::create_failed : U::Reason::allocate_failed) : U::Reason::duplicate_history));
        assert(f.maps == old_maps && f.handles == old_handles && f.shutdown_count == 0);
        f.external_at.clear(); f.failure.clear();
        if (other_facade) { ++in.frame; ++in.pair; assert(U::evaluate(old, in)); }
        first.disable(); second.disable();
        assert(f.maps == old_maps && f.handles == old_handles);
        assert(U::confirm_gpu_retirement(old)); old = {}; U::collect_retired();
        assert(f.maps.empty() && f.handles.empty() && f.shutdown_count == 1);
    }
    // A newly initialized device's capability out pointer is also untrusted.
    for (bool eye_map : {false, true}) for (bool failure : {false, true}) {
        Fixture f; auto first = f.owner(); auto second = f.owner();
        assert(Fixture::prepare(first)); auto old = first.lease(); auto in = f.eval();
        assert(U::evaluate(old, in));
        const auto old_maps = f.maps; const auto old_handles = f.handles;
        f.external_map = eye_map ? *std::next(f.maps.begin()) : *f.maps.begin();
        f.external_at = "capability"; f.failure = failure ? "capability" : ""; f.out_on_failure = failure;
        assert(!Fixture::prepare(second, Fixture::desc(), 12));
        assert(second.status().result == (failure ? bad : ok));
        assert(second.status().reason == (failure ? U::Reason::capability_failed : U::Reason::duplicate_history));
        assert(f.maps == old_maps && f.handles == old_handles && f.shutdown_count == 1); // Only new device closes.
        first.disable(); second.disable(); assert(U::confirm_gpu_retirement(old)); old = {}; U::collect_retired();
        assert(f.maps.empty() && f.handles.empty() && f.shutdown_count == 2);
    }
}
void repeated_prepare_context_validation() {
    {
        Fixture f; auto u = f.owner(); assert(Fixture::prepare(u));
        const auto events = f.events.size();
        for (auto* invalid : {static_cast<ID3D11DeviceContext*>(nullptr), opaque<ID3D11DeviceContext>(22)}) {
            assert(!u.prepare(opaque<ID3D11Device>(11), invalid, Fixture::desc(), Fixture::paths()));
            assert(u.status().reason == U::Reason::invalid_context && !u.ready() && f.events.size() == events);
        }
        f.validation->context_reason = U::Reason::wrong_device;
        assert(!Fixture::prepare(u) && u.status().reason == U::Reason::wrong_device && f.events.size() == events);
        f.validation->context_reason = U::Reason::none;
        f.validation->extra_context = opaque<ID3D11DeviceContext>(31); // Another context accepted by validation.
        assert(!u.prepare(opaque<ID3D11Device>(11), f.validation->extra_context, Fixture::desc(), Fixture::paths()));
        assert(!u.ready() && f.events.size() == events); // Must still match the retained Core's context.
        assert(Fixture::prepare(u) && u.ready() && f.events.size() == events);
        u.disable();
    }
    {
        Fixture f; auto u = f.owner();
        assert(!u.prepare(opaque<ID3D11Device>(11), nullptr, Fixture::desc(), Fixture::paths()));
        assert(f.init_count == 0 && Fixture::prepare(u) && f.init_count == 1); // Correction is eligible.
        u.disable();
    }
    {
        Fixture f; auto u = f.owner(); f.failure = "create0";
        assert(!Fixture::prepare(u)); const auto events = f.events.size();
        assert(!u.prepare(opaque<ID3D11Device>(11), nullptr, Fixture::desc(), Fixture::paths()));
        assert(!Fixture::prepare(u) && f.events.size() == events);
        assert(u.status().reason == U::Reason::create_failed && u.status().result == bad); // Original failure survived.
    }
}
void cleanup_failures_are_once() {
    for (const char* failure : {"release", "destroy", "shutdown"}) {
        Fixture f; auto u = f.owner(); assert(Fixture::prepare(u)); f.failure = failure;
        u.disable(); assert(f.maps.empty() && f.handles.empty() && f.shutdown_count == 1);
        assert(u.status().cleanup_result == bad); u.disable(); assert(f.shutdown_count == 1);
    }
}
void retire_keeps_core() {
    Fixture f; auto u = f.owner(); assert(Fixture::prepare(u)); auto lease = u.lease(); auto in = f.eval();
    assert(U::evaluate(lease, in));
    u.retire(); // a mode switch or resize: the pair retires, NGX stays initialized
    assert(!u.ready() && f.shutdown_count == 0 && f.handles.size() == 2);
    assert(U::confirm_gpu_retirement(lease)); lease = {}; U::collect_retired();
    assert(f.handles.empty() && f.shutdown_count == 0 && f.init_count == 1);
    auto d = Fixture::desc(); ++d.feature_generation;
    assert(Fixture::prepare(u, d) && u.ready() && f.init_count == 1 && f.handles.size() == 2); // no re-init, no coexistence refusal
    auto again = u.lease(); in.feature_generation = d.feature_generation; ++in.frame; ++in.pair;
    assert(U::evaluate(again, in) && f.resets.back() == 1);
    u.disable(); assert(f.shutdown_count == 0); // the live job lease still owns the Core
    assert(U::confirm_gpu_retirement(again)); again = {}; U::collect_retired();
    assert(f.handles.empty() && f.maps.empty() && f.shutdown_count == 1);
}
// The seam's session-end and owner-rebuild flows against the real registry: a retired pair whose EVENT ticket lease is
// still held (D3D11TemporalOwner slot) is abandoned at session end but reclaimed only once that lease drops; an owner
// rebuild drops the tickets with the owner, so abandonment frees the slot; repeated sessions never fill the 8 slots.
void session_end_cycles() {
    Fixture f; auto u = f.owner();
    for (unsigned session = 1; session <= 10; ++session) {
        auto d = Fixture::desc(); d.feature_generation = session;
        assert(Fixture::prepare(u, d) && u.ready()); auto lease = u.lease(); auto in = f.eval(); in.feature_generation = session;
        assert(U::evaluate(lease, in));                     // GPU work attempted: the pair is quarantined until proven complete
        auto ticket = lease.retained_owner();               // the EVENT ticket's generation lease, held by the owner's slot
        u.retire();                                         // session end on the stop thread: retire only, no NGX work
        assert(U::confirm_gpu_retirement(lease));           // the ledger abandons the ticket (next executing-thread dispatch)
        lease = {}; U::collect_retired();
        assert(f.handles.size() == 2);                      // still held through the ticket lease: never reclaimed early
        ticket.reset(); U::collect_retired();               // the owner polls (or is dropped): the ticket lease drops
        assert(f.handles.empty() && f.maps.size() == 1);    // slot reclaimed only now; the capability map (Core) stays
    }
    assert(f.init_count == 1 && f.shutdown_count == 0);    // ten sessions, one NGX init, no registry_full
    {   // owner rebuild (new device generation) with a pending ticket: the old Core is replaced once no pair holds it;
        // the dropped owner releases its ticket lease; abandonment then frees the slot
        auto d = Fixture::desc(); d.feature_generation = 11; ++d.device_generation;
        assert(Fixture::prepare(u, d) && f.init_count == 2 && f.shutdown_count == 1);
        auto lease = u.lease(); auto in = f.eval(); in.feature_generation = 11; in.device_generation = d.device_generation;
        assert(U::evaluate(lease, in)); auto ticket = lease.retained_owner();
        u.retire(); assert(U::confirm_gpu_retirement(lease)); lease = {}; U::collect_retired();
        assert(f.handles.size() == 2);                      // the pending ticket still owns it
        ticket.reset(); U::collect_retired(); assert(f.handles.empty());
    }
    auto d = Fixture::desc(); d.feature_generation = 12; d.device_generation = 2; // a later session still prepares: nothing stranded
    assert(Fixture::prepare(u, d) && u.ready() && u.status().reason == U::Reason::none && f.init_count == 2);
    u.disable(); assert(f.handles.empty() && f.maps.empty() && f.shutdown_count == 2);
}
void optimal_settings() {
    {   // Quality for a 1920x1080 output: 2/3 render size inside the dynamic range; the Core is shared with prepare.
        Fixture f; auto u = f.owner(); U::OptimalSettings out; NVSDK_NGX_Result result{};
        assert(u.optimal_settings(opaque<ID3D11Device>(11), opaque<ID3D11DeviceContext>(21), Fixture::paths(), 1, 1920, 1080, U::Quality::quality, out, &result) == U::Reason::none);
        assert(out.optimal_width == 1280 && out.optimal_height == 720 && out.max_width == 1920 && out.min_width == 960 && out.min_height == 540);
        assert(f.optimal_calls == 1 && f.init_count == 1 && result == ok);
        assert(u.optimal_settings(opaque<ID3D11Device>(11), opaque<ID3D11DeviceContext>(21), Fixture::paths(), 1, 1920, 1080, U::Quality::dlaa, out) == U::Reason::none);
        assert(out.optimal_width == 1920 && out.optimal_height == 1080 && f.init_count == 1);
        assert(Fixture::prepare(u) && f.init_count == 1); // no second init for the same device
        u.disable(); assert(f.shutdown_count == 1);
    }
    {   // Provider without the callback, a failing callback and a zero optimal size are distinct, feature-free failures.
        for (int which = 0; which < 3; ++which) {
            Fixture f; auto u = f.owner(); U::OptimalSettings out;
            if (which == 0) f.no_optimal_callback = true;
            if (which == 1) f.optimal_fails = true;
            if (which == 2) f.optimal_zero = true;
            const auto reason = u.optimal_settings(opaque<ID3D11Device>(11), opaque<ID3D11DeviceContext>(21), Fixture::paths(), 1, 1920, 1080, U::Quality::quality, out);
            assert(reason == (which == 0 ? U::Reason::optimal_unsupported : U::Reason::optimal_failed));
            assert(f.handles.empty() && !u.ready());
            u.disable(); assert(f.shutdown_count == 1);
        }
        Fixture f; auto u = f.owner(); U::OptimalSettings out;
        assert(u.optimal_settings(opaque<ID3D11Device>(11), nullptr, Fixture::paths(), 1, 1920, 1080, U::Quality::quality, out) == U::Reason::invalid_context);
        assert(u.optimal_settings(opaque<ID3D11Device>(11), opaque<ID3D11DeviceContext>(21), Fixture::paths(), 1, 0, 1080, U::Quality::quality, out) == U::Reason::invalid_dimensions);
        f.validation->path_reason = U::Reason::missing_provider;
        assert(u.optimal_settings(opaque<ID3D11Device>(11), opaque<ID3D11DeviceContext>(21), Fixture::paths(), 1, 1920, 1080, U::Quality::quality, out) == U::Reason::missing_provider);
        assert(f.init_count == 0);
    }
}
void quality_dimensions_and_subrects() {
    {   // A Quality feature renders 48x48 into 64x64 with render-resolution, jittered motion vectors.
        Fixture f; auto u = f.owner(); auto d = Fixture::desc();
        d.render_width = d.render_height = 48; d.quality = U::Quality::quality; d.mv_low_resolution = true; d.motion.jittered = true;
        f.expect_render_w = f.expect_render_h = 48; f.expect_quality = NVSDK_NGX_PerfQuality_Value_MaxQuality;
        f.expect_flags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_MVJittered;
        f.expect_subrect_w = f.expect_subrect_h = 48;
        assert(Fixture::prepare(u, d)); auto lease = u.lease();
        // Inputs may be the engine's larger textures with the render subrect at the top left; the output must match exactly.
        auto in = f.eval(); in.motion_contract = d.motion;
        f.validation->resources[0].shape.width = f.validation->resources[0].shape.height = 64; // colour 64 >= 48
        assert(U::evaluate(lease, in) && f.eval_count == 1);
        f.validation->resources[3].shape.width = 72; // output larger than the target
        assert(!U::evaluate(lease, in) && f.eval_count == 1);
        f.validation->resources[3].shape.width = 64; f.validation->resources[1].shape.width = 40; // depth smaller than the render size
        assert(!U::evaluate(lease, in) && f.eval_count == 1);
        u.disable(); assert(U::confirm_gpu_retirement(lease)); lease = {}; U::collect_retired();
    }
    for (int which = 0; which < 4; ++which) { // inconsistent dimension contracts never reach the SDK
        Fixture f; auto u = f.owner(); auto d = Fixture::desc();
        if (which == 0) { d.render_width = 48; } // DLAA must render at the output size
        if (which == 1) { d.quality = U::Quality::quality; } // Quality must render below it
        if (which == 2) { d.render_width = 80; d.quality = U::Quality::quality; } // never above it
        if (which == 3) { d.width = 48; d.height = 16; } // below the 64x32 output minimum
        assert(!Fixture::prepare(u, d) && u.status().reason == U::Reason::invalid_dimensions && f.init_count == 0);
    }
    {   // An incomplete call table (missing parameter access) is refused before any SDK call.
        Fixture f; auto partial = f.calls(); partial.get_void_pointer = nullptr;
        auto u = U::injected_for_cpu_tests(partial, f.validation);
        assert(!Fixture::prepare(u) && u.status().reason == U::Reason::invalid_calls && f.init_count == 0);
    }
}
}
int main() {
    static_assert(static_cast<int>(NVSDK_NGX_PerfQuality_Value_DLAA) == 5, "pinned SDK DLAA ABI");
    failure_cleanup(); reasons_and_validation(); histories_and_retirement();
    evaluation_failures_and_invalid_inputs(); bounded_quarantine(); shared_core_and_concurrency(); incompatible_shared_owner(); aliases_across_live_generations(); repeated_prepare_context_validation(); cleanup_failures_are_once();
    retire_keeps_core(); session_end_cycles(); optimal_settings(); quality_dimensions_and_subrects();
    std::cout << "dlss_lifecycle_check: passed (injected CPU calls only)\n";
}
