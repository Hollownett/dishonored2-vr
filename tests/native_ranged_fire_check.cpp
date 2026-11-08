// Standalone check for src/native_ranged_fire.hpp: the per-hand state machine, shot validation, the request queue, and the whole
// service flow over a fake game image with fake native calls (no game, no real calls).
#include "../src/native_ranged_fire.hpp"
#include <cassert>
#include <cstdio>
#include <unordered_map>

using namespace d2vr;
using Fire=NativeRangedFire;
using ranged_fire::HandState;
using ranged_fire::Kind;
using ranged_fire::Why;

// ---------------------------------------------------------------- fake game image
constexpr std::uintptr_t BASE=0x10000000,GAME=0x20000000,PLAYER=0x21000000,PCL=0x21100000,INV=0x22000000,RECS=0x23000000,
    PCOMP=0x24000000,PDECL=0x25000000,PENT=0x26000000,PAMMO_DECL=0x27000000,PAMMO=0x27100000,PDEF=0x27200000,
    ATTR=0x28000000,TABLE=0x28100000,CAM=0x28200000,
    XCOMP=0x2a000000,XDECL=0x2b000000,XENT=0x2c000000,XAMMO_DECL=0x2d000000,XAMMO=0x2d100000,XDEF=0x2d200000,PROJ=0x30000000;
