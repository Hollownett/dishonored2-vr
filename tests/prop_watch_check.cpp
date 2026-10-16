#include "../src/prop_watch.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
// PropWatch against a synthetic game: a registry of entities, three of them movable props (two glasses and a bottle), a table of another class. The cache must
// know every prop in reach of a hand by entity, keep the pose a prop had before it was flown to the camera bind, and cost a bounded number of reads a frame.
namespace {
constexpr std::uintptr_t base=0x140000000ull,game=0x200000000ull;
struct Memory {
    std::map<std::uintptr_t,std::array<unsigned char,4096>> pages;
    void write(std::uintptr_t at,const void* data,std::size_t size) {
        for(std::size_t i=0;i<size;++i) {auto& page=pages[(at+i)&~4095ull];page[(at+i)&4095]=static_cast<const unsigned char*>(data)[i];}
    }
    template<class T> void put(std::uintptr_t at,const T& v) {write(at,&v,sizeof v);}
    bool operator()(std::uintptr_t at,void* out,std::size_t size) const {
        for(std::size_t i=0;i<size;++i) {
            const auto page=pages.find((at+i)&~4095ull);
            static_cast<unsigned char*>(out)[i]=page==pages.end()?0:page->second[(at+i)&4095];
        }
        return true;
    }
};
// An entity: vtable at +0, position at +0x300, model at +0xf0; the model's box at +0x1840 (min xyz, 0, max xyz, 0), origin at +0x1860, axes at +0x186c.
void place(Memory& m,unsigned slot,std::uintptr_t vtable,std::array<float,3> at,std::array<float,3> half) {
    const std::uintptr_t entity=0x300000000ull+slot*0x20000ull,model=entity+0x10000;
    m.put(game+0x3130b0+slot*8,entity);m.put(entity,vtable);m.put(entity+0xf0,model);m.put(entity+0x300,at);
    const std::array<float,8> box{at[0]-half[0],at[1]-half[1],at[2]-half[2],0,at[0]+half[0],at[1]+half[1],at[2]+half[2],0};
    const std::array<float,3> origin{at[0],at[1],at[2]-half[2]};
    const std::array<float,9> axes{1,0,0,0,1,0,0,0,1};
    m.put(model+0x1840,box);m.put(model+0x1860,origin);m.put(model+0x186c,axes);
}
std::uintptr_t entity_of(unsigned slot) {return 0x300000000ull+slot*0x20000ull;}
}
int main() {
    const std::uintptr_t movable=base+0x1c886a8,table=base+0x1dead00;
    Memory m;
    place(m,5,movable,{37.81f,105.53f,13.70f},{.0614f,.0614f,.053f});   // the glass
    place(m,9,movable,{38.07f,105.29f,13.85f},{.0689f,.0689f,.208f});   // the bottle beside it
    place(m,12,table,{37.9f,105.4f,13.2f},{.5f,.4f,.02f});              // a table: not a prop class
    place(m,30,movable,{45.f,110.f,13.7f},{.05f,.05f,.05f});            // a prop nowhere near the hand
    std::uintptr_t held=0;
    const std::array<std::optional<std::array<float,3>>,2> hands{std::nullopt,std::array<float,3>{37.74f,105.48f,14.0f}};
    d2vr::PropWatch watch;
    std::uint64_t now=1000;
    unsigned worst=0;
    // a few frames at 90 Hz: the whole registry is walked (0x3fff slots, 256 a frame), every prop in reach is found
    for(unsigned frame=0;frame<80;++frame,now+=11) {watch.step(m,base,game,hands,now,held);worst=std::max(worst,watch.reads());}
    assert(watch.size()==2);
    const auto glass=watch.find(entity_of(5),now),bottle=watch.find(entity_of(9),now);
    assert(glass && bottle && !watch.find(entity_of(12),now) && !watch.find(entity_of(30),now));
    assert(std::abs(glass->box[0]-(37.81f-.0614f))<1e-4 && std::abs(glass->box[6]-(13.70f+.053f))<1e-4 && glass->axes[8]==1.f);
    assert(std::abs(bottle->box[6]-(13.85f+.208f))<1e-4);
    std::cout<<"worst reads in a frame: "<<worst<<"\n";
    assert(worst<=120);
    // the hand takes the glass: it flies to the camera bind (a jump of 1.2 m), the cache keeps the pose it had
    const auto before=*glass;
    place(m,5,movable,{38.05f,105.45f,14.90f},{.09f,.09f,.075f});
    for(unsigned frame=0;frame<12;++frame,now+=11) watch.step(m,base,game,hands,now,held);
    const auto after=watch.find(entity_of(5),now);
    assert(after && after->frozen && std::abs(after->box[0]-before.box[0])<1e-6 && std::abs(after->box[6]-before.box[6])<1e-6 && std::abs(after->position[2]-before.position[2])<1e-6);
    // ...and it stays that pose, however long it is held (not read again), until it is too old to be what the press touched
    held=entity_of(5);
    for(unsigned frame=0;frame<40;++frame,now+=11) watch.step(m,base,game,hands,now,held);
    assert(watch.find(entity_of(5),now) && std::abs(watch.find(entity_of(5),now)->position[2]-before.position[2])<1e-6);
    assert(!watch.find(entity_of(5),now+2000));
    // a prop pushed along slowly is followed (no jump between two reads)
    for(unsigned frame=0;frame<30;++frame,now+=11) {
        const float x=38.07f+.002f*static_cast<float>(frame);place(m,9,movable,{x,105.29f,13.85f},{.0689f,.0689f,.208f});watch.step(m,base,game,hands,now,held);
    }
    assert(std::abs(watch.find(entity_of(9),now)->position[0]-(38.07f+.002f*29))<.03f && !watch.find(entity_of(9),now)->frozen);
    // a new level: another game object, an empty cache; hands away: nothing is looked at
    d2vr::PropWatch other;
    other.step(m,base,game+0x1000,hands,now);assert(other.size()==0);
    other.step(m,base,game,{std::nullopt,std::nullopt},now);assert(other.size()==0 && other.reads()==0);
    // a prop that comes within reach later is found (the slice reaches its slot again within a cycle)
    place(m,200,movable,{37.5f,105.2f,13.9f},{.04f,.04f,.04f});
    for(unsigned frame=0;frame<80;++frame,now+=11) watch.step(m,base,game,hands,now,held);
    assert(watch.find(entity_of(200),now));
    std::cout<<"prop watch check passed\n";
    return 0;
}
