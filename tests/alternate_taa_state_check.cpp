#include "../src/alternate_taa_state.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace d2vr::alternate_taa;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

static Key key(int eye, std::uint64_t display, bool single = true) {
    return Key{single ? 100u : 100u + static_cast<unsigned>(eye), 200,
        {300, 400, 500}, 1, 2, display, static_cast<std::uint32_t>(display), 1200, 1300, eye, single};
}
static Snapshot sample(float marker) {
    Snapshot s;
    for (std::size_t m = 0; m < s.matrices.size(); ++m)
        for (std::size_t i = 0; i < 16; ++i) s.matrices[m][i] = marker + static_cast<float>(m * 16 + i);
    s.position = {marker, marker + 1, marker + 2};
    s.jitter = {marker / 100, -marker / 200};
    return s;
}
static bool equal(const Snapshot& a, const Snapshot& b) {
    return a.matrices == b.matrices && a.position == b.position && a.jitter == b.jitter;
}
static Receipt render(History& h, const Key& k, float marker, bool reset = false) {
    const auto p = h.prepare(k, reset); CHECK(p);
    const auto r = h.complete(*p, sample(marker)); CHECK(r); return *r;
}
static void own_eye_history_and_duplicates() {
    History h;
    const auto left = render(h, key(0, 10), 10);
    const auto right = render(h, key(1, 11), 100);
    CHECK(left.phase == 0 && right.phase == 0 && left.reset && right.reset);
    CHECK(equal(left.current, left.previous) && equal(right.current, right.previous));
    auto p = h.prepare(key(0, 12), false); CHECK(p && p->phase == 1 && p->has_previous && !p->reset);
    CHECK(equal(p->previous, left.current));
    auto r = h.complete(*p, sample(20)); CHECK(r && !r->admitted);
    auto duplicate = h.prepare(key(0, 12), false);
    CHECK(duplicate && duplicate->duplicate && duplicate->phase == 1);
    CHECK(equal(duplicate->previous, left.current));
    auto replacement = h.complete(*duplicate, sample(21)); CHECK(replacement);
    CHECK(equal(replacement->previous, left.current)); // includes that eye's previous jitter
    CHECK(replacement->phase == 1);
    const auto next_right = render(h, key(1, 13), 110);
    CHECK(next_right.phase == 1 && equal(next_right.previous, right.current));
    const auto next_left = render(h, key(0, 14), 30);
    CHECK(next_left.phase == 2 && equal(next_left.previous, sample(21)));
    CHECK(!h.complete(*p, sample(99))); // superseded transaction cannot publish
    History paired;
    for (std::uint64_t d = 20; d < 23; ++d) {
        auto l = render(paired, key(0, d, false), static_cast<float>(d));
        auto rgt = render(paired, key(1, d, false), static_cast<float>(d + 100));
        CHECK(l.phase == d - 20 && rgt.phase == d - 20);
    }
}
static void generations() {
    History h;
    CHECK(h.generation() == 0);
    render(h, key(0, 10), 10);
    CHECK(h.generation() == 0);
    render(h, key(1, 11), 11);
    CHECK(h.generation() == 0); // either eye's initial bootstrap leaves global ownership intact
    render(h, key(0, 12), 12);
    CHECK(h.generation() == 0);
    render(h, key(1, 13), 13, true);
    CHECK(h.generation() == 1);
    render(h, key(0, 14), 14);
    CHECK(h.generation() == 1);
    Key changed = key(1, 15); ++changed.reference_epoch;
    render(h, changed, 15);
    CHECK(h.generation() == 2);
    h.reset(); CHECK(h.generation() == 3);
    h.reset(); CHECK(h.generation() == 4);
    render(h, key(0, 20), 20);
    render(h, key(1, 21), 21);
    CHECK(h.generation() == 4);
}
static void resets() {
    for (int mutation = 0; mutation < 13; ++mutation) {
        History h;
        render(h, key(0, 10), 10); render(h, key(1, 11), 100);
        Key changed = key(0, 12);
        switch (mutation) {
        case 0: ++changed.owner[0]; break;
        case 1: ++changed.owner[1]; break;
        case 2: ++changed.owner[2]; break;
        case 3: ++changed.allocation; break;
        case 4: ++changed.pose_epoch; break;
        case 5: ++changed.reference_epoch; break;
        case 6: ++changed.width; break;
        case 7: ++changed.height; break;
        case 8: changed.single = false; break;
        case 9: ++changed.view; break;
        case 10: changed.display = 14; break;
        case 11: changed.display = 9; break;
        case 12: break;
        }
        auto stale = h.prepare(key(1, 13), false); CHECK(stale);
        auto p = h.prepare(changed, mutation == 12); CHECK(p && p->reset && !p->has_previous && p->phase == 0);
        CHECK(!h.complete(*stale, sample(777))); // either eye's reset cancels both active transactions
        auto done = h.complete(*p, sample(20)); CHECK(done && equal(done->previous, done->current));
        Key right = changed; right.eye = 1; ++right.display; ++right.native_counter;
        if (!right.single) ++right.view;
        auto other = h.prepare(right, false); CHECK(other && other->reset && other->phase == 0 && !other->has_previous);
    }
    History h;
    auto p = h.prepare(key(0, 10), false); CHECK(p);
    auto again = h.prepare(key(0, 10), false); CHECK(again && again->duplicate && again->phase == 0);
    auto r = h.complete(*again, sample(1)); CHECK(r && r->reset && equal(r->current, r->previous));
    h.reset(); CHECK(!h.complete(*p, sample(2)));
    auto new_p = h.prepare(key(0, 10), false); CHECK(new_p && new_p->transaction != p->transaction);
}
static void invalid_data() {
    for (int mutation = 0; mutation < 10; ++mutation) {
        History h; Key k = key(0, 10);
        switch (mutation) {
        case 0: k.view = 0; break; case 1: k.allocation = 0; break;
        case 2: k.owner[0] = 0; break; case 3: k.owner[1] = 0; break;
        case 4: k.pose_epoch = 0; break; case 5: k.reference_epoch = 0; break;
        case 6: k.display = 0; break; case 7: k.width = 0; break; case 8: k.height = 0; break;
        case 9: k.eye = 2; break;
        }
        CHECK(!h.prepare(k, false));
    }
    History zero_generation; ReceiptBook zero_book;
    Key zero = key(0, 10); zero.owner[2] = 0; zero.native_counter = 0;
    CHECK(valid(zero));
    const auto zero_receipt = render(zero_generation, zero, 10);
    CHECK(zero_book.publish(zero_receipt));
    CHECK(zero_book.admit(zero.view, 0, zero.display, zero.pose_epoch, zero.reference_epoch));
    CHECK(zero_book.resolve(zero.view, 0));
    zero.display += 2; zero.native_counter = 2;
    auto continuous = zero_generation.prepare(zero, false);
    CHECK(continuous && !continuous->reset && continuous->phase == 1);
    CHECK(zero_generation.complete(*continuous, sample(12)));
    ++zero.owner[2]; zero.display += 2; ++zero.native_counter;
    auto generation_changed = zero_generation.prepare(zero, false);
    CHECK(generation_changed && generation_changed->reset && !generation_changed->has_previous);
    History h; auto p = h.prepare(key(0, 10), false); CHECK(p);
    Snapshot bad = sample(1); bad.matrices[2][15] = std::numeric_limits<float>::quiet_NaN(); CHECK(!h.complete(*p, bad));
    bad = sample(1); bad.position[2] = std::numeric_limits<float>::infinity(); CHECK(!h.complete(*p, bad));
    bad = sample(1); bad.jitter[1] = std::numeric_limits<float>::quiet_NaN(); CHECK(!h.complete(*p, bad));
    auto good = h.complete(*p, sample(3)); CHECK(good && good->phase == 0);
}
static void receipt_admission() {
    History h; ReceiptBook book;
    auto first = render(h, key(0, 10), 10);
    CHECK(book.publish(first)); CHECK(!book.resolve(first.key.view, first.key.native_counter));
    CHECK(!book.admit(100, 10, 11, 1, 2)); CHECK(!book.admit(100, 10, 10, 2, 2));
    CHECK(!book.admit(100, 10, 10, 1, 3)); CHECK(!book.admit(101, 10, 10, 1, 2));
    auto admitted = book.admit(100, 10, 10, 1, 2); CHECK(admitted && admitted->admitted);
    auto duplicate = render(h, key(0, 10), 11); CHECK(book.publish(duplicate));
    auto resolved = book.resolve(100, 10); CHECK(resolved && equal(resolved->current, first.current));
    // Returned copies, duplicate Setup, and future Setup cannot mutate admitted constants.
    admitted->current = sample(999);
    auto future = render(h, key(0, 12), 12); CHECK(book.publish(future));
    CHECK(equal(book.resolve(100, 10)->current, first.current));
    for (int mutation = 0; mutation < 3; ++mutation) {
        auto conflict = first;
        if (mutation == 0) conflict.key.eye = 1;
        if (mutation == 1) ++conflict.key.display;
        if (mutation == 2) ++conflict.key.owner[1];
        CHECK(!book.publish(conflict));
    }
    CHECK(!book.resolve(100, 12)); CHECK(!book.resolve(999, 10));
    CHECK(book.admit(100, 12, 12, 1, 2)); CHECK(book.resolve(100, 12));
    for (std::uint64_t d = 14; d <= 28; d += 2) CHECK(book.publish(render(h, key(0, d), static_cast<float>(d))));
    CHECK(!book.resolve(100, 10)); CHECK(!book.admit(100, 10, 10, 1, 2));
    auto latest = book.admit(100, 28, 28, 1, 2); CHECK(latest && latest->key.display == 28);
    book.reset(); CHECK(!book.resolve(100, 28));
}
int main() {
    own_eye_history_and_duplicates(); generations(); resets(); invalid_data(); receipt_admission();
    std::puts("alternate TAA state checks passed");
}
