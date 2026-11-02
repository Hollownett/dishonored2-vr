#define NOMINMAX
#define D2VR_TESTING
#include "../src/npc_weapon_pickup.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
using namespace d2vr;
using namespace d2vr::npc_weapon;

// ---- pure book ------------------------------------------------------------------------------------------------------------
static WorldTrackedPose at(double x,double y,double z) {return {{x,y,z},{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}}};}
static Found mk(std::uint32_t id,Kind k,double x,double y,double z,const char* def="models/weapons/sword_gs_guard/gs_guard_sword.def") {
    Found f;f.entity=0x1000+id*0x100ull;f.id=id;f.kind=k;f.def=def;f.pose=at(x,y,z);return f;
}
static void book_checks() {
    assert(classify("models/weapons/sword_gs_guard/gs_guard_sword.def",true,false)==Kind::sword);
    assert(classify("Models/Weapons/gun_elite/elite_gun.def",false,true)==Kind::pistol);
    assert(classify("models/weapons/gun_elite/elite_gun.def",true,true)==Kind::pistol);
    assert(classify("models/weapons/item_throw/thrown_item_equipment.def",false,false)==Kind::other);
    assert(!classify("models/weapons/crossbow/wristbow_howler.def",false,false));
    assert(!classify("models/campaign/dunwall/midnightassets/sword_player_midnight.def",true,false)); // not a weapon def path
    assert(!classify("models/weapons/sword_player/ply_sword.def",false,false));
    {   // upkeep: add, refresh, age out, release grace
        Book b;
        assert(b.merge_scan({mk(1,Kind::sword,0,0,0),mk(2,Kind::pistol,5,0,0)},{},1000)==2 && b.size()==2);
        assert(b.merge_scan({mk(1,Kind::sword,.1,0,0)},{},1300)==0 && b.size()==2);          // 2 missed once
        assert(std::abs(b.find(1)->pose.position.x-.1)<1e-12 && b.find(2)->miss==1);
        b.merge_scan({mk(1,Kind::sword,.1,0,0)},{},1600);b.merge_scan({mk(1,Kind::sword,.1,0,0)},{},1900);
        assert(b.size()==1 && !b.find(2));                                                    // forgotten after 3 misses
        assert(b.merge_scan({},{},2200)==0 && b.size()==1 && b.find(1)->miss==1);
        assert(b.merge_scan({mk(1,Kind::sword,0,0,0)},{},2500)==0 && b.find(1)->miss==0);     // seen again resets
    }
    {   // capacity: a closer newcomer evicts the farthest lying entry, a farther one is ignored
        Book b;std::vector<Found> many;
        for(unsigned i=1;i<=Book::capacity;++i) many.push_back(mk(i,Kind::sword,i,0,0));
        assert(b.merge_scan(many,{0,0,0},0)==Book::capacity && b.size()==Book::capacity);
        assert(b.merge_scan(many,{0,0,0},10)==0);
        auto more=many;more.push_back(mk(100,Kind::sword,.5,0,0));
        assert(b.merge_scan(more,{0,0,0},20)==1 && b.size()==Book::capacity && b.find(100) && !b.find(Book::capacity));
        more.push_back(mk(101,Kind::sword,500,0,0));
        assert(b.merge_scan(more,{0,0,0},30)==0 && !b.find(101));
    }
    {   // nearest: reach, kind filter, box distance, a full hand gets nothing
        Book b;b.merge_scan({mk(1,Kind::sword,1,0,1),mk(2,Kind::pistol,.3,0,1),mk(3,Kind::sword,3,0,1)},{},0);
        auto n=b.nearest(1,{.2,0,1},.5);assert(n && n->id==2 && std::abs(n->distance-.1)<1e-9);
        assert(!b.nearest(1,{.2,0,1},.05));
        n=b.nearest(0,{.2,0,1},2,Kind::sword);assert(n && n->id==1);
        assert(!b.nearest(0,{.2,0,1},2,Kind::other));
        assert(!b.nearest(1,{NAN,0,1},2) && !b.nearest(1,{0,0,1},0));
        Book c;auto f=mk(7,Kind::sword,0,0,1);f.bounds=Box{{-.5,-.05,.9},{.5,.05,1.1}};c.merge_scan({f},{},0);  // a long blade: the hand at its tip
        n=c.nearest(0,{.45,.04,1.05},.1);assert(n && n->id==7 && n->distance<.06);
        assert(!c.nearest(0,{1.0,0,1.0},.1));
    }
    {   // take / release state machine
        Book b;b.merge_scan({mk(1,Kind::sword,0,0,0),mk(2,Kind::pistol,.2,0,0)},{},0);
        assert(!b.take(99,0));
        assert(b.take(1,0) && !b.take(1,1) && !b.take(2,0) && b.take(2,1));                  // lying only, one weapon per hand
        assert(!b.nearest(0,{0,0,0},2) && !b.nearest(1,{0,0,0},2));
        assert(b.held(0)->id==1 && b.held(1)->id==2 && b.held_count()==2 && b.lying()==0);
        auto ops=b.drain_ops();assert(ops.size()==2 && !ops[0].show && ops[0].id==1 && !ops[1].show);
        b.on_hidden(1);assert(b.find(1)->hidden_by_us);
        // held weapons survive scans that do not see them
        for(int i=0;i<6;++i) b.merge_scan({},{},100+i);
        assert(b.size()==2);
        assert(!b.release(0,{NAN,0,0},{},{},500));                                              // refused, still held
        assert(b.held(0));
        const auto r=b.release(0,{1,2,3},{4,0,0},std::nullopt,1000);assert(r && r->id==1 && r->def.find("gs_guard_sword")!=std::string::npos);
        assert(!b.held(0) && b.find(1)->holder<0 && b.find(1)->pose.position.y==2);
        ops=b.drain_ops();assert(ops.size()==1 && ops[0].show && ops[0].place && ops[0].vel.x==4 && !ops[0].have_axis);
        assert(!b.release(0,{0,0,0},{},{},1001));                                               // empty hand
        // grace: not forgotten while its show has not run, forgotten after the grace
        for(int i=0;i<4;++i) b.merge_scan({},{},1100+i*100);
        assert(b.find(1) && b.find(1)->miss==0);
        for(int i=0;i<4;++i) b.merge_scan({},{},3000+i*100);
        assert(!b.find(1));
        // a vanished held weapon frees its hand and tells the lead once
        assert(b.take_lost(1)==false);
        std::vector<Book::Refresh> gone{Book::Refresh{2,false}};b.apply_refresh(gone);
        assert(!b.held(1) && b.size()==0 && b.take_lost(1) && !b.take_lost(1));
    }
    {   // refresh keeps lying poses; reset/teardown tell the hands
        Book b;b.merge_scan({mk(1,Kind::sword,0,0,0),mk(2,Kind::sword,1,0,0)},{},0);
        Book::Refresh r{1,true,at(9,9,9),std::nullopt,true};b.apply_refresh({r});assert(b.find(1)->pose.position.x==9);
        assert(b.take(2,1));b.drain_ops();b.on_hidden(2);
        auto shown=b.teardown();assert(shown.size()==1 && shown[0].second==2 && b.size()==0 && b.take_lost(1));
        b.merge_scan({mk(3,Kind::sword,0,0,0)},{},0);assert(b.take(3,0));b.reset();assert(b.take_lost(0) && b.size()==0);
    }
    {   // a box with sane limits only
        const std::array<float,8> ok{-.5f,-.05f,.9f,0,.5f,.05f,1.1f,0};
        assert(sane_box(ok,{0,0,1}));
        assert(!sane_box(ok,{0,0,5}));                                                          // origin far from the box
        const std::array<float,8> flipped{.5f,0,0,0,-.5f,1,1,0};assert(!sane_box(flipped,{0,0,0}));
        const std::array<float,8> huge{0,0,0,0,10,10,10,0};assert(!sane_box(huge,{1,1,1}));
        const std::array<float,8> nan{NAN,0,0,0,1,1,1,0};assert(!sane_box(nan,{0,0,0}));
    }
}

