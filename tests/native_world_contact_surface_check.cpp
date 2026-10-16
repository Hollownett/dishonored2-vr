#define NOMINMAX
#include "../src/native_world_contact_surface.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace {
using namespace d2vr;
constexpr std::uintptr_t kBase=0x140000000,kGame=0x200000000,kPlayer=0x201000000,kCamera=0x202000000;
constexpr std::uintptr_t kRenderBody=0x203000000,kController=0x204000000,kCameraController=0x204100000,kPhysics=0x205000000;
constexpr std::uintptr_t kDelegate=0x206000000,kWorld=0x207000000,kPool=0x208000000;
constexpr std::uintptr_t kBody=kPool+2*0xb0;
constexpr std::uint32_t kHandle=0x12000002;
constexpr std::uintptr_t kHitBody=kPool+3*0xb0,kHitUserdata=0x20a100000,kHitComponent=0x20a200000,kHitEntity=0x20a300000;
constexpr std::uint32_t kHitHandle=0x12000003,kHitEntityId=0x00010002;
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;
    std::uintptr_t fail_at{},mutate_at{},mutate_target{},raise_at{};
    std::array<unsigned char,8> mutation{};
    std::uintptr_t throw_at{};
    bool operator()(std::uintptr_t at,void* out,std::size_t size) {
        if(at==fail_at)return false;
        for(std::size_t index=0;index<size;++index) {
            const auto found=bytes.find(at+index);if(found==bytes.end())return false;
            static_cast<unsigned char*>(out)[index]=found->second;
        }
        if(at==throw_at)throw 7;
        if(at==mutate_at) {for(std::size_t index=0;index<mutation.size();++index)bytes[mutate_target+index]=mutation[index];mutate_at=0;}
#if defined(_WIN32)
        if(at==raise_at)RaiseException(0xe0424242,0,0,nullptr);
#endif
        return true;
    }
    template<class T> void put(std::uintptr_t at,const T& value) {
        const auto* data=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t index=0;index<sizeof(value);++index)bytes[at+index]=data[index];
    }
    void erase(std::uintptr_t at,std::size_t size) {for(std::size_t index=0;index<size;++index)bytes.erase(at+index);}
    template<class T> void mutate_after_read(std::uintptr_t read_at,std::uintptr_t write_at,const T& value) {
        assert(sizeof(value)<=mutation.size());mutate_at=read_at;mutate_target=write_at;std::memcpy(mutation.data(),&value,sizeof(value));
    }
};
Memory fixture() {
    Memory memory;
    memory.put(kBase+0x39a5290,kGame);memory.put(kGame,kBase+0x1c940a8);memory.put(kGame+0x3130b0,kPlayer);
    memory.put(kPlayer,kBase+0x1c5e428);memory.put(kPlayer+0xa8,std::uint32_t{123});
    memory.put(kGame+0x60410,kWorld);memory.put(kGame+0x60280,kGame+0x60280);
    memory.put(kCamera,std::array<std::byte,d2vr::camera_read_size>{});memory.put(kCamera+0x25150,std::array<std::byte,0x200>{});
    for(const auto [address,table]:std::array<std::pair<std::uintptr_t,std::uintptr_t>,10>{
        {{kCamera,0x1c72218},{kCamera+0x20,0x1c722f8},{kRenderBody,0x1c5a8d8},{kRenderBody+0x20,0x1c5a9b0},
         {kCameraController,0x1c72918},{kController,0x1ad26a0},{kPhysics,0x1c73268},{kPhysics+0x20,0x1c73340},
         {kPhysics+0x30,0x1c73378},{kDelegate+0x160,0x1b02768}}})
        memory.put(address,kBase+table);
    for(const auto address:{kCamera,kRenderBody,kCameraController,kController,kPhysics}) {
        memory.put(address+0x10,kPlayer);memory.put(address+0x18,rig::Identity{5,10});
    }
    memory.put(kCamera+0x25158,kCameraController);memory.put(kCamera+0x25160,kPhysics);memory.put(kCamera+0x25170,kRenderBody);
    constexpr std::uintptr_t components=0x20a000000;
    memory.put(kPlayer+0x268,rig::List{components,4,4u<<8});memory.put(components,kCamera);
    memory.put(components+8,kRenderBody);memory.put(components+16,kCameraController);memory.put(components+24,kPhysics);
    memory.put(kPhysics+0x48,kDelegate);memory.put(kDelegate+0x168,kPhysics);memory.put(kDelegate+0x120,kPlayer+0x300);
    memory.put(kDelegate+0x1e0,kDelegate);memory.put(kDelegate+0xd0,1.5f);memory.put(kDelegate+0xbc,0.f);memory.put(kDelegate+0x40,0.f);
    memory.put(kPlayer+0x30c,std::array<float,9>{1,0,0,0,1,0,0,0,1});memory.put(kCameraController+0xd0,std::array<float,2>{.1f,-.2f});
    memory.put(kDelegate+0x148,kController);memory.put(kController+0x140,kGame+0x60280);memory.put(kController+0x40,kWorld);
    memory.put(kController+0x38,kHandle);memory.put(kController+0x3c,std::uint32_t{0x120});
    memory.put(kWorld+0x28,kPool);memory.put(kWorld+0x30,std::uint32_t{8});memory.put(kWorld+NativeWorldTrace::shape_tag_codec_offset,std::uint64_t{0xabcdef0});
    memory.put(kBody+0x44,std::uint8_t{3});memory.put(kBody+0x70,kHandle);memory.put(kBody+0x78,std::uint32_t{1});
    memory.put(kBody+0x6c,std::uint32_t{0x220});
    for(unsigned index=0;index<NativeInitialContactCollector::native_slots.size();++index)
        memory.put(kBase+NativeInitialContactCollector::native_vtable_rva+index*8,kBase+NativeInitialContactCollector::native_slots[index]);
    memory.put(kBase+NativeInitialContactCollector::native_slots[0],NativeInitialContactCollector::getter_code);
    memory.put(kBase+NativeInitialContactCollector::native_slots[4],NativeInitialContactCollector::noop_code);
    return memory;
}

