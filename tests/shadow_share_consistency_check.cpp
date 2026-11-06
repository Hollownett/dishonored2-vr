// v1009/v1011 offline consistency lab: the second view's shadow-reading passes are redirected
// to the first view's captured shadow setup resource, while the second view's setup writers
// are suppressed.  v1009 models the per-light link that PREPARE writes and GATHER_LIGHTS
// clears: TLFLIGHTCULL publishes the per-light shadow records from it.  v1011 adds the second
// half of that state: the per-view job builder 0x351dec..0x351e4d runs before EVERY view and
// clears entry+0x18's record count, so the second view's TLFLIGHTCULL must see the first
// view's count as well as its link or it still publishes nothing (0x2f7646, 0x2f7ffc).
//
// The lab invents a per-view setup id and a per-pass "resource" id derived from it, feeds a
// scripted two-view frame through the real state machine, and checks that in setup mode every
// second-view reader sees the first view's resource (never a mixture of redirected and own),
// that the second-view TLFLIGHTCULL publishes the first view's per-light shadow records, that
// the atlas-only mode keeps each view's own setup, and that a refused pair falls back.
#include "../src/shadow_share_model.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace ss = d2vr::shadow_share;

static void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}

static std::uint64_t mix(std::uint64_t value,std::uint64_t part) noexcept {
    value ^= part + 0x9e3779b97f4a7c15ull + (value << 6) + (value >> 2);
    value *= 0xbf58476d1ce4e5b9ull;
    return value ^ (value >> 31);
}

static ss::ViewSignature sig(std::uintptr_t view,std::uint32_t gate=0,std::uint32_t flags=0) {
    ss::ViewSignature result;result.view=view;result.gate=gate;result.flags=flags;result.valid=view!=0;return result;
}

static const std::array<int,5> writers{ss::pass_prepare,ss::pass_collect,ss::pass_gather,ss::pass_world,ss::pass_dynamic};
static const std::array<int,5> readers{ss::pass_screenshadows,ss::pass_forward_world,ss::pass_forward_dynamic,
    ss::pass_fog,ss::pass_render_water};
static constexpr unsigned lab_light_count=4;
static constexpr std::uintptr_t lab_light_base=0x900;
static constexpr std::uint32_t lab_records=6; // six atlas faces in one arkShadowMapInfo_t

struct Frame {
    ss::Model& model;
    unsigned frame;
    std::uintptr_t view[2]{};
    std::uint64_t setup[2]{};       // per-view setup id, produced by that view's non-skipped writers
    std::uint64_t own_reader[2][ss::pass_slots]{}; // what the view's own setup would give a reader
    std::uint64_t read[2][ss::pass_slots]{};       // what the reader actually samples
    bool redirected[2][ss::pass_slots]{};
    unsigned writer_runs[2]{};
    unsigned skipped[2]{};
    // v1009/v1011: the engine's per-light link (light+0x1a8), the entry's record count
    // (entry+0x18, six records) and the per-view shadow blob TLFLIGHTCULL publishes.
    std::uintptr_t link[lab_light_count]{};        // current light+0x1a8 values (shared light nodes)
    std::uint32_t records[lab_light_count]{};      // current entry+0x18 counts (shared entries)
    std::uintptr_t saved_link[lab_light_count]{};  // values the restore has to put back
    std::uint32_t saved_records[lab_light_count]{};
    std::uint64_t shadow_blob[2]{};                // what TLFLIGHTCULL publishes: eye0/eye1
    std::uint64_t before_restore_blob[2]{};        // second-view blob without the carry
    unsigned light_captures{};
    unsigned light_restores{};
    unsigned light_record_restores{};
    bool carry_records{true};                      // false = the v1009 link-only counterfactual

    Frame(ss::Model& m,unsigned f):model(m),frame(f) {}