constexpr std::uint32_t PLAYER_ID=7,PID=(5u<<16)|1,XID=(6u<<16)|1;
struct Mem {
    std::unordered_map<std::uintptr_t,unsigned char> b;
    bool erase_on_read_fail=false;
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
static int g_pool_bullet=7,g_pool_bolt=5;
static void build(int bullets,int bolts,int mag=1,int cap=1) {
    M=Mem{};g_pool_bullet=bullets;g_pool_bolt=bolts;
    M.put<std::uint64_t>(BASE+0x39a5290,GAME);
    M.put<std::uint64_t>(GAME,BASE+0x1c940a8);
    M.put<std::uint64_t>(GAME+0x3130b0,PLAYER);
    M.put<std::uint64_t>(GAME+0x3130b0+5*8,PENT);M.put<std::uint64_t>(GAME+0x3130b0+6*8,XENT);
    M.put<std::uint64_t>(PLAYER,BASE+0x1c5e428);M.put<std::uint32_t>(PLAYER+0xa8,PLAYER_ID);
    M.list(PLAYER+0x268,PCL,1);M.put<std::uint64_t>(PCL,INV);
    M.put<std::uint64_t>(INV,BASE+0x1b9a878);M.put<std::uint64_t>(INV+0x10,PLAYER);M.list(INV+0x38,RECS,4);
    struct R {std::uintptr_t decl,comp,eq;std::uint32_t id;std::int32_t count;};
    const R recs[4]{{0xa0000,PCOMP,PDECL,PID,1},{PAMMO_DECL,0,0,0,bullets},{0xb0000,XCOMP,XDECL,XID,1},{XAMMO_DECL,0,0,0,bolts}};
    for(unsigned i=0;i<4;++i) {
        const auto at=RECS+i*0x38ull;M.zero(at,0x38);
        M.put<std::uint64_t>(at,recs[i].decl);M.put<std::uint32_t>(at+8,recs[i].id);M.put<std::uint64_t>(at+0x10,recs[i].comp);
        M.put<std::uint64_t>(at+0x18,recs[i].eq);M.put<std::int32_t>(at+0x20,recs[i].count);
    }
    struct W {std::uintptr_t comp,decl,ent,ammo_decl,ammo,def,vt,dvt;std::uint32_t id;};
    const W w[2]{{PCOMP,PDECL,PENT,PAMMO_DECL,PAMMO,PDEF,0x1b53f58,0x1b53e18,PID},{XCOMP,XDECL,XENT,XAMMO_DECL,XAMMO,XDEF,0x1b9cbf8,0x1b9cab8,XID}};
    for(const auto& k:w) {
        M.zero(k.comp,0x200);M.put<std::uint64_t>(k.comp,BASE+k.vt);M.put<std::uint64_t>(k.comp+8,k.decl);M.put<std::uint64_t>(k.comp+0x10,k.ent);
        M.put<std::int32_t>(k.comp+0x18,1);M.put<std::uint32_t>(k.comp+0x1c,2); // retained, not started
        M.put<std::uint64_t>(k.comp+0x30,ATTR);M.put<std::uint64_t>(k.comp+0x40,CAM);M.put<std::uint64_t>(k.comp+0x48,CAM);M.put<std::uint64_t>(k.comp+0x68,INV);M.put<std::uint64_t>(k.comp+0x98,k.ammo);
        M.put<std::int32_t>(k.comp+0xa0,mag);M.put<std::int32_t>(k.comp+0xa4,cap);M.list(k.comp+0xa8,0,0);M.put<std::uint32_t>(k.comp+0xac,0);
        M.put<std::uint64_t>(k.decl,BASE+k.dvt);
        M.zero(k.ent,0x340);M.put<std::uint64_t>(k.ent,BASE+0x1ce1e98);M.put<std::uint32_t>(k.ent+0xa8,k.id);M.put<std::uint32_t>(k.ent+0xac,PLAYER_ID);
        M.put<std::uint64_t>(k.ammo,k.ammo_decl);M.put<std::uint64_t>(k.ammo+0x10,k.def);M.put<std::uint64_t>(k.ammo_decl,0);M.put<std::uint64_t>(k.def,0);
    }
    M.put<std::int32_t>(XCOMP+0x158,3);M.put<std::int32_t>(XCOMP+0x15c,4); // bolt speed and extra-flag attributes
    M.put<std::int32_t>(PCOMP+0x158,-1);M.put<std::int32_t>(PCOMP+0x15c,-1);
    M.put<std::uint64_t>(ATTR+0x48,TABLE);M.zero(TABLE,0x200);
    M.put<float>(TABLE+0x24+3*0x28,1.5f);M.put<float>(TABLE+0x24+4*0x28,1.0f);
    M.put<std::uint64_t>(CAM,0);
}

// ---------------------------------------------------------------- fake natives
static int n_create,n_launch,n_sound,n_spend,n_native_spend,n_native_reload,n_mark;
static bool g_fail_create,g_fail_launch,g_projectile_started=true,g_own_in_create,g_own_in_launch;
static std::uintptr_t g_last_proj;
static std::vector<std::string> g_calls;
static float g_origin[3],g_axis[9],g_speed;
static std::uintptr_t g_launch_comp,g_sound_entity;
static std::uint64_t g_now=1000;
static void set_pool(std::uintptr_t decl,int v) {
    for(unsigned i=0;i<4;++i) if(M.get<std::uint64_t>(RECS+i*0x38ull)==decl) M.put<std::int32_t>(RECS+i*0x38ull+0x20,v);
}
static int get_pool(std::uintptr_t decl) {
    for(unsigned i=0;i<4;++i) if(M.get<std::uint64_t>(RECS+i*0x38ull)==decl) return M.get<std::int32_t>(RECS+i*0x38ull+0x20);
    return -1;
}
static bool f_create(std::uintptr_t,std::uintptr_t comp,std::uintptr_t& proj) {
    ++n_create;g_calls.push_back("create");g_own_in_create=ranged_fire_own_launch();
    if(g_fail_create) {proj=0;return false;}
    const auto slot=9+n_create;proj=PROJ+static_cast<std::uintptr_t>(n_create)*0x10000;
    const std::uint32_t id=(static_cast<std::uint32_t>(slot)<<16)|3;
    M.zero(proj,0x3000);M.put<std::uint64_t>(proj,BASE+0x1c40000);M.put<std::uint32_t>(proj+0xa8,id);
    M.list(proj+0x268,proj+0x1000,3);
    for(unsigned i=0;i<3;++i) {
        const auto c=proj+0x2000+i*0x100ull;M.put<std::uint64_t>(proj+0x1000+i*8,c);
        M.put<std::int32_t>(c+0x18,1);M.put<std::uint32_t>(c+0x1c,g_projectile_started?0xa:2);
    }
    M.put<std::uint64_t>(GAME+0x3130b0+static_cast<std::uintptr_t>(slot)*8,proj);
    g_last_proj=proj;(void)comp;return true;
}
static bool f_launch(std::uintptr_t,std::uintptr_t comp,std::uintptr_t proj,const float* o,const float* a,float s) {
    ++n_launch;g_calls.push_back("launch");g_own_in_launch=ranged_fire_own_launch();
    if(g_fail_launch) return false;
    std::memcpy(g_origin,o,12);std::memcpy(g_axis,a,36);g_speed=s;g_launch_comp=comp;
    M.put<float>(proj+0x300,o[0]);M.put<float>(proj+0x304,o[1]);M.put<float>(proj+0x308,o[2]);
    return true;
}
static bool f_sound(std::uintptr_t,std::uintptr_t,std::uintptr_t entity,const float*,const float*) {++n_sound;g_calls.push_back("sound");g_sound_entity=entity;return true;}
static std::uintptr_t f_component(std::uintptr_t,std::uintptr_t entity,std::uintptr_t) {return entity+0x2000;}
static bool f_spend(std::uintptr_t,std::uintptr_t,std::uintptr_t decl) {++n_spend;g_calls.push_back("spend");set_pool(decl,get_pool(decl)-1);return true;}
static bool f_native_spend(std::uintptr_t,std::uintptr_t comp) {
    ++n_native_spend;g_calls.push_back("native_spend");
    const int a0=M.get<std::int32_t>(comp+0xa0);M.put<std::int32_t>(comp+0xa0,std::max(a0-1,0));
    const auto ammo=M.get<std::uint64_t>(comp+0x98);set_pool(M.get<std::uint64_t>(ammo),get_pool(M.get<std::uint64_t>(ammo))-1);return true;
}
static bool f_native_reload(std::uintptr_t,std::uintptr_t comp) {
    ++n_native_reload;
    const auto ammo=M.get<std::uint64_t>(comp+0x98);const int pool=get_pool(M.get<std::uint64_t>(ammo));
    M.put<std::int32_t>(comp+0xa0,std::min(pool,M.get<std::int32_t>(comp+0xa4)));return true;
}
static bool f_mark(std::uintptr_t,std::uintptr_t) {++n_mark;return true;}
static int n_flash;static std::uintptr_t g_flash_entity;
static bool f_flash(std::uintptr_t,std::uintptr_t,std::uintptr_t entity,const float*,const float*) {++n_flash;g_calls.push_back("flash");g_flash_entity=entity;return true;}
static const Fire::Natives NAT{&f_create,&f_launch,&f_sound,&f_component,&f_spend,&f_native_spend,&f_native_reload,&f_mark,&f_flash};

static void clear_counters() {
    n_create=n_launch=n_sound=n_spend=n_native_spend=n_native_reload=n_mark=0;g_calls.clear();g_fail_create=g_fail_launch=false;g_projectile_started=true;
    g_own_in_create=g_own_in_launch=false;Fire::reset_counters();
}
static void fresh(int bullets=7,int bolts=5,int mag=1,int cap=1) {
    build(bullets,bolts,mag,cap);clear_counters();Fire::reset();
    Fire::dry_run=false;Fire::play_sound=true;Fire::auto_reload=true;Fire::shared_native_magazine=false;
    g_now=1000;Fire::clock=+[]()->std::uint64_t {return g_now;};
}
static Fire::Shot shot(unsigned side,Fire::Shot::Kind kind,float x=1.f) {
    Fire::Shot s{};s.side=side;s.kind=kind;s.origin[0]=x;s.origin[1]=2;s.origin[2]=3; // forward +X
    return s;
}
namespace d2vr {
struct NativeRangedFireTest { // the friend: what NativeRangedFire::service does between the queue and service_with
    static std::vector<Fire::Request> take() {
        std::vector<Fire::Request> w;{std::lock_guard<std::mutex> l(Fire::m_);w.swap(Fire::queue_);Fire::pending_[0]=Fire::pending_[1]=false;}
        return w;
    }
    static bool latched() {return Fire::latched_.load();}
    static void track_ammo() {Fire::track_ammo(BASE,M);}
    static void swapped(std::uintptr_t base,std::uintptr_t rva,std::uintptr_t comp,std::uintptr_t entity,const float* o,const float* a) {Fire::swapped(base,rva,comp,entity,o,a);}
};
}
// v900: what 63b3c0 sees during the swap (real memory, the real swap; the call target is this stand-in)
static std::uintptr_t g_seen_owner;static std::int32_t g_seen_handle;static float g_seen_pos[3];
static void __fastcall fake_shot_fx(std::uintptr_t comp,char) {
    g_seen_owner=*reinterpret_cast<std::uintptr_t*>(comp+0x10);g_seen_handle=*reinterpret_cast<std::int32_t*>(comp+0x20);
    std::memcpy(g_seen_pos,reinterpret_cast<const void*>(g_seen_owner+0x300),12);
}
using Test=d2vr::NativeRangedFireTest;
static void service(const std::vector<Fire::Request>& w) {Fire::service_with(BASE,M,NAT,w);}
static void run_queued() {service(Test::take());}
static bool has(const std::string& s,const char* t) {return s.find(t)!=std::string::npos;}

int main() {
    // ---- HandState: cooldown, reload, magazine
    { // v1094: the upgrades' scene lengths
        using ranged_fire::timing;using ranged_fire::condition_of;
        assert(timing(Kind::pistol,0).cool==330 && timing(Kind::pistol,0).reload==1500);
        assert(timing(Kind::pistol,condition_of("ReloadSpeed")).reload==1000 && timing(Kind::pistol,condition_of("ReloadSpeed")|condition_of("ReloadSpeed2")).reload==766);
        assert(timing(Kind::crossbow,0).cool==666 && timing(Kind::crossbow,0).reload==900);
        assert(timing(Kind::crossbow,condition_of("ReloadSpeed")).cool==100 && timing(Kind::crossbow,condition_of("ReloadSpeed")).reload==566);
        assert(timing(Kind::crossbow,condition_of("NailGun")).reload==581 && condition_of("Other")==0 && condition_of(nullptr)==0);
        HandState t;t.sync(0,Kind::pistol,1,5);t.fired(0,true,330,766);assert(t.ready_at==330 && t.reload_done==330+766);
    }
    {
        HandState h;assert(!h.sync(1000,Kind::pistol,1,7) && h.loaded==1 && h.check(1000,7)==Why::ok);
        h.fired(1000,true);assert(h.loaded==0 && h.ready_at==1666 && h.reloading && h.reload_done==1666+1500);
        assert(h.check(1100,6)==Why::reloading);
        assert(!h.sync(3165,Kind::pistol,1,6) && h.reloading);
        assert(h.sync(3166,Kind::pistol,1,6) && h.loaded==1 && h.check(3166,6)==Why::ok);
        HandState x;x.sync(0,Kind::crossbow,1,5);x.fired(0,true);assert(x.reload_done==666+900); // crossbow reload 0.9 s
        HandState c;c.sync(0,Kind::pistol,2,9);c.fired(0,true);assert(c.loaded==1 && !c.reloading && c.check(665,8)==Why::cooldown && c.check(666,8)==Why::ok);
        HandState m;m.sync(0,Kind::pistol,1,3);m.fired(0,false);assert(m.loaded==0 && !m.reloading && m.check(700,2)==Why::empty);
        assert(m.reload(700,1,2) && m.reloading && m.reload_done==700+1500 && !m.reload(701,1,2));
        assert(m.sync(2200,Kind::pistol,1,2) && m.loaded==1);
        HandState e;e.sync(0,Kind::pistol,1,0);assert(e.loaded==0 && e.check(0,0)==Why::no_ammo && !e.reload(0,1,0));
        HandState full;full.sync(0,Kind::pistol,1,5);assert(!full.reload(0,1,5)); // already full
        HandState w;w.sync(0,Kind::pistol,1,5);w.fired(0,true);w.sync(10,Kind::crossbow,1,5);assert(w.loaded==1 && !w.reloading && w.check(10,5)==Why::ok); // another weapon starts full
        HandState p;p.sync(0,Kind::pistol,6,6);p.sync(1,Kind::pistol,6,2);assert(p.loaded==2); // the shared reserve fell
    }
    // ---- validation
    {
        const float o[3]{1,2,3},good[9]{1,0,0,0,1,0,0,0,1},left_handed[9]{1,0,0,0,-1,0,0,0,1},skew[9]{1,0,0,1,0,0,0,0,1},nan[9]{std::nanf(""),0,0,0,1,0,0,0,1};
        assert(ranged_fire::validate(0,0,o,good)==Why::ok && ranged_fire::validate(1,1,o,good)==Why::ok);
        assert(ranged_fire::validate(2,0,o,good)==Why::bad_request && ranged_fire::validate(0,2,o,good)==Why::bad_request);
        assert(ranged_fire::validate(0,0,o,left_handed)==Why::bad_frame && ranged_fire::validate(0,0,o,skew)==Why::bad_frame && ranged_fire::validate(0,0,o,nan)==Why::bad_frame);
        const float inf[3]{1,std::numeric_limits<float>::infinity(),3},distant[3]{1e7f,0,0};
        assert(ranged_fire::validate(0,0,inf,good)==Why::bad_frame && ranged_fire::validate(0,0,distant,good)==Why::bad_frame);
        const float yawed[9]{0,1,0,-1,0,0,0,0,1}; // forward +Y, left -X
        assert(ranged_fire::validate(0,0,o,yawed)==Why::ok);
    }
    // ---- queue
    {
        fresh();
        auto bad=shot(0,Fire::Shot::pistol);bad.axis[0]=2;
        assert(!Fire::fire(bad) && Fire::refused_count(Why::bad_frame)==1);
        assert(Fire::fire(shot(0,Fire::Shot::pistol)) && !Fire::fire(shot(0,Fire::Shot::pistol)) && Fire::refused_count(Why::pending)==1); // one pending per hand
        assert(Fire::fire(shot(1,Fire::Shot::pistol)));
        assert(Fire::requests_==4);
        run_queued();
        assert(Fire::fire(shot(0,Fire::Shot::pistol)) == false); // that hand is on cooldown now
        assert(Fire::refused_count(Why::cooldown)==1);
        Fire::reset();
        for(int i=0;i<2;++i) assert(Fire::fire(shot(static_cast<unsigned>(i),Fire::Shot::crossbow)));
        assert(!Fire::reload(2,Fire::Shot::pistol) && Fire::refused_count(Why::bad_request)>=1);
        assert(!Fire::reload(0)); // unknown kind
    }
    // ---- dry run: everything resolved, nothing called, no cooldown taken
    {
        fresh();Fire::dry_run=true;
        assert(Fire::fire(shot(0,Fire::Shot::pistol)));run_queued();
        assert(n_create==0 && n_launch==0 && n_spend==0 && n_sound==0 && Fire::dry_plans_==1 && Fire::fired_==0);
        assert(get_pool(PAMMO_DECL)==7 && Fire::ready(0));
        const auto j=Fire::status_json();assert(has(j,"\"dry_run\":true") && has(j,"create(63b5f0)") && has(j,"pool=7") && has(j,"cap=1"));
        // a dry run still tells why it could not resolve
        build(7,5);M.put<std::uint32_t>(PENT+0xac,99);
        assert(Fire::fire(shot(1,Fire::Shot::pistol)));run_queued();assert(Fire::refused_count(Why::bad_owner)==1);
        // v1113: a refused shot blocks the held trigger for 250 ms
        assert(Fire::held_blocked(1) && !Fire::held_blocked(0));
        g_now+=249;assert(Fire::held_blocked(1));g_now+=1;assert(!Fire::held_blocked(1));
    }
    // ---- a pistol shot, end to end
    {
        fresh();
        auto s=shot(0,Fire::Shot::pistol,5.f);
        assert(Fire::fire(s));run_queued();
        assert((g_calls==std::vector<std::string>{"create","launch","spend","sound"}));
        assert(g_own_in_create && g_own_in_launch && !ranged_fire_own_launch()); // the shot-aim hook is told, then released
        assert(g_origin[0]==5.f && g_origin[1]==2.f && g_axis[0]==1.f && g_axis[4]==1.f && g_axis[8]==1.f && g_speed==1.f && g_launch_comp==PCOMP);
        assert(g_sound_entity==PENT);
        assert(get_pool(PAMMO_DECL)==6 && get_pool(XAMMO_DECL)==5);
        assert(Fire::created_==1 && Fire::launched_==1 && Fire::fired_==1 && Fire::spent_==1 && Fire::ammo_mismatch_==0);
        assert(Fire::pool_before_==7 && Fire::pool_after_==6);
        assert(Fire::comps_total_==3 && Fire::comps_started0_==3);
        assert(M.get<std::int32_t>(PCOMP+0xa0)==1); // the component's own magazine is untouched in per-hand mode
        assert(!Fire::ready(0) && Fire::ready(1));
        // cooldown: no second call before 330 ms (v1094: the fire scene's BREAK_PLAYER_STATE); the other hand fires at once (two pistols)
        g_now=1329;assert(!Fire::fire(shot(0,Fire::Shot::pistol)) && Fire::refused_count(Why::cooldown)==1);
        assert(Fire::fire(shot(1,Fire::Shot::pistol,-5.f)));run_queued();
        assert(Fire::fired_==2 && get_pool(PAMMO_DECL)==5 && g_origin[0]==-5.f);
        // the pistol of hand 0 reloads by itself (330 + 1500)
        g_now=1000+330+1499;assert(!Fire::fire(shot(0,Fire::Shot::pistol)));
        g_now=1000+330+1500;assert(Fire::fire(shot(0,Fire::Shot::pistol)));run_queued();
        assert(Fire::fired_==3 && get_pool(PAMMO_DECL)==4);
    }
    // ---- a crossbow shot: the native bolt speed and flag, the selected bolt's reserve
    {
        fresh();
        assert(Fire::fire(shot(1,Fire::Shot::crossbow)));run_queued();
        assert(g_launch_comp==XCOMP && g_speed==1.5f && n_mark==1 && get_pool(XAMMO_DECL)==4 && get_pool(PAMMO_DECL)==7 && g_sound_entity==XENT);
        g_now+=666+900;assert(Fire::ready(1)); // crossbow reload 0.9 s
        // another bolt type selected natively: comp+0x98 now names it, the shot spends that reserve
        M.put<std::uint64_t>(XAMMO,0xe0000);M.put<std::uint64_t>(XAMMO+0x10,XDEF);
        M.put<std::uint64_t>(RECS+3*0x38ull,0xe0000);
        assert(Fire::fire(shot(1,Fire::Shot::crossbow)));run_queued();assert(get_pool(0xe0000)==3 && Fire::fired_==2);
    }
    // ---- refusals leave the ammo alone
    {
        fresh(0,0);assert(Fire::fire(shot(0,Fire::Shot::pistol)));run_queued();
        assert(Fire::refused_count(Why::no_ammo)==1 && n_create==0);
        fresh();g_fail_create=false;
        build(7,5);M.put<std::uint64_t>(PCOMP+0x10,0); // component without its entity
        assert(Fire::fire(shot(0,Fire::Shot::pistol)));run_queued();assert(Fire::refused_count(Why::no_component)==1);
        build(7,5);M.put<std::uint64_t>(RECS,0xa0000);M.put<std::uint64_t>(RECS+0x10,0); // record without a component: the weapon was never equipped
        assert(Fire::fire(shot(1,Fire::Shot::pistol)));run_queued();assert(Fire::refused_count(Why::no_weapon)==1);
        build(7,5);M.put<std::uint32_t>(PCOMP+0x1c,0xe); // component being torn down
        Fire::reset();assert(Fire::fire(shot(0,Fire::Shot::pistol)));run_queued();assert(Fire::refused_count(Why::not_ready)==1);
        build(7,5);Fire::reset();g_fail_create=false;
        M.put<std::uint64_t>(GAME,0);assert(Fire::fire(shot(0,Fire::Shot::pistol)));run_queued();assert(Fire::refused_count(Why::no_player)==1);
    }
    // ---- v764 (live, the user's chapter-4 save holds 816 inventory records): the weapons are found past 512 records
    {
        fresh();const std::uintptr_t big=RECS+0x100000;
        for(unsigned i=0;i<4;++i) {M.zero(big+(596+i)*0x38ull,0x38);for(unsigned k=0;k<0x28;k+=4) M.put<std::uint32_t>(big+(596+i)*0x38ull+k,M.get<std::uint32_t>(RECS+i*0x38ull+k));}
        for(unsigned i=0;i<596;++i) M.zero(big+i*0x38ull,0x38);
        M.list(INV+0x38,big,600);
        assert(Fire::fire(shot(0,Fire::Shot::pistol)));run_queued();
        assert(Fire::fired_==1 && Fire::refused_count(Why::no_weapon)==0 && get_pool(PAMMO_DECL)==6);
    }
    // ---- a projectile that fails to spawn spends nothing; faults latch the module until reset
    {
        fresh();
        auto create_zero=NAT;create_zero.create=+[](std::uintptr_t,std::uintptr_t,std::uintptr_t& p)->bool {p=0;++n_create;return true;};
        assert(Fire::fire(shot(0,Fire::Shot::pistol)));
        Fire::service_with(BASE,M,create_zero,Test::take());
        assert(Fire::refused_count(Why::spawn_failed)==1 && get_pool(PAMMO_DECL)==7 && Fire::ready(0) && !Test::latched());
        g_fail_create=true;
        for(int i=0;i<2;++i) {g_now+=2000;assert(Fire::fire(shot(0,Fire::Shot::pistol)));run_queued();}
        assert(Fire::refused_count(Why::fault)==2 && Test::latched() && get_pool(PAMMO_DECL)==7);
        g_now+=2000;assert(Fire::fire(shot(0,Fire::Shot::pistol)));run_queued();assert(Fire::refused_count(Why::latched)==1);
        Fire::reset_latch();g_fail_create=false;g_now+=2000;assert(Fire::fire(shot(0,Fire::Shot::pistol)));run_queued();assert(Fire::fired_==1);
    }
    // ---- reload by request, and a hand with an empty magazine and no auto reload
    {
        fresh();Fire::auto_reload=false;
        assert(Fire::fire(shot(0,Fire::Shot::pistol)));run_queued();
        g_now+=700;assert(Fire::fire(shot(0,Fire::Shot::pistol)));run_queued();assert(Fire::refused_count(Why::empty)==1 && Fire::fired_==1);
        assert(Fire::reload(0));run_queued();assert(Fire::reloads_==1 && !Fire::ready(0));
        assert(!Fire::fire(shot(0,Fire::Shot::pistol)) && Fire::refused_count(Why::reloading)==1);
        g_now+=1500;assert(Fire::ready(0) && Fire::fire(shot(0,Fire::Shot::pistol)));run_queued();assert(Fire::fired_==2 && get_pool(PAMMO_DECL)==5);
        assert(Fire::reload(0,Fire::Shot::crossbow)); // a queued reload for a different weapon is just another request
    }
    // ---- v815: the shot's sound and flash belong to the gun in the firing hand (its held copy), not to the record's native gun in the other hand
    {
        fresh();n_flash=0;Fire::muzzle_flash=false;
        auto s=shot(0,Fire::Shot::pistol,5.f);s.entity=XENT;s.entity_id=XID; // any live registered entity stands in for the copy
        assert(Fire::fire(s));run_queued();
        assert(g_sound_entity==XENT && n_flash==0 && Fire::fired_==1);
        fresh();n_flash=0;Fire::muzzle_flash=true;
        s=shot(1,Fire::Shot::pistol,5.f);s.entity=XENT;s.entity_id=XID;
        assert(Fire::fire(s));run_queued();
        assert((g_calls==std::vector<std::string>{"create","launch","spend","sound","flash"}) && g_flash_entity==XENT && g_sound_entity==XENT);
        fresh();n_flash=0; // a stale copy (id no longer registered): back to the record's gun, nothing is written to a dead entity
        s=shot(0,Fire::Shot::pistol,5.f);s.entity=XENT;s.entity_id=XID+0x10000;
        assert(Fire::fire(s));run_queued();
        assert(g_sound_entity==PENT && Fire::fired_==1);
        Fire::muzzle_flash=false;
    }
    // ---- v900 (headset v863: the flash still at the original pistol): 63b3c0 re-poses its FX from the joint behind the handle at comp+0x20 (the native gun),
    // so the swap empties that handle for the call: the FX keeps the muzzle frame. Owner, handle and the copy's pose come back afterwards.
    {
        alignas(16) static unsigned char comp[0x100]{},native_gun[0x340]{},copy[0x340]{};
        const auto c=reinterpret_cast<std::uintptr_t>(comp),copy_at=reinterpret_cast<std::uintptr_t>(copy);
        *reinterpret_cast<std::uintptr_t*>(c+0x10)=reinterpret_cast<std::uintptr_t>(native_gun);*reinterpret_cast<std::int32_t*>(c+0x20)=0x1234;
        const float copy_pos[3]{9,8,7};std::memcpy(copy+0x300,copy_pos,12);
        const float muzzle[3]{1,2,3},axis[9]{0,1,0,-1,0,0,0,0,1};
        constexpr std::uintptr_t rva=0x63b3c0;
        Test::swapped(reinterpret_cast<std::uintptr_t>(&fake_shot_fx)-rva,rva,c,copy_at,muzzle,axis);
        assert(g_seen_owner==copy_at && g_seen_handle==-1 && !std::memcmp(g_seen_pos,muzzle,12));
        assert(*reinterpret_cast<std::uintptr_t*>(c+0x10)==reinterpret_cast<std::uintptr_t>(native_gun) && *reinterpret_cast<std::int32_t*>(c+0x20)==0x1234 && !std::memcmp(copy+0x300,copy_pos,12));
    }
    // ---- sound off, and the native-magazine mode
    {
        fresh();Fire::play_sound=false;
        assert(Fire::fire(shot(0,Fire::Shot::pistol)));run_queued();assert(n_sound==0 && Fire::fired_==1);
        fresh();Fire::shared_native_magazine=true;
        assert(Fire::fire(shot(0,Fire::Shot::pistol)));run_queued();
        assert(n_native_spend==1 && n_spend==0 && M.get<std::int32_t>(PCOMP+0xa0)==0 && get_pool(PAMMO_DECL)==6);
        g_now+=700;assert(Fire::fire(shot(1,Fire::Shot::pistol)));run_queued();assert(Fire::fired_==1 && Fire::refused_count(Why::empty)+Fire::refused_count(Why::reloading)==1); // the one shared round is gone
        g_now+=2200;assert(Fire::fire(shot(0,Fire::Shot::pistol)));run_queued(); // the timer ran out: the native reload refilled the magazine, then the shot
        assert(n_native_reload==1 && Fire::fired_==2 && get_pool(PAMMO_DECL)==5);
    }
    // ---- tracking: components started, moved after 100 ms, ended early
    {
        fresh();
        assert(Fire::fire(shot(0,Fire::Shot::pistol)));run_queued();
        g_now+=120;M.put<float>(g_last_proj+0x300,30.f); // the bullet flew
        service({});assert(Fire::moved_==1 && Fire::stationary_==0 && Fire::comps_started100_==3 && Fire::proj_comp_started100_==1);
        // it hits something: the entity leaves the registry
        M.put<std::uint64_t>(GAME+0x3130b0+10*8,0);g_now+=100;service({});
        assert(Fire::ended_early_==1);
        const auto j=Fire::status_json();assert(has(j,"\"started100\":3") && has(j,"\"moved_100\":29.000") && has(j,"\"live\":false"));
        // a projectile that never starts its components and never moves (the unstarted-spawn risk) shows up as stationary
        fresh();g_projectile_started=false;
        assert(Fire::fire(shot(0,Fire::Shot::pistol)));run_queued();g_now+=150;service({});
        assert(Fire::stationary_==1 && Fire::moved_==0 && Fire::comps_started0_==0 && Fire::comps_started100_==0 && Fire::proj_comp_started100_==0);
        g_now+=5000;service({});assert(Fire::alive_4s_==1);
        const auto k=Fire::status_json();int depth=0;for(char c:k) {if(c=='{'||c=='[') ++depth;if(c=='}'||c==']') --depth;assert(depth>=0);}assert(depth==0 && has(k,"\"alive_4s\":1"));
    }
    // ---- v1101: a wheel choice by the hand that holds a real copy stays on the copy; the native weapon keeps its ammo
    {
        fresh();
        constexpr std::uintptr_t CCOMP=0x2e000000,CCOMP2=0x2e010000,SLEEP_DECL=0x2e100000,SLEEP_DEF=0x2e200000;
        M.put<std::uint64_t>(XDECL+0x188,XAMMO);M.put<std::uint32_t>(XDECL+0x190,2);   // the crossbow's ammo table: entry 0 = the native's bolt, entry 1 = sleep darts
        M.put<std::uint64_t>(XAMMO+0x70,SLEEP_DECL);M.put<std::uint64_t>(XAMMO+0x80,SLEEP_DEF);M.put<std::uint64_t>(SLEEP_DECL,0);M.put<std::uint64_t>(SLEEP_DEF,0);
        M.put<std::uint64_t>(INV+0x70,RECS+2*0x38);   // the crossbow record is in slot 1
        for(const auto c:{CCOMP,CCOMP2}) {M.zero(c,0x200);M.put<std::uint64_t>(c,BASE+0x1b9cbf8);M.put<std::uint64_t>(c+8,XDECL);M.put<std::uint64_t>(c+0x98,XAMMO);}
        Fire::note_copies(-1,1);Fire::set_copy_comp(1,CCOMP);Fire::note_wheel_open(1);
        assert(Fire::wheel_pick(BASE,M,0xa0000,SLEEP_DECL)==0 && Fire::copy_ammo(1,1)==0);                       // another item chosen: left alone
        assert(Fire::wheel_pick(BASE,M,0xb0000,SLEEP_DECL)==XAMMO_DECL && Fire::copy_ammo(1,1)==XAMMO+0x70);    // sleep darts for the copy: the game keeps the native's bolt
        assert(Fire::wheel_pick(BASE,M,0xb0000,0xdead0000)==0 && Fire::copy_ammo(1,1)==XAMMO+0x70);              // not an ammo of this weapon
        Fire::note_wheel_open(0);assert(Fire::wheel_pick(BASE,M,0xb0000,SLEEP_DECL)==0);                           // the hand without a copy: the native's own choice
        Fire::note_wheel_open(1);M.put<std::uint64_t>(INV+0x70,0);
        assert(Fire::wheel_pick(BASE,M,0xb0000,XAMMO_DECL)==0 && Fire::copy_ammo(1,1)==XAMMO);                    // native not in slot 1: recorded, the equip runs as chosen
        M.put<std::uint64_t>(INV+0x70,RECS+2*0x38);
        Fire::set_copy_comp(1,CCOMP);assert(Fire::copy_ammo(1,1)==XAMMO);                                            // the same instance keeps its choice
        Fire::set_copy_comp(1,0);Fire::set_copy_comp(1,CCOMP);assert(Fire::copy_ammo(1,1)==XAMMO);                  // hidden and shown again: kept
        Fire::set_copy_comp(1,CCOMP2);assert(Fire::copy_ammo(1,1)==0);                                               // a new instance starts as the weapon it copies
        Test::track_ammo();assert(Fire::copy_ammo(1,1)==XAMMO && Fire::native_ammo(1)==XAMMO);                       // adopted the native's bolt, and now its own
        M.put<std::uint64_t>(XCOMP+0x98,XAMMO+0x70);g_now+=3000;Test::track_ammo();                                  // the native hand chose sleep darts, no wheel of a copy hand pending
        assert(Fire::native_ammo(1)==XAMMO+0x70 && Fire::copy_ammo(1,1)==XAMMO);                                     // the copy keeps its own
        const auto j=Fire::status_json();int depth=0;for(char c:j) {if(c=='{'||c=='[') ++depth;if(c=='}'||c==']') --depth;}assert(depth==0 && has(j,"\"v1094\":[0,0,0,0,0,0,2]"));
    }
    std::puts("native ranged fire checks ok");
}
