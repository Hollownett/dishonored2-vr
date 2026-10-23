#pragma once
#include "temporal_eye_job.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <wintrust.h>
#include <softpub.h>
#include <tlhelp32.h>
#include <filesystem>
#include <cwctype>
#pragma comment(lib,"wintrust.lib")
#pragma comment(lib,"crypt32.lib")
#pragma comment(lib,"advapi32.lib")
#endif
#ifdef _MSC_VER
#pragma warning(push,0)
#endif
#include "../third_party/dlss/include/nvsdk_ngx_helpers_d3d.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace d2vr {
// The vendor macro compares an unsigned mask to a signed enum in clang-cl.
// Isolate that vendor diagnostic without weakening warnings for our implementation.
#ifdef _MSC_VER
#pragma warning(push,0)
#endif
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-compare"
#endif
inline bool ngx_failed(NVSDK_NGX_Result result) noexcept { return NVSDK_NGX_FAILED(result); }
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif
// No renderer takeover occurs here. All public SDK calls and helper parameter
// mutations run under the integration-wide mutex. Never invoke from a deferred context.
class DlssEyeUpscaler {
public:
    struct Calls {
        decltype(&NVSDK_NGX_D3D11_Init_with_ProjectID) init{};
        decltype(&NVSDK_NGX_D3D11_GetCapabilityParameters) capability{};
        decltype(&NVSDK_NGX_D3D11_AllocateParameters) allocate{};
        decltype(&NVSDK_NGX_Parameter_GetI) get_i{};
        decltype(&NGX_D3D11_CREATE_DLSS_EXT) create{};
        decltype(&NGX_D3D11_EVALUATE_DLSS_EXT) evaluate{};
        decltype(&NVSDK_NGX_D3D11_ReleaseFeature) release{};
        decltype(&NVSDK_NGX_D3D11_DestroyParameters) destroy{};
        decltype(&NVSDK_NGX_D3D11_Shutdown1) shutdown{};
        // Parameter map access for the optimal-settings callback (the SDK's NGX_DLSS_GET_OPTIMAL_SETTINGS helper
        // is not in the pinned header closure; the same calls are made here).
        decltype(&NVSDK_NGX_Parameter_SetUI) set_ui{};
        decltype(&NVSDK_NGX_Parameter_SetI) set_i{};
        decltype(&NVSDK_NGX_Parameter_GetUI) get_ui{};
        decltype(&NVSDK_NGX_Parameter_GetVoidPointer) get_void_pointer{};
        bool same_as(const Calls& other) const noexcept {
            return init == other.init && capability == other.capability && allocate == other.allocate &&
                get_i == other.get_i && create == other.create && evaluate == other.evaluate &&
                release == other.release && destroy == other.destroy && shutdown == other.shutdown &&
                set_ui == other.set_ui && set_i == other.set_i && get_ui == other.get_ui && get_void_pointer == other.get_void_pointer;
        }
        bool complete() const noexcept {
            return init && capability && allocate && get_i && create && evaluate && release && destroy && shutdown &&
                set_ui && set_i && get_ui && get_void_pointer;
        }
#ifdef _WIN32
        static Calls production() noexcept {
            return {&NVSDK_NGX_D3D11_Init_with_ProjectID, &NVSDK_NGX_D3D11_GetCapabilityParameters,
                &NVSDK_NGX_D3D11_AllocateParameters, &NVSDK_NGX_Parameter_GetI,
                &NGX_D3D11_CREATE_DLSS_EXT, &NGX_D3D11_EVALUATE_DLSS_EXT,
                &NVSDK_NGX_D3D11_ReleaseFeature, &NVSDK_NGX_D3D11_DestroyParameters,
                &NVSDK_NGX_D3D11_Shutdown1, &NVSDK_NGX_Parameter_SetUI, &NVSDK_NGX_Parameter_SetI,
                &NVSDK_NGX_Parameter_GetUI, &NVSDK_NGX_Parameter_GetVoidPointer};
        }
#endif
    };
    // The SDK's optimal-settings callback signature (nvsdk_ngx_helpers.h PFN_NVSDK_NGX_DLSS_GetOptimalSettingsCallback).
    using OptimalSettingsCallback = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter*);
    enum class Reason { none, disabled, invalid_calls, invalid_paths, missing_provider, provider_signature,
        cache_unwritable, provider_selection_unverified, coexistence_unsupported, invalid_context, wrong_device, invalid_dimensions, invalid_conventions,
        registry_full, owner_conflict, retry_suppressed, init_failed, capability_failed, capability_query_failed,
        unavailable, driver_update, allocate_failed, create_failed, duplicate_history, invalid_resource,
        stale_generation, retired, evaluate_failed, optimal_unsupported, optimal_failed, count };
    static const char* reason_name(Reason reason) noexcept {
        static constexpr const char* names[] = {"none", "disabled", "invalid_calls", "invalid_paths", "missing_provider", "provider_signature",
            "cache_unwritable", "provider_selection_unverified", "coexistence_unsupported", "invalid_context", "wrong_device", "invalid_dimensions", "invalid_conventions",
            "registry_full", "owner_conflict", "retry_suppressed", "init_failed", "capability_failed", "capability_query_failed",
            "unavailable", "driver_update", "allocate_failed", "create_failed", "duplicate_history", "invalid_resource",
            "stale_generation", "retired", "evaluate_failed", "optimal_unsupported", "optimal_failed"};
        static_assert(sizeof names / sizeof names[0] == static_cast<unsigned>(Reason::count), "reason names");
        const auto index = static_cast<unsigned>(reason);
        return index < static_cast<unsigned>(Reason::count) ? names[index] : "unknown";
    }
    enum class Quality { dlaa, quality }; // NVSDK_NGX_PerfQuality_Value_DLAA / _MaxQuality
    struct OptimalSettings { unsigned optimal_width{}, optimal_height{}, max_width{}, max_height{}, min_width{}, min_height{}; };
    struct Status {
        Reason reason = Reason::disabled;
        NVSDK_NGX_Result result = NVSDK_NGX_Result_Success;
        NVSDK_NGX_Result cleanup_result = NVSDK_NGX_Result_Success;
        NVSDK_NGX_Result driver_major_result = NVSDK_NGX_Result_Success;
        NVSDK_NGX_Result driver_minor_result = NVSDK_NGX_Result_Success;
        NVSDK_NGX_Result feature_init_query_result = NVSDK_NGX_Result_Success;
        int eye = -1, driver_major{}, driver_minor{}, feature_init_result{};
        std::wstring expected_provider, observed_provider;
        bool observed_provider_verified{};
    };
    enum class Format { rgba16_float, rgba8_unorm, r11g11b10_float, r32_float, rg16_float };
    struct Desc {
        unsigned width{}, height{};               // output (display) size of each eye
        unsigned render_width{}, render_height{}; // input size; 0 = same as the output (DLAA)
        Quality quality = Quality::dlaa;          // DLAA needs render == output; Quality takes the optimal-settings render size
        Format colour = Format::rgba16_float, output = Format::rgba16_float;
        std::uint64_t device_generation{}, dimension_generation{}, feature_generation{}, retry_generation{};
        temporal_eye::JitterContract jitter;
        temporal_eye::MotionContract motion;
        temporal_eye::ColourContract colour_contract;
        bool inverted_depth{}, mv_low_resolution{};
    };
    struct Paths { std::wstring provider_directory, cache_directory; };
    struct Surface {
        unsigned width{}, height{}, mip_levels{}, array_size{}, sample_count{}, sample_quality{};
        Format format = Format::rgba16_float;
        bool unordered_access{};
    };
    // Explicit test injection. Production factory always constructs NativeValidation
    // and production Calls; no environment flag or silent context/resource bypass.
    struct Validation {
        virtual ~Validation() = default;
        virtual Reason paths(const Paths&) noexcept = 0;
        virtual Reason context(ID3D11Device*, ID3D11DeviceContext*) noexcept = 0;
        virtual Reason before_init(const Paths&) noexcept = 0;
        virtual Reason observed_provider(const Paths&, std::wstring&) noexcept = 0;
        virtual temporal_eye::Lease hold_device(ID3D11Device*) = 0;
        virtual temporal_eye::Lease hold_context(ID3D11DeviceContext*) = 0;
        virtual bool surface(ID3D11Device*, ID3D11Resource*, Surface&) noexcept = 0;
    };
    struct Eval {
        unsigned eye{};
        std::uint64_t frame{}, pair{}, device_generation{}, dimension_generation{}, feature_generation{};
        ID3D11DeviceContext* context{};
        ID3D11Resource *colour{}, *depth{}, *motion{}, *output{}, *exposure{};
        temporal_eye::JitterContract jitter;
        temporal_eye::MotionContract motion_contract;
        temporal_eye::ColourContract colour_contract;
        float frame_time_ms{};
        bool discontinuity{};
        // T3 binds this to its generation's retained scratch bundle, and retains
        // all job resources through temporal_eye::RetainedJobs GPU retirement.
        temporal_eye::Lease scratch_owner;
    };
