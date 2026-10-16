#define NOMINMAX
#include "../src/native_lean_lifecycle.hpp"
#include <cassert>
#include <map>
#include <iostream>
#include <thread>
#include <sstream>
using namespace d2vr;
namespace d2vr {
struct NativeLeanLifecycleFixture {
    static bool invoke_fault(NativeLeanLifecycle& trace,const NativeLeanQueryEvent& event) {
        __try {trace.queue_call(&event,[] {RaiseException(0xe1370546,EXCEPTION_NONCONTINUABLE,0,nullptr);});}
        __except(GetExceptionCode()==0xe1370546?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return true;}
        return false;
    }
    static void check_queue_scope() {
        NativeLeanLifecycle trace;trace.active_=true;NativeLeanQueryEvent event{};
        NativeLeanLifecycle::Seed outer{&trace,&event};NativeLeanLifecycle::seed_=&outer;
        unsigned calls{};
        const auto result=trace.queue_call(&event,[&] {
            ++calls;assert(NativeLeanLifecycle::seed_!=&outer && NativeLeanLifecycle::seed_->event==&event);
            const auto* nested_parent=NativeLeanLifecycle::seed_;
            trace.queue_call(nullptr,[] {assert(!NativeLeanLifecycle::seed_);});
            assert(NativeLeanLifecycle::seed_==nested_parent);return std::uintptr_t{0x1234};
        });
        assert(result==0x1234 && calls==1 && NativeLeanLifecycle::seed_==&outer);
        bool propagated{};
        try {trace.queue_call(&event,[&] {if(trace.active_)throw 71;});}catch(int error) {propagated=error==71;}
        assert(propagated && NativeLeanLifecycle::seed_==&outer);
        assert(invoke_fault(trace,event) && NativeLeanLifecycle::seed_==&outer);
        trace.active_=false;
        trace.queue_call(&event,[] {assert(!NativeLeanLifecycle::seed_);});
        assert(NativeLeanLifecycle::seed_==&outer);NativeLeanLifecycle::seed_=nullptr;
    }
};
}
static constexpr std::uintptr_t fixture_base=0x140000000,fixture_game=0x200000,fixture_player=0x300000,fixture_machine=0x400000,fixture_sensor=0x500000,fixture_decl=0x600000,fixture_shape=0x700000,fixture_world=0x800000;
static std::map<std::uintptr_t,std::byte> memory;
static std::array<void*,9> hooks{};static std::array<unsigned,9> calls{};
static std::uintptr_t first{},second{};static bool corrupt{},throw_original{};
static bool forbid_retired_owner{},nested_original{},nested_throw{},native_destructor{},expect_nested{true};static NativeLeanLifecycle* nested_trace{};
static void simulate_nested_original();
static bool forbid_old_resources{};static std::uintptr_t forbidden_slot{};
template<class T>static void put(std::uintptr_t p,T value){const auto* b=reinterpret_cast<const std::byte*>(&value);for(std::size_t i=0;i<sizeof(T);++i)memory[p+i]=b[i];}
static void bytes(std::uintptr_t p,const void* v,std::size_t n){const auto* b=static_cast<const std::byte*>(v);for(std::size_t i=0;i<n;++i)memory[p+i]=b[i];}
static void zeros(std::uintptr_t p,std::size_t n){for(std::size_t i=0;i<n;++i)memory[p+i]=std::byte{};}
static bool read(std::uintptr_t p,void* v,std::size_t n){
    if(forbid_retired_owner)assert(!(p>=fixture_player && p<fixture_player+0x1000) && !(p>=fixture_machine && p<fixture_machine+0x1000) && !(p>=fixture_sensor && p<fixture_sensor+0x1000));
    if(forbid_old_resources)assert(p!=fixture_world && !(p>=forbidden_slot && p<forbidden_slot+0x140));
    for(unsigned i=0;i<9;++i)if(p==fixture_base+NativeLeanLifecycle::rvas[i] && n==32){auto* b=static_cast<unsigned char*>(v);const auto* s=NativeLeanLifecycle::signatures[i];
        for(unsigned j=0;j<32;++j){char part[]{s[j*2],s[j*2+1],0};b[j]=static_cast<unsigned char>(std::strtoul(part,nullptr,16));}if(corrupt)b[0]^=1;return true;}
    auto* b=static_cast<std::byte*>(v);for(std::size_t i=0;i<n;++i){const auto it=memory.find(p+i);if(it==memory.end())return false;b[i]=it->second;}return true;
}
template<unsigned I>static void __fastcall pair_original(std::uintptr_t a,std::uintptr_t b){assert(a==first && b==second);++calls[I];if(throw_original)throw 7;}
static std::uintptr_t __fastcall destroy_original(std::uintptr_t a,std::uint64_t b){assert(a==first && b==second);++calls[3];return a+17;}
template<unsigned I>static void __fastcall unary_original(std::uintptr_t a){assert(a==first);++calls[I];if constexpr(I==7 || I==8)if(nested_original)simulate_nested_original();}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID at,LPVOID hook,LPVOID* original){
    const std::array<void*,9> originals{reinterpret_cast<void*>(&pair_original<0>),reinterpret_cast<void*>(&pair_original<1>),reinterpret_cast<void*>(&pair_original<2>),reinterpret_cast<void*>(&destroy_original),reinterpret_cast<void*>(&pair_original<4>),reinterpret_cast<void*>(&unary_original<5>),reinterpret_cast<void*>(&unary_original<6>),reinterpret_cast<void*>(&unary_original<7>),reinterpret_cast<void*>(&unary_original<8>)};
    for(unsigned i=0;i<9;++i)if(reinterpret_cast<std::uintptr_t>(at)==fixture_base+NativeLeanLifecycle::rvas[i]){hooks[i]=hook;*original=originals[i];return MH_OK;}assert(false);return MH_ERROR_NOT_EXECUTABLE;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID){return MH_OK;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID){return MH_OK;}
