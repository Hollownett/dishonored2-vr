// Frame-submission hardening (xr_frame_hardening.hpp): the empty-frame policy, the last-good projection, the black quad, the bounded event log.
#include "../src/xr_frame_hardening.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
using namespace d2vr;
namespace {
void require(bool ok,const char* what) {if(!ok) {std::printf("FAIL: %s\n",what);std::exit(1);}}
XrSwapchain chain(std::uintptr_t n) {return reinterpret_cast<XrSwapchain>(n);}
XrSpace space(std::uintptr_t n) {return reinterpret_cast<XrSpace>(n);}
XrCompositionLayerProjection good_projection(std::array<XrCompositionLayerProjectionView,2>& views) {
    for(unsigned eye=0;eye<2;++eye) {
        views[eye]={XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        views[eye].pose.orientation={0,0,0,1};views[eye].pose.position={eye?.03f:-.03f,1.6f,0};
        views[eye].fov={-.9f,.9f,.9f,-.9f};views[eye].subImage.swapchain=chain(0x100+eye);views[eye].subImage.imageRect={{0,0},{2020,2160}};
    }
    XrCompositionLayerProjection p{XR_TYPE_COMPOSITION_LAYER_PROJECTION};p.space=space(7);p.viewCount=2;p.views=views.data();return p;
}
std::string slurp(const char* path) {std::ifstream in(path);return std::string((std::istreambuf_iterator<char>(in)),{});}
}
int main() {
    // session states that show frames
    require(xr_session_shows_frames(XR_SESSION_STATE_FOCUSED) && xr_session_shows_frames(XR_SESSION_STATE_VISIBLE),"focused and visible show frames");
    for(auto st:{XR_SESSION_STATE_IDLE,XR_SESSION_STATE_READY,XR_SESSION_STATE_SYNCHRONIZED,XR_SESSION_STATE_STOPPING,XR_SESSION_STATE_EXITING,XR_SESSION_STATE_LOSS_PENDING,XR_SESSION_STATE_UNKNOWN})
        require(!xr_session_shows_frames(st),"other states do not");
    // reason order = gate order
    EmptyFrameFacts f;
    require(classify_empty_frame(f)==EmptyFrameReason::layers_rejected,"all gates open but nothing sent: the pose guard rejected it");
    f.layer_count=1;require(classify_empty_frame(f)==EmptyFrameReason::none,"a frame with layers has no reason");f.layer_count=0;
    f.should_render=false;f.continuity_usable=false;require(classify_empty_frame(f)==EmptyFrameReason::not_rendering,"runtime said do not render");f.should_render=true;
    require(classify_empty_frame(f)==EmptyFrameReason::continuity_unusable,"reference change / resume");f.continuity_usable=true;
    f.have_sources=false;f.tracked_ok=false;require(classify_empty_frame(f)==EmptyFrameReason::no_eye_sources,"no eye textures");f.have_sources=true;
    require(classify_empty_frame(f)==EmptyFrameReason::tracked_pair_missing,"no tracked stereo pair");f.tracked_ok=true;
    f.alternate_ok=false;f.band_ready=false;require(classify_empty_frame(f)==EmptyFrameReason::alternate_eye_rejected,"alternate-eye kept image missing");f.alternate_ok=true;
    require(classify_empty_frame(f)==EmptyFrameReason::menu_band_not_ready,"menu band chain not ready");
    require(std::string(empty_frame_reason_name(EmptyFrameReason::menu_band_not_ready))=="menu_band_not_ready" && std::string(empty_frame_reason_name(EmptyFrameReason::count))=="unknown","names");
    // the fallback table
    FallbackFacts a;a.state=XR_SESSION_STATE_FOCUSED;a.reason=EmptyFrameReason::menu_band_not_ready;a.last_good_usable=true;a.blank_ready=true;
    require(choose_frame_fallback(a)==FrameFallback::last_projection,"a usable last projection is re-sent first");
    a.last_good_usable=false;require(choose_frame_fallback(a)==FrameFallback::black_quad,"no last projection: black quad");
    a.blank_ready=false;require(choose_frame_fallback(a)==FrameFallback::none,"nothing to send with: stays empty rather than guess");a.blank_ready=true;
    // G2 cutscene hold: no hold replays as before; a black hold never replays; a replay hold only this cutscene's own immersive layer
    require(scene_hold_replays(0,0,0) && scene_hold_replays(0,5,0),"no hold: the last projection as before");
    require(!scene_hold_replays(1,5,5) && !scene_hold_replays(1,0,0),"black hold: never a replay");
    require(scene_hold_replays(2,5,5),"replay hold: this cutscene's own immersive layer");
    require(!scene_hold_replays(2,5,0) && !scene_hold_replays(2,5,4) && !scene_hold_replays(2,0,0),"replay hold: never gameplay's, another scene's or an untagged layer");
    a.last_good_usable=true;a.reason=EmptyFrameReason::continuity_unusable;require(choose_frame_fallback(a)==FrameFallback::black_quad,"after a reference change the old poses are not reused");
    a.reason=EmptyFrameReason::tracked_pair_missing;a.state=XR_SESSION_STATE_VISIBLE;require(choose_frame_fallback(a)==FrameFallback::last_projection,"VISIBLE counts");
    a.state=XR_SESSION_STATE_READY;require(choose_frame_fallback(a)==FrameFallback::none,"READY/idle states may stay empty");a.state=XR_SESSION_STATE_FOCUSED;
    a.should_render=false;require(choose_frame_fallback(a)==FrameFallback::none,"shouldRender false: nothing to show");a.should_render=true;
    a.layer_count=2;require(choose_frame_fallback(a)==FrameFallback::none,"a frame that has layers is never touched");
    // last good projection
    std::array<XrCompositionLayerProjectionView,2> views;auto p=good_projection(views);
    LastGoodProjection last;const std::array<XrSwapchain,2> eyes{chain(0x100),chain(0x101)};
    require(LastGoodProjection::max_age_ms>=1000 && LastGoodProjection::max_age_ms<=10000,"a stale picture is not worth re-sending for long");
    require(!last.usable(eyes,1000,5),"nothing captured yet");
    require(last.capture(p,1000,5) && last.usable(eyes,1000,5) && last.usable(eyes,1000+LastGoodProjection::max_age_ms,5),"captured, fresh");
    require(!last.usable(eyes,1000+LastGoodProjection::max_age_ms+1,5),"too old");
    require(!last.usable(eyes,1000,6),"another reference epoch");
    require(!last.usable({chain(0x100),chain(0x999)},1000,5),"a resized (replaced) eye chain");
    require(!last.usable(eyes,900,5),"clock going backwards is not fresh");
    views[0].pose.position.x=99.f; // the captured copy owns its poses
    const auto* copy=reinterpret_cast<const XrCompositionLayerProjection*>(last.header());
    require(copy->views!=views.data() && copy->views[0].pose.position.x==-.03f && copy->viewCount==2 && copy->space==space(7) && !copy->next,"own poses, copied");
    require(copy->views[1].subImage.swapchain==chain(0x101),"own chains");
    last.reset();require(!last.usable(eyes,1000,5),"reset");
    auto bad=good_projection(views);views[1].pose.orientation={0,0,0,0};require(!last.capture(bad,1,1),"zero quaternion is never kept");
    bad=good_projection(views);views[0].pose.position.y=NAN;require(!last.capture(bad,1,1),"non-finite pose is never kept");
    bad=good_projection(views);views[0].fov={.9f,-.9f,.9f,-.9f};require(!last.capture(bad,1,1),"inverted fov is never kept");
    bad=good_projection(views);views[1].subImage.swapchain=XR_NULL_HANDLE;require(!last.capture(bad,1,1),"missing chain is never kept");
    bad=good_projection(views);bad.viewCount=1;require(!last.capture(bad,1,1),"mono projection is never kept");
    bad=good_projection(views);bad.space=XR_NULL_HANDLE;require(!last.capture(bad,1,1),"missing space is never kept");
    // black quad: passes the same validation the runtime applies
    {XrCompositionLayerQuad q=make_black_quad(space(7),chain(0x300),64,nullptr);
     require(black_quad_valid(q,64) && q.size.width>=4.f && q.pose.position.z==-1.f && q.eyeVisibility==XR_EYE_VISIBILITY_BOTH,"no head: 1 m ahead of the origin, valid");
     XrPosef head{{0,0,0,1},{1.f,1.6f,2.f}};q=make_black_quad(space(7),chain(0x300),64,&head);
     require(black_quad_valid(q,64) && q.pose.position.x==1.f && q.pose.position.y==1.6f && q.pose.position.z==1.f,"identity head looks down -z: quad 1 m ahead of it");
     const float s=std::sqrt(.5f);XrPosef turned{{0,s,0,s},{0,0,0}}; // yaw 90 degrees left: forward is -x
     q=make_black_quad(space(7),chain(0x300),64,&turned);
     require(black_quad_valid(q,64) && std::fabs(q.pose.position.x+1.f)<1e-5f && std::fabs(q.pose.position.z)<1e-5f,"quad follows the head heading");
     XrPosef unnormalized{{0,0,0,2.f},{0,0,0}};q=make_black_quad(space(7),chain(0x300),64,&unnormalized);require(black_quad_valid(q,64),"unnormalized head is normalized");
     XrPosef broken{{NAN,0,0,1},{0,0,0}};q=make_black_quad(space(7),chain(0x300),64,&broken);require(black_quad_valid(q,64) && q.pose.orientation.w==1.f,"non-finite head falls back to identity");
     require(!black_quad_valid(make_black_quad(XR_NULL_HANDLE,chain(0x300),64,nullptr),64),"validation is real: no space is rejected");
     require(!black_quad_valid(make_black_quad(space(7),chain(0x300),128,nullptr),64),"validation is real: rect larger than the chain is rejected");}
    // menu band: pure function of the eye size
    require(menu_band_rows(2020,2160)==1136 && menu_band_rows(1920,1080)==1080 && menu_band_rows(1000,100)==100 && menu_band_rows(0,10)==1 && menu_band_rows(2,2160)==1,"menu band rows");
    // bounded, rate-limited log
    {EventLogLimiter lim(5,2,1000);
     require(lim.admit(0).emit && lim.admit(10).emit,"two per window");
     require(!lim.admit(20).emit && !lim.admit(30).emit,"the rest of the window is suppressed");
     const auto next=lim.admit(1000);require(next.emit && next.suppressed_before==2,"the next window reports what it skipped");
     require(lim.admit(1010).emit && !lim.admit(1011).emit,"window again");
     const auto resumed=lim.admit(5000);require(resumed.emit && resumed.suppressed_before==1 && lim.emitted()==5,"cap is the total of emitted lines");
     const auto final_line=lim.admit(9000);require(final_line.emit && final_line.final_notice,"one notice at the cap");
     require(!lim.admit(9500).emit && !lim.admit(20000).emit && lim.dropped()>=4,"nothing after the notice");
     require(lim.seen()==11,"every call is counted");
     EventLogLimiter back(100,1,1000);require(back.admit(5000).emit && !back.admit(4000).emit,"a clock step back keeps the window, not a flood");}
    {EventLogLimiter flood(120,20,10000);unsigned lines=0;for(unsigned i=0;i<100000;++i) if(flood.admit(i).emit) ++lines;
     require(lines<=121,"100000 events inside 100 s never write more than the cap (+ notice)");}
    // line formats carry what is needed to match vrserver.txt
    {const auto clock=format_clock(10,34,14,748);require(clock=="10:34:14.748","clock");
     const auto line=format_chain_event(true,"Create menu panel surface",2020,1136,false,0xabc,"08:34:14.748","10:34:14.748",0);
     require(line.find("swapchain create")!=std::string::npos && line.find("2020x1136")!=std::string::npos && line.find("in_frame=0")!=std::string::npos &&
             line.find("utc=08:34:14.748")!=std::string::npos && line.find("local=10:34:14.748")!=std::string::npos && line.find("handle=0xabc")!=std::string::npos && line.find("suppressed")==std::string::npos,"create line");
     const auto d=format_chain_event(false,"Destroy replaced eye swapchain",2020,2160,true,0x10,"a","b",3);
     require(d.find("swapchain destroy")!=std::string::npos && d.find("in_frame=1")!=std::string::npos && d.find("suppressed_before=3")!=std::string::npos,"destroy line, in-frame flag and suppressed count");
     const auto e=format_empty_frame_event(5776,XR_SESSION_STATE_FOCUSED,EmptyFrameReason::continuity_unusable,FrameFallback::black_quad,true,true,false,true,true,"u","l",0);
     require(e.find("empty-frame")!=std::string::npos && e.find("frame=5776")!=std::string::npos && e.find("session_state=5")!=std::string::npos && e.find("reason=continuity_unusable")!=std::string::npos &&
             e.find("action=black_quad")!=std::string::npos && e.find("utc=u")!=std::string::npos,"empty-frame line");}
    // wiring pins (run from the repo root)
    const auto xr=slurp("src/game_xr.hpp");
    if(!xr.empty()) {
        const auto has=[&](const char* text) {return xr.find(text)!=std::string::npos;};
        require(has("choose_frame_fallback(") && has("black_fallback_layer_()") && has("last_good_projection_.header()"),"present() applies the fallback");
        require(has("last_layer_submitted=real_layers!=0"),"a fallback frame is not reported as content");
        require(has("ensure_menu_band_chain();ensure_blank_chain();"),"the band and black chains are made by prepare_overlays, between frames");
        require(has("if(context_) ensure_menu_band_chain();"),"resize() rebuilds the band chain at its final size");
        require(has("menu_band_rows(width_,height_,menu_band)"),"menu_view uses the pure row count");
        require(has("log_chain_event_(true,name,info.width,info.height,chain)") && has("log_chain_event_(false,name,size[0],size[1],chain)"),"overlay chain create/destroy are logged");
        require(has("log_chain_event_(true,\"Create game eye swapchain\"") && has("log_chain_event_(false,\"Destroy replaced eye swapchain\""),"eye chain create/destroy are logged");
        const auto from=xr.find("std::optional<XrCompositionLayerQuad> black_fallback_layer_()"),to=xr.find("void ensure_dot_chain()");
        require(from!=std::string::npos && to!=std::string::npos && from<to,"black fallback is located");
        const auto body=xr.substr(from,to-from);
        require(body.find("xrCreateSwapchain")==std::string::npos && body.find("create_overlay_chain")==std::string::npos && body.find("xrDestroySwapchain")==std::string::npos,"the in-frame fallback never creates or destroys a swapchain (SteamVR rule)");
        const auto fb=xr.find("XrCompositionLayerQuad fallback_quad{}"),end_frame=xr.find("xrEndFrame(session_,&end);XR_FAILED");
        require(fb!=std::string::npos && end_frame!=std::string::npos && fb<end_frame,"the fallback is decided before xrEndFrame");
        const auto guard_at=xr.find("guard_frame_layers(end,kept.data(),kept.size())");
        require(guard_at!=std::string::npos && guard_at<fb,"the fallback runs after the pose guard, so a guard-emptied frame is covered too");
    }
    const auto probe=slurp("src/dxgi_probe.cpp");
    if(!probe.empty()) require(probe.find("game_xr->observe_xr_event=")!=std::string::npos && probe.find("frame_hardening")!=std::string::npos,"log sink and status are wired");
    std::puts("PASS xr frame hardening");
}