private:
    static std::mutex& mutex() { static auto* m = new std::mutex; return *m; }
    static NVSDK_NGX_Result& cleanup_failure() { static auto value = NVSDK_NGX_Result_Success; return value; }
    static void cleanup_result(NVSDK_NGX_Result r) noexcept {
        if (ngx_failed(r) || r != NVSDK_NGX_Result_Success) cleanup_failure() = r;
    }
    struct Core {
        Calls calls;
        std::shared_ptr<Validation> validation;
        ID3D11Device* device{};
        ID3D11DeviceContext* context{};
        temporal_eye::Lease device_lease, context_lease;
        Paths paths;
        const wchar_t* search[1]{};
        NVSDK_NGX_FeatureCommonInfo common{};
        NVSDK_NGX_Parameter* capability{};
        bool initialized{};
        std::uint64_t device_generation{};
        Status status;
        // Private owners die only under mutex: partial init, registry collection,
        // or facade disable. No external Core ownership is exposed.
        ~Core() {
            if (capability) {
                const auto r = calls.destroy(capability); capability = nullptr;
                if (ngx_failed(r) || r != NVSDK_NGX_Result_Success) status.cleanup_result = r;
                cleanup_result(r);
            }
            if (initialized) {
                const auto r = calls.shutdown(device); initialized = false;
                if (ngx_failed(r) || r != NVSDK_NGX_Result_Success) status.cleanup_result = r;
                cleanup_result(r);
            }
        }
    };
    struct Eye {
        NVSDK_NGX_Parameter* parameters{};
        NVSDK_NGX_Handle* handle{};
        bool reset = true;
        std::uint64_t frame{}, pair{};
    };
    struct Pair {
        std::shared_ptr<Core> core;
        Desc desc;
        std::array<Eye,2> eyes{};
        temporal_eye::Lease scratch_owner;
        std::uint64_t serial{};
        bool retired{}, attempted_gpu{}, gpu_safe{};
        Status status;
        // SDK handles first, ALL eye maps second, Core capability/shutdown last.
        ~Pair() {
            for (auto& e : eyes) if (e.handle) {
                const auto r = core->calls.release(e.handle); e.handle = nullptr;
                if (ngx_failed(r) || r != NVSDK_NGX_Result_Success) status.cleanup_result = r;
                cleanup_result(r);
            }
            for (auto& e : eyes) if (e.parameters) {
                const auto r = core->calls.destroy(e.parameters); e.parameters = nullptr;
                if (ngx_failed(r) || r != NVSDK_NGX_Result_Success) status.cleanup_result = r;
                cleanup_result(r);
            }
        }
    };
