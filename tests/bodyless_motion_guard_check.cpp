// v1296: BodylessMotionGuard::doomed over fake engine memory (the 2026-11-13 crash shape and the cases that must pass through).
#include "../src/bodyless_motion_guard.hpp"
#include <cassert>
#include <cstdio>
#include <map>
static std::map<std::uintptr_t,unsigned char> mem;
static void put(std::uintptr_t a,const void* v,std::size_t n){for(std::size_t i=0;i<n;++i)mem[a+i]=static_cast<const unsigned char*>(v)[i];}
static bool rd(std::uintptr_t a,void* o,std::size_t n){for(std::size_t i=0;i<n;++i){auto f=mem.find(a+i);if(f==mem.end())return false;static_cast<unsigned char*>(o)[i]=f->second;}return true;}
static void system_at(std::uintptr_t s,std::uint32_t flags,std::int32_t count,std::initializer_list<std::uint32_t> ids){
    std::uintptr_t table=s+0x1000,list=s+0x2000;put(s+0x1c,&flags,4);put(s+0x138,&count,4);put(s+0x100,&table,8);put(table+0x28,&list,8);
    std::uint32_t i=0;for(auto id:ids){put(list+4*i,&id,4);++i;}}
int main(){
    using G=d2vr::BodylessMotionGuard;const std::uint16_t all=0xfffe;
    system_at(0x10000,0x8,2,{0xffffffu,0x3ffffffu});            // crash shape: every body slot dead (high byte is flags)
    assert(G::doomed(rd,0x10000,all,all));
    assert(!G::doomed(rd,0x10000,3,all));                        // a named bone: not this path
    system_at(0x20000,0x8,3,{0xffffff,0x1234,0xffffff});         // one live body: engine finds it
    assert(!G::doomed(rd,0x20000,all,all));
    system_at(0x30000,0x4,2,{0xffffff,0xffffff});                // flag test fails: engine skips the switch itself
    assert(!G::doomed(rd,0x30000,all,all));
    system_at(0x40000,0xc,0,{});                                 // no bodies at all
    assert(G::doomed(rd,0x40000,all,all));
    assert(!G::doomed(rd,0x50000,all,all));                      // unreadable: unchanged behaviour
    assert(G::forbidden_body(0) && G::forbidden_body(0x5000000) && G::forbidden_body(0xffffff) && G::forbidden_body(0xffffffff));   // world body / invalid id (high byte = flags)
    assert(!G::forbidden_body(1) && !G::forbidden_body(0x123));
    // v1384: the 2026-11-16 hang shape. world+0x28 -> pool (0xb0 per body, own id at +0x70). Slot 3 holds trigger 0x01000003; a caller
    // still holding the previous level's 0x3c000003 must not switch it (the engine indexes by the low 24 bits and links the stale id).
    const std::uintptr_t world=0x60000,pool=0x70000;put(world+0x28,&pool,8);
    const std::uint32_t live=0x01000003,freed=0x02ffffff;put(pool+3*0xb0+0x70,&live,4);put(pool+5*0xb0+0x70,&freed,4);
    assert(G::wrong_body(rd,world,0x3c000003));                  // same slot, other id: the 0x3c000003 data constant read as an id
    assert(!G::wrong_body(rd,world,0x01000003));                 // the body itself
    assert(G::wrong_body(rd,world,0x01000005));                  // a free slot (0x02ffffff): no body to link (v1386)
    assert(G::wrong_body(rd,world,0) && G::wrong_body(rd,world,0xffffff));   // world body / invalid id
    assert(!G::wrong_body(rd,world,0x01000007));                 // unreadable slot: unchanged behaviour
    assert(!G::wrong_body(rd,0x80000,0x3c000003));               // unreadable world
    // v1385: 0xc90a90(world, motion) walks the motion's body ring (motion +0x28 first, body +0x74 next) and reads every body's +0x88.
    // Crash 2026-11-16 10:18 (0xcaf005): a ring body with +0x88 null. A ring that never closes would spin like the 09:07 hang.
    const std::uintptr_t w2=0x90000,p2=0xa0000,m2=0xb0000;put(w2+0x28,&p2,8);put(w2+0x120,&m2,8);
    auto body=[&](std::uint32_t slot,std::uint32_t id,std::uint32_t next,std::uint64_t props){put(p2+slot*0xb0+0x70,&id,4);put(p2+slot*0xb0+0x74,&next,4);put(p2+slot*0xb0+0x88,&props,8);};
    auto motion=[&](std::uint32_t m,std::uint32_t first){put(m2+m*0x80+0x28,&first,4);};
    body(4,0x01000004,0x01000004,0x1234);motion(1,0x01000004);   // a lone live body
    assert(!G::dead_motion(rd,w2,1));
    body(5,0x01000005,0x01000006,0x1234);body(6,0x01000006,0x01000005,0x1234);motion(2,0x01000005);   // a live two-body ring
    assert(!G::dead_motion(rd,w2,2));
    body(7,0x01000007,0x01000007,0);motion(3,0x01000007);        // the crash shape: +0x88 null
    assert(G::dead_motion(rd,w2,3));
    motion(4,0x02000004);                                         // a stale first id (slot 4 holds 0x01000004)
    assert(G::dead_motion(rd,w2,4));
    body(8,0x01000008,0x01000009,0x1234);body(9,0x01000009,0x0100000a,0);motion(5,0x01000008);   // dead body later in the ring
    assert(G::dead_motion(rd,w2,5));
    body(10,0x0100000a,0x0100000b,0x1234);body(11,0x0100000b,0x0100000b,0x1234);motion(6,0x0100000a);   // ring never returns to its first
    assert(G::dead_motion(rd,w2,6));
    motion(7,0x00ffffff);                                         // no body: the engine's own early exit, pass through
    assert(!G::dead_motion(rd,w2,7));
    assert(!G::dead_motion(rd,w2,9));                             // unreadable motion: unchanged behaviour
    assert(!G::dead_motion(rd,0xc0000,1));                        // unreadable world
    // v1386: detach 0xc927c0 ring repair. The hang shape: body slot 3 (id 0x01000003, motion 0x916) whose ring link and motion first say 0x3c000003.
    using R=G::Ring;
    const std::uintptr_t w3=0xd0000,p3=0xe0000,m3=0xf0000;put(w3+0x28,&p3,8);put(w3+0x120,&m3,8);
    std::map<std::uintptr_t,std::uint32_t> writes;
    auto wr=[&](std::uintptr_t a,std::uint32_t v){writes[a]=v;put(a,&v,4);return true;};
    auto b3=[&](std::uint32_t slot,std::uint32_t id,std::uint32_t next,std::uint32_t motion){put(p3+slot*0xb0+0x70,&id,4);put(p3+slot*0xb0+0x74,&next,4);put(p3+slot*0xb0+0x40,&motion,4);};
    auto m3f=[&](std::uint32_t m,std::uint32_t first){put(m3+m*0x80+0x28,&first,4);};
    b3(3,0x01000003,0x3c000003,0x916);m3f(0x916,0x3c000003);
    assert(G::repair_ring(rd,wr,w3,p3+3*0xb0)==R::repaired);
    std::uint32_t v{};rd(p3+3*0xb0+0x74,&v,4);assert(v==0x01000003);rd(m3+0x916*0x80+0x28,&v,4);assert(v==0x01000003);
    assert(G::repair_ring(rd,wr,w3,p3+3*0xb0)==R::ok);           // a second detach sees a clean ring
    b3(4,0x01000004,0x01000005,0x20);b3(5,0x01000005,0x01000004,0x20);m3f(0x20,0x01000004);writes.clear();   // a healthy two-body ring
    assert(G::repair_ring(rd,wr,w3,p3+4*0xb0)==R::ok && writes.empty());
    b3(6,0x01000006,0x01000007,0x21);b3(7,0x01000007,0x01000007,0x21);m3f(0x21,0x01000006);   // ring leads away and never returns
    assert(G::repair_ring(rd,wr,w3,p3+6*0xb0)==R::broken);
    assert(G::repair_ring(rd,wr,0x110000,p3+3*0xb0)==R::unreadable);
    std::puts("bodyless_motion_guard_check ok");return 0;}
