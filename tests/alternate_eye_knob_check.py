"""alternate-eye (plan F task 1): source-routing pins. With the knob off nothing changes (every new branch is gated on the knob / on
GameXr::alternate_enabled, which defaults to off); with it on the one engine view is the parity eye with that eye's own pose and FOV,
only that eye's swapchain is refreshed from it, the other eye is resubmitted from its kept image with its kept pose, and a disagreement
falls back to paired for the session. The engine pipeline is one frame deep (2026-10-22 double vision): a Setup is recorded for the Present
that SHOWS its image (alternate_display_frame), promoted into GameXr at that Present, and the plan is made for that display frame; the
Present-time view count (the next frame's Setup) never gates a frame. The pure logic is tests/alternate_eye_check.cpp; the timing and
images are a sim run, and tools/alt_pose_trace_check.py joins the alt-pose-trace.csv of one."""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
probe = (root / 'src' / 'dxgi_probe.cpp').read_text(encoding='utf-8')
xr = (root / 'src' / 'game_xr.hpp').read_text(encoding='utf-8')
build = (root / 'tools' / 'build-msvc.cmd').read_text(encoding='utf-8')
features = root / 'tools' / 'default_features.json'


def between(text, start, end):
    a = text.index(start)
    return text[a:text.index(end, a)]


# knob: read once in StartProbe before prepare_stereo, never a default feature, needs the mono-view prerequisites, turns mono on
assert probe.count('L"alternate-eye"') == 1
at = probe.index('L"alternate-eye"')
assert at < probe.index('            prepare_stereo();', at)
block = probe[at:at + 700]
assert 'alt_eye_state=1;mono_view_state=1;' in block and 'alt_eye_state=2;' in block and 'native_eye_capture_requested' in block
if features.exists():  # plan F task 4: a launcher row exists (enable kind, file alternate-eye); default OFF since v637 (headset v636: the eyes a frame apart doubled one eye on motion)
    import json
    rows = [f for f in json.loads(features.read_text(encoding='utf-8'))['features'] if f['file'] == 'alternate-eye']
    assert len(rows) == 1 and rows[0]['id'] == 'performance-mode' and rows[0]['kind'] == 'enable' and rows[0]['default'] is False
    assert 'selected anti-aliasing' in rows[0]['help'] and 'double' in rows[0]['label']
    assert any(f['id']=='alternate-native-taa' and f['file']=='no-alternate-native-taa' and f['kind']=='disable' and f['default'] for f in json.loads(features.read_text(encoding='utf-8'))['features'])

# GameXr: everything is off by default and gated
assert 'bool alternate_enabled{};' in xr and 'int alternate_fresh_eye{-1};' in xr
assert 'if(!alternate_enabled) return rendered_mask_==3;' in xr   # the paired rule is untouched with the knob off
assert xr.count('rendered_mask_==3') == 2                           # the helper and its comment: no other direct use remains
assert 'return alternate_enabled?build_layer_views(alt_effective_(),alternate_frame,pose_epoch()).views[eye]:rendered_views_[eye];' in xr
assert 'const bool alt_stale=alternate_enabled && alternate_fresh_eye>=0' in xr
assert 'if(alternate_enabled && !alt_stale && gameplay) {' in xr
assert '&& !alt_stale?antialiasing_->apply(sources[eye])' in xr and 'if(sharpness>0 && !alt_stale)' in xr
assert 'if(!alternate_enabled || alternate_fresh_eye<0) return true;' in xr
# a stale eye is drawn from the kept copy of ITS OWN image and its layer view is its own record
assert 'if(alt_stale) sources[eye]=alt_kept_[eye].Get();' in xr
eff = between(xr, 'std::array<EyeRecord<XrView>,2> alt_effective_() const {', 'bool tracked_pair_() const')
assert 'out[e]={rendered_views_[e],alternate_frame' in eff and 'alternate_fresh_eye<0 || eye==alternate_fresh_eye' in eff
keep = between(xr, 'void keep_alternate_eye_(', '// perf-mono-view diagnostic')
assert 'record={};' in keep and 'record={rendered_views_[eye],alternate_frame,pose_epoch(),true' in keep
assert '!(rendered_mask_&(1u<<eye))' in keep   # no recorded view for this image: nothing kept
assert 'build_layer_views(alt_effective_(),alternate_frame,pose_epoch()).ok' in xr   # age and epoch gate

