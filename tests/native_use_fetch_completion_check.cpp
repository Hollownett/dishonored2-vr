#define NOMINMAX
#include "../src/native_interaction_aim.hpp"
#include <cassert>
#include <map>
#include <memory>
#include <iostream>
namespace {
constexpr std::uintptr_t fixture_base=0x140000000,fixture_game=0x2000000,fixture_sensor=0x3000000,fixture_world=0x4000000;
std::map<std::uintptr_t,unsigned char> fixture_memory;
struct FixtureOutput {std::uintptr_t data{};int count{};unsigned capacity{};} fixture_output;
std::uintptr_t fixture_raw{};bool fixture_readable=true;unsigned fixture_calls{};
template<class T>void fixture_put(std::uintptr_t at,const T& value) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t i=0;i<sizeof(value);++i)fixture_memory[at+i]=bytes[i];
}
bool fixture_read(std::uintptr_t at,void* out,std::size_t size) {
    using namespace d2vr;
    if(at==fixture_base+0xca6550 && size==NativeHandVolume::constructor_code.size()) {std::memcpy(out,NativeHandVolume::constructor_code.data(),size);return true;}
    if(at==fixture_base+0x95370 && size==NativeHandVolume::overlap_queue_code.size()) {std::memcpy(out,NativeHandVolume::overlap_queue_code.data(),size);return true;}
    const auto output_at=reinterpret_cast<std::uintptr_t>(&fixture_output);
    if(at>=output_at && at+size<=output_at+sizeof(fixture_output)) {
        if(!fixture_readable)return false;std::memcpy(out,reinterpret_cast<void*>(at),size);return true;
    }
    for(std::size_t i=0;i<size;++i) {
        const auto found=fixture_memory.find(at+i);if(found==fixture_memory.end())return false;
        static_cast<unsigned char*>(out)[i]=found->second;
    }
    return true;
}
void* __fastcall fixture_construct(void* out,const float* origin,float radius) {
    auto* bytes=static_cast<unsigned char*>(out);const std::uintptr_t table=fixture_base+0x1ec1e78;const unsigned reference=0x1ffff;
    std::memcpy(bytes,&table,8);std::memcpy(bytes+0x10,&reference,4);std::memcpy(bytes+0x28,&radius,4);std::memcpy(bytes+0x50,origin,12);return out;
}
std::uintptr_t __fastcall fixture_fetch(void*,void* id,void* out) {
    assert(out==&fixture_output);++fixture_calls;
    fixture_put(reinterpret_cast<std::uintptr_t>(id),unsigned{0xffffffff});return fixture_raw;
}
}
namespace d2vr {
struct NativeInteractionRangeFixture {
    static void run() {
        const auto shape=NativeHandVolume::shape(fixture_base,fixture_read,fixture_construct);assert(shape);
        auto storage=std::make_unique<NativeInteractionAim>();auto& aim=*storage;
        aim.base_=fixture_base;aim.read_=fixture_read;aim.route_=true;aim.active_=true;aim.fetch_=fixture_fetch;
        aim.routing_.next({true,false},GetTickCount64());
        NativeInteractionAim::Context scope{};scope.side=0;scope.stage=0;scope.sensor=fixture_sensor;scope.revision=aim.routing_.revision();
        scope.camera.player=0x5000000;scope.camera.camera=0x6000000;scope.camera.camera_identity={7,10};
        scope.hand={GetTickCount64(),1,scope.camera.player,scope.camera.camera,7,{},true,{}};
        NativeInteractionAim::context_=&scope;
        const auto manager=fixture_game+0x60280,token_at=fixture_sensor+0x70;constexpr unsigned token=0x2001;
        const auto slot=manager+0x8280+(token&0x1fff)*0x140;
        fixture_put(fixture_base+0x39a5290,fixture_game);fixture_put(manager+0x190,fixture_world);fixture_put(fixture_game+0x60410,fixture_world);
        fixture_put(slot+0x88,token);fixture_put(slot+0x8c,static_cast<unsigned char>(4));fixture_put(slot+0x8e,static_cast<unsigned char>(2));fixture_put(slot+0xb8,shape);
        aim.volume_tickets_[0]={fixture_sensor,token,0,scope.revision,GetTickCount64()};
        struct Case {std::uintptr_t raw;FixtureOutput output;bool readable;NativeUseQueryOutcome outcome;};
        const Case cases[]={
            {0x123456789abcde01ull,{0x7000000,2,2},true,NativeUseQueryOutcome::multiple},
            {0x123456789abcde01ull,{0,0,0},true,NativeUseQueryOutcome::empty},
            {0x123456789abcde01ull,{0x7000000,1,1},true,NativeUseQueryOutcome::single},
            {0x123456789abcde00ull,{0,-1,0},false,NativeUseQueryOutcome::incomplete},
            {0x123456789abcde01ull,{0,1,1},true,NativeUseQueryOutcome::rejected},
            {0x123456789abcde01ull,{0x7000000,-1,2},true,NativeUseQueryOutcome::rejected},
            {0x123456789abcde01ull,{0x7000000,65,65},true,NativeUseQueryOutcome::rejected},
            {0x123456789abcde01ull,{0x7000000,2,1},true,NativeUseQueryOutcome::rejected},
            {0x123456789abcde02ull,{0x7000000,2,2},true,NativeUseQueryOutcome::rejected},
            {0x123456789abcde01ull,{0x7000000,2,2},false,NativeUseQueryOutcome::rejected},
        };
        for(const auto& item:cases) {
            fixture_raw=item.raw;fixture_output=item.output;fixture_readable=item.readable;fixture_calls=0;
            fixture_put(token_at,token);for(auto& counter:aim.query_outcomes_)counter=0;
            const auto returned=aim.fetch_result(reinterpret_cast<void*>(manager),reinterpret_cast<void*>(token_at),&fixture_output,fixture_base+0x3b885b);
            assert(returned==item.raw && fixture_calls==1 && !aim.depth_);
            const auto outcomes=aim.query_outcomes();for(unsigned i=0;i<outcomes.size();++i)assert(outcomes[i]==(i==static_cast<unsigned>(item.outcome)?1u:0u));
            if(item.outcome==NativeUseQueryOutcome::multiple)assert(NativeInteractionAim::retrieved_.data==item.output.data && NativeInteractionAim::retrieved_.count==2);
            else assert(!NativeInteractionAim::retrieved_.scope); // malformed/pending does not preserve older multiple results
        }
        NativeInteractionAim::context_=nullptr;
    }
};
}
int main(){d2vr::NativeInteractionRangeFixture::run();std::cout<<"PASS: actual owned fetch body decodes AL-only completion, retains raw RAX and native-once call, rejects malformed/unreadable output and clears prior result scope\n";}
