// Offline check for d2vr::NativeTakedowns against a fake sparse process memory.
#include "../src/native_takedown.hpp"
#include "../src/native_action_camera.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::printf("FAIL: %s\n", message);
        std::exit(1);
    }
}

using uptr = std::uintptr_t;

struct Mem {
    std::map<uptr, std::uint8_t> bytes;
    void put(uptr at, const void* src, std::size_t n) {
        const auto* p = static_cast<const std::uint8_t*>(src);
        for (std::size_t i = 0; i < n; ++i) bytes[at + i] = p[i];
    }
    template <class T> void put(uptr at, T v) { put(at, &v, sizeof(v)); }
    bool read(uptr at, void* out, std::size_t n) const {
        auto* p = static_cast<std::uint8_t*>(out);
        for (std::size_t i = 0; i < n; ++i) {
            auto it = bytes.find(at + i);
            if (it == bytes.end()) return false;
            p[i] = it->second;
        }
        return true;
    }
    // The rig/takedown helpers invoke the Read object as read(at,out,n).
    bool operator()(uptr at, void* out, std::size_t n) const { return read(at, out, n); }
};

constexpr uptr base = 0x140000000;
constexpr std::uint32_t none = 0x3fff0010u;

constexpr uptr slot_game = base + 0x39a5290;
constexpr uptr off_game_vt = 0x1c940a8;
constexpr uptr off_player_off = 0x3130b0;
constexpr uptr off_player_vt = 0x1c5e428;
constexpr uptr off_machine_vt = 0x1c73d58;
constexpr uptr vt_choke = 0x1b3e158;
constexpr uptr vt_assassination = 0x1b3cc38;
constexpr uptr vt_drop = 0x1b3f0a8;

void machine(Mem& m, uptr at) { m.put<uptr>(at, base + off_machine_vt); }

void sensor(Mem& m, uptr c, uptr vtable_offset, uptr owner, uptr machine_ptr) {
    m.put<uptr>(c, base + vtable_offset);
    m.put<uptr>(c + 0x10, owner);
    m.put<uptr>(c + 0x20, machine_ptr);
    m.put<std::uint8_t>(c + 0x38, 1);
    m.put<std::uint32_t>(c + 0x40, none);
    m.put<std::uint8_t>(c + 0x48, 0);
    m.put<std::uint32_t>(c + 0x50, none);
    m.put<uptr>(c + 0x70, 0);
    m.put<std::uint32_t>(c + 0x78, 0);
}

void build_game(Mem& m, uptr game, uptr player, uptr comp_array,
                const std::vector<uptr>& comps) {
    m.put<uptr>(slot_game, game);
    m.put<uptr>(game, base + off_game_vt);
    m.put<uptr>(game + off_player_off, player);
    m.put<uptr>(player, base + off_player_vt);
    m.put<uptr>(player + 0x268, comp_array);
    m.put<std::uint32_t>(player + 0x268 + 8, static_cast<std::uint32_t>(comps.size()));
    m.put<std::uint32_t>(player + 0x268 + 12, static_cast<std::uint32_t>(comps.size()) << 8);
    for (std::size_t i = 0; i < comps.size(); ++i) m.put<uptr>(comp_array + i * 8, comps[i]);
}

