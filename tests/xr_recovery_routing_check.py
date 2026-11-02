"""Pin recovery ownership and base-transfer observation ordering."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
probe = (root / 'src/dxgi_probe.cpp').read_text()
xr = (root / 'src/game_xr.hpp').read_text()
assert 'void reset_alternate_session() noexcept {' in probe, 'XR recovery lacks alternate-session reset'
reset = probe.split('void reset_alternate_session() noexcept {', 1)[1].split('\n}', 1)[0]
for required in ('alt_pending.reset()', 'alt_plan.reset()', 'alt_plan_stamp=0', 'alt_epoch_seen=0',
                 'alt_per_frame_done=0', 'alt_descriptor_paired=false', 'mono_eye_frame=0',
                 'mono_view_misses=0', 'mono_view_unexpected=0', 'alt_mismatch_run.reset()',
                 'alt_planned_single[0]=0', 'alt_planned_single[1]=0'):
    assert required in reset, required
recovery = probe.split('void recover_xr_session(const char* reason) noexcept {', 1)[1].split('\nHRESULT WINAPI present_hook', 1)[0]
assert 'reset_alternate_session();' in recovery
creation = probe.split('game_xr=std::make_unique<d2vr::GameXr>', 1)[1]
assert creation.index('reset_alternate_session();') < creation.index('game_xr->wheel_native_pointer')
assert 'if(alternate_on()) alternate_select_descriptor(true);' in creation
assert 'alternate_miss_observation_allowed' in probe
assert 'if(alternate_on()) alternate_fallback("perf-mono-view: the one engine view was not matched for 180 gameplay frames")' in probe
copy = xr.index('} else context_->CopyResource(images_[eye].at(index).texture,submitted);')
observer = xr.index('observe_transfer(eye,submitted,images_[eye].at(index).texture')
overlay = xr.index('ModGpuScope overlay_scope(context_.Get(),ModSpan::eye_overlay)', copy)
assert copy < observer < overlay, 'Base transfer observer executes after overlays'
assert '\\"capture_stage\\":\\"base_before_overlays\\"' in probe
assert 'if(!equal && !native_widgets)' not in probe, 'Pre-overlay equality must not exempt widgets'
print('PASS: XR recovery resets alternate ownership, ignores cooldown misses, and validates base pixels before composition')
