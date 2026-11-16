// v1372 lab: a copy that comes to a hand shows the bolt that hand chose for it - or, when the hand's native weapon is displaced into the copy,
// the native's bolt - even when the copy pool hands back an entity that owned another bolt (NativeWeaponHands::pool_acquire reuses a hidden
// copy of the same weapon, whichever hand had it). Replays the user's v1381 headset run (artifacts/headset-v1381-20261116T0059,
// "I chose the sleep bolt and got the regular one"): dual-wield-events 147437359 the left hand held Blink and chose the crossbow (wheel
// sector 3) -> copy_restore, the left copy was the pooled crossbow copy that hand had used at 147105515-147144796; real-parts.csv shows that
// copy with its old bolt entity (23b4ec71c40) at 147437718 and a new bolt (23c3b397b40) only after the same choice again at 147439750.
// The flow runs through NativeRangedFire's own calls (note_copies, set_copy_comp, note_wheel_open, wheel_pick, track_ammo) on a fake game
// image (the fixture of native_ranged_fire_check); the game's equips are written into the image by hand. Builds on v1381 as on later trees;
// the pure rule (ranged_fire::fresh_copy_take) is tabled in bolt_owner_check.
#include "../src/native_ranged_fire.hpp"
#include <cassert>
#include <cstdio>
#include <unordered_map>

using namespace d2vr;
using Fire=NativeRangedFire;

constexpr std::uintptr_t BASE=0x10000000,GAME=0x20000000,PLAYER=0x21000000,PCL=0x21100000,INV=0x22000000,RECS=0x23000000,
    PCOMP=0x24000000,PDECL=0x25000000,PENT=0x26000000,PAMMO_DECL=0x27000000,PAMMO=0x27100000,PDEF=0x27200000,ATTR=0x28000000,CAM=0x28200000,
    XCOMP=0x2a000000,XDECL=0x2b000000,XENT=0x2c000000,XAMMO=0x2d100000,
    REGULAR=0x2d000000,SLEEP=0x2d300000,FIRE=0x2d400000,XDEF=0x2d200000,
    BLINK_REC=0x2f000000,CA=0x2e000000,CB=0x2e010000;   // CA, CB: two pooled crossbow copies
