"""Layer-pose sync for the paired / perf-mono paths (2026-10-22): source pins. The engine pipeline is one frame deep (a Setup made while
the Present counter reads F is on screen at Present F+2), and the knob-off paired path recorded a Setup's views into GameXr at Setup time,
so every Present submitted the NEXT Setup's pose with its image (the one-frame pose-versus-image skew the sim trace showed on every
distinguishable Present). With sync (default on) the paired Setup's views wait in the same PendingViews alternate mode uses and are promoted
at the Present that shows their image; knob build/no-layer-pose-sync (launcher row layer-pose-sync, kind disable) restores the Setup-time
record, line for line. The pure logic is tests/layer_pose_sync_check.cpp; the real timing is a sim run joined by tools/alt_pose_trace_check.py."""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
probe = (root / 'src' / 'dxgi_probe.cpp').read_text(encoding='utf-8')
xr = (root / 'src' / 'game_xr.hpp').read_text(encoding='utf-8')
header = (root / 'src' / 'alternate_eye.hpp').read_text(encoding='utf-8')
build = (root / 'tools' / 'build-msvc.cmd').read_text(encoding='utf-8')
features = json.loads((root / 'tools' / 'default_features.json').read_text(encoding='utf-8'))['features']


def between(text, start, end):
    a = text.index(start)
    return text[a:text.index(end, a)]


# the knob: read once in StartProbe (absent = sync on), counters reset there; a launcher row of kind disable, on by default
assert probe.count('L"no-layer-pose-sync"') == 1
assert 'layer_pose_sync_state=fs::exists(root/L"build"/L"no-layer-pose-sync")?0:1;layer_pose_synced=0;layer_pose_sync_fallbacks=0;' in probe
assert 'static std::atomic<int> layer_pose_sync_state{1};' in probe
assert 'static bool layer_pose_sync_on() noexcept {return layer_pose_sync_state.load()==1;}' in probe
rows = [f for f in features if f['file'] == 'no-layer-pose-sync']
assert len(rows) == 1 and rows[0]['id'] == 'layer-pose-sync' and rows[0]['kind'] == 'disable' and rows[0]['default'] is True, rows

# the Setup: with sync on, the paired / mono views are recorded for the display frame (alt_display = F+2), never into GameXr; the knob-off
# branches are the old lines, untouched, and the alternate-eye branch comes first
head = between(probe, 'void prepare_head_view(void* view) noexcept {', 'void restore_head_view() noexcept')
assert 'const UINT64 alt_display=d2vr::alternate_display_frame(frames.load());' in head
alt_at = head.index('if(alternate_on() && stereo_active.load()) { // alternate-eye: this view')
sync_at = head.index('} else if(layer_pose_sync_on()) { // paired / perf-mono, layer-pose sync')
mono_at = head.index('} else if(mono) { // the one image serves both projection layers')
assert alt_at < sync_at < mono_at
sync = head[sync_at:mono_at]
assert 'for(UINT sync_eye=mono?0u:static_cast<UINT>(eye);sync_eye<=(mono?1u:static_cast<UINT>(eye));++sync_eye) {' in sync
assert 'XrView recorded=game_xr->rendered_view(sync_eye,fov,(mono || cant)?&rendered_orientation:nullptr);' in sync
assert 'if(mono) recorded.pose.position=xr_eye.pose.position;' in sync          # the perf-mono centre, as record_rendered_eye recorded it
assert 'alt_pending.record(alt_display,static_cast<int>(sync_eye),recorded,game_xr->pose_epoch(),game_xr->tracking_prediction_time(),game_xr->locate_cpu_time());' in sync
assert '->record_rendered_eye(' not in sync and '->record_rendered_view(' not in sync   # nothing goes into GameXr at Setup
legacy = head[mono_at:head.index('} catch(const std::exception& error)', mono_at)]
assert 'game_xr->record_rendered_eye(0,fov,&rendered_orientation,&xr_eye.pose.position);' in legacy
assert 'game_xr->record_rendered_eye(1,fov,&rendered_orientation,&xr_eye.pose.position);' in legacy
assert '} else game_xr->record_rendered_eye(static_cast<UINT>(eye),\n            {render_fov.left,render_fov.right,render_fov.up,render_fov.down},cant?&rendered_orientation:nullptr);' in legacy
assert 'alt_pending' not in legacy.replace('alt_pose_trace_setup', '')

