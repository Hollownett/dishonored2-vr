// v1006 model lab: the per-frame second-eye shadow setup-redirect state machine.
// No game, no D3D, no engine memory.  Pure decision logic from src/shadow_share_model.hpp.
#include "../src/shadow_share_model.hpp"
#include "../src/shadow_share.hpp"
#include "../src/render_pass_table.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>

namespace ss = d2vr::shadow_share;

static void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}

static ss::ViewSignature sig(std::uintptr_t view,std::uint32_t gate=0,std::uint32_t flags=0) {
    ss::ViewSignature result;result.view=view;result.gate=gate;result.flags=flags;result.valid=view!=0;return result;
}

static const std::array<int,5> writers{ss::pass_prepare,ss::pass_collect,ss::pass_gather,ss::pass_world,ss::pass_dynamic};
static const std::array<int,5> readers{ss::pass_screenshadows,ss::pass_forward_world,ss::pass_forward_dynamic,
    ss::pass_fog,ss::pass_render_water};

// Capture a first-view reader resource.  `base` makes each pass's value distinct so a
// redirect to the wrong pass is visible.
static void capture_readers(ss::Model& model,std::uintptr_t view,std::uintptr_t base) {
    model.observe_shadow_param(ss::pass_screenshadows,view,true,base+25);
    model.observe_shadow_param(ss::pass_forward_world,view,true,base+27);
    model.observe_shadow_param(ss::pass_forward_dynamic,view,true,base+28);
    model.observe_shadow_param(ss::pass_fog,view,true,base+29);
}

// Run a first view's five writers (all must run) and its readers.
static void first_view(ss::Model& model,std::uintptr_t view,std::uintptr_t base) {
    for(const int pass:writers) check(!model.should_suppress(pass,view,false),"first-view writer must run");
    capture_readers(model,view,base);
}

