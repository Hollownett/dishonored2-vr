#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_crosshair.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace d2vr;
static void require(bool passed,const char* message) {if(!passed) throw std::runtime_error(message);}

// Pinned steam-win64-20250206 RVAs, hardcoded here so the check never reads the
// class's own layout constants.
static constexpr std::uintptr_t update_rva=0x672f70,setter_call_rva=0x67318f,setter_rva=0x2a2c0;
static constexpr std::uintptr_t stage_x_rva=0x3989eac,stage_y_rva=0x3989f2c;
static constexpr std::uintptr_t movie_offset=0x98,object_offset=0x3a8,x_member_offset=0x660,y_member_offset=0x670,latch_offset=0x94;
static constexpr std::size_t module_size=0x3990000;

static std::uintptr_t base{};
static unsigned creates{},enables{},disables{},fail_enable{};
static void* detour{};
static bool bad_read{},bad_write{};
static UINT logic_value=2;

static unsigned setter_calls{};
static void* set_object{};
static std::array<void*,4> set_members{};
static std::array<float,4> set_values{};
static void __fastcall fake_setter(void* object,const float* value,void* member) {
    if(setter_calls<set_members.size()) {set_members[setter_calls]=member;set_values[setter_calls]=value?*value:0.0f;}
    set_object=object;++setter_calls;
}
static void __fastcall original(void*) {}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID hook,LPVOID* out) {
    ++creates;detour=hook;*out=reinterpret_cast<void*>(&original);return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {++enables;return enables==fail_enable?MH_ERROR_NOT_EXECUTABLE:MH_OK;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {++disables;return MH_OK;}

static bool read(std::uintptr_t at,void* out,std::size_t size) {
    if(bad_read && at==base+update_rva) return false;
    std::memcpy(out,reinterpret_cast<const void*>(at),size);return true;
}
static bool write(std::uintptr_t at,const void* data,std::size_t size) {
    if(bad_write) return false;
    std::memcpy(reinterpret_cast<void*>(at),data,size);return true;
}
static UINT logic() {return logic_value;}
static void call(void* element) {reinterpret_cast<void(__fastcall*)(void*)>(detour)(element);}
static void bytes_at(std::uintptr_t at,const char* hex) {
    auto* out=reinterpret_cast<unsigned char*>(at);
    while(*hex) {char part[]{hex[0],hex[1],0};*out++=static_cast<unsigned char>(std::strtoul(part,nullptr,16));hex+=2;}
}
static void set_call_site(bool bad) {
    auto* call_site=reinterpret_cast<unsigned char*>(base+setter_call_rva);
    call_site[0]=0xe8;
    const auto displacement=static_cast<std::int32_t>(base+setter_rva+(bad?1:0)-(base+setter_call_rva+5));
    std::memcpy(call_site+1,&displacement,4);
}
static void set_stage(float x,float y) {
    std::memcpy(reinterpret_cast<void*>(base+stage_x_rva),&x,4);
    std::memcpy(reinterpret_cast<void*>(base+stage_y_rva),&y,4);
}
struct Element {alignas(8) std::array<std::byte,0x700> bytes{};};
static void make_element(Element& element) {
    const std::uintptr_t movie=0x1000,object=reinterpret_cast<std::uintptr_t>(element.bytes.data());
    std::memcpy(element.bytes.data()+movie_offset,&movie,8);
    std::memcpy(element.bytes.data()+object_offset,&object,8);
    element.bytes[latch_offset]=std::byte{1};
}
static std::byte latch(const Element& element) {return element.bytes[latch_offset];}
static void set_latch(Element& element,std::byte value) {element.bytes[latch_offset]=value;}

int main() {
    try {
        // The engine's own stage mapping, and the projection the integrator publishes.
        require(crosshair_stage_position(1.0f,1.0f,1920.0f,1080.0f)==std::array<float,2>{960.0f,-540.0f},"Stage bottom-right corner");
        require(crosshair_stage_position(0.0f,0.0f,1920.0f,1080.0f)==std::array<float,2>{0.0f,0.0f},"Stage centre");
        require(crosshair_stage_position(-0.75f,0.5f,2000.0f,1000.0f)==std::array<float,2>{-750.0f,-250.0f},"Stage quarter point");
        const std::array<float,16> projection{1,0,0,0, 0,1,0,0, 0,0,-1,-1, 0,0,-1,0};
        const float front[3]{2.0f,3.0f,-4.0f};
        const auto ndc=project_to_ndc(projection.data(),front);
        require(ndc && std::abs((*ndc)[0]-0.5f)<1e-6f && std::abs((*ndc)[1]-0.75f)<1e-6f,"Projection divides by w");
        require(!project_to_ndc(projection.data(),std::array<float,3>{0.0f,0.0f,4.0f}.data()),"Point behind the eye is rejected");
        require(!project_to_ndc(projection.data(),std::array<float,3>{2.0f,3.0f,0.0f}.data()),"Point on the eye plane is rejected");
        const float nan_point[3]{std::numeric_limits<float>::quiet_NaN(),0.0f,-4.0f};
        require(!project_to_ndc(projection.data(),nan_point),"Non-finite point is rejected");
        auto broken=projection;broken[5]=std::numeric_limits<float>::infinity();
        require(!project_to_ndc(broken.data(),front),"Non-finite matrix is rejected");
        require(!project_to_ndc(nullptr,front),"Null matrix is rejected");
        const auto world=project_to_ndc(projection.data(),std::array<float,3>{-1.0f,2.0f,-8.0f}.data());
        require(world && crosshair_stage_position((*world)[0],(*world)[1],1920.0f,1080.0f)==std::array<float,2>{-120.0f,-135.0f},"World point to stage placement");

        base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,module_size,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        require(base!=0,"Module allocation");
        bytes_at(base+update_rva,"488bc44889702055488d68a84881ec500100004883b99800000000488bf10f84");
        set_call_site(false);
        set_stage(1920.0f,1080.0f);
        auto* setter=reinterpret_cast<unsigned char*>(base+setter_rva);
        // mov rax, fake_setter; jmp rax: the module can land more than 2 GB away.
        setter[0]=0x48;setter[1]=0xb8;
        const auto target=reinterpret_cast<std::uintptr_t>(&fake_setter);
        std::memcpy(setter+2,&target,8);
        setter[10]=0xff;setter[11]=0xe0;

        Element element;make_element(element);
        NativeCrosshair crosshair;
        bool refused=false;
        set_call_site(true);
        try {crosshair.install(base,read,write,logic,L"build/native-crosshair-install-test.csv");}
        catch(const std::runtime_error&) {refused=true;}
        require(refused && creates==0 && enables==0,"A wrong setter call target is refused before any hook");
        set_call_site(false);
        auto* prologue=reinterpret_cast<unsigned char*>(base+update_rva);
        const auto saved=*prologue;*prologue=0x90;refused=false;
        try {crosshair.install(base,read,write,logic,L"build/native-crosshair-install-test.csv");}
        catch(const std::runtime_error&) {refused=true;}
        require(refused && creates==0,"A changed update prologue is refused");
        *prologue=saved;bad_read=true;refused=false;
        try {crosshair.install(base,read,write,logic,L"build/native-crosshair-install-test.csv");}
        catch(const std::runtime_error&) {refused=true;}
        require(refused && creates==0,"An unreadable prologue is refused");
        bad_read=false;

        fail_enable=enables+1;refused=false;
        try {crosshair.install(base,read,write,logic,L"build/native-crosshair-install-test.csv");}
        catch(const std::runtime_error&) {refused=true;}
        require(refused && creates==1 && enables==1 && !crosshair.routing(),"Enable failure rolls back to dormant");
        require(crosshair.disable() && disables==0,"An unenabled hook needs no disengage");

        fail_enable=0;
        crosshair.install(base,read,write,logic,L"build/native-crosshair-install-test.csv");
        require(creates==1 && enables==2 && !crosshair.routing() && detour,"Install is default-off");
        call(element.bytes.data());
        require(crosshair.calls()==1 && setter_calls==0 && crosshair.applied()==0 && crosshair.skipped()==1,"Default-off hook writes nothing");
        require(crosshair.disable() && disables==1 && !crosshair.routing(),"Clean default-off disable");

        crosshair.install(base,read,write,logic,L"build/native-crosshair-test.csv",true);
        require(crosshair.routing() && creates==1 && enables==3,"Explicit route install");

        setter_calls=0;set_object=nullptr;set_latch(element,std::byte{1});
        crosshair.publish(0.5f,-0.25f,1,GetTickCount64(),true);
        call(element.bytes.data());
        require(setter_calls==2 && crosshair.applied()==1 && crosshair.latch_clears()==1,"Hand NDC reaches the engine setter");
        require(set_object==reinterpret_cast<void*>(element.bytes.data()),"The cached AS3 object is the setter receiver");
        require(set_members[0]==element.bytes.data()+x_member_offset && std::abs(set_values[0]-480.0f)<1e-3f,"x uses member +0x660");
        require(set_members[1]==element.bytes.data()+y_member_offset && std::abs(set_values[1]-135.0f)<1e-3f,"y uses member +0x670");
        require(latch(element)==std::byte{0},"The centred latch is cleared for the handback");

        setter_calls=0;set_latch(element,std::byte{1});
        crosshair.publish(0.5f,-0.25f,1,GetTickCount64()-1000,true);
        call(element.bytes.data());
        crosshair.publish(0.5f,-0.25f,1,GetTickCount64(),false);
        call(element.bytes.data());
        crosshair.publish(0.5f,-0.25f,0,GetTickCount64(),true);
        call(element.bytes.data());
        require(setter_calls==0 && crosshair.skipped()==3,"Stale, inactive and unepoched publications write nothing");
        require(latch(element)==std::byte{1},"A non-owning frame leaves the engine's latch alone");

        set_stage(1000.0f,2000.0f);setter_calls=0;
        crosshair.publish(1.0f,0.0f,2,GetTickCount64(),true);
        call(element.bytes.data());
        require(setter_calls==2 && std::abs(set_values[0]-500.0f)<1e-3f,"Live stage extents are re-read");
        setter_calls=0;
        crosshair.publish(2.0f,-2.0f,3,GetTickCount64(),true);
        call(element.bytes.data());
        require(setter_calls==2 && std::abs(set_values[0]-500.0f)<1e-3f && std::abs(set_values[1]-1000.0f)<1e-3f,"Off-screen NDC parks on the stage edge");
        const auto rejected_before=crosshair.rejected();setter_calls=0;
        crosshair.publish(std::numeric_limits<float>::quiet_NaN(),0.0f,4,GetTickCount64(),true);
        call(element.bytes.data());
        require(setter_calls==0 && crosshair.rejected()==rejected_before+1,"Non-finite NDC is rejected");
        logic_value=1;setter_calls=0;
        crosshair.publish(0.0f,0.0f,5,GetTickCount64(),true);
        call(element.bytes.data());
        require(setter_calls==0 && crosshair.skipped()==4,"Native logic away from gameplay blocks the write");
        logic_value=2;

        const std::uintptr_t zero=0;
        std::memcpy(element.bytes.data()+object_offset,&zero,8);set_latch(element,std::byte{1});setter_calls=0;
        const auto clears_before=crosshair.latch_clears();
        crosshair.publish(0.0f,0.0f,6,GetTickCount64(),true);
        call(element.bytes.data());
        require(setter_calls==0 && crosshair.latch_clears()==clears_before+1 && latch(element)==std::byte{0},"A released AS3 object is never written through");
        const std::uintptr_t object=reinterpret_cast<std::uintptr_t>(element.bytes.data());
        std::memcpy(element.bytes.data()+object_offset,&object,8);
        std::memcpy(element.bytes.data()+movie_offset,&zero,8);set_latch(element,std::byte{1});setter_calls=0;
        const auto clear_mark=crosshair.latch_clears();
        crosshair.publish(0.0f,0.0f,7,GetTickCount64(),true);
        call(element.bytes.data());
        require(setter_calls==0 && crosshair.latch_clears()==clear_mark && latch(element)==std::byte{1},"A null movie is left completely untouched");
        const std::uintptr_t movie=0x1000;std::memcpy(element.bytes.data()+movie_offset,&movie,8);

        bad_write=true;set_latch(element,std::byte{1});setter_calls=0;
        crosshair.publish(0.25f,0.5f,8,GetTickCount64(),true);
        call(element.bytes.data());
        require(setter_calls==2 && crosshair.latch_failures()==1,"A latch failure is counted, not fatal");
        bad_write=false;

        require(crosshair.disable() && disables==2 && !crosshair.routing(),"Clean routed disable");
        setter_calls=0;const auto final_clears=crosshair.latch_clears();
        crosshair.publish(0.5f,-0.25f,9,GetTickCount64(),true);
        call(element.bytes.data());
        require(setter_calls==0 && crosshair.latch_clears()==final_clears && crosshair.applied()>0,"A disabled hook writes nothing");
        std::cout<<"PASS: stage mapping, projection rejection, prologue and setter-call verification, default-off install, "
            "freshness gate, member writes, live stage extents, edge clamp, latch handback and clean disable\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