// ---------------------------------------------------------------- scenario A
void scenario_basic() {
    const uptr game = 0x140100000, player = 0x140200000, array = 0x140300000;
    const uptr unrelated = 0x140400300, choke = 0x140400000, ass = 0x140400100,
               drop = 0x140400200, mch = 0x140500000, ids = 0x140600000;
    Mem m;
    build_game(m, game, player, array, {unrelated, choke, ass, drop});
    m.put<uptr>(unrelated, base + 0x999);
    machine(m, mch);
    sensor(m, choke, vt_choke, player, mch);
    sensor(m, ass, vt_assassination, player, mch);
    sensor(m, drop, vt_drop, player, mch);

    d2vr::NativeTakedowns t;
    auto st = t.poll(m, base, 1000);
    require(!st.choke && !st.kill && !st.drop, "A: all none -> no takedowns");
    require(t.sensors() == 7, "A: all none -> sensors()==7");

    m.put<std::uint32_t>(choke + 0x50, 0x1234u);
    st = t.poll(m, base, 1001);
    require(st.choke && !st.kill && !st.drop, "A: valid choke id -> choke only");
    require(st.spawn == 0x1234u, "A: choke spawn id exposed");
    m.put<std::uint32_t>(choke + 0x50, none);

    m.put<std::uint32_t>(ass + 0x40, 0x2000u);
    st = t.poll(m, base, 1002);
    require(!st.choke && st.kill && !st.drop, "A: valid kill id, +0x48==0 -> kill only");
    require(st.spawn == 0x2000u, "A: kill spawn id exposed");

    m.put<std::uint8_t>(ass + 0x48, 1);
    st = t.poll(m, base, 1003);
    require(!st.kill, "A: +0x48==1 -> no kill");
    m.put<std::uint8_t>(ass + 0x48, 0);
    m.put<std::uint32_t>(ass + 0x40, none);

    m.put<std::uint32_t>(ids, 0x3000u);
    m.put<uptr>(drop + 0x70, ids);
    m.put<std::uint32_t>(drop + 0x78, 1);
    st = t.poll(m, base, 1004);
    require(!st.choke && !st.kill && st.drop, "A: drop count 1 valid id -> drop only");

    m.put<std::uint32_t>(drop + 0x78, 0);
    st = t.poll(m, base, 1005);
    require(!st.drop, "A: drop count 0 -> no drop");
    m.put<std::uint32_t>(drop + 0x78, 1);

    m.put<std::uint8_t>(choke + 0x38, 0);
    m.put<std::uint32_t>(choke + 0x50, 0x1234u);
    st = t.poll(m, base, 1006);
    require(!st.choke, "A: enabled 0 -> choke reports nothing");

    m.put<std::uint8_t>(drop + 0x38, 0);
    st = t.poll(m, base, 1007);
    require(!st.drop, "A: enabled 0 -> drop reports nothing");
}

// ---------------------------------------------------------------- scenario B
void scenario_wrong_owner() {
    const uptr game = 0x140100000, player = 0x140200000, other = 0x140208000;
    const uptr array = 0x140300000, choke = 0x140400000, mch = 0x140500000;
    Mem m;
    m.put<uptr>(other, base + off_player_vt);
    build_game(m, game, player, array, {choke});
    machine(m, mch);
    sensor(m, choke, vt_choke, other, mch);
    m.put<std::uint32_t>(choke + 0x50, 0x1234u);

    d2vr::NativeTakedowns t;
    const auto st = t.poll(m, base, 1000);
    require(t.sensors() == 0, "B: foreign-owner sensor is ignored");
    require(!st.any(), "B: foreign-owner sensor reports nothing");
}

// ---------------------------------------------------------------- scenario C
void scenario_player_change() {
    const uptr game = 0x140100000;
    const uptr player_a = 0x140200000, array_a = 0x140300000;
    const uptr choke_a = 0x140400000, ass_a = 0x140400100, drop_a = 0x140400200;
    const uptr mch_a = 0x140500000;
    const uptr player_b = 0x140210000, array_b = 0x140310000;
    const uptr choke_b = 0x140410000, ass_b = 0x140410100, drop_b = 0x140410200;
    const uptr mch_b = 0x140510000;
    Mem m;
    build_game(m, game, player_a, array_a, {choke_a, ass_a, drop_a});
    machine(m, mch_a);
    sensor(m, choke_a, vt_choke, player_a, mch_a);
    sensor(m, ass_a, vt_assassination, player_a, mch_a);
    sensor(m, drop_a, vt_drop, player_a, mch_a);
    m.put<std::uint32_t>(choke_a + 0x50, 0x1111u);

    m.put<uptr>(player_b, base + off_player_vt);
    m.put<uptr>(player_b + 0x268, array_b);
    m.put<std::uint32_t>(player_b + 0x268 + 8, 3);
    m.put<std::uint32_t>(player_b + 0x268 + 12, 3u << 8);
    m.put<uptr>(array_b + 0, choke_b);
    m.put<uptr>(array_b + 8, ass_b);
    m.put<uptr>(array_b + 16, drop_b);
    machine(m, mch_b);
    sensor(m, choke_b, vt_choke, player_b, mch_b);
    sensor(m, ass_b, vt_assassination, player_b, mch_b);
    sensor(m, drop_b, vt_drop, player_b, mch_b);
    m.put<std::uint32_t>(choke_b + 0x50, none);

    d2vr::NativeTakedowns t;
    auto st = t.poll(m, base, 1000);
    require(t.sensors() == 7 && st.choke, "C: first scan finds player A sensors");

    // The game object now points at player B; the old sensor's vtable changes
    // (destroyed), so poll() must notice and rescan.
    m.put<uptr>(game + off_player_off, player_b);
    m.put<uptr>(choke_a, base + 0x999);
    st = t.poll(m, base, 1001);
    require(t.sensors() == 7, "C: rescan finds player B sensors after player change");
    require(!st.choke, "C: stale player A sensor no longer used");
}