public:
    class Lease {
        friend class DlssEyeUpscaler;
        std::shared_ptr<Pair> pair_;
        explicit Lease(std::shared_ptr<Pair> pair) : pair_(std::move(pair)) {}
    public:
        Lease() = default;
        explicit operator bool() const noexcept { return bool(pair_); }
        temporal_eye::Lease retained_owner() const noexcept { return pair_; }
        std::uint64_t generation() const noexcept { return pair_ ? pair_->serial : 0; }
    };
private:
    static auto& pairs() { static auto* a = new std::array<std::shared_ptr<Pair>,8>; return *a; }
    static auto& cores() { static auto* a = new std::array<std::weak_ptr<Core>,8>; return *a; }
    static std::uint64_t next_serial() noexcept { static std::uint64_t n{}; return ++n; }
    Calls calls_;
    std::shared_ptr<Validation> validation_;
    std::shared_ptr<Core> core_;
    std::shared_ptr<Pair> active_;
    Status status_, cached_status_;
    Desc last_request_;
    Paths last_paths_;
    ID3D11Device* last_device_{};
    ID3D11DeviceContext* last_context_{};
    bool attempted_{};
    static unsigned render_w(const Desc& d) noexcept { return d.render_width ? d.render_width : d.width; }
    static unsigned render_h(const Desc& d) noexcept { return d.render_height ? d.render_height : d.height; }
    static bool same(const Desc& a, const Desc& b) noexcept {
        // Phase/jitter values change each rendered frame; the create contract uses
        // the sequence generation supplied through feature_generation, not phase.
        return a.width == b.width && a.height == b.height && a.colour == b.colour && a.output == b.output &&
            render_w(a) == render_w(b) && render_h(a) == render_h(b) && a.quality == b.quality &&
            a.device_generation == b.device_generation && a.dimension_generation == b.dimension_generation &&
            a.feature_generation == b.feature_generation && a.retry_generation == b.retry_generation &&
            a.motion == b.motion && a.colour_contract == b.colour_contract &&
            a.inverted_depth == b.inverted_depth && a.mv_low_resolution == b.mv_low_resolution;
    }
    static bool conventions(const Desc& d) noexcept {
        const auto& m = d.motion; const auto& c = d.colour_contract;
        return std::isfinite(m.scale_x) && std::isfinite(m.scale_y) && m.scale_x != 0 && m.scale_y != 0 &&
            m.direction == temporal_eye::MotionDirection::current_to_previous &&
            m.units != temporal_eye::MotionUnits::unknown && m.resolution != temporal_eye::MotionResolution::unknown &&
            c.domain != temporal_eye::ColourDomain::unknown && c.encoding != temporal_eye::ColourEncoding::unknown &&
            c.exposure != temporal_eye::ExposureSource::unknown &&
            // Pinned guide section 3.1.2: linear color requires HDR processing.
            (c.hdr || c.encoding != temporal_eye::ColourEncoding::linear) && std::isfinite(c.pre_exposure) && c.pre_exposure > 0 &&
            std::isfinite(c.exposure_scale) && c.exposure_scale > 0 &&
            c.auto_exposure == (c.exposure == temporal_eye::ExposureSource::automatic);
    }
    static bool dimensions(const Desc& d) noexcept {
        const unsigned rw = render_w(d), rh = render_h(d);
        // Guide: the output must be at least 64x32; DLAA renders at the output size, Quality below it.
        return d.width >= 64 && d.height >= 32 && d.width <= 16384 && d.height <= 16384 &&
            rw && rh && rw <= d.width && rh <= d.height &&
            (d.quality == Quality::dlaa ? (rw == d.width && rh == d.height) : (rw < d.width || rh < d.height)) &&
            d.device_generation && d.dimension_generation && d.feature_generation &&
            (d.colour == Format::rgba16_float || d.colour == Format::rgba8_unorm || d.colour == Format::r11g11b10_float) &&
            (d.output == Format::rgba16_float || d.output == Format::rgba8_unorm || d.output == Format::r11g11b10_float);
    }
    // Returned SDK objects are not reference counted. Never adopt an alias to
    // ANY live Core/Pair, including quarantined generations and another facade.
    static bool map_owned(NVSDK_NGX_Parameter* value, const Pair* partial = nullptr, const Eye* receiving = nullptr) noexcept {
        if (!value) return false;
        for (const auto& weak : cores()) if (const auto core = weak.lock()) if (core->capability == value) return true;
        for (const auto& pair : pairs()) if (pair) for (const auto& eye : pair->eyes)
            if (eye.parameters == value) return true;
        if (partial) for (const auto& eye : partial->eyes)
            if (&eye != receiving && eye.parameters == value) return true;
        return false;
    }
    static bool handle_owned(NVSDK_NGX_Handle* value, const Pair* partial, const Eye* receiving) noexcept {
        if (!value) return false;
        for (const auto& pair : pairs()) if (pair) for (const auto& eye : pair->eyes)
            if (eye.handle == value) return true;
        for (const auto& eye : partial->eyes) if (&eye != receiving && eye.handle == value) return true;
        return false;
    }
    static void collect_locked() noexcept {
        for (auto& p : pairs()) if (p && p->retired && (!p->attempted_gpu || p->gpu_safe) && p.use_count() == 1) p.reset();
    }
    void retire_active_locked() noexcept {
        if (active_) { active_->retired = true; active_.reset(); }
        collect_locked();
    }
    bool fail(Reason reason, NVSDK_NGX_Result result = NVSDK_NGX_Result_Success, int eye = -1) {
        status_.reason = reason; status_.result = result; status_.eye = eye;
        cached_status_ = status_; return false;
    }
    // Finds or initializes the NGX Core for this device/context/paths (shared by prepare and optimal_settings). A
    // Core that fails capability checks is destroyed here (capability map, then shutdown) and never retained.
    Reason acquire_core_locked(ID3D11Device* device, ID3D11DeviceContext* context, const Paths& paths, std::uint64_t device_generation, NVSDK_NGX_Result& result) {
        result = NVSDK_NGX_Result_Success;
        if (core_ && (core_->device != device || core_->context != context || core_->device_generation != device_generation)) core_.reset();
        if (core_) {
            if (core_->paths.provider_directory != paths.provider_directory || core_->paths.cache_directory != paths.cache_directory) return Reason::owner_conflict;
            return Reason::none;
        }
        std::weak_ptr<Core>* free_core = nullptr;
        for (auto& weak : cores()) {
            auto existing = weak.lock();
            if (!existing) { if (!free_core) free_core = &weak; continue; }
            if (existing->device == device) {
                // Do not initialize/shutdown an already owned device through another table/context/path.
                if (existing->context != context || existing->device_generation != device_generation || !existing->calls.same_as(calls_) ||
                    existing->validation.get() != validation_.get() || existing->paths.provider_directory != paths.provider_directory ||
                    existing->paths.cache_directory != paths.cache_directory) return Reason::owner_conflict;
                core_ = std::move(existing); return Reason::none;
            }
        }
        if (!free_core) return Reason::registry_full;
        const auto foreign = validation_->before_init(paths);
        if (foreign != Reason::none) return foreign;
        auto c = std::make_shared<Core>();
        c->calls = calls_; c->validation = validation_; c->device = device; c->context = context; c->paths = paths;
        c->device_generation = device_generation;
        c->device_lease = validation_->hold_device(device); c->context_lease = validation_->hold_context(context);
        if (!c->device_lease || !c->context_lease) return Reason::wrong_device;
        c->search[0] = c->paths.provider_directory.c_str();
        c->common.PathListInfo.Path = c->search; c->common.PathListInfo.Length = 1;
        result = calls_.init("d1c5ea6c-f92c-48f9-898d-df8cc7d5f3b4", NVSDK_NGX_ENGINE_TYPE_CUSTOM,
            "dishonored2-vr-dlss-1", c->paths.cache_directory.c_str(), device, &c->common, NVSDK_NGX_Version_API);
        if (ngx_failed(result) || result != NVSDK_NGX_Result_Success) return Reason::init_failed;
        c->initialized = true;
        result = calls_.capability(&c->capability);
        const bool duplicate_capability = map_owned(c->capability);
        if (duplicate_capability) c->capability = nullptr;
        if (duplicate_capability && result == NVSDK_NGX_Result_Success) return Reason::duplicate_history;
        if (ngx_failed(result) || result != NVSDK_NGX_Result_Success || !c->capability) return Reason::capability_failed;
        int available = 0, driver = 0;
        result = calls_.get_i(c->capability, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
        if (ngx_failed(result) || result != NVSDK_NGX_Result_Success) return Reason::capability_query_failed;
        result = calls_.get_i(c->capability, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &driver);
        if (ngx_failed(result) || result != NVSDK_NGX_Result_Success) return Reason::capability_query_failed;
        status_.driver_major_result = calls_.get_i(c->capability, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &status_.driver_major);
        status_.driver_minor_result = calls_.get_i(c->capability, NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &status_.driver_minor);
        status_.feature_init_query_result = calls_.get_i(c->capability, NVSDK_NGX_Parameter_SuperSampling_FeatureInitResult, &status_.feature_init_result);
        if (driver) return Reason::driver_update;
        if (!available) return Reason::unavailable;
        c->status = status_; core_ = c; *free_core = c;
        return Reason::none;
    }
    static int flags(const Desc& d) noexcept {
        int f{};
        if (d.colour_contract.hdr) f |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
        if (d.colour_contract.auto_exposure) f |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
        if (d.motion.jittered) f |= NVSDK_NGX_DLSS_Feature_Flags_MVJittered;
        if (d.inverted_depth) f |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
        if (d.mv_low_resolution) f |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
        return f;
    }
    // Inputs may be larger than the render size (the engine renders a dynamic-resolution subrect at the top left and
    // DLSS reads InRenderSubrectDimensions); the output must match the feature's target size exactly.
    static bool matches(Core& c, ID3D11Resource* resource, unsigned w, unsigned h, Format f, bool output = false) noexcept {
        Surface s;
        return resource && c.validation->surface(c.device, resource, s) && (output ? (s.width == w && s.height == h) : (s.width >= w && s.height >= h)) &&
            s.mip_levels == 1 && s.array_size == 1 && s.sample_count == 1 && s.sample_quality == 0 &&
            s.format == f && (!output || s.unordered_access);
    }
    DlssEyeUpscaler(Calls calls, std::shared_ptr<Validation> validation) : calls_(calls), validation_(std::move(validation)) {}
public:
    static DlssEyeUpscaler injected_for_cpu_tests(Calls calls, std::shared_ptr<Validation> validation) {
        return DlssEyeUpscaler(calls, std::move(validation));
    }
    DlssEyeUpscaler(const DlssEyeUpscaler&) = delete;
    DlssEyeUpscaler& operator=(const DlssEyeUpscaler&) = delete;
    ~DlssEyeUpscaler() { disable(); }
    void disable() noexcept {
        std::lock_guard<std::mutex> lock(mutex());
        retire_active_locked(); core_.reset(); status_.reason = Reason::disabled; attempted_ = false;
    }
    // Retires the active pair (its features are released after GPU retirement) but keeps the initialized Core for
    // the next prepare on this device: mode switches and resizes neither shut NGX down nor re-initialize it.
    void retire() noexcept {
        std::lock_guard<std::mutex> lock(mutex());
        retire_active_locked(); status_.reason = Reason::disabled; attempted_ = false;
    }
    Status status() const {
        std::lock_guard<std::mutex> lock(mutex()); auto result = status_;
        result.cleanup_result = cleanup_failure(); return result;
    }
    bool ready() const { std::lock_guard<std::mutex> lock(mutex()); return active_ && !active_->retired && status_.reason == Reason::none; }
    Lease lease() const { std::lock_guard<std::mutex> lock(mutex()); return Lease(active_); }
    // Serialized lifecycle point on the actual immediate context, outside recording.
    bool prepare(ID3D11Device* device, ID3D11DeviceContext* context, const Desc& desc, const Paths& paths) {
        std::lock_guard<std::mutex> lock(mutex());
        // Context validation precedes EVERY cached-success/failure decision. An
        // invalid incoming boundary must not inherit a valid older preparation.
        if (!calls_.complete() || !validation_) return fail(Reason::invalid_calls);
        const auto context_reason = validation_->context(device, context);
        if (context_reason != Reason::none || (core_ && core_->device == device && core_->context != context)) {
            // This rejected boundary is not an attempted SDK request and must not
            // replace the cached diagnostic/retry identity of the last valid one.
            status_ = {};
            status_.reason = context_reason != Reason::none ? context_reason : Reason::invalid_context;
            return false;
        }
        if (attempted_ && last_device_ == device && last_context_ == context && same(last_request_, desc) &&
            last_paths_.provider_directory == paths.provider_directory && last_paths_.cache_directory == paths.cache_directory) {
            status_ = cached_status_;
            if (active_ && !active_->retired && core_ && core_->context == context) {
                status_.reason = Reason::none; return true;
            }
            // Preserve the original SDK failure and result without an init storm.
            return false;
        }
        attempted_ = true; last_device_ = device; last_context_ = context; last_request_ = desc; last_paths_ = paths;
        retire_active_locked(); status_ = {};
        status_.expected_provider = paths.provider_directory + L"\\nvngx_dlss.dll";
        if (!dimensions(desc)) return fail(Reason::invalid_dimensions);
        if (!conventions(desc)) return fail(Reason::invalid_conventions);
        const auto path_reason = validation_->paths(paths);
        if (path_reason != Reason::none) return fail(path_reason);
        collect_locked();
        std::shared_ptr<Pair>* slot = nullptr;
        for (auto& p : pairs()) if (!p && !slot) slot = &p;
        if (!slot) return fail(Reason::registry_full);
        {
            NVSDK_NGX_Result core_result = NVSDK_NGX_Result_Success;
            const auto core_reason = acquire_core_locked(device, context, paths, desc.device_generation, core_result);
            if (core_reason != Reason::none) return fail(core_reason, core_result);
        }
        auto pair = std::make_shared<Pair>(); pair->core = core_; pair->desc = desc; pair->serial = next_serial();
        auto abort_pair = [&](Reason reason, NVSDK_NGX_Result result, int eye) {
            fail(reason, result, eye);
            pair.reset(); // Release every obtained handle, then every eye map.
            core_.reset(); // Idle Core closes capability+SDK only if no older job owns it.
            return false;
        };
        for (unsigned eye = 0; eye < 2; ++eye) {
            auto& e = pair->eyes[eye];
            auto result = calls_.allocate(&e.parameters);
            const bool duplicate_map = map_owned(e.parameters, pair.get(), &e);
            if (duplicate_map) e.parameters = nullptr; // Matching object still owned elsewhere.
            if (ngx_failed(result) || result != NVSDK_NGX_Result_Success || !e.parameters)
                return abort_pair(duplicate_map && result == NVSDK_NGX_Result_Success ? Reason::duplicate_history : Reason::allocate_failed,
                                  result, static_cast<int>(eye));
            NVSDK_NGX_DLSS_Create_Params create{};
            create.Feature.InWidth = render_w(desc); create.Feature.InHeight = render_h(desc);
            create.Feature.InTargetWidth = desc.width; create.Feature.InTargetHeight = desc.height;
            create.Feature.InPerfQualityValue = desc.quality == Quality::dlaa ? NVSDK_NGX_PerfQuality_Value_DLAA : NVSDK_NGX_PerfQuality_Value_MaxQuality;
            create.InFeatureCreateFlags = flags(desc);
            result = calls_.create(context, &e.handle, e.parameters, &create);
            const bool duplicate_handle = handle_owned(e.handle, pair.get(), &e);
            if (duplicate_handle) e.handle = nullptr;
            if (ngx_failed(result) || result != NVSDK_NGX_Result_Success || !e.handle)
                return abort_pair(duplicate_handle && result == NVSDK_NGX_Result_Success ? Reason::duplicate_history : Reason::create_failed,
                                  result, static_cast<int>(eye));
        }
        status_.expected_provider = paths.provider_directory + L"\\nvngx_dlss.dll";
        const auto observed = validation_->observed_provider(paths, status_.observed_provider);
        if (observed != Reason::none) return abort_pair(observed, NVSDK_NGX_Result_Success, -1);
        status_.observed_provider_verified = true;
        pair->status.reason = Reason::none;
        *slot = pair; active_ = std::move(pair); status_.reason = Reason::none; cached_status_ = status_;
        return true;
    }
    // Serialized optimal-settings query for an output size (the SDK's NGX_DLSS_GET_OPTIMAL_SETTINGS on the capability
    // map). Initializes the Core like prepare but creates no feature and leaves the cached prepare status alone.
    Reason optimal_settings(ID3D11Device* device, ID3D11DeviceContext* context, const Paths& paths, std::uint64_t device_generation,
                            unsigned output_width, unsigned output_height, Quality quality, OptimalSettings& out, NVSDK_NGX_Result* result_out = nullptr) {
        std::lock_guard<std::mutex> lock(mutex());
        out = {};
        NVSDK_NGX_Result result = NVSDK_NGX_Result_Success;
        const auto done = [&](Reason reason) { if (result_out) *result_out = result; return reason; };
        if (!calls_.complete() || !validation_) return done(Reason::invalid_calls);
        if (!output_width || !output_height) return done(Reason::invalid_dimensions);
        const auto context_reason = validation_->context(device, context);
        if (context_reason != Reason::none) return done(context_reason);
        if (core_ && core_->device == device && core_->context != context) return done(Reason::invalid_context);
        const auto path_reason = validation_->paths(paths);
        if (path_reason != Reason::none) return done(path_reason);
        const auto core_reason = acquire_core_locked(device, context, paths, device_generation, result);
        if (core_reason != Reason::none) return done(core_reason);
        void* callback = nullptr;
        result = calls_.get_void_pointer(core_->capability, NVSDK_NGX_Parameter_DLSSOptimalSettingsCallback, &callback);
        if (ngx_failed(result) || result != NVSDK_NGX_Result_Success || !callback) return done(Reason::optimal_unsupported);
        calls_.set_ui(core_->capability, NVSDK_NGX_Parameter_Width, output_width);
        calls_.set_ui(core_->capability, NVSDK_NGX_Parameter_Height, output_height);
        calls_.set_i(core_->capability, NVSDK_NGX_Parameter_PerfQualityValue,
            static_cast<int>(quality == Quality::dlaa ? NVSDK_NGX_PerfQuality_Value_DLAA : NVSDK_NGX_PerfQuality_Value_MaxQuality));
        calls_.set_i(core_->capability, NVSDK_NGX_Parameter_RTXValue, 0);
        result = reinterpret_cast<OptimalSettingsCallback>(callback)(core_->capability);
        if (ngx_failed(result) || result != NVSDK_NGX_Result_Success) return done(Reason::optimal_failed);
        unsigned* fields[] = {&out.optimal_width, &out.optimal_height, &out.max_width, &out.max_height, &out.min_width, &out.min_height};
        const char* keys[] = {NVSDK_NGX_Parameter_OutWidth, NVSDK_NGX_Parameter_OutHeight,
            NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Width, NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Max_Render_Height,
            NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width, NVSDK_NGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height};
        for (unsigned i = 0; i < 6; ++i) {
            result = calls_.get_ui(core_->capability, keys[i], fields[i]);
            if (ngx_failed(result) || result != NVSDK_NGX_Result_Success) return done(Reason::optimal_failed);
        }
        // Guide: a zero optimal size means the quality mode is unsupported for this output size.
        if (!out.optimal_width || !out.optimal_height || out.optimal_width > output_width || out.optimal_height > output_height ||
            out.min_width > out.optimal_width || out.min_height > out.optimal_height ||
            out.max_width < out.optimal_width || out.max_height < out.optimal_height) return done(Reason::optimal_failed);
        return done(Reason::none);
    }
    // Job lease retains Pair/Core/device/maps and generation scratch even across
    // disable/resize. Retired generations refuse evaluation but remain GPU-owned.
    static bool evaluate(const Lease& lease, const Eval& in, Status* out = nullptr) {
        std::lock_guard<std::mutex> lock(mutex());
        if (!lease.pair_) { if (out) out->reason = Reason::retired; return false; }
        auto& p = *lease.pair_; auto& c = *p.core;
        auto fail_eval = [&](Reason reason, NVSDK_NGX_Result result = NVSDK_NGX_Result_Success) {
            p.status.reason = reason; p.status.result = result; p.status.eye = static_cast<int>(in.eye);
            for (auto& eye : p.eyes) eye.reset = true;
            if (out) *out = p.status;
            return false;
        };
        if (p.retired) return fail_eval(Reason::retired);
        if (in.eye > 1 || !in.frame || !in.pair || in.device_generation != p.desc.device_generation ||
            in.dimension_generation != p.desc.dimension_generation || in.feature_generation != p.desc.feature_generation) return fail_eval(Reason::stale_generation);
        if (in.context != c.context || c.validation->context(c.device, in.context) != Reason::none) return fail_eval(Reason::invalid_context);
        if (!(in.motion_contract == p.desc.motion) || !(in.colour_contract == p.desc.colour_contract) ||
            !std::isfinite(in.jitter.current_x) || !std::isfinite(in.jitter.current_y) ||
            !std::isfinite(in.jitter.previous_x) || !std::isfinite(in.jitter.previous_y) ||
            !in.jitter.phase_count || in.jitter.phase >= in.jitter.phase_count ||
            !std::isfinite(in.frame_time_ms) || in.frame_time_ms <= 0 || !in.scratch_owner) return fail_eval(Reason::invalid_conventions);
        const unsigned rw = render_w(p.desc), rh = render_h(p.desc);
        if (in.colour == in.output || in.depth == in.output || in.motion == in.output ||
            !matches(c, in.colour, rw, rh, p.desc.colour) ||
            !matches(c, in.output, p.desc.width, p.desc.height, p.desc.output, true) ||
            !matches(c, in.depth, rw, rh, Format::r32_float) ||
            !matches(c, in.motion, rw, rh, Format::rg16_float) ||
            (in.colour_contract.exposure == temporal_eye::ExposureSource::texture && !matches(c, in.exposure, 1, 1, Format::r32_float)) ||
            (in.colour_contract.exposure != temporal_eye::ExposureSource::texture && in.exposure)) return fail_eval(Reason::invalid_resource);
        if (p.scratch_owner && (p.scratch_owner.owner_before(in.scratch_owner) ||
            in.scratch_owner.owner_before(p.scratch_owner))) return fail_eval(Reason::stale_generation);
        p.scratch_owner = in.scratch_owner;
        auto& eye = p.eyes[in.eye];
        NVSDK_NGX_D3D11_DLSS_Eval_Params eval{};
        eval.Feature.pInColor = in.colour; eval.Feature.pInOutput = in.output;
        eval.pInDepth = in.depth; eval.pInMotionVectors = in.motion;
        eval.InJitterOffsetX = in.jitter.current_x; eval.InJitterOffsetY = in.jitter.current_y;
        eval.InRenderSubrectDimensions = {rw, rh};
        eval.InReset = eye.reset || in.discontinuity || in.jitter.reset_history ||
            (eye.frame && (in.frame != eye.frame + 1 || in.pair != eye.pair + 1));
        eval.InMVScaleX = in.motion_contract.scale_x; eval.InMVScaleY = in.motion_contract.scale_y;
        eval.pInExposureTexture = in.exposure;
        eval.InPreExposure = in.colour_contract.pre_exposure; eval.InExposureScale = in.colour_contract.exposure_scale;
        eval.InFrameTimeDeltaInMsec = in.frame_time_ms;
        // Even a failed evaluate can have recorded GPU work. Quarantine until a
        // real completion signal; return from this call never authorizes teardown.
        p.attempted_gpu = true; p.gpu_safe = false;
        const auto result = c.calls.evaluate(c.context, eye.handle, eye.parameters, &eval);
        if (ngx_failed(result) || result != NVSDK_NGX_Result_Success) return fail_eval(Reason::evaluate_failed, result);
        eye.reset = false; eye.frame = in.frame; eye.pair = in.pair;
        p.status.reason = Reason::none; p.status.result = result; p.status.eye = static_cast<int>(in.eye);
        if (out) *out = p.status;
        return true;
    }
    static void reset_histories(const Lease& lease) {
        std::lock_guard<std::mutex> lock(mutex());
        if (lease.pair_) for (auto& eye : lease.pair_->eyes) eye.reset = true;
    }
    // Call ONLY after retiring the generation and proving ALL its recorded GPU
    // uses complete (or proven device abandonment). No future evaluate is allowed.
    // Job leases still prevent collection; drop them after their T1 retirement.
    static bool confirm_gpu_retirement(const Lease& lease) {
        std::lock_guard<std::mutex> lock(mutex());
        if (!lease.pair_ || !lease.pair_->retired) return false;
        lease.pair_->gpu_safe = true; collect_locked(); return true;
    }
    static void collect_retired() { std::lock_guard<std::mutex> lock(mutex()); collect_locked(); }
#ifdef _WIN32
private:
    struct NativeValidation final : Validation {
        static bool absolute(const std::wstring& p) noexcept {
            return p.size() >= 3 && ((p[1] == L':' && (p[2] == L'\\' || p[2] == L'/')) ||
                (p[0] == L'\\' && p[1] == L'\\'));
        }
        static bool signed_nvidia(const std::wstring& path) noexcept {
            WINTRUST_FILE_INFO file{}; file.cbStruct = sizeof file; file.pcwszFilePath = path.c_str();
            WINTRUST_DATA trust{}; trust.cbStruct = sizeof trust; trust.dwUIChoice = WTD_UI_NONE;
            trust.fdwRevocationChecks = WTD_REVOKE_NONE; trust.dwUnionChoice = WTD_CHOICE_FILE; trust.pFile = &file;
            trust.dwStateAction = WTD_STATEACTION_VERIFY; trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
            GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
            const LONG verified = WinVerifyTrust(nullptr, &policy, &trust);
            bool nvidia = false;
            if (verified == ERROR_SUCCESS) {
                auto* data = WTHelperProvDataFromStateData(trust.hWVTStateData);
                auto* signer = data ? WTHelperGetProvSignerFromChain(data, 0, FALSE, 0) : nullptr;
                if (signer && signer->csCertChain) {
                    wchar_t name[512]{};
                    CertGetNameStringW(signer->pasCertChain[0].pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 512);
                    nvidia = _wcsicmp(name, L"NVIDIA Corporation") == 0;
                }
            }
            trust.dwStateAction = WTD_STATEACTION_CLOSE; WinVerifyTrust(nullptr, &policy, &trust);
            return verified == ERROR_SUCCESS && nvidia;
        }
        Reason paths(const Paths& p) noexcept override {
            if (!absolute(p.provider_directory) || !absolute(p.cache_directory) || p.provider_directory == p.cache_directory) return Reason::invalid_paths;
            try {
                const auto expected = std::filesystem::path(p.provider_directory) / L"nvngx_dlss.dll";
                std::error_code ec;
                if (!std::filesystem::is_regular_file(expected, ec)) return Reason::missing_provider;
                if (!signed_nvidia(expected.wstring())) return Reason::provider_signature;
                std::filesystem::create_directories(p.cache_directory, ec);
                if (ec) return Reason::cache_unwritable;
                wchar_t temp[MAX_PATH]{};
                if (!GetTempFileNameW(p.cache_directory.c_str(), L"ngx", 0, temp)) return Reason::cache_unwritable;
                const HANDLE h = CreateFileW(temp, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
                if (h == INVALID_HANDLE_VALUE) { DeleteFileW(temp); return Reason::cache_unwritable; }
                CloseHandle(h); return Reason::none;
            } catch (...) { return Reason::invalid_paths; }
        }
        static std::wstring loaded_provider() {
            const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
            if (snapshot == INVALID_HANDLE_VALUE) return L"?";
            struct CloseSnapshot { HANDLE handle; ~CloseSnapshot() { CloseHandle(handle); } } close{snapshot};
            MODULEENTRY32W entry{}; entry.dwSize = sizeof entry;
            if (!Module32FirstW(snapshot, &entry)) return L"?";
            std::wstring found;
            unsigned count = 0;
            do {
                if (_wcsicmp(entry.szModule, L"nvngx_dlss.dll") == 0) { found = entry.szExePath; ++count; }
            } while (Module32NextW(snapshot, &entry));
            // A partial scan proves neither absence nor uniqueness. Preserve the
            // API's terminal error before snapshot cleanup changes thread error state.
            const DWORD scan_end = GetLastError();
            if (scan_end != ERROR_NO_MORE_FILES) return L"?";
            return count > 1 ? L"?" : found;
        }
        Reason before_init(const Paths& paths) noexcept override {
            try {
                const auto loaded = loaded_provider();
                if (loaded.empty()) return Reason::none;
                if (loaded == L"?") return Reason::coexistence_unsupported;
                // Our own provider stays loaded after an earlier initialization in this process; another integration's does not pass.
                std::error_code ec;
                const auto expected = std::filesystem::canonical(std::filesystem::path(paths.provider_directory) / L"nvngx_dlss.dll", ec);
                if (ec) return Reason::coexistence_unsupported;
                const auto actual = std::filesystem::canonical(loaded, ec);
                return !ec && _wcsicmp(expected.c_str(), actual.c_str()) == 0 ? Reason::none : Reason::coexistence_unsupported;
            } catch (...) { return Reason::coexistence_unsupported; }
        }
        Reason observed_provider(const Paths& paths, std::wstring& observed) noexcept override {
            try {
                observed = loaded_provider();
                if (observed.empty() || observed == L"?") return Reason::provider_selection_unverified;
                std::error_code ec;
                const auto expected = std::filesystem::canonical(std::filesystem::path(paths.provider_directory) / L"nvngx_dlss.dll", ec);
                if (ec) return Reason::provider_selection_unverified;
                const auto actual = std::filesystem::canonical(observed, ec);
                if (ec || _wcsicmp(expected.c_str(), actual.c_str()) != 0 || !signed_nvidia(observed)) return Reason::provider_selection_unverified;
                return Reason::none;
            } catch (...) { return Reason::provider_selection_unverified; }
        }
        Reason context(ID3D11Device* device, ID3D11DeviceContext* context) noexcept override {
            if (!device || !context || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return Reason::invalid_context;
            ID3D11Device* actual = nullptr; context->GetDevice(&actual);
            const bool same = actual == device; if (actual) actual->Release();
            if (!same) return Reason::wrong_device;
            return Reason::none;
        }
        template<class T> static temporal_eye::Lease retain(T* p) {
            if (!p) return {};
            p->AddRef();
            return temporal_eye::Lease(p, [](const void* value) { const_cast<T*>(static_cast<const T*>(value))->Release(); });
        }
        temporal_eye::Lease hold_device(ID3D11Device* p) override { return retain(p); }
        temporal_eye::Lease hold_context(ID3D11DeviceContext* p) override { return retain(p); }
        bool surface(ID3D11Device* device, ID3D11Resource* resource, Surface& out) noexcept override {
            if (!resource) return false;
            ID3D11Device* owner = nullptr; resource->GetDevice(&owner);
            const bool same = owner == device; if (owner) owner->Release(); if (!same) return false;
            ID3D11Texture2D* texture = nullptr;
            if (FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))) || !texture) return false;
            D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d); texture->Release();
            Format format;
            switch (d.Format) {
            case DXGI_FORMAT_R16G16B16A16_FLOAT: format = Format::rgba16_float; break;
            case DXGI_FORMAT_R8G8B8A8_UNORM: format = Format::rgba8_unorm; break;
            case DXGI_FORMAT_R11G11B10_FLOAT: format = Format::r11g11b10_float; break;
            case DXGI_FORMAT_R32_FLOAT: format = Format::r32_float; break;
            case DXGI_FORMAT_R16G16_FLOAT: format = Format::rg16_float; break;
            default: return false;
            }
            out = {d.Width, d.Height, d.MipLevels, d.ArraySize, d.SampleDesc.Count, d.SampleDesc.Quality,
                   format, (d.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0};
            return d.Usage == D3D11_USAGE_DEFAULT && d.CPUAccessFlags == 0;
        }
    };
public:
    DlssEyeUpscaler() : calls_(Calls::production()), validation_(production_validation()) {}
    static std::shared_ptr<Validation> production_validation() {
        static auto value = std::make_shared<NativeValidation>(); return value;
    }
#endif
};
} // namespace d2vr
