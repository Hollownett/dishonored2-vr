// Pure checks for the DLSS mode logic: the 32-phase Halton jitter and its engine-pattern alternative, mode to
// render-fraction and governor rules, the NGX optimal-size acceptance, the per-session fallback state machine and the
// user-supplied provider lookup. No D3D, NGX, file system or game.
#include "../src/dlss_jitter.hpp"
#include "../src/dlss_mode.hpp"
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

static int failures=0;
static void expect(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}
static bool near(float a,float b,float tolerance=1e-6f) {return std::abs(a-b)<=tolerance;}

static void jitter_sequence() {
    using namespace d2vr;
    expect(dlss_jitter_phase_count==32,"32 phases");
    expect(near(halton(1,2),.5f) && near(halton(2,2),.25f) && near(halton(3,2),.75f) && near(halton(1,3),1.f/3) && near(halton(2,3),2.f/3) && near(halton(3,3),1.f/9) && near(halton(4,3),4.f/9),"radical inverse values");
    expect(near(halton(0,2),0.f),"index 0 is the degenerate zero sample");
    std::set<std::pair<int,int>> distinct;
    for(std::uint32_t phase=0;phase<32;++phase) {
        const auto s=dlss_jitter_px(phase);
        expect(s[0]>=-.5f && s[0]<.5f && s[1]>=-.5f && s[1]<.5f,"sample inside the pixel");
        const auto again=dlss_jitter_px(phase+32);
        expect(again==s,"phase wraps at 32");
        distinct.insert({static_cast<int>(std::lround(s[0]*4096)),static_cast<int>(std::lround(s[1]*4096))});
    }
    expect(distinct.size()==32,"32 distinct samples");
    expect(dlss_jitter_px(0)==std::array<float,2>{0.f,1.f/3-.5f},"phase 0 is Halton index 1");
    expect(dlss_jitter_px(7)==dlss_jitter_px(7),"deterministic");
    // The engine's pattern is 1/16 px steps; the DLSS sample replaces it only while DLSS owns the jitter.
    for(std::uint32_t counter=0;counter<40;++counter) {
        const auto& p=engine_taa_pattern[counter%8];
        expect(taa_sample_px(counter,false)==std::array<float,2>{p[0]/16.f,p[1]/16.f},"engine pattern in pixels");
        expect(taa_sample_px(counter,true)==dlss_jitter_px(counter),"DLSS sample in pixels");
    }
    const auto uv=taa_sample_uv(5,true,2000.f,1000.f);const auto px=taa_sample_px(5,true);
    expect(near(uv.x,px[0]/2000.f) && near(uv.y,px[1]/1000.f),"UV sample divides by the render size");
    const auto engine=taa_sample_uv(5,false,2000.f,1000.f);const auto reference=engine_taa_jitter(5,2000.f,1000.f);
    expect(near(engine.x,reference.x) && near(engine.y,reference.y),"engine UV sample matches game_stereo");
    // A 1/16-px pattern never equals a Halton sample, so a wrong selection would be visible in the capture stats.
    float mean_x=0,mean_y=0;for(std::uint32_t phase=0;phase<32;++phase) {const auto s=dlss_jitter_px(phase);mean_x+=s[0];mean_y+=s[1];}
    expect(std::abs(mean_x/32)<.05f && std::abs(mean_y/32)<.05f,"sequence is centred");
}

static void modes() {
    using namespace d2vr::dlss_mode;
    expect(dlaa==7 && quality==8 && last==8 && generation==5,"mode constants");
    expect(!active(6) && active(7) && active(8) && !active(9) && !active(-1),"active modes");
    expect(!is_quality(7) && is_quality(8) && fsr4(5) && fsr4(6) && !fsr4(7) && !fsr4(2),"quality and FSR 4 modes");
    expect(near(render_fraction(2,.5f),1.f) && near(render_fraction(3,.5f),.77f) && near(render_fraction(4,.5f),.67f) && near(render_fraction(6,.5f),.77f) && near(render_fraction(7,.5f),1.f),"existing fractions untouched");
    expect(near(render_fraction(8,.6667f),.6667f),"DLSS Quality follows the published optimal fraction");
    expect(near(render_fraction(8,0.f),default_quality_fraction) && near(render_fraction(8,1.5f),default_quality_fraction) && near(render_fraction(8,std::nanf("")),default_quality_fraction),"insane optimal fractions fall back to 2/3");
    expect(governor_eligible(3,.77f) && governor_eligible(4,.67f) && governor_eligible(6,.77f),"governor steps FSR scaled modes");
    expect(!governor_eligible(2,1.f) && !governor_eligible(5,1.f) && !governor_eligible(7,1.f) && !governor_eligible(8,.6667f) && !governor_eligible(1,.77f),"governor never steps native, TAA or DLSS modes");
    const auto f=optimal_fraction(1920,1080,1280,720);
    expect(f && near(*f,2.f/3,1e-4f),"optimal 2/3");
    expect(!optimal_fraction(1920,1080,0,720) && !optimal_fraction(0,1080,1280,720),"zero sizes refused");
    expect(!optimal_fraction(1920,1080,1921,1080),"optimal above the output refused");
    expect(!optimal_fraction(1920,1080,1280,540),"anisotropic optimal refused");
    expect(!optimal_fraction(1920,1080,640,360),"optimal below half refused");
    expect(optimal_fraction(1912,1046,1275,697).has_value(),"the laptop's eye size is accepted");
}