    // The per-view job builder 0x351780 runs before every view's passes and clears the shared
    // pool's record counts (0x351dec..0x351e4d) and the published base (0x351e50).
    void build_jobs() noexcept {
        for(auto& value:records) value=0;
    }
    void writer(int eye,int pass) noexcept {
        ++writer_runs[eye];
        setup[eye]=mix(mix(setup[eye]^view[eye],static_cast<std::uint64_t>(pass)),frame);
        if(pass==ss::pass_prepare) {
            // PREPARE links each light to this frame's shadow-map-info entry (0x3328ac) and
            // writes the entry's six-record count (0x332f7d..0x332f94).
            for(unsigned i=0;i<lab_light_count;++i) {
                link[i]=mix(0x700000u+eye*0x100u+i,frame);
                records[i]=lab_records;
            }
        }
    }
    static std::uint64_t blob(const std::uintptr_t* links,const std::uint32_t* counts) noexcept {
        std::uint64_t value=0x51ed;
        for(unsigned i=0;i<lab_light_count;++i) {
            // TLFLIGHTCULL copies a light's records only when its link is non-null and the
            // entry count is non-zero (0x2f7641/0x2f7646).
            value=mix(value,counts[i]?links[i]:0);
            value=mix(value,counts[i]);
        }
        return value;
    }
    // Pass 6 runs for both views and clears every light's shadow-map-info link (0x31c074).
    void gather_lights() noexcept {for(auto& value:link) value=0;}
    // Pass 26 runs for both views: publish the per-light shadow records, and on the second view
    // apply/publish/undo the captured first-view links and counts exactly as the hook does.
    void tlflightcull(int eye,std::uintptr_t identity) noexcept {
        const auto action=model.light_action(ss::pass_tlflightcull,identity);
        before_restore_blob[eye]=blob(link,records);
        if(action==ss::LightAction::capture) {
            for(unsigned i=0;i<lab_light_count;++i) if(link[i]) {
                model.capture_light(lab_light_base+i,link[i],records[i]);
                ++light_captures;
            }
        } else if(action==ss::LightAction::restore) {
            for(unsigned i=0;i<lab_light_count;++i) {
                saved_link[i]=link[i];
                saved_records[i]=records[i];
                std::uintptr_t captured=0;
                std::uint32_t captured_records=0;
                if(model.find_light(lab_light_base+i,captured,captured_records)) {
                    if(link[i]!=captured) {
                        link[i]=captured;
                        ++light_restores;
                        model.note_light_restored(); // the hook wrote the light+0x1a8 link
                    }
                    if(carry_records && records[i]!=captured_records) {
                        records[i]=captured_records;
                        ++light_record_restores;
                        model.note_light_record_restored(); // v1011: entry+0x18 count
                    }
                } else model.note_light_miss();
            }
        }
        shadow_blob[eye]=blob(link,records);
        if(action==ss::LightAction::restore)
            for(unsigned i=0;i<lab_light_count;++i) {link[i]=saved_link[i];records[i]=saved_records[i];}
    }
    void run_eye(int eye,std::uintptr_t identity,bool observe_readers) noexcept {
        view[eye]=identity;
        build_jobs();    // the view's job builder resets the shared entries' counts
        gather_lights(); // pass 6 runs for every view and clears light+0x1a8
        for(const int pass:writers) {
            if(model.should_suppress(pass,identity,false)) {++skipped[eye];continue;}
            writer(eye,pass);
        }
        tlflightcull(eye,identity); // pass 26
        for(const int pass:readers) {
            own_reader[eye][pass]=setup[eye]?mix(setup[eye],static_cast<std::uint64_t>(pass)):0;
            if(eye==0 && observe_readers) model.observe_shadow_param(pass,identity,true,own_reader[eye][pass]);
            std::uintptr_t value=0;
            redirected[eye][pass]=model.redirect_shadow_param(pass,identity,value);
            read[eye][pass]=redirected[eye][pass]?value:own_reader[eye][pass];
            if(redirected[eye][pass]) model.note_redirect_applied(); // the hook writes the table entry
        }
    }
    bool any_redirected(int eye) const noexcept {
        for(const int pass:readers) if(redirected[eye][pass]) return true;
        return false;
    }
    bool all_redirected(int eye) const noexcept {
        for(const int pass:readers) if(!redirected[eye][pass]) return false;
        return true;
    }
};

