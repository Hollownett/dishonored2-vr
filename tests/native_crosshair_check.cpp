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
static constexpr std::uintptr_t movie_offset=0x98,object_offset=0x3a8,x_member_offset=0x660,y_member_offset=0x670,latch_offset=0x94,as3_data_offset=0x18,as3_count_offset=0x20;
static constexpr std::size_t module_size=0x3990000;

static std::uintptr_t base{};
static unsigned creates{},enables{},disables{},fail_enable{};
static void* detour{};
static bool bad_read{},bad_write{};
static UINT logic_value=2;

static unsigned setter_calls{};
static bool setter_faults{};
static void* set_object{};
static std::array<void*,4> set_members{};
static std::array<float,4> set_values{};
static std::uint64_t __fastcall fake_setter(void* object,const float* value,void* member) {
    if(setter_calls<set_members.size()) {set_members[setter_calls]=member;set_values[setter_calls]=value?*value:0.0f;}
    set_object=object;++setter_calls;
    if(setter_faults) *reinterpret_cast<volatile int*>(0x10)=1; // an access violation inside the engine setter
    return 1;
}
static void __fastcall original(void*) {}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID hook,LPVOID* out) {
    ++creates;detour=hook;*out=reinterpret_cast<void*>(&original);return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {++enables;return enables==fail_enable?MH_ERROR_NOT_EXECUTABLE:MH_OK;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {++disables;return MH_OK;}

static unsigned read_count{};
static bool read(std::uintptr_t at,void* out,std::size_t size) {
    ++read_count;
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
    // object_offset is an embedded arkIggyAS3Object: +0x00 vtable (never null), +0x18 idList data, +0x20 count (low 24 bits).
    const std::uintptr_t vtable=0x2000,data=object+object_offset+0x28,iggy=0x3000;const std::uint32_t count=1;
    std::memcpy(element.bytes.data()+object_offset,&vtable,8);
    std::memcpy(element.bytes.data()+object_offset+8,&iggy,8);
    std::memcpy(element.bytes.data()+object_offset+as3_data_offset,&data,8);
    std::memcpy(element.bytes.data()+object_offset+as3_count_offset,&count,4);
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

        {   // fitted-stage mapping (G4): the visible stage is stage_x wide and stage_x*eye_h/eye_w tall
            for(const float nx:{-1.f,-.5f,0.f,.5f,1.f}) for(const float ny:{-1.f,-.5f,0.f,.5f,1.f}) {
                const auto fitted=crosshair_fitted_stage_position(nx,ny,1920.f,1920.f,1080.f),engine=crosshair_stage_position(nx,ny,1920.f,1080.f);
                require(std::abs(fitted[0]-engine[0])<1e-4f && std::abs(fitted[1]-engine[1])<1e-4f,"fitted_matches_engine_on_16_9");
            }
            const auto square=crosshair_fitted_stage_position(.5f,.5f,1920.f,2000.f,2000.f);
            require(std::abs(square[0]-480.f)<1e-3f && std::abs(square[1]+480.f)<1e-3f,"fitted_square_eye");
            const auto tall=crosshair_fitted_stage_position(0.f,1.f,1920.f,1832.f,1920.f);
            require(std::abs(tall[1]+1006.11f)<.01f,"fitted_tall_eye");
            const auto nan=std::numeric_limits<float>::quiet_NaN();
            for(const auto bad:{std::array<float,2>{0.f,0.f},{-1.f,1080.f},{1920.f,0.f},{nan,1080.f},{1920.f,std::numeric_limits<float>::infinity()}}) {
                const auto fallback=crosshair_fitted_stage_position(.5f,-.25f,1920.f,bad[0],bad[1]),engine=crosshair_stage_position(.5f,-.25f,1920.f,1080.f);
                require(fallback==engine,"fitted_bad_size_falls_back");
            }
            const auto shift=crosshair_eye_shift_px({120.f,-40.f},{100.f,-50.f},1.5f);
            require(std::abs(shift[0]-30.f)<1e-4f && std::abs(shift[1]-15.f)<1e-4f,"eye_shift_px");
            require(std::abs(crosshair_distance(.1)-3.f)<1e-4f && crosshair_distance(.001)==.5f && crosshair_distance(std::nullopt)==30.f &&
                    crosshair_distance(2.)==30.f,"distance_from_cast");
        }

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
        require(setter_calls==2 && crosshair.applied()==1 && crosshair.latch_clears()==0,"Hand NDC reaches the engine setter");
        require(set_object==reinterpret_cast<void*>(element.bytes.data()+object_offset),"The cached AS3 object FIELD ADDRESS is the pinned setter receiver");
        require(set_members[0]==element.bytes.data()+x_member_offset && std::abs(set_values[0]-480.0f)<1e-3f,"x uses member +0x660");
        require(set_members[1]==element.bytes.data()+y_member_offset && std::abs(set_values[1]-135.0f)<1e-3f,"y uses member +0x670");
        require(latch(element)==std::byte{1},"The active correction does not change the native centred latch");
        {   // a known eye target size scales y by its aspect (1000x2000: the stage is 1920 x 3840 tall)
            crosshair.target_size=+[]() noexcept {return std::array<float,2>{1000.f,2000.f};};
            setter_calls=0;call(element.bytes.data());
            require(std::abs(set_values[0]-480.0f)<1e-3f && std::abs(set_values[1]-0.5f*0.25f*1920.f*2.f)<1e-2f,"place() uses the fitted stage of the eye target");
            crosshair.target_size=nullptr;setter_calls=0;call(element.bytes.data());
            require(std::abs(set_values[1]-135.0f)<1e-3f,"no target size: the 16:9 stage");
        }
        const auto binding=crosshair.binding(GetTickCount64());
        require(binding && crosshair.owns(*binding,GetTickCount64()),"Live element/movie/list identity is owned");
        require(!crosshair.binding(binding->tick+251),"Old element updates cannot authorize permanent draw ownership");
        element.bytes[object_offset+0x28+8]=std::byte{1};
        require(!crosshair.binding(GetTickCount64()),"Changed AS3 record invalidates a node binding even with the same list address");
        element.bytes[object_offset+0x28+8]=std::byte{0};
        const std::uintptr_t changed_movie=0x1001;
        std::memcpy(element.bytes.data()+movie_offset,&changed_movie,8);
        require(!crosshair.owns(*binding,GetTickCount64()),"Replaced movie never inherits old crosshair binding");
        std::memcpy(element.bytes.data()+movie_offset,&binding->movie,8);

        // Per displayed frame the engine is read once: forty HUD child draws asking "who owns the crosshair" cost the reads of one
        // validation, not forty. A new frame validates again; a teardown the update thread saw is never answered from the cache.
        call(element.bytes.data());
        {
            const auto fresh=GetTickCount64();
            crosshair.begin_frame(0);
            const auto base_validations=crosshair.validations();
            auto reads_before=read_count;
            for(unsigned i=0;i<40;++i) require(crosshair.binding(fresh).has_value(),"Uncached binding stays live");
            const auto uncached_reads=read_count-reads_before;
            require(crosshair.validations()-base_validations==40 && uncached_reads>=40*5,"Without a frame number every call validates (old behaviour)");
            crosshair.begin_frame(7);
            reads_before=read_count;
            const auto one=crosshair.binding(fresh);require(one.has_value(),"First call of a frame validates");
            const auto first_reads=read_count-reads_before;
            reads_before=read_count;
            for(unsigned i=0;i<39;++i) require(crosshair.binding(fresh).has_value() && crosshair.owns(*one,fresh),"Cached answer is the validated binding");
            require(read_count==reads_before,"Later calls of the same frame read nothing from the engine");
            require(first_reads*40==uncached_reads && crosshair.cache_hits()>=39,"One validation per frame instead of forty");
            std::cout<<"native_crosshair engine reads per 40 queries: "<<uncached_reads<<" uncached, "<<first_reads<<" with the frame cache\n";
            crosshair.begin_frame(8);
            reads_before=read_count;
            require(crosshair.binding(fresh).has_value() && read_count>reads_before,"The next displayed frame validates again");
            // Teardown within the frame: the engine update sees a null movie, the cached answer must not outlive it.
            const std::uintptr_t no_movie=0;
            std::memcpy(element.bytes.data()+movie_offset,&no_movie,8);
            call(element.bytes.data());
            require(!crosshair.binding(fresh),"A torn-down element is never answered from the cache");
            std::memcpy(element.bytes.data()+movie_offset,&binding->movie,8);
            call(element.bytes.data());
            require(crosshair.binding(GetTickCount64()).has_value(),"A rebuilt element is validated afresh");
            // An answer is never older than cache_max_age_ms even if the frame counter stalls.
            crosshair.begin_frame(9);require(crosshair.binding(GetTickCount64()).has_value(),"Validates");
            Sleep(80);call(element.bytes.data());
            reads_before=read_count;require(crosshair.binding(GetTickCount64()).has_value() && read_count>reads_before,"A stalled frame counter cannot serve an old answer");
            crosshair.begin_frame(0);
        }


        setter_calls=0;set_latch(element,std::byte{1});
        crosshair.publish(0.5f,-0.25f,1,GetTickCount64()-1000,true);
        call(element.bytes.data());
        crosshair.publish(0.5f,-0.25f,1,GetTickCount64(),false);
        call(element.bytes.data());
        crosshair.publish(0.5f,-0.25f,0,GetTickCount64(),true);
        call(element.bytes.data());
        require(setter_calls==0 && crosshair.skipped()==3,"Stale, inactive and unepoched publications write nothing");
        require(latch(element)==std::byte{0} && crosshair.latch_clears()==1,"First stale frame clears the latch exactly once for native handback");

        set_stage(1000.0f,2000.0f);setter_calls=0;
        crosshair.publish(1.0f,0.0f,2,GetTickCount64(),true);
        call(element.bytes.data());
        require(setter_calls==2 && std::abs(set_values[0]-500.0f)<1e-3f,"Live stage extents are re-read");
        setter_calls=0;
        crosshair.publish(2.0f,-2.0f,3,GetTickCount64(),true);
        call(element.bytes.data());
        require(setter_calls==2 && std::abs(set_values[0]-500.0f)<1e-3f && std::abs(set_values[1]-281.25f)<1e-3f,"Off-screen NDC parks on the stage edge (y from the 16:9 fallback of stage_x)");
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
        // A cleared/destructed arkIggyAS3Object keeps its vtable but has an empty id list: the setter would write through
        // data-0x28, so it must never be called.
        const std::uint32_t count_zero=0,count_one=1;
        auto* object_bytes=element.bytes.data()+object_offset;
        std::memcpy(object_bytes+as3_count_offset,&count_zero,4);set_latch(element,std::byte{1});setter_calls=0;
        const auto clears_before=crosshair.latch_clears();const auto rejected_zero=crosshair.rejected();
        crosshair.publish(0.0f,0.0f,6,GetTickCount64(),true);
        call(element.bytes.data());
        require(setter_calls==0 && crosshair.rejected()==rejected_zero+1 && crosshair.latch_clears()==clears_before && latch(element)==std::byte{1},"A zero-count AS3 object is never written through");
        std::memcpy(object_bytes+as3_count_offset,&count_one,4);
        const std::uint32_t count_high=0x01000000;std::memcpy(object_bytes+as3_count_offset,&count_high,4); // only the low 24 bits count
        setter_calls=0;crosshair.publish(0.0f,0.0f,6,GetTickCount64(),true);call(element.bytes.data());
        require(setter_calls==0,"A count whose low 24 bits are zero is empty");
        std::memcpy(object_bytes+as3_count_offset,&count_one,4);
        std::uintptr_t data_saved{};std::memcpy(&data_saved,object_bytes+as3_data_offset,8);
        std::memcpy(object_bytes+as3_data_offset,&zero,8);setter_calls=0;
        crosshair.publish(0.0f,0.0f,6,GetTickCount64(),true);call(element.bytes.data());
        require(setter_calls==0,"A null id-list pointer is never written through");
        std::memcpy(object_bytes+as3_data_offset,&data_saved,8);
        setter_calls=0;crosshair.publish(0.0f,0.0f,6,GetTickCount64(),true);call(element.bytes.data());
        require(setter_calls==2,"A populated AS3 object is written again");
        set_latch(element,std::byte{1});
        std::memcpy(element.bytes.data()+movie_offset,&zero,8);set_latch(element,std::byte{1});setter_calls=0;
        const auto clear_mark=crosshair.latch_clears();
        crosshair.publish(0.0f,0.0f,7,GetTickCount64(),true);
        call(element.bytes.data());
        require(setter_calls==0 && crosshair.latch_clears()==clear_mark && latch(element)==std::byte{1},"A null movie is left completely untouched");
        const std::uintptr_t movie=0x1000;std::memcpy(element.bytes.data()+movie_offset,&movie,8);

        bad_write=true;set_latch(element,std::byte{1});setter_calls=0;
        crosshair.publish(0.25f,0.5f,8,GetTickCount64(),true);
        call(element.bytes.data());
        require(setter_calls==2 && crosshair.latch_failures()==0,"An active correction needs no latch write");
        crosshair.clear();call(element.bytes.data());
        require(crosshair.latch_failures()==1 && latch(element)==std::byte{1},"A failed handback retains restoration intent");
        bad_write=false;const auto retried=crosshair.latch_clears();call(element.bytes.data());
        require(crosshair.latch_clears()==retried+1 && latch(element)==std::byte{0},"Handback retries on next native owned update");

        // A setter that raises an access violation is survived, counted as a rejection, and stops all further writes.
        setter_faults=true;setter_calls=0;const auto applied_mark=crosshair.applied(),rejected_mark=crosshair.rejected();
        crosshair.publish(0.25f,0.5f,10,GetTickCount64(),true);
        call(element.bytes.data());
        require(setter_calls==1 && crosshair.applied()==applied_mark && crosshair.rejected()==rejected_mark+1,"A faulting setter is survived and counted as rejected");
        setter_faults=false;setter_calls=0;
        crosshair.publish(0.25f,0.5f,11,GetTickCount64(),true);
        call(element.bytes.data());
        require(setter_calls==0 && crosshair.applied()==applied_mark,"No further writes after a setter fault until reinstall");

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
