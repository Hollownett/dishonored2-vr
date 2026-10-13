#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_marker_probe.hpp"
#include <algorithm>
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
static constexpr std::uintptr_t update_rva=0x677b10,project_rva=0x6dda50;
static constexpr std::size_t module_size=0x6e0000;
static constexpr const char* update_prologue_hex="40534883ec2080b99600000000488bd90f85e70000008b490ce8324a06004885";
static constexpr const char* project_prologue_hex="40534883ec30498bd84c8d442420e80dff1a00488b0d26782c03f30f10442420";

static std::uintptr_t base{};
static unsigned creates{},enables{},disables{};
static void* detour_update{};
static void* detour_project{};
static unsigned original_updates{},original_projects{};
static void* fixture_view{};
static std::array<float,3> fixture_output{};
static std::array<std::byte,32> update_prologue(),project_prologue();
static void __fastcall original_update(void*) {++original_updates;}
static void __fastcall original_project(void* view,const void*,void* out) {
    ++original_projects;
    if(view==fixture_view) {
        const std::array<float,3> screen{851.25f,499.5f,.75f};
        std::memcpy(out,screen.data(),sizeof screen);
        const std::int32_t token=102;
        std::memcpy(static_cast<unsigned char*>(view)+0x3ef0,&token,sizeof token);
        const std::array<float,16> matrix{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
        std::memcpy(static_cast<unsigned char*>(view)+0x3eb0,matrix.data(),sizeof matrix);
    }
}
// MinHook is stubbed so the check exercises the probe without the game; the
// original each hook replaces is chosen from the target RVA.
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID target,LPVOID hook,LPVOID* out) {
    ++creates;
    if(reinterpret_cast<std::uintptr_t>(target)==base+update_rva) {detour_update=hook;*out=reinterpret_cast<void*>(&original_update);}
    else {detour_project=hook;*out=reinterpret_cast<void*>(&original_project);}
    return MH_OK;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {++enables;return MH_OK;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {++disables;return MH_OK;}

static bool bad_read{};
static bool read(std::uintptr_t at,void* out,std::size_t size) {
    if(bad_read && (at==base+update_rva || at==base+project_rva)) return false;
    SIZE_T bytes{};return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,size,&bytes) && bytes==size;
}
static std::array<std::byte,32> bytes(const char* hex) {
    std::array<std::byte,32> out{};
    for(std::size_t index=0;index<out.size();++index) {
        char part[]{hex[index*2],hex[index*2+1],0};
        out[index]=static_cast<std::byte>(std::strtoul(part,nullptr,16));
    }
    return out;
}
static std::array<std::byte,32> update_prologue() {return bytes(update_prologue_hex);}
static std::array<std::byte,32> project_prologue() {return bytes(project_prologue_hex);}
// The module is fabricated from its own PE headers: a .text section that covers
// both RVAs and carries IMAGE_SCN_MEM_EXECUTE, and both pinned prologues.
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
    const auto update=update_prologue(),project=project_prologue();
    std::memcpy(reinterpret_cast<void*>(base+update_rva),update.data(),update.size());
    std::memcpy(reinterpret_cast<void*>(base+project_rva),project.data(),project.size());
}
using Update=void (__fastcall*)(void*);
using Project=void (__fastcall*)(void*,const void*,void*);
static std::size_t lines(const std::filesystem::path& file) {
    std::ifstream in(file);std::string line;std::size_t count=0;while(std::getline(in,line)) ++count;return count;
}
// Sums the calls= fields of the "thread=" lines summary() prints, so per-thread
// accounting can be checked against the ring without another accessor.
static std::uint64_t threaded(const std::string& summary) {
    std::istringstream in(summary);std::string line;std::uint64_t total=0;
    while(std::getline(in,line)) {
        if(line.rfind("thread=",0)!=0) continue;
        const auto at=line.find("calls=");
        if(at!=std::string::npos) total+=std::strtoull(line.c_str()+at+6,nullptr,10);
    }
    return total;
}
static DWORD WINAPI worker(LPVOID) {
    for(std::uintptr_t index=0;index<3;++index) reinterpret_cast<Update>(detour_update)(reinterpret_cast<void*>(0x1000+index));
    return 0;
}

