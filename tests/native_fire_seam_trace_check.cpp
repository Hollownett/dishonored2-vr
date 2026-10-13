// Pure decode/format checks for the fire-path probe, plus a compile-only
// surface for the hook parts (MinHook is stubbed exactly as
// tests\native_gameplay_aim_lifecycle_check.cpp does). The fake reader stands in
// for the pinned .text so a single matching prologue proves that one failed
// match retires only that hook.
#include "../src/native_fire_seam_trace.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static unsigned hook_calls=0,remove_calls=0;static MH_STATUS hook_status=MH_ERROR_NOT_INITIALIZED;
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID target,LPVOID* original) {
    ++hook_calls;if(hook_status==MH_OK && original) *original=target;return hook_status;
}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {return hook_status;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {return hook_status==MH_OK?MH_OK:MH_ERROR_DISABLED;}
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID) {++remove_calls;return hook_status;}
static bool no_read(std::uintptr_t,void*,std::size_t) {return false;}
static constexpr std::uintptr_t fake_base=0x140000000;
static const std::array<unsigned char,32> fake_garbage{};
static unsigned fake_match=99;
static bool fake_read(std::uintptr_t address,void* out,std::size_t size) {
    if(size>32) return false;
    for(unsigned i=0;i<d2vr::fire_seam_rvas.size();++i) if(address==fake_base+d2vr::fire_seam_rvas[i]) {
        const auto& bytes=i==fake_match?d2vr::fire_seam_prologues[i]:fake_garbage;
        std::memcpy(out,bytes.data(),size);return true;
    }
    return false;
}
static std::string read_text(const wchar_t* path) {
    std::ifstream in(path,std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
}

static int failures=0;
static void check(bool value,const char* what) {if(!value) {++failures;std::cout<<"FAIL "<<what<<'\n';}}
static bool has(const std::string& text,const char* needle) {return text.find(needle)!=std::string::npos;}
static std::size_t fields(const std::string& line) {std::size_t count=1;for(char c:line) if(c==',') ++count;return count;}

int main() {
    check(d2vr::fire_seam_rvas.size()==7 && d2vr::fire_seam_rvas[0]==0x632f70 &&
          d2vr::fire_seam_rvas[1]==0x639970 && d2vr::fire_seam_rvas[2]==0x638570 &&
          d2vr::fire_seam_rvas[3]==0x867330 && d2vr::fire_seam_rvas[4]==0x62b560 &&
          d2vr::fire_seam_rvas[5]==0x639bc0 && d2vr::fire_seam_rvas[6]==0x88e140,"pinned rvas");
    for(unsigned i=0;i<7;++i) {
        unsigned nonzero=0;for(unsigned char byte:d2vr::fire_seam_prologues[i]) if(byte) ++nonzero;
        check(nonzero>18,"prologue table filled");
    }
    check(d2vr::fire_seam_prologues[0][0]==0x40 && d2vr::fire_seam_prologues[0][1]==0x55,"aim prologue");
    check(d2vr::fire_seam_prologues[1][0]==0x48 && d2vr::fire_seam_prologues[1][1]==0x89 &&
          d2vr::fire_seam_prologues[1][3]==0x24,"output prologue");
    check(d2vr::fire_seam_prologues[2][0]==0x48 && d2vr::fire_seam_prologues[2][1]==0x8b &&
          d2vr::fire_seam_prologues[2][2]==0xc4,"line prologue");
    check(d2vr::fire_seam_prologues[3][0]==0x40 && d2vr::fire_seam_prologues[3][1]==0x53 &&
          d2vr::fire_seam_prologues[3][8]==0x80 && d2vr::fire_seam_prologues[3][14]==0x59,"fire state prologue");
    check(d2vr::fire_seam_prologues[4][0]==0x40 && d2vr::fire_seam_prologues[4][3]==0x48 &&
          d2vr::fire_seam_prologues[4][7]==0x88 && d2vr::fire_seam_prologues[4][11]==0x78 &&
          d2vr::fire_seam_prologues[4][21]==0xff,"launch sibling prologue");
    check(d2vr::fire_seam_prologues[5][0]==0x48 && d2vr::fire_seam_prologues[5][10]==0x56 &&
          d2vr::fire_seam_prologues[5][14]==0x50 && d2vr::fire_seam_prologues[5][17]==0xf0,"validate prologue");
    check(d2vr::fire_seam_prologues[6][0]==0x8b && d2vr::fire_seam_prologues[6][1]==0x81 &&
          d2vr::fire_seam_prologues[6][2]==0xa8 && d2vr::fire_seam_prologues[6][28]==0xcc,"axis prologue");
    check(d2vr::fire_seam_hot(3) && d2vr::fire_seam_hot(6) && !d2vr::fire_seam_hot(0) && !d2vr::fire_seam_hot(5),"hot functions");
    check(d2vr::fire_seam_flush_each(0) && d2vr::fire_seam_flush_each(1) && d2vr::fire_seam_flush_each(4) &&
          d2vr::fire_seam_flush_each(5) && !d2vr::fire_seam_flush_each(2) && !d2vr::fire_seam_flush_each(3) &&
          !d2vr::fire_seam_flush_each(6),"per-row flush set");
    const std::array<float,9> frame{1,2,3,4,5,6,7,8,9};
    const auto forward=d2vr::fire_seam_forward_row(frame);
    check(forward[0]==1 && forward[1]==2 && forward[2]==3,"forward row is the first row");
    check(d2vr::fire_seam_forward_row({})[0]==0,"forward row of a zero frame");
    d2vr::FireSeamRow row{};
    row.call=7;row.tick=1000;row.thread=42;row.site=2;row.function=d2vr::fire_seam_rvas[2];row.caller=0x1234;
    row.a=0x11;row.b=0x22;row.c=0x33;row.axis=0x44;row.flag=0x800;row.result=1;row.value=0.5f;
    row.start={1,2,3};row.end={4,5,6};row.frame=frame;row.camera=0x55;
    row.override_origin={7,8,9};row.override_forward={0,0,1};
    std::ostringstream out;d2vr::fire_seam_write_row(out,row);
    const std::string line=out.str();
    const std::string expected="7,1000,42,line,"+std::to_string(d2vr::fire_seam_rvas[2])+",4660,"
        "17,34,51,68,2048,1,0.5,1,2,3,4,5,6,1,2,3,4,5,6,7,8,9,85,7,8,9,0,0,1\n";
    check(line==expected,"row fields and order");
    check(fields(line)==fields(std::string(d2vr::fire_seam_header())),"header and row agree");
    check(d2vr::fire_seam_header()[std::string(d2vr::fire_seam_header()).size()-1]=='\n',"header newline");
    row.call=9;row.tick=2000;row.thread=7;row.site=6;row.function=d2vr::fire_seam_rvas[6];row.caller=0x10;
    row.a=0xaa;row.b=0xbb;row.c=0xcc;row.axis=0;row.flag=0;row.result=0;row.value=0;
    row.start={1.5f,2.5f,3.5f};row.end={0,0,1};row.frame=frame;row.camera=0;row.override_origin={};row.override_forward={};
    std::ostringstream probe;d2vr::fire_seam_write_row(probe,row);
    check(probe.str()=="9,2000,7,axis,"+std::to_string(d2vr::fire_seam_rvas[6])+
          ",16,170,187,204,0,0,0,0,1.5,2.5,3.5,0,0,1,1,2,3,4,5,6,7,8,9,0,0,0,0,0,0,0\n","axis row fields");
    check(std::string(d2vr::fire_seam_site_name(0))=="aim" && std::string(d2vr::fire_seam_site_name(1))=="output" &&
          std::string(d2vr::fire_seam_site_name(2))=="line" && std::string(d2vr::fire_seam_site_name(3))=="fire" &&
          std::string(d2vr::fire_seam_site_name(4))=="launch" && std::string(d2vr::fire_seam_site_name(5))=="validate" &&
          std::string(d2vr::fire_seam_site_name(6))=="axis" && std::string(d2vr::fire_seam_site_name(7))=="other","site names");
    const std::string text=d2vr::fire_seam_prologue_text(d2vr::fire_seam_prologues[0]);
    check(text.rfind("0x40,0x55,0x57,0x48,",0)==0 && fields(text)==33 &&
          text.rfind("0x74,")==text.size()-5,"prologue text");
    // Compile-only surface: the hook class is complete and installable.
    (void)sizeof(d2vr::NativeFireSeamTrace);
    d2vr::NativeFireSeamTrace trace;
    check(trace.disable() && trace.hooks()==0 && trace.calls()==0 && trace.samples()==0,"no-op disable before install");
    check(!trace.installed(0x632f70) && !trace.installed(0xdead),"installed before install");
    const std::string fresh=trace.summary();
    check(has(fresh,"{\"hooks\":0,\"skipped\":[") && has(fresh,"\"0x632f70\":0") && has(fresh,"\"0x88e140\":0") &&
          has(fresh,"\"samples\":0,\"dropped\":0") && has(fresh,
          "\"callers\":{\"0x88e140\":{\"overflow\":0,\"top\":[]},\"0x638570\":{\"overflow\":0,\"top\":[]}}"),"fresh summary json");
    check(has(fresh,"\"skipped\":[\"0x632f70\",\"0x639970\",\"0x638570\",\"0x867330\",\"0x62b560\",\"0x639bc0\",\"0x88e140\"]"),
          "fresh summary skips every hook");
    bool rejected=false;
    try {trace.install(0,nullptr,L"unused.csv");} catch(const std::runtime_error&) {rejected=true;}
    check(rejected && trace.disable() && hook_calls==0,"missing readers rejected before hooks");
    rejected=false;
    try {trace.install(fake_base,no_read,L"unused.csv");} catch(const std::runtime_error&) {rejected=true;}
    check(rejected && trace.disable() && hook_calls==0 && trace.hooks()==0,"mismatched prologues rejected before hooks");
    check(!trace.installed(0x632f70) && trace.summary().find("\"hooks\":0")!=std::string::npos,"nothing installed after rejection");
    // One matching prologue must leave the other six retired and still install.
    hook_status=MH_OK;fake_match=3;
    const auto partial=L"build\\fire-seam-probe-check.csv";
    trace.install(fake_base,fake_read,partial);
    check(trace.hooks()==1 && trace.installed(0x867330) && !trace.installed(0x632f70) && !trace.installed(0x88e140),"one optional hook only");
    const std::string partial_summary=trace.summary();
    check(has(partial_summary,"{\"hooks\":1,\"skipped\":[") &&
          has(partial_summary,"\"skipped\":[\"0x632f70\",\"0x639970\",\"0x638570\",\"0x62b560\",\"0x639bc0\",\"0x88e140\"]") &&
          has(partial_summary,"\"0x867330\":0") && !trace.installed(0x639970),"partial summary json");
    check(trace.disable() && !trace.installed(0x867330),"disable restores the pinned prologue");
    // The rare functions flush, so the notes are on disk once the log closes.
    const std::string csv=read_text(partial);
    // The CSV is opened in text mode, so every '\n' lands as CRLF on disk.
    const std::string header=d2vr::fire_seam_header();
    check(csv.compare(0,header.size()-1,header,0,header.size()-1)==0,"csv header written");
    check(has(csv,"#skip 0x632f70 prologue_mismatch") && has(csv,"#skip 0x88e140 prologue_mismatch") &&
          has(csv,"#skip 0x62b560 prologue_mismatch") && !has(csv,"#skip 0x867330"),"csv notes the retired hooks");
    check(has(trace.summary(),"\"hooks\":0") && trace.calls()==0 && trace.samples()==0,"idle after disable");
    // Repeated sessions can transfer the retained observer launch hook to the
    // production router. Its next observer installation must create a new one.
    fake_match=1;
    trace.install(fake_base,fake_read,L"build\\fire-seam-transfer.csv");
    check(trace.installed(0x639970),"observer owns shot site");
    check(trace.disable() && trace.release_shot_hook() && remove_calls==1,"disabled observer releases retained shot trampoline");
    check(trace.release_shot_hook() && remove_calls==1,"second transfer is no-op");
    const auto before_recreate=hook_calls;
    trace.install(fake_base,fake_read,L"build\\fire-seam-transfer-again.csv");
    check(hook_calls==before_recreate+1 && trace.installed(0x639970),"retired site is recreated on next observer run");
    check(trace.disable(),"recreated observer drains");
    // Every prologue matched but MinHook refuses: the probe must not pretend.
    hook_status=MH_ERROR_NOT_CREATED;fake_match=6;
    rejected=false;
    try {trace.install(fake_base,fake_read,L"build\\fire-seam-probe-fail.csv");} catch(const std::runtime_error&) {rejected=true;}
    check(rejected && trace.hooks()==0 && !trace.installed(0x88e140) && trace.disable(),"no hook created is a rejection");
    if(failures) {std::cout<<"FAILURES: "<<failures<<'\n';return 1;}
    std::cout<<"PASS: fire seam decodes, row format, prologue table, optional hooks, summary json, install rejection\n";
    return 0;
}
