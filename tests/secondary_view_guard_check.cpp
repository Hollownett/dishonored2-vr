// v1246: secondary-view guard logic (src/secondary_view_guard.hpp). build\check-secondary-view.cmd
// v1257: per-eye lens logic and the shadow share's handling of a secondary pass (src/shadow_share.hpp Runtime over the pure Model).
#include "secondary_view_guard.hpp"
#include "shadow_share.hpp"
#include "weapon_draw_place.hpp"   // v1259: the per-eye placement census
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);std::exit(1);}} while(0)
using namespace d2vr::secondary_view;
namespace ss=d2vr::shadow_share;
static const int writer_passes[5]{ss::pass_prepare,ss::pass_collect,ss::pass_gather,ss::pass_world,ss::pass_dynamic};
static const int reader_passes[5]{ss::pass_screenshadows,ss::pass_forward_world,ss::pass_forward_dynamic,ss::pass_fog,ss::pass_render_water};
static ss::JobAction job(ss::Runtime& rt,int pass,std::uintptr_t view) {return rt.on_job(pass,view,false,ss::shadow_reader_pass(pass),0x5000+static_cast<std::uintptr_t>(pass));}
// Every shadow job of one view: true when none was suppressed, redirected or had its light links restored.
static bool view_jobs_untouched(ss::Runtime& rt,std::uintptr_t view) {
    bool clean=true;
    for(int p:writer_passes) clean=!job(rt,p,view).suppress && clean;
    clean=rt.light_action(ss::pass_tlflightcull,view)!=ss::LightAction::restore && clean;
    for(int p:reader_passes) {const auto a=job(rt,p,view);clean=!a.suppress && !a.redirect && clean;}
    return clean;
}
// The first eye: everything runs and its readers/lights are captured.
static void first_eye(ss::Runtime& rt,std::uintptr_t view) {
    rt.on_eye_begin(0,view,0,0,true);
    for(int p:writer_passes) CHECK(!job(rt,p,view).suppress);
    CHECK(rt.light_action(ss::pass_tlflightcull,view)==ss::LightAction::capture);
    for(int p:reader_passes) CHECK(!job(rt,p,view).suppress);
}
// The second eye reusing the first eye's shadows: all five writers skipped.
static bool second_eye_shares(ss::Runtime& rt,std::uintptr_t view) {
    bool all=true;for(int p:writer_passes) all=job(rt,p,view).suppress && all;return all;
}
int main() {
    // v1245 trace, Timepiece up: lens Begin of the eye-1 object first, then eye 0, then eye 1, then the null GUI view.
    const std::uintptr_t A=0x2580824802976,B=0x2580824851408;
    CHECK(classify(B,true,B,A,B)==Kind::secondary);
    CHECK(classify(A,false,B,A,B)==Kind::eye0);
    CHECK(classify(B,false,B,A,B)==Kind::eye1);
    CHECK(classify(0,false,B,A,B)==Kind::null_view);
    CHECK(classify(0x1234,false,B,A,B)==Kind::other);
    CHECK(classify(A,false,B,A,A)==Kind::other); // no distinct eye pair published: nothing is an eye
    CHECK(classify(A,true,A,A,B)==Kind::secondary); // a one-view frame lends slot 0
    // Timepiece camera: the engine camera's basis, 600 m down in z; an independent camera turns.
    const std::array<float,9> basis{-.890832f,.454332f,.00013f,-.454332f,-.890832f,-.00029f,0,-.0003f,1};
    auto turned=basis;turned[0]=.454332f;turned[1]=.890832f;
    CHECK(follows(basis,basis) && !follows(turned,basis));
    const auto t=translation({39.79f,-49.86f,-298.3f},{39.7732f,-49.8862f,301.7f},{39.8023f,-49.8292f,301.7f});
    CHECK(std::abs(t[2]+600.f)<.01f && std::abs(t[0])<.01f && std::abs(t[1])<.01f);
    // Replay gate.
    Record r;r.valid=true;r.view=B;r.prepared=0x100;r.allocation=0x200;r.frame=3503;
    const std::array<float,3> p{39.8f,-49.83f,301.7f};
    CHECK(replay_refusal(r,B,3504,0x200,p,p)==Refusal::none);
    CHECK(replay_refusal(r,A,3504,0x200,p,p)==Refusal::no_record);
    CHECK(replay_refusal(r,B,3505,0x200,p,p)==Refusal::stale);
    CHECK(replay_refusal(r,B,3504,0x300,p,p)==Refusal::allocation);
    CHECK(replay_refusal(r,B,3504,0x200,p,{39.8f,-49.83f,301.8f})==Refusal::prepared_moved);
    r.valid=false;CHECK(replay_refusal(r,B,3504,0x200,p,p)==Refusal::no_record);
    // History spans are disjoint and sum to the record size.
    std::size_t sum=0;for(const auto& s:history_spans) sum+=s.size;CHECK(sum==history_bytes);
    for(std::size_t i=0;i<history_spans.size();++i) for(std::size_t j=i+1;j<history_spans.size();++j) {
        const auto a=history_spans[i],b=history_spans[j];CHECK(a.offset+a.size<=b.offset || b.offset+b.size<=a.offset);}
    std::size_t v=0;for(const auto& s:verify_spans) v+=s.size;CHECK(v==sizeof(Record::verify));
    // v1248: the hook runs every frame while +0x88 stays set, but only frames with a lens Begin are lens passes; the hold expires 2 frames after.
    {std::uint64_t last=0;
     for(std::uint64_t f=100;f<110;++f) {last=lens_pass_frame(false,f,last);CHECK(!lens_paired_hold(last,f));}   // early returns only: never held
     last=lens_pass_frame(true,110,last);CHECK(last==110 && lens_paired_hold(last,110));
     for(std::uint64_t f=111;f<140;++f) {last=lens_pass_frame(false,f,last);CHECK(lens_paired_hold(last,f)==(f<=110+hold_frames));}   // expires
     last=lens_pass_frame(true,140,last);CHECK(lens_paired_hold(last,141) && !lens_paired_hold(last,143));}
    // v1256: the lens camera is the head's centre (the two eye poses' midpoint); a missing or non-finite eye pose keeps the other.
    {
        const auto c=centre({0.f,1.f,2.f},{.064f,1.4f,2.f});
        CHECK(std::abs(c[0]-.032f)<1e-6f && std::abs(c[1]-1.2f)<1e-6f && c[2]==2.f);
        const auto same=centre({.064f,0.f,0.f},{.064f,0.f,0.f});CHECK(same[0]==.064f);
        const auto bad=centre({1.f,2.f,3.f},{std::nanf(""),0.f,0.f});CHECK(bad[0]==1.f && bad[1]==2.f && bad[2]==3.f);
    }
    // v1257: per-eye lens. The pass for eye k is drawn with record k; the one-pass modes always use eye 0's window.
    CHECK(pass_record(LensMode::per_eye,0)==0 && pass_record(LensMode::per_eye,1)==1);
    CHECK(pass_record(LensMode::centre,2)==0 && pass_record(LensMode::eye0,1)==0 && pass_record(LensMode::per_eye,2)==0);
    CHECK(second_pass_refusal(LensMode::per_eye,true,true,true,true,true)==SecondRefusal::none);
    CHECK(second_pass_refusal(LensMode::centre,true,true,true,true,true)==SecondRefusal::mode);
    CHECK(second_pass_refusal(LensMode::eye0,true,true,true,true,true)==SecondRefusal::mode);
    CHECK(second_pass_refusal(LensMode::per_eye,true,false,true,true,true)==SecondRefusal::camera);    // an independent camera feed stays mono
    CHECK(second_pass_refusal(LensMode::per_eye,false,true,true,true,true)==SecondRefusal::camera);
    CHECK(second_pass_refusal(LensMode::per_eye,true,true,true,false,true)==SecondRefusal::record);
    CHECK(second_pass_refusal(LensMode::per_eye,true,true,true,true,false)==SecondRefusal::no_first);
    // Re-arm: the consumed image when the request slot is empty (nothing to put back); the game thread's newer request is used and kept.
    {const auto a=rearm(0,0x1111);CHECK(a.before==0x1111 && a.after==0);
     const auto b=rearm(0x2222,0x1111);CHECK(b.before==0x2222 && b.after==0x2222);}
    // Camera certificate: v1246 trace (render copy == written window to 7 digits); a re-orthonormalised basis passes, the other eye fails.
    {const std::array<float,3> cp{39.77324f,-49.8862f,-298.3025f};const std::array<float,9> cb{-.890832f,.454332f,.00013f,-.454332f,-.890832f,-.00029f,0,-.0003f,1};
     auto cb2=cb;cb2[0]+=2e-4f;CHECK(camera_matches(cp,cb,cp,cb) && camera_matches(cp,cb,cp,cb2));
     CHECK(!camera_matches(cp,cb,{39.80228f,-49.8292f,-298.3025f},cb));   // eye 1 is 6.4 cm away
     auto nan=cp;nan[1]=std::nanf("");CHECK(!camera_matches(cp,cb,nan,cb));}
    // v1257 shadows. Views A (eye 0) and B (eye 1); the Timepiece lens borrows B.
    {   // The v1256 root cause: the frame-start lens pass ran its jobs while the shadow share still held the previous frame's pair, so the
        // lens (view B) was the second eye: its five writers were skipped and its readers redirected to eye 0's setup.
        ss::Runtime rt;rt.configure(true,false);rt.set_installed(true);
        first_eye(rt,A);rt.on_eye_begin(1,B,0,0,true);CHECK(second_eye_shares(rt,B));
        CHECK(second_eye_shares(rt,B));   // lens pass of the next frame, no secondary notice: suppressed as eye 1
    }
    {   // v1257: frame-start lens pass isolated: everything it runs is its own; eye 1 then still reuses eye 0's shadows.
        ss::Runtime rt;rt.configure(true,false);rt.set_installed(true);
        first_eye(rt,A);rt.on_eye_begin(1,B,0,0,true);CHECK(second_eye_shares(rt,B));
        for(int frame=0;frame<3;++frame) {
            rt.on_secondary_begin(B,false,true);CHECK(view_jobs_untouched(rt,B));rt.on_secondary_end();
            first_eye(rt,A);rt.on_eye_begin(1,B,0,0,true);CHECK(second_eye_shares(rt,B));
        }
        const auto s=rt.stats();
        CHECK(s.secondary_prepare_jobs==3 && s.secondary_writer_jobs==15 && s.secondary_reader_jobs==15 && s.secondary_actions==0 && s.secondary_own_frames==0);
        CHECK(s.frames_shared==4 && s.fallbacks==0);
        // A lens pass that borrows eye 0's object (a one-view frame lends slot 0) is no first view either.
        rt.on_secondary_begin(A,false,true);CHECK(view_jobs_untouched(rt,A));rt.on_secondary_end();
        CHECK(rt.stats().secondary_actions==0);
    }
    {   // v1257 per eye: lens 0 at the frame start, eye 0, eye 1's Begin, lens 1 (view B) inside it, then eye 1's jobs. Lens 1 rewrote the
        // shared setup and atlas after eye 0, so eye 1 draws its own shadows that frame; the next frame without lens 1 shares again.
        ss::Runtime rt;rt.configure(true,false);rt.set_installed(true);
        rt.on_secondary_begin(B,false,true);CHECK(view_jobs_untouched(rt,B));rt.on_secondary_end();
        first_eye(rt,A);rt.on_eye_begin(1,B,0,0,true);
        rt.on_secondary_begin(B,true,true);CHECK(view_jobs_untouched(rt,B));rt.on_secondary_end();
        CHECK(view_jobs_untouched(rt,B));   // eye 1: own writers, no redirect, no restore
        auto s=rt.stats();
        CHECK(s.secondary_prepare_jobs==2 && s.secondary_actions==0 && s.secondary_own_frames==1 && s.frames_shared==0);
        CHECK(rt.json().find("\"reason\":\"secondary_view\"")!=std::string::npos);
        rt.on_secondary_begin(B,false,true);CHECK(view_jobs_untouched(rt,B));rt.on_secondary_end();
        first_eye(rt,A);rt.on_eye_begin(1,B,0,0,true);CHECK(second_eye_shares(rt,B));
        s=rt.stats();CHECK(s.frames_shared==1 && s.secondary_actions==0);
    }
    {   // build/lens-shared-shadows (isolate=false): the v1256 behaviour, now visible as actions on the lens's jobs.
        ss::Runtime rt;rt.configure(true,false);rt.set_installed(true);
        first_eye(rt,A);rt.on_eye_begin(1,B,0,0,true);CHECK(second_eye_shares(rt,B));
        rt.on_secondary_begin(B,false,false);CHECK(second_eye_shares(rt,B));rt.on_secondary_end();
        const auto s=rt.stats();CHECK(s.secondary_actions==5 && s.secondary_prepare_jobs==1 && s.secondary_own_frames==0);
        job(rt,ss::pass_prepare,B);CHECK(rt.stats().secondary_writer_jobs==5);   // outside the window nothing is counted
    }
    {   // Shadow share off: the secondary notice changes nothing and counts nothing.
        ss::Runtime rt;rt.configure(false,false);rt.set_installed(true);
        rt.on_secondary_begin(B,true,true);CHECK(view_jobs_untouched(rt,B));rt.on_secondary_end();
        CHECK(rt.stats().secondary_writer_jobs==0 && rt.stats().fallbacks==0);
    }
    {   // v1259 per-eye placement census: a render's placed draws by eye, split by whether the render had a lens pass; "one publish" = both eyes
        // placed from the same publication. v1257's failure (eye 1 never placed while the lens is open) shows as lens [.., eye0_only>0, ..].
        namespace wd=d2vr::weapon_draw;
        CHECK(wd::Place::eye_slot(16u|32u|9u)==0 && wd::Place::eye_slot(16u|1u)==1 && wd::Place::eye_slot(32u)==2 && wd::Place::eye_slot(9u)==3);
        std::array<float,15> raw{};raw[3]=raw[7]=raw[11]=1.f;raw[12]=raw[13]=raw[14]=1.f;   // origin 0, unit axes, unit scales
        const auto bound=wd::identity();
        const auto publish=[&](std::uint32_t frame,bool lens) {
            wd::Place::stage(0,wd::Place::kind_prop);wd::Place::render_secondary(lens);
            const std::array<const float*,2> rp{raw.data(),nullptr};const std::array<const wd::Mat*,2> bp{&bound,nullptr};
            wd::Place::publish_bound(frame,rp,bp,{.5,0.},{0x1234,0});
        };
        wd::Result e0;e0.side=0;e0.eye=0;wd::Result e1=e0;e1.eye=1;
        publish(100,true);wd::Place::census(e0,true,100);wd::Place::census(e1,true,100);    // lens render, both eyes, one publish
        publish(101,true);wd::Place::census(e0,true,101);                                   // lens render, eye 1 unplaced (v1257)
        publish(102,false);wd::Place::census(e0,false,102);wd::Place::census(e1,false,102); // plain render, both, one publish
        publish(103,false);wd::Place::census(e0,false,103);wd::Place::census(e1,false,102); // both, but eye 1 from another publish
        publish(104,false);                                                                 // rolls 103
        CHECK(wd::Place::eyes_json()=="{\"plain\":[2,0,0,1],\"lens\":[1,1,0,1]}");
        wd::Result miss;miss.side=-1;miss.near_side=0;wd::Place::census(miss,false,104);publish(105,false);   // nothing placed: no eye slot
        CHECK(wd::Place::eyes_json()=="{\"plain\":[2,0,0,1],\"lens\":[1,1,0,1]}");
    }
    std::printf("PASS secondary_view_guard\n");
    return 0;
}
