#include "../src/native_audio_listener.hpp"
#include <cassert>
#include <iostream>
#include <map>

namespace d2vr {
struct NativeAudioListenerFixture {
    static bool plan(NativeAudioListener& listener,std::uintptr_t target,std::uintptr_t caller) {
        AudioListenerPose pose{};return listener.plan(target,caller,pose);
    }
};
}
namespace {
constexpr std::uintptr_t fixture_game=0x200000000,fixture_player=0x201000000,fixture_camera=0x202000000,
    fixture_component=0x203000000,fixture_components=0x204000000,fixture_declaration=0x205000000;
std::map<std::uintptr_t,unsigned char> memory;
std::uintptr_t base{};
UINT mode=2;
std::uintptr_t mutate_after_read{},mutate_target{};
std::uint64_t mutate_value{};unsigned mutate_count{};
UINT logic() {return mode;}
template<class T> void put(std::uintptr_t at,const T& value) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t i=0;i<sizeof(value);++i) memory[at+i]=bytes[i];
}
bool read(std::uintptr_t at,void* out,std::size_t size) {
    if(at>=base && at+size<=base+0x800000) {std::memcpy(out,reinterpret_cast<void*>(at),size);return true;}
    for(std::size_t i=0;i<size;++i) {
        const auto found=memory.find(at+i);if(found==memory.end()) return false;
        static_cast<unsigned char*>(out)[i]=found->second;
    }
    if(at==mutate_after_read && mutate_count && --mutate_count==0) {
        put(mutate_target,mutate_value);mutate_after_read=0;
    }
    return true;
}
struct alignas(8) Listener {std::array<unsigned char,0x90> bytes{};};
template<class T> T field(const Listener& listener,std::size_t offset) {
    T result;std::memcpy(&result,listener.bytes.data()+offset,sizeof(result));return result;
}
void seed(std::uintptr_t target) {
    memory.clear();mutate_after_read=0;mutate_count=0;
    const auto main=target-0x10;
    put(base+d2vr::audio_main_pointer_rva,main);put(main,std::uint8_t{0x24});put(main+8,std::uint32_t{0xff});
    for(unsigned i=0;i<8;++i) {
        put(target+i*0x90,std::uint32_t{i});put(target+i*0x90+0x88,std::uint8_t{1});
    }
    put(fixture_declaration,base+d2vr::audio_listener_declaration_vtable);put(fixture_declaration+0x68,std::uint32_t{0});
    put(fixture_component+8,fixture_declaration);
    put(base+0x39a5290,fixture_game);put(fixture_game,base+0x1c940a8);
    put(fixture_game+0x3130b0,fixture_player);put(fixture_player,base+0x1c5e428);
    put(fixture_camera,base+d2vr::camera_vtable);put(fixture_camera+0x20,base+d2vr::camera_secondary_vtable);
    put(fixture_camera+0x10,fixture_player);put(fixture_camera+0x18,d2vr::rig::Identity{5,10});
    put(fixture_component,base+d2vr::audio_listener_component_vtable);put(fixture_component+0x10,fixture_player);
    put(fixture_component+0x18,d2vr::rig::Identity{7,10});put(fixture_component+0x40,target);
    put(fixture_player+0x268,d2vr::rig::List{fixture_components,1,1<<8});put(fixture_components,fixture_component);
}
}
int main() {
    using namespace d2vr;
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x800000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    assert(image);base=reinterpret_cast<std::uintptr_t>(image);
    std::memcpy(image+audio_listener_set_rva,audio_listener_set_code.data(),audio_listener_set_code.size());
    // Complete the exact native prefix, then record call count and incoming
    // pointer arguments. This is a synthetic setter, not game acceptance.
    const unsigned char tail[]={0x40,0x08,0x89,0x41,0x48,
        0x41,0x8b,0x01,0x89,0x41,0x4c,0x41,0x8b,0x41,0x04,0x89,0x41,0x50,
        0x41,0x8b,0x41,0x08,0x89,0x41,0x54,0x48,0xff,0x41,0x60,
        0x48,0x89,0x51,0x70,0x4c,0x89,0x41,0x78,0x4c,0x89,0x89,0x80,0,0,0,0xc3};
    std::memcpy(image+audio_listener_set_rva+32,tail,sizeof(tail));
    const unsigned char caller[]={0x48,0x83,0xec,0x28,0xe8,0,0,0,0,0x48,0x83,0xc4,0x28,0xc3};
    auto* entry=image+audio_listener_call_rva-4;std::memcpy(entry,caller,sizeof(caller));
    const auto displacement=static_cast<std::int32_t>(audio_listener_set_rva)-static_cast<std::int32_t>(audio_listener_call_rva+5);
    std::memcpy(image+audio_listener_call_rva+1,&displacement,sizeof(displacement));
    FlushInstructionCache(GetCurrentProcess(),image,0x800000);
    std::array<Listener,8> native_slots{},replacement_slots{};
    auto& selected=native_slots[0];auto& unrelated=native_slots[1];
    const auto target=reinterpret_cast<std::uintptr_t>(&selected);
    seed(target);auto baseline=memory;
    NativePlayerCamera owner{};owner.player=fixture_player;owner.camera=fixture_camera;owner.camera_identity={5,10};
    auto current=find_audio_listener(read,base,owner);assert(current && current->listener==target);
    put(fixture_game+0x3130b0,fixture_player+8);assert(!current_audio_listener(read,base,*current));memory=baseline;
    put(fixture_component+0x18,rig::Identity{8,10});assert(!current_audio_listener(read,base,*current));memory=baseline;
    put(fixture_camera+0x18,rig::Identity{6,10});assert(!find_audio_listener(read,base,owner));memory=baseline;
    put(fixture_component+0x18,rig::Identity{7,0});assert(!find_audio_listener(read,base,owner));memory=baseline;
    put(fixture_camera+0x18,rig::Identity{-1,10});assert(!find_audio_listener(read,base,owner));memory=baseline;
    put(fixture_component,base+audio_listener_component_vtable+8);assert(!find_audio_listener(read,base,owner));memory=baseline;
    put(fixture_component+0x10,fixture_player+8);assert(!find_audio_listener(read,base,owner));memory=baseline;
    put(fixture_component+0x40,std::uintptr_t{0});assert(!find_audio_listener(read,base,owner));memory=baseline;
    put(fixture_player+0x268,rig::List{fixture_components,2,2<<8});put(fixture_components+8,fixture_component);
    assert(!find_audio_listener(read,base,owner));memory=baseline;
    put(fixture_player+0x268,rig::List{fixture_components,2,1<<8});
    assert(!find_audio_listener(read,base,owner));memory=baseline;
    assert(current->declaration==fixture_declaration && current->main==target-0x10 && current->world==target-8);
    assert(current->listener_index==0 && current->component_slot==0 && current->components==fixture_components);
    put(fixture_declaration+0x68,std::uint32_t{1});assert(!current_audio_listener(read,base,*current));
    assert(!find_audio_listener(read,base,owner));memory=baseline;
    put(fixture_declaration+0x68,std::uint32_t{8});assert(!find_audio_listener(read,base,owner));memory=baseline;
    put(fixture_declaration,base+audio_listener_declaration_vtable+8);assert(!find_audio_listener(read,base,owner));memory=baseline;
    put(target,std::uint32_t{1});assert(!find_audio_listener(read,base,owner));memory=baseline;
    put(target+0x88,std::uint8_t{0});assert(!current_audio_listener(read,base,*current));memory=baseline;
    put(target-8,std::uint32_t{0xfe});assert(!current_audio_listener(read,base,*current));memory=baseline;
    put(target-0x10,std::uint8_t{0});assert(!find_audio_listener(read,base,owner));memory=baseline;
    put(base+audio_main_pointer_rva,target+0x1000);assert(!current_audio_listener(read,base,*current));memory=baseline;
    put(fixture_components,fixture_component+8);assert(!current_audio_listener(read,base,*current));
    assert(!find_audio_listener(read,base,owner));memory=baseline;
    // An unchanged list data/count must not hide a replaced selected slot.
    mutate_after_read=fixture_components;mutate_count=1;mutate_target=fixture_components;mutate_value=fixture_component+8;
    assert(!current_audio_listener(read,base,*current));memory=baseline;
    mutate_after_read=fixture_components;mutate_count=1;
    assert(!find_audio_listener(read,base,owner));memory=baseline;
    // The declaration and global world must also survive the read sandwich.
    mutate_after_read=fixture_components;mutate_count=1;mutate_target=fixture_component+8;mutate_value=fixture_declaration+0x100;
    assert(!current_audio_listener(read,base,*current));memory=baseline;
    mutate_after_read=fixture_components;mutate_count=1;mutate_target=base+audio_main_pointer_rva;mutate_value=target+0x1000;
    assert(!current_audio_listener(read,base,*current));memory=baseline;
    put(fixture_component+0x40,target+7*0x90);put(fixture_declaration+0x68,std::uint32_t{7});
    const auto last_slot=find_audio_listener(read,base,owner);assert(last_slot && last_slot->listener_index==7);memory=baseline;
    put(fixture_player+0x268,rig::List{fixture_components,2,2<<8});
    put(fixture_components,fixture_component+0x100);put(fixture_components+8,fixture_component);
    const auto moved=find_audio_listener(read,base,owner);assert(moved && moved->component_slot==1);
    put(fixture_components,fixture_component);put(fixture_components+8,fixture_component+0x100);
    assert(!current_audio_listener(read,base,*moved));
    const auto moved_again=find_audio_listener(read,base,owner);assert(moved_again && moved_again->component_slot==0);memory=baseline;
    mutate_after_read=fixture_declaration+0x68;mutate_count=1;mutate_target=fixture_declaration+0x68;mutate_value=1;
    assert(!current_audio_listener(read,base,*current));memory=baseline;
    mutate_after_read=fixture_declaration+0x68;mutate_count=1;mutate_target=fixture_declaration;
    mutate_value=base+audio_listener_declaration_vtable+8;
    assert(!current_audio_listener(read,base,*current));memory=baseline;
    mutate_after_read=target-8;mutate_count=1;mutate_target=target-8;mutate_value=0;
    assert(!current_audio_listener(read,base,*current));memory=baseline;
    assert(MH_Initialize()==MH_OK);
    NativeAudioListener listener;
    const auto prefix=image[audio_listener_set_rva];image[audio_listener_set_rva]^=1;
    bool rejected=false;try {listener.install(base,read,logic);} catch(const std::runtime_error&) {rejected=true;}
    assert(rejected);image[audio_listener_set_rva]=prefix;
    image[audio_listener_call_rva+1]^=1;rejected=false;
    try {listener.install(base,read,logic);} catch(const std::runtime_error&) {rejected=true;}
    assert(rejected);image[audio_listener_call_rva+1]^=1;
    listener.install(base,read,logic);assert(listener.active());
    const AudioListenerPose replacement{{10,20,30},{0,0,1},{1,0,0}};
    const std::array<float,3> position{1,2,3},top{4,5,6},front{7,8,9};
    using Set=void (__fastcall*)(void*,const float*,const float*,const float*);
    const auto invoke=reinterpret_cast<Set>(entry);
    auto check=[&](Listener& output,bool replaced) {
        const auto before=field<std::uint64_t>(output,0x60);
        invoke(&output,position.data(),top.data(),front.data());
        assert(field<std::uint64_t>(output,0x60)==before+1);
        assert((field<std::array<float,3>>(output,0x34)==(replaced?replacement.position:position)));
        assert((field<std::array<float,3>>(output,0x40)==(replaced?replacement.top:top)));
        assert((field<std::array<float,3>>(output,0x4c)==(replaced?replacement.front:front)));
        if(!replaced) {
            assert(field<const float*>(output,0x70)==position.data());
            assert(field<const float*>(output,0x78)==top.data());
            assert(field<const float*>(output,0x80)==front.data());
        } else {
            assert(field<const float*>(output,0x70)!=position.data());
            assert(field<const float*>(output,0x78)!=top.data());
            assert(field<const float*>(output,0x80)!=front.data());
        }
    };
    check(selected,false);
    const auto publish=[&] {listener.publish(owner,replacement,1,GetTickCount64());};
    publish();check(selected,true);check(unrelated,false);
    const auto alternate=reinterpret_cast<std::uintptr_t>(&unrelated);
    put(fixture_component+0x40,alternate);check(selected,false);
    publish();check(unrelated,false); // The declaration still selects slot zero.
    put(fixture_declaration+0x68,std::uint32_t{1});publish();check(unrelated,true);check(selected,false);
    memory=baseline;publish();check(selected,true);
    put(fixture_components,fixture_component+8);check(selected,false);publish();check(selected,false);
    memory=baseline;publish();check(selected,true);
    const auto replacement_target=reinterpret_cast<std::uintptr_t>(&replacement_slots[0]);
    put(base+audio_main_pointer_rva,replacement_target-0x10);check(selected,false);publish();check(selected,false);
    put(replacement_target-0x10,std::uint8_t{0x24});put(replacement_target-8,std::uint32_t{1});
    put(replacement_target,std::uint32_t{0});put(replacement_target+0x88,std::uint8_t{1});
    put(fixture_component+0x40,replacement_target);publish();check(replacement_slots[0],true);check(selected,false);
    memory=baseline;publish();check(selected,true);
    assert(!NativeAudioListenerFixture::plan(listener,target,base+audio_listener_call_rva+6));
    // A direct call has an unrelated return address and preserves all inputs.
    const auto before_direct=field<std::uint64_t>(selected,0x60);
    reinterpret_cast<Set>(image+audio_listener_set_rva)(&selected,position.data(),top.data(),front.data());
    assert(field<std::uint64_t>(selected,0x60)==before_direct+1);
    assert(field<const float*>(selected,0x70)==position.data());
    assert(field<const float*>(selected,0x78)==top.data());
    assert(field<const float*>(selected,0x80)==front.data());
    mode=1;check(selected,false);mode=2;
    listener.publish(owner,replacement,0,GetTickCount64());check(selected,false);
    listener.publish(owner,replacement,1,0);check(selected,false);
    listener.publish(owner,replacement,1,GetTickCount64()+1000);check(selected,false);
    listener.publish(owner,replacement,1,GetTickCount64()-101);check(selected,false);
    publish();put(fixture_component+0x18,rig::Identity{8,10});check(selected,false);
    publish();check(selected,true); // A fresh publication rediscovers recycled ownership.
    put(fixture_camera+0x18,rig::Identity{6,10});check(selected,false);publish();check(selected,false);
    owner.camera_identity.generation=6;publish();check(selected,true);
    put(fixture_game+0x3130b0,fixture_player+8);check(selected,false);publish();check(selected,false);
    memory=baseline;owner.camera_identity.generation=5;publish();listener.clear();check(selected,false);
    publish();assert(listener.applied()>=3 && listener.fallback()>=10);
    assert(listener.disable() && !listener.active());check(selected,false);
    listener.publish(owner,replacement,1,GetTickCount64());check(selected,false);
    assert(listener.disable());
    listener.install(base,read,logic);publish();check(selected,true);
    assert(listener.disable());check(selected,false);
    assert((position==std::array<float,3>{1,2,3}));
    assert((top==std::array<float,3>{4,5,6}));
    assert((front==std::array<float,3>{7,8,9}));
    assert(MH_RemoveHook(image+audio_listener_set_rva)==MH_OK);assert(MH_Uninitialize()==MH_OK);
    assert(VirtualFree(image,0,MEM_RELEASE));
    std::cout<<"PASS: real MinHook synthetic native ABI, exactly-once original, immutable caller vectors, acquired world slot/declaration/index/membership sandwiches, main replacement, selected caller, freshness/menu/clear/disable\n";
}

