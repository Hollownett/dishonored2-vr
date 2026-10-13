#include "../src/native_takedown_range.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>

static bool read_mem(std::uintptr_t at,void* out,std::size_t n) {std::memcpy(out,reinterpret_cast<const void*>(at),n);return true;}
static int failures=0;
static void expect(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}

int main() {
    using namespace d2vr;
    const std::uintptr_t base=0x140000000;
    alignas(16) static unsigned char sensor[0x40]{},decl[0xa0]{};
    const auto put=[](void* at,std::uintptr_t v) {std::memcpy(at,&v,8);};
    put(sensor+8,reinterpret_cast<std::uintptr_t>(decl));put(decl,base+NativeTakedownRange::decl_vtable_rva);
    const auto set=[&](unsigned i,float v) {std::memcpy(decl+NativeTakedownRange::offsets[i],&v,4);};
    const auto get=[&](unsigned i) {float v;std::memcpy(&v,decl+NativeTakedownRange::offsets[i],4);return v;};
    for(unsigned i=0;i<6;++i) set(i,NativeTakedownRange::shipped[i]);
    auto write=[](std::uintptr_t at,const float& v) {std::memcpy(reinterpret_cast<void*>(at),&v,4);return true;};
    auto read=&read_mem;const auto s=reinterpret_cast<std::uintptr_t>(sensor);
    NativeTakedownRange r;
    expect(r.apply(read,base,s,1.5f,write) && get(0)==4.5f && get(2)==-60.f && get(5)==75.f,"shipped choke gates scaled 1.5x");
    expect(r.apply(read,base,s,1.5f,write) && get(0)==4.5f,"re-apply does not scale twice");
    r.restore(read,write);
    bool shipped=true;for(unsigned i=0;i<6;++i) shipped=shipped && get(i)==NativeTakedownRange::shipped[i];
    expect(shipped && !r.applied(),"stop restores the shipped gates");
    set(0,3.3f);NativeTakedownRange t;
    expect(!t.apply(read,base,s,1.5f,write) && get(0)==3.3f && get(1)==1.7f,"unexpected data is never written");
    set(0,3.f);put(decl,base+0x1234);
    expect(!t.apply(read,base,s,1.5f,write) && get(0)==3.f,"wrong decl type is never written");
    {   // kill rules: GlobalAssassination hash map -> handler -> rules -> float conditions
        alignas(16) static unsigned char global[0x200]{},buckets[16]{},node[0x40]{},handler[0x40]{},rules[24]{},rule[3][16]{},conds[3][16]{},cond[6][0x30]{};
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
    }
    if(!failures) std::printf("PASS: choke gates and kill rules scaled only over the shipped values, once, restored; foreign data untouched\n");
    return failures?1:0;
}
