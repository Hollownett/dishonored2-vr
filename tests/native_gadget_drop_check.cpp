#define D2VR_TESTING 1
#include "../src/native_gadget_drop.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
using namespace d2vr;
using namespace d2vr::gadget_drop;
using G=NativeGadgetDrop;

// ---- a fake game image: sparse bytes -------------------------------------------------------------------------------------------------
static constexpr std::uintptr_t K_base=0x140000000ull,K_game=0x200000000ull,K_player=0x200100000ull,K_fsm=0x200200000ull,K_inv=0x200300000ull,K_rec=0x200400000ull,
    K_item_decl=0x200500000ull,K_comp=0x200600000ull,K_equip_ent=0x200700000ull,K_equip_decl=0x200800000ull,K_equip_name=0x200900000ull,K_list=0x200a00000ull,K_item_name=0x200b00000ull;
static constexpr std::uint32_t equip_id=(5u<<16)|7u;
static std::map<std::uintptr_t,unsigned char> mem;
template<class T> static void put(std::uintptr_t at,T v) {unsigned char b[sizeof(T)];std::memcpy(b,&v,sizeof(T));for(unsigned i=0;i<sizeof(T);++i) mem[at+i]=b[i];}
template<class T> static T get(std::uintptr_t at) {T v{};unsigned char b[sizeof(T)]{};for(unsigned i=0;i<sizeof(T);++i) b[i]=mem.count(at+i)?mem[at+i]:0;std::memcpy(&v,b,sizeof(T));return v;}
static bool reader(std::uintptr_t at,void* out,std::size_t n) {
    auto* o=static_cast<unsigned char*>(out);
    if(!mem.count(at)) return false;   // an unmapped start faults; the tail of a text buffer may be unmapped (reads as 0)
    for(std::size_t i=0;i<n;++i) {const auto it=mem.find(at+i);o[i]=it==mem.end()?0:it->second;}
    return true;
}
static std::array<float,3> pos3(std::uintptr_t e) {return get<std::array<float,3>>(e+0x300);}
static void put_text(std::uintptr_t at,const char* s) {for(std::size_t i=0;;++i) {mem[at+i]=static_cast<unsigned char>(s[i]);if(!s[i]) break;}}
static void put_entity(std::uint32_t id,std::uintptr_t e,const std::array<float,3>& pos) {
    put<std::uintptr_t>(K_game+0x3130b0+(id>>16)*8ull,e);put<std::uint32_t>(e+0xa8,id);put(e+0x300,pos);
}
static void pins() {
    const std::array<std::pair<std::uintptr_t,std::array<unsigned char,16>>,7> p{{
        {0x1e030,{0x44,0x88,0x44,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57}},
        {0x8ae9b0,{0x40,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x40,0x48,0xc7,0x44,0x24,0x20,0xfe}},
        {0x883f60,{0x48,0x83,0xec,0x28,0x45,0x33,0xc9,0xe8,0xc4,0x00,0x00,0x00,0x48,0x83,0xc4,0x28}},
        {0x87d1e0,{0x48,0x83,0xec,0x38,0xc7,0x44,0x24,0x20,0x00,0x00,0x00,0x00,0xe8,0x0f,0x00,0x00}},
        {0x614750,{0x4c,0x8b,0xdc,0x57,0x48,0x83,0xec,0x60,0x49,0xc7,0x43,0xb8,0xfe,0xff,0xff,0xff}},
        {0x6156e0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x50,0x8b}},
        {0x63a2b0,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x81,0x98,0x00,0x00}}}};
    for(const auto& [rva,code]:p) for(unsigned i=0;i<16;++i) mem[K_base+rva+i]=code[i];
}
static void build(const char* name,int count) {
    mem.clear();pins();
    put<std::uintptr_t>(K_base+0x39a5290,K_game);put<std::uintptr_t>(K_game,K_base+0x1c940a8);
    put<std::uintptr_t>(K_game+0x3130b0,K_player);put<std::uintptr_t>(K_player,K_base+0x1c5e428);
    put<std::uintptr_t>(K_player+0x268,K_list);put<std::uint32_t>(K_player+0x270,1);put<std::uint32_t>(K_player+0x274,4u<<8);
    put<std::uintptr_t>(K_list,K_fsm);
    put<std::uintptr_t>(K_fsm,K_base+0x1c73d58);put<std::uintptr_t>(K_fsm+0x10,K_player);put<std::int32_t>(K_fsm+0x18,1);put<std::uint32_t>(K_fsm+0x1c,0xf);put<std::uintptr_t>(K_fsm+0x328,K_inv);
    put<std::uintptr_t>(K_inv,K_base+0x1b9a878);put<std::uintptr_t>(K_inv+0x10,K_player);put<std::uintptr_t>(K_inv+0x70,K_rec);
    put<std::uintptr_t>(K_rec,K_item_decl);put<std::uint32_t>(K_rec+8,equip_id);put<std::uintptr_t>(K_rec+0x10,K_comp);put<std::int32_t>(K_rec+0x20,count);
    put<std::uintptr_t>(K_comp,K_base+0x1b9d8b8);
    put<std::uintptr_t>(K_item_decl,0x1234560ull);
    put_entity(equip_id,K_equip_ent,{0,0,0});
    put<std::uintptr_t>(K_equip_ent+0x2e8,K_equip_decl);put<std::uintptr_t>(K_equip_decl+0x48,K_equip_name);put_text(K_equip_name,name);
}
// ---- the fake engine ------------------------------------------------------------------------------------------------------------------
static unsigned spawns,removes,adds,syncs,teleports,entity_removals,next_slot;
static std::string last_def;static std::array<float,3> last_pos;static bool spawn_ok,infinite,corrupt;static std::uintptr_t last_entity;
static double clock_s;
static std::uintptr_t f_spawn(std::uintptr_t,const char* def,const float* pos,const float*) {
    ++spawns;last_def=def;if(!spawn_ok) return 0;
    const std::uintptr_t e=0x201000000ull+0x10000ull*next_slot;const std::uint32_t id=((6u+next_slot)<<16)|1u;++next_slot;
    last_pos={pos[0],pos[1],pos[2]};put_entity(id,e,last_pos);last_entity=e;return e;
}
static int f_remove(std::uintptr_t,std::uintptr_t,std::uintptr_t,int n) {
    ++removes;if(infinite) return 0;
    const int c=get<std::int32_t>(K_rec+0x20);put<std::int32_t>(K_rec+0x20,corrupt?c+5:c-n);return n;
}
static int f_add(std::uintptr_t,std::uintptr_t,std::uintptr_t,int n) {++adds;put<std::int32_t>(K_rec+0x20,get<std::int32_t>(K_rec+0x20)+n);return n;}
static void f_sync(std::uintptr_t,std::uintptr_t) {++syncs;}
static void f_teleport(std::uintptr_t,std::uintptr_t e,const float* pos,const float*) {++teleports;put(e+0x300,std::array<float,3>{pos[0],pos[1],pos[2]});}
static void f_remove_entity(std::uintptr_t,std::uintptr_t e) {++entity_removals;const auto id=get<std::uint32_t>(e+0xa8);put<std::uintptr_t>(K_game+0x3130b0+(id>>16)*8ull,0);}
static std::optional<WorldTraceHit> f_floor(Vec3 a,Vec3 b) {   // a floor at z = 0 for a sphere of GadgetFlight::radius
    const double r=GadgetFlight::radius;
    if(a.z>=r && b.z<r) {
        WorldTraceHit h;h.fraction=(a.z-r)/(a.z-b.z);h.centre=a+(b-a)*h.fraction;h.point=h.centre-Vec3{0,0,r};h.normal={0,0,1};return h;
    }
    return {};
}
// sim v767: once the engine starts a runtime spawn's body, every sweep from where it lies starts inside the item itself (an up-facing overlap)
static std::optional<WorldTraceHit> f_self_then_floor(Vec3 a,Vec3 b) {
    if(const auto floor=f_floor(a,b)) return floor;
    WorldTraceHit h;h.initial_overlap=true;h.fraction=0;h.centre=a;h.point=a;h.normal={0,0,1};h.entity=last_entity;return h;
}
static double f_clock() {return clock_s;}
static void reset(const char* name="models/weapons/gadgets/regular_grenade_equipment.def",int count=3) {
    build(name,count);G::reset_for_tests();
    for(auto* c:{&G::requested,&G::rejected,&G::queue_full,&G::services,&G::dropped,&G::not_verified,&G::no_item,&G::kind_mismatch,&G::mines_blocked,&G::spawn_failed,
        &G::remove_failed,&G::rolled_back,&G::infinite_ammo,&G::landed,&G::physics_seen,&G::deviated,&G::untracked,&G::taken,&G::take_failed,&G::take_none,&G::faults}) c->store(0);
    G::mines_enabled=false;
    spawns=removes=adds=syncs=teleports=entity_removals=next_slot=0;spawn_ok=true;infinite=corrupt=false;clock_s=100;last_def.clear();
    G::calls={f_spawn,f_remove,f_add,f_sync,f_teleport,f_remove_entity};G::cast_hook=f_floor;G::clock_hook=f_clock;
}
static void frames(unsigned n) {auto r=reader;for(unsigned i=0;i<n;++i) {clock_s+=1./60;G::service(K_base,r);}}
static const std::array<float,9> up_axis{1,0,0,0,1,0,0,0,1};