// The hardcoded prologues are proven against the pinned executable exactly as
// they were extracted: each RVA mapped through the file's own PE section table.
static void check_pinned_prologues() {
    const std::filesystem::path pinned=L"artifacts/re-toolchain/direct-ghidra/Dishonored2-steam17576867.exe";
    std::ifstream in(pinned,std::ios::binary);
    if(!in) {std::cout<<"note: pinned executable absent, the prologues are checked against the hardcoded bytes only\n";return;}
    IMAGE_DOS_HEADER dos{};
    in.read(reinterpret_cast<char*>(&dos),static_cast<std::streamsize>(sizeof dos));
    require(in && dos.e_magic==IMAGE_DOS_SIGNATURE,"The pinned executable has a DOS header");
    in.seekg(static_cast<std::streamoff>(dos.e_lfanew));
    IMAGE_NT_HEADERS64 nt{};
    in.read(reinterpret_cast<char*>(&nt),static_cast<std::streamsize>(sizeof nt));
    require(in && nt.Signature==IMAGE_NT_SIGNATURE && nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC,
        "The pinned executable has a PE32+ header");
    require(nt.FileHeader.NumberOfSections>=1 && nt.FileHeader.NumberOfSections<=16,"The pinned section table is small enough to walk");
    std::array<IMAGE_SECTION_HEADER,16> sections{};
    for(WORD index=0;index<nt.FileHeader.NumberOfSections;++index) {
        in.read(reinterpret_cast<char*>(&sections[index]),static_cast<std::streamsize>(sizeof(IMAGE_SECTION_HEADER)));
        require(static_cast<bool>(in),"The pinned section table is readable");
    }
    const std::array<std::uintptr_t,2> rvas{update_rva,project_rva};
    const std::array<std::array<std::byte,32>,2> expected{update_prologue(),project_prologue()};
    for(std::size_t which=0;which<rvas.size();++which) {
        std::uintptr_t offset{};
        for(WORD index=0;index<nt.FileHeader.NumberOfSections;++index) {
            const auto& section=sections[index];
            const auto extent=static_cast<std::uintptr_t>(std::max(section.Misc.VirtualSize,section.SizeOfRawData));
            if(rvas[which]>=section.VirtualAddress && rvas[which]+expected[which].size()<=section.VirtualAddress+extent)
                offset=static_cast<std::uintptr_t>(section.PointerToRawData)+(rvas[which]-section.VirtualAddress);
        }
        require(offset!=0,"The pinned section table maps the probe RVA");
        in.seekg(static_cast<std::streamoff>(offset));
        std::array<std::byte,32> code{};
        in.read(reinterpret_cast<char*>(code.data()),static_cast<std::streamsize>(code.size()));
        require(in && code==expected[which],"The pinned executable carries the prologue the hook verifies");
    }
}

static void check_flag() {
    const std::filesystem::path root=L"build/marker-probe-flag";std::error_code error;
    std::filesystem::remove_all(root,error);
    std::filesystem::create_directories(root/L"build",error);
    require(!native_marker_probe_requested(root),"The marker probe is off without the flag file");
    {std::ofstream flag(root/L"build"/L"marker-probe");flag<<"on";}
    require(native_marker_probe_requested(root),"build/marker-probe requests the probe");
    std::filesystem::remove_all(root,error);
    require(!native_marker_probe_requested(root),"A removed flag file stops the probe");
}

// Static storage: the ring and the thread table are a fixed ~64 KB by design.
static NativeMarkerProbe probe;

