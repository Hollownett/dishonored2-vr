#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

// Portable retained-command contract. Adapters own native API/ABI knowledge.
namespace temporal_eye {
using Lease = std::shared_ptr<const void>; // COM adapters can supply a Release deleter.
struct Identity {
    std::uintptr_t address{};
    std::uint64_t incarnation{}; // Monotonic adapter-issued identity; never just an address.
    bool valid() const noexcept { return address != 0 && incarnation != 0; }
};
inline bool operator==(Identity a, Identity b) noexcept {
    return a.address == b.address && a.incarnation == b.incarnation;
}
struct List { Identity id; Lease owner; };
struct EyeFrame {
    std::uint64_t frame{}, pair{};
    unsigned eye{};
};
inline bool operator==(EyeFrame a, EyeFrame b) noexcept {
    return a.frame == b.frame && a.pair == b.pair && a.eye == b.eye;
}
struct Generations {
    std::uint64_t device{}, backend{}, feature{}, reset{}, jitter{};
};
inline bool operator==(Generations a, Generations b) noexcept {
    return a.device == b.device && a.backend == b.backend && a.feature == b.feature &&
           a.reset == b.reset && a.jitter == b.jitter;
}
struct Shape {
    std::uint32_t width{}, height{}, x{}, y{}, rect_width{}, rect_height{}, format{};
    bool valid() const noexcept {
        return width && height && format && rect_width && rect_height && x <= width && y <= height &&
               rect_width <= width - x && rect_height <= height - y;
    }
};
inline bool operator==(Shape a, Shape b) noexcept {
    return a.width == b.width && a.height == b.height && a.x == b.x && a.y == b.y &&
           a.rect_width == b.rect_width && a.rect_height == b.rect_height && a.format == b.format;
}
struct InputIdentity {
    EyeFrame key;
    Generations generations;
    Shape colour, depth, motion, output;
};
inline bool operator==(const InputIdentity& a, const InputIdentity& b) noexcept {
    return a.key == b.key && a.generations == b.generations && a.colour == b.colour &&
           a.depth == b.depth && a.motion == b.motion && a.output == b.output;
}
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
    Lease feature_owner;
    float jitter_x{}, jitter_y{}, mv_scale_x{}, mv_scale_y{};
    bool reset_history{};
};
inline bool valid(const RenderInputs& r) noexcept {
    const auto& i = r.identity;
    if (i.key.eye > 1 || !i.key.frame || !i.key.pair || !i.generations.device ||
        !i.generations.backend || !i.generations.feature || !i.generations.reset ||
        !i.generations.jitter || !i.colour.valid() || !i.depth.valid() ||
        !i.motion.valid() || !i.output.valid() || !r.feature_owner ||
        !std::isfinite(r.jitter_x) || !std::isfinite(r.jitter_y) ||
        !std::isfinite(r.mv_scale_x) || !std::isfinite(r.mv_scale_y)) return false;
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
    virtual void fallback(const RenderInputs&) noexcept = 0;
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
private:
    struct Node {
        RenderInputs inputs;
        List prefix;
        std::uint64_t serial{};
    };
    enum class Phase { recording, sealed, executing, submitted };
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
        if (!context.valid() || !context_owner || !valid(inputs)) return {};
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
            created->context_owner = std::move(context_owner);
            created->serial = next();
            created->nodes.push_back(Node{inputs, {}, next()});
            created->pending = true;
            *free_slot = std::move(created);
            chain = free_slot->get();
        } else {
            chain->nodes.push_back(Node{inputs, {}, next()});
            chain->pending = true;
        }
        return {chain->serial, chain->nodes.back().serial};
    }
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
    // Execution inputs are copied before callbacks. A detached sealed chain cannot
    // be replayed/mutated by reentrant callbacks. A miss means caller executes native.
    Submission execute(Identity remainder, bool restore, ExecutionInputs current, Executor& executor) noexcept {
        std::shared_ptr<Chain> chain;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& s : slots_) if (s && s->phase == Phase::sealed && s->remainder.id == remainder) {
                chain = s;
                s->phase = Phase::executing;
                break;
            }
        }
        if (!chain) return {};
        for (const auto& n : chain->nodes) {
            executor.native(n.prefix, restore);
            const auto& r = n.inputs;
            const auto eye = r.identity.key.eye;
            const bool fresh = current.backend_enabled && r.identity == current.expected[eye] &&
                provenance(r.depth) == current.depth[eye] && provenance(r.motion) == current.motion[eye];
            if (!fresh || !executor.snapshot_depth(r) || !executor.evaluate(r)) executor.fallback(r);
        }
        executor.native(chain->remainder, restore);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            chain->phase = Phase::submitted;
        }
        return {chain->serial};
    }
    // Adapter signals only AFTER fence/query completion (or proven device loss).
    // Until then queue owns every list/resource/context/feature even if token is lost.
    // Early/reentrant retirement while CPU submission is active is rejected.
    bool retire(Submission submission) noexcept {
        std::shared_ptr<Chain> released;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& s : slots_) if (s && s->serial == submission.serial && s->phase == Phase::submitted) {
                released = std::move(s);
                break;
            }
        }
        return bool(released);
    }
};
} // namespace temporal_eye
