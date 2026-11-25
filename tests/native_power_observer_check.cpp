// Offline check for d2vr::read_powers against a fake sparse process memory.
#include "../src/native_power_observer.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>

namespace {
void require(bool c,const char* m) {if(!c) {std::printf("FAIL: %s\n",m);std::exit(1);}}
using uptr=std::uintptr_t;
constexpr uptr base=0x7ff600000000ull;
struct Mem {
    std::map<uptr,std::uint8_t> bytes;
    uptr fail_at{};
    void put(uptr at,const void* src,std::size_t n) {const auto* p=static_cast<const std::uint8_t*>(src);for(std::size_t i=0;i<n;++i) bytes[at+i]=p[i];}
    template<class T> void set(uptr at,T v) {put(at,&v,sizeof(v));}
    bool operator()(uptr at,void* dst,std::size_t n) {
        auto* o=static_cast<std::uint8_t*>(dst);
        for(std::size_t i=0;i<n;++i) {
            if(fail_at && at+i==fail_at) return false;
            const auto it=bytes.find(at+i);if(it==bytes.end()) return false;o[i]=it->second;
        }
        return true;
    }
};
constexpr uptr game=0x100000,player=0x200000,list=0x300000,machine=0x400000,pool=0x410000;
Mem world(const std::vector<uptr>& components) {
    Mem m;
    m.set<uptr>(base+0x39a5290,game);m.set<uptr>(game,base+0x1c940a8);m.set<uptr>(game+0x3130b0,player);m.set<uptr>(player,base+0x1c5e428);
    m.set<uptr>(player+0x268,list);m.set<std::uint32_t>(player+0x268+8,static_cast<std::uint32_t>(components.size()));
    m.set<std::uint32_t>(player+0x268+12,static_cast<std::uint32_t>(components.size()+4)<<8);
    for(std::size_t i=0;i<components.size();++i) m.set<uptr>(list+i*8,components[i]);
    return m;
}
void component(Mem& m,uptr at,uptr vtable) {m.set<uptr>(at,base+vtable);m.set<std::uint8_t>(at+0xa4,0);m.set<std::uint8_t>(at+0xa5,0);}
d2vr::PowerVtables vt() {
    auto v=d2vr::PowerVtables::current();
    for(auto& e:v.entries) if(e.kind==d2vr::PowerKind::displace) e.vtable=0x1c44a18;else if(e.kind==d2vr::PowerKind::foresight) e.vtable=0x1c451a8;
        else if(e.kind==d2vr::PowerKind::semblance) e.vtable=0x1c44518;
    v.state_ids={41,63,73,93,94,96,102,26};
    return v;
}
} // namespace