# probe: the plan is only a request; the view count decides; the logical eye is the frame parity, never the raw begin index
head = between(probe, 'void prepare_head_view(void* view) noexcept {', 'void restore_head_view() noexcept')
assert 'const UINT64 alt_display=d2vr::alternate_display_frame(frames.load());' in head   # the Present that shows this Setup's image
assert 'alt_eye=d2vr::AlternateEye::parity_eye(alt_display)' in head and 'parity_eye(target)' not in head
assert 'const bool planned=(plan>>2)==alt_display;' in head
assert 'alternate_fallback("alternate-eye: the engine view count disagreed with the plan for 30 frames")' in head
assert '(*xr_views)[static_cast<std::size_t>(alt_eye)]' in head
# the view is recorded for its display frame, never written into GameXr at Setup (that would name it for the next Present)
assert 'if(alternate_on() && stereo_active.load()) { // alternate-eye: this view' in head
assert 'const int logical_eye=alt_eye>=0?alt_eye:eye;' in head
assert 'game_xr->rendered_view(static_cast<UINT>(logical_eye),{render_fov.left,render_fov.right,render_fov.up,render_fov.down},cant?&rendered_orientation:nullptr)' in head
assert 'alt_pending.record(alt_display,logical_eye,recorded,game_xr->pose_epoch(),game_xr->tracking_prediction_time(),game_xr->locate_cpu_time());' in head
assert 'record_rendered_eye(static_cast<UINT>(alt_eye)' not in head and 'alt_head_stamp' not in probe
assert 'raw_index' not in head
# the Present: this Present shows the Setup recorded FOR it; the view count the engine prepared since is the next frame's and never gates it
present_alt = between(probe, 'if(alternate_on()) {\n                                // alternate-eye: this Present shows', 'mono_path=alt_ready.single;')
assert 'alt_slot=alt_pending.take(number);' in present_alt and 'views.size()' not in present_alt
assert 'd2vr::alternate_present_readiness(alt_slot.has_value(),alt_slot?alt_slot->mask:0u,alt_slot?alt_slot->epoch:0,' in present_alt
assert 'mono_eye_frame.load()==number,paired_left_ready && native_eye_pair.complete(),alt_nonfree);' in present_alt
assert 'mono_path=alt_ready.single;pair_ready=alt_ready.ready;alt_fresh=alt_ready.fresh_eye;' in probe
assert 'if((alternate_on() || layer_pose_sync_on()) && pair_ready && alt_slot && game_xr)' in probe   # layer-pose sync promotes paired slots the same way
assert 'game_xr->record_rendered_view(slot_eye,alt_slot->view[slot_eye],alt_slot->display_time[slot_eye],alt_slot->locate_cpu_time[slot_eye]);' in probe
assert probe.count('alt_pending.record(') == 2 and probe.count('alt_pending.take(') == 3 and 'alt_pending.reset();' in probe   # + the paired layer-pose sync (own / latest slot)
# knob-off: the old readiness line is untouched (the alternate branch is an else-less if before it)
assert 'pair_ready=mono_path?(stereo_active.load() && views.size()==1 && mono_eye_frame.load()==number):' in probe
assert 'game_xr->alternate_enabled=alternate_on();game_xr->alternate_fresh_eye=alt_fresh;game_xr->alternate_frame=number;' in probe
assert probe.count('alternate_enabled') == 1 or 'game_xr->alternate_enabled' in probe

