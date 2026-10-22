#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

// Portable retained-command contract. Adapters own native API/ABI knowledge.
namespace temporal_eye {
// Internal capture gate; T5 owns any eventual user opt-in. ABI availability alone never enables it.
inline std::atomic<bool> diagnostic_capture_enabled{false};
template<class Capture> void capture_diagnostic(Capture&& capture) {
    if(diagnostic_capture_enabled.load(std::memory_order_relaxed)) std::forward<Capture>(capture)();
}
using Lease = std::shared_ptr<const void>; // COM adapters can supply a Release deleter.
struct Identity {
    std::uintptr_t address{};
    std::uint64_t incarnation{}; // Monotonic adapter-issued identity; never just an address.
    bool valid() const noexcept { return address != 0 && incarnation != 0; }
};
inline bool operator==(Identity a, Identity b) noexcept {
    return a.address == b.address && a.incarnation == b.incarnation;
}
// Native diagnostics retain their source units. A view stamp of zero is valid;
// optional presence, not raw-counter truthiness, distinguishes missing metadata.
struct NativePass {
    std::uintptr_t view{};
    std::uint32_t counter{};
    std::array<float,4> jitter_uv{};
    std::array<float,16> projection{}; // Native current projection at the verified Setup output.
};
inline bool operator==(const NativePass& a, const NativePass& b) noexcept {
    return a.view == b.view && a.counter == b.counter && a.jitter_uv == b.jitter_uv && a.projection == b.projection;
}
struct SetupObservation {
    NativePass pass;
    std::uintptr_t allocation{};
    std::array<std::uint64_t,3> camera_owner{};
    unsigned eye{};
    bool setup_completed{}, forced_jitter_written{}, submitted_projection_present{};
    std::array<float,16> submitted_projection{}; // Observed source input; not proof of consumption.
};
struct ViewReceipt {
    SetupObservation setup;
    std::uint64_t serial{}, owner_generation{}, pair{}, frame{};
};
class NativePairBook {
    std::array<std::optional<ViewReceipt>,2> eyes_{};
    std::uint64_t serial_{}, generation_{1}, pair_{}, frame_{};
    std::uint32_t counter_{};
    bool has_counter_{}, poisoned_{};
public:
    void invalidate() noexcept { eyes_ = {}; has_counter_ = false; poisoned_ = false; ++generation_; }
    std::uint64_t generation() const noexcept { return generation_; }
    bool publish(const SetupObservation& value) noexcept {
        if (value.eye > 1 || !value.pass.view || !value.allocation || !value.camera_owner[0] ||
            !value.camera_owner[1] || !value.setup_completed) return false;
        for (float v : value.pass.jitter_uv) if (!std::isfinite(v)) return false;
        for (float v : value.pass.projection) if (!std::isfinite(v)) return false;
        if(value.submitted_projection_present) for (float v : value.submitted_projection) if (!std::isfinite(v)) return false;
        if (has_counter_) {
            const auto& previous = eyes_[0] ? eyes_[0] : eyes_[1];
            if (previous && (previous->setup.allocation != value.allocation || previous->setup.camera_owner != value.camera_owner)) invalidate();
        }
        if (!has_counter_ || counter_ != value.pass.counter) {
            if (has_counter_ && static_cast<std::uint32_t>(value.pass.counter - counter_) != 1u) ++generation_;
            eyes_ = {}; poisoned_ = false; counter_ = value.pass.counter; has_counter_ = true;
        }
        if (poisoned_) return false;
        if (eyes_[value.eye]) { eyes_ = {}; poisoned_ = true; ++generation_; return false; }
        eyes_[value.eye] = ViewReceipt{value, ++serial_, generation_, 0, 0};
        if (eyes_[0] && eyes_[1]) {
            if (eyes_[0]->setup.pass.view == eyes_[1]->setup.pass.view) { eyes_ = {}; poisoned_ = true; ++generation_; return false; }
            ++pair_; ++frame_;
            for (auto& eye : eyes_) { eye->pair = pair_; eye->frame = frame_; }
        }
        return true;
    }
    std::optional<ViewReceipt> resolve(unsigned eye, const NativePass& pass) const noexcept {
        if (eye > 1 || poisoned_ || !eyes_[0] || !eyes_[1] || !eyes_[eye]->pair ||
            !(eyes_[eye]->setup.pass == pass)) return {};
        return eyes_[eye];
    }
};
enum class Admission { disabled, missing_pair, stale_pass, missing_motion, missing_depth_producer,
    projection_consumption_unverified, native_callback_contract_unverified, unsupported_state, owner_unavailable, ready };
// A discovered clear has no producing-eye field: a TAA consumer cannot certify it.
struct DepthClearObservation {
    Identity context, view, resource;
    std::uint64_t sequence{}, thread{};
    std::uint32_t flags{};
    float depth{};
    std::uint8_t stencil{};
};
struct List { Identity id; Lease owner; };
struct EyeFrame {
    std::uint64_t frame{}, pair{};
    unsigned eye{};
};
inline bool operator==(EyeFrame a, EyeFrame b) noexcept {
    return a.frame == b.frame && a.pair == b.pair && a.eye == b.eye;
}
struct Generations {
    std::uint64_t device{}, backend{}, feature{}, reset{}, jitter{}, dimensions{};
};
inline bool operator==(Generations a, Generations b) noexcept {
    return a.device == b.device && a.backend == b.backend && a.feature == b.feature &&
           a.reset == b.reset && a.jitter == b.jitter && a.dimensions == b.dimensions;
}
struct Shape {
    // Complete Texture2D descriptor plus the selected active rectangle. Numeric
    // format/flags retain native values without depending on native SDK headers.
    std::uint32_t width{}, height{}, x{}, y{}, rect_width{}, rect_height{}, format{};
    std::uint32_t mip_levels = 1, array_size = 1, sample_count = 1, sample_quality{};
    std::uint32_t usage{}, bind_flags{}, cpu_access_flags{}, misc_flags{};
    bool valid() const noexcept {
        return width && height && format && mip_levels && array_size && sample_count &&
               rect_width && rect_height && x <= width && y <= height &&
               rect_width <= width - x && rect_height <= height - y;
    }
};
inline bool operator==(Shape a, Shape b) noexcept {
    return a.width == b.width && a.height == b.height && a.x == b.x && a.y == b.y &&
           a.rect_width == b.rect_width && a.rect_height == b.rect_height && a.format == b.format &&
           a.mip_levels == b.mip_levels && a.array_size == b.array_size &&
           a.sample_count == b.sample_count && a.sample_quality == b.sample_quality &&
           a.usage == b.usage && a.bind_flags == b.bind_flags &&
           a.cpu_access_flags == b.cpu_access_flags && a.misc_flags == b.misc_flags;
}
// Whole-resource copies cannot crop, convert, change array/mips or sample layout.
inline bool copy_compatible(const Shape& a, const Shape& b) noexcept {
    return a.valid() && b.valid() && a.width == b.width && a.height == b.height && a.format == b.format &&
        a.mip_levels == b.mip_levels && a.array_size == b.array_size &&
        a.sample_count == b.sample_count && a.sample_quality == b.sample_quality &&
        a.x == 0 && a.y == 0 && b.x == 0 && b.y == 0 &&
        a.rect_width == a.width && a.rect_height == a.height && b.rect_width == b.width && b.rect_height == b.height;
}
struct Selection {
    Identity resource;
    std::uint64_t generation{};
};
inline bool operator==(Selection a, Selection b) noexcept {
    return a.resource == b.resource && a.generation == b.generation;
}
enum class DepthViewDimension { unknown, texture2d, texture2d_array, texture2d_ms, texture2d_ms_array };
struct DepthSelection {
    Identity view;
    Selection resource;
    std::uint64_t generation{};
    DepthViewDimension dimension = DepthViewDimension::unknown;
    std::uint32_t format{}, most_detailed_mip{}, mip_levels = 1, first_array_slice{}, array_size = 1;
};
inline bool operator==(DepthSelection a, DepthSelection b) noexcept {
    return a.view == b.view && a.resource == b.resource && a.generation == b.generation &&
           a.dimension == b.dimension && a.format == b.format &&
           a.most_detailed_mip == b.most_detailed_mip && a.mip_levels == b.mip_levels &&
           a.first_array_slice == b.first_array_slice && a.array_size == b.array_size;
}
enum class RequestedMode { unknown, native_taa, temporal_upscale, dlaa, dlss_quality };
enum class Backend { unknown, native_taa, fsr3, fsr4, dlss };
enum class MotionDirection { unknown, current_to_previous, previous_to_current };
enum class MotionUnits { unknown, uv, pixels };
enum class MotionResolution { unknown, active_render, full_render, output };
struct JitterContract {
    float current_x{}, current_y{}, previous_x{}, previous_y{}; // Actual rendered pixels.
    std::uint32_t phase{}, phase_count{};
    bool reset_history{};
};
inline bool operator==(JitterContract a, JitterContract b) noexcept {
    return a.current_x == b.current_x && a.current_y == b.current_y &&
           a.previous_x == b.previous_x && a.previous_y == b.previous_y &&
           a.phase == b.phase && a.phase_count == b.phase_count && a.reset_history == b.reset_history;
}
struct MotionContract {
    float scale_x{}, scale_y{};
    bool jittered{};
    MotionDirection direction = MotionDirection::unknown;
    MotionUnits units = MotionUnits::unknown;
    MotionResolution resolution = MotionResolution::unknown;
};
inline bool operator==(MotionContract a, MotionContract b) noexcept {
    return a.scale_x == b.scale_x && a.scale_y == b.scale_y && a.jittered == b.jittered &&
           a.direction == b.direction && a.units == b.units && a.resolution == b.resolution;
}
enum class ColourDomain { unknown, native_scene, post_tonemap };
enum class ColourEncoding { unknown, linear, perceptual_gamma, srgb };
enum class ExposureSource { unknown, automatic, fixed, texture };
struct ColourContract {
    ColourDomain domain = ColourDomain::unknown;
    ColourEncoding encoding = ColourEncoding::unknown;
    ExposureSource exposure = ExposureSource::unknown;
    bool hdr{}, auto_exposure{};
    float pre_exposure{}, exposure_scale{};
    Selection exposure_resource; // Used only for texture exposure; owns an explicit lease below.
    Shape exposure_shape;
};
inline bool operator==(const ColourContract& a, const ColourContract& b) noexcept {
    return a.domain == b.domain && a.encoding == b.encoding && a.exposure == b.exposure &&
           a.hdr == b.hdr && a.auto_exposure == b.auto_exposure &&
           a.pre_exposure == b.pre_exposure && a.exposure_scale == b.exposure_scale &&
           a.exposure_resource == b.exposure_resource && a.exposure_shape == b.exposure_shape;
}
struct InputIdentity {
    EyeFrame key;
    Generations generations;
    Identity device;
    RequestedMode requested_mode = RequestedMode::unknown;
    Backend selected_backend = Backend::unknown;
    Shape colour, depth, motion, output;
    Selection colour_selection, depth_selection, motion_selection, output_selection;
    DepthSelection depth_view;
    JitterContract jitter;
    MotionContract motion_contract;
    ColourContract colour_contract;
    bool inverted_depth{};
};
inline bool operator==(const InputIdentity& a, const InputIdentity& b) noexcept {
    return a.key == b.key && a.generations == b.generations && a.device == b.device &&
           a.requested_mode == b.requested_mode && a.selected_backend == b.selected_backend &&
           a.colour == b.colour && a.depth == b.depth && a.motion == b.motion && a.output == b.output &&
           a.colour_selection == b.colour_selection && a.depth_selection == b.depth_selection &&
           a.motion_selection == b.motion_selection && a.output_selection == b.output_selection &&
           a.depth_view == b.depth_view && a.jitter == b.jitter && a.motion_contract == b.motion_contract &&
           a.colour_contract == b.colour_contract && a.inverted_depth == b.inverted_depth;
}
enum class EyeOutcome { temporal, fallback, failed };
enum class PairState { incomplete, coherent, fallback, failed };
// One pending pair per immediate queue. Results from a displaced/older pair can
// never complete a newer pair; caller resets histories on fallback/failed, not
// merely while waiting for the second eye. No output is published by this ledger.
class PairPublication {
    std::optional<InputIdentity> contract_;
    // Weak ownership identity keeps telemetry from extending GPU resource lifetime.
    std::weak_ptr<const void> feature_owner_;
    std::array<bool,2> seen_{},success_{};
    PairState state_ = PairState::incomplete;
public:
    PairState observe(const InputIdentity& input,const Lease& feature_owner,EyeOutcome outcome) noexcept {
        if(!contract_ || input.key.pair>contract_->key.pair) {contract_=input;feature_owner_=feature_owner;seen_={};success_={};state_=PairState::incomplete;}
        if(!feature_owner || feature_owner.owner_before(feature_owner_) || feature_owner_.owner_before(feature_owner) ||
           input.key.eye>1 || input.key.pair!=contract_->key.pair ||
           !(input.generations==contract_->generations) || !(input.device==contract_->device) ||
           input.requested_mode!=contract_->requested_mode || input.selected_backend!=contract_->selected_backend ||
           !(input.colour==contract_->colour) || !(input.depth==contract_->depth) ||
           !(input.motion==contract_->motion) || !(input.output==contract_->output) ||
           !(input.motion_contract==contract_->motion_contract) || !(input.colour_contract==contract_->colour_contract) ||
           !(input.jitter==contract_->jitter) || input.inverted_depth!=contract_->inverted_depth) return state_==PairState::failed?state_:state_=PairState::fallback;
        const auto eye=input.key.eye;
        if(seen_[eye]) return state_==PairState::failed?state_:state_=PairState::fallback;
        seen_[eye]=true;success_[eye]=outcome==EyeOutcome::temporal;
        if(outcome==EyeOutcome::failed) return state_=PairState::failed;
        if(outcome==EyeOutcome::fallback && state_!=PairState::failed) return state_=PairState::fallback;
        if(state_==PairState::incomplete && seen_[0] && seen_[1] && success_[0] && success_[1]) state_=PairState::coherent;
        return state_;
    }
    PairState fallback_only(bool succeeded) noexcept {if(state_==PairState::failed)return state_;return state_=succeeded?PairState::fallback:PairState::failed;}
    PairState state() const noexcept {return state_;}
};
struct Resource {
    Identity id;
    Lease owner;
    EyeFrame produced_for;
    std::uint64_t device_generation{}, producer_stamp{};
};
// Contains only facts available BEFORE FinishCommandList. No list or restore fields.
struct RenderInputs {
    InputIdentity identity;
    Resource colour, depth, motion, output;
    // feature_owner also retains eye-private snapshot/evaluation scratch storage.
    Lease feature_owner, device_owner, depth_view_owner, exposure_owner;
};
inline bool valid(const RenderInputs& r) noexcept {
    const auto& i = r.identity;
    if (i.key.eye > 1 || !i.key.frame || !i.key.pair || !i.generations.device ||
        !i.generations.backend || !i.generations.feature || !i.generations.reset ||
        !i.generations.jitter || !i.generations.dimensions || !i.colour.valid() || !i.depth.valid() ||
        !i.motion.valid() || !i.output.valid() || !r.feature_owner || !r.device_owner ||
        !i.device.valid() || i.requested_mode == RequestedMode::unknown ||
        i.selected_backend == Backend::unknown || !r.depth_view_owner ||
        !i.depth_view.view.valid() || !i.depth_view.generation || !i.depth_view.format ||
        i.depth_view.dimension == DepthViewDimension::unknown ||
        !(i.depth_view.resource == i.depth_selection) ||
        !i.depth_view.mip_levels || !i.depth_view.array_size ||
        i.depth_view.most_detailed_mip >= i.depth.mip_levels ||
        i.depth_view.mip_levels > i.depth.mip_levels - i.depth_view.most_detailed_mip ||
        i.depth_view.first_array_slice >= i.depth.array_size ||
        i.depth_view.array_size > i.depth.array_size - i.depth_view.first_array_slice ||
        !std::isfinite(i.jitter.current_x) || !std::isfinite(i.jitter.current_y) ||
        !std::isfinite(i.jitter.previous_x) || !std::isfinite(i.jitter.previous_y) ||
        !i.jitter.phase_count || i.jitter.phase >= i.jitter.phase_count ||
        !std::isfinite(i.motion_contract.scale_x) || !std::isfinite(i.motion_contract.scale_y) ||
        i.motion_contract.scale_x == 0 || i.motion_contract.scale_y == 0 ||
        i.motion_contract.direction == MotionDirection::unknown ||
        i.motion_contract.units == MotionUnits::unknown ||
        i.motion_contract.resolution == MotionResolution::unknown ||
        i.colour_contract.domain == ColourDomain::unknown ||
        i.colour_contract.encoding == ColourEncoding::unknown ||
        i.colour_contract.exposure == ExposureSource::unknown ||
        !std::isfinite(i.colour_contract.pre_exposure) || i.colour_contract.pre_exposure <= 0 ||
        !std::isfinite(i.colour_contract.exposure_scale) || i.colour_contract.exposure_scale <= 0 ||
        i.colour_contract.auto_exposure != (i.colour_contract.exposure == ExposureSource::automatic)) return false;
    const auto view = i.depth_view.dimension;
    const bool ms = view == DepthViewDimension::texture2d_ms || view == DepthViewDimension::texture2d_ms_array;
    const bool array = view == DepthViewDimension::texture2d_array || view == DepthViewDimension::texture2d_ms_array;
    if (ms != (i.depth.sample_count > 1) ||
        (!array && (i.depth.array_size != 1 || i.depth_view.first_array_slice != 0 || i.depth_view.array_size != 1)) ||
        (ms && (i.depth.mip_levels != 1 || i.depth_view.most_detailed_mip != 0 || i.depth_view.mip_levels != 1))) return false;
    if (i.colour_contract.exposure == ExposureSource::texture &&
        (!r.exposure_owner || !i.colour_contract.exposure_resource.resource.valid() ||
         !i.colour_contract.exposure_resource.generation || !i.colour_contract.exposure_shape.valid())) return false;
    const Resource* resources[] = {&r.colour, &r.depth, &r.motion, &r.output};
    const Selection* selections[] = {&i.colour_selection, &i.depth_selection, &i.motion_selection, &i.output_selection};
    for (std::size_t n = 0; n < 4; ++n) {
        if (!selections[n]->generation || !(selections[n]->resource == resources[n]->id)) return false;
    }
    for (const Resource* p : {&r.colour, &r.depth, &r.motion, &r.output}) {
        if (!p->owner || !p->id.valid() || !(p->produced_for == i.key) ||
            p->device_generation != i.generations.device || !p->producer_stamp) return false;
    }
    return true;
}
struct Provenance {
    Identity resource;
    EyeFrame produced_for;
    std::uint64_t device_generation{}, producer_stamp{};
};
inline Provenance provenance(const Resource& r) noexcept {
    return {r.id, r.produced_for, r.device_generation, r.producer_stamp};
}
inline bool operator==(Provenance a, Provenance b) noexcept {
    return a.resource == b.resource && a.produced_for == b.produced_for &&
           a.device_generation == b.device_generation && a.producer_stamp == b.producer_stamp;
}
struct ExecutionInputs {
    bool backend_enabled{};
    std::array<InputIdentity, 2> expected{};
    // Adapter's observed producer ledger, not a recopy of the frozen receipt.
    std::array<Provenance, 2> depth{}, motion{};
};
// All callbacks are nonthrowing. evaluate(false) must leave fallback usable; the
// adapter saves/restores any immediate pipeline state touched by these operations.
struct Executor {
    virtual ~Executor() = default;
    virtual void native(const List&, bool restore) noexcept = 0;
    virtual bool snapshot_depth(const RenderInputs&) noexcept = 0;
    virtual bool evaluate(const RenderInputs&) noexcept = 0;
    virtual bool fallback(const RenderInputs&) noexcept = 0;
    // Explicit legacy/native-only node: no qualified temporal receipt is implied.
    virtual bool fallback_only(const Lease&) noexcept = 0;
};
class RetainedJobs {
public:
    static constexpr std::size_t capacity = 8;
    struct Reservation {
        std::uint64_t chain{}, node{};
        explicit operator bool() const noexcept { return chain != 0; }
    };
    struct Submission {
        std::uint64_t serial{};
        explicit operator bool() const noexcept { return serial != 0; }
    };
    enum class ExecuteStatus { miss, busy, submitted };
    struct ExecuteResult {
        ExecuteStatus status = ExecuteStatus::miss;
        Submission submission;
        List deferred; // Incoming work retained on busy OR miss; no native work consumed.
        bool restore{};
        PairState pair_state = PairState::incomplete;
    };
    enum class Completion { pending, completed, failed, device_abandoned };
private:
    enum class NodeKind { temporal, fallback_only };
    struct Node {
        RenderInputs inputs;
        List prefix;
        std::uint64_t serial{};
        NodeKind kind = NodeKind::temporal;
        Lease fallback_owner;
    };
    enum class Phase { recording, sealed, executing, submitted, quarantined };
    struct Chain {
        Identity context;
        Lease context_owner;
        std::uint64_t serial{};
        Phase phase = Phase::recording;
        bool pending{};
        std::vector<Node> nodes;
        List remainder;
    };
    std::mutex mutex_;
    std::array<std::shared_ptr<Chain>, capacity> slots_{};
    std::uint64_t serial_{};
    PairPublication publication_;
    bool executing_{}; // One ordered immediate stream per queue, including callback reentry.
    std::uint64_t next() noexcept { return ++serial_; }
    Chain* find(Reservation r) noexcept {
        for (auto& s : slots_) if (s && s->serial == r.chain && s->phase == Phase::recording &&
            s->pending && !s->nodes.empty() && s->nodes.back().serial == r.node) return s.get();
        return nullptr;
    }
    bool list_used(Identity id) const noexcept {
        for (const auto& s : slots_) if (s) {
            if (s->remainder.id == id) return true;
            for (const auto& n : s->nodes) if (n.prefix.id == id) return true;
        }
        return false;
    }
public:
    RetainedJobs() = default;
    RetainedJobs(const RetainedJobs&) = delete;
    RetainedJobs& operator=(const RetainedJobs&) = delete;
    // The queue itself MUST outlive GPU retirement (including device teardown).
    // Destruction is permitted only after draining all native work and externally
    // confirming completion/device abandonment. No CPU return implies GPU completion.
    // reserve copies inputs and allocates everything needed BEFORE any native split.
    // A false return means keep the original native dispatch; no work was consumed.
    Reservation reserve(Identity context, Lease context_owner, const RenderInputs& inputs) {
        if (!valid(inputs)) return {};
        return reserve_node(context, std::move(context_owner), Node{inputs, {}, 0, NodeKind::temporal, {}});
    }
    Reservation reserve_fallback(Identity context, Lease context_owner, Lease fallback_owner) {
        if (!fallback_owner) return {};
        return reserve_node(context, std::move(context_owner), Node{{}, {}, 0, NodeKind::fallback_only, std::move(fallback_owner)});
    }
private:
    Reservation reserve_node(Identity context, Lease context_owner, Node node) {
        if (!context.valid() || !context_owner) return {};
        std::lock_guard<std::mutex> lock(mutex_);
        std::shared_ptr<Chain>* free_slot = nullptr;
        Chain* chain = nullptr;
        for (auto& s : slots_) {
            if (!s && !free_slot) free_slot = &s;
            if (s && s->context == context && s->phase == Phase::recording) chain = s.get();
        }
        if (chain && chain->pending) return {};
        if (!chain) {
            if (!free_slot) return {};
            auto created = std::make_shared<Chain>();
            created->context = context;
            created->serial = next();
            node.serial = next();
            created->nodes.push_back(std::move(node));
            // Transfer the possibly sole lease only after the last throwing allocation.
            // On failure, the parameter is destroyed after the lock guard unwinds.
            created->context_owner = std::move(context_owner);
            created->pending = true;
            *free_slot = std::move(created);
            chain = free_slot->get();
        } else {
            node.serial = next();
            chain->nodes.push_back(std::move(node));
            chain->pending = true;
        }
        return {chain->serial, chain->nodes.back().serial};
    }
public:
    // Call only when FinishCommandList(TRUE) succeeded and ownership is retained.
    // TRUE restores deferred recording state; it is unrelated to execute restore.
    // No allocation occurs here. A failed native split must instead call cancel().
    // Rejected attachment does not consume the caller's list: keep native work
    // on the adapter's recovery path and cancel; never suppress it on failure.
    bool split(Reservation r, List prefix) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        auto* c = find(r);
        if (!c || !prefix.owner || !prefix.id.valid() || list_used(prefix.id)) return false;
        c->nodes.back().prefix = std::move(prefix);
        c->pending = false;
        return true;
    }
    bool cancel(Reservation r) noexcept {
        Node cancelled;
        std::shared_ptr<Chain> released;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto* c = find(r);
            if (!c) return false;
            cancelled = std::move(c->nodes.back());
            c->nodes.pop_back();
            c->pending = false;
            if (c->nodes.empty()) for (auto& s : slots_) if (s.get() == c) {
                released = std::move(s);
                break;
            }
        }
        // Custom lease deleters may reenter the queue; never release under its lock.
        return true;
    }
    // Attaching the real remainder seals list identities AND the complete chain.
    bool finished(Identity context, List remainder) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!remainder.owner || !remainder.id.valid() || list_used(remainder.id)) return false;
        for (auto& s : slots_) if (s && s->context == context && s->phase == Phase::recording) {
            if (s->pending) return false;
            s->remainder = std::move(remainder);
            s->phase = Phase::sealed;
            return true;
        }
        return false;
    }
    // incoming MUST carry the real list identity and a strong native-list lease.
    // One queue represents ONE immediate execution stream. Busy is NOT a miss:
    // adapters MUST retain/defer the returned list+restore (including unknown native
    // lists), suppress immediate forwarding, and retry in arrival order after the
    // active call unwinds. Refresh ExecutionInputs from the real producer ledger
    // on retry. Only miss permits native forwarding of result.deferred with its
    // result.restore; even a moved retry retains that lease in the miss result.
    // Every native invocation that
    // can overwrite these inputs must share this serialization boundary.
    ExecuteResult execute(List incoming, bool restore, ExecutionInputs current, Executor& executor) noexcept {
        std::shared_ptr<Chain> chain;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (executing_) return {ExecuteStatus::busy, {}, std::move(incoming), restore};
            for (auto& s : slots_) if (s && s->phase == Phase::sealed && s->remainder.id == incoming.id) {
                chain = s;
                s->phase = Phase::executing;
                executing_ = true;
                break;
            }
        }
        if (!chain) return {ExecuteStatus::miss, {}, std::move(incoming), restore};
        bool fallback_only_seen=false,fallback_only_succeeded=true;
        for (const auto& n : chain->nodes) {
            executor.native(n.prefix, restore);
            if (n.kind == NodeKind::fallback_only) {
                fallback_only_seen=true;fallback_only_succeeded=executor.fallback_only(n.fallback_owner) && fallback_only_succeeded;
                publication_.fallback_only(fallback_only_succeeded);continue;
            }
            const auto& r = n.inputs;
            const auto eye = r.identity.key.eye;
            const bool fresh = current.backend_enabled && r.identity == current.expected[eye] &&
                provenance(r.depth) == current.depth[eye] && provenance(r.motion) == current.motion[eye];
            if (fresh && executor.snapshot_depth(r) && executor.evaluate(r)) publication_.observe(r.identity,r.feature_owner,EyeOutcome::temporal);
            else publication_.observe(r.identity,r.feature_owner,executor.fallback(r)?EyeOutcome::fallback:EyeOutcome::failed);
            if(fallback_only_seen)publication_.fallback_only(fallback_only_succeeded);
        }
        executor.native(chain->remainder, restore);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            chain->phase = Phase::submitted;
            executing_ = false;
        }
        return {ExecuteStatus::submitted, {chain->serial}, {}, restore, publication_.state()};
    }
    // Adapter signals only AFTER fence/query completion (or proven device loss).
    // Until then queue owns every list/resource/context/feature even if token is lost.
    // Early/reentrant retirement while CPU submission is active is rejected.
    bool retire(Submission submission) noexcept {return retire(submission,Completion::completed);}
    // Pending retains ownership; query/device errors quarantine it. A quarantined
    // submission requires proven device abandonment, never a later CPU/timer guess.
    bool retire(Submission submission,Completion proof) noexcept {
        std::shared_ptr<Chain> released;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& s : slots_) if (s && s->serial == submission.serial && (s->phase == Phase::submitted || s->phase == Phase::quarantined)) {
                if(proof==Completion::pending) return false;
                if(proof==Completion::failed) {s->phase=Phase::quarantined;return false;}
                if(s->phase==Phase::quarantined && proof!=Completion::device_abandoned) return false;
                released = std::move(s);
                break;
            }
        }
        return bool(released);
    }
};
} // namespace temporal_eye
