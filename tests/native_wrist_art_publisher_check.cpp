#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_wrist_art_publisher.hpp"
#include <cassert>
#include <iostream>
#include <string>
namespace {
constexpr std::uintptr_t base=0x140000000;
std::array<std::byte,0x1668> fixture_file{};
std::array<std::byte,0x2700> fixture_iggy{};
std::array<std::byte,0x300> fixture_element{};
std::array<std::byte,0x20> block{};
std::array<std::byte,0xc0> fixture_slots{};
std::array<std::array<std::byte,0xe0>,4> nodes{};
alignas(8) std::array<std::array<std::byte,64>,4> values{};
const char hud_path[]="ui/hud.iggy";
std::string fixture_path;
unsigned constructs{},destroys{},reads{};
bool visible=true,bad_node=false,replace_file=false;
template<class T> void put(void* at,const T& value){std::memcpy(at,&value,sizeof(value));}
bool read(std::uintptr_t at,void* out,std::size_t size){SIZE_T n{};return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,size,&n)&&n==size;}
unsigned selected(){
    if(fixture_path=="_masterHUD")return 3;
    for(unsigned i=0;i<3;++i)if(fixture_path==d2vr::NativeWristArtPublisher::paths[i])return i;
    assert(false);return 0;
}
void* __fastcall construct(void* out,void* source,const char* name){
    assert(source==fixture_file.data());++constructs;fixture_path=name;selected();
    auto* bytes=static_cast<std::byte*>(out);
    put(bytes+8,reinterpret_cast<std::uintptr_t>(fixture_iggy.data()));
    put(bytes+0x18,reinterpret_cast<std::uintptr_t>(bytes+0x28));put(bytes+0x20,std::uint32_t{1});return out;
}
void __fastcall destroy(void*){++destroys;}
std::uint64_t __fastcall get(const void*,std::uint64_t,const char* name,void*,const char*,std::uint32_t* status){
    ++reads;*status=0;const auto i=selected();
    if(!name){
        if(replace_file)put(fixture_file.data()+0x1608,reinterpret_cast<std::uintptr_t>(fixture_iggy.data())+8);
        return bad_node?7:reinterpret_cast<std::uintptr_t>(values[i].data())+1;
    }
    if(!std::strcmp(name,"visible"))return base+(visible?0x2424049:0x2424041);
    if(!std::strcmp(name,"width"))return 200+20*i;
    if(!std::strcmp(name,"height"))return 100;
    return 0;
}
}
int main(){
    using namespace d2vr;
    const auto identity=reinterpret_cast<std::uintptr_t>(fixture_iggy.data());
    const auto address=reinterpret_cast<std::uintptr_t>(fixture_element.data());
    const auto method=address+0x70,receiver=method+0xf0,args=reinterpret_cast<std::uintptr_t>(block.data());
    put(fixture_element.data(),base+0x1c39598);put(fixture_element.data()+0x68,reinterpret_cast<std::uintptr_t>(fixture_file.data()));
    put(fixture_element.data()+0x70,identity);put(fixture_element.data()+0x78,std::uintptr_t{1});
    put(fixture_file.data()+0x1608,identity);put(fixture_file.data()+0x1658,reinterpret_cast<std::uintptr_t>(hud_path));
    put(block.data(),reinterpret_cast<std::uintptr_t>(fixture_slots.data()));put(block.data()+0x10,std::uint32_t{4});put(block.data()+0x14,std::uint32_t{4});
    put(fixture_slots.data(),std::uint32_t{4});put(fixture_slots.data()+0x10,.5);
    for(unsigned i=0;i<4;++i){
        values[i][0]=std::byte{0x24};put(values[i].data()+0x28,reinterpret_cast<std::uintptr_t>(nodes[i].data()));
        put(nodes[i].data()+0x80,identity+0x26b0);
    }
    const NativeInteractionPrompt::Api api{construct,destroy,get};
    const auto observe=[&](bool before=false,std::uintptr_t caller=0x6749ba,std::uint32_t result=1){
        return NativeWristArtPublisher::observe(read,base,api,before,caller,method,receiver,args,result);
    };
    assert(!observe()[0].valid && constructs==0); // No current native Draw.
    // Load-time stat call (movie not drawn yet): its owner is kept and probed after the first draw.
    const auto early=NativeWristArtPublisher::early_owner(read,0x6749ba,method,receiver);
    assert(early && early->file==reinterpret_cast<std::uintptr_t>(fixture_file.data()) && early->iggy==identity && early->element==address);
    assert(!NativeWristArtPublisher::early_owner(read,0x6749ba,method,receiver+8) && !NativeWristArtPublisher::early_owner(read,0x1234,method,receiver));
    assert(!NativeWristArtPublisher::observe_owner(read,base,api,*early)[0].valid); // still not drawn
    NativeWristHud::observe_draw(identity,GetTickCount64());
    {const auto late=NativeWristArtPublisher::observe_owner(read,base,api,*early);assert(late[0].valid && late[1].valid && late[2].valid);}
    const auto master=[&](bool before=false,std::uintptr_t caller=0x6749ba,std::uint32_t result=1){return NativeWristArtPublisher::master_layout(read,base,api,before,caller,method,receiver,args,result);};
    const auto master_reads=reads;const auto layout=master();
    assert(layout && layout->file==reinterpret_cast<std::uintptr_t>(fixture_file.data()) && layout->iggy==identity && layout->node==reinterpret_cast<std::uintptr_t>(nodes[3].data()));
    assert(reads==master_reads+1 && fixture_path=="_masterHUD"); // one identity getter, no geometry probes
    const auto master_constructs=constructs;
    assert(!master(true) && !master(false,0x6749bb) && !master(false,0x6749ba,0) && constructs==master_constructs);
    const char foreign_path[]="ui/notes.iggy";put(fixture_file.data()+0x1658,reinterpret_cast<std::uintptr_t>(foreign_path));
    assert(!master());put(fixture_file.data()+0x1658,reinterpret_cast<std::uintptr_t>(hud_path));
    put(nodes[3].data()+0x80,reinterpret_cast<std::uintptr_t>(nodes[3].data()));assert(!master());put(nodes[3].data()+0x80,identity+0x26b0);
    put(values[3].data()+0x28,identity+0x26b0);assert(!master());put(values[3].data()+0x28,reinterpret_cast<std::uintptr_t>(nodes[3].data()));
    values[3][0]=std::byte{0x20};assert(!master());values[3][0]=std::byte{0x24};
    bad_node=true;assert(!master());bad_node=false;
    replace_file=true;assert(!master());replace_file=false;put(fixture_file.data()+0x1608,identity);
    auto art=observe();
    for(unsigned i=0;i<3;++i){assert(art[i].valid && art[i].kind==NativeWristArtPublisher::kinds[i]);
        assert(art[i].owner==address && art[i].geometry.display_node==reinterpret_cast<std::uintptr_t>(nodes[i].data()));
        assert(art[i].geometry.width==100+10*i && art[i].geometry.fields==31);}
    const auto count=constructs;assert(!observe(true)[0].valid && !observe(false,0x6749bb)[0].valid && !observe(false,0x6749ba,0)[0].valid && constructs==count);
    visible=false;assert(!observe()[0].valid);visible=true;
    bad_node=true;assert(!observe()[0].valid);bad_node=false;
    put(fixture_element.data(),base+0x1c390f8);assert(!observe()[0].valid);put(fixture_element.data(),base+0x1c39598);
    replace_file=true;assert(!observe()[0].valid);replace_file=false;put(fixture_file.data()+0x1608,identity);
    put(nodes[1].data()+0x80,reinterpret_cast<std::uintptr_t>(nodes[1].data()));art=observe();
    assert(art[0].valid && !art[1].valid && art[2].valid);
    WristHudDrawIdentity::clear();assert(!observe()[0].valid);
    assert(constructs==destroys && reads>0);
    std::cout<<"native individual wrist artwork publisher checks passed\n";
}