constexpr std::uintptr_t kShape=0x20b000000;
Memory seeded(bool capsule=false) {
    auto memory=fixture();
    memory.put(kHitBody,std::array<std::byte,0xb0>{});
    const rig::Transform pose{{1,0,0,99,0,1,0,98,0,0,1,97,3,4,5,96}};
    memory.put(kHitBody,pose);memory.put(kHitBody+0x44,std::uint32_t{2});memory.put(kHitBody+0x70,kHitHandle);
    memory.put(kHitBody+0x78,std::uint32_t{3});memory.put(kHitBody+0x60,kShape);memory.put(kHitBody+0x6a,std::uint16_t{0xffff});
    memory.put(kHitBody+0x6c,std::uint32_t{0x221});
    memory.put(kShape,std::array<std::byte,0x110>{});
    memory.put(kShape,kBase+(capsule?NativeWorldContactSurface::capsule_vtable:NativeWorldContactSurface::sphere_vtable));
    memory.put(kShape+0x20,std::uint16_t{0x111});memory.put(kShape+0x22,static_cast<std::uint8_t>(capsule?3:2));
    memory.put(kShape+0x24,std::uint8_t{2});memory.put(kShape+0x28,.075f);
    memory.put(kShape+0x40,static_cast<std::uint16_t>(capsule?8:1));memory.put(kShape+0x42,static_cast<std::uint16_t>(capsule?0x50:0x10));
    return memory;
}
void run(bool capsule) {
    auto memory=seeded(capsule);const auto source=native_world_query_source(memory,kBase,kCamera,kWorld);assert(source);
    const auto receipt=NativeWorldContactSurface::capture(memory,*source,kHitHandle,0);assert(receipt);
    assert(NativeWorldContactSurface::diagnostic_only && NativeWorldContactSurface::current(memory,*receipt));
    auto changed=memory;changed.put(kHitBody+0xc,std::uint32_t{0xff800000});changed.put(kHitBody+0x3c,12345.f);
    assert(NativeWorldContactSurface::current(changed,*receipt));
    for(unsigned row=0;row<4;++row)for(unsigned axis=0;axis<3;++axis) {
        changed=memory;changed.put(kHitBody+row*16+axis*4,1.25f);assert(!NativeWorldContactSurface::current(changed,*receipt));
    }
    changed=memory;changed.put(kHitBody+0x70,std::uint32_t{kHitHandle+0x1000000});assert(!NativeWorldContactSurface::current(changed,*receipt));
    changed=memory;changed.put(kHitBody+0x60,kShape+0x1000);assert(!NativeWorldContactSurface::current(changed,*receipt));
    changed=memory;changed.put(kShape+0x28,.1f);assert(!NativeWorldContactSurface::current(changed,*receipt));
    changed=memory;changed.put(kShape+0x40+receipt->vertex_offset,.125f);assert(!NativeWorldContactSurface::current(changed,*receipt));
    if(capsule) {changed=memory;changed.put(kShape+0x70,.125f);assert(!NativeWorldContactSurface::current(changed,*receipt));}
    for(const auto offset:{0x40u,0x44u,0x6cu,0x78u}) {
        changed=memory;changed.put(kHitBody+offset,std::uint32_t{0x2222});assert(!NativeWorldContactSurface::current(changed,*receipt));
    }
    changed=memory;changed.put(kHitBody+0x6a,std::uint16_t{0x10});assert(!NativeWorldContactSurface::current(changed,*receipt));
    changed=memory;changed.put(kShape,kBase+0x1ec1818);assert(!NativeWorldContactSurface::current(changed,*receipt));
    assert(!NativeWorldContactSurface::capture(memory,*source,kHitHandle,9));
    assert(NativeWorldContactSurface::capture(memory,*source,kHitHandle,0xffffffffu));
    changed=memory;changed.fail_at=kShape+0x28;assert(!NativeWorldContactSurface::current(changed,*receipt));
    changed=memory;changed.throw_at=kShape+0x28;assert(!NativeWorldContactSurface::current(changed,*receipt));
    changed=memory;changed.raise_at=kShape+0x28;assert(!NativeWorldContactSurface::current(changed,*receipt));
    changed=memory;changed.mutate_after_read(kShape+0x28,kShape+0x28,.1f);assert(!NativeWorldContactSurface::capture(changed,*source,kHitHandle,0));
    memory.put(kHitBody+0x90,kHitUserdata);memory.put(kHitUserdata+8,kHitComponent);
    memory.put(kHitComponent,kBase+0x1c745c8);memory.put(kHitComponent+0x18,rig::Identity{7,10});memory.put(kHitComponent+0x10,kHitEntity);
    memory.put(kHitEntity+0xa8,kHitEntityId);memory.put(kGame+0x3130b0+8,kHitEntity);
    constexpr std::uintptr_t members=kHitEntity+0x1000;
    memory.put(kHitEntity+0x268,rig::List{members,1,1u<<8});memory.put(members,kHitComponent);
    const auto owned=NativeWorldContactSurface::capture(memory,*source,kHitHandle,0);assert(owned);
    memory.put(kHitComponent+0x18,rig::Identity{8,10});assert(!NativeWorldContactSurface::current(memory,*owned));
}
}
int main(){run(false);run(true);std::cout<<"native world contact surface check passed\n";}
