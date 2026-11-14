// v1318 check of the held-part batch tag (src/part_batch_tag.hpp): the parm given to a model is one EVERY surface's material has with the
// same bits (so the shader reads the material's value whichever of model or material wins), with a normal component to tag; none when the
// materials disagree or hold only zeros/denormals; the tag changes only the lowest mantissa bits (< 8e-6 relative); models seen one after
// another get different tags, and a re-seen model after eviction a new one.
//   Build/run: build\tmp-check-run.cmd part_batch_tag_check /DNOMINMAX
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <set>
#include "../src/part_batch_tag.hpp"
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
static float f(std::uint32_t b) {float x;std::memcpy(&x,&b,4);return x;}
static std::uint32_t bits(float x) {std::uint32_t b;std::memcpy(&b,&x,4);return b;}
static d2vr::part_batch::Parm parm(std::uint16_t id,float a,float b,float c,float d) {d2vr::part_batch::Parm p;p.id=id;p.value.c[0]=bits(a);p.value.c[1]=bits(b);p.value.c[2]=bits(c);p.value.c[3]=bits(d);return p;}
int main() {
    using namespace d2vr::part_batch;
    Parm out{};unsigned comp=9;
    // one surface: the first parm with a normal component (zeros are skipped: a tagged zero would be a denormal)
    CHECK(choose({{parm(7,0,0,0,0),parm(9,0,.5f,0,0)}},out,comp)==9 && comp==1);
    // two surfaces: id 9 differs in value on the second, id 12 is the same on both
    CHECK(choose({{parm(9,0,.5f,0,0),parm(12,1,1,1,1)},{parm(12,1,1,1,1),parm(9,0,.25f,0,0)}},out,comp)==12 && comp==0);
    CHECK(choose({{parm(9,.5f,0,0,0)},{parm(12,1,1,1,1)}},out,comp)==-1);   // nothing in both
    CHECK(choose({{parm(3,0,0,0,0)}},out,comp)==-1);                          // only zeros
    CHECK(choose({},out,comp)==-1);
    // the tag: one component, low bits only
    const Vec4 v=parm(12,1,.75f,-2,123).value;
    double worst=0;
    for(std::uint32_t tag=1;tag<=max_tag;++tag) for(unsigned c=0;c<4;++c) {
        const Vec4 t=tagged(v,c,tag);
        for(unsigned k=0;k<4;++k) if(k!=c) CHECK(t.c[k]==v.c[k]);
        CHECK(t.c[c]!=v.c[c]);
        worst=std::max(worst,static_cast<double>(std::abs(f(t.c[c])-f(v.c[c]))/std::abs(f(v.c[c]))));
    }
    // per-model tags from the counter, eviction gives a new one
    std::array<Entry,4> t{};std::uint32_t counter=0;std::set<std::uint32_t> tags;
    for(unsigned m=0;m<4;++m) tags.insert(entry(t,counter,0x100+m,m+1).tag);
    CHECK(tags.size()==4 && !tags.count(0u));
    CHECK(entry(t,counter,0x100,9).tag==*tags.begin());                       // the same model keeps its tag
    const std::uint32_t evicted_tag=entry(t,counter,0x101,10).tag;
    entry(t,counter,0x200,11);entry(t,counter,0x201,12);entry(t,counter,0x202,13);entry(t,counter,0x203,14);   // evicts everything
    CHECK(entry(t,counter,0x101,15).tag!=evicted_tag);
    std::printf("part batch tag: worst relative change %.2e\n",worst);
    CHECK(worst<8e-6);
    std::printf("%s\n",failures?"FAILED":"ok");
    return failures?1:0;
}