# the Present: the knob-off readiness line is unchanged; with sync on and the pair ready, the slot recorded for THIS Present is promoted,
# else the latest Setup's (the legacy submission, counted), else nothing; alternate mode's branch is untouched and comes first
present = between(probe, 'if(alternate_on()) {\n                                // alternate-eye: this Present shows', 'if(pair_ready && native_eye_capture_requested && !mono_path) ++native_eye_pairs;')
assert 'pair_ready=mono_path?(stereo_active.load() && views.size()==1 && mono_eye_frame.load()==number):' in present
assert 'if(layer_pose_sync_on() && pair_ready) {' in present
assert 'const auto own_slot=alt_pending.take(number),latest_slot=alt_pending.take(number+1);' in present
assert 'const auto choice=d2vr::paired_slot_to_promote(own_slot.has_value(),own_slot?own_slot->mask:0u,own_slot?own_slot->epoch:0,' in present
assert 'latest_slot.has_value(),latest_slot?latest_slot->mask:0u,latest_slot?latest_slot->epoch:0,game_xr?game_xr->pose_epoch():0);' in present
assert 'if(choice==d2vr::PairedSlot::own) {alt_slot=own_slot;++layer_pose_synced;}' in present
assert 'else if(choice==d2vr::PairedSlot::latest) {alt_slot=latest_slot;++layer_pose_sync_fallbacks;}' in present
assert present.index('mono_path=alt_ready.single;') < present.index('} else {')
assert 'if((alternate_on() || layer_pose_sync_on()) && pair_ready && alt_slot && game_xr)' in probe
assert 'game_xr->record_rendered_view(slot_eye,alt_slot->view[slot_eye],alt_slot->display_time[slot_eye],alt_slot->locate_cpu_time[slot_eye]);' in probe
assert probe.count('alt_pending.take(') == 3 and probe.count('alt_pending.record(') == 2
# GameXr: the paired tracked-pair rule and the layer views are what they were (rendered_views_, promoted or recorded)
assert 'if(!alternate_enabled) return rendered_mask_==3;' in xr
assert 'return alternate_enabled?build_layer_views(alt_effective_(),alternate_frame,pose_epoch()).views[eye]:rendered_views_[eye];' in xr
# status, pure helpers, trace, tests
assert '\\"layer_pose_sync\\":{\\"enabled\\":" << (layer_pose_sync_on()?"true":"false") << ",\\"synced\\":" << layer_pose_synced.load() << ",\\"fallbacks\\":" << layer_pose_sync_fallbacks.load()' in probe
assert 'inline bool paired_views_promotable(bool have_slot, unsigned mask, std::uint64_t slot_epoch, std::uint64_t epoch) noexcept {' in header
assert 'return have_slot && mask == 3u && slot_epoch == epoch;' in header
assert 'enum class PairedSlot {none, own, latest};' in header and 'inline PairedSlot paired_slot_to_promote(' in header
assert 'static bool alt_pose_trace_on() noexcept {return alt_pose_trace_state.load()==1;}' in probe   # the trace sees every mode
assert 'if(alt_pose_trace_on() && !layer_pose_sync_on() && !(alternate_on() && stereo_active.load()))' in head   # the knob-off record is traced too
assert 'layer_pose_sync_check.cpp' in build and 'layer_pose_sync_routing_check.py' in build
print('PASS: layer-pose sync routing (knob, display-frame record, own-then-latest promotion, legacy lines untouched, status, tests)')