// ---- a fake process: the registry, NPCs with inventories and weapon entities, read through the same ABI offsets ----------
struct Segment {std::uintptr_t start{};std::vector<unsigned char> bytes;};
static std::vector<Segment> world;
static bool fake_read(std::uintptr_t a,void* dst,std::size_t n) {
    for(const auto& s:world) if(a>=s.start && a+n<=s.start+s.bytes.size()) {std::memcpy(dst,s.bytes.data()+(a-s.start),n);return true;}
    return false;
}
template<class T> static void put(std::uintptr_t a,const T& v) {
    for(auto& s:world) if(a>=s.start && a+sizeof(T)<=s.start+s.bytes.size()) {std::memcpy(s.bytes.data()+(a-s.start),&v,sizeof(T));return;}
    assert(false && "put outside any segment");
}
static const std::uintptr_t base=0x07000000,gm=0x20000000,heap=0x30000000,strs=0x31000000;
static std::uintptr_t heap_next=heap,str_next=strs;
static std::uintptr_t alloc(std::size_t n) {const auto r=heap_next;heap_next+=(n+15)&~15ull;return r;}
static std::uintptr_t text(const char* s) {const auto r=str_next;put(r,std::array<char,128>{});std::memcpy(world[3].bytes.data()+(r-strs),s,std::strlen(s)+1);str_next+=128;return r;}
static std::uintptr_t list(const std::vector<std::uintptr_t>& v) { // idList {data, packed_count, packed_capacity = cap<<8}
    const auto data=alloc(std::max<std::size_t>(v.size(),1)*8);
    for(std::size_t i=0;i<v.size();++i) put(data+i*8,v[i]);
    const auto l=alloc(16);put(l,data);put<std::uint32_t>(l+8,static_cast<std::uint32_t>(v.size()));put<std::uint32_t>(l+12,static_cast<std::uint32_t>(std::max<std::size_t>(v.size(),1))<<8);
    return l;
}
static void put_list(std::uintptr_t at_,const std::vector<std::uintptr_t>& v) {
    const auto l=list(v);std::array<unsigned char,16> raw{};fake_read(l,raw.data(),16);
    for(unsigned i=0;i<16;++i) put<unsigned char>(at_+i,raw[i]);
}
static void reset_world() {
    world.clear();heap_next=heap;str_next=strs;
    world.push_back({base+0x39a5290,std::vector<unsigned char>(8)});
    world.push_back({gm,std::vector<unsigned char>(0x340000)});
    world.push_back({heap,std::vector<unsigned char>(0x400000)});
    world.push_back({strs,std::vector<unsigned char>(0x10000)});
    put(base+0x39a5290,gm);
}
static std::uint32_t id_of(unsigned slot,unsigned gen=1) {return (slot<<16)|gen;}
static std::uintptr_t make_entity(unsigned slot,std::uintptr_t vtable_rva,unsigned gen=1) {
    const auto e=alloc(0x800);
    put(e,base+vtable_rva);put(e+0xa8,id_of(slot,gen));
    put(gm+0x3130b0+slot*8ull,e);
    return e;
}
static std::uintptr_t make_component(std::uintptr_t owner,std::uintptr_t vtable_rva) {const auto c=alloc(0x200);put(c,base+vtable_rva);put(c+0x10,owner);return c;}
struct Weapon {std::uintptr_t entity{},model{};std::uint32_t id{};};
// A guard weapon: idEntity (0x1c87d58) with def name, context, a model (box around the origin), weapon component.
static Weapon make_weapon(unsigned slot,const char* def,int ctx,std::uintptr_t comp_rva,float x,float y,float z,bool with_model=true,bool hidden=false) {
    Weapon w;w.entity=make_entity(slot,0x1c87d58);w.id=id_of(slot);
    const auto decl=alloc(0x80);put(decl+0x48,text(def));put(w.entity+0x2e8,decl);
    put<std::int32_t>(w.entity+0x2e0,ctx);put<unsigned char>(w.entity+0xd0,hidden?1:0);
    if(with_model) {w.model=alloc(0x1900);put(w.entity+0xf0,w.model);put(w.model+0x1840,std::array<float,8>{x-.4f,y-.03f,z-.03f,0,x+.4f,y+.03f,z+.03f,0});}
    std::vector<std::uintptr_t> comps;if(comp_rva) comps.push_back(make_component(w.entity,comp_rva));
    put_list(w.entity+0x268,comps);
    put(w.entity+0x300,std::array<float,3>{x,y,z});put(w.entity+0x30c,std::array<float,9>{1,0,0,0,1,0,0,0,1});
    return w;
}
// An NPC with an inventory whose equipped list holds `carried` (weapon ids).
static std::uintptr_t make_npc(unsigned slot,const std::vector<std::uint32_t>& carried) {
    const auto n=make_entity(slot,0x1c5e108);
    const auto inv=make_component(n,0x1b9a878);
    std::vector<std::uintptr_t> records;
    for(const auto id:carried) {const auto r=alloc(0x40);put(r+8,id);records.push_back(r);}
    put_list(inv+0x58,records);
    put_list(n+0x268,{inv});
    return n;
}
// Engine calls the hook saw.
struct Call {int what{};std::uintptr_t entity{};std::array<float,3> a{};std::array<float,9> b{};};
static std::vector<Call> calls;static bool hide_fails=false;static std::uint64_t now_ms=1000;
static bool hook(int what,std::uintptr_t e,const float* a,const float* b) {
    if(what==0 && hide_fails) return false;
    Call c{what,e};
    if(a) std::memcpy(c.a.data(),a,12);
    if(b) std::memcpy(c.b.data(),b,36);
    calls.push_back(c);
    if(what==0) put<unsigned char>(e+0xd0,1);                       // Hide
    if(what==1) put<unsigned char>(e+0xd0,0);                       // Show
    if(what==2) {put(e+0x300,c.a);put(e+0x30c,c.b);}                // Teleport
    return true;
}
static std::uint64_t clock_fn() {return now_ms;}
static std::size_t count_calls(int what) {return static_cast<std::size_t>(std::count_if(calls.begin(),calls.end(),[&](const Call& c){return c.what==what;}));}

