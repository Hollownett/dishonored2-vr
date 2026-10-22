#include "../src/overlay_dirty.hpp"
#include "../src/xr_cpu_spans.hpp"
#include <cmath>
#include <cstdio>
using namespace d2vr;
static int failures=0;
#define REQUIRE(c,m) do{if(!(c)){std::printf("FAIL: %s (line %d)\n",m,__LINE__);++failures;}}while(0)
int main(){
    // first_frame_always_renders
    {OverlayDirty d;const OverlayKey k{7,3,static_overlay_content()};
     REQUIRE(d.must_render(k,0,0),"a gate that never rendered must render");}
    // static_kinds_render_once (controls card, wheel dot, vignette with colour-scale)
    {OverlayDirty d;const OverlayKey k{7,3,static_overlay_content()};d.rendered(k,1000);
     bool any=false;for(std::uint64_t t=1001;t<60000;t+=11) any=any||d.must_render(k,t,0);
     REQUIRE(!any,"a static overlay never renders again while the swapchain and epoch stay");}
    // never_skip_after_recreate
    {OverlayDirty d;d.rendered({7,3,5},1000);
     REQUIRE(d.must_render({8,3,5},1001,0),"a recreated swapchain (new generation) renders even with identical content");
     REQUIRE(d.must_render({0,3,5},1001,0),"an unknown swapchain generation always renders");
     REQUIRE(!d.must_render({7,3,5},1001,0),"the same swapchain and content does not");
     OverlayDirty u;u.rendered({0,3,5},1000);REQUIRE(u.must_render({0,3,5},1001,0),"an unknown generation never skips, even against itself");}
    // never_skip_after_epoch
    {OverlayDirty d;d.rendered({7,3,5},1000);
     REQUIRE(d.must_render({7,4,5},1001,0),"a new pose epoch renders");}
    // invalidate_forces_a_render (failed, timed-out or half-drawn render; session restart)
    {OverlayDirty d;d.rendered({7,3,5},1000);d.invalidate();
     REQUIRE(d.must_render({7,3,5},1001,0) && !d.valid(),"after invalidate() the next frame renders");
     d.rendered({7,3,5},1002);REQUIRE(!d.must_render({7,3,5},1003,0),"and one render makes it clean again");}
    // content_change_renders_and_unchanged_does_not
    {OverlayDirty d;d.rendered({7,3,5},1000);
     REQUIRE(d.must_render({7,3,6},1001,0),"changed content renders");REQUIRE(!d.must_render({7,3,5},1001,0),"unchanged content does not");}
    // safety_refresh_for_tick_keys
    {OverlayDirty d;d.rendered({7,3,5},1000);
     REQUIRE(!d.must_render({7,3,5},1499,overlay_refresh_ms),"inside the hold: skip");
     REQUIRE(d.must_render({7,3,5},1500,overlay_refresh_ms),"at the hold age: refresh");
     REQUIRE(d.must_render({7,3,5},900,overlay_refresh_ms),"a clock that went backwards refreshes");}
    // world_ui kind
    {const auto a=world_ui_overlay_content(0x1000,50,2,false,1920,1080),b=world_ui_overlay_content(0x1000,50,2,false,1920,1080);
     REQUIRE(a==b,"same facts, same world UI key");
     REQUIRE(a!=world_ui_overlay_content(0x1000,51,2,false,1920,1080),"a new capture draw tick changes it");
     REQUIRE(a!=world_ui_overlay_content(0x2000,50,2,false,1920,1080),"a new capture texture changes it");
     REQUIRE(a!=world_ui_overlay_content(0x1000,50,3,false,1920,1080),"a new panel kind changes it");
     REQUIRE(a!=world_ui_overlay_content(0x1000,50,2,true,1920,1080),"full-height changes it");
     REQUIRE(a!=world_ui_overlay_content(0x1000,50,2,false,1920,1000),"the shown rows change it");}
    // hud window kind
    {const auto a=hud_window_overlay_content(0x1000,50,10,20,300,200);
     REQUIRE(a==hud_window_overlay_content(0x1000,50,10,20,300,200),"same facts, same HUD key");
     REQUIRE(a!=hud_window_overlay_content(0x1000,51,10,20,300,200),"draw tick");
     REQUIRE(a!=hud_window_overlay_content(0x1000,50,11,20,300,200),"crop x (HUD size or FOV changed)");
     REQUIRE(a!=hud_window_overlay_content(0x1000,50,10,20,300,201),"crop height");}
    // wrist kind
    {const WristArtFacts art{0x10,100,0x99,0,0,64,32,true};WristArtFacts mana=art;mana.texture=0x20;mana.tick=101;
     const auto base=wrist_overlay_content(art,nullptr,64,64);
     REQUIRE(base==wrist_overlay_content(art,nullptr,64,64),"same wrist facts, same key");
     WristArtFacts newer=art;newer.tick=101;REQUIRE(base!=wrist_overlay_content(newer,nullptr,64,64),"newer art tick");
     WristArtFacts moved=art;moved.right=65;REQUIRE(base!=wrist_overlay_content(moved,nullptr,64,64),"crop");
     WristArtFacts blend=art;blend.premultiplied=false;REQUIRE(base!=wrist_overlay_content(blend,nullptr,64,64),"alpha mode");
     REQUIRE(base!=wrist_overlay_content(art,&mana,64,64),"a mana half joins the canvas");
     const auto both=wrist_overlay_content(art,&mana,128,64);
     WristArtFacts mana2=mana;mana2.tick=102;REQUIRE(both!=wrist_overlay_content(art,&mana2,128,64),"the mana half's own tick");
     REQUIRE(base!=wrist_overlay_content(art,nullptr,128,64),"canvas size");}
    // settings and vignette kinds
    {REQUIRE(settings_overlay_content(4)==settings_overlay_content(4) && settings_overlay_content(4)!=settings_overlay_content(5),"settings key follows the pixel generation");
     REQUIRE(vignette_overlay_content(true,1)==vignette_overlay_content(true,99),"colour-scale vignette ignores bakes");
     REQUIRE(vignette_overlay_content(false,1)!=vignette_overlay_content(false,2),"baked vignette re-renders after a bake");
     REQUIRE(vignette_overlay_content(false,1)==vignette_overlay_content(false,1),"and not without one");}
    // stats
    {OverlayStats s;
     for(int f=0;f<10;++f) {s.note(OverlayKind::hud_window,f<2);s.note(OverlayKind::hint,false);s.end_frame();}
     REQUIRE(s.frames()==10 && s.released()==2 && s.skipped()==18,"totals");
     REQUIRE(s.releases_per_frame_p50()==0 && s.skips_per_frame_p50()==2,"p50 of releases and skips per frame");
     REQUIRE(s.released(OverlayKind::hud_window)==2 && s.skipped(OverlayKind::hint)==10,"per kind");
     const auto j=s.by_kind_json();REQUIRE(j.find("\"hud_window\":[2,8]")!=std::string::npos,"by_kind json");
     OverlayStats t;for(int f=0;f<5;++f) {t.note(OverlayKind::wrist_left,true);t.note(OverlayKind::wrist_right,true);t.end_frame();}
     REQUIRE(t.releases_per_frame_p50()==2,"two releases a frame");
     OverlayStats e;REQUIRE(e.releases_per_frame_p50()==0 && e.skipped()==0,"empty stats read zero");
     OverlayStats w;for(int f=0;f<2000;++f) {w.note(OverlayKind::dot,f>=1100);w.end_frame();}
     REQUIRE(w.releases_per_frame_p50()==1,"the p50 covers the last 900 frames only");}
    // xr cpu spans (measurement): per-frame sums over both eyes, median and p95 over the window
    {XrCpuSpans s;REQUIRE(s.percentile(XrSpan::release,.5)==0 && s.frames()==0,"empty spans read zero");
     for(int f=0;f<10;++f) {s.add(XrSpan::release,1'000'000);s.add(XrSpan::release,f==9?9'000'000:1'000'000);s.add(XrSpan::copy,-5);s.end_frame();}
     REQUIRE(s.frames()==10,"frames");
     REQUIRE(std::abs(s.percentile(XrSpan::release,.5)-2.0)<1e-4,"release p50 sums both eyes");
     REQUIRE(std::abs(s.percentile(XrSpan::release,.95)-10.0)<1e-4,"release p95 sees the slow frame");
     REQUIRE(s.percentile(XrSpan::copy,.5)==0,"negative durations are ignored");
     REQUIRE(s.json().find("\"eye_release\":[2.0")!=std::string::npos,"json carries [p50,p95] per span");}
    if(!failures) std::puts("PASS: overlay dirty gates render on first use, a new chain or epoch, content change, invalidate and refresh age; stats count releases and skips");
    return failures?1:0;
}