constexpr std::uint32_t PLAYER_ID=7,PID=(5u<<16)|1,XID=(6u<<16)|1;
struct Mem {
    std::unordered_map<std::uintptr_t,unsigned char> b;
    template<class T> void put(std::uintptr_t a,T v) {unsigned char t[sizeof(T)];std::memcpy(t,&v,sizeof(T));for(std::size_t i=0;i<sizeof(T);++i) b[a+i]=t[i];}
    template<class T> T get(std::uintptr_t a) const {T v{};read(a,&v,sizeof(T));return v;}
    bool read(std::uintptr_t a,void* out,std::size_t n) const {
        auto* o=static_cast<unsigned char*>(out);
        for(std::size_t i=0;i<n;++i) {const auto it=b.find(a+i);if(it==b.end()) return false;o[i]=it->second;}
        return true;
    }
    bool operator()(std::uintptr_t a,void* out,std::size_t n) const {return read(a,out,n);}
    void list(std::uintptr_t at,std::uintptr_t data,std::uint32_t count) {put<std::uint64_t>(at,data);put<std::uint32_t>(at+8,count);put<std::uint32_t>(at+12,(count?count:1)<<8);}
    void zero(std::uintptr_t a,std::size_t n) {for(std::size_t i=0;i<n;++i) b[a+i]=0;}
};
static Mem M;
static std::uint64_t g_now=1000;
constexpr std::uintptr_t entry(unsigned i) {return XAMMO+i*0x70ull;}   // the crossbow's ammo table: 0 regular bolt, 1 sleep dart, 2 incendiary
static void build() {
    M=Mem{};
    M.put<std::uint64_t>(BASE+0x39a5290,GAME);M.put<std::uint64_t>(GAME,BASE+0x1c940a8);M.put<std::uint64_t>(GAME+0x3130b0,PLAYER);
    M.put<std::uint64_t>(GAME+0x3130b0+5*8,PENT);M.put<std::uint64_t>(GAME+0x3130b0+6*8,XENT);
    M.put<std::uint64_t>(PLAYER,BASE+0x1c5e428);M.put<std::uint32_t>(PLAYER+0xa8,PLAYER_ID);
    M.list(PLAYER+0x268,PCL,1);M.put<std::uint64_t>(PCL,INV);
    M.put<std::uint64_t>(INV,BASE+0x1b9a878);M.put<std::uint64_t>(INV+0x10,PLAYER);M.list(INV+0x38,RECS,6);
    struct R {std::uintptr_t decl,comp,eq;std::uint32_t id;std::int32_t count;};
    const R recs[6]{{0xa0000,PCOMP,PDECL,PID,1},{PAMMO_DECL,0,0,0,7},{0xb0000,XCOMP,XDECL,XID,1},{REGULAR,0,0,0,5},{SLEEP,0,0,0,4},{FIRE,0,0,0,3}};
    for(unsigned i=0;i<6;++i) {
        const auto at=RECS+i*0x38ull;M.zero(at,0x38);
        M.put<std::uint64_t>(at,recs[i].decl);M.put<std::uint32_t>(at+8,recs[i].id);M.put<std::uint64_t>(at+0x10,recs[i].comp);
        M.put<std::uint64_t>(at+0x18,recs[i].eq);M.put<std::int32_t>(at+0x20,recs[i].count);
    }
    struct W {std::uintptr_t comp,decl,ent,ammo_decl,ammo,def,vt,dvt;std::uint32_t id;};
    const W w[2]{{PCOMP,PDECL,PENT,PAMMO_DECL,PAMMO,PDEF,0x1b53f58,0x1b53e18,PID},{XCOMP,XDECL,XENT,REGULAR,XAMMO,XDEF,0x1b9cbf8,0x1b9cab8,XID}};
    for(const auto& k:w) {
        M.zero(k.comp,0x200);M.put<std::uint64_t>(k.comp,BASE+k.vt);M.put<std::uint64_t>(k.comp+8,k.decl);M.put<std::uint64_t>(k.comp+0x10,k.ent);
        M.put<std::int32_t>(k.comp+0x18,1);M.put<std::uint32_t>(k.comp+0x1c,2);
        M.put<std::uint64_t>(k.comp+0x30,ATTR);M.put<std::uint64_t>(k.comp+0x40,CAM);M.put<std::uint64_t>(k.comp+0x48,CAM);M.put<std::uint64_t>(k.comp+0x68,INV);M.put<std::uint64_t>(k.comp+0x98,k.ammo);
        M.put<std::int32_t>(k.comp+0xa0,1);M.put<std::int32_t>(k.comp+0xa4,1);M.list(k.comp+0xa8,0,0);
        M.put<std::uint64_t>(k.decl,BASE+k.dvt);
        M.zero(k.ent,0x340);M.put<std::uint64_t>(k.ent,BASE+0x1ce1e98);M.put<std::uint32_t>(k.ent+0xa8,k.id);M.put<std::uint32_t>(k.ent+0xac,PLAYER_ID);
        M.put<std::uint64_t>(k.ammo,k.ammo_decl);M.put<std::uint64_t>(k.ammo+0x10,k.def);M.put<std::uint64_t>(k.ammo_decl,0);M.put<std::uint64_t>(k.def,0);
    }
    M.put<std::uint64_t>(XDECL+0x188,XAMMO);M.put<std::uint32_t>(XDECL+0x190,3);
    const std::uintptr_t decls[3]{REGULAR,SLEEP,FIRE};
    for(unsigned i=0;i<3;++i) {M.put<std::uint64_t>(entry(i),decls[i]);M.put<std::uint64_t>(entry(i)+0x10,XDEF);M.put<std::uint64_t>(decls[i],0);}
    M.put<std::uint64_t>(ATTR+0x48,0x28100000);M.zero(0x28100000,0x200);M.put<std::uint64_t>(CAM,0);
    for(const auto c:{CA,CB}) {M.zero(c,0x200);M.put<std::uint64_t>(c,BASE+0x1b9cbf8);M.put<std::uint64_t>(c+8,XDECL);M.put<std::uint64_t>(c+0x98,entry(0));}
    M.put<std::uint64_t>(INV+0x70,BLINK_REC);   // slot 1 holds a power
}
namespace d2vr {
struct NativeRangedFireTest {
    static void track() {g_now+=150;Fire::track_ammo(BASE,M);}
    static void clear() {Fire::hand_choice_={};Fire::copy_ammo_={};Fire::copy_comp_={};Fire::copy_last_={};Fire::wheel_hand_=-1;}
};
}
using Test=NativeRangedFireTest;
static int failures=0;
static const char* bolt(std::uintptr_t e) {return e==entry(0)?"regular":e==entry(1)?"sleep":e==entry(2)?"incendiary":e?"other":"none";}
static void expect(bool ok,const char* what,std::uintptr_t got) {std::printf("%s %s (got %s)\n",ok?"ok  ":"FAIL",what,bolt(got));if(!ok) ++failures;}
static std::uintptr_t shows(std::uintptr_t comp) {return M.get<std::uint64_t>(comp+0x98);}   // what the instance itself has selected
static void start() {
    build();Fire::reset();Test::clear();Fire::forget_owners();g_now+=10000;Fire::clock=+[]()->std::uint64_t {return g_now;};Test::track();
}
// A copy shown in a hand, the pass that follows, and the game's own ammo switch (select_ammo 0x63a490 is not callable here: the want lands
// in copy_ammo, the image is updated by hand as the game would).
static std::uintptr_t show(unsigned side,std::uintptr_t comp,int left_kind,int right_kind,int slot1_hand) {
    Fire::note_copies(left_kind,right_kind,slot1_hand);Fire::set_copy_comp(side,comp);Test::track();
    const auto want=Fire::copy_ammo(side,1);if(want) M.put<std::uint64_t>(comp+0x98,want);return want;
}
static void pick(unsigned side,std::uintptr_t comp,std::uintptr_t ammo) {   // a wheel choice by the hand that holds this copy (copy_own), and the switch
    Fire::note_wheel_open(side);(void)Fire::wheel_pick(BASE,M,0xb0000,ammo);Test::track();
    if(const auto want=Fire::copy_ammo(side,1)) M.put<std::uint64_t>(comp+0x98,want);
}