static void check_install() {
    std::error_code error;std::filesystem::create_directories(L"build",error);
    base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,module_size,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    require(base!=0,"Module allocation");
    write_module();
    // Both prologues wrong: nothing to prove, nothing hooked.
    std::memset(reinterpret_cast<void*>(base+update_rva),0x90,32);
    std::memset(reinterpret_cast<void*>(base+project_rva),0x90,32);
    bool refused=false;
    try {probe.install(base,read,L"build/native-marker-probe.csv");} catch(const std::runtime_error&) {refused=true;}
    require(refused && creates==0 && enables==0,"Two changed prologues are refused before any hook");
    bad_read=true;refused=false;
    try {probe.install(base,read,L"build/native-marker-probe.csv");} catch(const std::runtime_error&) {refused=true;}
    require(refused && creates==0,"Unreadable prologues are refused");
    bad_read=false;
    // One good, one bad: the probe proves and hooks what it can.
    const auto update=update_prologue();
    std::memcpy(reinterpret_cast<void*>(base+update_rva),update.data(),update.size());
    probe.install(base,read,L"build/native-marker-probe.csv");
    require(probe.active() && probe.hooks()==1 && probe.installed(update_rva) && !probe.installed(project_rva) &&
        probe.created(update_rva) && !probe.created(project_rva) && detour_update && !detour_project,
        "A single pinned prologue installs a single hook");
    require(probe.disable() && disables==1,"The single hook disengages cleanly");
    // Both good: both hooks, and the log is the install file.
    const auto project=project_prologue();
    std::memcpy(reinterpret_cast<void*>(base+project_rva),project.data(),project.size());
    probe.install(base,read,L"build/native-marker-probe.csv");
    require(probe.active() && probe.hooks()==2 && detour_update && detour_project,"Both pinned prologues install both hooks");
    require(probe.calls(0)==0 && probe.calls(1)==0 && probe.pushed()==0 && probe.published()==0,
        "A fresh install starts from zero");
}

static void check_calls() {
    // An out-of-range phase is published as 0, the pre-publish phase.
    probe.eye_phase(4);
    require(probe.phase()==0 && probe.published()==1 && probe.phase_publishes(0)==1,
        "A phase outside 1..3 is published as 0");
    reinterpret_cast<Update>(detour_update)(reinterpret_cast<void*>(0x1234));
    require(original_updates==1 && probe.calls(0)==1 && probe.phase_calls(0)==1,
        "An update call is counted against the live phase");
    probe.eye_phase(1);
    require(probe.phase()==1 && probe.published()==2 && probe.phase_publishes(1)==1,"Left-eye begin publishes phase 1");
    reinterpret_cast<Update>(detour_update)(reinterpret_cast<void*>(0x1234));
    reinterpret_cast<Update>(detour_update)(reinterpret_cast<void*>(0x1234));
    fixture_view=reinterpret_cast<void*>(base+0x1000);
    const std::int32_t old_token=101;
    std::memcpy(static_cast<unsigned char*>(fixture_view)+0x3ef0,&old_token,sizeof old_token);
    const std::array<float,3> world{11.25f,22.5f,33.75f};
    reinterpret_cast<Project>(detour_project)(fixture_view,world.data(),fixture_output.data());
    require((fixture_output==std::array<float,3>{851.25f,499.5f,.75f}),"Observer preserves original output exactly");
    require((world==std::array<float,3>{11.25f,22.5f,33.75f}),"Observer preserves original world input exactly");
    require(probe.rows().find(",1,31,11.25,22.5,33.75,851.25,499.5,0.75,101,102,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16\n")!=std::string::npos,
        "Projection telemetry records entry eye, world/output, original lazy-cache update and post-call matrix");
    require(probe.calls(0)==3 && probe.calls(1)==1 && probe.phase_calls(1)==3 && original_projects==1,
        "Updates and the world-to-screen projection fall in the left-eye phase");
    probe.eye_phase(2);
    reinterpret_cast<Project>(detour_project)(reinterpret_cast<void*>(0x2000),nullptr,nullptr);
    require(probe.phase_calls(2)==1 && probe.calls(1)==2,"Right-eye begin publishes phase 2");
    probe.eye_phase(3);
    reinterpret_cast<Update>(detour_update)(reinterpret_cast<void*>(0x5678));
    require(probe.phase_calls(3)==1 && probe.calls(0)==4,"After-present publishes phase 3");
    const auto last=probe.pushed();
    const auto& row=probe.ring()[static_cast<std::size_t>((last-1)%NativeMarkerProbe::ring_slots)];
    require(row.call==last && row.site==0 && row.phase==3 && row.thread==GetCurrentThreadId() && row.tick!=0 &&
        row.marker==0x5678,"The ring keeps the call, tick, thread, phase and marker pointer");
    require(probe.rows().find("world_to_screen")!=std::string::npos && probe.rows().rfind("1,",0)==0,
        "The ring dump names both sites, oldest call first and without a header of its own");
    require(probe.summary().find("phase=0 calls=1 publishes=1\n")!=std::string::npos &&
        probe.summary().find("phase=1 calls=3 publishes=1\n")!=std::string::npos &&
        probe.summary().find("phase=2 calls=1 publishes=1\n")!=std::string::npos &&
        probe.summary().find("phase=3 calls=1 publishes=1\n")!=std::string::npos,
        "The summary reports calls and publications per phase");
    // Two more threads: the per-thread table must hold every one of them.
    std::array<HANDLE,2> threads{};
    for(auto& thread:threads) thread=CreateThread(nullptr,0,&worker,nullptr,0,nullptr);
    for(auto thread:threads) {require(thread!=nullptr,"Worker thread");WaitForSingleObject(thread,10000);CloseHandle(thread);}
    require(probe.calls(0)==10 && probe.pushed()==12,"Every worker call is counted");
    require(threaded(probe.summary())==probe.pushed() && probe.thread_overflow()==0,
        "The summary accounts every call to a thread");
    require(probe.summary().find("thread="+std::to_string(GetCurrentThreadId())+" calls=6\n")!=std::string::npos,
        "The summary names this thread and its calls");
}

