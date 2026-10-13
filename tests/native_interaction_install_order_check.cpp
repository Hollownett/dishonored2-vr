#define NOMINMAX
#include "../src/native_interaction_aim.hpp"
#include <map>
#include <cassert>
#include <iostream>
#include <memory>
namespace {
constexpr std::uintptr_t fixture_base=0x140000000;
std::map<std::uintptr_t,unsigned char> fixture_bytes;
std::map<std::uintptr_t,std::array<unsigned char,32>> fixture_original_bytes;
unsigned fixture_created{},fixture_enabled{},fixture_obstruction_checks{};
bool fixture_checked_before_hook{};
template<class T>void fixture_put(std::uintptr_t at,const T& value) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t i=0;i<sizeof(value);++i)fixture_bytes[at+i]=bytes[i];
}
bool fixture_read(std::uintptr_t at,void* out,std::size_t size) {
    if(at==fixture_base+0x3b7b90 && size==32) {
        ++fixture_obstruction_checks;
        if(!fixture_created)fixture_checked_before_hook=true;
    }
    for(std::size_t i=0;i<size;++i) {
        const auto found=fixture_bytes.find(at+i);if(found==fixture_bytes.end())return false;
        static_cast<unsigned char*>(out)[i]=found->second;
    }
    return true;
}
UINT fixture_logic(){return 2;}
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID,LPVOID* original) {
    const auto at=reinterpret_cast<std::uintptr_t>(target);std::array<unsigned char,32> bytes{};
    assert(fixture_read(at,bytes.data(),bytes.size()));fixture_original_bytes[at]=bytes;
    *original=reinterpret_cast<void*>(at+0x10000000);++fixture_created;return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID target) {
    const auto at=reinterpret_cast<std::uintptr_t>(target);
    assert(fixture_original_bytes.count(at));fixture_bytes[at]=0xe9;++fixture_enabled;return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID target) {
    const auto at=reinterpret_cast<std::uintptr_t>(target);const auto found=fixture_original_bytes.find(at);
    if(found!=fixture_original_bytes.end())fixture_put(at,found->second);return MH_OK;
}
namespace d2vr {
struct NativeInteractionRangeFixture {
    static void prepare() {
        fixture_bytes.clear();fixture_original_bytes.clear();fixture_created=fixture_enabled=fixture_obstruction_checks=0;fixture_checked_before_hook=false;
        for(unsigned i=0;i<6;++i)fixture_put(fixture_base+NativeInteractionAim::rvas[i],NativeInteractionAim::expected[i]);
        for(unsigned i=0;i<3;++i) {
            std::array<unsigned char,32> bytes{};
            const auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};
            for(unsigned n=0;n<32;++n)bytes[n]=static_cast<unsigned char>(nibble(NativeInteractionAim::ranking_code[i][n*2])*16+nibble(NativeInteractionAim::ranking_code[i][n*2+1]));
            fixture_put(fixture_base+NativeInteractionAim::ranking_rvas[i],bytes);
        }
        fixture_put(fixture_base+0x417820,std::array<unsigned char,32>{});fixture_put(fixture_base+0x417820,NativeInteractionAim::range_origin_code);
        fixture_put(fixture_base+0x3b80a0,std::array<unsigned char,32>{0x48,0x8b,0xc4,0x48,0x89,0x78,0x20,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0xa0,0,0,0,0x4d,0x8b,0xe1,0x4d,0x8b,0xf0,0x48,0x8b,0xfa,0x4c,0x8b,0xf9});
        fixture_put(fixture_base+0x3b81d3,std::array<unsigned char,5>{0xe8,0x48,0xf6,0x05,0});
    }
    static void run() {
        const auto folder=std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path().parent_path()/L"artifacts"/L"playground"/L"native-interaction-install-order-check";
        std::filesystem::create_directories(folder);
        for(const bool route:{false,true}) {
            prepare();auto grab=std::make_unique<NativeGrab>();auto owned_aim=std::make_unique<NativeInteractionAim>();auto& aim=*owned_aim;aim.attach_physical_grab(grab.get());
            aim.install(fixture_base,fixture_read,fixture_logic,route,folder/(route?L"route-on.csv":L"route-off.csv"));
            assert(fixture_checked_before_hook && fixture_obstruction_checks && aim.active_);
            assert(fixture_created==(route?10u:6u) && fixture_enabled==fixture_created);
            assert(reinterpret_cast<std::uintptr_t>(aim.physical_occlusion_)==fixture_base+0x3b7b90+(route?0x10000000:0));
            if(route)assert(fixture_bytes.at(fixture_base+0x3b7b90)==0xe9); // signature really changed
            assert(aim.disable());
        }
        for(const bool route:{false,true}) {
            prepare();fixture_bytes[fixture_base+0x3b7b90]=0xcc;
            auto grab=std::make_unique<NativeGrab>();auto owned_aim=std::make_unique<NativeInteractionAim>();auto& aim=*owned_aim;aim.attach_physical_grab(grab.get());bool rejected{};
            try {aim.install(fixture_base,fixture_read,fixture_logic,route,folder/L"corrupt.csv");}
            catch(const std::runtime_error& error){rejected=std::string(error.what())=="Native physical-grab obstruction wrapper differs";}
            assert(rejected && fixture_checked_before_hook && !fixture_created && !fixture_enabled && !aim.active_);
        }
        // Preserve bounded fixture logs in the project artifact directory.
    }
};
}
int main(){try {d2vr::NativeInteractionRangeFixture::run();}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}std::cout<<"PASS: physical wrapper signature validated before hook mutations; real install route on/off chooses original/trampoline, corrupt original rejects before any hook\n";}