int main() {
    {   // A: the v1381 run - the left hand held Blink and chose crossbow + sleep darts; its pooled copy came back owning the regular bolt
        start();
        show(0,CA,1,-1,-1);                                                         // 147105515: the left's crossbow copy (regular, the native's)
        expect(shows(CA)==entry(0),"A0 the left's first copy starts with the native's bolt",shows(CA));
        Fire::note_copies(-1,-1,0);Fire::set_copy_comp(0,0);Test::track();          // Blink in the left (slot 1): the copy hidden in the pool
        Fire::note_wheel_open(0);const auto r=Fire::wheel_pick(BASE,M,0xb0000,SLEEP);   // 147437359: crossbow + sleep darts from the left's wheel
        M.put<std::uint64_t>(XCOMP+0x98,r?entry(0):entry(1));M.put<std::uint64_t>(INV+0x70,RECS+2*0x38);Test::track();   // the game equips the crossbow (slot 1)
        M.put<std::uint64_t>(INV+0x70,BLINK_REC);                                   // copy_restore: the power is equipped again, Blink moves to the right
        show(0,CA,1,-1,1);                                                          // the left's copy: the pool's CA again
        expect(shows(CA)==entry(1),"A1 the left's copy shows the sleep dart it was chosen with",shows(CA));
    }
    {   // B: the same item from the other hand while the left draws the native crossbow - the right's copy is a pooled one that owned incendiary
        start();M.put<std::uint64_t>(INV+0x70,RECS+2*0x38);                          // the native crossbow in slot 1, drawn by the left
        show(1,CB,-1,1,0);pick(1,CB,FIRE);                                         // the right's copy with incendiary (copy_own)
        expect(shows(CB)==entry(2),"B0 the right's copy took incendiary",shows(CB));
        Fire::note_copies(-1,-1,0);Fire::set_copy_comp(1,0);Test::track();          // the right puts it away (pool)
        Fire::note_wheel_open(1);const auto keep=Fire::wheel_pick(BASE,M,0xb0000,SLEEP);   // the right chooses the crossbow + sleep: the same item
        expect(keep==REGULAR,"B1 the game is handed the native's own bolt (no change on the left)",keep==REGULAR?entry(0):0);
        show(1,CB,-1,1,0);                                                          // the right's copy: the pool's CB again
        expect(shows(CB)==entry(1),"B2 the right's copy shows the sleep dart it was chosen with",shows(CB));
        expect(shows(XCOMP)==entry(0) && Fire::native_ammo(1)==entry(0),"B3 the left's native keeps its regular bolt",shows(XCOMP));
    }
    {   // C: the left draws the native crossbow with sleep darts; the right takes the pistol, the left's crossbow becomes its copy - the pool's CA
        start();
        show(0,CA,1,-1,-1);pick(0,CA,FIRE);                                         // CA owned incendiary once
        Fire::note_copies(-1,-1,-1);Fire::set_copy_comp(0,0);Test::track();         // back in the pool
        Fire::note_wheel_open(0);(void)Fire::wheel_pick(BASE,M,0xb0000,SLEEP);      // the left chooses the crossbow + sleep darts: the native comes
        M.put<std::uint64_t>(INV+0x70,RECS+2*0x38);M.put<std::uint64_t>(XCOMP+0x98,entry(1));Fire::note_copies(-1,-1,0);Test::track();
        g_now+=6000;Test::track();Test::track();                                    // drawn by the left for a while (the choice is old now)
        M.put<std::uint64_t>(INV+0x70,RECS);                                        // the right's pistol takes slot 1; the left's crossbow becomes its copy
        show(0,CA,1,-1,1);
        expect(shows(CA)==entry(1),"C1 the crossbow keeps its sleep dart when it becomes the left's copy",shows(CA));
    }
    {   // D: what stays as it was
        start();
        show(0,CA,1,-1,-1);pick(0,CA,FIRE);
        expect(shows(CA)==entry(2),"D1 a held copy takes its own hand's choice at once",shows(CA));
        Fire::set_copy_comp(0,0);Test::track();show(0,CA,1,-1,-1);                  // hidden for a moment (tracking lost) and shown again
        expect(shows(CA)==entry(2),"D2 a copy shown again keeps its bolt",shows(CA));
        Fire::note_copies(-1,-1,-1);Fire::set_copy_comp(0,0);Test::track();g_now+=20000;
        show(1,CA,-1,1,-1);                                                         // the same copy caught by the right hand long after (a toss)
        expect(shows(CA)==entry(2),"D3 a caught copy keeps its bolt",shows(CA));
        Fire::note_copies(-1,-1,0);Fire::set_copy_comp(1,0);Test::track();
        Fire::note_wheel_open(1);(void)Fire::wheel_pick(BASE,M,0xb0000,SLEEP);g_now+=4500;   // a choice whose copy never came
        show(1,CA,-1,1,-1);
        expect(shows(CA)==entry(2),"D4 a stale choice (no copy within 4 s) does not reach a later copy",shows(CA));
    }
    if(failures) {std::printf("bolt_reacquire_check: %d failure(s)\n",failures);return 1;}
    std::puts("bolt_reacquire_check: ok");
    return 0;
}