static void fallback_machine() {
    using d2vr::DlssFallback;using S=DlssFallback::State;
    DlssFallback fb;
    expect(fb.state()==S::off && !fb.engaged() && !fb.selected(),"starts off");
    expect(fb.select(true) && fb.state()==S::pending && fb.engaged() && fb.wants_feature() && !fb.should_evaluate(),"selecting arms feature creation");
    expect(!fb.select(true),"re-selecting changes nothing");
    fb.note_evaluate(false,9,-1);expect(fb.state()==S::pending && fb.failures()==0,"evaluate results are ignored while pending");
    fb.note_prepare(true);expect(fb.state()==S::active && fb.should_evaluate() && !fb.wants_feature(),"a created feature evaluates");
    fb.note_prepare(false,3);expect(fb.state()==S::active,"prepare results are ignored while active");
    fb.note_rebuild();expect(fb.state()==S::pending,"a resize needs a new feature, not a failure");
    fb.note_prepare(true);fb.note_evaluate(true);expect(fb.state()==S::active && fb.failures()==0,"successful evaluates keep it active");
    fb.note_evaluate(false,27,-2);
    expect(fb.state()==S::failed && fb.failed() && !fb.engaged() && fb.failures()==1 && fb.last_reason()==27 && fb.last_result()==-2,"a failed evaluate parks the session");
    expect(!fb.select(false) && fb.state()==S::failed && !fb.selected(),"deselecting keeps the failure");
    expect(!fb.select(true) && fb.state()==S::failed && fb.selected(),"re-selecting does not retry mid-session");
    fb.note_prepare(true);fb.note_rebuild();expect(fb.state()==S::failed,"nothing revives a failed session");
    fb.session_end();expect(fb.state()==S::off && !fb.selected() && fb.failures()==1,"the next session starts clean, counters kept");
    expect(fb.select(true) && fb.state()==S::pending,"a new session may retry");
    fb.note_prepare(false,16,7);expect(fb.state()==S::failed && fb.failures()==2 && fb.last_reason()==16 && fb.last_result()==7,"a failed create parks the session");
    DlssFallback other;other.select(true);other.note_prepare(true);
    expect(other.select(false) && other.state()==S::off,"deselecting an active mode retires it");
    expect(other.select(true) && other.state()==S::pending,"selecting again rebuilds");
    other.note_device_lost(10,0);expect(other.state()==S::failed,"device loss parks the session");
    DlssFallback idle;idle.note_device_lost();expect(idle.state()==S::off,"device loss while off is nothing");
    DlssFallback lost;lost.select(true);lost.note_prepare(true);lost.note_device_lost(1,2);
    expect(lost.state()==S::failed && lost.last_reason()==1 && lost.last_result()==2,"device loss while active parks the session");
}

static void provider_lookup() {
    using namespace d2vr;
    const std::filesystem::path home=L"C:\\users\\steamuser\\AppData\\Local\\DishonoredVR";
    std::vector<std::wstring> present;
    const auto exists=[&](const std::filesystem::path& p) {for(const auto& e:present) if(e==p.wstring()) return true;return false;};
    expect(!resolve_dlss_provider(home,L"",exists),"nothing configured, nothing found");
    expect(!resolve_dlss_provider(home,L"Z:\\games\\bin\\nvngx_dlss.dll",exists),"configured but missing");
    present.push_back(L"Z:\\games\\bin\\nvngx_dlss.dll");
    auto found=resolve_dlss_provider(home,L"Z:\\games\\bin\\nvngx_dlss.dll",exists);
    expect(found && found->directory==std::filesystem::path(L"Z:\\games\\bin") && found->source==DlssProvider::Source::configured,"configured DLL path");
    found=resolve_dlss_provider(home,L"  \"Z:\\games\\bin\\nvngx_dlss.dll\"\r\n",exists);
    expect(found && found->directory==std::filesystem::path(L"Z:\\games\\bin"),"quotes and line endings trimmed");
    found=resolve_dlss_provider(home,L"Z:\\games\\bin",exists);
    expect(found && found->directory==std::filesystem::path(L"Z:\\games\\bin"),"a folder names its nvngx_dlss.dll");
    found=resolve_dlss_provider(home,L"Z:\\games\\bin\\NVNGX_DLSS.DLL",exists);
    expect(!found,"the name check is case-insensitive but the file must exist under the given spelling");
    present.push_back(L"Z:\\games\\bin\\NVNGX_DLSS.DLL");
    found=resolve_dlss_provider(home,L"Z:\\games\\bin\\NVNGX_DLSS.DLL",exists);
    expect(found && found->directory==std::filesystem::path(L"Z:\\games\\bin"),"upper-case provider name accepted");
    present.push_back(L"Z:\\games\\bin\\other.dll");
    expect(!resolve_dlss_provider(home,L"Z:\\games\\bin\\other.dll",exists),"only nvngx_dlss.dll is a provider");
    expect(!resolve_dlss_provider(home,L"games\\bin\\nvngx_dlss.dll",exists),"relative paths refused");
    present.push_back((home/L"nvngx_dlss.dll").wstring());
    found=resolve_dlss_provider(home,L"Z:\\games\\bin\\nvngx_dlss.dll",exists);
    expect(found && found->directory==home && found->source==DlssProvider::Source::mod_home,"the mod home copy wins");
    expect(trim_dlss_path(L"'  x  '")==L"x" && trim_dlss_path(L"")==L"" && trim_dlss_path(L"\t\n")==L"","trim");
    expect(is_dlss_provider_name(L"a\\nvngx_dlss.dll") && is_dlss_provider_name(L"NvNgx_DLSS.dll") && !is_dlss_provider_name(L"nvngx_dlss.dll.bak") && !is_dlss_provider_name(L"nvngx.dll"),"provider name");
}

int main() {
    jitter_sequence();modes();fallback_machine();provider_lookup();
    if(!failures) std::printf("PASS: DLSS jitter sequence, mode rules, fallback state machine and provider lookup\n");
    return failures?1:0;
}
