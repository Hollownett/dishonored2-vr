#pragma once
// Pure temporal ownership. Offsets describe the pinned executable; this file never reads native memory.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace d2vr::alternate_taa {
inline constexpr std::array<std::uintptr_t, 3> current_matrix_offsets{0x7390, 0x7450, 0x7510};
inline constexpr std::array<std::uintptr_t, 3> previous_matrix_offsets{0x7410, 0x74d0, 0x7590};
inline constexpr std::uintptr_t position_offset = 0x3a78, previous_position_offset = 0x3a84;
inline constexpr std::uintptr_t jitter_offset = 0x7850, previous_jitter_offset = 0x7858;
inline constexpr std::uintptr_t stamp_offset = 0x77b4, reset_offset = 0xbcfc;

struct Snapshot {
    std::array<std::array<float, 16>, 3> matrices{};
    std::array<float, 3> position{};
    std::array<float, 2> jitter{};
};
inline bool finite(const Snapshot& value) noexcept {
    for (const auto& matrix : value.matrices) for (float x : matrix) if (!std::isfinite(x)) return false;
    for (float x : value.position) if (!std::isfinite(x)) return false;
    for (float x : value.jitter) if (!std::isfinite(x)) return false;
    return true;
}
struct Key {
    std::uintptr_t view{}, allocation{};
    std::array<std::uintptr_t, 3> owner{};
    std::uint64_t pose_epoch{}, reference_epoch{}, display{};
    std::uint32_t native_counter{}, width{}, height{};
    int eye{-1};
    bool single{};
};
inline bool valid(const Key& k) noexcept {
    return k.view && k.allocation && k.owner[0] && k.owner[1] &&
        k.pose_epoch && k.reference_epoch && k.display && k.width && k.height && (k.eye == 0 || k.eye == 1);
}
inline bool same_key(const Key& a, const Key& b) noexcept {
    return a.view == b.view && a.allocation == b.allocation && a.owner == b.owner &&
        a.pose_epoch == b.pose_epoch && a.reference_epoch == b.reference_epoch && a.display == b.display &&
        a.native_counter == b.native_counter && a.width == b.width && a.height == b.height &&
        a.eye == b.eye && a.single == b.single;
}
inline bool same_context(const Key& a, const Key& b) noexcept {
    return a.pose_epoch == b.pose_epoch && a.reference_epoch == b.reference_epoch &&
        a.width == b.width && a.height == b.height && a.single == b.single;
}
inline bool same_owner(const Key& a, const Key& b) noexcept {
    return a.view == b.view && a.allocation == b.allocation && a.owner == b.owner;
}
struct Prepared {
    Key key{};
    Snapshot previous{};
    std::uint32_t phase{};
    bool reset{}, duplicate{}, has_previous{};
    std::uint64_t transaction{};
    std::uint32_t previous_counter{}; // v1206: native_counter of the render `previous` was taken from (has_previous only)
};
struct Receipt {
    Key key{};
    Snapshot current{}, previous{};
    std::uint32_t phase{};
    bool reset{}, admitted{};
    std::uint32_t previous_counter{}; // v1206: the engine frame of this eye's render that `previous` is (== key.native_counter on a reset)
};

class History {
public:
    void reset() noexcept { eyes_ = {}; context_.reset(); ++generation_; } // tokens and generations survive reset
    std::uint64_t generation() const noexcept { return generation_; }
    std::optional<Prepared> prepare(const Key& key, bool native_reset) noexcept {
        if (!valid(key)) return std::nullopt;
        bool invalidate = native_reset;
        if (context_) {
            invalidate |= !same_context(*context_, key);
            if (context_->single && key.single) invalidate |= !same_owner(*context_, key);
        }
        const auto& old = eyes_[static_cast<std::size_t>(key.eye)];
        if (old.last) {
            const Key& last = old.last->key;
            const bool duplicate = same_key(last, key);
            invalidate |= !same_context(last, key) || !same_owner(last, key);
            if (!duplicate) invalidate |= key.display <= last.display ||
                key.display - last.display != (key.single ? 2u : 1u);
        }
        if (invalidate) reset();
        context_ = key;
        auto& eye = eyes_[static_cast<std::size_t>(key.eye)];
        if (eye.active && same_key(eye.active->key, key)) {
            Prepared p = *eye.active;
            p.duplicate = true;
            return p;
        }
        Prepared p;
        p.key = key;
        p.transaction = ++next_transaction_;
        p.reset = !eye.last;
        p.has_previous = eye.last.has_value();
        if (eye.last) {
            p.previous = eye.last->current;
            p.phase = eye.last->phase + 1u;
            p.previous_counter = eye.last->key.native_counter;
        }
        eye.active = p;
        return p;
    }
    std::optional<Receipt> complete(const Prepared& prepared, const Snapshot& current) noexcept {
        if (!valid(prepared.key) || !finite(current)) return std::nullopt;
        auto& eye = eyes_[static_cast<std::size_t>(prepared.key.eye)];
        if (!eye.active || prepared.transaction != eye.active->transaction ||
            !same_key(prepared.key, eye.active->key)) return std::nullopt;
        // Use the stored transaction, so caller-side edits cannot change temporal ownership.
        const Prepared& active = *eye.active;
        Receipt receipt{active.key, current, active.has_previous ? active.previous : current,
                        active.phase, active.reset, false,
                        active.has_previous ? active.previous_counter : active.key.native_counter};
        eye.last = receipt;
        return receipt;
    }
private:
    struct Eye { std::optional<Receipt> last; std::optional<Prepared> active; };
    std::array<Eye, 2> eyes_{};
    std::optional<Key> context_;
    std::uint64_t next_transaction_{};
    std::uint64_t generation_{};
};

class ReceiptBook {
public:
    void reset() noexcept { records_ = {}; next_ = 0; }
    bool publish(const Receipt& receipt) noexcept {
        if (!valid(receipt.key) || !finite(receipt.current) || !finite(receipt.previous)) return false;
        for (auto& record : records_) {
            if (!record || record->key.view != receipt.key.view ||
                record->key.native_counter != receipt.key.native_counter) continue;
            // A native lookup pair must never name two logical renders, even before admission.
            if (!same_key(record->key, receipt.key)) return false;
            if (record->admitted) return true; // admission freezes the captured constants
            record = receipt;
            record->admitted = false;
            return true;
        }
        records_[next_] = receipt;
        records_[next_]->admitted = false;
        next_ = (next_ + 1u) % records_.size();
        return true;
    }
    std::optional<Receipt> admit(std::uintptr_t view, std::uint32_t native_counter,
        std::uint64_t display, std::uint64_t pose_epoch, std::uint64_t reference_epoch) noexcept {
        for (auto& record : records_) {
            if (record && record->key.view == view && record->key.native_counter == native_counter &&
                record->key.display == display && record->key.pose_epoch == pose_epoch &&
                record->key.reference_epoch == reference_epoch) {
                record->admitted = true;
                return record;
            }
        }
        return std::nullopt;
    }
    std::optional<Receipt> resolve(std::uintptr_t view, std::uint32_t native_counter) const noexcept {
        for (const auto& record : records_) if (record && record->admitted &&
            record->key.view == view && record->key.native_counter == native_counter) return record;
        return std::nullopt;
    }
private:
    std::array<std::optional<Receipt>, 8> records_{};
    std::size_t next_{};
};
} // namespace d2vr::alternate_taa