int main() {
    try {
        // Frame 1: clean setup-sharing pair.  Eye 1 skips all five writers and every reader sees
        // eye 0's resource; no reader is left on its own setup and no writer runs between.
        {
            ss::Model model;Frame frame(model,1);
            model.configure(true,false);
            check(model.on_eye_begin(0,sig(0xA000)),"frame 1 eye 0 begins");
            frame.run_eye(0,0xA000,true);
            check(model.on_eye_begin(1,sig(0xB000)),"frame 1 eye 1 begins");
            frame.run_eye(1,0xB000,true);
            check(model.mode()==ss::Mode::setup,"frame 1 selects setup mode");
            check(frame.skipped[0]==0 && frame.skipped[1]==5,"eye 1 skipped exactly the five shadow writers");
            check(frame.writer_runs[1]==0,"eye 1 ran no shadow writer");
            check(frame.all_redirected(1),"every eye-1 reader was redirected (no mixed setup)");
            for(const int pass:readers) check(frame.read[1][pass]==frame.own_reader[0][pass],"eye 1 reads eye 0's setup resource");
            check(frame.own_reader[1][ss::pass_forward_world]==0,"eye 1 never built its own setup this frame");
            check(model.stats().frames_shared==1 && model.stats().second_view_skipped_jobs==5,"frame 1 counters");
            check(model.stats().setup_redirects==5 && model.stats().setup_fallbacks==0,"frame 1 redirect counters");
            check(frame.light_captures==lab_light_count,"frame 1 captured every light's shadow-map-info link");
            check(frame.light_restores==lab_light_count,"frame 1 restored every light's link for the second view");
            check(frame.light_record_restores==lab_light_count,"frame 1 restored every entry's record count");
            check(frame.shadow_blob[1]==frame.shadow_blob[0],"both eyes publish identical per-light shadow records");
            check(frame.before_restore_blob[1]!=frame.shadow_blob[0],
                "without the link+count carry the second view would publish empty local-shadow records");
            check(model.stats().light_captures==lab_light_count && model.stats().light_restores==lab_light_count &&
                  model.stats().light_record_restores==lab_light_count,"frame 1 light counters");
            std::printf("  frame 1 setup shared: eye0 setup %016llx, five writers skipped, five readers redirected, "
                "%u light links and %u record counts restored, local-shadow blob %016llx\n",
                static_cast<unsigned long long>(frame.setup[0]),frame.light_restores,frame.light_record_restores,
                static_cast<unsigned long long>(frame.shadow_blob[0]));
        }

        // v1011 counterfactual: the v1009 link-only carry.  The second view's job builder has
        // already zeroed the shared entries' record counts, so restoring only light+0x1a8
        // publishes an empty blob even though every link is correct.  This is the v1010
        // headset failure modelled directly.
        {
            ss::Model model;Frame frame(model,1);
            model.configure(true,false);
            check(model.on_eye_begin(0,sig(0xA000)),"counterfactual eye 0 begins");
            frame.run_eye(0,0xA000,true);
            check(model.on_eye_begin(1,sig(0xB000)),"counterfactual eye 1 begins");
            frame.carry_records=false;
            frame.run_eye(1,0xB000,true);
            check(frame.light_restores==lab_light_count,"counterfactual still restores every link");
            check(frame.light_record_restores==0,"counterfactual carries no record counts");
            check(frame.shadow_blob[1]!=frame.shadow_blob[0],
                "v1009 link-only carry leaves every entry count zero and publishes no local-shadow records");
            check(frame.shadow_blob[1]==frame.before_restore_blob[1],
                "the link-only restore does not change what TLFLIGHTCULL can publish");
            std::printf("  v1009 counterfactual: %u links restored, 0 counts, local-shadow blob %016llx (eye0 %016llx)\n",
                frame.light_restores,static_cast<unsigned long long>(frame.shadow_blob[1]),
                static_cast<unsigned long long>(frame.shadow_blob[0]));
        }

        // Setup mode with one uncaptured reader (eye 0 never ran RENDER_WATER): the canonical
        // first-view resource is used, so the frame is still not mixed.
        {
            ss::Model model;Frame frame(model,2);
            model.configure(true,false);
            check(model.on_eye_begin(0,sig(0xA000)),"canonical eye 0 begins");
            frame.view[0]=0xA000;
            for(const int pass:writers) {
                check(!model.should_suppress(pass,0xA000,false),"canonical eye 0 writer");
                frame.writer(0,pass);
            }
            // observe only four of the five readers
            static const std::array<int,4> observed{ss::pass_screenshadows,ss::pass_forward_world,
                ss::pass_forward_dynamic,ss::pass_fog};
            for(const int pass:observed) {
                const std::uint64_t own=mix(frame.setup[0],static_cast<std::uint64_t>(pass));
                model.observe_shadow_param(pass,0xA000,true,own);
            }
            check(model.on_eye_begin(1,sig(0xB000)),"canonical eye 1 begins");
            for(const int pass:writers) check(model.should_suppress(pass,0xB000,false),"canonical writers skipped");
            check(model.mode()==ss::Mode::setup,"canonical mode setup");
            std::uintptr_t canonical=0;
            check(model.redirect_shadow_param(ss::pass_render_water,0xB000,canonical),"uncaptured reader is redirected");
            check(canonical==mix(frame.setup[0],static_cast<std::uint64_t>(ss::pass_screenshadows)),
                "uncaptured reader uses the canonical first-view resource");
        }

        // Frame 3: atlas-only mode.  Eye 1 runs its own setup, only the two atlas writers are
        // skipped, and each eye samples its own resource (the v1005 baseline).
        {
            ss::Model model;Frame frame(model,3);
            model.configure(true,true);
            check(model.on_eye_begin(0,sig(0xA000)),"atlas frame eye 0 begins");
            frame.run_eye(0,0xA000,true);
            check(model.on_eye_begin(1,sig(0xB000)),"atlas frame eye 1 begins");
            frame.run_eye(1,0xB000,true);
            check(frame.skipped[1]==2 && frame.writer_runs[1]==3,"atlas mode runs the three setup writers");
            check(!frame.any_redirected(1),"atlas mode redirects no reader");
            check(frame.read[1][ss::pass_forward_world]==frame.own_reader[1][ss::pass_forward_world],"atlas mode eye 1 reads its own setup");
            check(frame.read[1][ss::pass_forward_world]!=frame.own_reader[0][ss::pass_forward_world],"atlas mode eye setup differs");
            check(frame.light_restores==0 && frame.shadow_blob[1]!=frame.shadow_blob[0],
                "atlas mode keeps the second view's own per-light shadow records (the v1005 baseline)");
        }

        // Frame 4: gate mismatch refuses the pair.  Nothing is skipped or redirected and the two
        // views keep their own setup.
        {
            ss::Model model;Frame frame(model,4);
            model.configure(true,false);
            check(model.on_eye_begin(0,sig(0xA000,0,0)),"fallback eye 0 begins");
            frame.run_eye(0,0xA000,true);
            check(!model.on_eye_begin(1,sig(0xB000,2,0)),"fallback pair refused");
            frame.run_eye(1,0xB000,true);
            check(frame.skipped[0]==0 && frame.skipped[1]==0,"fallback skips no writer");
            check(!frame.any_redirected(1),"fallback redirects no reader");
            check(frame.read[1][ss::pass_forward_world]!=frame.read[0][ss::pass_forward_world],"fallback keeps per-eye setup");
            check(model.reason()==ss::Reason::light_set_mismatch && model.stats().fallbacks==1,"fallback reason and counter");
        }

        // Frame 5: no first-view capture at all.  Atlas-only fallback (the v1005 behaviour) and
        // the reason is setup_unavailable, not a hard view fallback.
        {
            ss::Model model;Frame frame(model,5);
            model.configure(true,false);
            check(model.on_eye_begin(0,sig(0xA000)),"no-capture eye 0 begins");
            frame.run_eye(0,0xA000,false);
            check(model.on_eye_begin(1,sig(0xB000)),"no-capture eye 1 begins");
            frame.run_eye(1,0xB000,true);
            check(model.mode()==ss::Mode::atlas,"no-capture selects atlas");
            check(frame.skipped[1]==2 && frame.writer_runs[1]==3,"no-capture keeps the v1005 writer split");
            check(!frame.any_redirected(1),"no-capture does not redirect");
            check(model.reason()==ss::Reason::setup_unavailable && model.stats().setup_fallbacks==1,"no-capture reason");
        }

        // Frame 6: third view after a suppressed second-view prepare.  The partial frame is
        // counted, the third view runs, and no further redirect is emitted for the old second view.
        {
            ss::Model model;
            model.configure(true,false);
            check(model.on_eye_begin(0,sig(0xA000)),"third-view first eye");
            check(!model.should_suppress(ss::pass_prepare,0xA000,false),"third-view first prepare");
            model.observe_shadow_param(ss::pass_forward_world,0xA000,true,0x101b);
            check(model.on_eye_begin(1,sig(0xB000)),"third-view pair");
            check(model.should_suppress(ss::pass_prepare,0xB000,false),"third-view prepare suppressed first");
            check(!model.on_eye_begin(2,sig(0xC000)),"third view refused");
            std::uintptr_t value=0;
            check(!model.redirect_shadow_param(ss::pass_forward_world,0xB000,value),"third view stops redirects");
            check(model.reason()==ss::Reason::third_view && model.stats().fallbacks==1,"third-view reason and fallback");
        }

        // Resize: a new frame with new view identities must not redirect to the old capture.
        {
            ss::Model model;Frame frame(model,7);
            model.configure(true,false);
            check(model.on_eye_begin(0,sig(0xA000)),"resize old eye 0");
            frame.run_eye(0,0xA000,true);
            check(model.on_eye_begin(1,sig(0xB000)),"resize old eye 1");
            frame.run_eye(1,0xB000,true);
            check(frame.all_redirected(1),"resize old frame redirected");

            Frame resized(model,8);
            check(model.on_eye_begin(0,sig(0xA100)),"resize new eye 0");
            resized.run_eye(0,0xA100,false);
            check(model.on_eye_begin(1,sig(0xB100)),"resize new eye 1");
            resized.run_eye(1,0xB100,true);
            check(model.mode()==ss::Mode::atlas,"resize new frame has no stale capture");
            check(!resized.any_redirected(1),"resize new frame redirects nothing");
        }

        // Frame 9: a clean setup pair after a setup-unavailable frame clears the reason.
        {
            ss::Model model;Frame frame(model,9);
            model.configure(true,false);
            check(model.on_eye_begin(0,sig(0xA000)) && model.on_eye_begin(1,sig(0xB000)),"reason frame 1");
            check(!model.should_suppress(ss::pass_prepare,0xB000,false),"reason frame 1 prepare runs");
            check(model.reason()==ss::Reason::setup_unavailable,"reason frame 1 reason");
            Frame good(model,10);
            check(model.on_eye_begin(0,sig(0xA000)),"reason frame 2 eye 0");
            good.view[0]=0xA000;
            for(const int pass:writers) {
                check(!model.should_suppress(pass,0xA000,false),"reason frame 2 writer");
                good.writer(0,pass);
            }
            for(const int pass:readers) model.observe_shadow_param(pass,0xA000,true,mix(good.setup[0],static_cast<std::uint64_t>(pass)));
            check(model.on_eye_begin(1,sig(0xB000)),"reason frame 2 eye 1");
            check(model.should_suppress(ss::pass_prepare,0xB000,false),"reason frame 2 prepare suppressed");
            check(model.mode()==ss::Mode::setup,"reason frame 2 setup");
            check(model.reason()==ss::Reason::none,"reason clears on a setup-sharing frame");
        }

        std::printf("PASS: recorded per-view setup resource is identical for every second-view reader in setup frames, "
            "no mixed redirect, no writer between the views; the second view restores the first view's per-light "
            "shadow-map-info links AND entry+0x18 record counts (v1011) so its TLFLIGHTCULL publishes identical "
            "local-shadow records after the per-view job builder reset; the v1009 link-only counterfactual publishes "
            "nothing; atlas-only frames keep per-eye setup and per-eye links; resize and third-view partial frames "
            "are counted\n");
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what());
        return 1;
    }
}