# plan + descriptor + fallback + status
after = between(probe, 'void alternate_after_present(', 'bool capture_texture(')
assert 'alt_plan.rendered(eye,number)' in after
assert 'const UINT64 next_display=d2vr::alternate_display_frame(number);' in after   # the plan is for the frame the next Setup SHOWS
assert 'alt_plan.eye_for_frame(next_display,free_gameplay,authored,epoch_changed)' in after and 'number+1' not in after
assert 'alt_plan_stamp=(next_display<<2)|static_cast<UINT64>(next+1);' in after
assert 'if(gameplay) alternate_select_descriptor(next<0);' in after
sel = between(probe, 'void alternate_select_descriptor(bool paired) noexcept {', 'void alternate_after_present(')
assert 'paired?(write_pointer() && write_separation()):(write_separation() && write_pointer())' in sel
fb = between(probe, 'void alternate_fallback(const char* reason) noexcept {', 'void alternate_select_descriptor(')
assert 'compare_exchange_strong(expected,2)' in fb and 'mono_view_fallback(reason)' in fb
assert 'alternate_fallback("alternate-eye: an eye image could not be kept")' in probe
assert '\\"alternate_eye\\":{\\"state\\":' in probe and '\\"alternated\\":" << alt_alternated.load()' in probe
assert '\\"paired\\":" << alt_paired.load()' in probe and '\\"fallbacks\\":" << alt_fallbacks.load()' in probe
assert 'alternate_eye_check.cpp' in build and 'alternate_eye_knob_check.py' in build and 'alt_pose_trace_check.py' in build
# GameXr: the promoted view is exactly the one the Setup wrote (pose, FOV, canted orientation, locate times)
assert 'XrView rendered_view(UINT eye,XrFovf rendered_fov,const XrQuaternionf* rendered_orientation=nullptr) const {' in xr
assert 'void record_rendered_view(UINT eye,const XrView& view,XrTime display_time,std::int64_t locate_cpu) {' in xr
assert 'rendered_views_[eye]=view;rendered_mask_|=1u<<eye;' in xr
# the pose trace is a diagnostic: read once in StartProbe; every eye mode (alternate, paired, perf-mono), never a behaviour change
assert probe.count('L"alt-pose-trace"') == 1 and 'alt_pose_trace_state=fs::exists(root/L"build"/L"alt-pose-trace")?1:0;' in probe
assert 'static bool alt_pose_trace_on() noexcept {return alt_pose_trace_state.load()==1;}' in probe
assert 'if(alt_pose_trace_on() && !layer_pose_sync_on() && !(alternate_on() && stereo_active.load()))' in head   # paired / mono Setup rows (knob-off record), after the record
# fix round 1: authored / settings are read at the Present that submits the frame, not from the lagging plan; every gate is the tested function
assert 'const bool alt_nonfree=alternate_on() && !d2vr::alternate_single_view_allowed(authored_camera,game_xr && game_xr->settings_open());' in probe
assert 'std::optional<d2vr::PendingViews<XrView>::Slot> alt_slot;int alt_fresh=-1;' in probe
assert 'alternate_after_present(number,gameplay,d2vr::alternate_free_gameplay(gameplay,game_xr->settings_open()),authored_camera,alt_fresh);' in probe
assert 'if(alt_mismatch_run.note(mismatch)) alternate_fallback("alternate-eye: the engine view count disagreed with the plan for 30 frames");' in head and 'if(mismatch && mono)' in head
assert 'return alternate_pair_ready(rendered_mask_,alternate_fresh_eye,' in xr
assert 'if(alternate_may_keep(gameplay,authored_camera,settings_.open())) keep_alternate_eye_(eye,processed);' in xr
assert 'alt_kept_ok_(authored_camera || settings_.open())' in xr and 'if(nonfree) return false;' in xr
assert 'if(!alternate_enabled && (alt_kept_[0] || alt_kept_[1])) {alt_kept_[0].Reset();alt_kept_[1].Reset();' in xr   # released after fallback
# Temporal modes require verified ownership; selected upscalers are enabled unless explicitly disabled.
# The effective mode must never replace the saved/requested preference.
assert 'taa_preference=d2vr::alternate_effective_temporal' not in probe
assert 'temporal_live_mode=d2vr::alternate_effective_temporal(alternate_on(),taa_preference,alternate_taa_requested,alternate_upscalers_requested);' in probe
assert 'd2vr::alternate_effective_temporal(alternate_on(),*requested_temporal,alternate_native_taa_allowed(),alternate_upscalers_requested)' in probe
assert 'alternate_upscalers_requested=!fs::exists(root/L"build"/L"no-alternate-upscalers");' in probe
assert 'alternate_smaa_applies(alternate_enabled,settings_.effective_temporal_aa())' in xr
assert 'const std::size_t eye_slot=d2vr::alternate_state_slot(alt_eye,eye);' in head
assert 'fsr_vfov[eye_slot]=' in head and 'jitter_counter[eye_slot]=counter;' in head and 'auto& e=eye_jitter[eye_slot];' in head
assert 'fsr_vfov[static_cast<std::size_t>(eye)]' not in head and 'jitter_counter[static_cast<std::size_t>(eye)]' not in head
print('PASS: alternate-eye knob routing (gating, own-eye pose, display-frame records, kept image, view-count truth, fallback, status)')

# the 180-frame miss counter counts only frames planned as one view; paired frames reset it (review double-vision I1)
assert 'mono_view_misses=d2vr::alternate_miss_count(static_cast<unsigned>(mono_view_misses.load()),alt_planned_single[number&1].load()!=0,pair_ready);' in probe
assert 'alt_planned_single[next_display&1]=next>=0?1:0;' in probe