static void service_checks() {
    NpcWeaponPickup::engine_hook=hook;NpcWeaponPickup::clock_hook=clock_fn;
    auto read=fake_read;
    reset_world();calls.clear();hide_fails=false;now_ms=1000;NpcWeaponPickup::reset_for_test();
    // player (slot 0): at the origin
    const auto player=make_entity(0,0x1c5de18);put(player+0x300,std::array<float,3>{0,0,0});
    // 1: a dead guard's dropped sword; 2: an elite's dropped pistol; 3: a sword still in a living guard's hand (context alive);
    // 4: a dead NPC's weapon in context 2 but still in his equipped list; 5: a mech sword (no model); 6: the player's own arkWeapon sword
    // in context 2; 7: a prop under another path; 8: a hidden dropped sword nobody owns (an orphan of a save)
    const auto sword=make_weapon(1,"models/weapons/sword_gs_guard/gs_guard_sword.def",2,0x1b9cf48,1.0f,0.f,0.1f);
    const auto pistol=make_weapon(2,"models/weapons/gun_elite/elite_gun.def",2,0x1b54278,-1.0f,0.f,0.1f);
    const auto living=make_weapon(3,"models/weapons/sword_gs_guard/gs_guard_sword.def",0,0x1b9cf48,2.f,0.f,1.f);
    const auto carried=make_weapon(4,"models/weapons/sword_gs_elite/gs_elite_sword.def",2,0x1b9cf48,3.f,0.f,1.f);
    const auto mech=make_weapon(5,"models/weapons/sword_mech/mech_sword_left.def",2,0x1b9cf48,4.f,0.f,1.f,false);
    const auto prop=make_weapon(7,"models/props/crate.def",2,0x1b9cf48,6.f,0.f,.1f);
    const auto orphan=make_weapon(8,"models/weapons/sword_hatter/hatter_sword.def",2,0x1b9cf48,7.f,0.f,.1f,true,true);
    const auto own=make_entity(6,0x1ce1e98);   // arkWeapon: the class test alone excludes it
    (void)living;(void)mech;(void)prop;(void)own;
    make_npc(10,{carried.id});make_npc(11,{});
    NpcWeaponPickup::service(base,read);
    {   // the scan found exactly the dropped sword and pistol, and restored the orphan (first scan only)
        assert(NpcWeaponPickup::tracked()==3);
        const auto n=NpcWeaponPickup::nearest(1,{0.95,0,0.1},.3);assert(n && n->id==sword.id && n->kind==Kind::sword && n->def.find("gs_guard_sword")!=std::string::npos);
        const auto p=NpcWeaponPickup::nearest(0,{-.9,0,.1},.3);assert(p && p->id==pistol.id && p->kind==Kind::pistol && p->def=="models/weapons/gun_elite/elite_gun.def");
        assert(NpcWeaponPickup::nearest(1,{6,0,.1},.3)==std::nullopt || NpcWeaponPickup::nearest(1,{6,0,.1},.3)->id!=7u<<16);
        assert(!NpcWeaponPickup::nearest(0,{2,0,1},.3) && !NpcWeaponPickup::nearest(0,{4,0,1},.3));
        assert(NpcWeaponPickup::orphans_restored==1 && count_calls(1)==1 && calls[0].entity==orphan.entity);
    }
    // not rescanned before the interval; rescanned after
    const auto scans0=NpcWeaponPickup::scans.load();
    now_ms+=100;NpcWeaponPickup::service(base,read);assert(NpcWeaponPickup::scans==scans0);
    now_ms+=300;NpcWeaponPickup::service(base,read);assert(NpcWeaponPickup::scans==scans0+1 && NpcWeaponPickup::tracked()==3);
    // a dead guard's second weapon appears later
    const auto late=make_weapon(12,"models/weapons/gun_female/gun_female.def",2,0x1b54278,0.f,1.f,.1f);
    now_ms+=400;NpcWeaponPickup::service(base,read);assert(NpcWeaponPickup::tracked()==4 && NpcWeaponPickup::nearest(0,{0,1,.1},.2)->id==late.id);
    // the sword's NPC drops it at once in the middle of a swing: carried -> dropped when its record leaves the equipped list
    // take with the left hand: hides on the next service, once
    calls.clear();
    assert(NpcWeaponPickup::take(sword.id,0) && !NpcWeaponPickup::take(sword.id,1) && !NpcWeaponPickup::take(pistol.id,0));
    assert(NpcWeaponPickup::held(0)->id==sword.id && !NpcWeaponPickup::nearest(0,{1,0,.1},2));
    now_ms+=10;NpcWeaponPickup::service(base,read);
    assert(count_calls(0)==1 && calls[0].entity==sword.entity && NpcWeaponPickup::hides==1);
    // the hidden sword is not rediscovered or duplicated by later scans
    for(int i=0;i<4;++i) {now_ms+=400;NpcWeaponPickup::service(base,read);}
    assert(NpcWeaponPickup::tracked()==4 && NpcWeaponPickup::held(0)->id==sword.id && !NpcWeaponPickup::take_lost(0));
    // the right hand takes the pistol, then throws the sword with the left
    assert(NpcWeaponPickup::take(pistol.id,1));now_ms+=10;NpcWeaponPickup::service(base,read);assert(count_calls(0)==2);
    calls.clear();
    const auto rel=NpcWeaponPickup::release(0,{2,0,1.5},{3,0,1});assert(rel && rel->id==sword.id && !NpcWeaponPickup::held(0));
    now_ms+=10;NpcWeaponPickup::service(base,read);
    assert(calls.size()==3 && calls[0].what==2 && calls[1].what==1 && calls[2].what==3);          // teleport, show, velocity: in that order
    assert(calls[0].a[0]==2 && calls[0].a[2]==1.5f && calls[0].b[0]==1 && calls[2].a[0]==3 && calls[2].a[2]==1);  // axis kept as the entity had it
    assert(NpcWeaponPickup::last_release_err_mm<1 && NpcWeaponPickup::velocity_posts==1 && NpcWeaponPickup::teleports==1);
    {   // a released weapon is a pickup again, from where it landed
        const auto n=NpcWeaponPickup::nearest(0,{2,0,1.5},.2);assert(n && n->id==sword.id);
    }
    // a speed that is too fast is clamped to 15 m/s
    assert(NpcWeaponPickup::take(sword.id,0));now_ms+=10;NpcWeaponPickup::service(base,read);calls.clear();
    NpcWeaponPickup::release(0,{0,0,1},{100,0,0},std::array<float,9>{0,1,0,-1,0,0,0,0,1});now_ms+=10;NpcWeaponPickup::service(base,read);
    assert(calls.size()==3 && std::abs(calls[2].a[0]-15)<1e-4 && calls[0].b[0]==0 && calls[0].b[1]==1);
    // take then release before the gm thread ran: both run, in order
    calls.clear();
    assert(NpcWeaponPickup::take(sword.id,0) && NpcWeaponPickup::release(0,{0,0,2},{}));now_ms+=10;NpcWeaponPickup::service(base,read);
    assert(calls.size()==3 && calls[0].what==0 && calls[1].what==2 && calls[2].what==1);           // hide, teleport, show; zero velocity: no post
    // the held weapon vanishes (entity removed): the hand is told once
    assert(NpcWeaponPickup::held(1)->id==pistol.id);
    put(gm+0x3130b0+2*8ull,std::uintptr_t{0});                                                   // slot cleared as the engine does on removal
    now_ms+=10;NpcWeaponPickup::service(base,read);
    assert(!NpcWeaponPickup::held(1) && NpcWeaponPickup::take_lost(1) && !NpcWeaponPickup::take_lost(1));
    // a hide that fails leaves the hand empty and tells the lead
    hide_fails=true;calls.clear();
    assert(NpcWeaponPickup::take(late.id,1));now_ms+=10;NpcWeaponPickup::service(base,read);
    assert(!NpcWeaponPickup::held(1) && NpcWeaponPickup::take_lost(1) && NpcWeaponPickup::hide_failed==1);
    hide_fails=false;
    // teardown shows what is hidden, empties the book and tells the hands; a second teardown is harmless
    assert(NpcWeaponPickup::take(late.id,0));now_ms+=10;NpcWeaponPickup::service(base,read);
    assert(rig::value<unsigned char>(read,late.entity+0xd0).value()==1);
    calls.clear();NpcWeaponPickup::request_teardown();now_ms+=10;NpcWeaponPickup::service(base,read);
    assert(count_calls(1)==1 && calls[0].entity==late.entity && rig::value<unsigned char>(read,late.entity+0xd0).value()==0);
    assert(NpcWeaponPickup::take_lost(0) && NpcWeaponPickup::tracked()==0);
    // level change: another gm object / player id restarts the book without touching the old entities
    now_ms+=400;NpcWeaponPickup::service(base,read);assert(NpcWeaponPickup::tracked()>=1);
    assert(NpcWeaponPickup::take(sword.id,0));now_ms+=10;NpcWeaponPickup::service(base,read);calls.clear();
    put<std::uint32_t>(player+0xa8,id_of(0,9));now_ms+=10;NpcWeaponPickup::service(base,read);
    // the old hidden sword is not unhidden by a stale book, but the fresh scan's orphan sweep restores it (a save made while it was held)
    assert(NpcWeaponPickup::take_lost(0) && NpcWeaponPickup::resets>=1 && count_calls(1)==1 && calls[0].entity==sword.entity);
    now_ms+=400;NpcWeaponPickup::service(base,read);assert(NpcWeaponPickup::tracked()>=1);        // and scans again for the new level
    // an unreadable NPC inventory: do not guess which weapons are carried
    {
        const auto before=NpcWeaponPickup::tracked();
        const auto bad=make_npc(20,{});put<std::uint32_t>(bad+0x268+8,0x7fffffff);                   // corrupt component list header
        const auto degraded=NpcWeaponPickup::scan_degraded.load();
        now_ms+=400;NpcWeaponPickup::service(base,read);assert(NpcWeaponPickup::scan_degraded==degraded+1 && NpcWeaponPickup::tracked()==before);
    }
    assert(NpcWeaponPickup::status_json().find("\"tracked\":")!=std::string::npos);
    std::puts(NpcWeaponPickup::status_json().c_str());
}

