#include "../src/alternate_native_taa.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>
using namespace d2vr::alternate_taa;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

namespace {
constexpr std::size_t view_size = 0xc000;
std::array<unsigned char, view_size * 2> memory{};
unsigned writes{}, fail_write{};
bool partial_failure{}, change_stamp_on_matrix_read{};
Runtime* invalidate_on_read{};
Runtime* classification_probe{};
std::uintptr_t base() { return reinterpret_cast<std::uintptr_t>(memory.data()); }
std::uintptr_t view(unsigned index = 0) { return base() + view_size * index; }
bool in_bounds(std::uintptr_t address, std::size_t count) {
    return address >= base() && address - base() <= memory.size() && count <= memory.size() - (address - base());
}
bool read_bytes(std::uintptr_t address, void* out, std::size_t count) {
    if (!in_bounds(address, count)) return false;
    std::memcpy(out, reinterpret_cast<const void*>(address), count);
    if (classification_probe) CHECK(classification_probe->single_view == view());
    if (invalidate_on_read && address == view() + current_matrix_offsets[0]) {
        auto* runtime = invalidate_on_read; invalidate_on_read = nullptr; runtime->invalidate();
    }
    if (change_stamp_on_matrix_read && address == view() + current_matrix_offsets[0]) {
        change_stamp_on_matrix_read = false;
        std::uint32_t stamp{}; std::memcpy(&stamp, reinterpret_cast<void*>(view() + stamp_offset), 4);
        ++stamp; std::memcpy(reinterpret_cast<void*>(view() + stamp_offset), &stamp, 4);
    }
    return true;
}
bool write_bytes(std::uintptr_t address, const void* in, std::size_t count) {
    if (!in_bounds(address, count)) return false;
    if (classification_probe) CHECK(classification_probe->single_view == view());
    if (++writes == fail_write) {
        if (partial_failure) std::memcpy(reinterpret_cast<void*>(address), in, count / 2);
        return false; // fail once, allowing the rollback writes to succeed
    }
    std::memcpy(reinterpret_cast<void*>(address), in, count); return true;
}
template<class T> void put(std::uintptr_t address, const T& value) {
    CHECK(in_bounds(address, sizeof(value))); std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}
template<class T> T get(std::uintptr_t address) {
    T result{}; CHECK(read_bytes(address, &result, sizeof(result))); return result;
}
Snapshot sample(float marker) {
    Snapshot result;
    for (std::size_t m = 0; m < 3; ++m) for (std::size_t i = 0; i < 16; ++i)
        result.matrices[m][i] = marker + static_cast<float>(m * 16 + i);
    result.position = {marker, marker + 1, marker + 2}; result.jitter = {marker / 100, -marker / 200};
    return result;
}
void put_current(std::uintptr_t address, const Snapshot& s) {
    for (std::size_t i = 0; i < 3; ++i) put(address + current_matrix_offsets[i], s.matrices[i]);
    put(address + position_offset, s.position); put(address + jitter_offset, s.jitter);
}
void put_previous_matrices_position(std::uintptr_t address, const Snapshot& s) {
    for (std::size_t i = 0; i < 3; ++i) put(address + previous_matrix_offsets[i], s.matrices[i]);
    put(address + previous_position_offset, s.position);
}
Snapshot previous(std::uintptr_t address) {
    Snapshot s;
    for (std::size_t i = 0; i < 3; ++i) s.matrices[i] = get<std::array<float,16>>(address + previous_matrix_offsets[i]);
    s.position = get<std::array<float,3>>(address + previous_position_offset);
    s.jitter = get<std::array<float,2>>(address + previous_jitter_offset); return s;
}
Key key(int eye, std::uint64_t display, bool single = true) {
    return Key{view(single ? 0u : static_cast<unsigned>(eye)), 200, {300,400,0},
        1,2,display,static_cast<std::uint32_t>(display),1200,1300,eye,single};
}
void initialize(Runtime& runtime) {
    invalidate_on_read = classification_probe = nullptr;
    memory.fill(0); writes = fail_write = 0; partial_failure = change_stamp_on_matrix_read = false;
    put_current(view(), sample(-100)); put_current(view(1), sample(-200));
    runtime.configure(read_bytes, write_bytes);
}
// Models the independently verified native Setup branches, without calling the game:
// stamp-gated matrix/position rollover, unconditional jitter rollover, then reset copies new matrices/position only.
void original_setup(const Key& k, const Snapshot& next) {
    Snapshot old{}; CHECK(read_snapshot(read_bytes, k.view, old));
    if (get<std::uint32_t>(k.view + stamp_offset) != k.native_counter) {
        put_previous_matrices_position(k.view, old); put(k.view + stamp_offset, k.native_counter);
    }
    put(k.view + previous_jitter_offset, old.jitter);
    put_current(k.view, next);
    if (get<unsigned char>(k.view + reset_offset)) put_previous_matrices_position(k.view, next);
    put(k.view + reset_offset, static_cast<unsigned char>(0));
}
std::uint32_t setup(Runtime& runtime, const Key& k, const Snapshot& next) {
    const auto phase = runtime.plan(k); CHECK(phase); CHECK(runtime.seed(k.view));
    original_setup(k, next); const auto published = runtime.finish(k.view); CHECK(!runtime.failed);
    CHECK(published && published->key.native_counter == k.native_counter && same_snapshot(published->current, next)); // v1206: finish hands the receipt over
    return *phase;
}
Receipt admit(Runtime& runtime, const Key& k) {
    CHECK(runtime.begin(k.view, k.display, k.pose_epoch, k.reference_epoch));
    auto r = runtime.resolve(k.view); CHECK(r && r->admitted); return *r;
}
void alternating_continuity() {
    Runtime runtime; initialize(runtime);
    std::array<Snapshot,2> last{};
    for (std::uint64_t d = 10; d < 18; ++d) {
        const int eye = static_cast<int>(d & 1u); const Key k = key(eye,d);
        const Snapshot current = sample(static_cast<float>(1000 * eye + d));
        CHECK(setup(runtime,k,current) == (d-10)/2);
        CHECK(!runtime.resolve(k.view)); // Setup publication alone cannot authorize workers
        const auto receipt = admit(runtime,k);
        CHECK(same_snapshot(receipt.current,current));
        CHECK(same_snapshot(receipt.previous,d < 12 ? current : last[static_cast<std::size_t>(eye)]));
        CHECK(same_snapshot(previous(k.view),receipt.previous));
        CHECK(receipt.previous_counter == (d < 12 ? k.native_counter : k.native_counter - 2)); // v1206: the render `previous` came from
        last[static_cast<std::size_t>(eye)] = current;
    }
    CHECK(runtime.completed == 8 && runtime.admitted == 8);
}
void duplicate_setup() {
    Runtime runtime; initialize(runtime);
    Key k = key(0,10); CHECK(setup(runtime,k,sample(10)) == 0); admit(runtime,k);
    CHECK(setup(runtime,k,sample(10)) == 0); auto first = admit(runtime,k);
    CHECK(same_snapshot(first.previous,sample(10)));
    k = key(0,12); CHECK(setup(runtime,k,sample(12)) == 1);
    CHECK(setup(runtime,k,sample(13)) == 1);
    auto duplicated = admit(runtime,k);
    CHECK(same_snapshot(duplicated.current,sample(13)));
    CHECK(same_snapshot(duplicated.previous,sample(10)));
    CHECK(previous(k.view).jitter == sample(10).jitter);
    CHECK(setup(runtime,key(0,14),sample(14)) == 2);
    CHECK(same_snapshot(admit(runtime,key(0,14)).previous,sample(13)));
}
void resets_and_admission() {
    Runtime runtime; initialize(runtime);
    Key k = key(0,10); setup(runtime,k,sample(10)); admit(runtime,k);
    put(k.view + reset_offset,static_cast<unsigned char>(1)); k = key(1,11);
    CHECK(setup(runtime,k,sample(111)) == 0);
    CHECK(same_snapshot(admit(runtime,k).previous,sample(111)));
    k = key(0,12); CHECK(setup(runtime,k,sample(12)) == 0); admit(runtime,k);
    k = key(1,13); ++k.owner[2]; CHECK(setup(runtime,k,sample(113)) == 0);
    CHECK(!runtime.begin(k.view,k.display,k.pose_epoch,k.reference_epoch+1));
    CHECK(!runtime.resolve(k.view));
    auto r = admit(runtime,k); CHECK(r.reset && same_snapshot(r.previous,r.current) && r.previous_counter == r.key.native_counter); // v1206: a reset names itself
    put(k.view + jitter_offset,sample(999).jitter); CHECK(!runtime.resolve(k.view));
    put_current(k.view,r.current); CHECK(runtime.resolve(k.view));
    change_stamp_on_matrix_read = true; CHECK(!runtime.resolve(k.view));

    // Paired bootstrap must retain both receipts until their respective Begins.
    Runtime paired; initialize(paired);
    const auto left = key(0,20,false), right = key(1,20,false);
    setup(paired,left,sample(20)); setup(paired,right,sample(120));
    CHECK(same_snapshot(admit(paired,left).current,sample(20)));
    CHECK(same_snapshot(admit(paired,right).current,sample(120)));
}
void failed_seed_rolls_back() {
    for (bool partial : {false,true}) for (unsigned fail = 1; fail <= 5; ++fail) {
        Runtime runtime; initialize(runtime);
        setup(runtime,key(0,10),sample(10)); setup(runtime,key(1,11),sample(111));
        const auto before = memory;
        auto phase = runtime.plan(key(0,12)); CHECK(phase && *phase == 1);
        writes = 0; fail_write = fail; partial_failure = partial;
        classification_probe = &runtime;
        CHECK(!runtime.seed(view())); CHECK(runtime.failed); CHECK(memory == before);
        CHECK(runtime.single_view == view()); classification_probe = nullptr;
        CHECK(!runtime.planned()); CHECK(!runtime.resolve(view()));
    }
    // First-use reset flag write can fail too; bootstrap has no history writes.
    Runtime runtime; initialize(runtime); const auto before = memory;
    CHECK(runtime.plan(key(0,10))); writes=0; fail_write=1;
    CHECK(!runtime.seed(view())); CHECK(runtime.failed && memory == before);
}
void invalidation_and_cancellation() {
    Runtime runtime; initialize(runtime);
    auto k = key(0,10); setup(runtime,k,sample(10)); admit(runtime,k);
    const auto before = memory;
    std::thread recovery([&runtime] {runtime.invalidate();}); recovery.join();
    CHECK(memory == before && runtime.single_view == view() && !runtime.failed);
    CHECK(!runtime.resolve(view())); CHECK(!runtime.begin(view(),10,1,2));
    // Owner consumes the handoff and bootstraps rather than using revoked history.
    k = key(0,12); CHECK(setup(runtime,k,sample(12)) == 0);
    CHECK(same_snapshot(admit(runtime,k).previous,sample(12)));
    // Invalidation during worker snapshot reads cannot admit an old generation.
    invalidate_on_read = &runtime; CHECK(!runtime.resolve(view()));
    k = key(0,14); CHECK(setup(runtime,k,sample(14)) == 0); admit(runtime,k);
    CHECK(runtime.plan(key(0,16))); CHECK(runtime.seed(view()));
    const auto seeded = memory;
    runtime.invalidate(); CHECK(memory == seeded); // invalidate never touches the owner transaction/native view
    original_setup(key(0,16),sample(16));
    runtime.finish(view());
    CHECK(runtime.failed && runtime.single_view == view() && !runtime.planned());
    CHECK(!runtime.resolve(view()) && !runtime.begin(view(),16,1,2));

    Runtime before_seed; initialize(before_seed);
    CHECK(before_seed.plan(key(0,10))); const auto unseeded = memory;
    before_seed.invalidate(); CHECK(!before_seed.seed(view()));
    CHECK(before_seed.failed && before_seed.single_view == view() && memory == unseeded);

    Runtime canceled; initialize(canceled);
    CHECK(canceled.plan(key(0,10))); canceled.cancel_plan();
    CHECK(!canceled.planned() && !canceled.failed);
    setup(canceled,key(0,10),sample(10)); setup(canceled,key(1,11),sample(111));
    const auto saved = memory;
    CHECK(canceled.plan(key(0,12))); CHECK(canceled.seed(view()));
    classification_probe = &canceled; canceled.cancel_plan(); classification_probe = nullptr;
    CHECK(canceled.failed && !canceled.planned() && memory == saved && canceled.single_view == view());
    canceled.clear(); CHECK(canceled.single_view == view());
    canceled.retire(); CHECK(canceled.single_view == 0);

    Runtime invalid_finish; initialize(invalid_finish);
    CHECK(invalid_finish.plan(key(0,10))); CHECK(invalid_finish.seed(view()));
    original_setup(key(0,10),sample(10));
    auto invalid = sample(10); invalid.jitter[0] = std::numeric_limits<float>::quiet_NaN();
    put_current(view(),invalid); invalid_finish.finish(view());
    CHECK(invalid_finish.failed && invalid_finish.single_view == view() && !invalid_finish.planned());
    CHECK(!invalid_finish.resolve(view()));
}
} // namespace
int main() {
    alternating_continuity(); duplicate_setup(); resets_and_admission(); failed_seed_rolls_back(); invalidation_and_cancellation();
    std::puts("alternate native TAA CPU transaction checks passed");
}