int main() {
    // ---- kinds ----
    assert(classify("models/weapons/gadgets/regular_grenade_equipment.def")==Kind::regular_grenade);
    assert(classify("Models/Weapons/Gadgets/STICKY_GRENADE_EQUIPMENT.def")==Kind::sticky_grenade);
    assert(classify("models/weapons/gadgets/stun_mine_equipment.def")==Kind::stun_mine);
    assert(classify("models/weapons/gadgets/springrazor_equipment.def")==Kind::spring_razor);
    assert(classify("models/weapons/pistol/pistol_equipment.def")==Kind::any && classify("")==Kind::any);
    assert(std::string(info(Kind::regular_grenade)->pickup_def)=="models/interactive/pickups_entities/ammo_grenade_regular.def" && !info(Kind::regular_grenade)->mine);
    assert(info(Kind::stun_mine)->mine && info(Kind::spring_razor)->mine && !info(Kind::any));
    for(const auto& k:kinds) assert(k.keep || std::string(k.pickup_def).find("pickups_entities/ammo_")!=std::string::npos);

    // ---- validation ----
    {
        const std::array<float,3> p{1,2,3};
        assert(sanitize(p,{0,0,0},up_axis));
        assert(!sanitize({NAN,0,0},{0,0,0},up_axis) && !sanitize(p,{0,INFINITY,0},up_axis) && !sanitize({1e9f,0,0},{0,0,0},up_axis));
        assert(!sanitize(p,{0,0,0},{1,0,0,0,1,0,0,0,NAN}) && !sanitize(p,{0,0,0},{2,0,0,0,1,0,0,0,1}) && !sanitize(p,{0,0,0},{1,0,0,1,0,0,0,0,1}));
        assert(!sanitize(p,{0,0,0},{1,0,0,0,-1,0,0,0,1}));   // left-handed
        const auto l=sanitize(p,{30,0,40},up_axis);assert(l);
        assert(std::abs(std::hypot(l->vel[0],std::hypot(l->vel[1],l->vel[2]))-max_speed)<1e-3 && std::abs(l->vel[0]/l->vel[2]-.75)<1e-4);   // clamped, direction kept
    }

    // ---- flight ----
    {
        GadgetFlight f;f.launch(*sanitize({0,0,1.2f},{0,0,0},up_axis));
        unsigned n=0;while(f.step(1./60,f_floor) && n<600) ++n;
        assert(!f.flying && n<60 && f.bounces<=GadgetFlight::max_bounces);
        assert(std::abs(f.pos.z-(GadgetFlight::radius+.01))<.01 && length(f.vel)==0);
        assert(f.axis[2].z>.999 && std::abs(f.axis[0].z)<1e-6 && dot(cross(f.axis[0],f.axis[1]),f.axis[2])>.999);
        const auto rows=f.rows();for(float v:rows) assert(std::isfinite(v));
    }
    {   // thrown forward: travels, bounces at most 3 times, comes to rest flat
        GadgetFlight f;f.launch(*sanitize({0,0,1.3f},{5,2,3},up_axis));
        unsigned n=0;while(f.step(1./60,f_floor) && n<900) ++n;
        assert(!f.flying && n<400 && f.bounces>=1 && f.bounces<=GadgetFlight::max_bounces && f.pos.x>2 && f.axis[2].z>.999 && GadgetFlight::finite(f));
    }
    {   // never meets the world: stops at the flight limit with a finite pose
        GadgetFlight f;f.launch(*sanitize({0,0,50},{3,0,0},up_axis));
        const auto none=[](Vec3,Vec3)->std::optional<WorldTraceHit> {return {};};
        unsigned n=0;while(f.step(1./60,none) && n<2000) ++n;
        assert(!f.flying && n>=300 && n<=400 && GadgetFlight::finite(f));
    }
    {   // a moving body (static_support false) does not stop it
        GadgetFlight f;f.launch(*sanitize({0,0,1},{0,0,0},up_axis));
        unsigned n=0;const auto soft=[](Vec3 a,Vec3 b)->std::optional<WorldTraceHit> {auto h=f_floor(a,b);if(h) h->static_support=false;return h;};
        while(f.step(1./60,soft) && n<30) ++n;
        assert(f.flying && f.pos.z<.5);
    }
    {   // a wall: slides down it and lands on the floor
        GadgetFlight f;f.launch(*sanitize({0,0,1},{6,0,0},up_axis));
        const auto world=[](Vec3 a,Vec3 b)->std::optional<WorldTraceHit> {
            if(a.x<2 && b.x>=2) {WorldTraceHit h;h.fraction=(2-a.x)/(b.x-a.x);h.centre=a+(b-a)*h.fraction;h.point=h.centre+Vec3{.06,0,0};h.normal={-1,0,0};return h;}
            return f_floor(a,b);};
        unsigned n=0;while(f.step(1./60,world) && n<900) ++n;
        assert(!f.flying && f.pos.x<2.01 && f.axis[2].z>.999);
    }
    {   // landing on a slope lies on the slope (up = the normal), still a rotation
        GadgetFlight f;f.launch(*sanitize({0,0,1},{0,0,0},up_axis));
        const Vec3 n=unit(Vec3{.3,0,1});
        f.land(n);
        assert(length(f.axis[2]-n)<1e-9 && std::abs(dot(f.axis[0],n))<1e-9 && dot(cross(f.axis[0],f.axis[1]),f.axis[2])>.999999);
    }

    // ---- the request queue ----
    {
        reset();
        for(unsigned i=0;i<G::max_queue;++i) assert(G::drop(Kind::any,{0,0,1},{0,0,0},up_axis));
        assert(!G::drop(Kind::any,{0,0,1},{0,0,0},up_axis) && G::queue_full==1);
        assert(!G::drop(Kind::any,{NAN,0,1},{0,0,0},up_axis) && G::rejected==1);
        assert(!G::drop(Kind::any,{0,0,1},{0,0,0},{0,0,0,0,0,0,0,0,0}) && G::rejected==2);
        assert(!G::take({NAN,0,0},.3f) && G::rejected==3);
    }

    // ---- drop: the whole path ----
    {
        reset();
        assert(G::drop(Kind::any,{1.f,2.f,1.2f},{0,0,0},up_axis));
        frames(1);
        assert(spawns==1 && last_def=="models/interactive/pickups_entities/ammo_grenade_regular.def" && last_pos==(std::array<float,3>{1.f,2.f,1.2f}));
        assert(removes==1 && get<std::int32_t>(K_rec+0x20)==0 && syncs==1 && G::dropped==1 && G::spawn_failed==0);   // v790: the whole stack of 3
        auto pub=G::published();assert(pub.size()==1 && pub[0].kind==Kind::regular_grenade && !pub[0].resting);
        frames(90);
        pub=G::published();
        assert(G::landed==1 && pub.size()==1 && pub[0].resting && std::abs(pub[0].pos[2]-(GadgetFlight::radius+.01f))<.02f && pub[0].pos[0]==1.f);
        assert(teleports>10 && teleports<60);
        const auto before=teleports;frames(10);assert(teleports==before);   // resting: no more commands
        assert(G::probe().have && G::probe().entity_flags==0 && G::probe().components==0);   // probed ~1 s after the spawn
        const std::string js=G::status_json();
        assert(js.find("\"dropped\":1")!=std::string::npos && js.find("\"landed\":1")!=std::string::npos && js.find("\"tracked\":1")!=std::string::npos);
        assert(js.front()=='{' && js.back()=='}');
        // the engine takes over the body after the watch window: observed, never fought
        put(last_entity+0x300,std::array<float,3>{1.f,2.f,.3f});
        frames(1);assert(G::deviated==1 && pos3(last_entity)[2]>.06f);   // within the settle window: put back
        clock_s+=4;put(last_entity+0x300,std::array<float,3>{1.f,2.f,.3f});frames(1);
        assert(G::physics_seen==1);
        const auto t2=teleports;frames(5);assert(teleports==t2);
        put(last_entity+0x300,std::array<float,3>{2.f,3.f,.9f});frames(1);{const auto live=G::published();assert(live.size()==1 && live[0].pos[0]==2.f && live[0].pos[2]==.9f);}   // v1046: an engine-simulated pickup is published where it IS
    }
    {   // a thrown drop travels first
        reset();
        assert(G::drop(Kind::regular_grenade,{0,0,1.3f},{5,0,2},up_axis));frames(120);
        const auto pub=G::published();assert(pub.size()==1 && pub[0].resting && pub[0].pos[0]>1.5f && G::landed==1);
    }
    {   // other kinds
        reset("models/weapons/gadgets/sticky_grenade_equipment.def");
        assert(G::drop(Kind::any,{0,0,1},{0,0,0},up_axis));frames(1);
        assert(last_def=="models/interactive/pickups_entities/ammo_grenade_sticky.def" && G::dropped==1);
        reset("models/weapons/gadgets/stun_mine_equipment.def");
        assert(G::drop(Kind::any,{0,0,1},{0,0,0},up_axis));frames(1);
        assert(spawns==0 && removes==0 && G::mines_blocked==1 && get<std::int32_t>(K_rec+0x20)==3);   // off by default: nothing spawned, nothing consumed
        G::mines_enabled=true;
        assert(G::drop(Kind::stun_mine,{0,0,1},{0,0,0},up_axis));frames(1);
        assert(last_def=="models/interactive/pickups_entities/ammo_stun_mine.def" && get<std::int32_t>(K_rec+0x20)==0);   // the whole stack
        reset("models/weapons/gadgets/springrazor_equipment.def");G::mines_enabled=true;
        assert(G::drop(Kind::spring_razor,{0,0,1},{0,0,0},up_axis));frames(1);
        assert(last_def=="models/interactive/pickups_entities/ammo_springrazor.def");
    }
    {   // refusals leave the inventory alone
        reset();assert(G::drop(Kind::stun_mine,{0,0,1},{0,0,0},up_axis));frames(1);
        assert(G::kind_mismatch==1 && spawns==0 && removes==0);
        reset("models/weapons/pistol/pistol_equipment.def");assert(G::drop(Kind::any,{0,0,1},{0,0,0},up_axis));frames(1);
        assert(G::no_item==1 && spawns==0);
        reset("models/weapons/gadgets/regular_grenade_equipment.def",0);assert(G::drop(Kind::any,{0,0,1},{0,0,0},up_axis));frames(1);
        assert(G::no_item==1 && spawns==0);
        reset();spawn_ok=false;assert(G::drop(Kind::any,{0,0,1},{0,0,0},up_axis));frames(1);
        assert(G::spawn_failed==1 && removes==0 && get<std::int32_t>(K_rec+0x20)==3 && G::published().empty());   // a failed spawn costs nothing
        reset();corrupt=true;assert(G::drop(Kind::any,{0,0,1},{0,0,0},up_axis));frames(1);
        assert(G::rolled_back==1 && entity_removals==1 && G::dropped==0 && G::published().empty());   // an unreadable result removes the new entity again
        reset();infinite=true;assert(G::drop(Kind::any,{0,0,1},{0,0,0},up_axis));frames(1);
        assert(G::infinite_ammo==1 && G::dropped==1 && get<std::int32_t>(K_rec+0x20)==3);   // the K_game's own rule: nothing consumed
        reset();mem.erase(mem.find(K_inv+0x70));G::drop(Kind::any,{0,0,1},{0,0,0},up_axis);frames(1);assert(G::no_item==1);   // a slot that cannot be read
    }
    {   // the tracked K_list is bounded; the oldest is left to the K_game's Use
        reset("models/weapons/gadgets/regular_grenade_equipment.def",20);
        for(unsigned i=0;i<G::max_tracked+2;++i) {put<std::int32_t>(K_rec+0x20,3);assert(G::drop(Kind::any,{float(i),0,1},{0,0,0},up_axis));frames(1);}   // v790: each drop takes the whole stack: refill
        assert(G::published().size()==G::max_tracked && G::untracked==2 && G::dropped==G::max_tracked+2);
    }

    {   // a free probe spawn takes nothing and cannot be taken back; it needs an explicit kind
        reset("models/weapons/pistol/pistol_equipment.def");
        assert(!G::spawn_free(Kind::any,{0,0,1},{0,0,0},up_axis) && G::rejected==1);
        assert(G::spawn_free(Kind::sticky_grenade,{0,0,1},{0,0,0},up_axis));frames(90);
        assert(last_def=="models/interactive/pickups_entities/ammo_grenade_sticky.def" && removes==0 && get<std::int32_t>(K_rec+0x20)==3 && G::landed==1);
        assert(G::take({0,0,0},.5f));frames(1);assert(G::take_none==1 && adds==0);
        // v766: the free spawn is the mine probe (mines_enabled stays off): it spawns, takes nothing, and its probe is kept per mine kind
        assert(G::spawn_free(Kind::stun_mine,{0,0,1},{0,0,0},up_axis));frames(1);
        assert(G::mines_blocked==0 && last_def=="models/interactive/pickups_entities/ammo_stun_mine.def" && removes==0 && get<std::int32_t>(K_rec+0x20)==3);
        frames(90);const std::string js=G::status_json();
        assert(js.find("\"stun_probe\":{\"mine\":false,\"mine_started\":false")!=std::string::npos && js.find("razor_probe")==std::string::npos && js.back()=='}');
    }

    {   // live v769 (user's save): the gadget weapon entity is the generic grenade_base.def; the inventory item decl names the gadget at +0x8
        reset("models/weapons/gadgets/grenade_base.def");G::mines_enabled=true;
        put<std::uintptr_t>(K_item_decl+8,K_item_name);put_text(K_item_name,"components/equipment/gadgets/stun_mine.inventoryitem");
        assert(G::drop(Kind::any,{0,0,1},{0,0,0},up_axis));frames(1);
        assert(G::no_item==0 && spawns==1 && last_def=="models/interactive/pickups_entities/ammo_stun_mine.def" && removes==1);
        reset("models/weapons/gadgets/grenade_base.def");
        put<std::uintptr_t>(K_item_decl+8,K_item_name);put_text(K_item_name,"components/equipment/gadgets/regular_grenade.inventoryitem");
        assert(G::drop(Kind::any,{0,0,1},{0,0,0},up_axis));frames(1);
        assert(spawns==1 && last_def=="models/interactive/pickups_entities/ammo_grenade_regular.def");
    }
    {   // v773 (user, headset v772: the Heart vanished when let go): the Heart drops as its world pickup but never leaves the inventory; a take
        // adds nothing back, and the Heart equipped again (the wheel) removes a pickup still lying a second after its drop
        reset("models/weapons/heart/heart_equipment.def",1);put<std::uintptr_t>(K_comp,K_base+0x1b99c68);
        put<std::uintptr_t>(K_item_decl+8,K_item_name);put_text(K_item_name,"models/items/heart/heart.inventoryitem");
        assert(G::drop(Kind::any,{1.f,2.f,1.2f},{0,0,0},up_axis));frames(1);
        assert(spawns==1 && last_def=="models/weapons/heart/heart_pickup.def" && removes==0 && get<std::int32_t>(K_rec+0x20)==1 && G::dropped==1);
        auto pub=G::published();assert(pub.size()==1 && pub[0].keep && pub[0].kind==Kind::heart && pub[0].entity==last_entity);
        frames(30);assert(G::published().size()==1);                       // still in the hand (the unequip follows): the pickup stays for a second
        frames(60);assert(G::published().empty() && entity_removals==1);   // a second later the Heart is still equipped: one Heart, the pickup goes
        reset("models/weapons/heart/heart_equipment.def",1);put<std::uintptr_t>(K_comp,K_base+0x1b99c68);
        put<std::uintptr_t>(K_item_decl+8,K_item_name);put_text(K_item_name,"models/items/heart/heart.inventoryitem");
        assert(G::drop(Kind::heart,{1.f,0,1.f},{0,0,0},up_axis));frames(1);
        put<std::uintptr_t>(K_inv+0x70,0);frames(90);                         // unequipped: it lies on the floor
        assert(G::take({1.f,.1f,.1f},.3f));frames(1);
        assert(adds==0 && G::taken==1 && G::published().empty() && get<std::int32_t>(K_rec+0x20)==1); // taken back: nothing added (it never left)
        reset("models/weapons/heart/heart_equipment.def",1);put<std::uintptr_t>(K_comp,K_base+0x1b99c68);
        put<std::uintptr_t>(K_item_decl+8,K_item_name);put_text(K_item_name,"models/items/heart/heart.inventoryitem");
        assert(G::drop(Kind::heart,{1.f,0,1.f},{0,0,0},up_axis));frames(1);
        assert(G::take({1.f,0,1.f},.3f));frames(1);                            // v779: caught before the hand's unequip ran: the Heart is still in the slot
        assert(adds==0 && G::taken==1 && syncs==0);                              // ... and has no ammo count: no sync on its component
    }
    {   // its own body is never its floor: it falls to the real one (it landed in mid-air on itself, or fell through the deck, sim v767)
        reset();G::cast_hook=f_self_then_floor;
        assert(G::drop(Kind::any,{1.f,2.f,1.2f},{0,0,0},up_axis));frames(90);
        const auto pub=G::published();
        assert(pub.size()==1 && pub[0].resting && std::abs(pub[0].pos[2]-(GadgetFlight::radius+.01f))<.02f && G::self_hits>0);
        G::cast_hook=f_floor;
    }

    // ---- take back ----
    {
        reset();assert(G::drop(Kind::any,{1.f,0,1.f},{0,0,0},up_axis));frames(90);
        const auto miss=G::take_ticket({3.f,0,0},.3f);assert(miss);frames(1);assert(G::take_none==1 && adds==0 && G::published().size()==1);   // out of reach
        {const auto r=G::take_result();assert(r.id==miss && r.result==2);}   // v1046: the caller sees 'nothing taken' and equips nothing
        const auto hit=G::take_ticket({1.f,.1f,.1f},.3f);assert(hit && hit!=miss);frames(1);
        {const auto r=G::take_result();assert(r.id==hit && r.result==1 && r.kind==Kind::regular_grenade);}
        assert(adds==1 && entity_removals==1 && G::taken==1 && get<std::int32_t>(K_rec+0x20)==3 && G::published().empty());
        assert(syncs==2);   // once after the drop, once after the take
        // the native Use took it first: the entity is gone, the K_list forgets it
        assert(G::drop(Kind::any,{1.f,0,1.f},{0,0,0},up_axis));frames(90);
        f_remove_entity(K_base,last_entity);frames(1);assert(G::published().empty());
        assert(G::take({1.f,0,0},.3f));frames(1);assert(G::take_none==2);
    }
    std::cout<<"native_gadget_drop_check: ok\n";
}
