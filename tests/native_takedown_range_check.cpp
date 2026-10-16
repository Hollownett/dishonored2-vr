#include "../src/native_takedown_range.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>

static constexpr std::uintptr_t image_base=0x140000000;
static std::uintptr_t game_address{},player_address{};
static bool read_mem(std::uintptr_t at,void* out,std::size_t n) {
    if(at==image_base+0x39a5290 && n==8) {std::memcpy(out,&game_address,n);return true;}
    if(at==game_address+0x3130b0 && n==8) {std::memcpy(out,&player_address,n);return true;}
    std::memcpy(out,reinterpret_cast<const void*>(at),n);return true;
}
static int failures=0;
static void expect(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}

int main() {
    using namespace d2vr;
    const std::uintptr_t base=0x140000000;
    alignas(16) static unsigned char sensor[0x40]{},decl[0xa0]{};
    const auto put=[](void* at,std::uintptr_t v) {std::memcpy(at,&v,8);};
    alignas(16) static unsigned char game[8]{},player[0xb0]{};
    game_address=reinterpret_cast<std::uintptr_t>(game);player_address=reinterpret_cast<std::uintptr_t>(player);
    put(game,base+0x1c940a8);put(player,base+0x1c5e428);const std::uint32_t spawn=0x1234;std::memcpy(player+0xa8,&spawn,4);
    put(sensor,base+0x1b3e158);put(sensor+0x10,player_address);
    put(sensor+8,reinterpret_cast<std::uintptr_t>(decl));put(decl,base+NativeTakedownRange::decl_vtable_rva);
    const auto set=[&](unsigned i,float v) {std::memcpy(decl+NativeTakedownRange::offsets[i],&v,4);};
    const auto get=[&](unsigned i) {float v;std::memcpy(&v,decl+NativeTakedownRange::offsets[i],4);return v;};
    for(unsigned i=0;i<6;++i) set(i,NativeTakedownRange::shipped[i]);
    auto write=[](std::uintptr_t at,const float& before,const float& after) {
        std::uint32_t expected{},current{};std::memcpy(&expected,&before,4);std::memcpy(&current,reinterpret_cast<const void*>(at),4);
        if(current!=expected)return false;std::memcpy(reinterpret_cast<void*>(at),&after,4);return true;
    };
    auto read=&read_mem;const auto s=reinterpret_cast<std::uintptr_t>(sensor);
    NativeTakedownRange r;
    expect(r.apply(read,base,s,1.5f,write) && get(0)==4.5f && get(2)==-60.f && get(5)==75.f,"shipped choke gates scaled 1.5x");
    expect(r.apply(read,base,s,1.5f,write) && get(0)==4.5f,"re-apply does not scale twice");
    r.restore(read,write);
    bool shipped=true;for(unsigned i=0;i<6;++i) shipped=shipped && get(i)==NativeTakedownRange::shipped[i];
    expect(shipped && !r.applied(),"stop restores the shipped gates");
    expect(r.apply(read,base,s,1.5f,write),"choke gates applied for failed restoration");
    unsigned restore_writes{};
    auto fail_choke=[&](std::uintptr_t at,const float& before,const float& value) {
        ++restore_writes;
        if(at==reinterpret_cast<std::uintptr_t>(decl)+NativeTakedownRange::offsets[0]) return false;
        return write(at,before,value);
    };
    expect(!r.restore(read,fail_choke) && r.applied() && r.pending() && get(0)==4.5f && get(1)==1.7f,
           "failed choke restoration retains only unresolved owned writes");
    expect(!r.apply(read,base,s,1.5f,write),"partial choke restoration cannot become a fresh applied lease");
    restore_writes=0;
    auto retry_choke=[&](std::uintptr_t at,const float& before,const float& value) {++restore_writes;return write(at,before,value);};
    expect(r.restore(read,retry_choke) && !r.pending() && restore_writes==1 && get(0)==3.f,
           "choke restoration retries only the failed owned float");
    unsigned apply_writes{};
    auto fail_apply_and_rollback=[&](std::uintptr_t at,const float& before,const float& value) {
        ++apply_writes;
        if(apply_writes>1) return false;
        return write(at,before,value);
    };
    expect(!r.apply(read,base,s,1.5f,fail_apply_and_rollback) && r.pending() && get(0)==4.5f,
           "failed apply rollback retains its first successful write");
    expect(r.restore(read,write) && !r.pending() && get(0)==3.f,"failed apply rollback can be retried");
    set(0,3.3f);NativeTakedownRange t;
    expect(!t.apply(read,base,s,1.5f,write) && get(0)==3.3f && get(1)==1.7f,"unexpected data is never written");
    set(0,3.f);put(decl,base+0x1234);
    expect(!t.apply(read,base,s,1.5f,write) && get(0)==3.f,"wrong decl type is never written");
    {   // kill rules: GlobalAssassination hash map -> handler -> rules -> float conditions
        alignas(16) static unsigned char global[0x200]{},buckets[16]{},node[0x40]{},handler[0x40]{},rules[24]{},rule[3][16]{},conds[3][16]{},cond[6][0x60]{};
        put(global,base+0x1b3f998);put(global+0x10,player_address);
        put(global+0x190,reinterpret_cast<std::uintptr_t>(buckets));const std::int32_t nb=2;std::memcpy(global+0x198,&nb,4);
        put(buckets,reinterpret_cast<std::uintptr_t>(node));put(node+0x28,reinterpret_cast<std::uintptr_t>(handler));
        put(handler+0x20,reinterpret_cast<std::uintptr_t>(rules));const std::uint32_t three=3,two=2;std::memcpy(handler+0x28,&three,4);
        const float vals[6]{1.7f,3.f, 3.f,10.f, .5f,2.f};const std::int32_t ops[6]{4,4, 4,4, 5,5};
        for(int q=0;q<3;++q) {put(rules+q*8,reinterpret_cast<std::uintptr_t>(rule[q]));put(rule[q],reinterpret_cast<std::uintptr_t>(conds[q]));std::memcpy(rule[q]+8,&two,4);
            for(int c=0;c<2;++c) {auto* k=cond[q*2+c];put(conds[q]+c*8,reinterpret_cast<std::uintptr_t>(k));put(k,base+NativeTakedownRange::float_condition_vtable_rva);
                std::memcpy(k+0x28,&ops[q*2+c],4);std::memcpy(k+0x2c,&vals[q*2+c],4);}}
        const auto f=[&](int i) {float v;std::memcpy(&v,cond[i]+0x2c,4);return v;};
        NativeTakedownRange k;const auto g=reinterpret_cast<std::uintptr_t>(global);
        expect(k.apply_kill(read,base,g,1.5f,write)==2 && std::fabs(f(0)-2.55f)<1e-4f && f(1)==4.5f,"kill rule {<=1.7,<=3} scaled");
        expect(f(2)==3.f && f(3)==10.f && f(4)==.5f && f(5)==2.f,"other rules untouched");
        expect(k.apply_kill(read,base,g,1.5f,write)==2 && f(1)==4.5f,"applied once");
        k.restore(read,write);expect(f(0)==1.7f && f(1)==3.f,"kill rules restored");
        expect(k.apply_kill(read,base,g,1.5f,write)==2,"kill rules applied for failed restoration");
        unsigned kill_writes{};
        auto fail_kill=[&](std::uintptr_t at,const float& before,const float& value) {
            ++kill_writes;
            if(at==reinterpret_cast<std::uintptr_t>(cond[0])+0x2c) return false;
            return write(at,before,value);
        };
        expect(!k.restore(read,fail_kill) && k.pending() && std::fabs(f(0)-2.55f)<1e-4f && f(1)==3.f,
               "failed kill restoration retains the unresolved condition");
        kill_writes=0;
        auto retry_kill=[&](std::uintptr_t at,const float& before,const float& value) {++kill_writes;return write(at,before,value);};
        expect(k.restore(read,retry_kill) && !k.pending() && kill_writes==1 && f(0)==1.7f,
               "kill restoration retries only its failed condition");
        expect(k.apply_kill(read,base,g,1.5f,write)==2,"kill rules applied for validation-to-writer recycling");
        const auto raced_vtable=base+0x1c521b8;
        auto recycle_during_write=[&](std::uintptr_t at,const float& before,const float& value) {
            if(at==reinterpret_cast<std::uintptr_t>(cond[0])+0x2c)put(cond[0]+0x28,raced_vtable);
            return write(at,before,value);
        };
        expect(!k.restore(read,recycle_during_write) && k.pending(),"CAS failure after validation remains pending");
        std::uintptr_t raced_after{};std::memcpy(&raced_after,cond[0]+0x28,8);
        expect(raced_after==raced_vtable && f(1)==3.f,"expected float CAS never stomps a recycled vtable high word");
        expect(k.restore(read,write) && !k.pending(),"recycled condition is retired on the next owned validation");
        std::memcpy(cond[0]+0x28,&ops[0],4);std::memcpy(cond[0]+0x2c,&vals[0],4);
        expect(k.apply_kill(read,base,g,1.5f,write)==2,"kill rules reapplied for detached-condition regression");
        put(conds[0],reinterpret_cast<std::uintptr_t>(cond[2]));
        k.restore(read,write);expect(std::fabs(f(0)-2.55f)<1e-4f && f(1)==3.f,"same type and scaled value detached from current owner map is not restored");
        put(conds[0],reinterpret_cast<std::uintptr_t>(cond[0]));std::memcpy(cond[0]+0x2c,&vals[0],4);
        expect(k.apply_kill(read,base,g,1.5f,write)==2,"kill rules reapplied for foreign-value regression");
        const float foreign=9.f;std::memcpy(cond[0]+0x2c,&foreign,4);
        k.restore(read,write);expect(f(0)==foreign && f(1)==3.f,"restore leaves another writer's value intact");
        std::memcpy(cond[0]+0x2c,&vals[0],4);
        expect(k.apply_kill(read,base,g,1.5f,write)==2,"kill rules reapplied for recycled-memory regression");
        const auto lod_vtable=base+0x1c521b8;put(cond[0]+0x28,lod_vtable);
        k.restore(read,write);std::uintptr_t after{};std::memcpy(&after,cond[0]+0x28,8);
        expect(after==lod_vtable,"recycled component vtable high half is never overwritten with 1.7");
        std::memcpy(cond[0]+0x28,&ops[0],4);std::memcpy(cond[0]+0x2c,&vals[0],4);
        expect(k.apply_kill(read,base,g,1.5f,write)==2,"kill rules reapplied for owner-generation regression");
        const std::int32_t next_generation=1;std::memcpy(global+0x18,&next_generation,4);
        k.restore(read,write);expect(std::fabs(f(0)-2.55f)<1e-4f && f(1)==4.5f,"same type and scaled values with changed sensor identity are not restored");
        std::memcpy(cond[0]+0x2c,&vals[0],4);std::memcpy(cond[1]+0x2c,&vals[1],4);
        expect(k.apply_kill(read,base,g,1.5f,write)==2,"new sensor generation can own fresh patches");
        const std::uint32_t next_spawn=spawn+1;std::memcpy(player+0xa8,&next_spawn,4);
        expect(k.apply_kill(read,base,g,1.5f,write)==0,"same sensor address with a new player identity drops old patches");
        expect(std::fabs(f(0)-2.55f)<1e-4f && f(1)==4.5f,"changed owner spawn never receives stale restoration");
        std::memcpy(player+0xa8,&spawn,4);
    }
    put(decl,base+NativeTakedownRange::decl_vtable_rva);
    expect(t.apply(read,base,s,1.5f,write),"choke gates reapplied for owner regression");
    const std::int32_t next_generation=1;std::memcpy(sensor+0x18,&next_generation,4);
    expect(!t.apply(read,base,s,1.5f,write) && !t.applied(),"changed choke owner drops stale applied telemetry without restoring");
    t.restore(read,write);expect(get(0)==4.5f && std::fabs(get(1)-2.55f)<1e-4f,"changed choke sensor generation is never restored");
    if(!failures) std::printf("PASS: choke gates and kill rules scaled only over the shipped values, once, restored; foreign data untouched\n");
    return failures?1:0;
}
