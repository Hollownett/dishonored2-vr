#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_hud_census.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace d2vr;
static void require(bool passed,const char* message) {if(!passed) throw std::runtime_error(message);}

// Pinned steam-win64-20250206 values, hardcoded here so the check never reads the
// class's own layout constants.
static constexpr std::uintptr_t setter_rva=0x2a2c0;
static constexpr std::size_t module_size=0x2c000,detail_limit=20000;
static constexpr std::uintptr_t stub_rva=0x1000,stub_caller=0x1010,jit_stub_rva=0x1040,jit_caller=0x1050;
// Crosshair field arithmetic from the decompile: the helpers 0x672E20/0x672DB0
// write element+0x3A8 through the handles at +0x660/+0x670, the update's own call
// at 0x67318F writes element+0x100 through the handle at +0x650.
static constexpr std::uintptr_t element_object=0x3a8,element_x_member=0x660,element_y_member=0x670,
    second_object=0x3b0,second_member=0x3b8,label_object=0x100,label_member=0x650;
static constexpr const char* prologue_hex="40574883ec5048c7442420feffffff48895c246848896c24704889742478498b";

// MinHook is stubbed so the check exercises the census without the game.
static unsigned creates{},enables{},disables{},fail_enable{};
static void* detour{};
static unsigned original_calls{};
static std::uintptr_t original_object{},original_member{},original_value_address{};
static std::uint64_t __fastcall original(std::uintptr_t object_field,const float* value,void* member) {
    ++original_calls;original_object=object_field;original_member=reinterpret_cast<std::uintptr_t>(member);
    // Never dereference here: the check deliberately passes faulting pointers.
    original_value_address=reinterpret_cast<std::uintptr_t>(value);
    return 0x5a5a;
}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID hook,LPVOID* out) {
    ++creates;detour=hook;*out=reinterpret_cast<void*>(&original);return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {++enables;return enables==fail_enable?MH_ERROR_NOT_EXECUTABLE:MH_OK;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {++disables;return MH_OK;}

static std::uintptr_t base{};
static bool bad_read{};
static bool read(std::uintptr_t at,void* out,std::size_t size) {
    if(bad_read && at==base+setter_rva) return false;
    std::memcpy(out,reinterpret_cast<const void*>(at),size);return true;
}
using Stub=std::uint64_t (__fastcall*)(std::uintptr_t,const float*,void*);
// element is the fabricated HUD element the setter is handed; as3 holds the two
// fabricated AS3 objects, their member-record tables and the name strings.
static std::array<std::byte,0x700> element{};
static std::array<std::byte,0x4000> as3{};
static std::uintptr_t field(std::uintptr_t object_offset=element_object) {
    return reinterpret_cast<std::uintptr_t>(element.data())+object_offset;
}
static void* handle(std::uintptr_t delta,std::uintptr_t object_offset=element_object) {
    return reinterpret_cast<void*>(field(object_offset)+delta);
}
static std::array<std::byte,32> prologue() {
    std::array<std::byte,32> bytes{};
    for(std::size_t index=0;index<bytes.size();++index) {
        char part[]{prologue_hex[index*2],prologue_hex[index*2+1],0};
        bytes[index]=static_cast<std::byte>(std::strtoul(part,nullptr,16));
    }
    return bytes;
}
// The module is fabricated from its own PE headers: a .text section that both
// covers 0x2A2C0 and carries IMAGE_SCN_MEM_EXECUTE, and the pinned prologue.
static void write_module() {
    std::memset(reinterpret_cast<void*>(base),0,module_size);
    IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=0x80;
    std::memcpy(reinterpret_cast<void*>(base),&dos,sizeof dos);
    IMAGE_NT_HEADERS64 nt{};
    nt.Signature=IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;nt.FileHeader.NumberOfSections=1;
    nt.FileHeader.SizeOfOptionalHeader=static_cast<WORD>(sizeof(IMAGE_OPTIONAL_HEADER64));
    nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;nt.OptionalHeader.SizeOfImage=static_cast<DWORD>(module_size);
    std::memcpy(reinterpret_cast<void*>(base+0x80),&nt,sizeof nt);
    IMAGE_SECTION_HEADER section{};
    section.VirtualAddress=0x1000;section.Misc.VirtualSize=static_cast<DWORD>(module_size-0x1000);
    section.SizeOfRawData=static_cast<DWORD>(module_size-0x1000);
    section.Characteristics=IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_READ;
    std::memcpy(reinterpret_cast<void*>(base+0x80+sizeof nt),&section,sizeof section);
    const auto bytes=prologue();std::memcpy(reinterpret_cast<void*>(base+setter_rva),bytes.data(),bytes.size());
}
// sub rsp,0x28; mov rax,target; call rax; add rsp,0x28; ret - so the detour's own
// caller lives inside the fabricated module, with a shadow-space reservation so the
// detour may spill its arguments.
static void write_call_stub(std::uintptr_t at,const void* target) {
    auto* code=reinterpret_cast<unsigned char*>(base+at);
    code[0]=0x48;code[1]=0x83;code[2]=0xec;code[3]=0x28;code[4]=0x48;code[5]=0xb8;
    std::memcpy(code+6,&target,8);
    code[14]=0xff;code[15]=0xd0;code[16]=0x48;code[17]=0x83;code[18]=0xc4;code[19]=0x28;code[20]=0xc3;
}
static std::uint64_t call_stub(std::uintptr_t object_field,const float* value,void* member) {
    return reinterpret_cast<Stub>(base+stub_rva)(object_field,value,member);
}
// The JIT frame: a call the image cannot name (this test's own code) whose caller
// is the module stub, so only the census's in-image frame scan can attribute it.
// noinline and the counter keep the compiler from tail-calling the detour, which
// would make the module stub the direct caller and hide the frame under test.
static std::atomic<unsigned> jit_calls{};
static __declspec(noinline) std::uint64_t __fastcall jit(std::uintptr_t object_field,const float* value,void* member) {
    const auto result=reinterpret_cast<Stub>(detour)(object_field,value,member);
    ++jit_calls;
    return result;
}
static std::uint64_t call_jit_stub(std::uintptr_t object_field,const float* value,void* member) {
    return reinterpret_cast<Stub>(base+jit_stub_rva)(object_field,value,member);
}
static std::string slurp(const std::filesystem::path& file) {
    std::ifstream in(file);std::stringstream buffer;buffer<<in.rdbuf();return buffer.str();
}
static std::size_t lines(const std::filesystem::path& file) {
    std::ifstream in(file);std::string line;std::size_t count=0;while(std::getline(in,line)) ++count;return count;
}
static std::string hex_bytes(const std::byte* data,std::size_t size) {
    constexpr char digits[]="0123456789abcdef";std::string out;
    for(std::size_t index=0;index<size;++index) {
        const auto value=std::to_integer<unsigned>(data[index]);
        out+=digits[value>>4];out+=digits[value&0xf];
    }
    return out;
}
static void poke(std::uintptr_t at,std::uint64_t value,std::size_t size) {
    std::memcpy(reinterpret_cast<void*>(at),&value,size);
}
// Two AS3 objects. The first has three members, a one-character name, a long name
// and two fields that must not be read as names (a small integer and a string with
// a binary byte); the second's table pointer is unreadable, so its dump is empty.
static void write_as3() {
    as3={};
    const auto module=reinterpret_cast<std::uintptr_t>(as3.data());
    const auto object=module+0x100,second=module+0x200,table=module+0x800;
    std::memcpy(reinterpret_cast<void*>(module+0x2000),"x",2);
    std::memcpy(reinterpret_cast<void*>(module+0x2020),"SetAS3Value",12);
    const char binary[]{'a','\x01','\0'};std::memcpy(reinterpret_cast<void*>(module+0x2060),binary,sizeof binary);
    std::memset(reinterpret_cast<void*>(module+0x2080),'a',80);
    poke(object+0x18,table,8);poke(object+0x20,3,4);
    poke(second+0x18,1,8);poke(second+0x20,5,4);
    poke(table+0x00,module+0x2000,8);
    poke(table+0x08,0,8);
    poke(table+0x10,7,8);
    poke(table+0x18,module+0x2060,8);
    poke(table+0x20,module+0x2020,8);
    for(std::size_t index=0;index<5;++index) poke(table+0x28+index*8,index+1,8);
    poke(table+0x50,module+0x2080,8);
    poke(reinterpret_cast<std::uintptr_t>(element.data())+element_object,object,8);
    poke(reinterpret_cast<std::uintptr_t>(element.data())+second_object,second,8);
}

// The hardcoded prologue is proven against the pinned executable exactly as it was
// extracted: RVA 0x2A2C0 mapped through the file's own PE section table.
static void check_pinned_prologue() {
    const std::filesystem::path pinned=L"artifacts/re-toolchain/direct-ghidra/Dishonored2-steam17576867.exe";
    std::ifstream in(pinned,std::ios::binary);
    if(!in) {std::cout<<"note: pinned executable absent, the prologue is checked against the hardcoded bytes only\n";return;}
    IMAGE_DOS_HEADER dos{};
    in.read(reinterpret_cast<char*>(&dos),static_cast<std::streamsize>(sizeof dos));
    require(in && dos.e_magic==IMAGE_DOS_SIGNATURE,"The pinned executable has a DOS header");
    in.seekg(static_cast<std::streamoff>(dos.e_lfanew));
    IMAGE_NT_HEADERS64 nt{};
    in.read(reinterpret_cast<char*>(&nt),static_cast<std::streamsize>(sizeof nt));
    require(in && nt.Signature==IMAGE_NT_SIGNATURE && nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC,
        "The pinned executable has a PE32+ header");
    require(nt.FileHeader.NumberOfSections>=1 && nt.FileHeader.NumberOfSections<=16,
        "The pinned section table is small enough to walk");
    std::uintptr_t offset{};
    for(WORD index=0;index<nt.FileHeader.NumberOfSections;++index) {
        IMAGE_SECTION_HEADER section{};
        in.read(reinterpret_cast<char*>(&section),static_cast<std::streamsize>(sizeof section));
        require(static_cast<bool>(in),"The pinned section table is readable");
        const auto extent=static_cast<std::uintptr_t>(std::max(section.Misc.VirtualSize,section.SizeOfRawData));
        if(setter_rva>=section.VirtualAddress && setter_rva+prologue().size()<=section.VirtualAddress+extent)
            offset=static_cast<std::uintptr_t>(section.PointerToRawData)+(setter_rva-section.VirtualAddress);
    }
    require(offset!=0,"The pinned section table maps the setter RVA");
    in.seekg(static_cast<std::streamoff>(offset));
    std::array<std::byte,32> bytes{};
    in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    require(in && bytes==prologue(),"The pinned executable carries the prologue the hook verifies");
}

static void check_constants() {
    require(hud_census_crosshair_x_delta==static_cast<std::int32_t>(element_x_member-element_object) &&
        hud_census_crosshair_y_delta==static_cast<std::int32_t>(element_y_member-element_object) &&
        hud_census_update_delta==static_cast<std::int32_t>(label_member-label_object),
        "The named deltas are the decompiled field arithmetic, not the update call's");
    require(hud_census_update_delta!=hud_census_crosshair_x_delta && hud_census_update_delta!=hud_census_crosshair_y_delta,
        "The update's own call is not one of the crosshair x/y deltas");
    require(hud_census_member_table_offset==0x18 && hud_census_member_index_offset==0x20 &&
        member_record_stride==0x28 && member_record_limit==64 && member_record_fields*8==member_record_stride,
        "The AS3 member-record layout is the one function-14002a8b0 reads");
}

static void check_flag() {
    const std::filesystem::path root=L"build/hud-census-flag";std::error_code error;
    std::filesystem::remove_all(root,error);
    std::filesystem::create_directories(root/L"build",error);
    require(!native_hud_census_requested(root),"The census is off without the flag file");
    {std::ofstream flag(root/L"build"/L"hud-census");flag<<"on";}
    require(native_hud_census_requested(root),"build/hud-census requests the census");
    std::filesystem::remove_all(root,error);
    require(!native_hud_census_requested(root),"A removed flag file stops the census");
}

// Static storage: a table is 4096 groups and the census adds 20000 raw records.
static HudCensusTable table,source;
static NativeHudCensus census;

static void check_table() {
    table.clear();
    table.insert(0x672e20,0x2b8,field(),1.5f);
    table.insert(0x672e20,0x2b8,field(),-2.0f);
    table.insert(0x672e20,0x2b8,field(),0.5f);
    table.insert(0x672e20,0x2b8,field(),std::numeric_limits<float>::quiet_NaN());
    const auto group=table.find(0x672e20,0x2b8);
    require(group.has_value(),"The inserted group is found");
    require(group->count==4 && group->samples==3,"Counts every call and every readable value");
    require(group->last==0.5f && group->minimum==-2.0f && group->maximum==1.5f,"Tracks last/min/max values");
    require(group->objects_seen==1 && group->objects[0]==field() && !group->saturated,"One distinct object field");
    table.insert(0x672db0,0x2c8,field(),9.0f);
    const auto second=table.find(0x672db0,0x2c8);
    require(second.has_value() && second->count==1,"The member delta keys its own group");
    require(table.size()==2 && !table.find(0x67318f,0x2b8),"Absent keys are not created by lookups");

    table.clear();
    for(std::uintptr_t object=1;object<=HudCensusTable::tracked_objects;++object)
        table.insert(0x672e20,0x2b8,object,1.0f);
    const auto tracked=table.find(0x672e20,0x2b8);
    require(tracked.has_value() && tracked->objects_seen==HudCensusTable::tracked_objects,"Every tracked object is distinct");
    table.insert(0x672e20,0x2b8,static_cast<std::uintptr_t>(HudCensusTable::tracked_objects)+1,1.0f);
    const auto saturated=table.find(0x672e20,0x2b8);
    require(saturated.has_value(),"The saturated group is found");
    require(saturated->objects_seen==HudCensusTable::tracked_objects && saturated->saturated,
        "A further object field saturates the distinct count instead of allocating");
    require(saturated->count==HudCensusTable::tracked_objects+1,"A saturated group still counts calls");

    table.clear();
    table.insert(0x672e20,0x2b8,field(),1.0f);
    const auto cleared=table.find(0x672e20,0x2b8);
    require(table.size()==1 && cleared.has_value() && cleared->samples==1,"Cleared tables start from zero");
}

static void check_csv() {
    table.clear();
    table.insert(0x2000,0x2c8,2,0.25f);
    table.insert(0x1000,0x2b8,1,1.0f);
    table.insert(0x1000,0x2b8,1,-2.0f);
    const auto csv=table.csv();
    require(csv.rfind("caller_rva,delta,count,samples,distinct_objects,objects_saturated,last_value,min_value,max_value,"
        "object0,object1,object2,object3\n",0)==0,"CSV header");
    require(csv.find("0x1000,0x2b8,2,2,1,0,-2,-2,1,0x1,0x0,0x0,0x0\n")!=std::string::npos,
        "CSV row keeps order, extremes, hex keys and the receiver field addresses");
    require(csv.find("0x2000,0x2c8,1,1,1,0,0.25,0.25,0.25,0x2,0x0,0x0,0x0\n")!=std::string::npos,"CSV second row");
    require(csv.find("0x1000,0x2b8")<csv.find("0x2000,0x2c8"),"CSV is sorted by caller then delta");

    table.clear();
    table.insert(0x2000,0x2b8,1,std::numeric_limits<float>::quiet_NaN());
    require(table.csv().find("0x2000,0x2b8,1,0,1,0,nan,nan,nan,0x1,0x0,0x0,0x0\n")!=std::string::npos,
        "A group with no readable value reports nan");

    table.clear();
    for(std::uint32_t index=0;index<50;++index)
        for(std::uint32_t repeat=0;repeat<=index;++repeat) table.insert(0x1000+index,0x2b8,1,1.0f);
    const auto summary=table.summary();
    require(summary.rfind("caller=0x1031 delta=0x2b8 count=50 samples=50 objects=1 saturated=0 last=1 min=1 max=1 field0=0x1\n",0)==0,
        "The summary leads with the largest group and names its receiver field");
    require(std::count(summary.begin(),summary.end(),'\n')==40,"The summary keeps the top 40");
    const auto limited=table.summary(2);
    require(table.summary(0).empty() && std::count(limited.begin(),limited.end(),'\n')==2,"The summary honours its limit");
    require(census.summary().rfind("native hud census calls=0 recorded=0 foreign=0 stacked=0 unattributed=0 stack_dropped=0 "
        "odd=0 groups=0 overflow=0 busy=0 stack_groups=0 stack_overflow=0 objects=0 object_saturated=0 dump_busy=0\n",0)==0,
        "The census summary starts with its counters");
}

static void check_overflow() {
    table.clear();
    for(std::uint32_t index=0;index<HudCensusTable::slots;++index)
        require(table.insert(0x400000+index,0x2b8,index+1,1.0f),"Filling the table records every group");
    require(table.size()==HudCensusTable::slots,"The table is full");
    require(!table.insert(0x900000,0x2b8,1,1.0f),"A full table refuses a new group");
    require(table.overflow()==1 && table.size()==HudCensusTable::slots,"The refusal is counted as an overflow");
    const auto existing=table.find(0x400000,0x2b8);
    require(existing.has_value() && existing->count==1,"An existing group is still found in a full table");

    source.clear();source.insert(0xa00000,0x2b8,1,1.0f);
    require(!table.merge(source),"Merging into a full table reports the dropped group");
    require(table.overflow()==2,"A dropped merge increments the overflow");
}

static void check_merge() {
    table.clear();source.clear();
    require(!table.merge(table),"A table cannot merge into itself");
    table.insert(0x1000,0x2b8,1,1.0f);
    source.insert(0x1000,0x2b8,2,3.0f);
    source.insert(0x2000,0x2c8,3,-1.0f);
    source.insert(0x2000,0x2c8,3,-4.0f);
    require(table.merge(source),"A merge into a free table completes");
    const auto merged=table.find(0x1000,0x2b8);
    require(merged.has_value() && merged->count==2 && merged->samples==2,"Merged counts add");
    require(merged->last==3.0f && merged->minimum==1.0f && merged->maximum==3.0f,"Merged extremes span both tables");
    require(merged->objects_seen==2 && !merged->saturated,"Merged object fields are distinct");
    const auto second=table.find(0x2000,0x2c8);
    require(second.has_value() && second->count==2 && second->minimum==-4.0f && second->maximum==-1.0f,"Merged second key");
    require(table.size()==2 && source.size()==2,"Merge leaves the source table alone");
}

static void check_hook() {
    std::error_code error;std::filesystem::create_directories(L"build",error);
    base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,module_size,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    require(base!=0,"Module allocation");
    write_module();
    write_as3();
    bool refused=false;
    // A changed prologue is the same guard the crosshair install uses.
    auto* prologue_byte=reinterpret_cast<unsigned char*>(base+setter_rva);
    const auto saved=*prologue_byte;*prologue_byte=0x90;
    try {census.install(base,read,L"build/hud-census-test.csv");} catch(const std::runtime_error&) {refused=true;}
    require(refused && creates==0 && enables==0,"A changed setter prologue is refused before any hook");
    *prologue_byte=saved;
    bad_read=true;refused=false;
    try {census.install(base,read,L"build/hud-census-test.csv");} catch(const std::runtime_error&) {refused=true;}
    require(refused && creates==0,"An unreadable setter prologue is refused");
    bad_read=false;
    // The section table must both cover the RVA and mark it executable.
    auto* section=reinterpret_cast<IMAGE_SECTION_HEADER*>(base+0x80+sizeof(IMAGE_NT_HEADERS64));
    const auto characteristics=section->Characteristics;section->Characteristics=IMAGE_SCN_MEM_READ;refused=false;
    try {census.install(base,read,L"build/hud-census-test.csv");} catch(const std::runtime_error&) {refused=true;}
    require(refused && creates==0,"A non-executable covering section is refused");
    section->Characteristics=characteristics;
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    const auto magic=dos->e_magic;dos->e_magic=0;refused=false;
    try {census.install(base,read,L"build/hud-census-test.csv");} catch(const std::runtime_error&) {refused=true;}
    require(refused && creates==0,"A module without the pinned DOS signature is refused");
    dos->e_magic=magic;

    fail_enable=enables+1;refused=false;
    try {census.install(base,read,L"build/hud-census-test.csv");} catch(const std::runtime_error&) {refused=true;}
    require(refused && creates==1 && enables==1 && !census.active(),"An enable failure leaves the census dormant");
    require(census.disable() && disables==0 && census.calls()==0,"A dormant census needs no disengage");

    fail_enable=0;
    census.install(base,read,L"build/hud-census-test.csv");
    require(creates==1 && enables==2 && census.active() && detour,"Install enables the read-only detour");
    refused=false;
    try {census.install(base,read,L"build/hud-census-test.csv");} catch(const std::runtime_error&) {refused=true;}
    require(refused,"A second install is refused");
    write_call_stub(stub_rva,detour);
    write_call_stub(jit_stub_rva,&jit);

    float value=1.5f;
    const auto object=field();
    const auto result=call_stub(object,&value,handle(0x2b8));
    require(result==0x5a5a,"The detour returns the original's result unchanged");
    require(original_calls==1 && original_object==object && original_member==field()+0x2b8 &&
        original_value_address==reinterpret_cast<std::uintptr_t>(&value),
        "The original setter sees its arguments unchanged");
    const auto group=census.find(stub_caller,0x2b8);
    require(group.has_value() && group->count==1 && group->samples==1 && group->last==1.5f,
        "A HUD call lands in its caller/delta group");
    require(group->objects_seen==1 && group->objects[0]==object,"The receiver field address is tracked");
    require(census.calls()==1 && census.recorded()==1,"The call is counted and kept as raw detail");
    require(census.dumped_objects()==1,"The AS3 object behind the receiver field is dumped once");

    // A call from outside the pinned image (here, the test's own code) names no
    // element update and must not pollute the aggregate; one frame up is also
    // foreign, so it is counted as unattributed.
    original_calls=0;
    require(reinterpret_cast<Stub>(detour)(object,&value,handle(0x2b8))==0x5a5a,"A direct detour call still forwards");
    const auto untouched=census.find(stub_caller,0x2b8);
    require(census.foreign()==1 && census.calls()==2 && census.recorded()==1 && untouched.has_value() && untouched->count==1,
        "A foreign caller is counted, not aggregated");
    require(census.stacked()==0 && census.unattributed()==1,"A foreign call with no engine frame above it stays unattributed");

    // The same call from one frame deeper: the detour's caller is the test's own
    // jit function and its caller is the module stub, so the walk names the stub.
    require(call_jit_stub(object,&value,handle(0x2b8))==0x5a5a,"The jit path still forwards");
    const auto attributed=census.foreign_find(jit_caller,0x2b8);
    require(jit_calls==1 && census.foreign()==2 && census.stacked()==1 && census.unattributed()==1 && census.stack_dropped()==0,
        "The frame above a foreign caller is walked once and lands inside the image");
    require(attributed.has_value() && attributed->count==1 && attributed->samples==1 && attributed->last==1.5f &&
        attributed->objects[0]==object,"The JIT write is attributed to the engine frame that called it");

    // The second object behind a second receiver field is a distinct dump, and a
    // receiver field whose AS3 pointer is null is not a dump at all.
    require(call_stub(field(second_object),&value,handle(8,second_object))==0x5a5a,"The second receiver field is called");
    require(census.dumped_objects()==2 && census.dump_saturated()==0 && census.dump_busy()==0,
        "A second distinct AS3 object is dumped once");
    require(call_stub(field(label_object),&value,handle(0x550,label_object))==0x5a5a,"The update call site is called");
    require(census.dumped_objects()==2,"A null AS3 pointer adds no dump");

    call_stub(object,nullptr,handle(0x2c8));
    const auto unreadable=census.find(stub_caller,0x2c8);
    require(unreadable.has_value() && unreadable->count==1 && unreadable->samples==0,
        "A null value pointer is counted without a sample");

    auto* denied=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_NOACCESS);
    require(denied!=nullptr,"Denied page allocation");
    call_stub(object,reinterpret_cast<const float*>(denied),handle(0x2d8));
    const auto faulted=census.find(stub_caller,0x2d8);
    require(faulted.has_value() && faulted->count==1 && faulted->samples==0,"A faulting value pointer is caught, not fatal");
    VirtualFree(denied,0,MEM_RELEASE);

    for(unsigned index=0;index<20001;++index) call_stub(object,&value,handle(0x2b8));
    const auto capped=census.find(stub_caller,0x2b8);
    require(census.recorded()==detail_limit,"The raw call detail stops at 20000 calls");
    require(census.calls()==20008,"Every call after that is still counted");
    require(capped.has_value() && capped->count==20002,"The aggregate keeps counting past the detail cap");
    require(census.summary().find("delta=0x2b8")!=std::string::npos &&
        census.summary().find("delta=0x2c8")!=std::string::npos,"The summary names the crosshair deltas");

    require(census.disable() && disables==1 && !census.active(),"Clean census disable");
    std::array<std::byte,32> restored{};
    require(read(base+setter_rva,restored.data(),restored.size()) && restored==prologue(),"The pinned prologue is back");
    require(census.calls()==20008 && census.recorded()==detail_limit,"Disable preserves the counts");
    const auto csv=slurp(L"build/hud-census-test.csv");
    require(csv.rfind("caller_rva,delta,count,samples,distinct_objects,objects_saturated,last_value,min_value,max_value,"
        "object0,object1,object2,object3\n",0)==0,"The aggregate CSV is dumped at disable");
    require(csv.find("0x1010,0x2b8,20002,20002,1,0,1.5,1.5,1.5,0x")!=std::string::npos,
        "The aggregate CSV carries the hooked call site and its receiver field");
    require(csv.find("0x1010,0x2c8,1,0,1,0,nan,nan,nan,0x")!=std::string::npos,"An unreadable group is dumped as nan");
    require(csv.find("0x1010,0x550,1,1,1,0,1.5,1.5,1.5,0x")!=std::string::npos,
        "The update call site is its own delta, not a crosshair x/y row");
    const auto detail=slurp(L"build/hud-census-test-calls.csv");
    require(detail.rfind("caller_rva,delta,object_field,member,value,thread\n",0)==0,"The raw detail CSV is dumped at disable");
    require(detail.find("0x1010,0x2b8,0x")!=std::string::npos && detail.find(",1.5,")!=std::string::npos,
        "The raw detail keeps caller, delta and value");
    require(lines(L"build/hud-census-test-calls.csv")==detail_limit+1,"The raw detail file holds the first 20000 calls");
    const auto stack=slurp(L"build/hud-census-test-stack.csv");
    require(stack.rfind("caller_rva,delta,count,samples,distinct_objects,objects_saturated,last_value,min_value,max_value,"
        "object0,object1,object2,object3\n",0)==0,"The stack attribution CSV is dumped at disable");
    require(stack.find("0x1050,0x2b8,1,1,1,0,1.5,1.5,1.5,0x")!=std::string::npos,
        "The JIT call is dumped against the engine frame above it");

    std::array<std::byte,0x28> record{};
    std::memcpy(record.data(),as3.data()+0x800,record.size());
    const auto objects=slurp(L"build/hud-census-test-objects.csv");
    require(objects.rfind("object,object_field,members,dumped,member,raw,names\n",0)==0,"The AS3 object CSV header");
    require(objects.find(hex_bytes(record.data(),record.size()))!=std::string::npos,
        "The object CSV records the raw 0x28-byte member record as hex");
    require(objects.find("+0x0=x;+0x20=SetAS3Value")!=std::string::npos,
        "The object CSV keeps only the printable name pointers");
    require(objects.find(",3,3,1,")!=std::string::npos && objects.find(",3,3,2,")!=std::string::npos,
        "All three members of the first object are dumped");
    require(objects.find(",5,0,,,")!=std::string::npos,"An object whose table pointer is unreadable dumps as empty");
    require(census.dumped_objects()==2 && census.dump_saturated()==0,"The object dump stays inside its bound");
    require(census.summary().rfind("native hud census calls=20008 recorded=20000 foreign=2 stacked=1 unattributed=1 stack_dropped=0 "
        "odd=0 groups=5 overflow=0 busy=0 stack_groups=1 stack_overflow=0 objects=2 object_saturated=0 dump_busy=0\n",0)==0,
        "The census summary reports the counters and the five deltas");
}

int main() {
    try {
        check_pinned_prologue();
        check_constants();
        check_flag();
        check_table();
        check_csv();
        check_overflow();
        check_merge();
        check_hook();
        std::cout<<"PASS: pinned executable section-table prologue, decompiled crosshair/update member deltas, "
            "census flag, aggregation (insert/merge/overflow/object saturation/nan/receiver fields), CSV and summary "
            "formatting, install verification, unchanged original call, in-module caller attribution, foreign caller "
            "rejection with in-image stack attribution, SEH-safe value reads, 20000-call detail cap, per-object "
            "member-record dumps (raw hex and printable names) and the disable dumps\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