// ---- sword identity -------------------------------------------------------------------------------------------------------
static std::uintptr_t stub_decl() {return 0x5a5a5a0;}
static void identity_checks() {
    auto read=fake_read;
    reset_world();NpcWeaponPickup::reset_for_test();
    NpcWeaponPickup::decl_hook=stub_decl;
    const auto player=make_entity(0,0x1c5de18);
    // no inventory at all: the by-name decl, the player as inflictor
    put_list(player+0x268,{});
    auto s=NpcWeaponPickup::sword_identity(base,read,1000.f);
    assert(s && s->player==player && s->entity==player && s->weapon==0 && s->decl==stub_decl() && s->player_id==id_of(0) && s->sword_id==id_of(0));
    assert(s->amount==0 && s->unaware_amount==1000.f);
    // with the player's inventory: his sword is the inflictor and its damage decl is used; no native hit list
    const auto sword=make_entity(3,0x1ce1e98);
    const auto wcomp=make_component(sword,0x1b9d508);const auto wdecl=alloc(0x200);const auto dmg=alloc(0x40);
    put(wcomp+8,wdecl);put(wdecl+0x120,dmg);put_list(sword+0x268,{wcomp});
    const auto inv=make_component(player,0x1b9a878);const auto item=alloc(0x20);put(item+8,id_of(3));put(inv+0x68,item);
    put_list(player+0x268,{inv});
    s=NpcWeaponPickup::sword_identity(base,read);
    assert(s && s->entity==sword && s->decl==dmg && s->sword_id==id_of(3) && s->player_id==id_of(0) && s->weapon==0 && s->unaware_amount==0);
    // no world: nothing
    put(base+0x39a5290,std::uintptr_t{0});
    NpcWeaponPickup::decl_hook=nullptr;
    assert(!NpcWeaponPickup::sword_identity(base,read));
}

int main() {
    book_checks();
    service_checks();
    identity_checks();
    std::cout<<"npc_weapon_pickup_check ok\n";
}
