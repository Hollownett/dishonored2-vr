#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_hud_art_probe.hpp"
#include <cassert>
#include <iostream>
#include <string>
namespace {
constexpr std::uintptr_t base=0x140000000;
std::array<std::byte,0x1668> fixture_file{};
std::array<std::byte,16> fixture_iggy{};
alignas(8) std::array<std::byte,64> fixture_value{};
alignas(8) std::array<std::byte,0xe0> fixture_node{};
const char fixture_path[]="ui/hud.iggy";
std::string current_path;
void* object;
unsigned allocated{},released{};
bool bad_matrix{};
bool read(std::uintptr_t at,void* out,std::size_t size){SIZE_T n{};return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,size,&n) && n==size;}
void* __fastcall construct(void* out,void*,const char* name){
    ++allocated;object=out;current_path=name;auto* bytes=static_cast<unsigned char*>(out);
    const auto identity=reinterpret_cast<std::uintptr_t>(fixture_iggy.data()),table=reinterpret_cast<std::uintptr_t>(bytes+0x28);
    const std::uint32_t index=1;std::memcpy(bytes+8,&identity,8);std::memcpy(bytes+0x18,&table,8);std::memcpy(bytes+0x20,&index,4);return out;
}
void __fastcall destroy(void* out){assert(out==object);++released;}
std::uint64_t __fastcall get(const void*,std::uint64_t,const char* name,void*,const char*,std::uint32_t* status){
    *status=0;
    if(!name)return reinterpret_cast<std::uintptr_t>(fixture_value.data())+1;
    if(current_path.find("concatenatedMatrix")!=std::string::npos){
        if(bad_matrix && std::strcmp(name,"tx")==0){*status=5;return 7;}
        if(std::strcmp(name,"a")==0 || std::strcmp(name,"d")==0)return 2;
        if(std::strcmp(name,"tx")==0)return 1920;
        if(std::strcmp(name,"ty")==0)return 1080;
        return 0;
    }
    assert(current_path=="_interactionWindow" || current_path=="_masterHUD" ||
        current_path=="_tutorialMessage" || current_path=="_informationMessage");
    if(std::strcmp(name,"width")==0)return 800;
    if(std::strcmp(name,"height")==0)return 240;
    if(std::strcmp(name,"visible")==0)return base+0x2424049;
    return 0;
}
}
int main(){
    using namespace d2vr;
    const auto identity=reinterpret_cast<std::uintptr_t>(fixture_iggy.data()),file_address=reinterpret_cast<std::uintptr_t>(fixture_file.data()),path_address=reinterpret_cast<std::uintptr_t>(fixture_path);
    std::memcpy(fixture_file.data()+0x1608,&identity,8);std::memcpy(fixture_file.data()+0x1658,&path_address,8);
    fixture_value[0]=std::byte{0x24};const auto node_address=reinterpret_cast<std::uintptr_t>(fixture_node.data()),root=identity+0x26b0;
    std::memcpy(fixture_value.data()+0x28,&node_address,8);std::memcpy(fixture_node.data()+0x80,&root,8);
    const NativePromptCall owner{file_address,identity,1};const NativeInteractionPrompt::Api api{construct,destroy,get};
    WristHudDrawIdentity::clear();assert(!NativeHudArtProbe::probe(read,base,owner,api,false).valid && allocated==0);
    NativeWristHud::observe_draw(identity,GetTickCount64());
    auto result=NativeHudArtProbe::probe(read,base,owner,api,false);
    assert(result.valid && result.display_node==node_address && result.visible && result.width==400 && result.height==120 && result.tx==960 && result.ty==540 && result.fields==2047);
    result=NativeHudArtProbe::probe(read,base,owner,api,true);assert(result.valid && result.visible && current_path=="_masterHUD.transform.concatenatedMatrix");
    // v677: scripted HUD messages use the same verified probe with an explicit HUDScene path.
    result=NativeHudArtProbe::probe(read,base,owner,api,false,"_tutorialMessage");
    assert(result.valid && result.visible && current_path=="_tutorialMessage.transform.concatenatedMatrix");
    result=NativeHudArtProbe::probe(read,base,owner,api,false,"_informationMessage");
    assert(result.valid && result.visible && current_path=="_informationMessage.transform.concatenatedMatrix");
    bad_matrix=true;result=NativeHudArtProbe::probe(read,base,owner,api,false);assert(!result.valid && result.status==5);
    fixture_value[0]=std::byte{7};assert(!NativeHudArtProbe::display_node(read,owner,api,"_interactionWindow"));
    fixture_value[0]=std::byte{0x24};const std::uintptr_t cycle=node_address;std::memcpy(fixture_node.data()+0x80,&cycle,8);
    assert(!NativeHudArtProbe::display_node(read,owner,api,"_interactionWindow"));
    assert(allocated==released);std::cout<<"native HUD art geometry probe checks passed\n";
}