int main() {
    const auto v=vt();
    {   // no game
        Mem m;auto r=[&](uptr a,void* d,std::size_t n){return m(a,d,n);};
        const auto s=d2vr::read_powers(r,base,v);
        require(!s.found_player && s.power_count==0 && s.theeye_state==-1 && !s.aim_active,"no game gives defaults");
    }
    {   // Far Reach aiming, unknown component ignored
        auto m=world({0x500000,0x501000});
        component(m,0x500000,0x1b52818);m.set<std::uint8_t>(0x500000+0xa4,1);
        m.set<uptr>(0x501000,base+0x12345);
        auto r=[&](uptr a,void* d,std::size_t n){return m(a,d,n);};
        const auto s=d2vr::read_powers(r,base,v);
        require(s.found_player && s.power_count==1,"one power found");
        require(s.aim_active && s.aiming_kind==static_cast<std::uint32_t>(d2vr::PowerKind::far_reach),"far reach aim");
        require(!s.cast_flag && !s.swapper_present && s.theeye_state==-1,"others default");
    }
    {   // Swapper aiming with a target, then a sentinel
        auto m=world({0x500000});
        component(m,0x500000,0x1c44a18);m.set<std::uint8_t>(0x500000+0xa4,1);m.set<std::uint8_t>(0x500000+0x259,1);
        m.set<uptr>(0x500000+0xc8,0x600000);m.set<float>(0x600000+0x148,1.5f);m.set<float>(0x600000+0x14c,-2.f);m.set<float>(0x600000+0x150,3.f);
        auto r=[&](uptr a,void* d,std::size_t n){return m(a,d,n);};
        auto s=d2vr::read_powers(r,base,v);
        require(s.swapper_present && s.swapper_aiming && s.swapper_mode==1,"swapper aim");
        require(s.swapper_has_target && s.swapper_target[0]==1.5f && s.swapper_target[1]==-2.f && s.swapper_target[2]==3.f,"swapper target");
        require(s.aiming_kind==static_cast<std::uint32_t>(d2vr::PowerKind::displace),"displace kind");
        m.set<float>(0x600000+0x148,3.4e38f);
        s=d2vr::read_powers(r,base,v);
        require(s.swapper_aiming && !s.swapper_has_target,"sentinel means no target");
        m.set<float>(0x600000+0x148,std::numeric_limits<float>::quiet_NaN());
        s=d2vr::read_powers(r,base,v);
        require(!s.swapper_has_target,"nan means no target");
    }
    {   // powers_found_in_inventory: no power on the player; the state machine's inventory (machine+0x328) holds a Swapper record
        // (records of 0x38 bytes, the item's component at +0x10), seen once even if the player's list also holds it
        auto m=world({0x500000});
        component(m,0x500000,0x1c44a18);m.set<std::uint8_t>(0x500000+0xa4,1);
        constexpr uptr inventory=0x700000,records=0x710000;
        m.set<uptr>(machine+0x328,inventory);m.set<uptr>(inventory+0x38,records);
        m.set<std::uint32_t>(inventory+0x38+8,3u);m.set<std::uint32_t>(inventory+0x38+12,8u<<8);
        m.set<uptr>(records+0x10,0x500000);           // the same Swapper component as the player's list
        m.set<uptr>(records+0x38+0x10,0x520000);component(m,0x520000,0x1b9bb78);   // Blink, only in the inventory
        m.set<uptr>(records+0x70+0x10,0);                // an empty record
        auto r=[&](uptr a,void* d,std::size_t n){return m(a,d,n);};
        const auto s=d2vr::read_powers(r,base,v,machine);
        require(s.power_count==2 && s.swapper_present && s.swapper_aiming,"inventory powers, no double count");
        const auto none=d2vr::read_powers(r,base,v);   // no machine: the inventory is not read
        require(none.power_count==1,"inventory needs the machine");
    }
    {   // TheEye and Mimic
        auto m=world({0x500000,0x510000});
        component(m,0x500000,0x1c451a8);m.set<std::int32_t>(0x500000+0x19c,4);m.set<float>(0x500000+0x358,7.f);m.set<float>(0x500000+0x35c,8.f);m.set<float>(0x500000+0x360,9.f);m.set<float>(0x500000+0x364,0.5f);
        component(m,0x510000,0x1c44518);m.set<std::uint8_t>(0x510000+0x120,1);
        auto r=[&](uptr a,void* d,std::size_t n){return m(a,d,n);};
        const auto s=d2vr::read_powers(r,base,v);
        require(s.theeye_state==4 && s.theeye_body[0]==7.f && s.theeye_body_yaw==0.5f,"theeye");
        require(s.mimic_present && s.mimic_armed && s.power_count==2,"mimic");
    }
    {   // player states: choke active, UsePower inactive, others absent from the pool
        auto m=world({});
        m.set<uptr>(machine,base+0x1c73d58);m.set<uptr>(machine+0x90,pool);
        for(unsigned id:{41u,63u}) {
            const uptr st=0x420000+id*0x100;
            m.set<uptr>(pool+0x10+id*8,st);m.set<std::uint32_t>(st+8,id);m.set<uptr>(st+0x10,machine);m.set<std::uint8_t>(st+0x20,id==41?1:0);
        }
        auto r=[&](uptr a,void* d,std::size_t n){return m(a,d,n);};
        const auto s=d2vr::read_powers(r,base,v,machine);
        require((s.states&d2vr::power_state_bit::choke) && !(s.states&d2vr::power_state_bit::use_power),"state bits");
        require(s.states_known==(d2vr::power_state_bit::choke|d2vr::power_state_bit::use_power),"known bits");
    }
    {   // Void Strike: the Slash state (id 39, vtable 0x1d0b978) read from the pool: owned byte +0xdb, hold time +0x38, decl (+0x18) commit +0xa0, mana +0xf0
        auto m=world({});
        m.set<uptr>(machine,base+0x1c73d58);m.set<uptr>(machine+0x90,pool);
        constexpr uptr slash=0x430000,decl=0x440000;
        m.set<uptr>(pool+0x10+39*8,slash);m.set<uptr>(slash,base+0x1d0b978);m.set<std::uint32_t>(slash+8,39);m.set<uptr>(slash+0x10,machine);
        m.set<std::uint8_t>(slash+0x20,0);m.set<uptr>(slash+0x18,decl);m.set<std::uint8_t>(slash+0xdb,1);m.set<float>(slash+0x38,0.25f);
        m.set<float>(decl+0xa0,0.75f);m.set<std::int32_t>(decl+0xf0,40);
        auto r=[&](uptr a,void* d,std::size_t n){return m(a,d,n);};
        auto vs=v;vs.slash_vtable=0x1d0b978;
        auto s=d2vr::read_powers(r,base,vs,machine);
        require(s.voidstrike_found && s.voidstrike_owned && s.voidstrike_commit_s==0.75f && s.voidstrike_mana==40 && s.slash_hold_s==0.25f,"void strike fields");
        require(d2vr::NativePowerObserver::status_json().find("\"void_strike\":")!=std::string::npos,"void strike status key");
        d2vr::NativePowerObserver::publish(s);
        const auto js=d2vr::NativePowerObserver::status_json();
        require(js.find("\"void_strike\":{\"found\":true,\"owned\":true,\"commit_s\":0.75,\"mana\":40,\"hold_s\":0.25,\"presses\":0,\"last_press_ms\":0,\"commits\":0}")!=std::string::npos,"void strike status values");
        d2vr::NativePowerObserver::note_void_strike_press(860);d2vr::NativePowerObserver::note_void_strike_press(900);
        require(d2vr::NativePowerObserver::status_json().find("\"presses\":2,\"last_press_ms\":900")!=std::string::npos,"press counter");
        s=d2vr::read_powers(r,base,v,machine);   // trait off (vtable 0): never read
        require(!s.voidstrike_found && !s.voidstrike_owned && s.voidstrike_mana==-1,"trait off leaves defaults");
        m.set<uptr>(slash,base+0x12345);          // wrong vtable: ignored
        s=d2vr::read_powers(r,base,vs,machine);
        require(!s.voidstrike_found && !s.voidstrike_owned,"wrong vtable ignored");
        m.set<uptr>(slash,base+0x1d0b978);m.set<float>(decl+0xa0,std::numeric_limits<float>::quiet_NaN());
        s=d2vr::read_powers(r,base,vs,machine);
        require(s.voidstrike_found && !(s.voidstrike_commit_s>0.f),"nan commit time is not a usable time");
    }
    {   // a read failure in the middle gives defaults for the rest, no crash
        auto m=world({0x500000});
        component(m,0x500000,0x1c44a18);m.set<std::uint8_t>(0x500000+0xa4,1);
        m.set<uptr>(0x500000+0xc8,0x600000);m.set<float>(0x600000+0x148,1.f);m.set<float>(0x600000+0x14c,1.f);m.set<float>(0x600000+0x150,1.f);
        m.fail_at=0x600000+0x14c;
        auto r=[&](uptr a,void* d,std::size_t n){return m(a,d,n);};
        const auto s=d2vr::read_powers(r,base,v);
        require(s.found_player && s.swapper_aiming && !s.swapper_has_target,"read failure yields no target");
        m.fail_at=player;
        const auto s2=d2vr::read_powers(r,base,v);
        require(!s2.found_player,"player read failure");
    }
    {   // publish / latest / status
        d2vr::PowerSnapshot s;s.found_player=true;s.theeye_state=3;
        d2vr::NativePowerObserver::publish(s);
        require(d2vr::NativePowerObserver::latest().theeye_state==3,"publish");
        require(d2vr::NativePowerObserver::status_json().find("\"state\":3")!=std::string::npos,"status");
    }
    std::puts("native_power_observer_check: ok");
    return 0;
}
