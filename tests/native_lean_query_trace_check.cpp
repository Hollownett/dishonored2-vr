#define NOMINMAX
#include "../src/native_interaction_aim.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <sstream>
static constexpr std::uintptr_t fixture_base=0x140000000,fixture_game=0x200000,fixture_player=0x300000,fixture_machine=0x400000,fixture_sensor=0x500000,fixture_decl=0x600000,fixture_shape=0x700000,fixture_world=0x800000;
static std::map<std::uintptr_t,std::byte> memory;
static bool race{},queue_in_stage{};static unsigned native_stages{},native_queues{};
static std::array<void*,10> detours{};
static alignas(16) std::array<std::byte,112> request;
static alignas(16) d2vr::InteractionRay axes{};
static int issued_id=-1;
template<class T> static void put(std::uintptr_t at,T value) {
    const auto* bytes=reinterpret_cast<const std::byte*>(&value);for(std::size_t i=0;i<sizeof(T);++i)memory[at+i]=bytes[i];
}
static void fill(std::uintptr_t at,std::size_t size) {for(std::size_t i=0;i<size;++i)memory[at+i]=std::byte{};}
static bool read(std::uintptr_t at,void* out,std::size_t size) {
    if(at==fixture_base+0x417820 && size==8){const unsigned char code[]{0x48,0x8d,0x81,0xd8,0,0,0,0xc3};std::memcpy(out,code,8);return true;}
    if(at==fixture_base+0x3b80a0 && size==32){const unsigned char code[]{0x48,0x8b,0xc4,0x48,0x89,0x78,0x20,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0xa0,0,0,0,0x4d,0x8b,0xe1,0x4d,0x8b,0xf0,0x48,0x8b,0xfa,0x4c,0x8b,0xf9};std::memcpy(out,code,32);return true;}
    if(at==fixture_base+0x3b81d3 && size==5){const unsigned char code[]{0xe8,0x48,0xf6,0x05,0};std::memcpy(out,code,5);return true;}
    constexpr std::array<std::uintptr_t,9> signature_rvas{0x3b8430,0x3b8d60,0x3b9650,0x6c6760,0x95280,0x95550,0x92630,0x3a37f0,0x3b7b90};
    constexpr std::array<const char*,9> signatures{"488bc4555356574154415541564157488da898feffff4881ec2802000048c745","488bc455488da838ffffff4881ecc0010000807938000f847603000048895818","488bc45556488da838ffffff4881ecb801000080793800488bf10f8428040000","48895c24185556574881ecb0000000803900498bf9498bf0488bea488bd97507","48895c241855415641574883ec404883b990010000004d8bf14d8bf8488bda48","48895c241855415641574883ec404883b990010000004d8bf14d8bf8488bda48","488954241048894c240857415541564883ec508b024c8db180820000498bf844","44884c242044894424188954241048894c2408535556574881ec880000004863","488bc4554154415541564157488da8f8faffff4881ece005000048c745e0feff"};
    if(size==32)for(unsigned i=0;i<9;++i)if(at==fixture_base+signature_rvas[i]) {
        auto* bytes=static_cast<unsigned char*>(out);for(unsigned j=0;j<32;++j) {char part[]{signatures[i][j*2],signatures[i][j*2+1],0};bytes[j]=static_cast<unsigned char>(std::strtoul(part,nullptr,16));}return true;
    }
    if(at==reinterpret_cast<std::uintptr_t>(request.data()) && size==112) {std::memcpy(out,request.data(),size);return true;}
    if(at==reinterpret_cast<std::uintptr_t>(&axes) && size==48) {std::memcpy(out,&axes,size);return true;}
    if(at==reinterpret_cast<std::uintptr_t>(&issued_id) && size==4) {std::memcpy(out,&issued_id,size);return true;}
    auto* bytes=static_cast<std::byte*>(out);
    for(std::size_t i=0;i<size;++i) {const auto found=memory.find(at+i);if(found==memory.end())return false;bytes[i]=found->second;}
    if(race && at==fixture_decl+0x70)put(fixture_sensor+0x40,fixture_shape+0x1000);
    return true;
}
static void __fastcall stage_original(void* at) {
    assert(reinterpret_cast<std::uintptr_t>(at)==fixture_sensor);++native_stages;
    if(queue_in_stage)reinterpret_cast<void*(__fastcall*)(void*,void*,void*,const d2vr::InteractionRay*,UINT,UINT)>(detours[5])(
        reinterpret_cast<void*>(fixture_game+0x60280),&issued_id,request.data(),&axes,0x802b,0x40);
}
static std::uintptr_t __fastcall direct_original(void*,const d2vr::InteractionRay*,void*,void*,void*) {return 0;}
static void* __fastcall queue_original(void* manager,void* id,void* filter,const d2vr::InteractionRay* frame,UINT mask,UINT flags) {
    assert(reinterpret_cast<std::uintptr_t>(manager)==fixture_game+0x60280 && id==&issued_id && filter==request.data() && frame==&axes && mask==0x802b && flags==0x40);
    ++native_queues;issued_id=17;return id;
}
static unsigned native_fetches{},native_sorts{},native_occlusions{};
static std::uintptr_t __fastcall occlusion_original(void*,void*,const void*,const void*,const void*,unsigned,unsigned char* output) {++native_occlusions;*output=0;return 0x123401;}
static std::uintptr_t __fastcall fetch_original(void*,void*,void*) {++native_fetches;return 0x123400;}
static void __fastcall sort_original(void*,unsigned,int,unsigned char) {++native_sorts;}
static const float* __fastcall range_origin_original(void* node){return reinterpret_cast<const float*>(reinterpret_cast<std::uintptr_t>(node)+0xd8);}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID at,LPVOID detour,LPVOID* original) {
    if(reinterpret_cast<std::uintptr_t>(at)==fixture_base+0x417820){detours[9]=detour;*original=reinterpret_cast<void*>(&range_origin_original);return MH_OK;}
    constexpr std::array<std::uintptr_t,9> rvas{0x3b8430,0x3b8d60,0x3b9650,0x6c6760,0x95280,0x95550,0x92630,0x3a37f0,0x3b7b90};
    for(unsigned i=0;i<9;++i)if(reinterpret_cast<std::uintptr_t>(at)==fixture_base+rvas[i]) {
        detours[i]=detour;*original=i<3?reinterpret_cast<void*>(&stage_original):i==3?reinterpret_cast<void*>(&direct_original):i<6?reinterpret_cast<void*>(&queue_original):i==6?reinterpret_cast<void*>(&fetch_original):i==7?reinterpret_cast<void*>(&sort_original):reinterpret_cast<void*>(&occlusion_original);return MH_OK;
    }
    assert(false);return MH_ERROR_NOT_EXECUTABLE;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {return MH_OK;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {return MH_OK;}
static UINT logic() {return 2;}
static void fixtures() {
    fill(fixture_sensor,0x88);fill(fixture_machine,0x28);fill(fixture_decl+0x70,24);
    put(fixture_base+0x39a5290,fixture_game);put(fixture_game,fixture_base+0x1c940a8);put(fixture_game+0x3130b0,fixture_player);put(fixture_player,fixture_base+0x1c5e428);
    put(fixture_sensor,fixture_base+0x1b40648);put(fixture_sensor+8,fixture_decl);put(fixture_sensor+0x10,fixture_player);put(fixture_sensor+0x18,d2vr::rig::Identity{1,10});put(fixture_sensor+0x20,fixture_machine);
    put(fixture_machine,fixture_base+0x1c73d58);put(fixture_machine+0x10,fixture_player);put(fixture_machine+0x18,d2vr::rig::Identity{2,10});
    put(fixture_sensor+0x38,static_cast<unsigned char>(1));put(fixture_sensor+0x40,fixture_shape);put(fixture_game+0x60410,fixture_world);put(fixture_shape,fixture_base+0x1ec1ab8);put(fixture_world,fixture_base+0x1ad4f38);
    put(fixture_decl+0x70,.05f);put(fixture_decl+0x74,.2f);put(fixture_decl+0x78,.1f);put(fixture_decl+0x7c,.8f);put(fixture_decl+0x80,0xdeadbeefu);put(fixture_decl+0x84,0x802bu);
    put(fixture_sensor+0x78,-1);put(fixture_sensor+0x7c,-1);put(fixture_sensor+0x80,1.f);put(fixture_sensor+0x84,1.f);
    request={};std::memcpy(request.data()+0x28,&fixture_shape,8);
}
int main() {
    fixtures();auto reader=&read;
    const auto s=d2vr::native_lean_query_snapshot(reader,fixture_base,fixture_sensor);assert(s && s->mask==0x802b && s->enabled);
    d2vr::NativeLeanQueryEvent event{};
    assert(d2vr::native_lean_query_arguments(reader,*s,fixture_base+0x3b9962,fixture_base,s->manager,reinterpret_cast<std::uintptr_t>(request.data()),reinterpret_cast<std::uintptr_t>(&axes),s->mask,0x40,event));
    for(unsigned bad=0;bad<4;++bad) assert(!d2vr::native_lean_query_arguments(reader,*s,fixture_base+(bad==0?0x3b9963:0x3b9962),fixture_base,s->manager+(bad==1?8:0),reinterpret_cast<std::uintptr_t>(request.data()),reinterpret_cast<std::uintptr_t>(&axes),s->mask+(bad==2?1:0),bad==3?0:0x40,event));
    auto changed=*s;changed.shape+=8;assert(!d2vr::same_lean_resources(*s,changed));changed=*s;++changed.sensor_identity.generation;assert(!d2vr::same_lean_resources(*s,changed));
    race=true;assert(!d2vr::native_lean_query_snapshot(reader,fixture_base,fixture_sensor));race=false;fixtures();
    put(fixture_sensor+0x18,d2vr::rig::Identity{1,0});assert(!d2vr::native_lean_query_snapshot(reader,fixture_base,fixture_sensor));fixtures();
    unsigned calls=0;d2vr::passive_lean_call([] {throw 1;},[&] {++calls;},[] {throw 2;});assert(calls==1);
    const auto before_request=request;const auto before_axes=axes;
    auto* result=d2vr::passive_lean_call([] {throw 1;},[&] {return queue_original(reinterpret_cast<void*>(s->manager),&issued_id,request.data(),&axes,s->mask,0x40);},[](void*) {throw 2;});
    assert(result==&issued_id && native_queues==1 && issued_id==17 && request==before_request && std::memcmp(&axes,&before_axes,48)==0);
    d2vr::NativeLeanSamplingPolicy policy;
    assert(policy.select(*s,*s,1000,false)==1);
    auto moved=*s;moved.origin[0]=3;moved.ids={17,18};moved.limits={.5f,.5f};
    assert(policy.select(moved,moved,1499,false)==0);assert(policy.select(moved,moved,1500,false)==16);
    assert(policy.select(moved,moved,1501,false)==0);assert(policy.select(moved,moved,9000,false)==16);
    moved.enabled=false;assert(policy.select(moved,moved,9001,false)&4);
    auto owner_changed=moved;++owner_changed.sensor_identity.generation;
    assert(policy.select(owner_changed,owner_changed,9002,false)&2);
    assert(policy.select(owner_changed,owner_changed,8000,false)&32);
    assert(policy.select(owner_changed,owner_changed,8001,true)&8);
    assert(policy.select(owner_changed,owner_changed,8002,true)==0);
    assert(policy.select(owner_changed,owner_changed,8003,false)&8);
    policy.reset();assert(policy.select(*s,*s,1,false)==1);
    auto transaction=[&](bool queries) {
        d2vr::NativeLeanQueryTransaction tx;event={};event.state=*s;tx.push(event,1);
        if(queries)for(unsigned i=0;i<2;++i) {event.phase=2;event.caller=fixture_base+(i?0x3b9a49:0x3b9962);tx.push(event,2);event.phase=3;tx.push(event,3);}
        event.phase=1;tx.push(event,4);return tx;
    };
    d2vr::NativeLeanQueryTrace trace;trace.open(L"build/hands-lean-trace-check.csv");
    const auto tx=transaction(true);std::uint64_t clock=1000;auto now=[&] {return clock;};
    trace.commit(tx,now);assert(trace.rows()==6 && trace.transactions()==1);
    for(unsigned i=1;i<500;++i) {clock=1000+i;trace.commit(tx,now);}
    assert(trace.rows()==6 && trace.skipped()==499);
    for(unsigned i=1;i<42;++i) {clock=1000+i*500;trace.commit(tx,now);}
    assert(trace.rows()==252);clock+=500;trace.commit(tx,now);assert(trace.rows()==252 && trace.dropped()==6);
    const auto short_stage=transaction(false);clock+=500;trace.commit(short_stage,now);clock+=500;trace.commit(short_stage,now);
    assert(trace.rows()==256 && trace.transactions()==44 && trace.close());
    // Missing queue-after or a buffer overflow must never emit a partial stage.
    trace.reset();trace.open(L"build/hands-lean-trace-lifecycle-check.csv");
    auto incomplete=short_stage;incomplete.events[1].event.phase=3;trace.commit(incomplete,now);assert(trace.rows()==0 && trace.rejected()==1);
    auto overflow=tx;for(unsigned i=0;i<4;++i)overflow.push(event,5);trace.commit(overflow,now);assert(trace.rows()==0 && trace.rejected()==2);
    clock=1;trace.commit(tx,now);assert(trace.rows()==6 && trace.transactions()==1 && trace.skipped()==0 && trace.close());
    trace.reject();trace.queue();assert(trace.rejected()==3 && trace.queues()==1);
    // Actual hook-stage default/opt-in checks use pinned instruction fixtures below.
    d2vr::NativeInteractionAim observer;
    observer.install(fixture_base,read,logic,true,L"build/hands-lean-interaction-check.csv");
    assert(reinterpret_cast<std::uintptr_t(__fastcall*)(void*,void*,void*)>(detours[6])(nullptr,nullptr,nullptr)==0x123400);
    reinterpret_cast<void(__fastcall*)(void*,unsigned,int,unsigned char)>(detours[7])(nullptr,0,0,0);
    unsigned char obstruction_flag=9;
    assert(reinterpret_cast<std::uintptr_t(__fastcall*)(void*,void*,const void*,const void*,const void*,unsigned,unsigned char*)>(detours[8])(nullptr,nullptr,nullptr,nullptr,nullptr,0,&obstruction_flag)==0x123401);
    assert(native_fetches==1 && native_sorts==1 && native_occlusions==1 && obstruction_flag==0 && observer.ranked_results()==0 && !observer.target_side(GetTickCount64()));
    reinterpret_cast<void(__fastcall*)(void*)>(detours[2])(reinterpret_cast<void*>(fixture_sensor));
    assert(native_stages==1 && observer.lean_rows()==0 && observer.routed()==0 && observer.disable());
    observer.install(fixture_base,read,logic,true,L"build/hands-lean-interaction-check.csv",true);queue_in_stage=true;
    reinterpret_cast<void(__fastcall*)(void*)>(detours[2])(reinterpret_cast<void*>(fixture_sensor));
    assert(native_stages==2 && native_queues==2 && observer.lean_rows()==2 && observer.routed()==0);
    assert(request==before_request && std::memcmp(&axes,&before_axes,48)==0);
    put(fixture_sensor+0x18,d2vr::rig::Identity{1,0});
    reinterpret_cast<void(__fastcall*)(void*)>(detours[2])(reinterpret_cast<void*>(fixture_sensor));
    assert(native_stages==3 && observer.lean_rejected()==1 && observer.disable());
    std::cout<<"PASS: typed Lean ownership, shape/mask/caller rejection, read races, default-off actual stage passthrough, original-once byte-identical calls and bounded change trace\n";
}