int main() {
    try {
        // OFF: no pair, no suppression, no redirects.
        {
            ss::Model model;
            model.configure(false);
            check(!model.enabled(),"knob off means disabled");
            check(!model.on_eye_begin(0,sig(0xA000)),"knob off refuses a frame");
            check(model.reason()==ss::Reason::disabled,"knob off reason");
            check(!model.should_suppress(ss::pass_world,0xB000,false),"knob off never suppresses");
            std::uintptr_t value=0;
            check(!model.redirect_shadow_param(ss::pass_forward_world,0xB000,value),"knob off never redirects");
            check(model.stats().second_view_skipped_jobs==0,"knob off skips nothing");
        }

        // Atlas-only knob: exactly the v1005 machine.  The second view runs its setup and only
        // the two atlas writers are skipped; no redirect is emitted and setup_fallbacks stays 0.
        {
            ss::Model model;
            model.configure(true,true);
            check(model.atlas_only(),"atlas-only flag");
            check(model.on_eye_begin(0,sig(0xA000)),"atlas-only first eye");
            first_view(model,0xA000,0x1000);
            check(model.on_eye_begin(1,sig(0xB000)),"atlas-only second eye");
            check(!model.should_suppress(ss::pass_prepare,0xB000,false),"atlas-only prepare runs");
            check(model.mode()==ss::Mode::atlas && model.mode_decided(),"atlas-only mode");
            check(!model.should_suppress(ss::pass_collect,0xB000,false),"atlas-only collect runs");
            check(!model.should_suppress(ss::pass_gather,0xB000,false),"atlas-only gather runs");
            check(model.should_suppress(ss::pass_world,0xB000,false),"atlas-only world skipped");
            check(model.should_suppress(ss::pass_dynamic,0xB000,false),"atlas-only dynamic skipped");
            std::uintptr_t value=0;
            check(!model.redirect_shadow_param(ss::pass_forward_world,0xB000,value),"atlas-only never redirects");
            check(model.stats().frames_shared==1 && model.stats().second_view_skipped_jobs==2,"atlas-only counters");
            check(model.stats().setup_fallbacks==0 && model.stats().fallbacks==0,"atlas-only is not a fallback");
            check(model.reason()==ss::Reason::none,"atlas-only reason none");
        }

        // Setup mode: five writers skipped, every reading pass redirected to the first view's
        // resource.  Pass 32 was not captured, so it gets the canonical value; no reader is
        // ever left on its own setup (no mixed frame).
        {
            ss::Model model;
            model.configure(true,false);
            check(model.on_eye_begin(0,sig(0xA000)),"setup first eye");
            first_view(model,0xA000,0x1000);
            check(model.on_eye_begin(1,sig(0xB000)),"setup second eye");
            check(model.should_suppress(ss::pass_prepare,0xB000,false),"setup mode skips all five writers");
            check(model.mode()==ss::Mode::setup && model.mode_decided(),"setup mode selected");
            for(const int pass:writers) if(pass!=ss::pass_prepare) check(model.should_suppress(pass,0xB000,false),"setup mode skips all five writers");
            std::uintptr_t value=0;
            check(model.redirect_shadow_param(ss::pass_forward_world,0xB000,value) && value==0x101b,"forward world redirect value");
            model.note_redirect_applied();
            check(model.redirect_shadow_param(ss::pass_forward_dynamic,0xB000,value) && value==0x101c,"forward dynamic redirect value");
            model.note_redirect_applied();
            check(model.redirect_shadow_param(ss::pass_fog,0xB000,value) && value==0x101d,"fog redirect value");
            model.note_redirect_applied();
            check(model.redirect_shadow_param(ss::pass_screenshadows,0xB000,value) && value==0x1019,"screenshadows redirect value");
            model.note_redirect_applied();
            check(model.redirect_shadow_param(ss::pass_render_water,0xB000,value) && value==0x1019,"uncaptured reader gets canonical value");
            model.note_redirect_applied();
            check(model.stats().setup_redirects==5,"five reader jobs redirected");
            check(model.stats().second_view_skipped_jobs==5,"five writer jobs skipped");
            check(model.stats().fallbacks==0 && model.reason()==ss::Reason::none,"clean setup frame has no fallback");
            // The mixed-setup guard: every reader answered true, so no pass can read its own setup.
            for(const int pass:readers) check(model.redirect_shadow_param(pass,0xB000,value),"every reader is redirected in setup mode");
            check(!model.redirect_shadow_param(ss::pass_prepare,0xB000,value),"a writer is not a redirect reader");
            check(!model.redirect_shadow_param(ss::pass_forward_world,0xA000,value),"the first view is never redirected");
        }

        // No capture at all: fall back to atlas-only for the frame, record setup_unavailable,
        // and still save the two atlas writes.  This is the v1005 behaviour.
        {
            ss::Model model;
            model.configure(true,false);
            check(model.on_eye_begin(0,sig(0xA000)),"fallback first eye");
            check(!model.should_suppress(ss::pass_prepare,0xA000,false),"fallback first prepare");
            check(!model.should_suppress(ss::pass_world,0xA000,false),"fallback first world");
            check(model.on_eye_begin(1,sig(0xB000)),"fallback second eye");
            check(!model.should_suppress(ss::pass_prepare,0xB000,false),"fallback second prepare runs");
            check(model.mode()==ss::Mode::atlas,"fallback selects atlas");
            check(model.stats().setup_fallbacks==1,"setup fallback counted");
            check(model.reason()==ss::Reason::setup_unavailable,"setup fallback reason");
            check(model.should_suppress(ss::pass_world,0xB000,false),"fallback still skips the atlas");
            std::uintptr_t value=0;
            check(!model.redirect_shadow_param(ss::pass_forward_world,0xB000,value),"fallback does not redirect");
            check(model.stats().fallbacks==0,"atlas-only fallback is not a hard frame fallback");
        }

        // One-view frame: nothing to reuse and no mode decision.
        {
            ss::Model model;
            model.configure(true,false);
            check(model.on_eye_begin(0,sig(0xA000)),"mono frame starts");
            first_view(model,0xA000,0x1000);
            check(!model.mode_decided(),"mono frame does not select a reuse mode");
            check(model.stats().frames_shared==0,"mono frame is not shared");
        }

        // Third view after a shared frame: refuse, stop redirecting, and record third_view.
        {
            ss::Model model;
            model.configure(true,false);
            check(model.on_eye_begin(0,sig(0xA000)) && model.on_eye_begin(1,sig(0xB000)),"third-view pair");
            check(model.mode()==ss::Mode::atlas,"third-view pair without capture is atlas");
            check(model.on_eye_begin(0,sig(0xA000)),"third-view setup first eye");
            check(!model.should_suppress(ss::pass_prepare,0xA000,false),"third-view first prepare");
            model.observe_shadow_param(ss::pass_forward_world,0xA000,true,0x101b);
            check(model.on_eye_begin(1,sig(0xB000)),"third-view setup pair");
            check(model.should_suppress(ss::pass_prepare,0xB000,false),"third-view frame suppressed prepare");
            check(!model.on_eye_begin(2,sig(0xC000)),"third view refused");
            check(model.reason()==ss::Reason::third_view,"third view reason");
            std::uintptr_t value=0;
            check(!model.redirect_shadow_param(ss::pass_forward_world,0xB000,value),"third view stops redirects");
        }

        // Writer ordering: in setup mode the second view's PREPARE must not be suppressed before
        // the first view's PREPARE.  The frame falls back hard and later jobs run.
        {
            ss::Model model;
            model.configure(true,false);
            check(model.on_eye_begin(0,sig(0xA000)),"ordering first eye");
            model.observe_shadow_param(ss::pass_forward_world,0xA000,true,0x101b);
            check(model.on_eye_begin(1,sig(0xB000)),"ordering second eye");
            check(!model.should_suppress(ss::pass_prepare,0xB000,false),"second prepare before first runs");
            check(model.mode()==ss::Mode::setup,"ordering selected setup");
            check(model.reason()==ss::Reason::ordering,"ordering reason");
            check(model.stats().fallbacks==1,"ordering hard fallback");
            check(!model.should_suppress(ss::pass_world,0xB000,false),"frame stays normal after ordering fallback");
            std::uintptr_t value=0;
            check(!model.redirect_shadow_param(ss::pass_forward_world,0xB000,value),"no redirects after hard fallback");
            check(!model.should_suppress(ss::pass_world,0xA000,false),"first view world runs after fallback");
        }

        // Cached job: a job that would replay a first-view command list is never suppressed.
        {
            ss::Model model;
            model.configure(true,false);
            check(model.on_eye_begin(0,sig(0xA000)),"cached first eye");
            first_view(model,0xA000,0x1000);
            check(model.on_eye_begin(1,sig(0xB000)),"cached second eye");
            check(!model.should_suppress(ss::pass_prepare,0xB000,true),"cached prepare runs");
            check(model.reason()==ss::Reason::cached_job,"cached job reason");
            check(!model.should_suppress(ss::pass_dynamic,0xB000,false),"later job runs after cached fallback");
            std::uintptr_t value=0;
            check(!model.redirect_shadow_param(ss::pass_forward_world,0xB000,value),"cached fallback stops redirects");
            check(model.stats().second_view_skipped_jobs==0,"cached fallback skipped nothing");
        }

        // Resize / view reallocation: a new frame never reuses the old view's captured resource.
        // The first post-resize frame has no capture and uses atlas-only; the next one captures
        // the new first view and redirects to it.
        {
            ss::Model model;
            model.configure(true,false);
            check(model.on_eye_begin(0,sig(0xA000)),"pre-resize first");
            first_view(model,0xA000,0x1000);
            check(model.should_suppress(ss::pass_prepare,0xA000,false)==false,"pre-resize first prepare ran");
            check(model.on_eye_begin(1,sig(0xB000)),"pre-resize second");
            check(model.should_suppress(ss::pass_world,0xB000,false),"pre-resize suppression");
            check(model.on_eye_begin(0,sig(0xA100)),"post-resize first");
            check(model.on_eye_begin(1,sig(0xB100)),"post-resize second");
            std::uintptr_t value=0;
            check(!model.redirect_shadow_param(ss::pass_forward_world,0xB100,value),"stale capture is not redirected");
            check(model.mode()==ss::Mode::atlas,"resize does not reuse stale captures");
            check(model.on_eye_begin(0,sig(0xA100)),"post-resize recapture first");
            first_view(model,0xA100,0x3000);
            check(model.on_eye_begin(1,sig(0xB100)),"post-resize recapture second");
            check(model.should_suppress(ss::pass_prepare,0xB100,false),"new capture suppresses prepare");
            check(model.mode()==ss::Mode::setup,"new capture selects setup");
            check(model.redirect_shadow_param(ss::pass_forward_world,0xB100,value) && value==0x301b,"new capture used after resize");
        }

        // Fallback reason clears on the next real setup-sharing frame.
        {
            ss::Model model;
            model.configure(true,false);
            check(model.on_eye_begin(0,sig(0xA000)) && model.on_eye_begin(1,sig(0xB000)),"reason-clear fallback frame");
            check(!model.should_suppress(ss::pass_prepare,0xB000,false),"reason-clear fallback prepare runs");
            check(model.reason()==ss::Reason::setup_unavailable,"reason-clear fallback reason");
            check(model.on_eye_begin(0,sig(0xA000)),"reason-clear next first");
            first_view(model,0xA000,0x1000);
            check(model.on_eye_begin(1,sig(0xB000)),"reason-clear next second");
            check(model.should_suppress(ss::pass_prepare,0xB000,false),"reason-clear next prepare suppressed");
            check(model.mode()==ss::Mode::setup,"reason-clear next mode");
            check(model.reason()==ss::Reason::none,"reason clears on the setup-sharing frame");
        }

        // v1009: the per-light shadow-map-info link (light+0x1a8).  GATHER_LIGHTS clears it for
        // every view and PREPARE sets it; TLFLIGHTCULL is the only consumer.  The first view's
        // TLFLIGHTCULL captures the links, the second view's writes them back only in setup mode.
        {
            ss::Model model;
            model.configure(true,false);
            check(model.on_eye_begin(0,sig(0xA000)),"light-link first eye");
            first_view(model,0xA000,0x1000);
            check(model.light_action(ss::pass_tlflightcull,0xA000)==ss::LightAction::capture,"first view captures light links");
            check(model.light_action(ss::pass_forward_world,0xA000)==ss::LightAction::none,"only TLFLIGHTCULL handles light links");
            check(model.light_action(ss::pass_tlflightcull,0)==ss::LightAction::none,"no view has no light action");
            model.capture_light(0x900,0x1900,6);
            model.capture_light(0x901,0x1901,2);
            model.capture_light(0x900,0x1910,6); // a repeated light updates its entry and count
            check(model.light_capture_count()==2,"two distinct lights captured");
            std::uintptr_t info=0;
            std::uint32_t records=0;
            check(model.find_light(0x900,info,records)&&info==0x1910&&records==6,"captured light is updated");
            check(model.find_light(0x901,info,records)&&info==0x1901&&records==2,"second captured light");
            check(!model.find_light(0x902,info,records),"uncaptured light is a miss");
            check(!model.find_light(0,info,records),"null light is a miss");
            model.capture_light(0x902,0,6);
            check(model.light_capture_count()==2,"null shadow entry is not captured");
            check(model.on_eye_begin(1,sig(0xB000)),"light-link second eye");
            check(model.should_suppress(ss::pass_prepare,0xB000,false),"light-link setup suppresses prepare");
            check(model.second_prepare_suppressed(),"light-link records that the second prepare was suppressed");
            check(model.mode()==ss::Mode::setup,"light-link mode setup");
            check(model.light_action(ss::pass_tlflightcull,0xB000)==ss::LightAction::restore,"second view restores light links");
            model.note_light_restored();
            model.note_light_restored();
            model.note_light_record_restored();
            model.note_light_miss();
            check(model.stats().light_captures==3,"light capture counter (two lights plus the update)");
            check(model.stats().light_restores==2 && model.stats().light_misses==1,"light restore/miss counters");
            check(model.stats().light_record_restores==1,"v1011 record-count restore counter");
            check(model.view_index(0xA000)==0 && model.view_index(0xB000)==1 && model.view_index(0xC000)==-1,
                "probe view index names both views");
            const auto frame_serial=model.frame_serial();
            check(frame_serial==1,"frame serial counts the shared frame");
            // Captures are per frame: the next frame starts empty and captures again.
            check(model.on_eye_begin(0,sig(0xA000)),"light-link next frame");
            check(model.frame_serial()==frame_serial+1,"frame serial advances once per frame");
            check(model.light_capture_count()==0,"light captures are per frame");
            check(model.light_action(ss::pass_tlflightcull,0xA000)==ss::LightAction::capture,"next frame captures again");
            check(model.on_eye_begin(1,sig(0xB000)),"light-link next second eye");
            check(model.light_action(ss::pass_tlflightcull,0xB000)==ss::LightAction::none,"no restore without a capture");
        }

        // Atlas mode runs the second view's PREPARE, which writes that view's own links, so
        // TLFLIGHTCULL must not be redirected there; a hard fallback refuses it too.
        {
            ss::Model atlas;
            atlas.configure(true,true);
            check(atlas.on_eye_begin(0,sig(0xA000)),"light atlas first eye");
            first_view(atlas,0xA000,0x1000);
            check(atlas.on_eye_begin(1,sig(0xB000)),"light atlas second eye");
            check(!atlas.should_suppress(ss::pass_prepare,0xB000,false),"light atlas prepare runs");
            check(atlas.mode()==ss::Mode::atlas,"light atlas mode");
            check(atlas.light_action(ss::pass_tlflightcull,0xB000)==ss::LightAction::none,"atlas mode does not restore light links");

            ss::Model fallback;
            fallback.configure(true,false);
            check(fallback.on_eye_begin(0,sig(0xA000)),"light fallback first eye");
            first_view(fallback,0xA000,0x1000);
            check(fallback.on_eye_begin(1,sig(0xB000)),"light fallback second eye");
            fallback.fail(ss::Reason::cached_job);
            check(fallback.light_action(ss::pass_tlflightcull,0xA000)==ss::LightAction::none,"hard fallback captures nothing");
            check(fallback.light_action(ss::pass_tlflightcull,0xB000)==ss::LightAction::none,"hard fallback restores nothing");
        }

        // A live skip mask that lets the second view's PREPARE run must not be overridden: its
        // PREPARE wrote that view's own links, so TLFLIGHTCULL keeps them.
        {
            ss::Model model;
            model.configure(true,false);
            model.set_skip_mask(8u); // A/B mask "w": skip only the atlas world writer
            check(model.on_eye_begin(0,sig(0xA000)),"mask-w first eye");
            first_view(model,0xA000,0x1000);
            check(model.on_eye_begin(1,sig(0xB000)),"mask-w second eye");
            check(!model.should_suppress(ss::pass_prepare,0xB000,false),"mask-w second prepare runs");
            check(!model.second_prepare_suppressed(),"mask-w does not claim the second prepare was suppressed");
            check(model.should_suppress(ss::pass_world,0xB000,false),"mask-w skips world");
            check(model.mode()==ss::Mode::setup,"mask-w mode setup");
            check(model.light_action(ss::pass_tlflightcull,0xB000)==ss::LightAction::none,
                "a second view that ran its own PREPARE keeps its own light links");
        }

        // Pass ids and callback RVAs used by the ownership write-up.
        {
            check(std::string(d2vr::render_passes[ss::pass_prepare].name)=="ARK_RENDERPASS_SHADOWS_PREPARE" &&
                  d2vr::render_passes[ss::pass_prepare].callback_rva==0x333550,"pass id 11 is SHADOWS_PREPARE");
            check(std::string(d2vr::render_passes[ss::pass_collect].name)=="ARK_RENDERPASS_COLLECT_DYNAMIC_CASTERS" &&
                  d2vr::render_passes[ss::pass_collect].callback_rva==0x330020,"pass id 12 is COLLECT_DYNAMIC_CASTERS");
            check(std::string(d2vr::render_passes[ss::pass_gather].name)=="ARK_RENDERPASS_GATHER_CASTERS" &&
                  d2vr::render_passes[ss::pass_gather].callback_rva==0x330c10,"pass id 13 is GATHER_CASTERS");
            check(std::string(d2vr::render_passes[ss::pass_world].name)=="ARK_RENDERPASS_SHADOWS_WORLD" &&
                  d2vr::render_passes[ss::pass_world].callback_rva==0x32f390,"pass id 20 is SHADOWS_WORLD");
            check(std::string(d2vr::render_passes[ss::pass_dynamic].name)=="ARK_RENDERPASS_SHADOWS_DYNAMIC" &&
                  d2vr::render_passes[ss::pass_dynamic].callback_rva==0x32f380,"pass id 21 is SHADOWS_DYNAMIC");
            check(std::string(d2vr::render_passes[25].name)=="ARK_RENDERPASS_SCREENSHADOWS" &&
                  d2vr::render_passes[25].callback_rva==0x32c7f0,"pass id 25 is SCREENSHADOWS and is not a writer");
            check(std::string(d2vr::render_passes[26].name)=="ARK_RENDERPASS_TLFLIGHTCULL" &&
                  d2vr::render_passes[26].callback_rva==0x2f7260,"pass id 26 is TLFLIGHTCULL");
            check(std::string(d2vr::render_passes[27].name)=="ARK_RENDERPASS_FORWARD_WORLD" &&
                  d2vr::render_passes[27].callback_rva==0x3133d0,"pass id 27 is FORWARD_WORLD");
            check(std::string(d2vr::render_passes[28].name)=="ARK_RENDERPASS_FORWARD_DYNAMIC" &&
                  d2vr::render_passes[28].callback_rva==0x3133b0,"pass id 28 is FORWARD_DYNAMIC");
            check(std::string(d2vr::render_passes[29].name)=="ARK_RENDERPASS_FOG" &&
                  d2vr::render_passes[29].callback_rva==0x309430,"pass id 29 is FOG");
            check(std::string(d2vr::render_passes[32].name)=="ARK_RENDERPASS_RENDER_WATER" &&
                  d2vr::render_passes[32].callback_rva==0x3045a0,"pass id 32 is RENDER_WATER");
            check(ss::shadow_reader_pass(25) && ss::shadow_reader_pass(27) && ss::shadow_reader_pass(28) &&
                  ss::shadow_reader_pass(29) && ss::shadow_reader_pass(32),"reader pass set");
            check(!ss::shadow_reader_pass(26) && !ss::shadow_reader_pass(11),"writer passes are not readers");
        }

        // Runtime status and the on_job path the hook uses.
        {
            d2vr::shadow_share::Runtime runtime;
            runtime.configure(false,false);
            std::string json=runtime.json();
            check(json.find("\"enabled\":false")!=std::string::npos,"status disabled");
            check(json.find("\"mode\":\"atlas\"")!=std::string::npos,"status mode key");
            check(json.find("\"setup_redirects\":0")!=std::string::npos,"status setup_redirects key");
            check(json.find("\"setup_fallbacks\":0")!=std::string::npos,"status setup_fallbacks key");
            check(json.find("\"reason\":\"disabled\"")!=std::string::npos,"status disabled reason");
            runtime.configure(true,false);
            runtime.set_installed(false);
            json=runtime.json();
            check(json.find("\"enabled\":false")!=std::string::npos,"refused install is not enabled");
            check(json.find("\"reason\":\"prologue_mismatch\"")!=std::string::npos,"refused install reason");
            runtime.configure(true,false);
            runtime.set_installed(true);
            runtime.on_eye_begin(0,0xA000,0,0,true);
            auto action=runtime.on_job(ss::pass_forward_world,0xA000,false,true,0x1111);
            check(!action.suppress && !action.redirect,"runtime first reader is observed only");
            action=runtime.on_job(ss::pass_prepare,0xA000,false,false,0);
            check(!action.suppress,"runtime first prepare runs");
            runtime.on_eye_begin(1,0xB000,0,0,true);
            action=runtime.on_job(ss::pass_prepare,0xB000,false,false,0);
            check(action.suppress,"runtime setup prepare suppressed");
            action=runtime.on_job(ss::pass_forward_world,0xB000,false,true,0x2222);
            check(action.redirect && action.redirect_value==0x1111,"runtime reader redirected");
            runtime.note_redirect_applied();
            json=runtime.json();
            check(json.find("\"mode\":\"setup\"")!=std::string::npos,"runtime mode setup");
            check(json.find("\"setup_redirects\":1")!=std::string::npos,"runtime setup redirect counted");
            check(json.find("\"frames_shared\":1")!=std::string::npos,"runtime frames_shared counted");
            check(json.find("\"light_captures\":0")!=std::string::npos,"runtime light_captures key");
            check(json.find("\"light_restores\":0")!=std::string::npos,"runtime light_restores key");
            check(json.find("\"light_misses\":0")!=std::string::npos,"runtime light_misses key");
            check(json.find("\"light_record_restores\":0")!=std::string::npos,"runtime light_record_restores key");
            check(json.find("\"probe\":false")!=std::string::npos,"runtime probe is off by default");
            check(!runtime.probe_enabled(),"runtime probe atomic is off by default");
            std::uint64_t probe_frame=0;int probe_view=-1;
            check(!runtime.probe_begin(0xB000,probe_frame,probe_view),"probe refuses when disabled");
            check(runtime.light_action(ss::pass_tlflightcull,0xB000)==ss::LightAction::restore,
                "runtime setup second-view TLFLIGHTCULL asks for the link restore");
            runtime.capture_light(0x900,0x1900,6);
            runtime.note_light_restored();
            runtime.note_light_record_restored();
            json=runtime.json();
            check(json.find("\"light_captures\":1")!=std::string::npos,"runtime light capture counted");
            check(json.find("\"light_restores\":1")!=std::string::npos,"runtime light restore counted");
            check(json.find("\"light_record_restores\":1")!=std::string::npos,"runtime record-count restore counted");
            runtime.note_light_miss();
            json=runtime.json();
            check(json.find("\"light_misses\":1")!=std::string::npos,"runtime light miss counted");
            runtime.stop();
            check(!runtime.enabled(),"runtime stop disables suppression");
        }

        // v1011 probe: opt-in, one JSON line per view, with the per-light record counts and
        // the shadowMapsInfos handle.  This is a file-format check only; it does not touch
        // engine memory (the hook is the only caller that does).
        {
            d2vr::shadow_share::Runtime runtime;
            runtime.configure(true,false);
            runtime.set_installed(true);
            runtime.on_eye_begin(0,0xA000,0,0,true);
            runtime.on_eye_begin(1,0xB000,0,0,true);
            runtime.configure_probe(true,"build\\shadow-share-probe-state-check.jsonl");
            std::uint64_t frame=0;int view=-1;
            check(runtime.probe_begin(0xA000,frame,view) && view==0,"probe admits the first view");
            check(runtime.probe_begin(0xB000,frame,view) && view==1,"probe admits the second view");
            ss::ProbeLight lights[2]{};
            lights[0].light=0x900;lights[0].info=0x1900;lights[0].records=6;lights[0].published=0;
            lights[1].light=0x901;lights[1].info=0;lights[1].records=0;lights[1].published=-1;
            const std::uint32_t words[2]={0x11223344u,0x55667788u};
            runtime.probe_write(1,frame,0x8800,0x9900,0x1234,lights,2,6,words,2);
            runtime.set_probe_enabled(false);
            std::ifstream probe("build\\shadow-share-probe-state-check.jsonl");
            std::string line;
            check(static_cast<bool>(std::getline(probe,line)),"probe wrote a line");
            check(line.find("\"view\":1")!=std::string::npos && line.find("\"frame\":1")!=std::string::npos,
                "probe line names the view and frame");
            check(line.find("\"lightList\":\"0x8800\"")!=std::string::npos &&
                  line.find("\"lightListData\":\"0x9900\"")!=std::string::npos,"probe line has the light list");
            check(line.find("\"shadowMapsInfos\":\"0x1234\"")!=std::string::npos,"probe line has the setup handle");
            check(line.find("\"records\":6,\"published\":0")!=std::string::npos,"probe line has the record count");
            check(line.find("\"records_published\":6")!=std::string::npos,"probe line has the published count");
            check(line.find("\"0x11223344\"")!=std::string::npos,"probe line has the first record words");
            probe.close();
            std::remove("build\\shadow-share-probe-state-check.jsonl");
        }

        // Atlas-only status.
        {
            d2vr::shadow_share::Runtime runtime;
            runtime.configure(true,true);
            runtime.set_installed(true);
            std::string json=runtime.json();
            check(json.find("\"atlas_only\":true")!=std::string::npos,"atlas-only status flag");
            check(json.find("\"mode\":\"atlas\"")!=std::string::npos,"atlas-only status mode");
        }

        // Reason strings are the status vocabulary.
        {
            check(std::string(ss::reason_name(ss::Reason::none))=="none","reason none string");
            check(std::string(ss::reason_name(ss::Reason::light_set_mismatch))=="light_set_mismatch","reason light string");
            check(std::string(ss::reason_name(ss::Reason::ordering))=="ordering","reason ordering string");
            check(std::string(ss::reason_name(ss::Reason::prologue))=="prologue_mismatch","reason prologue string");
            check(std::string(ss::reason_name(ss::Reason::setup_unavailable))=="setup_unavailable","reason setup string");
            check(std::string(ss::mode_name(ss::Mode::atlas))=="atlas" && std::string(ss::mode_name(ss::Mode::setup))=="setup","mode strings");
            check(ss::mutating_shadow_pass(11) && ss::mutating_shadow_pass(13) && ss::mutating_shadow_pass(21),"writer pass ids");
            check(!ss::mutating_shadow_pass(25) && !ss::mutating_shadow_pass(27),"screen/forward passes are not writers");
        }

        std::printf("PASS: shadow-share per-frame machine (off, atlas-only, setup pair with five writers skipped and five readers redirected, "
            "canonical redirect, setup fallback, single view, third view, writer ordering, cached job, resize, reason clear, status; "
            "v1009 TLFLIGHTCULL per-light link capture/restore and v1011 entry+0x18 record-count carry, probe off by default, "
            "atlas-mode and fallback exclusions)\n");
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what());
        return 1;
    }
}