static NativeLeanQueryEvent fixture(){
    zeros(fixture_sensor,0x88);zeros(fixture_machine,0x28);zeros(fixture_decl+0x70,24);
    put(fixture_base+0x39a5290,fixture_game);put(fixture_game,fixture_base+0x1c940a8);put(fixture_game+0x3130b0,fixture_player);put(fixture_player,fixture_base+0x1c5e428);
    put(fixture_sensor,fixture_base+0x1b40648);put(fixture_sensor+8,fixture_decl);put(fixture_sensor+0x10,fixture_player);put(fixture_sensor+0x18,rig::Identity{1,10});put(fixture_sensor+0x20,fixture_machine);
    put(fixture_machine,fixture_base+0x1c73d58);put(fixture_machine+0x10,fixture_player);put(fixture_machine+0x18,rig::Identity{2,10});put(fixture_sensor+0x38,static_cast<unsigned char>(1));put(fixture_sensor+0x40,fixture_shape);
    put(fixture_game+0x60280,fixture_base+0x1c80090);put(fixture_game+0x60410,fixture_world);put(fixture_shape,fixture_base+0x1ec1ab8);put(fixture_world,fixture_base+0x1ad4f38);
    put(fixture_decl+0x70,.05f);put(fixture_decl+0x74,.2f);put(fixture_decl+0x78,.1f);put(fixture_decl+0x7c,.8f);put(fixture_decl+0x84,0x802bu);
    auto r=&read;const auto state=native_lean_query_snapshot(r,fixture_base,fixture_sensor);assert(state);
    NativeLeanQueryEvent e{};e.state=*state;e.id_pointer=0x900000;e.mask=state->mask;e.flags=0x40;std::memcpy(e.request.data()+0x28,&fixture_shape,8);return e;
}
static std::uintptr_t initialize(const NativeLeanQueryEvent& e,std::uint32_t token){const auto at=head_query_slot(e.state.manager,token);zeros(at,0x140);put(at+0x88,token);put(at+0x8c,static_cast<unsigned char>(4));put(at+0x8d,static_cast<unsigned char>(1));put(at+0x80,e.mask);put(at+0x84,e.flags);bytes(at+0x90,e.request.data(),112);bytes(at+0x100,e.axes.data(),48);return at;}
static void destroyed(std::uintptr_t at,std::uint32_t token){put(at+0x88,std::uint32_t(token+0x2000));put(at,std::uintptr_t{0});put(at+8,0U);}
static void retire_fixture_owner(){
    for(auto it=memory.begin();it!=memory.end();) {
        const auto p=it->first;
        if((p>=fixture_player && p<fixture_player+0x1000) || (p>=fixture_machine && p<fixture_machine+0x1000) || (p>=fixture_sensor && p<fixture_sensor+0x1000))it=memory.erase(it);else ++it;
    }
    forbid_retired_owner=true;
}
static void simulate_nested_original(){
    // Real installed GameLocal hook has put its accepted entry copy into TLS.
    if(native_destructor && expect_nested){
        // TLS parent cannot authorize a different thread. Global replacement makes
        // that thread take the strict standalone rejection before old actor reads.
        std::thread other([]{assert(!nested_trace->before_boundary(fixture_game+0x60280,fixture_base+0x8a4bb2,2).admitted);});other.join();
    }
    retire_fixture_owner();
    const auto expected_caller=fixture_base+(native_destructor?0x8a4bb2:0x8ab13f);
    auto t=nested_trace->before_boundary(fixture_game+0x60280,expected_caller,2);
    if(!expect_nested){assert(!t.admitted);return;}assert(t.admitted);
    assert(!nested_trace->before_boundary(fixture_game+0x60280,fixture_base+(native_destructor?0x8ab13f:0x8a4bb2),2).admitted);
    assert(!nested_trace->before_boundary(fixture_game+0x60280,fixture_base+0x8ab13e,2).admitted);
    assert(!nested_trace->before_boundary(fixture_game+0x60281,expected_caller,2).admitted);
    // Nested invalid scope masks the parent, then restores it without falling back to retired reads.
    auto invalid=nested_trace->before_boundary(0,0,1);
    {NativeLeanLifecycle::GameShutdownScope scope(*nested_trace,invalid);assert(!nested_trace->before_boundary(fixture_game+0x60280,expected_caller,2).admitted);}
    nested_trace->after_boundary(t);
    // A changed manager world revokes the nested match immediately.
    put(fixture_game+0x60410,std::uintptr_t{0});assert(!nested_trace->before_boundary(fixture_game+0x60280,expected_caller,2).admitted);
    if(nested_throw)throw 13;
}
int main(){
    NativeLeanLifecycleFixture::check_queue_scope();
    auto e=fixture();NativeLeanLifecycle trace;corrupt=true;bool rejected=false;try{trace.install(fixture_base,&read,L"build/lean-lifecycle-invalid.csv");}catch(...){rejected=true;}assert(rejected);assert(trace.disable());corrupt=false;
    trace.install(fixture_base,&read,L"build/lean-lifecycle-check.csv");
    // Installed callbacks preserve arguments, original count and the native return even when unregistered.
    first=0x123456;second=0x987654;
    for(unsigned i:{0U,1U,2U,4U})reinterpret_cast<void(__fastcall*)(std::uintptr_t,std::uintptr_t)>(hooks[i])(first,second);
    assert(reinterpret_cast<std::uintptr_t(__fastcall*)(std::uintptr_t,std::uint64_t)>(hooks[3])(first,second)==first+17);
    for(unsigned i:{5U,6U,7U,8U})reinterpret_cast<void(__fastcall*)(std::uintptr_t)>(hooks[i])(first);for(auto n:calls)assert(n==1);
    throw_original=true;try{reinterpret_cast<void(__fastcall*)(std::uintptr_t,std::uintptr_t)>(hooks[2])(first,second);assert(false);}catch(int v){assert(v==7);}throw_original=false;
    auto at=initialize(e,0x2005);assert(!trace.before_publish(e,e.state.manager+0x200,at,fixture_base+0x95601));
    assert(trace.before_publish(e,e.state.manager+0x200,at,fixture_base+0x95600));assert(!trace.before_publish(e,e.state.manager+0x200,at,fixture_base+0x95600));
    auto worker=trace.before_worker(e.state.manager,at,fixture_base+0x999ad);assert(worker);put(at+0x8e,static_cast<unsigned char>(2));
    auto cleanup=trace.before_cleanup(e.state.manager,at,fixture_base+0x926b0);assert(cleanup);destroyed(at,0x2005);trace.after_destroy(cleanup,at,at);
    // Simulate immediate slot reuse BEFORE the worker wrapper returns. No post-return native reads allowed.
    memory.erase(at+0x88);memory.erase(e.state.manager+0x190);const auto before=trace.rejected();trace.after_worker(worker);assert(trace.rejected()==before);
    put(e.state.manager+0x190,fixture_world);at=initialize(e,0x6005);assert(trace.before_publish(e,e.state.manager+0x200,at,fixture_base+0x95600));
    put(e.id_pointer,0x6005U);const auto cancelled=trace.before_cancel(e.state.manager,e.id_pointer,fixture_base+0x3b943e);assert(cancelled);
    put(e.id_pointer,0xffffffffU);trace.after_cancel(cancelled,e.id_pointer);put(at+0x8e,static_cast<unsigned char>(1));
    cleanup=trace.before_cleanup(e.state.manager,at,fixture_base+0x999a6);destroyed(at,0x6005);trace.after_destroy(cleanup,at,at);
    at=initialize(e,0x8006);assert(trace.before_publish(e,e.state.manager+0x200,at,fixture_base+0x95600));
    cleanup=trace.before_cleanup(e.state.manager,at,fixture_base+0x97763);destroyed(at,0x8006);const auto rejects=trace.rejected();trace.after_destroy(cleanup,at,at);assert(trace.rejected()==rejects+1);
    // An owned join is sampled as a coherent pair; all return paths use copied full keys.
    const auto join=trace.before_boundary(e.state.manager,fixture_base+0x8af900,0,1000);assert(join.admitted);
    assert(!trace.before_boundary(e.state.manager,0,0,1499).admitted);
    trace.after_boundary(join);
    auto rollback=trace.before_boundary(e.state.manager,0,0,999);assert(rollback.admitted);trace.after_boundary(rollback);
    auto stale=trace.rejected();put(fixture_game+0x3130b0,std::uintptr_t{0});
    assert(!trace.before_boundary(e.state.manager,0,0,1500).admitted && trace.rejected()==stale+1);
    put(fixture_game+0x3130b0,fixture_player);
    // Fill ordinary capacity. Boundary rows remain available and pair reservations survive nesting.
    at=initialize(e,0xa007);assert(trace.before_publish(e,e.state.manager+0x200,at,fixture_base+0x95600));
    auto repeated=trace.before_worker(e.state.manager,at,fixture_base+0x999ad);assert(repeated);
    for(unsigned i=0;i<300;++i)trace.after_worker(repeated);
    assert(trace.rows()==192 && trace.dropped()>0);
    for(unsigned i=0;i<16;++i){auto t=trace.before_boundary(e.state.manager,0,0,2000+i*500);assert(t.admitted);trace.after_boundary(t);}
    assert(trace.rows()==224);assert(!trace.before_boundary(e.state.manager,0,0,20000).admitted);
    auto game_end=trace.before_boundary(fixture_game,fixture_base+0x123,1);assert(game_end.admitted);
    auto world_end=trace.before_boundary(e.state.manager,fixture_base+0x8534ce,2);assert(world_end.admitted);
    // Free manager, owner and slot BEFORE returns; no post-original reads are permitted.
    memory.clear();forbidden_slot=at;forbid_old_resources=true;
    trace.after_boundary(world_end);trace.after_boundary(game_end);
    assert(trace.rows()==228);
    {std::ifstream pending(L"build/lean-lifecycle-check.csv");std::string contents((std::istreambuf_iterator<char>(pending)),{});assert(contents.find("issued")==std::string::npos);}
    assert(trace.disable());assert(trace.disable());
    std::ifstream csv(L"build/lean-lifecycle-check.csv");std::string contents((std::istreambuf_iterator<char>(csv)),{});
    assert(contents.find("retirement_deferred")!=std::string::npos && contents.find("retired_deferred")!=std::string::npos && contents.find("retirement_unproven")!=std::string::npos);
    assert(contents.find("join_return")!=std::string::npos && contents.find("game_shutdown_return")!=std::string::npos && contents.find("world_shutdown_return")!=std::string::npos);
    trace.install(fixture_base,&read,L"build/lean-lifecycle-reset.csv");assert(trace.rows()==0 && trace.dropped()==0 && trace.rejected()==0 && trace.overflow()==0);
    forbid_old_resources=false;e=fixture();for(unsigned i=0;i<8;++i){at=initialize(e,0xa010+i);assert(trace.before_publish(e,e.state.manager+0x200,at,fixture_base+0x95600));}
    at=initialize(e,0xa020);assert(!trace.before_publish(e,e.state.manager+0x200,at,fixture_base+0x95600));assert(trace.overflow()==1);assert(trace.disable());
    trace.install(fixture_base,&read,L"build/lean-lifecycle-nested.csv");e=fixture();at=initialize(e,0xc007);
    assert(trace.before_publish(e,e.state.manager+0x200,at,fixture_base+0x95600));
    nested_trace=&trace;nested_original=true;first=fixture_game;const auto original_calls=calls[7];
    reinterpret_cast<void(__fastcall*)(std::uintptr_t)>(hooks[7])(first);assert(calls[7]==original_calls+1);
    // Recreate the same addresses and exercise original exception unwinding/TLS restoration.
    forbid_retired_owner=false;e=fixture();nested_throw=true;
    // Actor is already gone and current-game changed BEFORE GameLocal shutdown.
    retire_fixture_owner();put(fixture_base+0x39a5290,std::uintptr_t{0});
    try{reinterpret_cast<void(__fastcall*)(std::uintptr_t)>(hooks[7])(first);assert(false);}catch(int v){assert(v==13);}
    assert(calls[7]==original_calls+2);nested_throw=nested_original=false;forbid_retired_owner=false;
    e=fixture();
    const auto wrong_vt=fixture_base+0x123;put(e.state.manager,wrong_vt);
    assert(!trace.before_boundary(fixture_game,0,1).admitted);
    put(e.state.manager,fixture_base+0x1c80090);
    put(e.state.manager+0x190,std::uintptr_t{0});assert(!trace.before_boundary(fixture_game,0,1).admitted);
    put(e.state.manager+0x190,fixture_world);
    auto standalone=trace.before_boundary(e.state.manager,0,2);assert(standalone.admitted);trace.after_boundary(standalone);
    assert(trace.disable());
    {std::ifstream f(L"build/lean-lifecycle-nested.csv");std::string c((std::istreambuf_iterator<char>(f)),{});assert(c.find("game_shutdown_unwind")!=std::string::npos);
        assert(c.find(",1,2,1,"+std::to_string(fixture_base+0x1c80090)+","+std::to_string(wrong_vt)+"\n")!=std::string::npos);
        assert(c.find(",1,3,1,"+std::to_string(fixture_world)+",0\n")!=std::string::npos);}
    trace.install(fixture_base,&read,L"build/lean-lifecycle-destructor.csv");e=fixture();at=initialize(e,0xe007);
    assert(trace.before_publish(e,e.state.manager+0x200,at,fixture_base+0x95600));
    // Global already changed, but old actor registry/typed graph still live before original.
    put(fixture_base+0x39a5290,std::uintptr_t{0});native_destructor=nested_original=true;
    const auto destructor_calls=calls[8];first=fixture_game;
    reinterpret_cast<void(__fastcall*)(std::uintptr_t)>(hooks[8])(first);assert(calls[8]==destructor_calls+1);
    forbid_retired_owner=false;e=fixture();put(fixture_base+0x39a5290,std::uintptr_t{0});nested_throw=true;
    try{reinterpret_cast<void(__fastcall*)(std::uintptr_t)>(hooks[8])(first);assert(false);}catch(int v){assert(v==13);}
    assert(calls[8]==destructor_calls+2);nested_throw=false;forbid_retired_owner=false;
    // Missing old actor at ENTRY is rejected before attempting retired actor storage.
    e=fixture();retire_fixture_owner();put(fixture_game+0x3130b0,std::uintptr_t{0});expect_nested=false;
    reinterpret_cast<void(__fastcall*)(std::uintptr_t)>(hooks[8])(first);assert(calls[8]==destructor_calls+3);
    expect_nested=true;native_destructor=nested_original=forbid_retired_owner=false;e=fixture();
    auto resumed=trace.before_boundary(e.state.manager,0,2);assert(resumed.admitted);trace.after_boundary(resumed);
    assert(trace.disable());
    {std::ifstream f(L"build/lean-lifecycle-destructor.csv");std::string c((std::istreambuf_iterator<char>(f)),{});
        assert(c.find("destructor_begin")!=std::string::npos && c.find("destructor_return")!=std::string::npos && c.find("destructor_unwind")!=std::string::npos);
        std::istringstream rows(c);std::string line;
        while(std::getline(rows,line)){std::istringstream row(line);std::vector<std::string> fields;std::string field;while(std::getline(row,field,','))fields.push_back(field);
            if(fields.size()<28)continue;
            if(fields[3]=="destructor_begin")assert(fields[23]=="0");
            if(fields[3]=="destructor_return" || fields[3]=="destructor_unwind" ||
                (fields[3]=="world_shutdown_begin" && fields[4]==std::to_string(fixture_base+0x8a4bb2)))assert(fields[23]=="1");}
        assert(c.find(",0,5,1,"+std::to_string(fixture_player)+",0\n")!=std::string::npos);}
    std::cout<<"PASS: native Lean lifecycle admission, retirement races, default-off integration and original-once bounded observers\n";
}