static void check_ring_wrap() {
    for(unsigned index=0;index<1100;++index) reinterpret_cast<Update>(detour_update)(reinterpret_cast<void*>(0x1234));
    require(probe.pushed()==1112,"Every wrapped call is still counted");
    const auto rows=probe.rows();
    require(std::count(rows.begin(),rows.end(),'\n')==NativeMarkerProbe::ring_slots,"The ring keeps the newest ring_slots calls");
    require(rows.rfind("89,",0)==0,"The ring dump starts with the oldest surviving call");
}

static void check_disable() {
    require(probe.disable() && disables==3 && !probe.active() && probe.hooks()==0,"Clean marker probe disable");
    std::array<std::byte,32> code{};
    require(read(base+update_rva,code.data(),code.size()) && code==update_prologue(),"The update prologue is back");
    require(read(base+project_rva,code.data(),code.size()) && code==project_prologue(),"The projection prologue is back");
    require(probe.calls(0)==1110 && probe.pushed()==1112,"Disable preserves the counts");
    require(lines(L"build/native-marker-probe.csv")==NativeMarkerProbe::ring_slots+1,"The ring is dumped at disable");
    std::ifstream in(L"build/native-marker-probe.csv");std::string header;
    std::getline(in,header);
    require(header+"\n"==NativeMarkerProbe::header(),"The dumped ring carries the header");
    require(probe.summary().rfind("native marker probe update=1110 world_to_screen=2 phase=3 publishes=4 ring=1112 busy=0 dropped=0 thread_overflow=0\n",0)==0,
        "The summary leads with the counters");
}

int main() {
    try {
        check_pinned_prologues();
        check_flag();
        check_install();
        check_calls();
        check_ring_wrap();
        check_disable();
        std::cout<<"PASS: pinned section-table prologues for 0x677B10/0x6DDA50, marker probe flag, "
            "per-function prologue retirement and install verification, unchanged originals, per-call ring "
            "(tick/thread/phase/pointers), eye-phase publication and clamping, per-phase and per-thread "
            "counts, ring wrap, and the disable dump\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