// ---------------------------------------------------------------- scenario D
void scenario_missing_and_rate_limit() {
    const uptr game = 0x140100000, player = 0x140200000, array = 0x140300000;
    const uptr choke = 0x140400000, ass = 0x140400100, drop = 0x140400200;
    const uptr mch = 0x140500000;
    Mem m;
    d2vr::NativeTakedowns t;
    auto st = t.poll(m, base, 1000);
    require(!st.any() && t.sensors() == 0, "D: missing game object -> nothing, no crash");

    machine(m, mch);
    sensor(m, choke, vt_choke, player, mch);
    sensor(m, ass, vt_assassination, player, mch);
    sensor(m, drop, vt_drop, player, mch);
    build_game(m, game, player, array, {choke, ass, drop});

    st = t.poll(m, base, 2000);
    require(t.sensors() == 0, "D: failed rescan is rate-limited (+2000)");

    st = t.poll(m, base, 3000);
    require(t.sensors() == 7, "D: rescan allowed once now reaches next_scan_");
}

} // namespace

int main() {
    {
        Mem m;d2vr::NativeActionCameraReader reader;
        constexpr uptr player=0x140200000,mch=0x140500000,pool=0x140600000,states=0x140700000;
        build_game(m,0x140100000,player,0x140300000,{mch});
        machine(m,mch);m.put(mch+0x10,player);m.put(mch+0x90,pool);
        auto put_state=[&](unsigned index,uptr vt,bool active) {
            const auto at=states+index*0x100;
            m.put(pool+0x10+index*8,at);m.put(at,base+vt);m.put<std::uint32_t>(at+8,index);
            m.put(at+0x10,mch);m.put<std::uint8_t>(at+0x20,active?1:0);
            return at;
        };
        put_state(44,0x1c81a68,true);
        auto state=reader.poll(m,base,player,1000);
        require(state.combat && !state.scripted && !state.owns_camera(true),"Block must keep controller camera ownership");
        const auto assassination=put_state(13,0x1c83468,true);
        state=reader.poll(m,base,player,1001);
        require(state.scripted && state.owns_camera(false),"Active native finisher must own first-person camera");
        m.put<std::uint8_t>(assassination+0x20,0);put_state(44,0x1c81a68,false);
        require(!reader.poll(m,base,player,1002).owns_camera(false),"Finished action did not return controller camera");
        m.put<std::uint8_t>(assassination+0x20,1);m.put(assassination+0x10,mch+0x100);
        require(!reader.poll(m,base,player,1003).scripted,"Foreign action owner was accepted");
        put_state(13,0x1c83468,true);m.put<std::uint32_t>(assassination+8,55);
        require(!reader.poll(m,base,player,1004).scripted,"Wrong action pool index was accepted");
        m.put(mch+0x10,player+0x100);
        require(!reader.poll(m,base,player,1005).scripted,"Recycled state machine owner was accepted");
    }
    scenario_basic();
    scenario_wrong_owner();
    scenario_player_change();
    scenario_missing_and_rate_limit();
    std::printf("PASS\n");
    return 0;
}
